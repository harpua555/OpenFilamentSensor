#!/usr/bin/env python3
"""
PlatformIO pre-build script to inject current build timestamp as compile flags.
This ensures the build timestamp updates on every build, not just when source files change.
"""
import os
from datetime import datetime
Import("env")


def _as_string_literal(value: str) -> str:
    """
    Produce a C-style quoted string suitable for embedding in CPPDEFINES.
    
    Parameters:
        value (str): The raw string to convert into a C string literal payload.
    
    Returns:
        str: The input wrapped in escaped double quotes with internal double quotes escaped, suitable for use as a C string literal in CPPDEFINES (no additional surrounding quotes).
    """
    escaped = value.replace('"', r'\"')
    return f'\\"{escaped}\\"'

# Generate current timestamp
now = datetime.now()

# Create build flags with current date and time
build_date = now.strftime("%b %d %Y")  # Format like __DATE__: "Nov 27 2025"
build_time = now.strftime("%H:%M:%S")  # Format like __TIME__: "14:30:45"

# Add build flags that will override __DATE__ and __TIME__
env.Append(CPPDEFINES=[
    ('BUILD_DATE', f'\\"{build_date}\\"'),
    ('BUILD_TIME', f'\\"{build_time}\\"'),
])

print(f"Build timestamp set to: {build_date} {build_time}")

chip_family = os.environ.get("CHIP_FAMILY", "").strip()
if chip_family:
    # Forward CHIP_FAMILY into the compiler in a safe way even if it contains spaces.
    env.Append(CPPDEFINES=[("CHIP_FAMILY_RAW", _as_string_literal(chip_family))])
    print(f"CHIP_FAMILY define set to: {chip_family}")

firmware_label = os.environ.get("FIRMWARE_VERSION", "").strip()
if firmware_label:
    env.Append(CPPDEFINES=[("FIRMWARE_VERSION_RAW", _as_string_literal(firmware_label))])
    print(f"FIRMWARE_VERSION define set to: {firmware_label}")