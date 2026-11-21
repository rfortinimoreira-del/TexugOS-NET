CC := gcc
AS := as
LD := ld
OBJCOPY := objcopy
PYTHON := python3
XORRISO ?= xorriso
BUILD := build
RTL8192SE_FIRMWARE ?= $(wildcard $(BUILD)/rtl8192sefw.bin)
RTL8192SE_FIRMWARE_ARG = $(if $(strip $(RTL8192SE_FIRMWARE)),--rtl8192se-firmware "$(RTL8192SE_FIRMWARE)")
BIOS_DISK ?= 1

CFLAGS := -m32 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
	-fno-pie -fno-pic -fno-stack-protector -fno-builtin
CPPFLAGS := -I$(BUILD) -Ikernel -Imemory -Idrivers -Ifilesystem -Ishell -Idesktop \
	-Inetwork -Igames
CPPFLAGS += -DTEXUGOS_BIOS_DISK=$(BIOS_DISK)
ASFLAGS := --32
LDFLAGS := -m elf_i386 -nostdlib

.PHONY: all clean hp-g42 iso test test-iso run run-iso FORCE

FORCE:

all: $(BUILD)/texugos.img

$(BUILD):
	mkdir -p $@

$(BUILD)/boot.o: boot/boot.S | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/boot.elf: $(BUILD)/boot.o boot/boot.ld
	$(LD) $(LDFLAGS) -T boot/boot.ld -o $@ $<

$(BUILD)/boot.bin: $(BUILD)/boot.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/stage2.o: boot/stage2.S | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/kernel.o: kernel/kernel.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: kernel/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/paging.o: memory/paging.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/frame_allocator.o: memory/frame_allocator.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/heap.o: memory/heap.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/pic.o: drivers/pic.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/pit.o: drivers/pit.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/keyboard.o: drivers/keyboard.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/mouse.o: drivers/mouse.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/pci.o: drivers/pci.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/rtl8139.o: drivers/rtl8139.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/network_link.o: drivers/network_link.c network/link.h \
		drivers/rtl8192se.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/rtl8192se_firmware.o: drivers/rtl8192se_firmware.c \
		drivers/rtl8192se_firmware.h filesystem/fat32.h \
		$(BUILD)/rtl8192se_firmware_blob.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/rtl8192se_firmware_blob.h: FORCE tools/embed_binary.py \
		$(RTL8192SE_FIRMWARE) | $(BUILD)
	$(PYTHON) tools/embed_binary.py --output $@ \
		$(if $(strip $(RTL8192SE_FIRMWARE)),--input "$(RTL8192SE_FIRMWARE)")

$(BUILD)/rtl8192se.o: drivers/rtl8192se.c drivers/rtl8192se.h \
		drivers/rtl8192se_hw.h drivers/pci.h memory/paging.h \
		memory/frame_allocator.h network/secure_random.h \
		network/wpa2_ccmp_frame.h network/wpa2_handshake.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/rtl8192se_hw.o: drivers/rtl8192se_hw.c \
		drivers/rtl8192se_hw.h drivers/rtl8192se_firmware.h \
		drivers/rtl8192se_tables.c drivers/rtl8192se_tables.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/rtl8192se_tables.o: drivers/rtl8192se_tables.c \
		drivers/rtl8192se_tables.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/test_rtl8192se_firmware: tests/test_rtl8192se_firmware.c \
		drivers/rtl8192se_firmware.c drivers/rtl8192se_firmware.h \
		filesystem/fat32.h $(BUILD)/rtl8192se_firmware_blob.h | $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror $(CPPFLAGS) \
		-o $@ tests/test_rtl8192se_firmware.c drivers/rtl8192se_firmware.c

$(BUILD)/test_wpa2_crypto: tests/test_wpa2_crypto.c \
		network/wpa2_crypto.c network/wpa2_crypto.h | $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Inetwork \
		-o $@ tests/test_wpa2_crypto.c network/wpa2_crypto.c

$(BUILD)/test_wpa2_handshake: tests/test_wpa2_handshake.c \
		network/wpa2_handshake.c network/wpa2_handshake.h \
		network/wpa2_crypto.c network/wpa2_crypto.h | $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Inetwork \
		-o $@ tests/test_wpa2_handshake.c network/wpa2_handshake.c \
		network/wpa2_crypto.c

$(BUILD)/test_wpa2_ccmp_frame: tests/test_wpa2_ccmp_frame.c \
		network/wpa2_ccmp_frame.c network/wpa2_ccmp_frame.h \
		network/wpa2_crypto.c network/wpa2_crypto.h | $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Inetwork \
		-o $@ tests/test_wpa2_ccmp_frame.c network/wpa2_ccmp_frame.c \
		network/wpa2_crypto.c

$(BUILD)/test_secure_random: tests/test_secure_random.c \
		network/secure_random.c network/secure_random.h | $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Inetwork \
		-o $@ tests/test_secure_random.c network/secure_random.c

$(BUILD)/test_rtl8192se_probe: tests/test_rtl8192se_probe.c \
		drivers/rtl8192se.c drivers/rtl8192se.h \
		drivers/rtl8192se_hw.c drivers/rtl8192se_hw.h \
		drivers/rtl8192se_tables.c drivers/rtl8192se_tables.h \
		drivers/rtl8192se_firmware.c drivers/rtl8192se_firmware.h \
		filesystem/fat32.h network/secure_random.c \
		network/wpa2_ccmp_frame.c network/wpa2_handshake.c \
		network/wpa2_crypto.c | $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror $(CPPFLAGS) \
		-o $@ tests/test_rtl8192se_probe.c drivers/rtl8192se.c \
		drivers/rtl8192se_hw.c drivers/rtl8192se_tables.c \
		drivers/rtl8192se_firmware.c network/secure_random.c \
		network/wpa2_ccmp_frame.c network/wpa2_handshake.c \
		network/wpa2_crypto.c

$(BUILD)/network.o: network/network.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/wpa2_crypto.o: network/wpa2_crypto.c network/wpa2_crypto.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/wpa2_handshake.o: network/wpa2_handshake.c \
		network/wpa2_handshake.h network/wpa2_crypto.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/wpa2_ccmp_frame.o: network/wpa2_ccmp_frame.c \
		network/wpa2_ccmp_frame.h network/wpa2_crypto.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/secure_random.o: network/secure_random.c \
		network/secure_random.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/mouse_cursor.h: assets/Mouse.png tools/mkcursor.py | $(BUILD)
	$(PYTHON) tools/mkcursor.py $< $@

$(BUILD)/video.o: drivers/video.c drivers/video.h \
		drivers/font_dejavu.h $(BUILD)/mouse_cursor.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/ata.o: drivers/ata.c drivers/ata.h drivers/ahci.h \
		drivers/bios_disk.h kernel/console.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/bios_disk.o: drivers/bios_disk.c drivers/bios_disk.h \
		drivers/pic.h kernel/console.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/bios_video.o: drivers/bios_video.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/ahci.o: drivers/ahci.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/fat32.o: filesystem/fat32.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/shell.o: shell/shell.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/desktop.o: desktop/desktop.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/desktop_apps.o: CFLAGS += -Os

$(BUILD)/desktop_apps.o: desktop/apps.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/video.o: CFLAGS += -Os

$(BUILD)/arena.o: games/arena.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/snake.o: games/snake.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/tetris.o: games/tetris.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/paint.o: games/paint.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/interrupt_stubs.o: kernel/interrupt_stubs.S | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/user_mode_asm.o: kernel/user_mode.S | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/bios_disk_asm.o: drivers/bios_disk.S | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/context_switch.o: kernel/context_switch.S | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/payload.elf: $(BUILD)/stage2.o $(BUILD)/interrupt_stubs.o \
		$(BUILD)/context_switch.o $(BUILD)/user_mode_asm.o \
		$(BUILD)/bios_disk_asm.o \
		$(BUILD)/kernel.o $(BUILD)/console.o $(BUILD)/user_mode.o \
		$(BUILD)/interrupts.o $(BUILD)/threads.o $(BUILD)/paging.o \
		$(BUILD)/frame_allocator.o $(BUILD)/heap.o \
		$(BUILD)/pic.o $(BUILD)/pit.o $(BUILD)/keyboard.o $(BUILD)/mouse.o \
		$(BUILD)/pci.o $(BUILD)/rtl8139.o $(BUILD)/network_link.o \
		$(BUILD)/network.o $(BUILD)/wpa2_crypto.o \
		$(BUILD)/wpa2_handshake.o $(BUILD)/wpa2_ccmp_frame.o \
		$(BUILD)/secure_random.o \
		$(BUILD)/video.o $(BUILD)/desktop.o \
		$(BUILD)/desktop_apps.o $(BUILD)/arena.o $(BUILD)/snake.o \
		$(BUILD)/tetris.o $(BUILD)/paint.o $(BUILD)/ata.o \
		$(BUILD)/bios_disk.o $(BUILD)/bios_disk_asm.o $(BUILD)/ahci.o \
		$(BUILD)/bios_video.o $(BUILD)/fat32.o $(BUILD)/shell.o \
		$(BUILD)/rtl8192se.o $(BUILD)/rtl8192se_hw.o \
		$(BUILD)/rtl8192se_tables.o \
		$(BUILD)/rtl8192se_firmware.o \
		boot/payload.ld
	$(LD) $(LDFLAGS) -T boot/payload.ld -o $@ $(filter %.o,$^)

$(BUILD)/payload.bin: $(BUILD)/payload.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/texugos.img: $(BUILD)/boot.bin $(BUILD)/payload.bin tools/mkimage.py \
		$(RTL8192SE_FIRMWARE)
	$(PYTHON) tools/mkimage.py $(BUILD)/boot.bin $(BUILD)/payload.bin $@ \
		$(RTL8192SE_FIRMWARE_ARG)

$(BUILD)/texugos-hp-g42.img: $(BUILD)/boot.bin $(BUILD)/payload.bin \
		tools/mkimage.py $(RTL8192SE_FIRMWARE)
	$(PYTHON) tools/mkimage.py $(BUILD)/boot.bin $(BUILD)/payload.bin $@ \
		$(RTL8192SE_FIRMWARE_ARG)

hp-g42: $(BUILD)/texugos-hp-g42.img

$(BUILD)/texugos.iso: $(BUILD)/texugos.img
	cd $(BUILD) && $(XORRISO) -as mkisofs -o texugos.iso \
		-V TEXUGOS -b texugos.img -hard-disk-boot texugos.img

iso: $(BUILD)/texugos.iso

test: all $(BUILD)/test_rtl8192se_firmware $(BUILD)/test_wpa2_crypto \
		$(BUILD)/test_wpa2_handshake $(BUILD)/test_wpa2_ccmp_frame \
		$(BUILD)/test_secure_random $(BUILD)/test_rtl8192se_probe
	$(BUILD)/test_rtl8192se_firmware
	$(BUILD)/test_wpa2_crypto
	$(BUILD)/test_wpa2_handshake
	$(BUILD)/test_wpa2_ccmp_frame
	$(BUILD)/test_secure_random
	$(BUILD)/test_rtl8192se_probe
	TEXUGOS_IMAGE=$(BUILD)/texugos.img $(PYTHON) -m unittest discover -s tests -v

test-iso: $(BUILD)/texugos.iso
	TEXUGOS_TEST_ISO=1 TEXUGOS_IMAGE=$(BUILD)/texugos.img \
		TEXUGOS_ISO=$(BUILD)/texugos.iso \
		$(PYTHON) -m unittest discover -s tests -p test_iso.py -v

run: all
	qemu-system-i386 -netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-drive file=$(BUILD)/texugos.img,format=raw,if=ide -boot c

run-iso: $(BUILD)/texugos.iso
	qemu-system-i386 -netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-drive file=$(BUILD)/texugos.img,format=raw,if=ide,index=0 \
		-cdrom $(BUILD)/texugos.iso -boot d

clean:
	rm -rf $(BUILD)