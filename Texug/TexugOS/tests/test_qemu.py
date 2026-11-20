import json
import http.server
import os
import pathlib
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest

IMAGE_PATH = pathlib.Path(
    os.environ.get("TEXUGOS_IMAGE", "build/texugos.img")
).resolve()
SECTOR_SIZE = 512
VGA_BYTES = 80 * 25 * 2
GRAPHICS_BYTES = 320 * 200
class BrowserTestHandler(http.server.BaseHTTPRequestHandler):
    last_host = ""
    last_request_line = ""
    last_error = ""

    def do_GET(self):
        type(self).last_host = self.headers.get("Host", "")
        type(self).last_request_line = self.requestline
        body = b"TEXUGOS BROWSER TEST\r\n"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_error(self, code, message=None, explain=None):
        type(self).last_error = f"{code} {message or ''}"
        super().send_error(code, message, explain)

    def log_message(self, format_string, *args):
        pass


@unittest.skipUnless(shutil.which("qemu-system-i386"), "QEMU is not installed")
class QemuBootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tempdir = tempfile.TemporaryDirectory(prefix="texugos-qemu-")
        cls.http_server = http.server.ThreadingHTTPServer(
            ("127.0.0.1", 0), BrowserTestHandler
        )
        cls.http_port = cls.http_server.server_address[1]
        cls.http_thread = threading.Thread(
            target=cls.http_server.serve_forever, daemon=True
        )
        cls.http_thread.start()
        cls.qemu_image = pathlib.Path(cls.tempdir.name) / "texugos.img"
        cls.framebuffer_path = pathlib.Path(cls.tempdir.name) / "framebuffer.bin"
        shutil.copyfile(IMAGE_PATH, cls.qemu_image)
        cls.qmp_path = pathlib.Path(cls.tempdir.name) / "qmp.sock"
        cls.vga_path = pathlib.Path(cls.tempdir.name) / "vga.bin"
        cls.screenshot_path = pathlib.Path(cls.tempdir.name) / "screenshot.ppm"
        cls.vbe_info_path = pathlib.Path(cls.tempdir.name) / "vbe-info.bin"
        cls.vbe_framebuffer_address = None
        cls.vbe_pitch = None
        cls.process = subprocess.Popen(
            [
                "qemu-system-i386",
                "-display",
                "none",
                "-no-reboot",
                "-no-shutdown",
                "-netdev",
                "user,id=net0",
                "-device",
                "rtl8139,netdev=net0",
                "-device",
                "ich9-ahci,id=ahci",
                "-drive",
                f"file={cls.qemu_image},format=raw,if=ide",
                "-boot",
                "c",
                "-qmp",
                f"unix:{cls.qmp_path},server=on,wait=off",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        try:
            cls._connect_qmp()
            cls._execute_qmp({"execute": "qmp_capabilities"})
            cls.screen = ""
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                cls.screen = cls._read_screen()
                if (
                    "TexugOS shell ready." in cls.screen
                    and "texugos:" in cls.screen
                ):
                    break
                time.sleep(0.1)
            else:
                visible = "\n".join(
                    line.rstrip()
                    for line in (
                        cls.screen[index : index + 80]
                        for index in range(0, len(cls.screen), 80)
                    )
                    if line.strip()
                )
                raise TimeoutError(
                    f"TexugOS did not finish booting in QEMU; VGA:\n{visible}"
                )
        except Exception:
            cls._stop_qemu()
            cls.http_server.shutdown()
            cls.http_server.server_close()
            cls.tempdir.cleanup()
            raise

    @classmethod
    def _connect_qmp(cls):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if cls.process.poll() is not None:
                raise RuntimeError("QEMU exited before the QMP socket became ready")
            try:
                cls.qmp_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                cls.qmp_socket.settimeout(2)
                cls.qmp_socket.connect(str(cls.qmp_path))
                cls.qmp_file = cls.qmp_socket.makefile("rwb", buffering=0)
                json.loads(cls.qmp_file.readline())
                return
            except (FileNotFoundError, ConnectionRefusedError):
                cls.qmp_socket.close()
                time.sleep(0.05)
        raise TimeoutError("QEMU QMP socket did not become ready")

    @classmethod
    def _execute_qmp(cls, command):
        cls.qmp_file.write((json.dumps(command) + "\n").encode())
        while True:
            response = json.loads(cls.qmp_file.readline())
            if "return" in response:
                return response["return"]
            if "error" in response:
                raise RuntimeError(response["error"])

    @classmethod
    def _read_screen(cls):
        cls._execute_qmp(
            {
                "execute": "human-monitor-command",
                "arguments": {
                    "command-line": (
                        f'pmemsave 0xb8000 0x{VGA_BYTES:x} "{cls.vga_path}"'
                    )
                },
            }
        )
        if not cls.vga_path.exists() or cls.vga_path.stat().st_size != VGA_BYTES:
            return ""
        memory = cls.vga_path.read_bytes()
        return "".join(chr(memory[index]) for index in range(0, len(memory), 2))

    @classmethod
    def _read_screenshot(cls):
        cls._execute_qmp(
            {
                "execute": "human-monitor-command",
                "arguments": {
                    "command-line": f'screendump "{cls.screenshot_path}"'
                },
            }
        )
        image = cls.screenshot_path.read_bytes()
        header, pixels = image.split(b"\n255\n", 1)
        width, height = (int(value) for value in header.split()[-2:])
        if len(pixels) != width * height * 3:
            raise AssertionError("QEMU screendump has an unexpected pixel size")
        return width, height, pixels

    @classmethod
    def _send_command(cls, command, submit=True):
        text = command + ("\n" if submit else "")
        for character in text:
            qcodes = {
                " ": ["spc"],
                "\n": ["ret"],
                ".": ["dot"],
                ":": ["shift", "semicolon"],
                "/": ["slash"],
            }.get(character, [character])
            cls._execute_qmp(
                {
                    "execute": "send-key",
                    "arguments": {
                        "keys": [
                            {"type": "qcode", "data": qcode}
                            for qcode in qcodes
                        ],
                        "hold-time": 10,
                    },
                }
            )
            time.sleep(0.01)

    @classmethod
    def _send_mouse_move(cls, x, y):
        cls._execute_qmp(
            {
                "execute": "input-send-event",
                "arguments": {
                    "events": [
                        {"type": "rel", "data": {"axis": "x", "value": x}},
                        {"type": "rel", "data": {"axis": "y", "value": y}},
                    ]
                },
            }
        )

    @classmethod
    def _send_mouse_button(cls, down):
        cls._execute_qmp(
            {
                "execute": "input-send-event",
                "arguments": {
                    "events": [
                        {
                            "type": "btn",
                            "data": {"button": "left", "down": down},
                        }
                    ]
                },
            }
        )

    @classmethod
    def _move_mouse_to(cls, current_x, current_y, target_x, target_y):
        delta_x = target_x - current_x
        delta_y = target_y - current_y
        while delta_x != 0 or delta_y != 0:
            step_x = max(-127, min(127, delta_x))
            step_y = max(-127, min(127, delta_y))
            cls._send_mouse_move(step_x, step_y)
            delta_x -= step_x
            delta_y -= step_y
        time.sleep(0.1)

    @classmethod
    def _click_mouse(cls):
        cls._send_mouse_button(True)
        time.sleep(0.1)
        cls._send_mouse_button(False)
        time.sleep(0.1)

    @classmethod
    def _send_key(cls, key):
        cls._execute_qmp(
            {
                "execute": "send-key",
                "arguments": {
                    "keys": [{"type": "qcode", "data": key}],
                    "hold-time": 10,
                },
            }
        )

    @classmethod
    def _read_framebuffer(cls):
        if cls.vbe_framebuffer_address is None:
            cls._execute_qmp(
                {
                    "execute": "human-monitor-command",
                    "arguments": {
                        "command-line": (
                            f'pmemsave 0x6600 0x40 "{cls.vbe_info_path}"'
                        )
                    },
                }
            )
            if (
                cls.vbe_info_path.exists()
                and cls.vbe_info_path.stat().st_size == 0x40
            ):
                info = cls.vbe_info_path.read_bytes()
                attributes = struct.unpack_from("<H", info, 0)[0]
                width = struct.unpack_from("<H", info, 18)[0]
                height = struct.unpack_from("<H", info, 20)[0]
                pitch = struct.unpack_from("<H", info, 16)[0]
                framebuffer_address = struct.unpack_from("<I", info, 40)[0]
                linear_pitch = struct.unpack_from("<H", info, 50)[0]
                pitch = linear_pitch
                if (
                    attributes & 0x91 == 0x91
                    and (width, height, info[25], info[27]) == (640, 480, 8, 4)
                    and pitch >= width
                    and framebuffer_address != 0
                ):
                    cls.vbe_framebuffer_address = framebuffer_address
                    cls.vbe_pitch = pitch

        if cls.vbe_framebuffer_address is not None:
            vbe_bytes = cls.vbe_pitch * 480
            cls._execute_qmp(
                {
                    "execute": "human-monitor-command",
                    "arguments": {
                        "command-line": (
                            f'pmemsave 0x{cls.vbe_framebuffer_address:x} '
                            f'0x{vbe_bytes:x} "{cls.framebuffer_path}"'
                        )
                    },
                }
            )
            if (
                not cls.framebuffer_path.exists()
                or cls.framebuffer_path.stat().st_size != vbe_bytes
            ):
                return b""
            physical = cls.framebuffer_path.read_bytes()
            return bytes(
                physical[(y * 12 // 5) * cls.vbe_pitch + x * 2]
                for y in range(200)
                for x in range(320)
            )

        cls._execute_qmp(
            {
                "execute": "human-monitor-command",
                "arguments": {
                    "command-line": (
                        f'pmemsave 0xa0000 0x{GRAPHICS_BYTES:x} '
                        f'"{cls.framebuffer_path}"'
                    )
                },
            }
        )
        if (
            not cls.framebuffer_path.exists()
            or cls.framebuffer_path.stat().st_size != GRAPHICS_BYTES
        ):
            return b""
        return cls.framebuffer_path.read_bytes()

    @classmethod
    def _enter_desktop(cls):
        previous_frame = cls._read_framebuffer()
        cls._send_command("desktop")
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            framebuffer = cls._read_framebuffer()
            if (
                len(framebuffer) == GRAPHICS_BYTES
                and framebuffer != previous_frame
                and framebuffer[0] == 2
            ):
                return framebuffer
            time.sleep(0.05)
        raise AssertionError("Desktop did not render after the shell command")

    @staticmethod
    def _framebuffer_region(framebuffer, x, y, width, height):
        return b"".join(
            framebuffer[row * 320 + x : row * 320 + x + width]
            for row in range(y, y + height)
        )

    def _run_shell_command(self, command, expected_output):
        self._send_command(command)
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            command_position = screen.rfind(command)
            while command_position >= 0:
                prompt_before = screen.rfind("texugos:", 0, command_position)
                if prompt_before // 80 == command_position // 80:
                    break
                command_position = screen.rfind(command, 0, command_position)
            if command_position >= 0:
                last_command_character = command_position + len(command) - 1
                output_start = (last_command_character // 80 + 1) * 80
                next_prompt = screen.find("texugos:", output_start)
            else:
                output_start = len(screen)
                next_prompt = -1
            if (
                expected_output in screen[output_start:]
                and next_prompt >= 0
            ):
                return screen
            time.sleep(0.05)
        self.fail(
            f"Shell command {command!r} did not produce {expected_output!r}; "
            f"VGA:\n{screen}"
        )

    @classmethod
    def _stop_qemu(cls):
        if hasattr(cls, "qmp_file"):
            cls.qmp_file.close()
        if hasattr(cls, "qmp_socket"):
            cls.qmp_socket.close()
        if cls.process.poll() is None:
            cls.process.send_signal(signal.SIGTERM)
            try:
                cls.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                cls.process.kill()
                cls.process.wait(timeout=5)

    @classmethod
    def tearDownClass(cls):
        cls._stop_qemu()
        cls.http_server.shutdown()
        cls.http_server.server_close()
        cls.http_thread.join(timeout=1)
        cls.tempdir.cleanup()

    def test_kernel_reports_working_memory_and_interrupt_setup(self):
        self.assertIn("Usable RAM:", self.screen)
        self.assertIn("Paging: 16 MiB identity map active", self.screen)
        self.assertIn("Memory: frame allocator and kernel heap active", self.screen)
        self.assertRegex(self.screen, r"PCI: [1-9][0-9]* devices enumerated")
        self.assertIn("Network: RTL8139 ready", self.screen)
        self.assertIn("ATA: QEMU HARDDISK", self.screen)
        self.assertIn("FAT32: mount, CRUD and LFN test passed", self.screen)
        self.assertIn("Keyboard: PS/2 input enabled", self.screen)
        self.assertIn("Mouse: PS/2 initialized", self.screen)
        self.assertIn("IDT: exceptions and IRQ vectors installed", self.screen)
        self.assertIn("PIC: remapped; PIT: 100 Hz", self.screen)
        self.assertIn("Threads: 2 preemptive tasks completed", self.screen)
        self.assertIn(
            "User mode: syscall and kernel-page isolation tests passed",
            self.screen,
        )
        self.assertIn("Kernel subsystems active (partial).", self.screen)
        self.assertIn("TexugOS shell ready.", self.screen)
        self.assertRegex(self.screen, r"PIT ticks: [1-9][0-9]*")

    def _boot_with_test_storage(
        self, attach_ahci_disk, unformatted_ahci_disk=False
    ):
        suffix = (
            "ahci-disk"
            if attach_ahci_disk
            else "usb-with-blank-sata"
            if unformatted_ahci_disk
            else "usb-boot"
        )
        qmp_path = pathlib.Path(self.tempdir.name) / f"qmp-{suffix}.sock"
        vga_path = pathlib.Path(self.tempdir.name) / f"vga-{suffix}.bin"
        command = [
            "qemu-system-i386",
            "-cpu",
            "athlon",
            "-m",
            "3G",
            "-smp",
            "2",
            "-display",
            "none",
            "-no-reboot",
            "-no-shutdown",
            "-device",
            "ich9-ahci,id=ahci",
        ]
        boot_image = pathlib.Path(self.tempdir.name) / f"texugos-{suffix}.img"
        shutil.copyfile(IMAGE_PATH, boot_image)
        if attach_ahci_disk:
            command.extend(
                [
                    "-drive",
                    f"file={boot_image},format=raw,if=none,id=sata-disk",
                    "-device",
                    "ide-hd,drive=sata-disk,bus=ahci.0",
                ]
            )
        else:
            if unformatted_ahci_disk:
                blank_disk = pathlib.Path(self.tempdir.name) / "blank-sata.img"
                with blank_disk.open("wb") as disk_file:
                    disk_file.truncate(16 * 1024 * 1024)
                command.extend(
                    [
                        "-drive",
                        f"file={blank_disk},format=raw,if=none,id=sata-disk",
                        "-device",
                        "ide-hd,drive=sata-disk,bus=ahci.0",
                    ]
                )
            command.extend(
                [
                    "-drive",
                    f"file={boot_image},format=raw,if=none,id=boot-disk",
                    "-device",
                    "qemu-xhci",
                    "-device",
                    "usb-storage,drive=boot-disk,bootindex=1",
                ]
            )
        command.extend(
            [
                "-boot",
                "c",
                "-qmp",
                f"unix:{qmp_path},server=on,wait=off",
            ]
        )
        process = subprocess.Popen(
            command,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        qmp_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        qmp_file = None

        def execute_qmp(command):
            qmp_file.write((json.dumps(command) + "\n").encode())
            while True:
                response = json.loads(qmp_file.readline())
                if "return" in response:
                    return response["return"]
                if "error" in response:
                    raise RuntimeError(response["error"])

        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    error = process.stderr.read().decode(errors="replace")
                    self.fail(
                        f"QEMU exited before the AHCI {suffix} test started: "
                        + error
                    )
                try:
                    qmp_socket.connect(str(qmp_path))
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    time.sleep(0.05)
            else:
                self.fail("QEMU QMP socket did not become ready")

            qmp_socket.settimeout(2)
            qmp_file = qmp_socket.makefile("rwb", buffering=0)
            json.loads(qmp_file.readline())
            execute_qmp({"execute": "qmp_capabilities"})

            screen = ""
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                execute_qmp(
                    {
                        "execute": "human-monitor-command",
                        "arguments": {
                            "command-line": (
                                f'pmemsave 0xb8000 0x{VGA_BYTES:x} "{vga_path}"'
                            )
                        },
                    }
                )
                if vga_path.exists() and vga_path.stat().st_size == VGA_BYTES:
                    memory = vga_path.read_bytes()
                    screen = "".join(
                        chr(memory[index]) for index in range(0, len(memory), 2)
                    )
                if "TexugOS shell ready." in screen:
                    break
                time.sleep(0.1)

            self.assertIn("TexugOS shell ready.", screen)
            for key in ["m", "k", "d", "i", "r", "spc", "o", "i", "ret"]:
                execute_qmp(
                    {
                        "execute": "send-key",
                        "arguments": {
                            "keys": [{"type": "qcode", "data": key}],
                            "hold-time": 10,
                        },
                    }
                )
                time.sleep(0.01)
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                execute_qmp(
                    {
                        "execute": "human-monitor-command",
                        "arguments": {
                            "command-line": (
                                f'pmemsave 0xb8000 0x{VGA_BYTES:x} '
                                f'"{vga_path}"'
                            )
                        },
                    }
                )
                if vga_path.exists() and vga_path.stat().st_size == VGA_BYTES:
                    memory = vga_path.read_bytes()
                    screen = "".join(
                        chr(memory[index]) for index in range(0, len(memory), 2)
                    )
                if (
                    "Directory created." in screen
                    or "Storage is read-only; cannot create directory." in screen
                ):
                    break
                time.sleep(0.05)
            return screen
        finally:
            if qmp_file is not None:
                qmp_file.close()
            qmp_socket.close()
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            if process.stderr is not None:
                process.stderr.close()

    def test_usb_boot_disk_mounts_read_only_without_hanging_on_write(self):
        screen = self._boot_with_test_storage(attach_ahci_disk=False)
        self.assertIn("ATA: BIOS boot disk", screen)
        self.assertIn("Storage: BIOS boot disk active", screen)
        self.assertIn(
            "FAT32: BIOS boot volume mounted read-only; writes disabled.",
            screen,
        )
        self.assertIn("mkdir oi", screen)
        self.assertIn("Storage is read-only; cannot create directory.", screen)

    def test_unformatted_ahci_disk_does_not_hang_usb_boot(self):
        screen = self._boot_with_test_storage(
            attach_ahci_disk=False,
            unformatted_ahci_disk=True,
        )
        self.assertIn("Storage: AHCI SATA controller active", screen)
        self.assertIn(
            "FAT32: no supported volume mounted; continuing without storage.",
            screen,
        )
        self.assertIn("TexugOS shell ready.", screen)

    def test_ahci_disk_mounts_and_writes_fat32(self):
        screen = self._boot_with_test_storage(attach_ahci_disk=True)
        self.assertIn("ATA: QEMU HARDDISK", screen)
        self.assertIn("Storage: AHCI SATA controller active", screen)
        self.assertIn("FAT32: mount, CRUD and LFN test passed", screen)
        self.assertIn("mkdir oi", screen)
        self.assertIn("Directory created.", screen)

    def test_native_arena_raycasting_controls_and_shooting(self):
        self._send_command("desktop")
        time.sleep(0.2)
        self._send_mouse_move(-250, 90)
        time.sleep(0.1)
        framebuffer = self._read_framebuffer()
        self.assertEqual(len(framebuffer), GRAPHICS_BYTES)
        before_window = self._framebuffer_region(framebuffer, 100, 42, 205, 140)

        self._send_key("4")
        deadline = time.monotonic() + 3
        title_screen = before_window
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            title_screen = self._framebuffer_region(
                framebuffer, 100, 42, 205, 140
            )
            if title_screen != before_window:
                break
            time.sleep(0.05)
        self.assertNotEqual(title_screen, before_window)

        self._send_key("ret")
        deadline = time.monotonic() + 3
        game_view = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            game_view = self._framebuffer_region(
                framebuffer, 107, 63, 186, 118
            )
            if bytes((4,)) in game_view:
                break
            time.sleep(0.05)
        self.assertIn(bytes((4,)), game_view)

        view_before_enemy_move = game_view
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            game_view = self._framebuffer_region(
                framebuffer, 107, 63, 186, 118
            )
            if game_view != view_before_enemy_move:
                break
            time.sleep(0.05)
        self.assertNotEqual(game_view, view_before_enemy_move)

        view_before_shot = game_view
        self._send_key("f")
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            game_view = self._framebuffer_region(
                framebuffer, 107, 63, 186, 118
            )
            if game_view != view_before_shot:
                break
            time.sleep(0.05)
        self.assertNotEqual(game_view, view_before_shot)

        view_before_aim = game_view
        self._send_mouse_move(8, 0)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            game_view = self._framebuffer_region(
                framebuffer, 107, 63, 186, 118
            )
            if game_view != view_before_aim:
                break
            time.sleep(0.05)
        self.assertNotEqual(game_view, view_before_aim)

        view_before_move = game_view
        for _ in range(6):
            self._send_key("w")
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            game_view = self._framebuffer_region(
                framebuffer, 107, 63, 186, 118
            )
            if game_view != view_before_move:
                break
            time.sleep(0.05)
        self.assertNotEqual(game_view, view_before_move)

        self._send_key("esc")
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            if "Returned from TexugOS Desktop." in screen:
                break
            time.sleep(0.05)
        self.assertIn("Returned from TexugOS Desktop.", screen)

    def test_desktop_snake_tetris_paint_apps_open(self):
        try:
            for shortcut in ("6", "7", "8"):
                self._run_shell_command(
                    "help", "Commands: help clear echo ls cd mkdir touch cat rm"
                )
                desktop_frame = self._enter_desktop()
                self.assertEqual(len(desktop_frame), GRAPHICS_BYTES)

                self._send_key(shortcut)
                deadline = time.monotonic() + 3
                app_frame = b""
                desktop_region = self._framebuffer_region(
                    desktop_frame, 35, 40, 250, 130
                )
                while time.monotonic() < deadline:
                    app_frame = self._read_framebuffer()
                    if (
                        len(app_frame) == GRAPHICS_BYTES
                        and self._framebuffer_region(
                            app_frame, 35, 40, 250, 130
                        )
                        != desktop_region
                    ):
                        break
                    time.sleep(0.05)
                self.assertEqual(len(app_frame), GRAPHICS_BYTES)
                self.assertNotEqual(
                    desktop_region,
                    self._framebuffer_region(app_frame, 35, 40, 250, 130),
                    f"Desktop shortcut {shortcut} did not open its app",
                )

                self._send_key("esc")
                deadline = time.monotonic() + 3
                screen = ""
                while time.monotonic() < deadline:
                    screen = self._read_screen()
                    if "Returned from TexugOS Desktop." in screen:
                        break
                    time.sleep(0.05)
                self.assertIn("Returned from TexugOS Desktop.", screen)
                time.sleep(0.2)
        finally:
            self._send_key("esc")
            time.sleep(0.1)
            self._send_key("esc")
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if "Returned from TexugOS Desktop." in self._read_screen():
                    break
                time.sleep(0.05)
            time.sleep(0.1)

    def test_paint_displays_palette_and_draws_with_mouse(self):
        mouse_screen = self._run_shell_command("mouse", "Mouse packets:")
        position = mouse_screen.split(" position ", 1)[1].split(" buttons", 1)[0]
        mouse_x, mouse_y = (int(value) for value in position.split(","))
        canvas_pixels = b""
        self._enter_desktop()
        try:
            self._send_key("8")
            time.sleep(0.2)

            def move_mouse_to(target_x, target_y):
                nonlocal mouse_x, mouse_y
                delta_x = target_x - mouse_x
                delta_y = target_y - mouse_y
                while delta_x != 0 or delta_y != 0:
                    step_x = max(-127, min(127, delta_x))
                    step_y = max(-127, min(127, delta_y))
                    self._send_mouse_move(step_x, step_y)
                    delta_x -= step_x
                    delta_y -= step_y
                mouse_x = target_x
                mouse_y = target_y
                time.sleep(0.1)

            move_mouse_to(180, 100)
            initial_framebuffer = self._read_framebuffer()
            self.assertEqual(len(initial_framebuffer), GRAPHICS_BYTES)
            self.assertEqual(initial_framebuffer[65 * 320 + 216], 12)

            self._send_mouse_button(False)
            self._send_mouse_button(True)
            time.sleep(0.1)
            move_mouse_to(220, 100)
            self._send_mouse_button(False)
            time.sleep(0.2)

            framebuffer = self._read_framebuffer()
            self.assertEqual(len(framebuffer), GRAPHICS_BYTES)
            canvas_pixels = self._framebuffer_region(
                framebuffer, 56, 76, 198, 78
            )
        finally:
            self._send_key("esc")
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if "Returned from TexugOS Desktop." in self._read_screen():
                    break
                time.sleep(0.05)
            time.sleep(0.1)
        final_mouse_screen = self._run_shell_command("mouse", "Mouse packets:")
        final_position = (
            final_mouse_screen.split(" position ", 1)[1]
            .split(" buttons", 1)[0]
        )
        self.assertEqual(final_position, "220,100")
        self.assertIn(
            bytes((0,)),
            canvas_pixels,
            f"Paint stroke missing; mouse {position} -> {final_position}",
        )

    def test_desktop_window_buttons_use_frontmost_window(self):
        mouse_screen = self._run_shell_command("mouse", "Mouse packets:")
        position = mouse_screen.split(" position ", 1)[1].split(" buttons", 1)[0]
        mouse_x, mouse_y = (int(value) for value in position.split(","))
        self._enter_desktop()
        try:
            self._send_key("1")
            time.sleep(0.2)

            self._move_mouse_to(mouse_x, mouse_y, 20, 190)
            self._click_mouse()
            self._move_mouse_to(20, 190, 200, 75)
            self._click_mouse()
            time.sleep(0.2)

            self._move_mouse_to(200, 75, 274, 160)
            before = self._read_framebuffer()
            self.assertEqual(before[160 * 320 + 140], 9)
            self._click_mouse()
            time.sleep(0.2)
            after = self._read_framebuffer()
            self.assertEqual(after[160 * 320 + 140], 10)
        finally:
            self._send_key("esc")
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if "Returned from TexugOS Desktop." in self._read_screen():
                    break
                time.sleep(0.05)

    def test_native_arena_second_level_progression(self):
        self._send_command("desktop")
        time.sleep(0.2)
        self._send_key("4")
        time.sleep(0.2)
        self._send_key("ret")

        deadline = time.monotonic() + 3
        level_one_view = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            level_one_view = self._framebuffer_region(
                framebuffer, 107, 63, 186, 118
            )
            if bytes((4,)) in level_one_view:
                break
            time.sleep(0.05)
        self.assertIn(bytes((4,)), level_one_view)

        for _ in range(3):
            self._send_key("f")
        for _ in range(11):
            self._send_key("e")
        for _ in range(3):
            self._send_key("f")

        deadline = time.monotonic() + 3
        won_view = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            won_view = self._framebuffer_region(
                framebuffer, 107, 63, 186, 118
            )
            if bytes((10,)) in won_view:
                break
            time.sleep(0.05)
        self.assertIn(bytes((10,)), won_view)

        self._send_key("ret")
        deadline = time.monotonic() + 3
        level_two_view = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            level_two_view = self._framebuffer_region(
                framebuffer, 107, 63, 186, 118
            )
            if bytes((4,)) in level_two_view and level_two_view != level_one_view:
                break
            time.sleep(0.05)
        self.assertIn(bytes((4,)), level_two_view)
        self.assertNotEqual(level_two_view, level_one_view)

        view_before_level_two_enemy_move = level_two_view
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            level_two_view = self._framebuffer_region(
                framebuffer, 107, 63, 186, 118
            )
            if level_two_view != view_before_level_two_enemy_move:
                break
            time.sleep(0.05)
        self.assertNotEqual(level_two_view, view_before_level_two_enemy_move)

        self._send_key("esc")
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            if "Returned from TexugOS Desktop." in screen:
                break
            time.sleep(0.05)
        self.assertIn("Returned from TexugOS Desktop.", screen)

    def test_shell_commands_and_fat32_navigation(self):
        help_screen = self._run_shell_command(
            "help", "Commands: help clear echo ls cd mkdir touch cat rm"
        )
        self.assertIn("desktop net dhcp dns ping", help_screen)
        self.assertIn("wifi scan|connect INDEX|disconnect|status", help_screen)
        self._run_shell_command("wifi scan", "Wi-Fi scan unavailable")
        self._send_mouse_move(25, 15)
        mouse_screen = self._run_shell_command("mouse", "Mouse packets:")
        mouse_packets = int(
            mouse_screen.split("Mouse packets: ", 1)[1].split()[0]
        )
        self.assertGreater(mouse_packets, 0)
        position = mouse_screen.split(" position ", 1)[1].split(" buttons", 1)[0]
        self.assertNotEqual(position, "320,100")
        self._run_shell_command("version", "TexugOS version 0.1.0")
        devices = self._run_shell_command("devices", "PCI devices:")
        self.assertIn("PCI 0:", devices)
        network = self._run_shell_command("net", "IPv4:")
        self._run_shell_command("ping invalid", "Usage: ping A.B.C.D")
        if "IPv4: not configured." in network:
            self._run_shell_command("dhcp", "DHCP lease acquired.")
        else:
            self.assertIn("IPv4: 10.0.2.15", network)
        network = self._run_shell_command("net", "IPv4: 10.0.2.15")
        self.assertIn("Gateway: 10.0.2.2", network)
        self._run_shell_command("dns localhost", "DNS reply: 127.0.0.1")
        self._run_shell_command("dns localhost.", "DNS reply: 127.0.0.1")
        self._run_shell_command("dns invalid..name", "DNS lookup failed")
        self._run_shell_command("ping 10.0.2.2", "Reply from 10.0.2.2")
        self._run_shell_command("echo ready", "ready")
        self._run_shell_command("mkdir test", "Directory created.")
        screen = self._run_shell_command("cd test", "texugos:/TEST >")
        self.assertIn("texugos:/TEST >", screen)
        self._run_shell_command("mkdir child", "Directory created.")
        self._run_shell_command("cd child", "texugos:/TEST/CHILD >")
        self._run_shell_command("cd ..", "texugos:/TEST >")
        self._run_shell_command("rm child", "Removed.")
        self._run_shell_command("touch note", "File created.")
        self._run_shell_command("ls", "NOTE  0 bytes")
        screen = self._run_shell_command("cat note", "texugos:/TEST >")
        self.assertNotIn("Unable to read file", screen)
        self._run_shell_command("rm note", "Removed.")
        self._run_shell_command("cd", "texugos:/ >")
        self._run_shell_command("rm test", "Removed.")
        self._run_shell_command(
            "mkdir long directory with spaces", "Directory created."
        )
        self._run_shell_command(
            "cd long directory with spaces",
            "texugos:/long directory with spaces >",
        )
        self._run_shell_command("touch note with spaces.txt", "File created.")
        self._run_shell_command("ls", "note with spaces.txt  0 bytes")
        self._run_shell_command(
            "cat note with spaces.txt", "texugos:/long directory with spaces >"
        )
        self._run_shell_command("rm note with spaces.txt", "Removed.")
        self._run_shell_command("cd ..", "texugos:/ >")
        self._run_shell_command(
            "rm long directory with spaces", "Removed."
        )
        self._run_shell_command("memory", "Free physical frames:")
        self._run_shell_command("processes", "User processes: 0")

        self._send_command("clear")
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            if screen.startswith("texugos:/ >"):
                break
            time.sleep(0.05)
        self.assertTrue(screen.startswith("texugos:/ >"), screen)

    def test_desktop_graphical_mode_and_text_mode_return(self):
        mouse_screen = self._run_shell_command("mouse", "Mouse packets:")
        mouse_position = mouse_screen.rsplit(" position ", 1)[1].split(
            " buttons", 1
        )[0]
        current_x, current_y = (
            int(value) for value in mouse_position.split(",")
        )
        framebuffer = self._enter_desktop()
        self.assertEqual(len(framebuffer), GRAPHICS_BYTES)
        self.assertEqual(framebuffer[0], 2)
        self.assertIsNotNone(self.vbe_framebuffer_address)
        self.assertGreaterEqual(self.vbe_framebuffer_address, 16 * 1024 * 1024)
        width, height, _ = self._read_screenshot()
        self.assertEqual((width, height), (640, 480))

        self._send_key("2")
        self._move_mouse_to(current_x, current_y, 160, 143)
        self._click_mouse()
        self._send_key("esc")
        deadline = time.monotonic() + 10
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            if "Returned from TexugOS Desktop." in screen:
                break
            time.sleep(0.05)
        self.assertIn("Returned from TexugOS Desktop.", screen)
        network = self._run_shell_command("net", "IPv4: 10.0.2.15")
        self.assertIn("Gateway: 10.0.2.2", network)
        current_x, current_y = 160, 143
        framebuffer = self._enter_desktop()

        sample = 120 * 320 + 250
        wallpaper_pixel = framebuffer[sample]

        self._move_mouse_to(current_x, current_y, 50, 75)
        self._click_mouse()
        deadline = time.monotonic() + 3
        framebuffer = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            if len(framebuffer) == GRAPHICS_BYTES and framebuffer[sample] == 15:
                break
            time.sleep(0.05)
        self.assertNotEqual(wallpaper_pixel, 15)
        window_pixels = [
            framebuffer[y * 320 + x]
            for y in (35, 40, 50, 75, 120, 160)
            for x in (80, 90, 150, 250, 270)
        ]
        self.assertEqual(
            framebuffer[sample],
            15,
            f"mouse started at {current_x},{current_y}; "
            f"window-region pixels: {window_pixels}",
        )

        self._send_key("esc")
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            if "Returned from TexugOS Desktop." in screen:
                break
            time.sleep(0.05)
        self.assertIn("Returned from TexugOS Desktop.", screen)

        self._send_command("desktop")
        time.sleep(0.2)
        self._send_key("1")
        deadline = time.monotonic() + 3
        framebuffer = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            files_header_pixels = sum(
                framebuffer[row * 320 + column] == 1
                for row in range(50, 57)
                for column in range(80, 205)
            ) if len(framebuffer) == GRAPHICS_BYTES else 0
            if files_header_pixels > 20:
                break
            time.sleep(0.05)
        self.assertGreater(files_header_pixels, 20)
        files_before = self._framebuffer_region(framebuffer, 75, 45, 190, 110)
        files_after = files_before
        for attempt in range(3):
            self._send_key("ret")
            deadline = time.monotonic() + 1
            while time.monotonic() < deadline:
                framebuffer = self._read_framebuffer()
                files_after = self._framebuffer_region(
                    framebuffer, 75, 45, 190, 110
                )
                if files_after != files_before:
                    break
                time.sleep(0.05)
            if files_after != files_before:
                break
        self.assertNotEqual(files_after, files_before)

        self._send_key("esc")
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            if "Returned from TexugOS Desktop." in screen:
                break
            time.sleep(0.05)
        self.assertIn("Returned from TexugOS Desktop.", screen)

        self._send_command("desktop")
        time.sleep(0.2)
        self._send_key("2")
        deadline = time.monotonic() + 3
        framebuffer = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            settings_text_pixels = sum(
                framebuffer[row * 320 + column] == 1
                for row in range(64, 71)
                for column in range(96, 205)
            ) if len(framebuffer) == GRAPHICS_BYTES else 0
            if settings_text_pixels > 20:
                break
            time.sleep(0.05)
        self.assertGreater(settings_text_pixels, 20)

        self._send_key("esc")
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            if "Returned from TexugOS Desktop." in screen:
                break
            time.sleep(0.05)
        self.assertIn("Returned from TexugOS Desktop.", screen)
        self._send_command("desktop")
        time.sleep(0.2)
        self._send_key("3")
        deadline = time.monotonic() + 3
        framebuffer = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            browser_app_pixels = sum(
                framebuffer[row * 320 + column] == 15
                for row in range(71, 78)
                for column in range(104, 205)
            ) if len(framebuffer) == GRAPHICS_BYTES else 0
            if browser_app_pixels > 20:
                break
            time.sleep(0.05)
        self.assertGreater(browser_app_pixels, 20)
        browser_before = self._framebuffer_region(
            framebuffer, 100, 70, 180, 110
        )
        self._send_command(f"10.0.2.2:{self.http_port}", submit=False)
        deadline = time.monotonic() + 3
        browser_input = browser_before
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            browser_input = self._framebuffer_region(
                framebuffer, 100, 70, 180, 110
            )
            if browser_input != browser_before:
                break
            time.sleep(0.05)
        self.assertNotEqual(browser_input, browser_before)
        browser_page_before = browser_input
        self._send_key("ret")
        deadline = time.monotonic() + 10
        browser_after = browser_page_before
        browser_text_pixels = 0
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            browser_after = self._framebuffer_region(
                framebuffer, 100, 70, 180, 110
            )
            browser_text_pixels = sum(
                framebuffer[row * 320 + column] == 1
                for row in range(91, 99)
                for column in range(104, 224)
            ) if len(framebuffer) == GRAPHICS_BYTES else 0
            if browser_after != browser_page_before and browser_text_pixels > 30:
                break
            time.sleep(0.05)
        status_colors = {
            color: sum(
                framebuffer[row * 320 + column] == color
                for row in range(82, 90)
                for column in range(104, 224)
            )
            for color in (4, 5, 10, 12, 14)
        }
        self.assertNotEqual(
            browser_after,
            browser_page_before,
            f"request={BrowserTestHandler.last_request_line!r}, "
            f"error={BrowserTestHandler.last_error!r}, "
            f"status colors={status_colors}",
        )
        self.assertGreater(
            browser_text_pixels,
            30,
            f"request={BrowserTestHandler.last_request_line!r}, "
            f"error={BrowserTestHandler.last_error!r}, "
            f"status colors={status_colors}",
        )
        self.assertEqual(
            BrowserTestHandler.last_host,
            f"10.0.2.2:{self.http_port}",
            f"request={BrowserTestHandler.last_request_line!r}, "
            f"error={BrowserTestHandler.last_error!r}, "
            f"status colors={status_colors}",
        )
        self._send_key("esc")
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            if "Returned from TexugOS Desktop." in screen:
                break
            time.sleep(0.05)
        self.assertIn("Returned from TexugOS Desktop.", screen)
        self._send_command("desktop")
        time.sleep(0.2)
        self._send_key("3")
        time.sleep(0.2)
        self._send_command("http://localhost:80/")
        self._send_key("ret")
        deadline = time.monotonic() + 10
        http_failure_pixels = 0
        dns_failure_pixels = 0
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            http_failure_pixels = sum(
                framebuffer[row * 320 + column] == 5
                for row in range(82, 90)
                for column in range(104, 224)
            )
            dns_failure_pixels = sum(
                framebuffer[row * 320 + column] == 12
                for row in range(82, 90)
                for column in range(104, 224)
            )
            if http_failure_pixels > 10 or dns_failure_pixels > 10:
                break
            time.sleep(0.05)
        self.assertGreater(
            http_failure_pixels, 10,
            f"HTTP failure pixels={http_failure_pixels}, "
            f"DNS failure pixels={dns_failure_pixels}",
        )
        self._send_key("esc")
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            if "Returned from TexugOS Desktop." in screen:
                break
            time.sleep(0.05)
        self.assertIn("Returned from TexugOS Desktop.", screen)
        self._send_command("desktop")
        time.sleep(0.2)
        self._send_key("5")
        self._send_command("note.txt", submit=False)
        self._send_key("tab")
        self._send_command("hello", submit=False)
        time.sleep(0.2)
        self._move_mouse_to(50, 75, 594, 66)
        self._click_mouse()
        self._move_mouse_to(594, 66, 122, 191)
        self._click_mouse()
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            if "Returned from TexugOS Desktop." in screen:
                break
            time.sleep(0.05)
        self.assertIn("Returned from TexugOS Desktop.", screen)
        width, height, pixels = self._read_screenshot()
        blank_row = pixels[(height - 1) * width * 3 :]
        blank_colors = {
            blank_row[index : index + 3]
            for index in range(0, len(blank_row), 3)
        }
        self.assertEqual(len(blank_colors), 1)
        self._run_shell_command("cat note.txt", "hello")

    def test_zz_invalid_fsinfo_does_not_block_writes(self):
        cls = type(self)
        cls._stop_qemu()
        cls.qemu_image = pathlib.Path(cls.tempdir.name) / "invalid-fsinfo.img"
        shutil.copyfile(IMAGE_PATH, cls.qemu_image)

        with cls.qemu_image.open("r+b") as image_file:
            mbr = image_file.read(SECTOR_SIZE)
            partition_start = struct.unpack_from("<I", mbr, 454)[0]
            image_file.seek(partition_start * SECTOR_SIZE)
            boot_sector = image_file.read(SECTOR_SIZE)
            fsinfo_sector = struct.unpack_from("<H", boot_sector, 48)[0]
            backup_sector = struct.unpack_from("<H", boot_sector, 50)[0]
            reserved_sectors = struct.unpack_from("<H", boot_sector, 14)[0]
            fat_count = boot_sector[16]
            fat_size = struct.unpack_from("<I", boot_sector, 36)[0]
            sectors_per_cluster = boot_sector[13]
            root_cluster = struct.unpack_from("<I", boot_sector, 44)[0]
            fsinfo_lbas = [partition_start + fsinfo_sector]
            if backup_sector not in (0, 0xffff):
                fsinfo_lbas.append(
                    partition_start + backup_sector + fsinfo_sector
                )
            for lba in fsinfo_lbas:
                image_file.seek(lba * SECTOR_SIZE)
                fsinfo = bytearray(image_file.read(SECTOR_SIZE))
                struct.pack_into("<I", fsinfo, 0, 0)
                struct.pack_into("<I", fsinfo, 484, 0)
                image_file.seek(lba * SECTOR_SIZE)
                image_file.write(fsinfo)
            data_start = (
                partition_start + reserved_sectors + fat_count * fat_size
            )
            root_lba = data_start + (root_cluster - 2) * sectors_per_cluster
            image_file.seek(root_lba * SECTOR_SIZE)
            root_sector = bytearray(image_file.read(SECTOR_SIZE))
            user_file = bytearray(32)
            user_file[:11] = b"USER    TXT"
            user_file[11] = 0x20
            root_sector[:32] = user_file
            image_file.seek(root_lba * SECTOR_SIZE)
            image_file.write(root_sector)

        cls.qmp_path = pathlib.Path(cls.tempdir.name) / "qmp-invalid-fsinfo.sock"
        cls.vga_path = pathlib.Path(cls.tempdir.name) / "vga-invalid-fsinfo.bin"
        cls.process = subprocess.Popen(
            [
                "qemu-system-i386",
                "-display",
                "none",
                "-no-reboot",
                "-no-shutdown",
                "-netdev",
                "user,id=net0",
                "-device",
                "rtl8139,netdev=net0",
                "-drive",
                f"file={cls.qemu_image},format=raw,if=ide",
                "-boot",
                "c",
                "-qmp",
                f"unix:{cls.qmp_path},server=on,wait=off",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        cls._connect_qmp()
        cls._execute_qmp({"execute": "qmp_capabilities"})

        deadline = time.monotonic() + 10
        screen = ""
        while time.monotonic() < deadline:
            screen = cls._read_screen()
            if "TexugOS shell ready." in screen:
                break
            time.sleep(0.1)
        self.assertIn(
            "FAT32: mount, CRUD and LFN test passed",
            screen,
            f"filesystem self-test failed on invalid FSInfo:\n{screen}",
        )
        self.assertIn("TexugOS shell ready.", screen)


if __name__ == "__main__":
    unittest.main()
