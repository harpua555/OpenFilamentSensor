#!/bin/bash

# Build script for lightweight WebUI (Linux/Mac)

echo "Building lightweight WebUI..."
node build.js

if [ $? -eq 0 ]; then
    echo ""
    echo "Build successful!"
else
    echo ""
    echo "Build failed!"
    exit 1
fi
