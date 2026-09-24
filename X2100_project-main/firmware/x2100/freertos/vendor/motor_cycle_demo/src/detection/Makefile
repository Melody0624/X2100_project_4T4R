# Standalone Makefile to build libmmw_detection.a
# Toolchain prefix (can be overridden via command line: make CROSS=...)
CROSS ?= ../../../../../tools/toolchains/mips-xburst2-newlib430/bin/mips-sde-elf-
CC := $(CROSS)gcc
AR := $(CROSS)ar

# Include paths relative to the detection source directory
INCLUDES := -I../../inc \
            -I../../../ne10_fft_lib/inc \

# Compile flags
CFLAGS := -O3 -Wall -fno-builtin -G 0 -EL -mno-gpopt -mno-abicalls -fno-pic -mfp64 -march=mips32r2 -mabi=32 -mmsa -D__mips_msa -ffunction-sections -fdata-sections -fsigned-char -mno-check-zero-division -fno-strict-aliasing $(INCLUDES)
ifeq ($(CONFIG_XBURST2_MSA), y)
CFLAGS += -mmsa
endif

# Source files compiled into the library (including FFT and shared functions)
SRCS := $(wildcard *.c) \
        ../../../ne10_fft_lib/src/NE10_fft.c \
        ../../../ne10_fft_lib/src/NE10_rfft_float32.c \
        ../../../ne10_fft_lib/src/NE10_rfft_float32_mxu.c \
        ../../../ne10_fft_lib/src/NE10_fft_generic_float32.c \
        ../general_functions.c \
        ../complex_abs_f32.c \
        ../algorithm_functions.c

OBJDIR := ../../../../objs/mmwlib/detection/
OBJS := $(addprefix $(OBJDIR), $(notdir $(SRCS:.c=.o)))
VPATH := $(sort $(dir $(SRCS)))

LIBDIR := ../../mmwlib
TARGET := $(LIBDIR)/libmmw_detection.a

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(LIBDIR)
	$(AR) rcs $@ $(OBJS)

$(OBJDIR)%.o: %.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET)
