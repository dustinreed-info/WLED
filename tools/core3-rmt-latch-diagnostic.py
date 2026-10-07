#!/usr/bin/env python3
"""Apply or restore one pinned NeoPixelBus reset-timing change for test builds.

Source: Makuna/NeoPixelBus commit 76afe832f74b0738a3fa1bba0caf389ade9e7693,
src/internal/methods/ESP/ESP32/NeoEsp32RmtXMethod.h.
No firmware is flashed and no PlatformIO configuration is changed.
"""
import argparse
import hashlib
import os
from pathlib import Path

HEADER = Path("src/internal/methods/ESP/ESP32/NeoEsp32RmtXMethod.h")
ORIGINAL_SHA256 = "873f04f31542043993106dddc483e42adbc7d30cb12198645354c46dbf097ee6"
OLD = b"uint32_t reset_ticks = config->resolution / 1000000 * 50 / 2; // reset code duration defaults to 50us"
NEW = b"uint32_t reset_ticks = T_SPEED::RmtDurationReset / 2; // use the selected protocol's reset interval"


def transform(data: bytes, restore: bool = False) -> bytes:
    """Only change the known header; accept repeated apply/restore operations."""
    if OLD in data and NEW not in data:
        original = data
    elif NEW in data and OLD not in data:
        original = data.replace(NEW, OLD)
    else:
        raise ValueError("Header has unexpected reset code; refusing to modify it")
    if hashlib.sha256(original).hexdigest() != ORIGINAL_SHA256 or original.count(OLD) != 1:
        raise ValueError("Header differs from the reviewed pinned version")
    return original if restore else original.replace(OLD, NEW)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path, help="PlatformIO NeoPixelBus library directory")
    parser.add_argument("--restore", action="store_true", help="Restore the original pinned header")
    parser.add_argument("--check", action="store_true", help="Verify source without writing")
    args = parser.parse_args()
    path = args.library / HEADER
    current = path.read_bytes()
    replacement = transform(current, args.restore)
    if not args.check and current != replacement:
        temp = path.with_suffix(path.suffix + ".tmp")
        temp.write_bytes(replacement)
        os.replace(temp, path)
    action = "verified" if args.check else "restored" if args.restore else "patched"
    print(f"{action}: {path}")
    print(f"SHA256: {hashlib.sha256(current if args.check else replacement).hexdigest()}")


if __name__ == "__main__":
    main()
