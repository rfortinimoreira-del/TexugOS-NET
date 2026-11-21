# TexugOS

TexugOS is an experimental x86 operating system being developed in stages. Its
current milestones cover the Legacy BIOS boot path, storage, shell, emulated
input, and a basic graphical desktop.

## Current implementation (partial)

The boot image contains a 512-byte boot sector followed by a fixed 512-sector
payload area and a FAT32 partition. PCI enumeration starts on bus 0 and follows
configured PCI-to-PCI bridges, with a bounded 256-device inventory. Storage
initialization reports ATA PIO, AHCI, and BIOS fallback stages and uses bounded
polling timeouts. The boot sector uses BIOS LBA extensions
when available and falls back to BIOS CHS reads when needed, loads the payload
at physical address `0x10000`, and transfers control to it. The payload collects
the BIOS E820 memory map, enables A20, enters 32-bit protected mode, and calls a
freestanding C entry point. The entry point installs the IDT exception handlers
before paging and device initialization, so early CPU faults stop with vector,
EIP, and page-fault address diagnostics instead of an unexplained reset. It
then remaps and masks the 8259 PIC before probing devices, reports usable RAM,
initializes and enables a 16 MiB identity-mapped paging region, and starts the
PIT at 100 Hz before unmasking its IRQ. Timer
interrupts are acknowledged and counted. A physical-frame bitmap is populated
from E820; the linker places zero-initialized kernel data above 1 MiB, outside
the VGA/ROM hole, and the allocator reserves the loaded kernel through its end.
The kernel has a small coalescing heap, and a preemptive scheduler can switch
between kernel threads with separate stacks. A polled primary-master
ATA PIO driver mounts the image's FAT32 partition and supports 8.3 aliases,
ASCII VFAT long names, and file/directory creation, reading, and deletion. The
kernel also provides PCI enumeration for hardware identification, PS/2
keyboard and mouse IRQ
handlers, and an interactive text shell with
`help`, `clear`, `echo`, `ls`, `cd`, `mkdir`, `touch`, `cat`, `rm`, `reboot`,
`shutdown`, `memory`, `version`, `processes`, and `mouse` commands.
The shell also runs TEF applications with `run FILE.TEF`; the disk image
contains a small `HELLO.TEF` demonstration.

### Texug Executable Format (TEF)

`.tef` packages contain a `TEF\0` signature, version 1, an ASCII app name, an
8x8 one-bit logo, and a flat 32-bit x86 binary of up to 4096 bytes. The entry
offset selects its first instruction. Applications run in ring 3 with a
separate writable stack and read-only code page. `int 0x80` syscall 2 exits
with the value in EBX; syscall 3 prints the low byte of EBX when it is a
printable character or newline. Other syscalls terminate the app. This first
version does not load ELF, provide relocations, or expose general OS services.

Build a TEF from a raw binary with:

```sh
python3 tools/mktef.py --name "My App" --binary app.bin \
  --icon 3c7e5a5a7e3c1818 --output MYAPP.TEF
```

The icon argument is eight hexadecimal bytes, one bitmap row per byte, with
the most significant bit drawn first. Copy the resulting file into the FAT32
volume and run it from the shell with `run MYAPP.TEF`.

The default image is a raw, partitioned 64 MiB BIOS disk image. `make iso`
packages it in a BIOS El Torito ISO using hard-disk emulation. The ISO still
requires a separate writable ATA disk for FAT32 storage; `make run-iso`
attaches the raw image as that disk. It is not an installer.

### Build and test

Required tools: GNU Make, GCC with 32-bit compilation support, GNU binutils,
Python 3, and QEMU's `qemu-system-i386` for runtime testing. The ISO targets
also require `xorriso`.

```sh
make
make hp-g42
make test
make run
make iso
make test-iso
make run-iso
```

For machines whose firmware hangs during the runtime BIOS EDD probe, build
without BIOS disk access and continue with ATA PIO/AHCI:

```sh
make -B BIOS_DISK=0 build/texugos.img
```

Use `make -B BIOS_DISK=0 hp-g42` to build the HP G42 image instead.

The BIOS boot sector still uses BIOS disk services to load the kernel; this
option only disables the kernel's later BIOS disk probe and access. It can
therefore remove runtime access to a USB boot disk. The default remains
`BIOS_DISK=1`.

`make test` builds the image, checks its boot-sector signature and FAT32
partition layout, and runs a headless QEMU test. It checks the kernel's boot
status and sends keyboard and mouse input to exercise shell commands, PCI
device listing, FAT32 short and long filename CRUD, directory navigation, and
PS/2 mouse packet handling.
It also verifies the 640×480 VBE desktop when supported, with a
320×200 VGA fallback, opens and closes a window using the mouse, exercises the FAT32 file
browser and system-information app, starts the
native raycasting arena, verifies enemy
pursuit and tests its shooting and mouse-aim controls, obtains a DHCP lease,
pings the QEMU network gateway,
resolves `localhost` through the DHCP-provided DNS server,
loads a page from a local HTTP test server through QEMU networking using a
formatted HTTP URL, and
confirms that Escape restores text mode.
`make test-iso` separately boots the ISO through SeaBIOS and checks the kernel
and FAT32 test with a writable ATA disk attached.
The kernel also creates a directory and a 700-byte file, verifies its contents,
then removes both.
`make run` starts QEMU with the raw disk image.
`make hp-g42` builds `build/texugos-hp-g42.img` for the HP G42-215BR.
The RTL8192SE firmware is redistributable by its upstream license and can be
downloaded from the Linux firmware repository and embedded in the HP image:

```sh
curl -fL https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/rtlwifi/rtl8192sefw.bin \
  -o build/rtl8192sefw.bin
make -B BIOS_DISK=0 RTL8192SE_FIRMWARE=build/rtl8192sefw.bin hp-g42
```

If `build/rtl8192sefw.bin` already exists, the Makefile selects it automatically
when building the HP image.
The supplied file is embedded in the boot payload and copied to the FAT32
image, so loading the firmware does not depend on mounting the disk at runtime.
On boot, TexugOS detects the RTL8191SE and validates the embedded firmware,
reads the adapter's eFuse configuration, and attempts the command-queue firmware
upload. Its pre-firmware MAC/DMA setup follows the RTL8192SE hardware sequence,
including command, RX, and TX ring initialization. Successful upload and
connectivity still require validation on the target hardware. Calibrated CCK
transmit power is read from the adapter eFuse and applied per channel; invalid
or absent calibration data is not replaced with an arbitrary maximum.
The supplied PCI inventory for the target reports a Realtek RTL8191SEvA
wireless adapter (10ec:8171) and RTL810xE Ethernet adapter (10ec:8136).
The RTL8191SE driver and RTL810xE support are incomplete; the functional
network backend still supports RTL8139 (10ec:8139). The RTL8191SE now has
open-network and WPA2-PSK/CCMP association paths, but its MAC/DMA, PHY/RF,
channel scanning, and protected networking remain unverified on hardware. The
target devices have not been tested directly by the project.

## Current status and limitations

- Implemented: BIOS boot sector, LBA disk read, E820 handoff, A20 enable,
  protected-mode transition, initial 16 MiB paging, CPU exceptions, PIC IRQ
  handling, 100 Hz PIT timer, E820-backed frame allocation with a bounded
  low-memory allocator for 32-bit DMA, kernel heap, and
  preemptive kernel threads, ATA PIO, FAT32 file/directory operations, a
  PCI enumeration, PS/2 keyboard-driven shell and mouse input, and a
  640×480 VBE desktop with a 320×200 VGA fallback, clickable icons,
  a taskbar, compact PNG cursor, and
  movable/resizable/minimizable/maximizable windows. Clicks target the topmost
  visible window, and the high-resolution mode uses a larger bitmap
  sans-serif font with lowercase glyphs. The desktop includes
  kernel-integrated Files, System Information, browser, Notepad, Snake, Tetris,
  and Paint apps. Snake uses WASD with P to pause and R to restart; Tetris uses
  A/D to move, W to rotate, S to drop, Space for a hard drop, and P to pause.
  Paint supports mouse drawing, a 16-color palette, and a clear button.
  Redraws use off-screen buffers and update only changed pixels; they are
  limited to 50 Hz while input continues to be processed each loop.
  The shell is available from the desktop taskbar. Networking has a polled
  RTL8139 driver, DHCP configuration, ARP, IPv4 ICMP echo, and `net`, `dhcp`,
  `dns` A-record lookup and `ping` shell commands. A text browser can fetch
  HTTP/1.0 pages using IPv4 addresses or DNS names over a minimal TCP client.
  Settings can request or renew a DHCP lease on a detected RTL8139 Ethernet
  adapter and display its IPv4 address. The IP stack now consumes Ethernet
  frames through a generic network-link interface, currently provided only by
  RTL8139. PCI helpers can decode device BARs and enable memory/bus-master
  access. A separately supplied RTL8192SE firmware file can be embedded in the
  FAT32 image and is checked at boot. The RTL8191SE PCI ID is recognized and
  its 0x4000-byte BAR1 MMIO window is mapped. With valid firmware present, the
  driver runs MAC power/clock preparation, reads and parses the eFuse map for
  the factory MAC address, RF board type, and channel-plan byte, then uses the
  board RF type when preparing firmware. The firmware uploader configures the
  command ring and enables PCI bus mastering when submitting its DMA descriptors.
  The IMEM, EMEM, and private-block upload stages have timeouts. Firmware
  upload failures report the failed stage and TCR register value at boot.
  These steps remain unverified on physical hardware and do not provide
  working Wi-Fi.
  Two RX rings and their low-memory DMA buffers are prepared before bus
  mastering, then their base registers are programmed after firmware upload.
  Descriptor construction, bounded frame extraction, and RX descriptor
  rearming have unit tests. Management and best-effort TX rings and low-memory
  frame buffers are also prepared and programmed. The driver has a raw 802.11
  frame submission path for those queues. After firmware upload, the driver
  enables the MAC,
  RX/TX DMA, PHY driver-info sizing, and broad receive filters using the
  RTL8192SE register sequence;
  that sequence has register-mock tests but is not verified on physical
  hardware. Host-tested
  management-frame parsing extracts BSSID, SSID, 2.4 GHz channel, and
  open/WPA2-Personal-CCMP security classification. Bounds-checked conversion
  between Ethernet and three-address 802.11 data frames is implemented as a
  building block. A guarded network-link adapter can carry Ethernet frames on
  an explicitly confirmed open-network association. Open System authentication,
  WPA2 RSN association, and bounded polling state machines are implemented.
  The shell exposes `wifi scan`, `wifi connect INDEX|SSID`,
  `wifi disconnect`, and `wifi status`. The scan attempts a wildcard probe on
  channels 1 through 13, switching channels and collecting bounded results
  for 500 ms per channel (about 7 seconds total). It can list up to 32
  networks. A connection can select a network by its scan index or exact SSID;
  when an SSID is not in the latest results, `wifi connect SSID` sends
  SSID-directed probe requests across channels 1 through 13 before attempting
  association, including for networks that do not broadcast their name. If
  multiple access points share an SSID, use the index to choose one. When no
  networks are found, the shell reports how
  many received frames and valid beacon/probe responses were observed to help
  distinguish RX/DMA problems from frame-parsing problems. Failed association
  reports identify whether authentication, association, or the WPA2 handshake
  timed out or was rejected. Open System authentication requests are retried
  up to three times at bounded intervals when no response is received.
  The PHY/RF tables and channel changes are based on the Linux RTL8192SE
  driver; fixed power-table application does not yet include complete eFuse
  calibration or regulatory channel-plan handling, and hardware behavior is
  unverified. Open-network
  association responses are polled and DHCP starts only after the open
  association or WPA2 four-way handshake succeeds. WPA2 passphrase input is
  echoed visibly in the shell; avoid entering it where others can see the
  display. The supplicant uses RDRAND when supported. Without it, the supplicant
  nonce is derived with HMAC-SHA1 keyed by the PMK and bound to the AP nonce,
  addresses, replay counter, and key generation; it does not treat the PIT or
  TSC as an entropy source. EAPOL M1–M4, pairwise-key rekey, and
  group-key rekey messages are transported as 802.11 data frames. Software
  CCMP protects/unprotects unicast and group data, checks MICs and replay
  packet numbers, and installs
  the negotiated keys before the IP link is exposed. Host tests cover the
  handshake, pairwise rekey, and CCMP frame path. Enterprise authentication
  and protected management frames are not implemented. Firmware, PHY/RF,
  scanning, association, and WPA2 still require physical validation.
  A BIOS El Torito ISO target is available. HTTPS is not supported. The
  desktop also has a native, two-level raycasting game with movement, mouse
  aiming, shooting, health, ammo, enemies, and wall collision.
  A boot-time ring 3 self-test exercises a DPL 3 `int 0x80` gate and confirms
  that user code cannot read a supervisor-only kernel page. User code and its
  stack use separate pages; this is a test harness, not a process runtime.
- Partial: DNS currently resolves IPv4 A records only. The game has two
  compact arena layouts; QEMU tests combat in the first and progression to
  the second. Enemies route around walls while pursuing the player. The PC
  speaker provides firing, hit, damage, and reload tones; audible output is
  unverified. Stage 6 device
  support and all networking have only been exercised with QEMU-emulated
  hardware.
  Desktop apps and the game run inside the kernel; there is no user-mode
  process manager or application ABI. Networking has no TLS/HTTPS,
  JavaScript, or general-purpose TCP sockets and has only been tested with
  QEMU user-mode networking.
- Partial: FAT32 long filenames support ASCII names up to 255 characters with
  generated 8.3 aliases; Unicode names are not supported.
- Not yet implemented: a full-featured browser or user-mode application
  processes. When BIOS EDD is available, storage uses the BIOS boot disk
  directly instead of probing a potentially different ATA/AHCI disk. This
  avoids selecting the HP G42's internal SSD when booting from another device.
  BIOS disk access and USB boot are tested in QEMU; the HP G42-215BR has not
  been tested directly.
- If no BIOS EDD boot disk, ATA PIO disk, or supported AHCI disk with a FAT32
  volume is available, the kernel continues to the shell and desktop without
  filesystem storage. The target notebook's RTL8191SEvA Wi-Fi and RTL810xE
  Ethernet adapters are not yet validated on physical hardware.
- The RTL8191SEvA (PCI ID 10ec:8171) is not yet usable in TexugOS. The optional
  firmware image is validated and loaded from FAT32. The PCI ID is detected
  and BAR1 is mapped. With valid firmware present, the pre-firmware MAC
  sequence configures the command queue; the bounded DMA uploader enables bus
  mastering and attempts the IMEM, EMEM, and private-block transfers. No
  physical RTL8191SE is available here, so the register sequence and upload are
  not hardware-verified. MAC/RX/TX DMA enable and receive filtering are now
  programmed after firmware upload. PHY/RF initialization, channel switching,
  a channels 1–13 scan, and open-network authentication/association followed
  by DHCP are implemented, but remain unverified on hardware. The guarded
  open-network Ethernet link is usable only after the actual AP accepts the
  association. WPA2 RSN association, hidden passphrase entry, RDRAND-backed
  or PMK-derived SNonce generation, EAPOL pairwise/group handshakes, software
  CCMP protection, replay checks, pairwise-key rekey, and DHCP-after-key-
  installation are integrated and host tested. WPA2 can start without RDRAND
  by deriving a keyed SNonce from the PMK and fresh AP handshake inputs. The
  PHY tables were ported from Linux's RTL8192SE driver under GPL-2.0; see
  `COPYING`. No connection has been tested
  on the HP G42 hardware.
- USB storage is accessed through BIOS disk services; there is no native USB
  host-controller or USB mass-storage driver. The BIOS must expose the boot
  device through INT 13h EDD for the kernel to keep using it. The kernel saves
  the BIOS boot-drive number and uses a protected-mode-to-real-mode thunk for
  disk calls, temporarily restoring the BIOS PIC interrupt masks while the
  firmware runs. The extensive boot-time FAT32 write self-test is skipped for
  BIOS-backed boot media; filesystem reads remain available. BIOS-backed boot
  media is mounted read-only because INT 13h writes hang on the HP firmware;
  directory and file creation, deletion, and Notepad saves report that storage
  is read-only.
- The QEMU smoke test confirms boot in emulation; physical hardware has not
  been tested.
- The ISO boots through BIOS El Torito hard-disk emulation; the kernel's ATA
  driver still needs a separate writable disk. No installer is included.
- The disk and USB controller are QEMU-emulated in tests; the physical HP
  G42-215BR hardware has not been tested directly.
- The PS/2 keyboard and mouse are QEMU-emulated devices; the notebook's
  touchpad and actual controller models have not been identified.
- ATA writes are polled to command completion, but the driver does not issue
  an ATA cache-flush command; power-loss durability is not guaranteed.
- `shutdown` halts the CPU; ACPI power-off support is not implemented.
