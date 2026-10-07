#!/usr/bin/env python3
"""Compile the pinned NeoPixelBus encoder creator with host SDK models."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("diagnostic", ROOT / "tools/core3-rmt-latch-diagnostic.py")
diagnostic = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnostic)


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise ValueError("Unclosed function: " + signature)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--patched", action="store_true", help="Test corrected reset timing in memory")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    data = (args.library / diagnostic.HEADER).read_bytes()
    original = diagnostic.transform(data, restore=True)
    patched = diagnostic.transform(original)
    assert diagnostic.transform(patched) == patched
    assert diagnostic.transform(patched, restore=True) == original
    assert diagnostic.transform(original, restore=True) == original
    try:
        diagnostic.transform(original + b"\n// changed source\n")
    except ValueError:
        pass
    else:
        raise AssertionError("Unexpected dependency source was accepted")
    print("PASS patch integrity, idempotence, restoration and unknown-source rejection", flush=True)
    source = (patched if args.patched else original).decode()
    fixture = (ROOT / "test/rmt-latch/fixture.cpp").read_text()
    speeds = (args.library / diagnostic.HEADER.parent / "NeoEsp32RmtSpeed.h").read_text().replace("#pragma once", "")
    structs = source[source.index("struct led_strip_encoder_config_t"):source.index("#define NEOPIXELBUS_RMT_INT_FLAGS")]
    fixture = fixture.replace("/* SPEED_CLASSES */", speeds).replace("/* ENCODER_STRUCTS */", structs)
    fixture = fixture.replace("/* DELETE_ENCODER */", function(source, "static esp_err_t rmt_del_led_strip_encoder("))
    fixture = fixture.replace("/* CREATE_ENCODER */", function(source, "static esp_err_t rmt_new_led_strip_encoder("))
    fixture = source[:source.index("#pragma once")] + fixture
    with tempfile.TemporaryDirectory(prefix="wled-rmt-latch-") as directory:
        cpp = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        cpp.write_text(fixture)
        command = [os.environ.get("CXX", "g++"), "-std=c++17", "-O0", "-Wall", "-Wextra"]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command + [str(cpp), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)])
        raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
