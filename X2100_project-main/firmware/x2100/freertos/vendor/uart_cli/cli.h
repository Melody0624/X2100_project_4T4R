#ifndef CLI_H
#define CLI_H

#include <stdbool.h>
#include <stdint.h>

#include "cheetah.h"
#include "radar_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   This is the maximum number of CLI commands which are supported
 */
#define     CLI_MAX_CMD         32

/**
 * @brief
 *  This is the maximum number of CLI arguments which can be passed to a
 *  command.
 */
#define     CLI_MAX_ARGS        256

/**
 @}
 */

#define CLI_MAX_PARTNO_STRING_LEN          31U

/**************************************************************************
 ************************** CLI Data Structures ***************************
 **************************************************************************/

/** @addtogroup CLI_UTIL_EXTERNAL_DATA_STRUCTURE
 @{ */

/**
 * @brief   Handle to the CLI module:
 */
typedef void *CLI_Handle;

/**
 * @brief   CLI command handler:
 *
 *  @param[in]  argc
 *      Number of arguments
 *  @param[in]  argv
 *      Pointer to the arguments
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
typedef int32_t (*CLI_CmdHandler)(int32_t argc, char *argv[]);

/**
 * @brief
 *  CLI command table entry
 *
 * @details
 *  This is command entry which holds information which maps a
 *  command string to the corresponding command handler.
 */
typedef struct CLI_CmdTableEntry_t
{
    /**
     * @brief   Command string
     */
    const char *cmd;

    /**
     * @brief   CLI Command Help string
     */
    const char *helpString;

    /**
     * @brief   Command Handler to be executed
     */
    CLI_CmdHandler cmdHandlerFxn;
} CLI_CmdTableEntry;

/**
 * @brief
 *  CLI configuration
 *
 * @details
 *  This is the configuration structure which is used to initialize and open
 *  the CLI module.
 */
typedef struct CLI_Cfg_t
{
    /**
     * @brief   CLI Prompt string (if any to be displayed)
     */
    const char *cliPrompt;

    /**
     * @brief   UART Command Handle used by the CLI
     */
    struct uart_config *uartDev;

    /**
     * @brief   Flag which determines if the CLI Write should use the UART
     * in polled or blocking mode.
     */
    bool usePolledMode;

    /**
     * @brief   Flag which identifies the processing chain. 0 implies TDM chain and
     *          1 implies DDM chain.
     */
    bool procChain;

    /**
     * @brief   This is the table which specifies the supported CLI commands
     */
    CLI_CmdTableEntry tableEntry[CLI_MAX_CMD];
} CLI_Cfg;

/**
 * @brief
 *  CLI Master control block
 *
 * @details
 *  This is the MCB which tracks the CLI module
 */
typedef struct CLI_MCB_t
{
    /**
     * @brief   Configuration which was used to configure the CLI module
     */
    CLI_Cfg cfg;

    /**
     * @brief   This is the number of CLI commands which have been added to the module
     */
    uint32_t numCLICommands;

//    /**
//     * @brief   CLI Task Handle:
//     */
//    TaskHandle_t     cliTaskHandle;
//
//    /**
//     * @brief   CLI BYTask Semaphore Handle:
//     */
//    SemaphoreP_Object cliBypasssemaphoreObj;
} CLI_MCB;

/**
 * @brief
 *  CLI device part number information
 *
 * @details
 *  This is the struct to define part number and its corresponding string to be used in CLI
 */
typedef struct CLI_partInfoString_t
{
    uint8_t partNumber;
    uint8_t partNumString[CLI_MAX_PARTNO_STRING_LEN];
} CLI_partInfoString;
/**
 @}
 */
typedef struct ParamsRegsConfig_t
{
    struct reg_line *regs;
    uint32_t count;
} ParamsRegsConfig; 
/**************************************************************************
 *************************** Extern Definitions ***************************
 **************************************************************************/

/***************************Software version definition macro*********************************/
#define MAJOR_VERSION     (1)
#define MINOR_VERSION     (3)
#define BUGFIX_VERSION    (4)
#define BUILD_ID          (0)

#define MAKE_VERSION(major, minor, bugfix, build) \
    ((((uint32_t)(major) & 0xFF) << 24) | \
     (((uint32_t)(minor) & 0xFF) << 16) | \
     (((uint32_t)(bugfix) & 0xFF) << 8) | \
     (((uint32_t)(build) & 0xFF)))      

#define X2000_VERSION_INFO  MAKE_VERSION(MAJOR_VERSION,MINOR_VERSION,BUGFIX_VERSION,BUILD_ID)

int cli_init(int use_script);
void cli_exit(void);
void* cli_task(void *args);
void cli_wait_regs_cfg_ok(void);
void cli_get_server_ip(char **ip, int *port);
void cli_wirte(const char *format, ...);
void CliSiganlProcess(uint8_t *data, uint32_t len);
extern int sensor_stop_flag;
extern int link_freq_idx;

#ifdef __cplusplus
}
#endif

#endif /* CLI_H */

