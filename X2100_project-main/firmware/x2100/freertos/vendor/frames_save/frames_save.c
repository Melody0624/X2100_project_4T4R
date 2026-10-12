#include <dfs_posix.h>
#include <os.h>
#include <cpu/cpu.h>
#include "frames_save.h"
#include "memory_pool.h"
#include "general_functions.h"
// #include <os/freertos/include/semphr.h>
// #include <os/freertos/include/FreeRTOS.h>

#define MAX_FSYNC_INTERVAL  5

/* 帧索引表初始容量（每帧一条记录），不足时自动 2 倍扩容 */
#define FRAME_INDEX_INIT_CAP  256

extern volatile int adc_finished;
extern struct mutex adc_mutex;

static struct mutex save_mutex;
static thread_cond_t save_thread_cond;
static thread_ptr_t save_thread = NULL;
static volatile int save_thread_running = 0;
static volatile int data_ready = 0;

static int dump_fd = 0;
// static int current_pool_index = 0;
static int file_is_ok = 0;
static int gFrameID = 0;
static size_t gFrameBufSize = 0;
static char *gFrameBuf = NULL;

static const char *g_pktBuf = NULL;
static int32_t g_pktSize = 0;

/* ========== 帧索引表（全局缓存，避免每次从头逐帧遍历） ========== */
typedef struct {
    uint32_t frameNumber;  /* 帧 header 中记录的 frameNumber */
    off_t    file_offset;  /* 该帧在文件中的起始偏移（含文件头）*/
    uint32_t packetLen;    /* 该帧 totalPacketLen，供校验/跳过使用 */
} FrameIndexEntry_t;

static FrameIndexEntry_t *g_frame_index = NULL;  /* 动态数组 */
static uint32_t           g_frame_index_cnt = 0; /* 当前有效记录数 */
static uint32_t           g_frame_index_cap = 0; /* 当前分配容量 */
static off_t              g_frame_index_end = 0; /* 已扫描到的文件末尾偏移 */
static off_t              g_frame_index_filesize = 0; /* 已扫描文件的大小 */
static char               g_frame_index_filename[256] = {0};

int DetFrameNum = 0;
/* 释放并重置帧索引 */
static void frame_index_reset(void)
{
    if (g_frame_index) {
        free(g_frame_index);
        g_frame_index = NULL;
    }
    g_frame_index_cnt = 0;
    g_frame_index_cap = 0;
    g_frame_index_end = 0;
    g_frame_index_filesize = 0;
    g_frame_index_filename[0] = '\0';
}

/* 向帧索引表追加一条记录；容量不足时 2 倍扩容，失败返回 -1 */
static int frame_index_append(uint32_t frameNumber, off_t file_offset, uint32_t packetLen)
{
    if (g_frame_index_cnt >= g_frame_index_cap) {
        uint32_t new_cap = g_frame_index_cap ? g_frame_index_cap * 2 : FRAME_INDEX_INIT_CAP;
        FrameIndexEntry_t *new_buf =
            (FrameIndexEntry_t *)realloc(g_frame_index, (size_t)new_cap * sizeof(FrameIndexEntry_t));
        if (!new_buf) {
            printf("[frame_index] 错误: 扩容失败 (cap=%u)\n", new_cap);
            return -1;
        }
        g_frame_index = new_buf;
        g_frame_index_cap = new_cap;
    }
    g_frame_index[g_frame_index_cnt].frameNumber = frameNumber;
    g_frame_index[g_frame_index_cnt].file_offset = file_offset;
    g_frame_index[g_frame_index_cnt].packetLen   = packetLen;
    g_frame_index_cnt++;
    return 0;
}

/*
 * @brief 检测是否需要切换/重建索引（文件名或文件大小变化）
 * @return 1 表示已重建（或保持有效），0 表示仍可沿用旧索引继续增量扫描
 */
static int frame_index_check_rebuild_needed(const char *filename, off_t file_size)
{
    if (g_frame_index_filename[0] == '\0' ||
        strcmp(g_frame_index_filename, filename) != 0 ||
        g_frame_index_filesize != file_size) {
        /* 文件名或文件大小变化 -> 重建 */
        frame_index_reset();
        strncpy(g_frame_index_filename, filename, sizeof(g_frame_index_filename) - 1);
        g_frame_index_filename[sizeof(g_frame_index_filename) - 1] = '\0';
        g_frame_index_filesize = file_size;
        return 1;
    }
    return 0;
}

/*
 * @brief 从 g_frame_index_end 开始继续扫描文件，直到文件末尾或索引表中已有目标帧
 * @param target_frameNumber 若为 (uint32_t)-1，则扫描到文件末尾
 * @param[out] out_entry  若找到目标帧，返回其索引条目；可为 NULL
 * @return 1 找到目标，0 扫描完但未找到，-1 错误
 */
/* 扫描时对 totalPacketLen 的上限，一帧雷达数据不可能超过此大小 */
#define FRAME_MAX_PACKET_LEN  (1024u * 1024u)  /* 1 MB */
#define FRAME_MIN_PACKET_LEN  (28u)            /* header 最小大小 */

static int frame_index_scan_forward(int read_fd, off_t file_size, off_t data_start,
                                    uint32_t target_frameNumber,
                                    FrameIndexEntry_t *out_entry)
{
    off_t cur_offset = g_frame_index_end;   /* 从上一次扫描结束位置继续 */
    off_t off_magic, off_total, off_framenum;
    uint16_t magic_words[4];
    uint32_t totalPacketLen;
    uint32_t hdr_frameNumber;
    int found = 0;

    /* header 字段在结构体中的偏移(考虑对齐) */
    const off_t TOTALPACKETLEN_OFF_IN_HDR = 12;   /* magic(8)+version(4)=12 */
    /* platform(uint8)@16 + 3字节对齐填充 -> frameNumber(uint32)@20 */
    const off_t FRAMENUMBER_OFF_IN_HDR = 20;

    (void)data_start;

    while (cur_offset < file_size) {
        /* 1) 先验证当前帧起始位置的 magic word: {0x0102,0x0304,0x0506,0x0708} */
        if (cur_offset + 8 > file_size) break;
        if (lseek(read_fd, cur_offset, SEEK_SET) != cur_offset) {
            printf("[frame_index] 错误: 无法定位帧起始 (off=%ld)\n", (long)cur_offset);
            return -1;
        }
        if (read(read_fd, magic_words, sizeof(magic_words)) != sizeof(magic_words)) {
            printf("[frame_index] 错误: 读取 magic word 失败\n");
            return -1;
        }
        if (magic_words[0] != 0x0102 || magic_words[1] != 0x0304 ||
            magic_words[2] != 0x0506 || magic_words[3] != 0x0708) {
            printf("[frame_index] 错误: off=%ld 处 magic word 不匹配 (%#x %#x %#x %#x), 中止扫描\n",
                   (long)cur_offset,
                   (unsigned)magic_words[0], (unsigned)magic_words[1],
                   (unsigned)magic_words[2], (unsigned)magic_words[3]);
            return -1;
        }

        /* 2) 读取 totalPacketLen */
        off_total = cur_offset + TOTALPACKETLEN_OFF_IN_HDR;
        if (lseek(read_fd, off_total, SEEK_SET) != off_total) {
            printf("[frame_index] 错误: 无法定位 totalPacketLen (off=%ld)\n", (long)off_total);
            return -1;
        }
        if (read(read_fd, &totalPacketLen, sizeof(uint32_t)) != (ssize_t)sizeof(uint32_t)) {
            printf("[frame_index] 错误: 读取 totalPacketLen 失败\n");
            return -1;
        }
        if (totalPacketLen < FRAME_MIN_PACKET_LEN || totalPacketLen > FRAME_MAX_PACKET_LEN ||
            totalPacketLen > (uint32_t)(file_size - cur_offset)) {
            printf("[frame_index] 错误: off=%ld 处 totalPacketLen=%ld 无效 (剩余=%ld)\n",
                   (long)cur_offset, (long)totalPacketLen,
                   (long)(file_size - cur_offset));
            return -1;
        }

        /* 3) 读取 frameNumber */
        off_framenum = cur_offset + FRAMENUMBER_OFF_IN_HDR;
        if (lseek(read_fd, off_framenum, SEEK_SET) != off_framenum) {
            printf("[frame_index] 错误: 无法定位 frameNumber (off=%ld)\n", (long)off_framenum);
            return -1;
        }
        if (read(read_fd, &hdr_frameNumber, sizeof(uint32_t)) != (ssize_t)sizeof(uint32_t)) {
            printf("[frame_index] 错误: 读取 frameNumber 失败\n");
            return -1;
        }

        /* 4) 写入索引表 */
        if (frame_index_append((uint32_t)hdr_frameNumber, cur_offset, (uint32_t)totalPacketLen) != 0) {
            return -1;
        }

        /* 5) 是否命中目标？ */
        if (target_frameNumber != (uint32_t)-1 && (uint32_t)hdr_frameNumber == target_frameNumber) {
            if (out_entry) {
                out_entry->frameNumber = (uint32_t)hdr_frameNumber;
                out_entry->file_offset = cur_offset;
                out_entry->packetLen   = totalPacketLen;
            }
            found = 1;
            cur_offset += totalPacketLen;
            g_frame_index_end = cur_offset;
            break;
        }

        /* 6) 推进到下一帧起始 */
        cur_offset += totalPacketLen;
        g_frame_index_end = cur_offset;
    }
    return found ? 1 : 0;
}

/*
 * @brief 在已建立的索引表中查找
 *        frameID 的语义: 文件中第 frameID 帧 (1-based 顺序索引)
 *        即 frameID=1 -> 数组下标 0, frameID=2 -> 数组下标 1 ...
 *        return 1 命中, 0 未命中（当前记录数不足 frameID）
 */
static int frame_index_lookup(uint32_t frameID, FrameIndexEntry_t *out_entry)
{
    if (frameID == 0 || frameID > g_frame_index_cnt) {
        return 0;
    }
    if (out_entry) {
        *out_entry = g_frame_index[frameID - 1];
    }
    return 1;
}
// static Mmw_output_message_DetInfo gDetInfo = {0};
// static Mmw_output_message_TrkInfo gTrkInfo = {0};
// #if EGO_VLC_ENABLE
// static Mmw_output_message_EgoVlcInfo gEgoVlcInfo = {0};
// #endif
// #if WARNING_ENABLE
// static Mmw_output_message_WarnInfo gWarnInfo = {0};
// #endif

static int save_frame_pkt_collect(void);
static int save_data_to_file(char *data, int len);

#if USE_SAVE_ANGLE
    static int32_t save_angle = 666;

    void set_save_angle(int32_t angle) 
    {
        save_angle = angle;
    }
#endif

#define RAW_PACKET_HEADER_SIZE (sizeof(Mmw_output_message_header) + sizeof(Mmw_output_message_tl))
static char raw_packet_header[RAW_PACKET_HEADER_SIZE];
static void set_raw_packet_header(void)
{
    Mmw_output_message_header header;
	Mmw_output_message_tl tl;
	uint32_t packetLen = 0;
	char *currentPtr = NULL;

	memset((void *)&header, 0, sizeof(Mmw_output_message_header));
	memset((void *)&tl, 0, sizeof(Mmw_output_message_tl));

	header.platform = 0x24;
	header.magicWord[0] = 0x0102;
	header.magicWord[1] = 0x0304;
	header.magicWord[2] = 0x0506;
	header.magicWord[3] = 0x0708;

	packetLen = sizeof(Mmw_output_message_header);

    tl.type = MMW_OUTPUT_MSG_ADC_FRAME;
#if USE_SAVE_ANGLE
    tl.length = gFrameBufSize + sizeof(int32_t) * 6; // sizeof(int32_t) * 6 : angle data
#else
    tl.length = gFrameBufSize;
#endif
    packetLen += sizeof(Mmw_output_message_tl) + tl.length;

    header.numTLVs = 1;
	header.totalPacketLen = packetLen;
    header.frameNumber = gFrameID;

	memset(raw_packet_header, 0, RAW_PACKET_HEADER_SIZE);
	currentPtr = raw_packet_header;

	memcpy(currentPtr, &header, sizeof(Mmw_output_message_header));
	currentPtr += sizeof(Mmw_output_message_header);

    memcpy(currentPtr, &tl, sizeof(Mmw_output_message_tl));
    currentPtr += sizeof(Mmw_output_message_tl);

    return;
}

static void save_thread_func(void *data)
{
    // uint64_t start_time, end_time;

    while (save_thread_running)
    {
        mutex_lock(&save_mutex);

        while (!data_ready && save_thread_running)
        {
            thread_cond_wait(&save_thread_cond, &save_mutex);
        }

        if (!save_thread_running)
        {
            mutex_unlock(&save_mutex);
            break;
        }

        data_ready = 0;

        // start_time = get_time_ns();
        // printf("[*** frame %d ***] 开始保存数据\n", gFrameID);

        if (SAVE_RAW_DATA)
        {
            set_raw_packet_header();
            // save_data_to_file(raw_packet_header, RAW_PACKET_HEADER_SIZE);
            save_data_to_file(gFrameBuf, gFrameBufSize);
            // printf("[*** frame %d ***] 保存原始数据完成\n", gFrameID);
        }
        else
        {
            save_frame_pkt_collect();
        }

        // end_time = get_time_ns();

        // double save_time_ms = (end_time - start_time) / 1000000.0;
		// printf("[*** frame %d ***] save thread 总耗时: %.2f 毫秒\n", gFrameID, save_time_ms);

        mutex_unlock(&save_mutex);

        // /************************************************************* */
        mutex_lock(&adc_mutex);
        adc_finished = 1;
        mutex_unlock(&adc_mutex);
        printf("ADC采样完成通知已发送\n");
        // /************************************************************* */
    }
    
    // printf("Save thread exiting normally\n");
}

static int save_thread_init(void)
{
    static int first_init = 1;
    if (first_init)
    {
        mutex_init(&save_mutex);
        thread_cond_init(&save_thread_cond);

        first_init = 0;
    }

    save_thread = thread_create("save_test", 8192, save_thread_func, NULL);
    if (save_thread == NULL)
    {
        printf("Failed to create save thread\n");
        return -1;
    }

    save_thread_running = 1;
    
    return 0;
}

int save_frame_pkt_update_raw(void *rawdata, uint32_t len, uint32_t frameID, int pool_index)
{
    if (!file_is_ok || !rawdata || len <= 0)
    {
        mutex_lock(&adc_mutex);
        adc_finished = 1;
        mutex_unlock(&adc_mutex);

        return -1;
    }

    mutex_lock(&save_mutex);

    gFrameBuf = g_memoryPool[pool_index].saver_pkt_buf_raw;
    // memcpy(gFrameBuf, rawdata, len);
    gFrameBufSize = len;
    gFrameID = frameID;

    data_ready = 1;

    mutex_unlock(&save_mutex);

#ifdef USE_SAVE_THREAD
    thread_cond_broadcast(&save_thread_cond);
#else
    save_data_to_file(gFrameBuf, gFrameBufSize);
#endif

    return 0;
}

int save_frame_pkt_update(const char *pktBuf, int32_t pktSize, uint32_t frameID, int pool_index)
{
    if (!file_is_ok || !pktBuf || pktSize <= 0)
    {
        mutex_lock(&adc_mutex);
        adc_finished = 1;
        mutex_unlock(&adc_mutex);
        
        return -1;
    }

    mutex_lock(&save_mutex);

#if SAVE_RAW_DATA
    // gFrameBuf = g_memoryPool[pool_index].saver_pkt_buf_raw;
    // memcpy(gFrameBuf, rawdata, len);
    // gFrameBufSize = len;
#endif


    g_pktBuf = pktBuf;
    g_pktSize = pktSize;
    gFrameID = frameID;

    data_ready = 1;

    mutex_unlock(&save_mutex);
    
#ifdef USE_SAVE_THREAD
    thread_cond_broadcast(&save_thread_cond);
#else
    save_frame_pkt_collect();
#endif

    return 0;
}

static int file_header_update(void)
{
    MmwSaveDataHeader_t file_header;
    off_t current_pos;
    
    file_header.magic = FILE_MAGIC_WORD;
    file_header.framesNum = gFrameID + 1;

    if (!file_is_ok)
    {
        return -1;
    }
    
    /* 保存当前位置 */
    current_pos = lseek(dump_fd, 0, SEEK_CUR);
    if (current_pos == (off_t)-1)
    {
        printf("get current position failed!\r\n");
        return -1;
    }

    /* 移动到文件开头更新文件头 */
    if (lseek(dump_fd, 0, SEEK_SET) == (off_t)-1)
    {
        printf("seek to file beginning failed!\r\n");
        return -1;
    }
    
    if (save_data_to_file((char *)&file_header, sizeof(MmwSaveDataHeader_t)))
    {
        printf("save file header failed!\r\n");
        return -1;
    }

    /* 恢复到原来的位置，便于后续数据追加 */
    if (lseek(dump_fd, current_pos, SEEK_SET) == (off_t)-1)
    {
        printf("restore position failed!\r\n");
        return -1;
    }

    return 0;
}

static int save_frame_pkt_collect(void)
{
	if (!g_pktBuf || g_pktSize <= 0) {
		printf("ERROR: Invalid shared packet buffer\n");
		return -1;
	}

	/* 一次性写入文件 */
	save_data_to_file((char *)g_pktBuf, g_pktSize);

	file_header_update();

	return 0;
}

static int save_data_to_file(char *data, int len)
{
    int ret;

    if (file_is_ok)
    {
#if (SAVE_RAW_DATA == 1)
#if USE_SAVE_ANGLE
        int32_t save_tmp[6] = {0};
        save_tmp[0] = 0x12345678; // 示例数据1
        save_tmp[1] = 0x9ABCDEF0; // 示例数据2
        save_tmp[2] = 0x12345678; // 示例数据3
        save_tmp[3] = 0x9ABCDEF0; // 示例数据4
        save_tmp[4] = save_angle;
        save_tmp[5] = 0x87654321; // 示例数据5，覆盖角度数据以便验证

#endif

        // tlv header
        ret = write(dump_fd, (void *)raw_packet_header, RAW_PACKET_HEADER_SIZE);
        if (ret != RAW_PACKET_HEADER_SIZE)
        {
            printf("save raw packet header failed!\r\n");
            return -1;
        }

#if USE_SAVE_ANGLE
        // angle data
        ret = write(dump_fd, (void *)save_tmp, sizeof(save_tmp));
        if (ret != sizeof(save_tmp))
        {
            printf("save angle failed!\r\n");
            return -1;
        }
#endif

        // adc data
#endif
        ret = write(dump_fd, (void *)data, len);
        if (ret != len)
        {
            printf("save data failed!\r\n");
            return -1;
        }

        if ((gFrameID % MAX_FSYNC_INTERVAL) == 0)
        {
            if (fsync(dump_fd))
            {
                printf("fsync failed!\r\n");
                return -1;
            }
        }

        printf("save data len=%d success!\r\n", len);
    }

    return 0;
}

int save_init(char *file_name)
{
    if (strcmp(file_name, ""))
    {
        char new_file_name[256] = {0};
        int file_counter = 0;
        
        /* 检查原文件是否存在 */
        if (access(file_name, F_OK) == 0)
        {
            printf("file %s exists!\r\n", file_name);
            /* 文件已存在，查找文件名中的最后一个数字 */
            char *last_underscore = strrchr(file_name, '_');
            char base_name[256] = {0};
            char ext_name[64] = {0};
            char *dot_pos = strrchr(file_name, '.');
            
            if (dot_pos != NULL)
            {
                /* 分离文件名和扩展名 */
                strncpy(base_name, file_name, dot_pos - file_name);
                strcpy(ext_name, dot_pos);
            }
            else
            {
                /* 没有扩展名 */
                strcpy(base_name, file_name);
                strcpy(ext_name, "");
            }
            
            /* 查找最后一个下划线后的数字 */
            if (last_underscore != NULL && last_underscore < dot_pos)
            {
                char *number_start = last_underscore + 1;
                char number_str[16] = {0};
                int number_len = 0;
                
                /* 提取数字部分 */
                while (*number_start >= '0' && *number_start <= '9' && number_len < 15)
                {
                    number_str[number_len++] = *number_start++;
                }
                
                if (number_len > 0)
                {
                    /* 有数字，从该数字+1开始 */
                    file_counter = atoi(number_str) + 1;
                    /* 截断基础名称到最后一个下划线前 */
                    base_name[last_underscore - file_name] = '\0';
                }
                else
                {
                    /* 没有数字，从1开始 */
                    file_counter = 1;
                }
            }
            else
            {
                /* 没有下划线或下划线在扩展名后，从1开始 */
                file_counter = 1;
            }
            
            /* 查找可用的文件名 */
            do
            {
                snprintf(new_file_name, sizeof(new_file_name), "%s_%d%s", base_name, file_counter, ext_name);
                file_counter++;
            } while (access(new_file_name, F_OK) == 0 && file_counter < 10000);
            
            if (file_counter >= 10000)
            {
                printf("ERROR: Cannot find available file name after 10000 attempts\r\n");
                return -1;
            }
            
            printf("File %s exists, using new name: %s\r\n", file_name, new_file_name);
        }
        else
        {
            /* 文件不存在，使用原文件名 */
            strcpy(new_file_name, file_name);
        }
        
        dump_fd = open(new_file_name, O_CREAT | O_RDWR | O_TRUNC, 0);
        if (dump_fd < 0)
        {
            printf("create dump file %s failed!\r\n", new_file_name);
            return -1;
        }

        file_is_ok = 1;
    }

    if (!SAVE_RAW_DATA)
    {
        MmwSaveDataHeader_t file_header;
        file_header.magic = FILE_MAGIC_WORD;
        file_header.framesNum = 0;
        if (save_data_to_file((char *)&file_header, sizeof(MmwSaveDataHeader_t)))
        {
            printf("save file header failed!\r\n");
            return -1;
        }
        
        if (fsync(dump_fd))
        {
            printf("fsync failed!\r\n");
            return -1;
        }
    }

#ifdef USE_SAVE_THREAD
    save_thread_init();
#endif

    return 0;
}

void save_close(void)
{
    if (file_is_ok)
    {
#ifdef USE_SAVE_THREAD
        mutex_lock(&save_mutex);
        save_thread_running = 0;
        data_ready = 1;
        thread_cond_broadcast(&save_thread_cond);
        mutex_unlock(&save_mutex);
        thread_join(save_thread, NULL);
#endif

        close(dump_fd);
        file_is_ok = 0;
        data_ready = 0;
    }
    
    return;
}

/**
 * @brief 从SD卡回读指定帧的原始ADC数据
 * @details 使用持久化文件描述符（缓存fd，避免每帧open/close），帧索引扫描定位目标帧，
 *          验证TLV帧头合法性，跳过36字节TLV header + 24字节角度数据，
 *          读取纯ADC payload数据到buffer。
 *          文件格式 (SAVE_RAW_DATA=1): [Frame: 36B TLV header + 24B angle + g_total_adc_size payload]
 *          TLV header结构: [Mmw_output_message_header(28B)] + [Mmw_output_message_tl(8B)]
 *
 * @param filename 要读取的文件名
 * @param frameID 要读取的帧号 (1-based, 即 1 = 第 1 帧)
 * @param buffer 存储读取数据的缓冲区
 * @param buffer_size 缓冲区大小 (通常 = g_total_adc_size)
 * @param offset 未使用，保留兼容接口
 * @return 成功返回读取的字节数，失败返回-1
 */

/* === 持久化文件描述符 (文件级静态变量，避免每帧 open/close 导致堆损坏) === */
static int   s_cached_read_fd = -1;
static char  s_cached_read_filename[256] = {0};
static off_t s_cached_read_filesize = 0;
static off_t s_cached_read_data_start = 0;
static int   s_cached_read_has_header = 0;
static int   s_cached_read_init = 0;

int read_frame_raw_data(const char *filename, uint32_t frameID, void *buffer, uint32_t buffer_size, off_t offset)
{

    if (!buffer || buffer_size == 0) {
        printf("[read_frame_raw_data] 错误: 缓冲区无效\n");
        return -1;
    }

    const char *read_filename = filename;
    if (!read_filename) {
        if (!file_is_ok) {
            printf("[read_frame_raw_data] 错误: 没有指定文件名且无活动保存文件\n");
            return -1;
        }
        printf("[read_frame_raw_data] 错误: 需要指定文件名\n");
        return -1;
    }

    /* === 检查是否需要重新打开文件 === */
    int need_open = 0;
    if (!s_cached_read_init || s_cached_read_fd < 0) {
        need_open = 1;
    } else if (strcmp(s_cached_read_filename, read_filename) != 0) {
        /* 文件名变化 -> 关闭旧文件，重新打开 */
        printf("[read_frame_raw_data] 文件名变化，重新打开文件\n");
        if (s_cached_read_fd >= 0) {
            close(s_cached_read_fd);
            s_cached_read_fd = -1;
        }
        frame_index_reset();
        need_open = 1;
    }

    if (need_open) {
        /* === 打开文件 (仅首次或文件名变化时) === */
        s_cached_read_fd = open(read_filename, O_RDONLY);
        if (s_cached_read_fd < 0) {
            printf("[read_frame_raw_data] 错误: 无法打开文件 %s\n", read_filename);
            return -1;
        }
        strncpy(s_cached_read_filename, read_filename, sizeof(s_cached_read_filename) - 1);
        s_cached_read_filename[sizeof(s_cached_read_filename) - 1] = '\0';
        s_cached_read_init = 1;

        /* === 检查文件大小 === */
        s_cached_read_filesize = lseek(s_cached_read_fd, 0, SEEK_END);
        if (s_cached_read_filesize <= 0) {
            printf("[read_frame_raw_data] 错误: 文件大小为0\n");
            close(s_cached_read_fd);
            s_cached_read_fd = -1;
            s_cached_read_init = 0;
            return -1;
        }

        /* === 检测文件头 === */
        MmwSaveDataHeader_t file_header;
        s_cached_read_has_header = 0;
        s_cached_read_data_start = 0;

        if (lseek(s_cached_read_fd, 0, SEEK_SET) < 0) {
            close(s_cached_read_fd);
            s_cached_read_fd = -1;
            s_cached_read_init = 0;
            return -1;
        }
        if (read(s_cached_read_fd, &file_header, sizeof(file_header)) == (ssize_t)sizeof(file_header)) {
            if (file_header.magic == FILE_MAGIC_WORD &&
                file_header.framesNum > 0 && file_header.framesNum < 1000000u) {
                /* 二次校验: 检查紧随其后的帧 magicWord */
                uint16_t next_magic[4];
                if (lseek(s_cached_read_fd, (off_t)sizeof(MmwSaveDataHeader_t), SEEK_SET) ==
                    (off_t)sizeof(MmwSaveDataHeader_t) &&
                    read(s_cached_read_fd, next_magic, sizeof(next_magic)) == (ssize_t)sizeof(next_magic) &&
                    next_magic[0] == 0x0102 && next_magic[1] == 0x0304 &&
                    next_magic[2] == 0x0506 && next_magic[3] == 0x0708) {
                    s_cached_read_has_header = 1;
                    s_cached_read_data_start = sizeof(MmwSaveDataHeader_t);
                    printf("[read_frame_raw_data] 检测到文件头, 跳过%u字节, 文件大小=%ld\n",
                           (unsigned)sizeof(MmwSaveDataHeader_t), (long)s_cached_read_filesize);
                }
            }
        }

        if (!s_cached_read_has_header) {
            s_cached_read_data_start = 0;
        }

        frame_index_reset();
        printf("[read_frame_raw_data] 文件已打开 (fd=%d, size=%ld, data_start=%ld)\n",
               s_cached_read_fd, (long)s_cached_read_filesize, (long)s_cached_read_data_start);
    }

    /* === 使用缓存的文件描述符读取 === */
    int read_fd = s_cached_read_fd;
    off_t file_size = s_cached_read_filesize;
    off_t data_start = s_cached_read_data_start;

    /* === 使用帧索引定位目标帧 === */
    frame_index_check_rebuild_needed(read_filename, file_size);
    if (g_frame_index_cnt == 0 && g_frame_index_end < data_start) {
        g_frame_index_end = data_start;
    }

    FrameIndexEntry_t target_entry;
    int hit = frame_index_lookup(frameID, &target_entry);

    if (!hit) {
        /* 索引不足，继续扫描 */
        if (g_frame_index_end >= file_size) {
            printf("[read_frame_raw_data] 错误: 已扫描到文件尾仍不足%u帧 (共%u帧)\n",
                   frameID, g_frame_index_cnt);
            return -1;
        }
        int sres = frame_index_scan_forward(read_fd, file_size, data_start,
                                            (uint32_t)-1, NULL);
        if (sres < 0) {
            printf("[read_frame_raw_data] 错误: 扫描失败\n");
            frame_index_reset();
            return -1;
        }
        if (!frame_index_lookup(frameID, &target_entry)) {
            printf("[read_frame_raw_data] 错误: 扫描后仍未找到第%u帧\n", frameID);
            return -1;
        }
        hit = 1;
    }

    /* === 第三步: 读取目标帧完整header验证合法性 === */
    off_t frame_offset = target_entry.file_offset;
    uint32_t totalPacketLen = target_entry.packetLen;

    /* 读取28B帧头 + 8B TLV头进行验证 */
    Mmw_output_message_header frame_hdr;
    Mmw_output_message_tl     frame_tl;

    if (lseek(read_fd, frame_offset, SEEK_SET) != frame_offset) {
        printf("[read_frame_raw_data] 错误: 无法定位到第%u帧起始位置 (off=%ld)\n",
               frameID, (long)frame_offset);
        return -1;
    }

    /* 读取帧头 (28字节) */
    if (read(read_fd, &frame_hdr, sizeof(frame_hdr)) != (ssize_t)sizeof(frame_hdr)) {
        printf("[read_frame_raw_data] 错误: 读取帧头失败 (frameID=%u, off=%ld)\n",
               frameID, (long)frame_offset);
        return -1;
    }

    /* 验证 magic word */
    if (frame_hdr.magicWord[0] != 0x0102 || frame_hdr.magicWord[1] != 0x0304 ||
        frame_hdr.magicWord[2] != 0x0506 || frame_hdr.magicWord[3] != 0x0708) {
        printf("[read_frame_raw_data] 错误: 第%u帧magic word不匹配 (%#x %#x %#x %#x)\n",
               frameID,
               (unsigned)frame_hdr.magicWord[0], (unsigned)frame_hdr.magicWord[1],
               (unsigned)frame_hdr.magicWord[2], (unsigned)frame_hdr.magicWord[3]);
        return -1;
    }

    /* 读取TLV头 (8字节) */
    if (read(read_fd, &frame_tl, sizeof(frame_tl)) != (ssize_t)sizeof(frame_tl)) {
        printf("[read_frame_raw_data] 错误: 读取TLV头失败 (frameID=%u)\n", frameID);
        return -1;
    }

    /* 验证 totalPacketLen 一致性 */
    if (frame_hdr.totalPacketLen != totalPacketLen) {
        printf("[read_frame_raw_data] 警告: totalPacketLen不一致 (索引=%u, 帧头=%u)\n",
               totalPacketLen, frame_hdr.totalPacketLen);
        totalPacketLen = frame_hdr.totalPacketLen;  /* 使用帧头中的权威值 */
    }

    /* 验证帧号一致性 */
    if (frame_hdr.frameNumber != target_entry.frameNumber) {
        printf("[read_frame_raw_data] 警告: frameNumber不一致 (索引=%u, 帧头=%u)\n",
               target_entry.frameNumber, frame_hdr.frameNumber);
    }

    /* 验证TLV类型应为ADC_FRAME */
    if (frame_tl.type != (uint32_t)MMW_OUTPUT_MSG_ADC_FRAME) {
        printf("[read_frame_raw_data] 警告: TLV类型=%u, 期望=%u(ADC_FRAME)\n",
               frame_tl.type, (uint32_t)MMW_OUTPUT_MSG_ADC_FRAME);
    }

    printf("[read_frame_raw_data] 第%u帧帧头验证: frameNumber=%u, totalPacketLen=%u, "
           "TLV_type=%u, TLV_length=%u, numTLVs=%u\n",
           frameID, frame_hdr.frameNumber, frame_hdr.totalPacketLen,
           frame_tl.type, frame_tl.length, frame_hdr.numTLVs);

    /* === 第四步: 计算payload偏移并读取 === */
    /* payload在frame_offset + 36处 (跳过28B frame header + 8B TLV header) */
    off_t payload_offset = frame_offset + (off_t)RAW_PACKET_HEADER_SIZE;
    uint32_t payload_len = totalPacketLen - RAW_PACKET_HEADER_SIZE;

    /* 使用TLV length做交叉验证 */
    if (frame_tl.length != payload_len) {
        printf("[read_frame_raw_data] 警告: payloadLen不一致 (totalPacketLen推导=%u, TLV_length=%u)\n",
               payload_len, frame_tl.length);
        /* 信任TLV length，因为它是payload的权威长度 */
        payload_len = frame_tl.length;
    }

    /* 边界检查: payload_len必须是合理的ADC数据大小 */
    if (payload_len < 32) {  /* 至少要有一个32B chirp header */
        printf("[read_frame_raw_data] 错误: payload_len=%u过小，不合法\n", payload_len);
        return -1;
    }

    /* === 关键修复: 跳过角度数据 (USE_SAVE_ANGLE) ===
     * 当保存时启用了USE_SAVE_ANGLE，payload格式为:
     *   [24B angle数据] + [gFrameBufSize ADC数据]
     * TLV_length = 24 + gFrameBufSize
     * buffer_size = gFrameBufSize (调用方传入)
     * 所以 angle_data_size = TLV_length - buffer_size
     * 需要跳过angle数据，读取纯ADC payload
     */
    uint32_t angle_data_size = 0;
    if (payload_len > buffer_size) {
        angle_data_size = payload_len - buffer_size;
        if (angle_data_size > 0 && angle_data_size <= 128) {
            /* angle_data_size必须是合理的小值 (通常24=sizeof(int32_t)*6) */
            printf("[read_frame_raw_data] 检测到%d字节角度数据，跳过\n", angle_data_size);
            payload_offset += angle_data_size;
            payload_len = buffer_size;  /* 读取纯ADC数据 */
        } else {
            /* angle_data_size异常，按原逻辑截断 */
            printf("[read_frame_raw_data] 警告: payload(%u) > buffer_size(%u), 截断读取 (angle_data异常=%u)\n",
                   payload_len, buffer_size, angle_data_size);
            payload_len = buffer_size;
        }
    } else if (payload_len < buffer_size) {
        printf("[read_frame_raw_data] 警告: payload(%u) < buffer_size(%u), 实际读取%u字节\n",
               payload_len, buffer_size, payload_len);
    }

    /* === 定位并读取payload === */
    if (lseek(read_fd, payload_offset, SEEK_SET) != payload_offset) {
        printf("[read_frame_raw_data] 错误: 无法定位到payload (off=%ld)\n",
               (long)payload_offset);
        return -1;
    }

    ssize_t bytes_read = read(read_fd, buffer, payload_len);
    if (bytes_read < 0) {
        printf("[read_frame_raw_data] 错误: 读取payload失败 (期望=%u字节)\n", payload_len);
        return -1;
    }

    /* 读取完成验证: 检查是否读到了有效数据 (以chirp header起始) */
    if (bytes_read >= 32) {
        uint8_t *p = (uint8_t *)buffer;
        /* Chirp header的第一个字段通常为0或小值 */
        uint16_t first_word = *((uint16_t *)p);
        if (first_word == 0xFFFF || first_word == 0x0000) {
            /* 可能是空数据，记录警告但不视为错误 */
            printf("[read_frame_raw_data] 警告: payload起始字为%04x, 可能为空帧\n", first_word);
        }
    }

    printf("[read_frame_raw_data] 第%u帧读取成功: frameNumber=%u, payload_offset=%ld, payload=%zd字节\n",
           frameID, frame_hdr.frameNumber, (long)payload_offset, bytes_read);

    return bytes_read;
}

/**
 * @brief 关闭持久化的文件描述符 (REPLAY会话结束时调用)
 */
void read_frame_raw_data_close(void)
{
    if (s_cached_read_fd >= 0) {
        close(s_cached_read_fd);
        s_cached_read_fd = -1;
        s_cached_read_init = 0;
        printf("[read_frame_raw_data] 已关闭持久化文件描述符\n");
    }
}

/**
 * @brief 将文件格式的 DETINFO payload 解析填充到内存中的 DetInfo 结构体
 *        文件格式: DetInfo_Header(uint16 numDets) + MotorCycle_DetObj_Read[numDets]
 *        内存格式: DetInfo(int numDets, int numStaticDets) + DetObj[numDets]
 * @param payload 从文件读取的 DETINFO payload 原始数据
 * @param payload_size payload 字节数
 * @param detInfo 输出: 填充后的 DetInfo 结构体
 * @return 成功返回解析的检测点数量，失败返回-1
 */
int parse_detinfo_payload(const uint8_t *payload, uint32_t payload_size, DetInfo *detInfo)
{
    if (!payload || !detInfo) {
        printf("[parse_detinfo] 错误: 参数无效 (payload=%p, detInfo=%p)\n", payload, detInfo);
        return -1;
    }
    if (payload_size < sizeof(DetInfo_Header)) {
        printf("[parse_detinfo] 错误: payload_size=%u < sizeof(header)=%u\n",
               payload_size, (unsigned)sizeof(DetInfo_Header));
        return -1;
    }

    // === 1. 读取 DetInfo_Header (与保存时 memcpy 布局一致) ===
    // 文件格式: [uint16_t numDets][MotorCycle_DetObj_Read * N]
    DetInfo_Header file_header;
    memcpy(&file_header, payload, sizeof(DetInfo_Header));
    uint16_t numDets_file = file_header.numDets;

    printf("[parse_detinfo] payload_size=%u, sizeof_header=%u, sizeof_obj=%u, numDets_raw=%u\n",
           payload_size, (unsigned)sizeof(DetInfo_Header),
           (unsigned)sizeof(MotorCycle_DetObj_Read), numDets_file);

    // === 2. 边界检查 ===
    uint32_t bytes_for_obj = payload_size - sizeof(DetInfo_Header);
    uint32_t max_by_size = bytes_for_obj / sizeof(MotorCycle_DetObj_Read);

    if (numDets_file > max_by_size) {
        printf("[parse_detinfo] 警告: numDets=%u > 容量=%u (剩余%u字节, 每对象%u字节), 截断为%u\n",
               numDets_file, max_by_size, bytes_for_obj,
               (unsigned)sizeof(MotorCycle_DetObj_Read), max_by_size);
        numDets_file = (uint16_t)max_by_size;
    }
    if (numDets_file > MAX_DETECTIONS) {
        printf("[parse_detinfo] 警告: numDets=%u > MAX_DETECTIONS=%u, 截断\n",
               numDets_file, MAX_DETECTIONS);
        numDets_file = MAX_DETECTIONS;
    }

    // === 3. 初始化目标 detInfo (清空避免未初始化字段) ===
    memset(detInfo, 0, sizeof(DetInfo));
    detInfo->numDets = (int)numDets_file;
    detInfo->numStaticDets = 0;

    // === 4. 逐个解析 MotorCycle_DetObj_Read -> DetObj ===
    //    参考 read_mat_file.c 中 read_detInfo_mat 的字段读取顺序:
    //    DetObj: relRDIdx, pwr, snr, isPeak, rng, vlc_amb, vlc,
    //            vlc_disAmb_conf, vlc_disAmb_fac, azm_deg,
    //            x_rcs, y_rcs, z_rcs, x_output, y_output, motion_state, ...
    //    MotorCycle_DetObj_Read: relRDIdx, vlc, x_output, y_output, motion_state,
    //                       pwr, snr, isPeak, rng, vlc_amb, vlc_disAmb_conf,
    //                       vlc_disAmb_fac, azm_deg, x_rcs, y_rcs
    //    注意: 字段顺序不同，但各自结构体内部的 memcpy 布局由编译器保证
    const uint8_t *obj_base = payload + sizeof(DetInfo_Header);
    uint32_t obj_size = sizeof(MotorCycle_DetObj_Read);

    for (uint16_t i = 0; i < numDets_file; i++) {
        MotorCycle_DetObj_Read file_obj;
        memcpy(&file_obj, obj_base + (uint32_t)i * obj_size, obj_size);

        DetObj *obj = &detInfo->detObj[i];
        // 按 read_mat_file.c 的 DetObj 字段顺序填充
        obj->relRDIdx = (int)file_obj.relRDIdx;
        obj->pwr = (float)file_obj.pwr;         // uint16(2位小数) -> float
        obj->snr = (float)file_obj.snr;         // uint16(2位小数) -> float
        obj->isPeak = (file_obj.isPeak != 0);
        obj->rng = file_obj.rng;
        obj->vlc_amb = (float)file_obj.vlc_amb; // int16(2位小数) -> float
        obj->vlc = file_obj.vlc;
        obj->vlc_disAmb_conf = (int)file_obj.vlc_disAmb_conf;
        obj->vlc_disAmb_fac = (int)file_obj.vlc_disAmb_fac;
        obj->azm_deg = file_obj.azm_deg;
        obj->x_rcs = (float)file_obj.x_rcs;     // int16(2位小数) -> float
        obj->y_rcs = (float)file_obj.y_rcs;     // int16(2位小数) -> float
        // obj->z_rcs = 0.0f;                               // 文件中无此字段
        obj->x_output = file_obj.x_output;
        obj->y_output = file_obj.y_output;
        obj->motion_state = (int)file_obj.motion_state;
        // obj->isInStaticZone = false;                     // 文件中无此字段
        // obj->assocStatus = 0;                            // 文件中无此字段
        // assocTrkID, isForVlcUpdate, assocTrkVlc, secondAng 等保持 memset 的 0

        // 详细调试: 打印前 3 个检测对象的关键字段
        // if (i < 3) {
        //     printf("[parse_detinfo] obj[%u]: relRDIdx=%d, x_output=%.3f, y_output=%.3f, "
        //            "rng=%.3f, azm_deg=%.3f, pwr=%.2f, snr=%.2f, vlc=%.3f, motion_state=%d\n",
        //            i, obj->relRDIdx, obj->x_output, obj->y_output,
        //            obj->rng, obj->azm_deg, obj->pwr, obj->snr, obj->vlc, obj->motion_state);
        // }
    }

    printf("[parse_detinfo] 完成: 解析 %u 个检测点, detInfo->numDets=%d\n",
           numDets_file, detInfo->numDets);
    return (int)numDets_file;
}

/**
 * @brief 从TLV格式的雷达数据文件中读取指定帧的DETINFO（检测点信息）
 * @param filename 要读取的文件名
 * @param frameID 要读取的帧号（从1开始）
 * @param buffer 存储读取数据的缓冲区
 * @param buffer_size 缓冲区大小
 * @return 成功返回读取的字节数，失败返回-1
 */
int read_Det_data(const char *filename, uint32_t frameID, void *buffer, uint32_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        printf("[read_Det_data] 错误: 缓冲区无效\n");
        return -1;
    }

    int read_fd = -1;
    int ret = -1;
    off_t frame_offset = 0;          /* 目标帧在文件中的起始偏移 */
    uint32_t i;
    const char *read_filename = filename;

    if (!read_filename) {
        if (!file_is_ok) {
            printf("[read_Det_data] 错误: 没有活动的保存文件\n");
            return -1;
        }
        printf("[read_Det_data] 警告: 需要实现文件名存储逻辑\n");
        return -1;
    }

    read_fd = open(read_filename, O_RDONLY);
    if (read_fd < 0) {
        printf("[read_Det_data] 错误: 无法打开文件 %s\n", read_filename);
        return -1;
    }

    off_t file_size = lseek(read_fd, 0, SEEK_END);
    if (file_size <= 0) {
        printf("[read_Det_data] 错误: 文件大小为0或获取失败\n");
        goto cleanup;
    }

    /* === 第一步: 检测文件头，确定 data_start 偏移 ===
     * [注意] FILE_MAGIC_WORD=0x03040102 与帧 magicWord[4]={0x0102,0x0304,...}
     * 的前 4 字节(uint32_t 小端)重合，必须二次校验，否则会把纯 TLV 数据文件
     * 的第一帧误当作文件头，从偏移 8 开始读，导致 totalPacketLen/frameNumber 错位。
     */
    MmwSaveDataHeader_t file_header;
    off_t data_start = 0;
    int has_file_header = 0;

    if (lseek(read_fd, 0, SEEK_SET) < 0) {
        printf("[read_Det_data] 错误: 无法重置文件指针\n");
        goto cleanup;
    }
    if (read(read_fd, &file_header, sizeof(file_header)) == (ssize_t)sizeof(file_header)) {
        if (file_header.magic == FILE_MAGIC_WORD) {
            /* 二次校验: framesNum 合理 && 紧跟其后是合法帧 magicWord */
            if (file_header.framesNum > 0 && file_header.framesNum < 1000000u) {
                uint16_t next_magic[4];
                if (lseek(read_fd, (off_t)sizeof(MmwSaveDataHeader_t), SEEK_SET) ==
                    (off_t)sizeof(MmwSaveDataHeader_t) &&
                    read(read_fd, next_magic, sizeof(next_magic)) == (ssize_t)sizeof(next_magic) &&
                    next_magic[0] == 0x0102 && next_magic[1] == 0x0304 &&
                    next_magic[2] == 0x0506 && next_magic[3] == 0x0708) {
                    has_file_header = 1;
                    data_start = sizeof(MmwSaveDataHeader_t);
                    printf("[read_Det_data] 检测到合法文件头, 跳过%u字节, framesNum=%u, 目标frameNumber=%u\n",
                           (unsigned)sizeof(MmwSaveDataHeader_t), file_header.framesNum, frameID);
                }
            }
        }
        if (!has_file_header) {
            /* 无文件头: 文件偏移 0 即为第一帧起始 */
            printf("[read_Det_data] 未检测到文件头 (magic=0x%08x framesNum=%u), 按无文件头从偏移0开始\n",
                   file_header.magic, file_header.framesNum);
            data_start = 0;
        }
    } else {
        printf("[read_Det_data] 文件太小无法读取文件头，按无文件头处理\n");
    }

    /* === 第二步: 构建/复用帧索引表，查找 frameNumber == frameID 的帧 === */
    frame_index_check_rebuild_needed(read_filename, file_size);

    /* 首次建表：设置扫描起点为 data_start */
    if (g_frame_index_cnt == 0 && g_frame_index_end < data_start) {
        g_frame_index_end = data_start;
    }

    FrameIndexEntry_t target_entry;
    int hit = frame_index_lookup(frameID, &target_entry);

    if (!hit) {
        /* 当前记录数 < frameID -> 需继续向前扫描; 若已到文件尾则直接失败 */
        printf("[read_Det_data] 索引表不足: 当前仅%u帧记录, 请求第%u帧(已扫描到%ld), 继续向前扫描...\n",
               g_frame_index_cnt, frameID, (long)g_frame_index_end);
        if (g_frame_index_end >= file_size) {
            printf("[read_Det_data] 错误: 已扫描到文件尾仍不足%u帧, 文件共%u帧记录\n",
                   frameID, g_frame_index_cnt);
            if (g_frame_index_cnt > 0) {
                printf("[read_Det_data] 已记录帧号范围: %u ~ %u\n",
                       g_frame_index[0].frameNumber,
                       g_frame_index[g_frame_index_cnt - 1].frameNumber);
            }
            goto cleanup;
        }
        int sres = frame_index_scan_forward(read_fd, file_size, data_start,
                                            (uint32_t)-1, NULL);
        if (sres < 0) {
            printf("[read_Det_data] 错误: 扫描过程中读取失败, 已重置索引表\n");
            frame_index_reset();
            goto cleanup;
        }
        /* 扫描完成后再次尝试 lookup */
        if (!frame_index_lookup(frameID, &target_entry)) {
            printf("[read_Det_data] 错误: 扫描到文件尾仍不足%u帧 (当前共%u帧记录)\n",
                   frameID, g_frame_index_cnt);
            if (g_frame_index_cnt > 0) {
                printf("[read_Det_data] 已记录帧号范围: %u ~ %u\n",
                       g_frame_index[0].frameNumber,
                       g_frame_index[g_frame_index_cnt - 1].frameNumber);
            }
            goto cleanup;
        }
        hit = 1;
    }

    frame_offset = target_entry.file_offset;
    printf("[read_Det_data] 第%u帧定位成功: frameNumber=%u, 文件偏移=%ld, totalPacketLen=%u (已记录%u帧)\n",
           frameID, target_entry.frameNumber, (long)frame_offset,
           target_entry.packetLen, g_frame_index_cnt);
    DetFrameNum = target_entry.frameNumber;

    /* === 第三步: 读取目标帧完整 header 以确认 magic 与帧信息 === */
    Mmw_output_message_header frame_header;
    if (lseek(read_fd, frame_offset, SEEK_SET) != frame_offset) {
        printf("[read_Det_data] 错误: 无法定位到目标帧起始位置\n");
        goto cleanup;
    }
    if (read(read_fd, &frame_header, sizeof(frame_header)) != (ssize_t)sizeof(frame_header)) {
        printf("[read_Det_data] 错误: 读取目标帧 header 失败\n");
        goto cleanup;
    }
    if (frame_header.magicWord[0] != 0x0102 || frame_header.magicWord[1] != 0x0304 ||
        frame_header.magicWord[2] != 0x0506 || frame_header.magicWord[3] != 0x0708) {
        printf("[read_Det_data] 错误: 目标帧 header magic word 不匹配\n");
        goto cleanup;
    }
    printf("[read_Det_data] 目标帧 header: totalPacketLen=%u, 帧号=%u, numTLVs=%u\n",
           frame_header.totalPacketLen, frame_header.frameNumber, frame_header.numTLVs);

    /* === 第四步: 帧内遍历 TLV 查找 DETINFO === */
    off_t tlv_offset = frame_offset + sizeof(Mmw_output_message_header);
    off_t frame_end  = frame_offset + frame_header.totalPacketLen;
    uint8_t numTLVs  = frame_header.numTLVs;
    uint32_t tlv_type, tlv_length;
    int found_detinfo = 0;

    for (i = 0; i < numTLVs; i++) {
        if (tlv_offset + 8 > frame_end) {
            printf("[read_Det_data] 错误: TLV 遍历超出帧范围\n");
            goto cleanup;
        }
        if (lseek(read_fd, tlv_offset, SEEK_SET) != tlv_offset) {
            printf("[read_Det_data] 错误: 无法定位到 TLV 头\n");
            goto cleanup;
        }
        if (read(read_fd, &tlv_type, 4) != 4 || read(read_fd, &tlv_length, 4) != 4) {
            printf("[read_Det_data] 错误: 读取 TLV 头失败\n");
            goto cleanup;
        }

        if (tlv_type == (uint32_t)MMW_OUTPUT_MSG_MOTORCYCLE_DETINFO) {
            found_detinfo = 1;
            off_t payload_offset = tlv_offset + 8;
            size_t read_size = (tlv_length < buffer_size) ? tlv_length : buffer_size;
            if (lseek(read_fd, payload_offset, SEEK_SET) != payload_offset) {
                printf("[read_Det_data] 错误: 无法定位到 DETINFO payload\n");
                goto cleanup;
            }
            ssize_t bytes_read = read(read_fd, buffer, read_size);
            if (bytes_read < 0) {
                printf("[read_Det_data] 错误: 读取 DETINFO payload 失败\n");
                goto cleanup;
            }
            printf("[read_Det_data] 成功读取 DETINFO payload: %zd 字节 (位置: %ld)\n",
                   bytes_read, (long)payload_offset);
            ret = bytes_read;
            goto cleanup;
        }

        tlv_offset += 8 + tlv_length;
    }

    if (!found_detinfo) {
        printf("[read_Det_data] 错误: 帧中未找到 DETINFO TLV (numTLVs=%u)\n", numTLVs);
    }

cleanup:
    if (read_fd >= 0) {
        close(read_fd);
    }
    return ret;
}

#if ZU3_FORMAT_DATA_REPLAY == 1
static int zu3_adc_fd = -1;
/* ZU3 帧索引：一次扫描构建，后续 O(1) 直接定位 */
#define ZU3_MAX_FRAME_INDEX 640
static off_t zu3_frame_adc_offset[ZU3_MAX_FRAME_INDEX]; /* 每帧 ADC 数据的文件偏移 */
static uint32_t zu3_frame_adc_len[ZU3_MAX_FRAME_INDEX]; /* 每帧 ADC 数据长度 */
static uint32_t zu3_total_frames = 0;
static int zu3_index_ready = 0; /* 索引是否已构建 */

/**
 * @brief 初始化 ZU3 ADC 数据文件：打开文件 + 构建帧索引
 *
 * 在开始读取帧数据前调用一次，打开 ZU3 格式文件并扫描所有帧，
 * 并建立帧偏移索引表.
 *
 * @param filename ZU3 格式文件路径
 * @return int     成功返回 0；失败返回 -1
 */
int zu3_adc_init(const char *filename)
{
    if (!filename) {
        printf("[zu3_adc_init] 错误: 无效参数\n");
        return -1;
    }

    /* 打开文件 */
    zu3_adc_fd = open(filename, O_RDONLY);
    if (zu3_adc_fd < 0) {
        printf("[zu3_adc_init] 错误: 无法打开文件 %s\n", filename);
        return -1;
    }
    printf("[zu3_adc_init] 文件已打开 (fd=%d)\n", zu3_adc_fd);

    /* 获取文件总大小 */
    // off_t file_size = lseek(zu3_adc_fd, 0, SEEK_END);
    off_t file_size = 100 * 1024 * 1024;
    // printf("[zu3_adc_init] 文件大小: %ld 字节\n", (long)file_size);
    // if (file_size <= (off_t)FILE_HEADER_BYTES) {
    //     printf("[zu3_adc_init] 错误: 文件大小 %ld 不足 ZU3 文件头 %d 字节\n", (long)file_size, FILE_HEADER_BYTES);
    //     close(zu3_adc_fd);
    //     zu3_adc_fd = -1;
    //     return -1;
    // }

    /* 跳过文件头 (25712 字节)，逐帧扫描构建索引 */
    off_t scan_off = (off_t)FILE_HEADER_BYTES;
    uint32_t count = 0;

    /*
     * ZU3 帧结构 (无需遍历 TLV，tag 就是 ADC 标识，payload 就是原始 ADC 数据):
     *   [PACKET_HEADER_BYTES 包头部 (76B)]
     *       ├── 前 68 字节: 头部字段
     *       ├── 4 字节: tag   = ADC 类型标识
     *       └── 4 字节: payloadLen = 原始 chirp ADC 数据总长度
     *   [payloadLen 字节] : 原始 chirp ADC 数据
     *       └── 直接送入 extract_ADCData_from_buffer() 解析
     *   [对齐填充: packetAlignBytes + PACKET_PAD_ZERO_BYTES]
     */
    while (scan_off < file_size && count < ZU3_MAX_FRAME_INDEX) {
        if (scan_off + (off_t)PACKET_HEADER_BYTES > file_size)
        {
            printf("[zu3_adc_init] 警告: 帧 %u 偏移 %ld 超出文件大小，终止扫描\n",
                   count, (long)scan_off);
            break;
        }

        /* 读取 payloadLen（最后 4 字节）算出跳过长度 */
        if (lseek(zu3_adc_fd, scan_off + PACKET_HEADER_BYTES - 4, SEEK_SET) !=
            scan_off + PACKET_HEADER_BYTES - 4)
        {
            printf("[zu3_adc_init] 错误: 无法定位到 payloadLen 字段\n");
            break;
        }

        uint32_t plen;
        if (read(zu3_adc_fd, &plen, sizeof(uint32_t)) != (ssize_t)sizeof(uint32_t))
        {
            printf("[zu3_adc_init] 错误: 无法读取 payloadLen 字段\n");
            break;
        }
        if (plen == 0)
        {
            printf("[zu3_adc_init] 警告: 帧 %u 偏移 %ld 无有效 payloadLen，跳过\n",
                   count, (long)scan_off);
            break;
        }

        zu3_frame_adc_offset[count] = scan_off + (off_t)PACKET_HEADER_BYTES;
        zu3_frame_adc_len[count]    = plen;
        count++;

        int align = 16 - ((PACKET_HEADER_BYTES + plen) % 16);
        if (align == 16) align = 0;
        scan_off += (off_t)PACKET_HEADER_BYTES + plen + align + PACKET_PAD_ZERO_BYTES;
    }

    zu3_total_frames = count;
    zu3_index_ready  = 1;
    printf("[zu3_adc_init] 帧索引构建完成: 共 %u 帧\n", count);
    return 0;
}

/**
 * @brief 从已打开的 ZU3 文件中读取指定帧的 ADC 数据
 *
 * 调用前须先通过 zu3_adc_init() 完成文件打开和索引构建。
 * 内部通过帧索引表 O(1) 直接定位，无逐帧扫描开销。
 *
 * @param frameID    要读取的帧号 (1-based, 即 1 = 文件中第 1 帧)
 * @param buffer     输出缓冲区，用于接收 ADC 原始 chirp 数据
 * @param buffer_size 缓冲区最大字节数
 * @return int       成功返回 ADC 数据有效字节数；失败返回 -1
 */
int read_zu3_adc_frame_data(uint32_t frameID, void *buffer, uint32_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        printf("[read_zu3_adc_frame_data] 错误: 无效参数\n");
        return -1;
    }

    if (zu3_adc_fd < 0 || !zu3_index_ready) {
        printf("[read_zu3_adc_frame_data] 错误: 未初始化，请先调用 zu3_adc_init()\n");
        return -1;
    }

    if (frameID == 0 || frameID > zu3_total_frames) {
        printf("[read_zu3_adc_frame_data] 错误: 帧号 %u 超出范围 (共 %u 帧)\n",
               frameID, zu3_total_frames);
        return -1;
    }

    /* O(1) 直接定位到目标帧的 ADC 数据 */
    off_t adc_offset = zu3_frame_adc_offset[frameID - 1];
    uint32_t adc_len    = zu3_frame_adc_len[frameID - 1];

    /* 读取原始 chirp ADC 数据 */
    uint32_t read_size = (adc_len < buffer_size) ? adc_len : buffer_size;
    if (lseek(zu3_adc_fd, adc_offset, SEEK_SET) != adc_offset) {
        printf("[read_zu3_adc_frame_data] 错误: 无法定位到 ADC 数据 (offset=%ld)\n", (long)adc_offset);
        return -1;
    }
    ssize_t bytes_read = read(zu3_adc_fd, buffer, read_size);
    if (bytes_read < 0) {
        printf("[read_zu3_adc_frame_data] 错误: 读取 ADC 数据失败\n");
        return -1;
    }

    // printf("[read_zu3_adc_frame_data] 成功: 帧 %u 读取 ADC 数据 %zd 字节\n", frameID, bytes_read);

    return (int)bytes_read;
}

/**
 * @brief 关闭 ZU3 ADC 数据文件并重置帧索引
 *
 */
void zu3_close_adc_file(void)
{
    if (zu3_adc_fd >= 0) {
        close(zu3_adc_fd);
        printf("[zu3_close_adc_file] 文件已关闭 (fd=%d)\n", zu3_adc_fd);
        zu3_adc_fd = -1;
    }
    zu3_index_ready  = 0;
    zu3_total_frames = 0;
}

#endif