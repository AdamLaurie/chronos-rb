#!/bin/bash
# OTA firmware update for CHRONOS-Rb
# Usage: ./ota.sh <firmware.bin> [host]

set -e

FILE="$1"
HOST="${2:-172.16.13.40}"
CHUNK_SIZE=1024  # Must be multiple of 256, matches web interface

if [ ! -f "$FILE" ]; then
    echo "Usage: $0 <firmware_fota_image_encrypted.bin> [host]"
    exit 1
fi

SIZE=$(stat -c%s "$FILE")
CHUNKS=$(( (SIZE + CHUNK_SIZE - 1) / CHUNK_SIZE ))

echo "=== CHRONOS-Rb OTA Update ==="
echo "File: $FILE ($SIZE bytes, $CHUNKS chunks)"
echo "Host: $HOST"
echo ""

# Check device is reachable
echo "Checking device..."
if ! curl -s --max-time 3 "http://$HOST/api/status" > /dev/null; then
    echo "ERROR: Device not reachable at $HOST"
    exit 1
fi
OLD_VERSION=$(curl -s "http://$HOST/api/status" | grep -o '"version":"[^"]*"' | cut -d'"' -f4)
echo "Current version: $OLD_VERSION"
echo ""

# Begin OTA
echo "Starting OTA upload..."
RESP=$(curl -s -X POST "http://$HOST/api/ota/begin" -H "X-OTA-Size: $SIZE")
if [[ "$RESP" != *"OK"* ]]; then
    echo "ERROR: Failed to begin OTA: $RESP"
    exit 1
fi

# Send chunks
echo "Uploading firmware..."
OFFSET=0
CHUNK_NUM=0
while [ $OFFSET -lt $SIZE ]; do
    REMAINING=$((SIZE - OFFSET))
    if [ $REMAINING -lt $CHUNK_SIZE ]; then
        BYTES=$REMAINING
    else
        BYTES=$CHUNK_SIZE
    fi

    # Extract chunk and send
    RESP=$(dd if="$FILE" bs=$CHUNK_SIZE skip=$CHUNK_NUM count=1 2>/dev/null | \
        curl -s -X POST "http://$HOST/api/ota/chunk" \
        -H "Content-Type: application/octet-stream" \
        --data-binary @-)

    if [[ "$RESP" != *"OK"* ]]; then
        echo -e "\nERROR at chunk $CHUNK_NUM: $RESP"
        exit 1
    fi

    OFFSET=$((OFFSET + BYTES))
    CHUNK_NUM=$((CHUNK_NUM + 1))
    PERCENT=$((OFFSET * 100 / SIZE))
    printf "\r  Progress: %d/%d bytes (%d%%)" $OFFSET $SIZE $PERCENT
done
echo ""

# Finish and validate
echo "Validating firmware..."
RESP=$(curl -s -X POST "http://$HOST/api/ota/finish")
if [[ "$RESP" != *"OK"* ]]; then
    echo "ERROR: Validation failed: $RESP"
    exit 1
fi
echo "  Validation OK"

# Apply update
echo "Applying update and rebooting..."
curl -s -X POST "http://$HOST/api/ota/apply" > /dev/null &
sleep 2

# Wait for device to come back
echo "Waiting for device..."
for i in {1..30}; do
    sleep 2
    if curl -s --max-time 2 "http://$HOST/api/status" > /dev/null 2>&1; then
        NEW_VERSION=$(curl -s "http://$HOST/api/status" | grep -o '"version":"[^"]*"' | cut -d'"' -f4)
        echo ""
        echo "=== Update Complete ==="
        echo "Old version: $OLD_VERSION"
        echo "New version: $NEW_VERSION"
        if [ "$OLD_VERSION" = "$NEW_VERSION" ]; then
            echo "WARNING: Version unchanged - update may have rolled back"
            exit 1
        fi
        exit 0
    fi
    printf "."
done

echo ""
echo "ERROR: Device did not come back online"
exit 1
