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

HOST_CC := gcc

# Flags
CFLAGS := -std=c11 -ffreestanding -fno-stack-protector -fno-stack-check \
          -fno-pie -fno-pic -m64 -march=x86-64 -mno-80387 -mno-mmx \
          -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel \
          -Wall -Wextra -Werror -O2 -g \
          -Isrc/include -Isrc -Ivendor

KERNEL_CFLAGS := $(CFLAGS) -DYAM_KERNEL

ASFLAGS := -f elf64
LDFLAGS := -nostdlib -static -T linker.ld -z max-page-size=0x1000

# Directories
SRC_DIR   := src
BUILD_DIR := build
ISO_DIR   := $(BUILD_DIR)/iso_root

# Source files (Exclude OS-level apps, drivers, and userspace libs from kernel build, keep services like compositor)
C_SRCS := $(shell find $(SRC_DIR) -name '*.c' -type f -not -path '$(SRC_DIR)/os/apps/*' -not -path '$(SRC_DIR)/os/drivers/*' -not -path '$(SRC_DIR)/os/lib/*')
ASM_SRCS := $(shell find $(SRC_DIR) -name '*.asm' -type f -not -path '$(SRC_DIR)/os/apps/*' -not -path '$(SRC_DIR)/os/drivers/*' -not -path '$(SRC_DIR)/os/lib/*')

# Object files
C_OBJS   := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst $(SRC_DIR)/%.asm,$(BUILD_DIR)/%.asm.o,$(ASM_SRCS))
OBJS     := $(C_OBJS) $(ASM_OBJS)

# Final targets
KERNEL_ELF := $(BUILD_DIR)/$(KERNEL_NAME).elf
KERNEL_ISO := $(BUILD_DIR)/$(KERNEL_NAME).iso

# Splash screen images
IMG2RAW       := $(BUILD_DIR)/img2raw
LOGO_BIN      := $(BUILD_DIR)/logo.bin
WALLPAPER_BIN := $(BUILD_DIR)/wallpaper.bin

# User-space ELF apps
USER_ELF := $(BUILD_DIR)/test_app.elf
CALC_ELF := $(BUILD_DIR)/calculator.elf
TERM_ELF := $(BUILD_DIR)/terminal.elf
BROWSER_ELF := $(BUILD_DIR)/browser.elf
NET_ELF     := $(BUILD_DIR)/net_service.elf
VIDEO_ELF   := $(BUILD_DIR)/video.elf
AUDIO_ELF   := $(BUILD_DIR)/audio.elf
IMG_ELF     := $(BUILD_DIR)/image.elf
WIFI_ELF    := $(BUILD_DIR)/wifi.elf
AUTHD_ELF   := $(BUILD_DIR)/authd.elf

USER_CFLAGS := -std=c11 -ffreestanding -fno-stack-protector -fno-stack-check \
               -fno-pie -fno-pic -no-pie -static -m64 -march=x86-64 -mno-80387 -mno-mmx \
               -mno-sse -mno-sse2 -mno-red-zone -O2 -g \
               -Isrc -Isrc/include -Isrc/os/apps

# ============================================================================
#  Targets
# ============================================================================

.PHONY: all clean run run-vmware run-serial run-serial-only debug iso setup

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
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@
	@echo "[CC]   $<"

# Assemble NASM sources
$(BUILD_DIR)/%.asm.o: $(SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@
	@echo "[ASM]  $<"

# ============================================================================
#  Host Tools & Resources
# ============================================================================

$(IMG2RAW): tools/img2raw.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -O2 -g -Itools $< -lm -o $@

$(LOGO_BIN): assets/logo.png $(IMG2RAW)
	@mkdir -p $(dir $@)
	$(IMG2RAW) $< $@ 256

$(WALLPAPER_BIN): assets/owl_wallpaper.jpg $(IMG2RAW)
	@mkdir -p $(dir $@)
	$(IMG2RAW) $< $@ 1920

$(USER_ELF): src/os/apps/test_app.c src/os/apps/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -Wl,-T,src/os/apps/user.ld -o $@ src/os/apps/test_app.c
	@echo "[USER] $@"

$(CALC_ELF): src/os/apps/calculator.c src/os/apps/font_data.c src/os/apps/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -Wl,-T,src/os/apps/user.ld -o $@ src/os/apps/calculator.c src/os/apps/font_data.c
	@echo "[CALC] $@"

$(TERM_ELF): src/os/apps/terminal.c src/os/apps/font_data.c src/os/apps/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -Wl,-T,src/os/apps/user.ld -o $@ src/os/apps/terminal.c src/os/apps/font_data.c
	@echo "[TERM] $@"

$(BROWSER_ELF): src/os/apps/browser.c src/os/apps/font_data.c src/os/apps/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -Wl,-T,src/os/apps/user.ld -o $@ src/os/apps/browser.c src/os/apps/font_data.c
	@echo "[BROWSER] $@"

$(NET_ELF): src/os/drivers/net_service.c src/os/apps/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -Wl,-T,src/os/apps/user.ld -o $@ src/os/drivers/net_service.c
	@echo "[DRV_NET] $@"

$(VIDEO_ELF): src/os/drivers/video.c src/os/apps/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -Wl,-T,src/os/apps/user.ld -o $@ src/os/drivers/video.c
	@echo "[DRV_VID] $@"

$(AUDIO_ELF): src/os/drivers/audio.c src/os/apps/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -Wl,-T,src/os/apps/user.ld -o $@ src/os/drivers/audio.c
	@echo "[DRV_AUD] $@"

$(IMG_ELF): src/os/drivers/image.c src/os/apps/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -Wl,-T,src/os/apps/user.ld -o $@ src/os/drivers/image.c
	@echo "[DRV_IMG] $@"

$(WIFI_ELF): src/os/drivers/wifi.c src/os/apps/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -Wl,-T,src/os/apps/user.ld -o $@ src/os/drivers/wifi.c
	@echo "[DRV_WIFI] $@"

$(AUTHD_ELF): src/os/apps/authd.c src/os/apps/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -Wl,-T,src/os/apps/user.ld -o $@ src/os/apps/authd.c
	@echo "[SVC_AUTH] $@"

# ============================================================================
#  ISO Creation (Limine-based bootable ISO)
# ============================================================================

$(KERNEL_ISO): $(KERNEL_ELF) $(LOGO_BIN) $(WALLPAPER_BIN) $(USER_ELF) $(CALC_ELF) $(TERM_ELF) $(BROWSER_ELF) $(NET_ELF) $(VIDEO_ELF) $(AUDIO_ELF) $(IMG_ELF) $(WIFI_ELF) $(AUTHD_ELF)
	@echo "[ISO]  Building bootable ISO..."
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/limine
	@mkdir -p $(ISO_DIR)/boot/grub
	@mkdir -p $(ISO_DIR)/EFI/BOOT
	
	# Copy kernel and modules
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/$(KERNEL_NAME).elf
	cp $(LOGO_BIN) $(ISO_DIR)/boot/logo.bin
	cp $(WALLPAPER_BIN) $(ISO_DIR)/boot/wallpaper.bin
	cp $(USER_ELF) $(ISO_DIR)/boot/test_app.elf
	cp $(CALC_ELF) $(ISO_DIR)/boot/calculator.elf
	cp $(TERM_ELF) $(ISO_DIR)/boot/terminal.elf
	cp $(BROWSER_ELF) $(ISO_DIR)/boot/browser.elf
	cp $(NET_ELF) $(ISO_DIR)/boot/net_service.elf
	cp $(VIDEO_ELF) $(ISO_DIR)/boot/video.elf
	cp $(AUDIO_ELF) $(ISO_DIR)/boot/audio.elf
	cp $(IMG_ELF) $(ISO_DIR)/boot/image.elf
	cp $(WIFI_ELF) $(ISO_DIR)/boot/wifi.elf
	cp $(AUTHD_ELF) $(ISO_DIR)/boot/authd.elf
	
	# Copy limine config
	cp limine.conf $(ISO_DIR)/boot/limine/limine.conf
	
	# Copy limine binaries (built via make setup)
	cp limine-git/limine-bios.sys $(ISO_DIR)/boot/limine/ 2>/dev/null || true
	cp limine-git/limine-bios-cd.bin $(ISO_DIR)/boot/limine/ 2>/dev/null || true
	cp limine-git/limine-uefi-cd.bin $(ISO_DIR)/boot/limine/ 2>/dev/null || true
	cp limine-git/BOOTX64.EFI $(ISO_DIR)/EFI/BOOT/ 2>/dev/null || true
	cp limine-git/BOOTIA32.EFI $(ISO_DIR)/EFI/BOOT/ 2>/dev/null || true
	
	# Create ISO with xorriso
	xorriso -as mkisofs \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $@
	
	# Install limine to ISO
	./limine-git/limine bios-install $@ 2>/dev/null || true
	
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

# Run with GDB server enabled (listens on localhost:1234)
debug: $(KERNEL_ISO)
	qemu-system-x86_64 \
		-cdrom $(KERNEL_ISO) \
		-serial stdio \
		-m 256M \
		-smp 2 \
		-boot d \
		-s -S \
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

# Run with serial output captured to a log file
run-serial: $(KERNEL_ISO)
	@echo "[SERIAL] Starting QEMU with serial log -> build/serial.log"
	@echo "[SERIAL] View log:  tail -f build/serial.log"
	qemu-system-x86_64 \
		-cdrom $(KERNEL_ISO) \
		-serial file:build/serial.log \
		-m 256M \
		-smp 2 \
		-boot d \
		-no-reboot \
		-no-shutdown

# Run headless — serial only, no graphical window (fastest for CI/debug)
run-serial-only: $(KERNEL_ISO)
	@echo "[SERIAL] Headless mode — all output on terminal"
	qemu-system-x86_64 \
		-cdrom $(KERNEL_ISO) \
		-nographic \
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
	sudo apt install -y nasm gcc make xorriso mtools qemu-system-x86 ovmf git
	@if [ ! -d "limine-git" ]; then \
		echo "[SETUP] Cloning and building Limine bootloader..."; \
		git clone https://github.com/limine-bootloader/limine.git limine-git --branch=v8.x-binary --depth=1; \
		make -C limine-git; \
	fi
	@echo "[SETUP] Done! Run 'make iso' to build."

clean:
	rm -rf $(BUILD_DIR)
	@echo "[CLEAN] Done."
