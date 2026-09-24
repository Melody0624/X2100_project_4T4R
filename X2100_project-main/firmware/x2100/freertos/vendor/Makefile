# Configuration options
BUILD_MODE ?= DEBUG

# Auto-detect if source Makefiles exist
HAS_DETECTION_SRC := $(if $(wildcard vendor/motor_cycle_demo/src/detection/Makefile),y,n)
HAS_TRACKING_SRC  := $(if $(wildcard vendor/motor_cycle_demo/src/Tracking/Makefile),y,n)
HAS_WARNING_SRC   := $(if $(wildcard vendor/motor_cycle_demo/src/warningFunc/Makefile),y,n)

# --- DETECTION MODULE CONFIG ---
ifeq ($(HAS_DETECTION_SRC), n)
  # Safety net: If source is physically missing, force library link to prevent compile errors
  CONFIG_USE_DETECTION_LIB := y
else
  ifeq ($(BUILD_MODE), RELEASE)
    CONFIG_USE_DETECTION_LIB := y
  else
    CONFIG_USE_DETECTION_LIB := n
  endif
endif

# --- TRACKING MODULE CONFIG ---
ifeq ($(HAS_TRACKING_SRC), n)
  # Safety net: If source is physically missing, force library link to prevent compile errors
  CONFIG_USE_TRACKING_LIB := y
else
  ifeq ($(BUILD_MODE), RELEASE)
    CONFIG_USE_TRACKING_LIB := y
  else
    CONFIG_USE_TRACKING_LIB := n
  endif
endif

# --- WARNING MODULE CONFIG ---
ifeq ($(HAS_WARNING_SRC), n)
  # Safety net: If source is physically missing, force library link to prevent compile errors
  CONFIG_USE_WARNING_LIB := y
else
  ifeq ($(BUILD_MODE), RELEASE)
    CONFIG_USE_WARNING_LIB := y
  else
    CONFIG_USE_WARNING_LIB := n
  endif
endif
src-y += vendor.c

src-y += cheetah/cheetah.c

src-y += heap_malloc/heap_malloc.c

src-y += gadget_serial/gadget_serial.c
src-y += uart_trans/uart_trans.c
src-y += uart_cli/cli.c
src-y += mcp2515/mcp2515.c
src-y += mcp2515/mcp2515_task.c
ifeq ($(CONFIG_NET_LWIP), y)
src-y += lwip/net_client.c
endif

src-y += ota_upgrade/pkt.c
src-y += ota_upgrade/pkt_api.c
src-y += ota_upgrade/rtos_ota_init.c
src-y += ota_upgrade/ota_trigger_ab.c

src-y += frames_save/frames_save.c

src-y += motor_cycle_demo/src/board_calib_config.c
src-y += motor_cycle_demo/src/get_adc_from_dat_file.c
src-y += motor_cycle_demo/src/set_params.c
src-y += motor_cycle_demo/src/memory_pool.c
src-y += motor_cycle_demo/src/parse_dat_detInfo.c

src-y += config_manager.c
src-y += eol_cal/eol_calibration.c

src-y += control/board.c

src-y += mmw_msg_pkt/mmw_msg_pkt.c

# --- DETECTION MODULE COMPILATION CONDITIONAL ---
ifeq ($(CONFIG_USE_DETECTION_LIB), y)
  # Do not compile sources
else
  # Compile sources directly
  src-y += ne10_fft_lib/src/NE10_fft.c \
           ne10_fft_lib/src/NE10_rfft_float32.c \
           ne10_fft_lib/src/NE10_rfft_float32_mxu.c \
           ne10_fft_lib/src/NE10_fft_generic_float32.c \
           motor_cycle_demo/src/general_functions.c \
           motor_cycle_demo/src/complex_abs_f32.c \
           motor_cycle_demo/src/algorithm_functions.c \
           $(patsubst vendor/%,%,$(wildcard vendor/motor_cycle_demo/src/detection/*.c))
endif

# --- TRACKING MODULE COMPILATION CONDITIONAL ---
ifeq ($(CONFIG_USE_TRACKING_LIB), y)
  # Do not compile sources
else
  # Compile sources directly
  src-y += $(patsubst vendor/%,%,$(wildcard vendor/motor_cycle_demo/src/Tracking/*.c))
endif

# --- WARNING MODULE COMPILATION CONDITIONAL ---
ifeq ($(CONFIG_USE_WARNING_LIB), y)
  # Do not compile sources
else
  # Compile sources directly
  src-y += $(patsubst vendor/%,%,$(wildcard vendor/motor_cycle_demo/src/warningFunc/*.c))
endif

ifeq ($(CONFIG_XBURST2_MSA), y)
$(warning >>> Entering MSA config block: CONFIG_XBURST2_MSA is y <<<)
CFLAGS += -mmsa
endif
