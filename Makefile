# ============================================================================
#  YamKernel - Graph-Based Adaptive Operating System Kernel
#  Build System
# ============================================================================

KERNEL_NAME := yamkernel

# Toolchain (requires x86_64-elf cross-compiler or native Linux GCC)
CC      := x86_64-elf-gcc
AS      := nasm
LD      := x86_64-elf-ld
OBJCOPY := x86_64-elf-objcopy

# If cross-compiler not found, try native (for WSL/Linux)
ifeq ($(shell which $(CC) 2>/dev/null),)
    CC      := gcc
    LD      := ld
    OBJCOPY := objcopy
endif

# Flags
CFLAGS := -std=c11 -ffreestanding -fno-stack-protector -fno-stack-check \
          -fno-pie -fno-pic -m64 -march=x86-64 -mno-80387 -mno-mmx \
          -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel \
          -Wall -Wextra -Werror -O2 \
          -Isrc/include -Isrc -Ivendor

ASFLAGS := -f elf64
LDFLAGS := -nostdlib -static -T linker.ld -z max-page-size=0x1000

# Directories
SRC_DIR   := src
BUILD_DIR := build
ISO_DIR   := $(BUILD_DIR)/iso_root

# Source files
C_SRCS := $(shell find $(SRC_DIR) -name '*.c' -type f)
ASM_SRCS := $(shell find $(SRC_DIR) -name '*.asm' -type f)

# Object files
C_OBJS   := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst $(SRC_DIR)/%.asm,$(BUILD_DIR)/%.asm.o,$(ASM_SRCS))
OBJS     := $(C_OBJS) $(ASM_OBJS)

# Final targets
KERNEL_ELF := $(BUILD_DIR)/$(KERNEL_NAME).elf
KERNEL_ISO := $(BUILD_DIR)/$(KERNEL_NAME).iso

# ============================================================================
#  Targets
# ============================================================================

.PHONY: all clean run run-vmware iso setup

all: $(KERNEL_ELF)

iso: $(KERNEL_ISO)

# Link kernel
$(KERNEL_ELF): $(OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "[LINK] $@"

# Compile C sources
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "[CC]   $<"

# Assemble NASM sources
$(BUILD_DIR)/%.asm.o: $(SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@
	@echo "[ASM]  $<"

# ============================================================================
#  ISO Creation (Limine-based bootable ISO)
# ============================================================================

$(KERNEL_ISO): $(KERNEL_ELF)
	@echo "[ISO]  Building bootable ISO..."
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/limine
	@mkdir -p $(ISO_DIR)/boot/grub
	@mkdir -p $(ISO_DIR)/EFI/BOOT
	
	# Copy kernel
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/$(KERNEL_NAME).elf
	
	# Copy limine config
	cp limine.conf $(ISO_DIR)/boot/limine/limine.conf
	
	# Copy limine binaries (must be installed: apt install limine)
	cp /usr/share/limine/limine-bios.sys $(ISO_DIR)/boot/limine/ 2>/dev/null || true
	cp /usr/share/limine/limine-bios-cd.bin $(ISO_DIR)/boot/limine/ 2>/dev/null || true
	cp /usr/share/limine/limine-uefi-cd.bin $(ISO_DIR)/boot/limine/ 2>/dev/null || true
	cp /usr/share/limine/BOOTX64.EFI $(ISO_DIR)/EFI/BOOT/ 2>/dev/null || true
	cp /usr/share/limine/BOOTIA32.EFI $(ISO_DIR)/EFI/BOOT/ 2>/dev/null || true
	
	# Create ISO with xorriso
	xorriso -as mkisofs \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $@
	
	# Install limine to ISO
	limine bios-install $@ 2>/dev/null || true
	
	@echo "============================================"
	@echo "  YamKernel ISO ready: $@"
	@echo "  Use with VMware, VirtualBox, or bare metal"
	@echo "============================================"

# ============================================================================
#  Run in QEMU
# ============================================================================

run: $(KERNEL_ISO)
	qemu-system-x86_64 \
		-cdrom $(KERNEL_ISO) \
		-serial stdio \
		-m 256M \
		-smp 2 \
		-boot d \
		-no-reboot \
		-no-shutdown

# Run with UEFI (requires OVMF)
run-uefi: $(KERNEL_ISO)
	qemu-system-x86_64 \
		-cdrom $(KERNEL_ISO) \
		-bios /usr/share/OVMF/OVMF_CODE.fd \
		-serial stdio \
		-m 256M \
		-smp 2 \
		-boot d \
		-no-reboot \
		-no-shutdown

# ============================================================================
#  Setup (install dependencies on Debian/Ubuntu/WSL)
# ============================================================================

setup:
	@echo "[SETUP] Installing build dependencies..."
	sudo apt update
	sudo apt install -y nasm gcc make xorriso mtools qemu-system-x86 ovmf limine
	@echo "[SETUP] Done! Run 'make iso' to build."

clean:
	rm -rf $(BUILD_DIR)
	@echo "[CLEAN] Done."
