# ⚛️ CHRONOS-Rb

**C**ompact **H**igh-precision **R**ubidium **O**scillator **N**etwork **O**perating **S**ystem

A Stratum-1 NTP/PTP time server for Raspberry Pi Pico 2-W, disciplined by an FE-5680A rubidium atomic frequency standard.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-RP2350-green.svg)
![Status](https://img.shields.io/badge/status-beta-yellow.svg)

## 🎯 Features

- **Stratum-1 NTP Server** - Direct atomic reference, no upstream servers needed
- **IEEE 1588 PTP Support** - Sub-microsecond precision time protocol
- **Rubidium Disciplined** - 10⁻¹¹ frequency stability from FE-5680A
- **WiFi Connected** - Serves time over 802.11 b/g/n
- **Web Interface** - Real-time status and configuration
- **JSON API** - Integration with monitoring systems
- **OTA Updates** - Encrypted firmware updates over WiFi with automatic rollback
- **PIO Precision** - Hardware-timed capture for <1µs accuracy
- **Automatic Holdover** - Maintains accuracy during reference loss
- **Interval Pulse Outputs** - 0.5s, 1s, 6s, 30s, 60s timing signals

## 📊 Specifications

| Parameter | Value |
|-----------|-------|
| NTP Stratum | 1 (when locked) |
| Time Accuracy | < 1 µs to UTC |
| Frequency Stability | < 5×10⁻¹¹ (1 day) |
| PPS Jitter | < 100 ns |
| Warmup Time | 3-5 minutes |
| Power Consumption | ~35W during warmup, ~12W running |

## 🔧 Hardware Requirements

### Main Components

1. **Raspberry Pi Pico 2-W** - RP2350 with WiFi
2. **FE-5680A Rubidium Oscillator** - 10MHz output (DB-9 version)
3. **GPS Module** - u-blox NEO-6M or similar with 1PPS output (~$10-15)
4. **15V 3A Power Supply** - For rubidium physics package
5. **LT1016 or MAX999 Comparator** - 10MHz sine-to-square conversion
6. **Signal Conditioning Components** - See BOM in documentation

### Block Diagram

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│   FE-5680A      │     │ Signal           │     │ Raspberry Pi    │
│   Rubidium      │────▶│ Conditioning     │────▶│ Pico 2-W        │
│   Oscillator    │     │ Circuit          │     │                 │
└─────────────────┘     └──────────────────┘     └────────┬────────┘
       │                        ▲                         │
       │ 10MHz Sine             │ 1PPS + NMEA             │ WiFi
       │ Lock Status            │                         ▼
       │                 ┌──────┴───────┐         ┌───────────────┐
       │                 │  GPS Module  │         │ NTP/PTP       │
       │                 │  (NEO-6M)    │         │ Clients       │
       │                 └──────────────┘         └───────────────┘
       │
       └─ Rb provides frequency stability (10⁻¹¹)
          GPS provides UTC alignment (±10ns)
```

## 📁 Project Structure

```
chronos-rb/
├── firmware/
│   ├── CMakeLists.txt          # Build configuration
│   ├── flash.sh                # Flashing helper script
│   ├── ota_key.txt             # AES encryption key (gitignored)
│   ├── deps/
│   │   └── pico_fota_bootloader/  # A/B partition bootloader
│   ├── include/
│   │   ├── chronos_rb.h        # Main header with configs
│   │   └── ota_update.h        # OTA update API
│   └── src/
│       ├── main.c              # Entry point
│       ├── pps_capture.c       # 1PPS timing capture
│       ├── pps_capture.pio     # PIO program for PPS
│       ├── freq_counter.c      # 10MHz measurement
│       ├── freq_counter.pio    # PIO program for freq
│       ├── rubidium_sync.c     # Rb sync state machine
│       ├── time_discipline.c   # PI controller
│       ├── ntp_server.c        # NTPv4 implementation
│       ├── ptp_server.c        # IEEE 1588 PTP
│       ├── wifi_manager.c      # WiFi handling
│       ├── web_interface.c     # HTTP status page + OTA
│       └── ota_update.c        # OTA firmware updates
├── hardware/
│   └── schematics/             # KiCad files (future)
└── docs/
    └── CHRONOS-Rb_Hardware_Guide.docx
```

## 🚀 Quick Start

### 1. Configure WiFi

Edit `firmware/include/chronos_rb.h`:

```c
#define WIFI_SSID_DEFAULT    "YourNetwork"
#define WIFI_PASS_DEFAULT    "YourPassword"
#define WIFI_COUNTRY         "US"
```

### 2. Build Firmware

```bash
cd firmware
mkdir build && cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
make -j4
```

**Build outputs:**
| File | Purpose |
|------|---------|
| `pico_fota_bootloader.uf2` | A/B partition bootloader (flash first) |
| `chronos_rb.uf2` | Application firmware |
| `chronos_rb_fota_image_encrypted.bin` | Encrypted image for OTA updates |

### 3. Flash to Pico (First Time)

For new devices, flash the bootloader first, then the application:

**Option A: Using make targets**
```bash
# Flash bootloader, wait for reboot, then flash application
make flash-initial
```

**Option B: Manual BOOTSEL flashing**
```bash
# Step 1: Hold BOOTSEL, connect USB, copy bootloader
cp deps/pico_fota_bootloader/pico_fota_bootloader.uf2 /media/$USER/RP2350/

# Step 2: Hold BOOTSEL again, copy application
cp chronos_rb.uf2 /media/$USER/RP2350/
```

**Subsequent updates** can use OTA (see below) or:
```bash
make flash-app
```

### 4. Wire Hardware

See the Hardware Guide document for complete schematics. Key connections:

| Pico GPIO | Signal | Source |
|-----------|--------|--------|
| GP0 | GPS TX (UART0 TX) | To GPS module RX |
| GP1 | GPS RX (UART0 RX) | From GPS module TX |
| GP2 | 1PPS Input | From GPS module PPS |
| GP20 | 10MHz Input | FE-5680A via comparator |
| GP4/GP5 | I2C0 (optional) | For OLED display |
| GP6-9 | Status LEDs | With 330Ω resistors |
| GP22 | Lock Status | FE-5680A pin 3 via NPN level shifter |
| GP14 | 0.5s Pulse | Interval output |
| GP15 | 1s Pulse | Interval output |
| GP16 | 6s Pulse | Interval output |
| GP17 | 30s Pulse | Interval output |
| GP18 | 60s Pulse | Interval output |

### 5. Power Up

1. Connect 15V to FE-5680A
2. Wait 3-5 minutes for rubidium lock
3. Green LED indicates sync achieved
4. Access web UI at `http://<ip-address>/`

## 📡 Using the Time Server

### NTP Client (Linux)

```bash
# Test connection
ntpdate -q 192.168.1.100

# Configure as time source
sudo nano /etc/systemd/timesyncd.conf
# Add: NTP=192.168.1.100
sudo systemctl restart systemd-timesyncd
```

### NTP Client (Windows)

```powershell
w32tm /config /manualpeerlist:192.168.1.100 /syncfromflags:manual /update
w32tm /resync
```

### Web Interface

Navigate to `http://<device-ip>/` for real-time status:

- Sync state (INIT → FREQ_CAL → COARSE → FINE → LOCKED)
- Time offset (nanoseconds)
- Frequency correction (ppb)
- NTP request count
- PTP sync statistics

### JSON API

```bash
curl http://192.168.1.100/api/status
```

```json
{
  "sync_state": 4,
  "rb_locked": true,
  "time_valid": true,
  "offset_ns": 127,
  "freq_offset_ppb": 0.023,
  "pps_count": 3847,
  "ntp_requests": 1542
}
```

## 📦 OTA Firmware Updates

CHRONOS-Rb supports encrypted over-the-air updates with automatic rollback protection.

### How It Works

- **A/B Partitioning**: Two firmware slots allow safe updates without bricking
- **AES-128 Encryption**: Firmware images are encrypted with a device-specific key
- **SHA-256 Validation**: Images are verified before applying
- **Automatic Rollback**: If the new firmware fails to boot 3 times, the previous version is restored
- **Boot Confirmation**: Firmware must run successfully for 60 seconds to be marked as good

### Web Interface Update

1. Navigate to `http://<device-ip>/ota`
2. Click "Choose File" and select `chronos_rb_fota_image_encrypted.bin`
3. Click "Upload Firmware" and wait for upload + validation
4. Click "Apply & Reboot" to install the update

### API Update

```bash
# Get OTA status
curl http://192.168.1.100/api/ota/status

# Upload firmware (chunked)
FILE="chronos_rb_fota_image_encrypted.bin"
SIZE=$(stat -c%s "$FILE")

# Begin upload
curl -X POST -H "X-OTA-Size: $SIZE" http://192.168.1.100/api/ota/begin

# Upload in chunks (1KB each)
split -b 1024 "$FILE" /tmp/chunk_
for chunk in /tmp/chunk_*; do
    curl -X POST --data-binary @"$chunk" \
         -H "Content-Type: application/octet-stream" \
         http://192.168.1.100/api/ota/chunk
done

# Finalize and validate
curl -X POST http://192.168.1.100/api/ota/finish

# Apply update (device will reboot)
curl -X POST http://192.168.1.100/api/ota/apply
```

### Encryption Key

The first build generates a random AES-128 key in `firmware/ota_key.txt`. This file is gitignored to keep it secret.

**Important:**
- Keep `ota_key.txt` backed up - you need it to create valid update images
- All firmware builds must use the same key to be accepted by the device
- If you lose the key, you must reflash via BOOTSEL

### Recovery

If an update fails and automatic rollback doesn't work:

1. Hold BOOTSEL button while connecting USB
2. Copy `pico_fota_bootloader.uf2` to the drive
3. Wait for reboot, hold BOOTSEL again
4. Copy `chronos_rb.uf2` to the drive

## 🔬 How It Works

### Time Discipline Loop

```
    ┌─────────────────────────────────────────────────┐
    │                                                 │
    ▼                                                 │
┌───────┐    ┌──────────┐    ┌────────────┐    ┌─────┴─────┐
│ 1PPS  │───▶│ Capture  │───▶│ Calculate  │───▶│    PI     │
│ Input │    │ (PIO)    │    │  Offset    │    │ Controller│
└───────┘    └──────────┘    └────────────┘    └───────────┘
                                                     │
                                                     ▼
┌───────────┐    ┌──────────┐    ┌────────────┐    ┌─────────┐
│ Timestamp │◀───│ Apply    │◀───│ Frequency  │◀───│Correction│
│ Output    │    │ Correction│   │  Adjust    │    │  (ppb)  │
└───────────┘    └──────────┘    └────────────┘    └─────────┘
```

### Sync State Machine

```
INIT ──▶ FREQ_CAL ──▶ COARSE ──▶ FINE ──▶ LOCKED
  │          │           │          │         │
  └──────────┴───────────┴──────────┴─────────┘
                         │
                    (on error)
                         ▼
                     HOLDOVER ──▶ ERROR
```

## 📐 Signal Conditioning

### 10MHz Sine to Square Converter

The FE-5680A outputs a ~860mVpp sine wave at ~0.7V DC. An LT1016 high-speed comparator converts this to 3.3V LVCMOS for the Pico:

```
           +5V         +5V
            │           │
           R2          ┌┴┐
          (6.8k)       │C1│ 0.1µF
            │          └┬┘
FE-5680A    │           │
Pin 7 ─────R1────┬─────Pin 1 (V+)
(10MHz)   (100Ω) │
                 │    ┌────────────┐
            Pin 2├────┤            ├─Pin 8──R6───LED──GND
            (+IN)│    │   LT1016   │       (500Ω) (purple)
                 │    │            │
            Pin 3├────┤            ├─Pin 7──R4───┬───► GP20
            (-IN)│    └────────────┘       (100Ω)│    (~3V)
                 │     │   │   │              R5
                R3    Pin4 Pin5 Pin6         (220Ω)
               (1k)    │   │   │              │
                │      └───┴───┴──────────────┴───GND
               GND

Threshold: ~0.64V from R2/R3 divider
Level shift: 5V→3V via R4/R5 divider
LED: On Q̅ (pin 8), lights when output LOW (inverted indicator)
```

**Components:**
- R1: 100Ω (input series resistor)
- R2: 6.8kΩ (threshold divider, top)
- R3: 1kΩ (threshold divider, bottom) → ~0.64V at -IN
- R4: 100Ω (level shifter, top)
- R5: 220Ω (level shifter, bottom) → ~3V output
- R6: 500Ω (LED current limit)
- C1: 0.1µF (bypass capacitor)
- U1: LT1016 (5V supply, 10ns comparator)
- 1× Purple LED (10MHz signal indicator, inverted)

**Notes:**
- No hysteresis feedback needed for 860mVpp signal
- LED on Q̅ (pin 8) to reduce loading on Q (pin 7)
- Lower value level shifter resistors prevent RC filtering at 10MHz

### GPS Module (Required)

A GPS module provides two critical functions:
1. **1PPS signal** - UTC-aligned pulse marking exact second boundaries (±10ns)
2. **Time-of-day** - NMEA sentences with current UTC time via UART

The firmware counts 10MHz cycles from the rubidium oscillator and verifies against GPS 1PPS to ensure exactly 10,000,000 cycles per second. This provides rubidium-level frequency stability with GPS-level UTC alignment.

#### Recommended Module: u-blox NEO-6M

Widely available as breakout boards (GY-GPS6MV2, GY-NEO6MV2):
- **Price:** ~$10-15
- **1PPS accuracy:** ±10ns to UTC
- **Interface:** UART (9600 baud default) + 1PPS output
- **Voltage:** 3.3-5V (check your specific board)

**Alternative:** BN-220 (~$15-20) - smaller form factor, same chipset

#### Wiring

```
GPS Module                              Pico
───────────                             ────
VCC ────────────────────────────────── 3.3V (or 5V if module requires)
GND ────────────────────────────────── GND
TX  ────────────────────────────────── GP1 (UART0 RX)
RX  ────────────────────────────────── GP0 (UART0 TX)
PPS ────────────────────────────────── GP2 (1PPS input)
```

**Note:** Most 3.3V GPS modules can connect directly. For 5V modules, add level shifting on PPS:

```
GPS 1PPS ──── R4 ────┬──── To Pico GP2
(5V)        (2.2k)   │
                     R5
                    (3.3k)
                     │
                    GND

Divider: 5V × 3.3k/(2.2k+3.3k) ≈ 3.0V
```

### Lock Status Level Shifter

The FE-5680A lock output (Pin 3) is 4.8V when unlocked, 0.8V when locked. An NPN transistor inverts and level-shifts this to 3.3V logic:

```
                      +3.3V
                        │
FE-5680A               R8 (1k)              +3.3V
Pin 3 ─── R6 ───┬───── B     C ─────┬─────── GP22 (HIGH = locked)
(Lock)   (22k)  │      ┌─────┐      │
                │      │2N3904│      ├─ R9 ─── GREEN LED ─── GND
           R7 ──┴─ GND │  Q1  │      │  (330Ω)   (locked)
          (10k)        └──┬──┘       │
                          E          └─ R10 ── YELLOW LED ── +3.3V
                          │             (330Ω)  (unlocked)
                         GND

Operation:
- Unlocked (4.8V): Q1 ON  → collector LOW  → YELLOW on,  GREEN off
- Locked (0.8V):   Q1 OFF → collector HIGH → YELLOW off, GREEN on
```

**Components:**
- 1× 2N3904 NPN transistor
- R6: 22kΩ (base input)
- R7: 10kΩ (base to GND)
- R8: 1kΩ (collector pull-up to 3.3V)
- R9: 330Ω (green LED current limit)
- R10: 330Ω (yellow LED current limit)
- 1× Green LED (indicates locked)
- 1× Yellow LED (indicates unlocked/warmup)

## ⏱️ Interval Pulse Outputs

CHRONOS-Rb provides five precision timing outputs synchronized to the rubidium 1PPS reference:

| GPIO | Interval | Use Case |
|------|----------|----------|
| GP14 | 0.5 second | High-rate timing, servo synchronization |
| GP15 | 1 second | PPS distribution, general timing |
| GP16 | 6 second | GPS-compatible timing intervals |
| GP17 | 30 second | Calibration triggers |
| GP18 | 60 second | Minute markers, event logging |

**Specifications:**
- Output level: 3.3V LVCMOS (active high)
- Pulse width: 10ms
- Timing accuracy: Phase-locked to atomic 1PPS reference
- Drive capability: 12mA (use buffer IC for cables/higher loads)

## 🐛 Troubleshooting

| Issue | Cause | Solution |
|-------|-------|----------|
| No lock after 10 min | Power issue | Check +15V supply (needs 2A) |
| Erratic 1PPS | Bad connection | Check signal conditioning |
| WiFi fails | Wrong region | Set correct WIFI_COUNTRY |
| High offset | Comparator issue | Verify 10MHz square wave |
| NTP timeout | Firewall | Open UDP port 123 |

## 📜 License

MIT License - See LICENSE file for details.

## 🙏 Acknowledgments

- Raspberry Pi Foundation for the Pico SDK
- The time-nuts community for rubidium oscillator knowledge
- NTP and PTP specification authors

---

**CHRONOS-Rb** - *Because every nanosecond counts* ⚛️⏱️
