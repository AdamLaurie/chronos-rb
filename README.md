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
3. **1PPS Source** - Either 7× 74HC4017 divider chain OR GPS module (see Signal Conditioning)
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
       │                                                   │
       │                                                   │
       │ 10MHz Sine                                       │ WiFi
       │ Lock Status                                      ▼
       │                                          ┌───────────────┐
       └──────────────────────────────────────────│ NTP/PTP       │
                                                  │ Clients       │
                                                  └───────────────┘
```

## 📁 Project Structure

```
chronos-rb/
├── firmware/
│   ├── CMakeLists.txt          # Build configuration
│   ├── include/
│   │   └── chronos_rb.h        # Main header with configs
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
│       └── web_interface.c     # HTTP status page
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

### 3. Flash to Pico

Hold BOOTSEL button while connecting USB, then:

```bash
cp chronos_rb.uf2 /media/$USER/RPI-RP2/
```

### 4. Wire Hardware

See the Hardware Guide document for complete schematics. Key connections:

| Pico GPIO | Signal | Source |
|-----------|--------|--------|
| GP2 | 1PPS Input | From 10MHz divider or external GPS |
| GP3 | 10MHz Input | FE-5680A via comparator |
| GP4 | Lock Status | FE-5680A pin 3 via NPN level shifter |
| GP6-9 | Status LEDs | With 330Ω resistors |
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

The FE-5680A outputs a 1Vpp sine wave. A high-speed comparator converts this to 3.3V LVCMOS:

```
                +3.3V
                  │
                  R1 (10k)
                  │
10MHz ──┬── R2 ──┤+
Sine    │  (100)  │      LT1016
        C1        │               ──── 10MHz Square
       (100nF)    │                    to Pico GP3
        │    ┌────┤-
       GND   │    │
             R3   │
            (10k) │
             │    │
            GND  GND
```

### 1PPS Generation (Required)

**The FE-5680A DB-9 version does NOT have a 1PPS output.** You must generate it using one of these methods:

#### Option 1: Divide 10MHz (Recommended)

Use cascaded decade counters to divide 10MHz by 10,000,000:

```
10MHz ───┬──► 74HC4017 ──► 74HC4017 ──► 74HC4017 ──► ... ──► 1PPS
Square   │    (÷10)        (÷10)        (÷10)         (×7)
         │
         └──► To Pico GP3

Total: 7 × 74HC4017 decade counters in series
Each 74HC4017 divides by 10, so 10^7 = 10,000,000
```

**Components:**
- 7× 74HC4017 decade counter
- Bypass capacitors (100nF per IC)
- Output may need level shifting if not 3.3V logic

#### Option 2: External GPS Module

Use a GPS/GNSS module with 1PPS output (e.g., u-blox NEO-6M/7M/8M):

```
GPS Module
1PPS Out ──── R4 ────┬──── D1 ────┬──── To Pico GP2
(3.3-5V)    (2.2k)   │   (BAT54)  │
                     │            │
                     R5          GND
                    (3.3k)
                     │
                    GND

(Level shifter only needed if GPS outputs 5V)
```

This also provides a GPS time reference for initial time-of-day setting.

### Lock Status Level Shifter

The FE-5680A lock output (Pin 3) is 4.8V when unlocked, 0.8V when locked. An NPN transistor inverts and level-shifts this to 3.3V logic:

```
FE-5680A                                            To Pico
Pin 3 ────── R6 ──────┬─────── B ┌───┐ C ─────────── GP4
(Lock)      (22k)     │         │ Q1 │              (HIGH = locked)
                      │         │2N3904
                 R7 ──┴── GND   └─┬─┘ E
                (10k)              │
                                  GND

            +3.3V ─── R8 ─────────┘
                     (10k)        (pull-up on collector)

Operation:
- Unlocked (4.8V): Q1 ON  → collector LOW  → GP4 = LOW
- Locked (0.8V):   Q1 OFF → collector HIGH → GP4 = HIGH
```

**Components:**
- 1× 2N3904 NPN transistor
- R6: 22kΩ (base input)
- R7: 10kΩ (base to GND)
- R8: 10kΩ (collector pull-up to 3.3V)

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
