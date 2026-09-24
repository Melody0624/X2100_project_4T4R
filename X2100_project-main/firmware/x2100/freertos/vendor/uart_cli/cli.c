/**************************************************************************
 *************************** Include Files ********************************
 **************************************************************************/

/* Standard Include Files. */
#include "cli.h"
#include "radar_types.h"
#include "memory_pool.h"
#include "../config_manager.h"
#include "../eol_cal/eol_calibration.h"

#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>
#include <dfs_posix.h>
#include <frames_save.h>
// #include <arpa/inet.h>
// #include <signal.h>
// #include <sys/time.h> 
// #include <unistd.h>
// #include <stdio.h>
// #include <time.h>

#include "driver/uart.h"
#include <sched.h>  // 添加CPU亲和性支持
#include <os/freertos/include/FreeRTOS.h>
#include <os/freertos/include/task.h>
#include "../../freertos/include/driver/uart_console.h"
#include <os/freertos/include/semphr.h>
#include "../config_manager.h"
#include "ota_trigger_ab.h"

extern void restart_cheetah(void);
extern void update_frmCnt(int input_cnt);
extern void write_dump_file_name(const char* name);
extern float cheetah_get_temperature(int devidx);
extern int uart_general_send(char *buf, int size);
extern int gadget_serial_write(const uint8_t *buf, uint32_t count, uint32_t block, uint32_t timeout_ms);

static TaskHandle_t uart_rec_task_handle = NULL;
static int uart_rec_task_running = 0;
static int uart_rec_task_init(void);
static void uart_rec_task(void *pvParameters);
static SemaphoreHandle_t uart_rec_sem = NULL;  /* FreeRTOS信号量句柄 */

// 持久的命令行缓冲区（用于保存多次接收的长命令）
#define PERSISTENT_LINE_BUF_SIZE  8192
static uint8_t persistent_line_buf[PERSISTENT_LINE_BUF_SIZE];
static uint32_t persistent_line_index = 0;

static struct uart_config uart2_cfg = {
    .uart_id = CONFIG_UART_CONSOLE_UART_ID,
    .data_bits = 8,
    .stop_bits = 1,
    .loop_mode = 0,
    .tx_poll_mode = 1,
#ifdef CONFIG_UART_CONSOLE_RX_MODE_POLL
    .rx_poll_mode = 1,
#else
    .rx_poll_mode = 0,
#endif
    .parity = UART_PARITY_NONE,
    .follow_contrl = UART_FC_NONE,
    .baud_rate = CONFIG_UART_CONSOLE_BAUD_RATE,
};

#define READ_LINE_BUFSIZE   	    2048
#define SPI_DATA_BUF_SIZE	        512
uint32_t spi_data_buf[SPI_DATA_BUF_SIZE];
// static struct reg_line cheetah_Studio_config[600];

#if USE_SAVE_ANGLE
/*
* Function: setAngle
* Input: argc - int - 0 on success, -1 on failure
* Output: Always successful (0)
* Description: Sets the angle of the radar.
 */
static int32_t setAngle(int32_t argc, char *argv[])
{
    int32_t angle = strtol(argv[1], NULL, 10);
    set_save_angle(angle);
    printf("Cheetah idx %d angle: %d\r\n", 0, angle);

    return 0;
}
#endif

static int32_t MagAngFFT(int32_t argc, char *argv[])
{
    float l_angFFT[128] = {0.0f}; // 128个float的数组
    char buffer[256] = {0}; // 用于格式化字符串的缓冲区
    
    if (argc < 2) {
        usb_serial_send((char *)"Usage: angFFT read | angFFT write v0 v1 ... v127\n", strlen("Usage: angFFT read | angFFT write v0 v1 ... v127\n"));
        return -1;
    }
    
    if (strcmp(argv[1], "read") == 0) {
        if (param_get(PARAM_ANG_FFT_CFG, l_angFFT, sizeof(l_angFFT)) == 0) {
            usb_serial_send((char *)"angFFT:\n", strlen("angFFT:\n"));
            for (int i = 0; i < 128; i++) {
                snprintf(buffer, sizeof(buffer), "  [%d] %.6ff%s\n", i, l_angFFT[i], (i < 127) ? "," : "");
                usb_serial_send(buffer, strlen(buffer));
            }
            usb_serial_send((char *)"angFFT read done\n", strlen("angFFT read done\n"));
            return 0;
        } else {
            usb_serial_send((char *)"Failed to read angFFT config!\n", strlen("Failed to read angFFT config!\n"));
            return -1;
        }
    } else if (strcmp(argv[1], "write") == 0) {
        int count = 0;
        // 简单解析：每个 argv[i] 是一个 float（空格分隔）
        for (int i = 2; i < argc && count < 128; i++) {
            float val = atof(argv[i]);
            l_angFFT[count++] = val;
        }
        
        if (count != 128) {
            snprintf(buffer, sizeof(buffer), "Error: Need 128 float values, got %d\n", count);
            usb_serial_send(buffer, strlen(buffer));
            usb_serial_send((char *)"Usage: angFFT write v0 v1 ... v127\n", strlen("Usage: angFFT write v0 v1 ... v127\n"));
            return -1;
        }
        
        if (param_set(PARAM_ANG_FFT_CFG, l_angFFT, sizeof(l_angFFT)) == 0) {
            usb_serial_send((char *)"angFFT config updated successfully.\n", strlen("angFFT config updated successfully.\n"));
            usb_serial_send((char *)"angFFT write done\n", strlen("angFFT write done\n"));
            return 0;
        } else {
            usb_serial_send((char *)"Failed to update angFFT config!\n", strlen("Failed to update angFFT config!\n"));
            return -1;
        }
    } else {
        snprintf(buffer, sizeof(buffer), "Unknown subcommand: %s\n", argv[1]);
        usb_serial_send(buffer, strlen(buffer));
        usb_serial_send((char *)"Usage: angFFT read | angFFT write v0 v1 ... v127\n", strlen("Usage: angFFT read | angFFT write v0 v1 ... v127\n"));
        return -1;
    }
}

static int32_t MagAngCalibMat(int32_t argc, char *argv[])
{
    float l_angCalibMat[16] = {0.0f}; // 8个复数，每个2个float
    char buffer[256] = {0}; // 用于格式化字符串的缓冲区
    
    if (argc < 2) {
        usb_serial_send((char *)"Usage: angCalibMat read | angCalibMat write { r0, i0 }, { r1, i1 }, ...\n", strlen("Usage: angCalibMat read | angCalibMat write { r0, i0 }, { r1, i1 }, ...\n"));
        return -1;
    }
    
    if (strcmp(argv[1], "read") == 0) {
        if (param_get(PARAM_ANG_CALIB_MAT, l_angCalibMat, sizeof(l_angCalibMat)) == 0) {
            usb_serial_send((char *)"angCalibMat:\n", strlen("angCalibMat:\n"));
            for (int i = 0; i < 8; i++) {
                // 改用 %.6f 或 %f，不要用 %.12f
                snprintf(buffer, sizeof(buffer), "  { %.6ff, %.6ff }\n", l_angCalibMat[i*2], l_angCalibMat[i*2+1]);
                usb_serial_send(buffer, strlen(buffer));
            }
            return 0;
        }
    } else if (strcmp(argv[1], "write") == 0) {    int count = 0;
        // 清零数组
        memset(l_angCalibMat, 0, sizeof(l_angCalibMat));
        
        // 简单解析：每个 argv[i] 是一个 float
        for (int i = 2; i < argc && count < 16; i++) {
            float val = atof(argv[i]);
            l_angCalibMat[count++] = val;
        }
        
        if (count != 16) {
            snprintf(buffer, sizeof(buffer), "Error: Need 16 float values, got %d\n", count);
            usb_serial_send(buffer, strlen(buffer));
            return -1;
        }
        
        // 写入 flash
        if (param_set(PARAM_ANG_CALIB_MAT, l_angCalibMat, sizeof(l_angCalibMat)) == 0) {
            usb_serial_send((char *)"OK\n", strlen("OK\n"));
            return 0;
        } else {
            usb_serial_send((char *)"Failed\n", strlen("Failed\n"));
            return -1;
        }
    } else {
        snprintf(buffer, sizeof(buffer), "Unknown subcommand: %s\n", argv[1]);
        usb_serial_send(buffer, strlen(buffer));
        usb_serial_send((char *)"Usage: angCalibMat read | angCalibMat write { r0, i0 }, { r1, i1 }, ...\n", strlen("Usage: angCalibMat read | angCalibMat write { r0, i0 }, { r1, i1 }, ...\n"));
        return -1;
    }
}

/*
* Function: getTemperature
* Input: argc - int - 0 on success, -1 on failure
* Output: Always successful (0)
* Description: Gets the temperature of the Cheetah device.
 */
static int32_t getTemperature(int32_t argc, char *argv[])
{
    float temperature = cheetah_get_temperature(0);
    printf("Cheetah idx %d temperature: %.2f°C\r\n", 0, temperature);

    return 0;
}

/*
* Function: getBoardVersion
* Input: argc - int - 0 on success, -1 on failure
* Output: Always successful (0)
* Description: Gets the version of the Cheetah device.
 */
static int32_t getBoardVersion(int32_t argc, char *argv[])
{
    // printf("execute cli command %s \r\n", __FUNCTION__);

    // uart_send(&uart2_cfg, (uint8_t *)argv[0], strlen(argv[0]));
    usb_serial_send((char *)"getBoardVersion", strlen("getBoardVersion"));
    usb_serial_send((char *)"\n\n", strlen("\n\n"));
    
    char version_str[20];
    snprintf(version_str, sizeof(version_str), "%d", X2000_VERSION_INFO);
    usb_serial_send((char *)version_str, strlen(version_str));
    

    usb_serial_send((char *)"\r\nDone\r\nmmwDemo:/>\r\n", strlen("\r\nDone\r\nmmwDemo:/>\r\n"));

	restart_cheetah();

    return 0;
}

extern void led_test(void);
extern void buzzer_test(void);

static int32_t dev_test_cmd(int32_t argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: DevTest <1|2>\n");
        printf("  1: Test LED\n");
        printf("  2: Test Buzzer\n");
        return -1;
    }
    int mode = atoi(argv[1]);
    if (mode == 1) {
        printf("Starting LED test...\n");
        led_test();
    } else if (mode == 2) {
        printf("Starting Buzzer test...\n");
        buzzer_test();
    } else {
        printf("Invalid param: %d. Use 1 for LED, 2 for Buzzer.\n", mode);
    }
    return 0;
}

static int32_t uds_cmd(int32_t argc, char *argv[])
{
    uint8_t req_data[8] = {0};
    uint8_t resp_data[8] = {0};
    uint8_t resp_len = 0;

    if (argc < 2 || argc > 9) {
        char err_msg[] = "Error: UDS payload length must be 1-8 bytes.\r\n";
        uart_general_send(err_msg, strlen(err_msg));
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        req_data[i - 1] = (uint8_t)strtol(argv[i], NULL, 16);
    }
    uint8_t req_len = argc - 1;

    eol_uds_process_request(req_data, req_len, resp_data, &resp_len);

    if (resp_len > 0) {
        char resp_str[128];
        int offset = snprintf(resp_str, sizeof(resp_str), "[UDS_RESP] ");
        for (int i = 0; i < resp_len; i++) {
            offset += snprintf(resp_str + offset, sizeof(resp_str) - offset, "%02X ", resp_data[i]);
        }
        snprintf(resp_str + offset, sizeof(resp_str) - offset, "\r\n");
        uart_general_send(resp_str, strlen(resp_str));
    }

    return 0;
}

/*
 * Function: otaUpgrade_cmd
 * Input: argc - int - 0 on success, -1 on failure
 * Output: Always successful (0)
 * Description: OTA upgrade command (USB CDC path).
 */
static int32_t otaUpgrade_cmd(int32_t argc, char *argv[])
{
    // printf("otaUpgrade_cmd\n");

    /* OTA 固件升级命令
     * 此命令来自 USB CDC 虚拟串口，传入 OTA_TRANSPORT_USB */
    if (ota_cmd_handler(argc, argv, OTA_TRANSPORT_USB) != 0) {
        return -1;
    }

    // 不再在此处返回 OK 响应 ，由 OTA 线程准备启动前发送
    // 因为主线程可能也在发送数据，导致两者同时占用，发送失败

    return 0;
}

static int32_t set_calib_ang_cmd(int32_t argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: setCalibAng <angle_float>\n");
        return -1;
    }
    float angle = strtof(argv[1], NULL);
    param_set(PARAM_CALIB_RESULT_ANG, &angle, sizeof(float));
    printf("PARAM_CALIB_RESULT_ANG set to %.2f\n", angle);
    return 0;
}

static int32_t set_is_calib_install_ang_cmd(int32_t argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: SetIsCalibAng <0_or_1>\n");
        return -1;
    }
    uint8_t flag = (uint8_t)strtol(argv[1], NULL, 10);
    param_set(PARAM_IS_CALIB_INSTALL_ANG, &flag, sizeof(uint8_t));
    printf("PARAM_IS_CALIB_INSTALL_ANG set to %d\n", flag);
    return 0;
}

static int32_t set_radar_install_ang_cmd(int32_t argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: SetRadarAng <angle_float>\n");
        return -1;
    }
    float angle = strtof(argv[1], NULL);
    param_set(PARAM_RADAR_INSTALL_ANG, &angle, sizeof(float));
    printf("PARAM_RADAR_INSTALL_ANG set to %.2f\n", angle);
    return 0;
}

static int32_t SetFrameCnt(int32_t argc, char *argv[])
{
    int cnt = strtol(argv[1], NULL, 10);
    update_frmCnt(cnt);
    return 0;
}

/*
* Function: SetDumpFileName
* Input: argc - int - 0 on success, -1 on failure
* Output: Always successful (0)
* Description: Sets the dump file name.
 */
static int32_t SetDumpFileName(int32_t argc, char *argv[])
{
    write_dump_file_name(argv[1]);
    return 0;
}


/*
* Function: CheetahResetRams
* Input: argc - int - 0 on success, -1 on failure
* Output: Always successful (0)
* Description: Resets the RAMs of the Cheetah device.
 */
static int32_t CheetahResetRams(int32_t argc, char *argv[])
{
    int retVal = 0;
    uint16_t addr = 0x0000;
    uint32_t datalen = 97;
    uint32_t i;
    uint32_t idx;

    // printf("execute cli command %s \r\n", __FUNCTION__);

    char cmd_str[100] = {0};
    // uart_send(&uart2_cfg, (uint8_t *)"ResetRams ", strlen("ResetRams "));
    snprintf(cmd_str, sizeof(cmd_str), "%s %s\n", argv[0], argv[1]);
    usb_serial_send((char *)cmd_str, strlen(cmd_str));

    idx = strtol(argv[1], NULL, 10);

    // if(mergeDispatchFlag == MD_ENABLE)
    // {
    //     cheetah_Studio_config[config_num].addr = CMD_RESET_RAMS;
    //     cheetah_Studio_config[config_num].chipIdx = idx;
    //     cheetah_Studio_config[config_num].valLen = 1;
    //     cheetah_Studio_config[config_num].value[0] = 0x00000000;
    //     config_num ++;
    // }
    // else if (mergeDispatchFlag == SD_ENABLE)
    // {
        uint32_t *pdata = spi_data_buf;
        memset((void*) pdata, 0, sizeof(uint32_t) * 16);

        for (i = 0; i < datalen; i++)
        {
        retVal = cheetah_reg_write_burst(idx, addr + (i * 16), pdata, 16, CHEETAH_CHECK); 
        }
    // }


    if (retVal < 0)
    {
        char error_str[100];
        snprintf(error_str, sizeof(error_str), "Error: spi transmit to idx%d error\r\n", idx);
        usb_serial_send((char *)error_str, strlen(error_str));
    }
    else
    {
        usb_serial_send((char *)"\n\n", strlen("\n\n"));
        char success_str[100];
        snprintf(success_str, sizeof(success_str), "idx%d, Start at %#04hx, write %d bytes finish\r\n", idx, addr, datalen);
        usb_serial_send((char *)success_str, strlen(success_str));
        usb_serial_send((char *)"\r\nDone\r\nmmwDemo:/>\r\n", strlen("\r\nDone\r\nmmwDemo:/>\r\n"));
    }
    return retVal;
}

/*
* Function: ardardelay
* Input: argc - int - 0 on success, -1 on failure
* Output: Always successful (0)
* Description: Delays for the specified delay in milliseconds.
 */
static int32_t ardardelay(int32_t argc, char *argv[])
{
    int delay;

    delay = strtol(argv[1], NULL, 10);
    char cmd_str[100] = {0};
    // usb_serial_send((uint8_t *)"delay ", strlen("delay "));
    snprintf(cmd_str, sizeof(cmd_str), "%s %s\n", argv[0], argv[1]);
    usb_serial_send((char *)cmd_str, strlen(cmd_str));
    usb_serial_send((char *)"\r\nDone\r\nmmwDemo:/>\r\n", strlen("\r\nDone\r\nmmwDemo:/>\r\n"));
    // if(mergeDispatchFlag == MD_ENABLE)
    // {
    //     cheetah_Studio_config[config_num].addr = CMD_DELAY;
    //     cheetah_Studio_config[config_num].chipIdx = 0;
    //     cheetah_Studio_config[config_num].valLen = 1;
    //     cheetah_Studio_config[config_num].value[0] = delay;
    //     config_num ++;
    // }
    // else if (mergeDispatchFlag == SD_ENABLE)
    // {
        usleep(delay * 1000);
    // }

    // printf("execute cli command %s \r\n", __FUNCTION__); 
    return 0;
}

/*
* Function: read_regs_cmd
* Input: argc - int - 0 on success, -1 on failure
* Output: Always successful (0)
* Description: Reads the specified register values.
 */
static int32_t read_regs_cmd(int32_t argc, char *argv[])
{
    int retVal = 0;
    uint16_t addr;
    uint32_t datalen = 0;
    uint32_t i;
    uint32_t idx;

    /* Sanity Check: argument check */
    if ((argc != 3) && (argc != 4))
    {
        char error_str[200] = {0};
        snprintf(error_str, sizeof(error_str), "Error: Invalid usage of the CLI read register command, use 'help' command to learn more\r\n");
        usb_serial_send((char *)error_str, strlen(error_str));
        return -1;
    }

    // printf("execute cli command %s \r\n", __FUNCTION__);

    /* get the idx and address strings */
    idx = strtol(argv[1], NULL, 10);
    addr = strtol(argv[2], NULL, 16);
    if (argc == 4)
    {
    	datalen = strtol(argv[3], NULL, 10);

        if (datalen > SPI_DATA_BUF_SIZE)
        {
            char error_str[200];
            snprintf(error_str, sizeof(error_str), "Error: Invalid length, not more than %d bytes per read\r\n", SPI_DATA_BUF_SIZE);
            usb_serial_send((char *)error_str, strlen(error_str));
            return -1;
        }
    }
    else
    {
    	datalen = 1;
    }

    /* read by spi */
    // if (chip_types == CHEETAH_V1 || chip_types == CHEETAH_V2)
    // {
    	uint32_t *pdata = spi_data_buf;
        memset((void*) pdata, 0, sizeof(uint32_t) * datalen);

        if (datalen == 1)
        {
            cheetah_reg_read_single(idx, addr, pdata, CHEETAH_CHECK);
        }
        else
        {
            retVal = cheetah_reg_read_burst(idx, addr, pdata, datalen, CHEETAH_CHECK);
        }
        // printf("idx%d, Start at %#04hx, read %d bytes:", idx, addr, datalen);
        // for (i = 0; i < datalen; i++)
        // {
        //     if ((i % 16) == 0)
        //     {
        //         printf("\r\n%#04x: ", addr);
        //         addr += 16;
        //     }
        //     printf("%08x ", pdata[i]);
        // }  

    	if (retVal == 0)
    	{
        /* output in cli */
             char header_str[200];
            snprintf(header_str, sizeof(header_str), "idx%d, Start at %#04hx, read %d bytes:", idx, addr, datalen);
            usb_serial_send((char *)header_str, strlen(header_str));

            uint16_t current_addr = addr;
            for (i = 0; i < datalen; i++)
            {
                if ((i % 16) == 0)
                {
                    char addr_str[100];
                    snprintf(addr_str, sizeof(addr_str), "\r\n%#04x: ", current_addr);
                    usb_serial_send((char *)addr_str, strlen(addr_str));
                    current_addr += 16;
                }
                char data_str[10];
                snprintf(data_str, sizeof(data_str), "%08x ", pdata[i]);
                usb_serial_send((char *)data_str, strlen(data_str));
            } 
            usb_serial_send((char *)"\r\n", strlen("\r\n"));  
    	}
    // }
    // else
    // {
    // 	uint8_t *pdata = (uint8_t *)spi_data_buf;
    //     memset((void*) pdata, 0, sizeof(uint8_t) * datalen);
    //     retVal = kestrel_reg_read(idx, addr, pdata, datalen);

    // 	if (retVal == 0)
    // 	{
    //     /* output in cli */
    //         printf("idx%d, Start at %#04hx, read %d bytes:", idx, addr, datalen);
    //         for (i = 0; i < datalen; i++)
    //         {
    //             if ((i % 16) == 0)
    //             {
    //                 printf("\r\n%#04x: ", addr);
    //                 addr += 16;
    //             }
    //             printf("%02x ", pdata[i]);
    //         }   
    // 	}
    //     printf("\r\n");
    // }
    
    if (retVal < 0)
    {
        printf("Error: spi transmit to radar idx%d fail\r\n", idx);
        return retVal;
    }

    return 0;
}

/*
* Function: write_regs_cmd
* Input: argc - int - 0 on success, -1 on failure
* Output: Always successful (0)
* Description: Writes the specified register values.
 */
static int32_t write_regs_cmd(int32_t argc, char *argv[])
{
    int retVal = 0;
    uint16_t addr;
    uint32_t datalen;
    uint32_t i;
    uint32_t idx;

    /* Sanity Check: argument check */
    if (argc < 4)
    {
        printf("Error: Invalid usage of the CLI write register command, use 'help' command to learn more\r\n");
        return -1;
    }
    
    // printf("execute cli command %s \r\n", __FUNCTION__);

    if (argc > (SPI_DATA_BUF_SIZE + 3))
    {
        printf("Error: Not more than %d data per write\r\n", SPI_DATA_BUF_SIZE);
        return -1;
    }

    /* get the idx, address and data strings */
    idx = strtol(argv[1], NULL, 10);
    addr = strtol(argv[2], NULL, 16);
    datalen = argc - 3;

    char cmd_str[200] = {0};
    // usb_serial_send((uint8_t *)"regSet ", strlen("regSet "));
    snprintf(cmd_str, sizeof(cmd_str), "%s %s %s %s", argv[0], argv[1], argv[2], argv[3]);
    usb_serial_send((char *)cmd_str, strlen(cmd_str));
    // if (chip_types == CHEETAH_V1 || chip_types == CHEETAH_V2)
    // {
    	uint32_t *pdata = spi_data_buf;
        memset((void*) pdata, 0, sizeof(uint32_t) * datalen);

        for (i = 0; i < datalen; i++)
        {
        	char *endptr;
        	unsigned long temp_val = strtoul(argv[i + 3], &endptr, 16);
        	
        	/* 检查解析是否成功 */
        	if (endptr == argv[i + 3] || *endptr != '\0')
        	{
        	    printf("Error: Invalid hexadecimal value: %s\r\n", argv[i + 3]);
        	    return -1;
        	}
        	
        	pdata[i] = (uint32_t)temp_val;
            // printf("argv[i + 3] = %s, pdata[i] = 0x%x\r\n", argv[i + 3], pdata[i]);
        }
        // printf("argv[0] = %s, argv[1] = %s, argv[2] = %s, argv[3] = %s\r\n", argv[0], argv[1], argv[2], argv[3]);

        // if(mergeDispatchFlag == MD_ENABLE)
        // {
        //     cheetah_Studio_config[config_num].addr = addr;
        //     cheetah_Studio_config[config_num].chipIdx = idx;
        //     cheetah_Studio_config[config_num].valLen = datalen;
 
        //     for (i = 0; i < datalen; i++)
        //     {
        //         cheetah_Studio_config[config_num].value[i] = strtol(argv[i + 3], NULL, 16);
        //     }
        //     config_num ++;

        // }
        // else if (mergeDispatchFlag == SD_ENABLE)
        // {
            if (datalen == 1)
            {
                cheetah_reg_write_single(idx, addr, pdata[0], CHEETAH_CHECK); 
            }
            else
            {
                retVal = cheetah_reg_write_burst(idx, addr, pdata, datalen, CHEETAH_CHECK);
            }
        // }

        // if( addr == 0x1051)
        // {
        //     link_freq_idx = (pdata[0] >> 6)&0x02;
        // }

    // }
    // else
    // {
    // 	uint8_t *pdata = (uint8_t *)spi_data_buf;
    //     memset((void*) pdata, 0, sizeof(uint8_t) * datalen);
    //     for (i = 0; i < datalen; i++)
    //     {
    //     	pdata[i] = strtol(argv[i + 3], NULL, 16);
    //     }

    //     retVal = kestrel_reg_write(idx, addr, pdata, datalen);
    // }

    if (retVal < 0)
    {
        printf("Error: spi transmit to idx%d error\r\n", idx);
    }
    else
    {
        usb_serial_send((char *)"\n\n", strlen("\n\n"));
        char success_str[200];
        snprintf(success_str, sizeof(success_str), "idx%d, Start at %#04hx, write %d bytes finish\r\n", idx, addr, datalen);
        usb_serial_send((char *)success_str, strlen(success_str));
        usb_serial_send((char *)"\r\nDone\r\nmmwDemo:/>", strlen("\r\nDone\r\nmmwDemo:/>"));
    }
    return retVal;
}

static int32_t SetMaxFrameCnt(int32_t argc, char *argv[])
{
    int cnt = strtol(argv[1], NULL, 10);
    update_frmCnt(cnt);
    char msg_buf[256];

    int32_t save_val = (int32_t)cnt;
    if (param_set(PARAM_MAX_FRAME_CNT, &save_val, sizeof(save_val)) != 0)
    {
        snprintf(msg_buf, sizeof(msg_buf), "Error: Write maxFrameCnt=%d to Flash failed!\r\n", cnt);
        usb_serial_send((char *)msg_buf, strlen(msg_buf));
        return -1;
    }

    snprintf(msg_buf, sizeof(msg_buf), "OK: maxFrameCnt=%d saved to Flash (persistent)\r\n", cnt);
    usb_serial_send((char *)msg_buf, strlen(msg_buf));
    return 0;
}

/**
 * @brief 设置 outRawDataFlg 标志，控制是否输出 Raw Data
 * 用法: setRawDataFlg <0|1>
 * 注意: 仅上电时读取一次，设置后需重启生效
 */
static int32_t setRawDataFlg_cmd(int32_t argc, char *argv[])
{
    char msg_buf[256];

    if (argc < 2)
    {
        snprintf(msg_buf, sizeof(msg_buf), "Usage: setRawDataFlg <0|1>\r\n");
        usb_serial_send((char *)msg_buf, strlen(msg_buf));
        return -1;
    }

    int flg_val = strtol(argv[1], NULL, 10);

    // 如果大于1则视为无效，将outRawDataFlg设置为0
    if (flg_val > 1)
    {
        flg_val = 0;
    }

    uint8_t save_val = (uint8_t)flg_val;
    if (param_set(PARAM_OUT_RAW_DATA_FLG, &save_val, sizeof(save_val)) != 0)
    {
        snprintf(msg_buf, sizeof(msg_buf), "Error: Write outRawDataFlg=%d to Flash failed!\r\n", flg_val);
        usb_serial_send((char *)msg_buf, strlen(msg_buf));
        return -1;
    }

    snprintf(msg_buf, sizeof(msg_buf), "OK: outRawDataFlg=%d saved to Flash. Reboot to take effect.\r\n", flg_val);
    usb_serial_send((char *)msg_buf, strlen(msg_buf));
    return 0;
}

/*
* Function: write_flash_cmd
* Input: use_script - int - 0 on success, -1 on failure
* Output: Always successful (0)
* Description: Writes the specified data to the flash memory.
 */
static int32_t write_flash_cmd(int32_t argc, char *argv[])
{
    int retVal = 0;
    char sn_num[16] = {0};
    char prod_data[16] = {0};
    uint32_t memory_addr;
    int fd;
    int len;
    // int i;

    /* Sanity Check: argument check */
    if (argc < 1)
    {
        printf("Error: Invalid usage of the CLI write register command, use 'help' command to learn more\r\n");
        return -1;
    }
    
    // printf("execute cli command %s \r\n", __FUNCTION__);

    if (argc > (SPI_DATA_BUF_SIZE + 3))
    {
        printf("Error: Not more than %d data per write\r\n", SPI_DATA_BUF_SIZE);
        return -1;
    }
    
    memory_addr = strtol(argv[1], NULL, 10);
    switch(memory_addr)
    {
        case 0:
        {
            const char *test_file = "/rootfs/radar_parameters.txt";
            // const char *base_pattern = "Hello Ingenic NAND Flash! Configuration Test Data. ";
            strncpy(sn_num, argv[2], sizeof(sn_num) - 1);
            strncpy(prod_data, argv[3], sizeof(prod_data) - 1);

            fd = open(test_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd < 0)
            {
                printf("Error: open file %s error\r\n", test_file);
                return -1;
            }

            len = write(fd, sn_num, strlen(sn_num));
            if (len != strlen(sn_num))
            {
                printf("Error: write file %s error, len = %d, expect = %d\r\n", test_file, len, (int)strlen(sn_num));
                return -1;
            }

            len = write(fd, prod_data, strlen(prod_data));
            if (len != strlen(prod_data))
            {
                printf("Error: write file %s error, len = %d, expect = %d\r\n", test_file, len, (int)strlen(prod_data));
                return -1;
            }

            close(fd);

            break;
        }
        case 1:
        {
            const char *test_file = "/rootfs/radar_parameters.txt";
            fd = open(test_file, O_RDONLY, 0);
            if (fd < 0) {
                printf("[FLASH_TEST] Error: Failed to open %s for reading. fd=%d\n", test_file, fd);
                return -1;
            }
            char read_buf[1025] = {0};
            memset(read_buf, 0, sizeof(read_buf));  
            len = read(fd, read_buf, sizeof(read_buf) - 1);   
            if (len < 0) {
                printf("[FLASH_TEST] Error: Failed to read from %s. len=%d\n", test_file, len);
                close(fd);
                return -1;
            }
            read_buf[len] = '\0';
            printf("read_buf = %s\n", read_buf);
            if (strcmp(read_buf, "Hello Ingenic NAND Flash! Configuration Test Data.\n") == 0) {
                printf("[FLASH_TEST] Data verify SUCCESS! Read 1KB data matches written data exactly.\n");
            } else {
                printf("[FLASH_TEST] Data verify FAILED! Data mismatch.\n");
            }
            close(fd);
            break;
        }

        default:
        {
            printf("Error: Invalid flash memory address index, should be 0, 1, 2 or 3\r\n");
            return -1;
        }

    }


    return retVal;
}

/*
* Function: cli_init
* Input: use_script - int - 0 on success, -1 on failure
* Output: Always successful (0)
* Description: Initializes the CLI with the specified configuration.
 */
int cli_init(int use_script)
{

    // uart_rec_task_init();

    return 0;
}

/*
* Function: cli_exit
* Input: None
* Output: None
* Description: Cleans up and exits the CLI.
 */
void cli_exit(void)
{
    uart_stop(&uart2_cfg);
    // cli_close();
}

/*
* Function: uart_rec_task
* Input: None
* Output: None
* Description: UART receive task optimized for performance.
 */
// static void uart_rec_task(void *pvParameters)
// {
//     uint8_t recv_buffer[4096];  /* 增大缓冲区 */
//     uint32_t recv_index = 0;
//     int32_t ret;
    
//     printf("UART receive task started (optimized version)\n");
    
//     while (uart_rec_task_running)
//     {
//         /* 获取信号量保护UART接收操作 */
//         if (xSemaphoreTake(uart_rec_sem, portMAX_DELAY) == pdTRUE)
//         {
//             memset(recv_buffer, 0, sizeof(recv_buffer));
//             recv_index = 0u;
//             /* 批量接收数据 */
//             ret = uart_console_receive_timeout((char*)&recv_buffer[recv_index], 
//                                                 sizeof(recv_buffer) - recv_index - 1, 100);
//             // printf("recv_buffer: %s, ret: %d, recv_index: %d\n", recv_buffer, ret, recv_index);
//             /* 释放信号量 */
//             xSemaphoreGive(uart_rec_sem);
//             if (ret > 0)
//             {
//                 recv_index += ret;
//                 recv_buffer[recv_index] = '\0';  /* 确保字符串终止 */
                
//                 /* 调试信息：显示接收到的原始数据 */
//                 // printf("Raw UART data received (%d bytes): ", ret);
//                 // for (uint32_t j = 0; j < (ret < 64 ? ret : 64); j++)
//                 // {
//                 //     printf("%02X ", recv_buffer[j]);
//                 // }
//                 // printf("\n");

//                 /* 处理接收到的数据 */
//                 for (uint32_t i = 0; i < recv_index; i++)
//                 {
//                     uint8_t current_byte = recv_buffer[i];
                    
//                     /* 处理接收到的字节 */
//                     if ((current_byte == '\r') || (current_byte == '\n'))
//                     {
//                         /* 收到完整命令 */
//                         if (persistent_line_index > 0)
//                         {
//                             persistent_line_buf[persistent_line_index] = '\0';
//                             // printf("UART Received: %s\n", (char*)persistent_line_buf);
                            
//                             /* 解析并执行命令 */
//                             char *tokenizedArgs[CLI_MAX_ARGS] = {0};
//                             char *ptrCommand = NULL;
//                             char delimiter[] = " \r\n";
//                             uint32_t argIndex = 0;
                            
//                             /* 重置参数数组 */
//                             memset((void*) &tokenizedArgs, 0, sizeof(tokenizedArgs));
//                             ptrCommand = (char*) &persistent_line_buf[0];
                            
//                             /* 解析命令参数 */
//                             while (1)
//                             {
//                                 tokenizedArgs[argIndex] = strtok(ptrCommand, delimiter);
//                                 if (tokenizedArgs[argIndex] == NULL)
//                                     break;
                                
//                                 argIndex++;
//                                 if (argIndex >= CLI_MAX_ARGS)
//                                     break;
                                
//                                 ptrCommand = NULL;
//                             }
                            
//                             /* 根据命令类型调用相应的函数 */
//                             if (argIndex > 0)
//                             {
//                                 if (strcmp(tokenizedArgs[0], "readreg") == 0 || strcmp(tokenizedArgs[0], "rr") == 0 || strcmp(tokenizedArgs[0], "regGet") == 0)
//                                 {
//                                     /* 调用read_regs_cmd函数 */
//                                     // printf("Executing read_regs_cmd with %d arguments\n", argIndex);
//                                     read_regs_cmd(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "writereg") == 0 || strcmp(tokenizedArgs[0], "egSet") == 0 || strcmp(tokenizedArgs[0], "regSet") == 0)
//                                 {
//                                     /* 调用write_regs_cmd函数 */
//                                     // printf("Executing write_regs_cmd with %d arguments\n", argIndex);
//                                     write_regs_cmd(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "delay") == 0 || strcmp(tokenizedArgs[0], "elay") == 0)
//                                 {
//                                     /* 调用ardardelay函数 */
//                                     // printf("Executing ardardelay with %d arguments\n", argIndex);
//                                     ardardelay(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "ResetRams") == 0 || strcmp(tokenizedArgs[0], "esetRams") == 0)
//                                 {
//                                     /* 调用ardardelay函数 */
//                                     // printf("Executing ardardelay with %d arguments\n", argIndex);
//                                     CheetahResetRams(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "SetDumpFileName") == 0 || strcmp(tokenizedArgs[0], "etDumpFileName") == 0)
//                                 {
//                                     /* 调用ardardelay函数 */
//                                     printf("Executing SetDumpFileName with %d arguments\n", argIndex);
//                                     SetDumpFileName(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "SetFrameCnt") == 0 || strcmp(tokenizedArgs[0], "etFrameCnt") == 0)
//                                 {
//                                     /* 调用ardardelay函数 */
//                                     // printf("Executing SetFrameCnt with %d arguments\n", argIndex);
//                                     SetFrameCnt(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "getBoardVersion") == 0 || strcmp(tokenizedArgs[0], "etBoardVersion") == 0)
//                                 {
//                                     getBoardVersion(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "writeflash") == 0 || strcmp(tokenizedArgs[0], "riteflash") == 0)
//                                 {
//                                     write_flash_cmd(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "SetMaxFrameCnt") == 0 || strcmp(tokenizedArgs[0], "etMaxFrameCnt") == 0)
//                                 {
//                                     SetMaxFrameCnt(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "getTemperature") == 0 || strcmp(tokenizedArgs[0], "etTemperature") == 0)
//                                 {
//                                     getTemperature(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "SetCalibAng") == 0 || strcmp(tokenizedArgs[0], "etCalibAng") == 0)
//                                 {
//                                     set_calib_ang_cmd(argIndex, tokenizedArgs);
//                                 }
// #if USE_SAVE_ANGLE
//                                 else if (strcmp(tokenizedArgs[0], "setAngle") == 0 || strcmp(tokenizedArgs[0], "etAngle") == 0)
//                                 {
//                                     setAngle(argIndex, tokenizedArgs);
//                                 }
// #endif                          
//                                 else if (strcmp(tokenizedArgs[0], "SetIsCalibAng") == 0 || strcmp(tokenizedArgs[0], "etIsCalibAng") == 0)
//                                 {
//                                     set_is_calib_install_ang_cmd(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "SetRadarAng") == 0 || strcmp(tokenizedArgs[0], "etRadarAng") == 0)
//                                 {
//                                     set_radar_install_ang_cmd(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "DevTest") == 0 || strcmp(tokenizedArgs[0], "evTest") == 0)
//                                 {
//                                     dev_test_cmd(argIndex, tokenizedArgs);
//                                 }
//                                 else if (strcmp(tokenizedArgs[0], "uds") == 0 || strcmp(tokenizedArgs[0], "ds") == 0)
//                                 {
//                                     uds_cmd(argIndex, tokenizedArgs);
//                                 }
//                                 else
//                                 {
//                                     printf("Unknown command: %s\n", tokenizedArgs[0]);

//                                     printf("Available commands: readreg/rr, writereg/wr, delay/dl, usb_serial_test, SetCalibAng, SetIsCalibAng, SetRadarAng, uds, angCalibMat, angFFT\n");
//                                 }
//                             }
                            
//                             /* 清空行缓冲区 */
//                             persistent_line_index = 0;
//                         }
//                     }
//                     else if (current_byte == 0x08 || current_byte == 0x7F) /* Backspace or Delete */
//                     {
//                         /* 处理退格 */
//                         if (persistent_line_index > 0)
//                         {
//                             persistent_line_index--;
//                         }
//                     }
//                     else if (persistent_line_index < PERSISTENT_LINE_BUF_SIZE - 1)
//                     {
//                         /* 存储有效字符到行缓冲区 */
//                         persistent_line_buf[persistent_line_index++] = current_byte;
//                     }
//                 }
                
//                 /* 处理剩余数据（如果有） */
//                 if (recv_index > 0)
//                 {
//                     /* 将未处理的数据移动到缓冲区开头 */
//                     uint32_t processed_len = 0;
//                     for (uint32_t i = 0; i < recv_index; i++)
//                     {
//                         if (recv_buffer[i] == '\r' || recv_buffer[i] == '\n')
//                         {
//                             processed_len = i + 1;
//                         }
//                     }
                    
//                     if (processed_len > 0 && processed_len < recv_index)
//                     {
//                         /* 移动剩余数据到缓冲区开头 */
//                         memmove(recv_buffer, &recv_buffer[processed_len], recv_index - processed_len);
//                         recv_index = recv_index - processed_len;
//                     }
//                     else
//                     {
//                         /* 缓冲区已满或没有换行符，清空缓冲区 */
//                         recv_index = 0;
//                     }
//                 }
//             }
//             else if (ret == 0)
//             {
//                 /* 超时，短暂延时后继续 */
//                 vTaskDelay(pdMS_TO_TICKS(10));
//             }
//             else
//             {
//                 /* 接收错误，短暂延时后继续 */
//                 vTaskDelay(pdMS_TO_TICKS(10));
//             }
//         }

//     }
    
//     printf("UART receive task stopped\n");
//     vTaskDelete(NULL);
// }

/*
* Function: uart_rec_task_init
* Input: None
* Output: int - 0 on success, -1 on failure
* Description: Initializes the UART receive task with a semaphore to protect access.
 */
static int uart_rec_task_init(void)
{
//     if (uart_rec_task_running)
//     {
//         printf("UART receive task is already running\n");
//         return 0;
//     }
    
//     /* 创建FreeRTOS二进制信号量 */
//     uart_rec_sem = xSemaphoreCreateBinary();
//     if (uart_rec_sem == NULL)
//     {
//         printf("Failed to create UART receive semaphore\n");
//         return -1;
//     }
    
//     /* 初始释放信号量 */
//     xSemaphoreGive(uart_rec_sem);
    
//     uart_rec_task_running = 1;
    
//     if (xTaskCreate(uart_rec_task, "UART_REC", 65535, NULL, 1, &uart_rec_task_handle) != pdPASS)
//     {
//         printf("Failed to create UART receive task\n");
//         vSemaphoreDelete(uart_rec_sem);
//         uart_rec_sem = NULL;
//         return -1;
//     }
    
//     printf("UART receive task initialized successfully with semaphore protection\n");
//     return 0;
}

void CliSiganlProcess(uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        uint8_t current_byte = data[i];
        
        /* 处理接收到的字节 */
        if ((current_byte == '\r') || (current_byte == '\n'))
        {
            /* 收到完整命令 */
            if (persistent_line_index > 0)
            {
                persistent_line_buf[persistent_line_index] = '\0';
                // printf("UART Received: %s\n", (char*)persistent_line_buf);
                
                /* 解析并执行命令 */
                char *tokenizedArgs[CLI_MAX_ARGS] = {0};
                char *ptrCommand = NULL;
                char delimiter[] = " \r\n";
                uint32_t argIndex = 0;
                
                /* 重置参数数组 */
                memset((void*) &tokenizedArgs, 0, sizeof(tokenizedArgs));
                ptrCommand = (char*) &persistent_line_buf[0];
                
                /* 解析命令参数 */
                while (1)
                {
                    tokenizedArgs[argIndex] = strtok(ptrCommand, delimiter);
                    if (tokenizedArgs[argIndex] == NULL)
                        break;
                    
                    argIndex++;
                    if (argIndex >= CLI_MAX_ARGS)
                        break;
                    
                    ptrCommand = NULL;
                }
                
                /* 根据命令类型调用相应的函数 */
                if (argIndex > 0)
                {
                    if (strcmp(tokenizedArgs[0], "readreg") == 0 || strcmp(tokenizedArgs[0], "rr") == 0 || strcmp(tokenizedArgs[0], "regGet") == 0)
                    {
                        /* 调用read_regs_cmd函数 */
                        // printf("Executing read_regs_cmd with %d arguments\n", argIndex);
                        read_regs_cmd(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "writereg") == 0 || strcmp(tokenizedArgs[0], "egSet") == 0 || strcmp(tokenizedArgs[0], "regSet") == 0)
                    {
                        /* 调用write_regs_cmd函数 */
                        // printf("Executing write_regs_cmd with %d arguments\n", argIndex);
                        write_regs_cmd(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "delay") == 0 || strcmp(tokenizedArgs[0], "elay") == 0)
                    {
                        /* 调用ardardelay函数 */
                        // printf("Executing ardardelay with %d arguments\n", argIndex);
                        ardardelay(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "ResetRams") == 0 || strcmp(tokenizedArgs[0], "esetRams") == 0)
                    {
                        /* 调用ardardelay函数 */
                        // printf("Executing ardardelay with %d arguments\n", argIndex);
                        CheetahResetRams(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "SetDumpFileName") == 0 || strcmp(tokenizedArgs[0], "etDumpFileName") == 0)
                    {
                        /* 调用ardardelay函数 */
                        printf("Executing SetDumpFileName with %d arguments\n", argIndex);
                        SetDumpFileName(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "SetFrameCnt") == 0 || strcmp(tokenizedArgs[0], "etFrameCnt") == 0)
                    {
                        /* 调用ardardelay函数 */
                        // printf("Executing SetFrameCnt with %d arguments\n", argIndex);
                        SetFrameCnt(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "getBoardVersion") == 0 || strcmp(tokenizedArgs[0], "etBoardVersion") == 0)
                    {
                        getBoardVersion(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "writeflash") == 0 || strcmp(tokenizedArgs[0], "riteflash") == 0)
                    {
                        write_flash_cmd(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "SetMaxFrameCnt") == 0 || strcmp(tokenizedArgs[0], "etMaxFrameCnt") == 0)
                    {
                        SetMaxFrameCnt(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "getTemperature") == 0 || strcmp(tokenizedArgs[0], "etTemperature") == 0)
                    {
                        getTemperature(argIndex, tokenizedArgs);
                    }
#if USE_SAVE_ANGLE
                    else if (strcmp(tokenizedArgs[0], "setAngle") == 0 || strcmp(tokenizedArgs[0], "etAngle") == 0)
                    {
                        setAngle(argIndex, tokenizedArgs);
                    }
#endif
                    else if ((strcmp(tokenizedArgs[0], "angCalibMat") == 0) || (strcmp(tokenizedArgs[0], "ngCalibMat") == 0))
                    {
                        MagAngCalibMat(argIndex, tokenizedArgs);
                    }
                    else if ((strcmp(tokenizedArgs[0], "angFFT") == 0) || (strcmp(tokenizedArgs[0], "ngFFT") == 0))
                    {
                        MagAngFFT(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "DevTest") == 0 || strcmp(tokenizedArgs[0], "evTest") == 0)
                    {
                        dev_test_cmd(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "uds") == 0 || strcmp(tokenizedArgs[0], "ds") == 0)
                    {
                        uds_cmd(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "setRawDataFlg") == 0 || strcmp(tokenizedArgs[0], "etRawDataFlg") == 0)
                    {
                        setRawDataFlg_cmd(argIndex, tokenizedArgs);
                    }
                    else if (strcmp(tokenizedArgs[0], "otaUpgrade") == 0 || strcmp(tokenizedArgs[0], "ota") == 0)
                    {
                        /* OTA 固件升级命令 */
                        otaUpgrade_cmd(argIndex, tokenizedArgs);
                    }
                    else
                    {
                        printf("Unknown command: %s\n", tokenizedArgs[0]);
                        printf("Available commands: readreg/rr, writereg/wr, delay/dl, usb_serial_test, angCalibMat, angFFT, uds, setRawDataFlg, otaUpgrade/ota\n");
                    }
                }
                
                /* 清空行缓冲区 */
                persistent_line_index = 0;
            }
        }
        else if (current_byte == 0x08 || current_byte == 0x7F) /* Backspace or Delete */
        {
            /* 处理退格 */
            if (persistent_line_index > 0)
            {
                persistent_line_index--;
            }
        }
        else if (persistent_line_index < PERSISTENT_LINE_BUF_SIZE - 1)
        {
            /* 存储有效字符到行缓冲区 */
            persistent_line_buf[persistent_line_index++] = current_byte;
        }
    }
}
