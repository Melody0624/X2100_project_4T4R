
#-------------------------------------------------------
package_name = vendor
package_depends =
package_builtin_src = vendor/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

# Capture BUILD_MODE for linking
BUILD_MODE ?= DEBUG

# SDK Mode probe: If makefiles are missing, we MUST link the libraries
HAS_DETECTION_SRC := $(if $(wildcard $(TOPDIR)vendor/motor_cycle_demo/src/detection/Makefile),y,n)
HAS_TRACKING_SRC  := $(if $(wildcard $(TOPDIR)vendor/motor_cycle_demo/src/Tracking/Makefile),y,n)
HAS_WARNING_SRC   := $(if $(wildcard $(TOPDIR)vendor/motor_cycle_demo/src/warningFunc/Makefile),y,n)

ifeq ($(HAS_DETECTION_SRC), n)
  CONFIG_USE_DETECTION_LIB := y
else
  ifeq ($(BUILD_MODE), RELEASE)
    CONFIG_USE_DETECTION_LIB := y
  else
    CONFIG_USE_DETECTION_LIB := n
  endif
endif

ifeq ($(HAS_TRACKING_SRC), n)
  CONFIG_USE_TRACKING_LIB := y
else
  ifeq ($(BUILD_MODE), RELEASE)
    CONFIG_USE_TRACKING_LIB := y
  else
    CONFIG_USE_TRACKING_LIB := n
  endif
endif

ifeq ($(HAS_WARNING_SRC), n)
  CONFIG_USE_WARNING_LIB := y
else
  ifeq ($(BUILD_MODE), RELEASE)
    CONFIG_USE_WARNING_LIB := y
  else
    CONFIG_USE_WARNING_LIB := n
  endif
endif

ifeq ($(BUILD_MODE), RELEASE)

# Inject a hook to automatically recompile the standalone static libraries before linking
package_make_hook += vendor_build_mmw_libs_cmd

define vendor_build_mmw_libs_cmd
	@echo "======================================================"
	@echo "    BUILD_MODE=RELEASE: Checking MMW Static Libs      "
	@echo "======================================================"
	$(if $(wildcard $(TOPDIR)vendor/motor_cycle_demo/src/detection/Makefile), \
		$(Q)$(MAKE) -C $(TOPDIR)vendor/motor_cycle_demo/src/detection clean all, \
		@echo "    [SDK Mode] Detection source missing. Skipping rebuild.")
	$(if $(wildcard $(TOPDIR)vendor/motor_cycle_demo/src/Tracking/Makefile), \
		$(Q)$(MAKE) -C $(TOPDIR)vendor/motor_cycle_demo/src/Tracking clean all, \
		@echo "    [SDK Mode] Tracking source missing. Skipping rebuild.")
	$(if $(wildcard $(TOPDIR)vendor/motor_cycle_demo/src/warningFunc/Makefile), \
		$(Q)$(MAKE) -C $(TOPDIR)vendor/motor_cycle_demo/src/warningFunc clean all, \
		@echo "    [SDK Mode] Warning source missing. Skipping rebuild.")
endef
endif

CFLAGS += -Ivendor/cheetah

CFLAGS += -Ivendor/heap_malloc

CFLAGS += -Ivendor/gadget_serial
CFLAGS += -Ivendor/uart_trans
CFLAGS += -Ivendor/uart_cli
CFLAGS += -Ivendor/mcp2515
CFLAGS += -Ivendor/lwip

CFLAGS += -Ivendor/ota_upgrade

CFLAGS += -Ivendor/frames_save

CFLAGS += -Ivendor/ne10_fft_lib
CFLAGS += -Ivendor/ne10_fft_lib/inc

CFLAGS += -Ivendor/motor_cycle_demo
CFLAGS += -Ivendor/motor_cycle_demo/inc

CFLAGS += -Ivendor/eol_cal

CFLAGS += -Ivendor/control
CFLAGS += -Ivendor/mmw_msg_pkt

# Conditionally link detection and tracking libraries
ifeq ($(CONFIG_USE_DETECTION_LIB), y)
	package_lib-y += -L$(TOPDIR)vendor/motor_cycle_demo/mmwlib -lmmw_detection
endif

ifeq ($(CONFIG_USE_TRACKING_LIB), y)
	package_lib-y += -L$(TOPDIR)vendor/motor_cycle_demo/mmwlib -lmmw_tracking
endif

ifeq ($(CONFIG_USE_WARNING_LIB), y)
	package_lib-y += -L$(TOPDIR)vendor/motor_cycle_demo/mmwlib -lmmw_warning
endif

# End of vendor.mk
