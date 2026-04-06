# create_flash_package.py
import os
import shutil
from pathlib import Path

# Define targets: pio env name, subdirectory name, bootloader address
TARGETS = [
    {
        "env": "esp32-s3",
        "subdir": "esp32-s3",
        "bootloader_addr": "0x0000",
    },
    {
        "env": "esp32-c3",
        "subdir": "esp32-c3",
        "bootloader_addr": "0x0000",
    },
]

# Common flash addresses (from partitions_4M.csv)
COMMON_FILES = {
    "partitions.bin": "0x8000",
    "firmware.bin":   "0x10000",
    "littlefs.bin":   "0x3D0000",
}


def build_and_package(target: dict, base_release_dir: Path):
    env = target["env"]
    subdir = target["subdir"]
    bootloader_addr = target["bootloader_addr"]

    print(f"\n{'='*50}")
    print(f"  Building target: {env}")
    print(f"{'='*50}")

    # Build firmware + filesystem
    ret = os.system(f"~/.platformio/penv/bin/pio run -e {env}")
    if ret != 0:
        print(f"ERROR: firmware build failed for {env}")
        return False
    ret = os.system(f"~/.platformio/penv/bin/pio run --target buildfs -e {env}")
    if ret != 0:
        print(f"ERROR: filesystem build failed for {env}")
        return False

    build_dir = Path(f".pio/build/{env}")
    release_dir = base_release_dir / subdir
    release_dir.mkdir(parents=True, exist_ok=True)

    # Full file list including bootloader with target-specific address
    files = {"bootloader.bin": bootloader_addr, **COMMON_FILES}

    # Copy files
    for filename, address in files.items():
        src = build_dir / filename
        if src.exists():
            shutil.copy2(src, release_dir / filename)
            print(f"  Copied {filename} -> {address}")
        else:
            print(f"  WARNING: Missing {filename} in {build_dir}")

    # Create flash addresses file
    with open(release_dir / "flash_addresses.txt", "w") as f:
        f.write(f"ESP Flash Download Tool Settings ({env}):\n")
        f.write("=" * 50 + "\n\n")
        f.write(f"{'File Name':<18} {'Address'}\n")
        f.write("-" * 30 + "\n")
        for filename, address in files.items():
            f.write(f"{filename:<18} {address}\n")
        f.write("\nFlash Settings:\n")
        f.write("- SPI Speed: 40MHz\n")
        f.write("- SPI Mode: DIO\n")
        f.write("- Flash Size: 4MB\n")

    print(f"  Package ready: {release_dir}")
    return True


def create_flash_package():
    """Create ESP Flash Download Tool packages for all targets."""
    base_release_dir = Path("shotstopper_flash_package")

    # Clean previous package
    if base_release_dir.exists():
        shutil.rmtree(base_release_dir)

    success = True
    for target in TARGETS:
        if not build_and_package(target, base_release_dir):
            success = False

    print(f"\n{'='*50}")
    if success:
        print("All flash packages created successfully!")
    else:
        print("Some targets failed - check output above.")
    print(f"Output directory: {base_release_dir}")


if __name__ == "__main__":
    create_flash_package()
