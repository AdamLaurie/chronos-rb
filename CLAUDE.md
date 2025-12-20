# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

CHRONOS-Rb (Compact High-precision Rubidium Oscillator Network Operating System) is a Stratum-1 NTP/PTP time server for Raspberry Pi Pico 2-W, disciplined by an FE-5680A rubidium atomic frequency standard. The project achieves sub-microsecond timing accuracy (<1µs to UTC) with 10⁻¹¹ frequency stability.

**Key Specifications:**
- NTP Stratum 1 (when locked to rubidium reference)
- PPS jitter < 100ns
- Time accuracy < 1µs to UTC
- Frequency stability < 5×10⁻¹¹ (1 day)

## Build System

### Prerequisites
- Pico SDK must be installed and `PICO_SDK_PATH` environment variable set
- Target hardware: Raspberry Pi Pico 2-W (RP2350 with WiFi)

### Building Firmware

```bash
cd firmware
mkdir build && cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
make -j4
```

Output: `chronos_rb.uf2` ready for flashing

### Flashing to Pico

Hold BOOTSEL button while connecting USB:
```bash
cp chronos_rb.uf2 /media/$USER/RPI-RP2/
```

### Configuration Before Build

Edit `firmware/include/chronos_rb.h` to set WiFi credentials:
```c
#define WIFI_SSID_DEFAULT    "YourNetwork"
#define WIFI_PASS_DEFAULT    "YourPassword"
#define WIFI_COUNTRY         "US"
```

## Architecture Overview

### Time Discipline System

The core architecture implements a closed-loop control system that disciplines the Pico's system clock to the rubidium reference:

1. **PPS Capture (PIO)**: `pps_capture.c` + `pps_capture.pio` - Captures 1PPS rising edge with cycle-accurate timing (~6.67ns resolution at 150MHz system clock). Uses PIO state machine for hardware-timed capture.

2. **Frequency Counter (PIO)**: `freq_counter.c` + `freq_counter.pio` - Measures the 10MHz reference signal to calculate frequency offset in ppb (parts per billion).

3. **Rubidium Sync State Machine**: `rubidium_sync.c` - Manages progression through synchronization states:
   - INIT → FREQ_CAL → COARSE → FINE → LOCKED
   - Handles warmup (3-5 min), frequency calibration, coarse time acquisition, fine discipline
   - Monitors rubidium lock status (GPIO_RB_LOCK_STATUS, active low)

4. **Time Discipline Loop**: `time_discipline.c` - PI controller that calculates frequency corrections based on measured offset. Uses configurable time constants (DISCIPLINE_TAU_FAST=64s, DISCIPLINE_TAU_SLOW=1024s).

5. **Time Distribution**:
   - `ntp_server.c` - NTPv4 server (UDP port 123, Stratum 1, refid "RBDM")
   - `ptp_server.c` - IEEE 1588 PTP (ports 319/320, clock class 6)
   - Synchronized GPIO outputs at 0.5s, 1s, 6s, 30s, 60s intervals (GP14-GP18)

### Signal Flow

```
FE-5680A (10MHz + 1PPS)
    → Signal Conditioning (comparator + level shifter)
    → Pico GPIO (GP2=PPS, GP3=10MHz, GP4=Lock)
    → PIO Hardware Capture
    → Time Discipline Algorithm
    → System Clock Adjustment
    → NTP/PTP/GPIO Outputs
```

### Hardware Interface (GPIO Mapping)

Critical pins defined in `chronos_rb.h`:
- **GP2**: 1PPS input (PIO capture)
- **GP3**: 10MHz input (frequency counter)
- **GP4**: Rubidium lock status (active low from FE-5680A pin 9)
- **GP6-9**: Status LEDs (sync/network/activity/error)
- **GP14-18**: Interval pulse outputs (0.5s/1s/6s/30s/60s)

### Network Stack

- **WiFi Manager**: `wifi_manager.c` - Handles CYW43 WiFi chip initialization and connection management
- **Web Interface**: `web_interface.c` - HTTP server (port 80) with real-time status page and JSON API at `/api/status`
- **lwIP Integration**: Uses `pico_cyw43_arch_lwip_threadsafe_background` for non-blocking network operations

## Key Design Patterns

### PIO for Precision Timing

Both PPS capture and frequency counting use RP2350's PIO (Programmable I/O) state machines for deterministic, microsecond-accurate timing independent of CPU interrupts. PIO programs (`*.pio` files) are compiled to header files during build via `pico_generate_pio_header()`.

### Global State Management

Time state is managed through global volatile structures:
- `g_time_state` (time_state_t): current time, offset, frequency correction, sync state
- `g_stats` (statistics_t): NTP/PTP request counters, offset statistics
- `g_wifi_connected`: network status

These are defined in `main.c` and declared extern in `chronos_rb.h`.

### Synchronization State Machine

The sync state machine in `rubidium_sync.c` progresses through calibration phases before declaring time valid. Each state has entry conditions and timeout handling. Time is only marked valid (`g_time_state.time_valid = true`) after reaching SYNC_STATE_LOCKED.

## Signal Conditioning Requirements

The firmware expects specific signal conditioning between FE-5680A and Pico:

1. **10MHz Conversion**: FE-5680A outputs 1Vpp sine wave. Must be converted to 3.3V LVCMOS square wave using high-speed comparator (LT1016 or MAX999) before connecting to GP3.

2. **1PPS Level Shift**: FE-5680A outputs 5V TTL. Requires resistive divider with Schottky diode protection to bring to 3.3V before GP2.

3. **Lock Status**: Direct connection with appropriate current limiting. Active low signal.

See `docs/CHRONOS-Rb_Hardware_Guide.docx` for complete schematics with component values.

## Development Workflow

### Typical Development Cycle

1. Modify source files in `firmware/src/` or headers in `firmware/include/`
2. Rebuild from `firmware/build/`: `make -j4`
3. Flash updated UF2 to Pico (BOOTSEL + copy)
4. Monitor via USB serial (115200 baud): `screen /dev/ttyACM0 115200`
5. Check web interface at device IP for real-time status

### Debug Output

UART debugging is enabled on both USB and hardware UART (GP0/GP1):
```c
pico_enable_stdio_usb(chronos_rb 1)
pico_enable_stdio_uart(chronos_rb 1)
```

All modules use `printf()` for status messages with module prefixes like `[RB]`, `[NTP]`, `[PTP]`.

### Timing Constants and Tuning

Key constants in `chronos_rb.h` that affect performance:
- `DISCIPLINE_TAU_FAST` / `DISCIPLINE_TAU_SLOW`: PI controller time constants
- `DISCIPLINE_GAIN_P` / `DISCIPLINE_GAIN_I`: Proportional and integral gains
- `PPS_TOLERANCE_US`: Lock tolerance (±100µs default)
- `FREQ_TOLERANCE_PPB`: Frequency lock tolerance (±1000ppb)

## Testing the Time Server

### Verify NTP Operation
```bash
ntpdate -q <device-ip>
ntpq -p <device-ip>
```

### Check Status via JSON API
```bash
curl http://<device-ip>/api/status
```

Returns sync state, lock status, offset, frequency correction, and statistics.

### Monitor Sync Progress

Web interface shows state machine progression and real-time offset graphs. Normal progression takes 5-15 minutes after rubidium lock:
- First 3-5 min: Rubidium warmup (INIT state)
- Next 2-5 min: Frequency calibration and coarse sync
- Then: Fine discipline and lock maintenance

## Project Documentation Standards

This project emphasizes professional documentation quality:
- Hardware documentation uses embedded PNG schematics (not ASCII art)
- Circuit diagrams show component values and signal levels
- README includes complete block diagrams and specifications
- Code documentation focuses on timing-critical sections and state machine logic

## Important Notes

- **System Clock**: Runs at 150MHz (SYSTEM_CLOCK_HZ), critical for PIO timing resolution
- **NTP Epoch Offset**: Code uses NTP epoch (1900) with 2208988800s offset to Unix epoch
- **Power Requirements**: FE-5680A needs 15V/2-3A during warmup, ~12W steady-state
- **Interval Outputs**: All GPIO timing outputs (GP14-18) are synchronized to atomic 1PPS, 10ms pulse width, 3.3V LVCMOS
