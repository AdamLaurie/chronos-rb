#!/bin/bash
# Flash CHRONOS-Rb firmware to Pico 2
# Usage: ./flash.sh [path_to_uf2]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
UF2_FILE="${1:-$SCRIPT_DIR/build/chronos_rb.uf2}"

if [ ! -f "$UF2_FILE" ]; then
    echo "Error: UF2 file not found: $UF2_FILE"
    echo "Build first with: cd build && make"
    exit 1
fi

echo "Rebooting into bootloader..."
echo "reboot bl" > /dev/ttyACM0 2>/dev/null || true

echo "Waiting for RP2350 to mount..."
for i in 1 2 3 4 5 6 7 8 9 10; do
    MOUNT_DIR=$(find /media -maxdepth 2 -name "RP2350" -type d 2>/dev/null | head -1)
    [ -n "$MOUNT_DIR" ] && break
    sleep 1
done

if [ -z "$MOUNT_DIR" ]; then
    echo "Error: RP2350 not found. Is the Pico connected?"
    exit 1
fi

echo "Copying $UF2_FILE to $MOUNT_DIR..."
cp "$UF2_FILE" "$MOUNT_DIR/"
echo "Flashed successfully!"
