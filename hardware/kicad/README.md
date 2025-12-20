# CHRONOS-Rb KiCad Project

This directory contains the KiCad hardware design files for the CHRONOS-Rb Rubidium Time Server signal conditioning and interface board.

## Requirements

- **KiCad Version:** 7.0 or later (8.0 recommended)
- **Target Platform:** Raspberry Pi Pico 2-W (RP2350 with WiFi)
- **Reference Clock:** FE-5680A Rubidium Frequency Standard

## Project Structure

```
kicad/
├── chronos_rb.kicad_pro     # Main project file
├── chronos_rb.kicad_sch     # Top-level schematic (block diagram)
├── power_supply.kicad_sch   # Power supply: 15V/5V/3.3V rails
├── signal_conditioning.kicad_sch  # 10MHz comparator & 1PPS level shifter
├── pico_interface.kicad_sch # Raspberry Pi Pico 2-W connections
├── connectors.kicad_sch     # FE-5680A & external connectors
├── sym-lib-table            # Symbol library references
├── fp-lib-table             # Footprint library references
├── symbols/
│   └── chronos_rb.kicad_sym # Custom symbol library
├── footprints/
│   └── chronos_rb.pretty/   # Custom footprint library
├── 3dmodels/                # 3D models for components
└── gerbers/                 # Manufacturing output files
```

## Design Rules (JLCPCB Compatible)

The project is configured with design rules suitable for JLCPCB manufacturing:

| Parameter | Value |
|-----------|-------|
| Minimum trace width | 6 mil (0.1524 mm) |
| Minimum trace spacing | 6 mil (0.1524 mm) |
| Minimum via diameter | 0.5 mm |
| Minimum via drill | 0.3 mm |
| Minimum hole size | 0.3 mm |
| Copper to edge clearance | 0.3 mm |

## Schematic Hierarchy

1. **Top Level** (`chronos_rb.kicad_sch`)
   - Block diagram overview
   - Power distribution reference
   - Signal flow documentation

2. **Power Supply** (`power_supply.kicad_sch`)
   - 15V input for FE-5680A (2-3A during warmup)
   - 5V regulated rail for logic
   - 3.3V LDO for Pico and signal conditioning

3. **Signal Conditioning** (`signal_conditioning.kicad_sch`)
   - 10MHz sine-to-square converter (LT1016/MAX999)
   - 1PPS 5V to 3.3V level shifter
   - Lock status input conditioning
   - ESD protection

4. **Pico Interface** (`pico_interface.kicad_sch`)
   - Pico 2-W module connections
   - Decoupling and power filtering
   - Status LED drivers
   - Interval output buffers
   - Debug/programming headers

5. **Connectors** (`connectors.kicad_sch`)
   - FE-5680A DB-9 interface
   - DC power input
   - Optional external timing outputs (BNC/SMA)

## Usage

1. Open `chronos_rb.kicad_pro` in KiCad 7.0+
2. The top-level schematic shows the system block diagram
3. Double-click hierarchical sheets to navigate to sub-schematics
4. Custom symbols are in the `chronos_rb` library
5. Export gerbers to the `gerbers/` directory for manufacturing

## Critical Signals

| Signal | Description | Level |
|--------|-------------|-------|
| PPS_IN | 1PPS from FE-5680A | 3.3V (level shifted from 5V) |
| CLK_10MHZ | 10MHz reference | 3.3V LVCMOS (from 1Vpp sine) |
| RB_LOCK | Rubidium lock status | Active low, 3.3V |

## GPIO Mapping (Pico 2-W)

| GPIO | Function |
|------|----------|
| GP2 | PPS input (PIO capture) |
| GP3 | 10MHz input (frequency counter) |
| GP4 | Rubidium lock status |
| GP6-9 | Status LEDs |
| GP14-18 | Interval pulse outputs |

## License

Open Source Hardware - See main project LICENSE file.
