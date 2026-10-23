"""PlatformIO post-build action: include the actual bootloader/partition images."""

from pathlib import Path
import subprocess

Import("env")


def merge_firmware(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    esptool = Path(env.PioPlatform().get_package_dir("tool-esptoolpy")) / "esptool.py"
    board = env.BoardConfig()
    images = [(offset, env.subst(path)) for offset, path in env.get("FLASH_EXTRA_IMAGES", [])]
    images.append((env.subst("$ESP32_APP_OFFSET"), str(target[0])))
    command = [
        env.subst("$PYTHONEXE"), str(esptool),
        "--chip", board.get("build.mcu", "esp32"),
        "merge_bin", "-o", str(build_dir / "firmware.merged.bin"),
        "--flash_mode", board.get("build.flash_mode", "dio"),
        "--flash_size", board.get("upload.flash_size", "4MB"),
    ]
    for offset, path in images:
        command.extend([offset if isinstance(offset, str) else hex(offset), path])
    subprocess.run(command, check=True)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_firmware)
