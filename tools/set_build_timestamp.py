#!/usr/bin/env python3
"""
PlatformIO pre-build script to inject current build timestamp as compile flags.
This ensures the build timestamp updates on every build, not just when source files change.
"""
from datetime import datetime
Import("env")

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
