import json
import os
import pathlib
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import unittest

ISO_PATH = pathlib.Path(
    os.environ.get("TEXUGOS_ISO", "build/texugos.iso")
).resolve()
DISK_PATH = pathlib.Path(
    os.environ.get("TEXUGOS_IMAGE", "build/texugos.img")
).resolve()
VGA_BYTES = 80 * 25 * 2


@unittest.skipUnless(
    os.environ.get("TEXUGOS_TEST_ISO") == "1"
    and shutil.which("qemu-system-i386")
    and ISO_PATH.exists(),
    "run make test-iso with QEMU and xorriso to test El Torito boot",
)
class QemuIsoBootTests(unittest.TestCase):
    def test_el_torito_boot_with_writable_ata_disk(self):
        with tempfile.TemporaryDirectory(prefix="texugos-iso-qemu-") as tempdir:
            temp_path = pathlib.Path(tempdir)
            disk_path = temp_path / "texugos.img"
            qmp_path = temp_path / "qmp.sock"
            vga_path = temp_path / "vga.bin"
            shutil.copyfile(DISK_PATH, disk_path)
            process = subprocess.Popen(
                [
                    "qemu-system-i386",
                    "-display",
                    "none",
                    "-no-reboot",
                    "-no-shutdown",
                    "-drive",
                    f"file={disk_path},format=raw,if=ide,index=0",
                    "-cdrom",
                    str(ISO_PATH),
                    "-boot",
                    "d",
                    "-m",
                    "256M",
                    "-netdev",
                    "user,id=net0",
                    "-device",
                    "rtl8139,netdev=net0",
                    "-qmp",
                    f"unix:{qmp_path},server=on,wait=off",
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            connection = None
            try:
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        self.fail("QEMU exited before the QMP socket was ready")
                    try:
                        connection = socket.socket(
                            socket.AF_UNIX, socket.SOCK_STREAM
                        )
                        connection.settimeout(2)
                        connection.connect(str(qmp_path))
                        qmp_file = connection.makefile("rwb", buffering=0)
                        json.loads(qmp_file.readline())
                        break
                    except (FileNotFoundError, ConnectionRefusedError):
                        if connection is not None:
                            connection.close()
                        connection = None
                        time.sleep(0.05)
                else:
                    self.fail("QMP socket did not become ready")

                self._execute_qmp(qmp_file, {"execute": "qmp_capabilities"})
                deadline = time.monotonic() + 20
                screen = ""
                while time.monotonic() < deadline:
                    self._execute_qmp(
                        qmp_file,
                        {
                            "execute": "human-monitor-command",
                            "arguments": {
                                "command-line": (
                                    f'pmemsave 0xb8000 0x{VGA_BYTES:x} '
                                    f'"{vga_path}"'
                                )
                            },
                        },
                    )
                    if vga_path.exists() and vga_path.stat().st_size == VGA_BYTES:
                        memory = vga_path.read_bytes()
                        screen = "".join(
                            chr(memory[index])
                            for index in range(0, len(memory), 2)
                        )
                    if "TexugOS shell ready." in screen:
                        break
                    time.sleep(0.1)

                self.assertIn("TexugOS shell ready.", screen)
                self.assertIn("FAT32: mount, CRUD and LFN test passed", screen)
                self.assertIn(
                    "User mode: syscall and kernel-page isolation tests passed",
                    screen,
                )
            finally:
                if connection is not None:
                    connection.close()
                if process.poll() is None:
                    process.send_signal(signal.SIGTERM)
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)

    @staticmethod
    def _execute_qmp(qmp_file, command):
        qmp_file.write((json.dumps(command) + "\n").encode())
        while True:
            response = json.loads(qmp_file.readline())
            if "return" in response:
                return response["return"]
            if "error" in response:
                raise RuntimeError(response["error"])


if __name__ == "__main__":
    unittest.main()
