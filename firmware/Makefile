# firmware build system

TARGET  := firmware
BUILD   := build

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

FREERTOS := FreeRTOS
PORT     := $(FREERTOS)/portable/GCC/ARM_CM3

INCLUDES := \
	-I. \
	-I../include \
	-I$(FREERTOS)/include \
	-I$(PORT)

ARCHFLAGS := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
CFLAGS    := $(ARCHFLAGS) -std=c11 -O2 -g3 \
             -ffunction-sections -fdata-sections \
             -fno-common $(INCLUDES)

APP_WARN  := -Wall -Wextra -Wshadow -Wundef
RTOS_WARN := -Wall

LDFLAGS := $(ARCHFLAGS) -T lm3s6965.ld -nostartfiles \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map \
           -Wl,--print-memory-usage

APP_SRCS  := main.c startup.c uart.c fastmath.c
RTOS_SRCS := $(FREERTOS)/tasks.c \
             $(FREERTOS)/queue.c \
             $(FREERTOS)/list.c \
             $(PORT)/port.c \
             $(FREERTOS)/portable/MemMang/heap_4.c

APP_OBJS  := $(addprefix $(BUILD)/,$(APP_SRCS:.c=.o))
RTOS_OBJS := $(addprefix $(BUILD)/,$(RTOS_SRCS:.c=.o))
OBJS      := $(APP_OBJS) $(RTOS_OBJS)
DEPS      := $(OBJS:.o=.d)

QEMU      := /mnt/c/Program Files/qemu/qemu-system-arm.exe
QEMU_PORT := 5555
ELF_WIN    = $(shell wslpath -w $(CURDIR)/$(BUILD)/$(TARGET).elf 2>/dev/null || echo $(BUILD)/$(TARGET).elf)

.PHONY: all clean run run-nodebug size

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin

$(BUILD)/$(TARGET).elf: $(OBJS) lm3s6965.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	@$(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(if $(filter $(FREERTOS)/%,$<),$(RTOS_WARN),$(APP_WARN)) \
	      -MMD -MP -c $< -o $@

size: $(BUILD)/$(TARGET).elf
	$(SIZE) -A $<

run: all
	"$(QEMU)" -M lm3s6965evb -kernel "$(ELF_WIN)" \
	          -serial tcp::$(QEMU_PORT),server \
	          -serial stdio -nographic

run-nodebug: all
	"$(QEMU)" -M lm3s6965evb -kernel "$(ELF_WIN)" \
	          -serial tcp::$(QEMU_PORT),server \
	          -serial null -nographic

clean:
	rm -rf $(BUILD)

-include $(DEPS)
