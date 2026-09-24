#ifndef _RING_BUFFER_H_
#define _RING_BUFFER_H_

/**
 *                           writer
 *                           |
 * +-------------------------------------+
 * |                                     |
 * +-------------------------------------+
 *     |               |
 *     |               reader1  
 *     reader2
 *
 * 环形缓冲区用于缓冲数据,这里封装了裸的环形缓冲区操作,且有如下特点
 *   1 当写入/读取到缓冲区尾部时,又跳到首部接着写入/读取
 *   2 写入者 和 读取者分离, 可用于实现一写多读
 *   3 写入一定会成功,但有效数据由缓冲区的容量决定
 *   4 读端总是在追赶写端,但不会超过写端,所以读取的数据经常会小于实际传入的
 *   5 写入可能会覆盖原有数据,所以这需要缓冲区被及时读取
 *   6 采用无锁设计,需要用户自行考虑多线程/中断的问题
 *   7 提供针对计数/索引的操作,以用于实现环形计数器等应用
 *
 */

struct ring_buffer_writer {
    void *mem;
    unsigned int buffer_size;
    unsigned int index;
    unsigned int n;
};

struct ring_buffer_reader {
    unsigned int n;
    struct ring_buffer_writer *writer;
};

/**
 * @brief 初始化环形缓冲区写端
 * @param writer 待初始化的写端指针
 * @param mem 缓冲区实际使用的内存,可以为NULL
 * @param mem_size 缓冲区大小, 不能为 0
 */
void ring_buffer_writer_init(struct ring_buffer_writer *writer, void *mem, unsigned int mem_szie);

/**
 * @brief 初始化环形缓冲区读端
 * @param reader 待初始化的读端指针
 * @param writer 与之对应的写端指针
 */
void ring_buffer_reader_init(struct ring_buffer_reader *reader, struct ring_buffer_writer *writer);

/**
 * @brief 返回环形缓冲区已使用的大小,即未读大小
 */
unsigned int ring_buffer_used_size(struct ring_buffer_reader *reader);

/**
 * @brief 返回环形缓冲区未使用的大小,即已读大小
 */
unsigned int ring_buffer_free_size(struct ring_buffer_reader *reader);

/**
 * @brief 返回环形缓冲区容量
 */
unsigned int ring_buffer_capacity(struct ring_buffer_reader *reader);

/**
 * @brief 返回环形缓冲区容量
 */
unsigned int ring_buffer_capacity2(struct ring_buffer_writer *writer);

/**
 * @brief 增加环形缓冲区写端的计数/索引
 * @param writer 写端指针
 * @param n 增加的计数个数/字节数
 */
void ring_buffer_add_writer(struct ring_buffer_writer *writer, unsigned int n);

/**
 * @brief 像环形缓冲区写入数据
 * @param writer 写端指针
 * @param n 写入的字节数
 */
void ring_buffer_write(struct ring_buffer_writer *writer, void *src, unsigned int n);

/**
 * @brief 增加环形缓冲区读端的计数
 * @param reader 读端指针
 * @param n 增加的计数个数/字节数
 * @return 返回实际增加的个数(因为读端不能超越写端的计数,所以返回值可能小于传入的个数)
 */
unsigned int ring_buffer_add_reader(struct ring_buffer_reader *reader, unsigned int n);

/**
 * @brief 读取环形缓冲区的数据
 * @param reader 读端指针
 * @param n 读取的字节数
 * @return 返回实际增加的个数(因为读端不能超越写端的计数,所以返回值可能小于传入的个数)
 */
unsigned int ring_buffer_read(struct ring_buffer_reader *reader, void *dst, unsigned int n);

/**
 * @brief 返回环形缓冲区写端索引
 */
unsigned int ring_buffer_get_writer_index(struct ring_buffer_writer *writer);

/**
 * @brief 返回环形缓冲区读端索引
 * @note 即使未进行读取操作,读端的索引仍然可能由于写端的不断写入而发生变化
 */
unsigned int ring_buffer_get_reader_index(struct ring_buffer_reader *reader);

#endif /*  */
