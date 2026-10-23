"""Check the production post-build action without requiring an ESP toolchain."""

from pathlib import Path
import runpy
import unittest
from unittest.mock import patch


class BuildEnvironment(dict):
    def subst(self, value):
        return {
            "$BUILD_DIR": "/tmp/scms build",
            "$PYTHONEXE": "/tmp/python with spaces",
            "$ESP32_APP_OFFSET": "0x20000",
            "$BOOTLOADER": "/tmp/scms build/bootloader.bin",
        }.get(value, value)

    def BoardConfig(self):
        return {"build.mcu": "esp32", "upload.flash_size": "4MB"}

    def PioPlatform(self):
        return self

    def get_package_dir(self, package):
        return "/tmp/tool packages/" + package

    def AddPostAction(self, target, callback):
        self.target = target
        self.callback = callback


class MergeFirmwareTest(unittest.TestCase):
    def test_actual_image_offsets_and_paths_are_preserved(self):
        env = BuildEnvironment(FLASH_EXTRA_IMAGES=[
            (0x1000, "$BOOTLOADER"),
            ("0x8000", "/tmp/scms build/partitions.bin"),
            ("0xe000", "/tmp/scms build/ota_data_initial.bin"),
        ])
        script = Path(__file__).resolve().parents[1] / "merge_firmware.py"
        runpy.run_path(str(script), init_globals={
            "env": env, "Import": lambda name: None,
        })
        self.assertEqual(env.target, "$BUILD_DIR/${PROGNAME}.bin")
        with patch("subprocess.run") as run:
            env.callback([], ["/tmp/scms build/firmware.bin"], env)
        command = run.call_args.args[0]
        self.assertEqual(command[:2], [
            "/tmp/python with spaces", "/tmp/tool packages/tool-esptoolpy/esptool.py",
        ])
        self.assertEqual(command[-8:], [
            "0x1000", "/tmp/scms build/bootloader.bin",
            "0x8000", "/tmp/scms build/partitions.bin",
            "0xe000", "/tmp/scms build/ota_data_initial.bin",
            "0x20000", "/tmp/scms build/firmware.bin",
        ])
        self.assertIn("/tmp/scms build/firmware.merged.bin", command)
        self.assertTrue(run.call_args.kwargs["check"])


if __name__ == "__main__":
    unittest.main()
