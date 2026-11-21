import pathlib
import subprocess
import sys
import tempfile
import unittest

TOOL = pathlib.Path(__file__).parents[1] / "tools" / "mktef.py"


class TefPackagerTests(unittest.TestCase):
    def test_packages_name_icon_and_binary(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary = pathlib.Path(temporary_directory)
            binary = temporary / "program.bin"
            output = temporary / "program.tef"
            binary.write_bytes(b"\xb8\x02\x00\x00\x00\xcd\x80")
            subprocess.run(
                [
                    sys.executable,
                    str(TOOL),
                    "--name",
                    "Demo",
                    "--binary",
                    str(binary),
                    "--icon",
                    "3c7e5a5a7e3c1818",
                    "--output",
                    str(output),
                ],
                check=True,
            )
            package = output.read_bytes()
            self.assertEqual(package[:4], b"TEF\0")
            self.assertEqual(int.from_bytes(package[6:8], "little"), 36)
            self.assertEqual(package[24:28], b"Demo")
            self.assertEqual(package[28:36], bytes.fromhex("3c7e5a5a7e3c1818"))
            self.assertEqual(package[36:], binary.read_bytes())


if __name__ == "__main__":
    unittest.main()
