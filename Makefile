TARGET   := firmware
LDSCRIPT := link.ld
BUILD    := build
SRC := src

rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))
SRCS      := $(call rwildcard,$(SRC),*.c) $(call rwildcard,$(SRC),*.S)
SRCS_RAW  := $(SRCS:$(SRC)/%=%)

CROSS   := arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size

# Override on the command line: make SDK=/path/to/pico-sdk/src
SDK ?= C:/Users/Vasilis/pico-sdk/src
INCS := -Iinclude \
        -I$(SDK)/rp2350/hardware_structs/include \
        -I$(SDK)/rp2350/hardware_regs/include

CPUFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
CFLAGS   := $(CPUFLAGS) -std=gnu17 -O2 -g3 -Wall -Wextra -ffreestanding \
            -ffunction-sections -fno-tree-loop-distribute-patterns -fdata-sections \
			-Werror=switch $(INCS)
LDFLAGS  := $(CPUFLAGS) -T$(LDSCRIPT) -nostdlib -Wl,--gc-sections \
            -Wl,-Map=$(BUILD)/$(TARGET).map

OBJS := $(addprefix $(BUILD)/,$(addsuffix .o,$(basename $(SRCS_RAW))))
DEPS := $(OBJS:.o=.d)
ELF  := $(BUILD)/$(TARGET).elf
UF2  := $(BUILD)/$(TARGET).uf2
BIN  := $(BUILD)/$(TARGET).bin


all: $(UF2)

$(ELF): $(OBJS) $(LDSCRIPT)
	@mkdir -p $(@D)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(UF2): $(ELF)
	picotool uf2 convert $< $@ --family rp2350-arm-s

# $(@D) is the target's directory, so build/ mirrors src/ at any depth.
$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

CHIP := RP235x

# Flash + reset via the CMSIS-DAP debug probe.
flash: $(ELF)
	probe-rs download --chip $(CHIP) $<
	probe-rs reset --chip $(CHIP)

# Flash and stay attached (RTT output, catches panics/faults).
# Stream RTT from an already-running target (no flash, no reset).
attach: $(ELF)
	probe-rs attach --chip $(CHIP) --always-print-stacktrace --no-timestamps $<

run: $(ELF)
	probe-rs run --chip $(CHIP) --always-print-stacktrace --no-timestamps $<

# No probe needed: hold BOOTSEL, plug in, then copy the UF2 to the
# RP2350 mass-storage volume. Set DRIVE= to your mounted drive letter.
DRIVE ?= /d
uf2: $(UF2)
	cp $< $(DRIVE)/

clean:
	rm -rf $(BUILD)

print-%:
	@echo '$* = $($*)'

.PHONY: all flash run attach uf2 clean print-%
-include $(DEPS)
