# Phase 03: Power Supply Design

This phase creates the complete power supply subsystem for CHRONOS-Rb using **mains AC input** (100-240V AC, 50/60Hz). The FE-5680A rubidium oscillator requires 15V at 2-3A during warmup (dropping to ~1A steady-state), while the signal conditioning and Pico require regulated 5V and 3.3V rails. The power supply includes a zero-crossing detector circuit to monitor local AC mains frequency via a GPIO input.

**SAFETY WARNING**: This design involves mains AC voltage which can cause serious injury or death. All mains-connected circuitry must be properly isolated, fused, and enclosed. Follow all local electrical codes and regulations.

## Specifications

- **Input**: 100-240V AC, 50/60Hz (universal mains input)
- **Outputs**: +15V @ 3A (FE-5680A), +5V @ 1A (logic), +3.3V @ 500mA (digital), +3.3V @ 100mA (analog)
- **Isolation**: Reinforced isolation between mains and low-voltage sections
- **Features**: Mains frequency monitoring via GPIO (zero-crossing detector)

## Power Conversion Block Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              MAINS SIDE (HAZARDOUS)                         │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌──────────┐  │
│  │ IEC C14 │───>│  Fuse   │───>│   EMI   │───>│ Bridge  │───>│  Bulk    │  │
│  │  Inlet  │    │   2A    │    │ Filter  │    │Rectifier│    │  Cap     │  │
│  │100-240V │    │slow-blow│    │  + MOV  │    │ GBU806  │    │470uF/400V│  │
│  └─────────┘    └─────────┘    └─────────┘    └─────────┘    └────┬─────┘  │
│                                     │                              │        │
│                                     │                         140-340V DC   │
│                                     │                              │        │
│                              ┌──────┴──────┐               ┌───────┴──────┐ │
│                              │Zero-Crossing│               │   Isolated   │ │
│                              │  Detector   │               │   Flyback    │ │
│                              │   H11AA1    │               │  DC-DC Conv  │ │
│                              └──────┬──────┘               └───────┬──────┘ │
│                                     │                              │        │
├─────────────────────────────────────│──────────────ISOLATION───────│────────┤
│                              LOW VOLTAGE SIDE                      │        │
│                                     │                              │        │
│                              ┌──────┴──────┐               ┌───────┴──────┐ │
│                              │   GP19      │               │    +15V      │ │
│                              │  (Pico)     │               │   @ 3A       │ │
│                              │AC_ZERO_CROSS│               │  (FE-5680A)  │ │
│                              └─────────────┘               └───────┬──────┘ │
│                                                                    │        │
│                                                            ┌───────┴──────┐ │
│                                                            │   LM2596     │ │
│                                                            │  Buck Conv   │ │
│                                                            └───────┬──────┘ │
│                                                                    │        │
│                                                            ┌───────┴──────┐ │
│                                                            │     +5V      │ │
│                                                            │    @ 1A      │ │
│                                                            └───────┬──────┘ │
│                                                                    │        │
│                                                     ┌──────────────┼───────┐│
│                                                     │              │       ││
│                                              ┌──────┴─────┐ ┌──────┴─────┐ ││
│                                              │ AMS1117    │ │  LP5907    │ ││
│                                              │   LDO      │ │ Low-noise  │ ││
│                                              └──────┬─────┘ └──────┬─────┘ ││
│                                                     │              │       ││
│                                              ┌──────┴─────┐ ┌──────┴─────┐ ││
│                                              │+3.3V Digital│ │+3.3V Analog│ ││
│                                              │  @ 500mA   │ │  @ 100mA   │ ││
│                                              └────────────┘ └────────────┘ ││
└─────────────────────────────────────────────────────────────────────────────┘
```

## Zero-Crossing Detector Circuit

The H11AA1 is an AC-input optocoupler with two back-to-back LEDs, providing isolation while detecting both positive and negative zero crossings of the AC waveform.

```
        MAINS SIDE                    │            LOW VOLTAGE SIDE
        (ISOLATED)                    │
                                      │
    AC Live (L) ───┬──────────────────│
                   │                  │
                  ┌┴┐                 │
                  │ │ R1              │
                  │ │ 47k 1/2W        │
                  └┬┘                 │              +3.3V
                   │                  │                │
                  ┌┴┐                 │               ┌┴┐
                  │ │ R2              │               │ │ R3
                  │ │ 47k 1/2W        │               │ │ 10k
                  └┬┘                 │               └┬┘
                   │    ┌─────────┐   │                │
                   ├────┤1  H11AA1├───│────────────────┼──────> To GP19
                   │    │   opto  │   │                │        (AC_ZERO_CROSS)
                   │  ┌─┤2       4├─┬─│────────────────┘
                   │  │ └─────────┘ │ │
                   │  │             │ │
    AC Neutral (N)─┴──┴─────────────┴─│─────> GND (isolated from mains)
                                      │
                                      │
                              ISOLATION BARRIER
                              (3kV reinforced)
```

**Component Details:**
- **R1, R2**: 47kΩ 1/2W resistors in series (94kΩ total) - limits LED current to ~2.5mA at 240VAC peak
- **H11AA1**: AC-input optocoupler with back-to-back LEDs, 5.3kV isolation rating
- **R3**: 10kΩ pull-up to 3.3V - output is LOW during zero-crossing, HIGH otherwise

**Output Signal:**
- Produces a pulse at each zero crossing (both positive and negative)
- Pulse frequency = 2× mains frequency (100Hz for 50Hz mains, 120Hz for 60Hz mains)
- Firmware measures time between pulses to calculate mains frequency
- Connected to **GP19** (GPIO_AC_ZERO_CROSS) on Pico 2-W

## Tasks

### AC Input Section (Mains-Isolated)
- [ ] In `power_supply.kicad_sch`, create the AC mains input section with IEC C14 inlet connector (fused, with EMI filter)
- [ ] Add mains fuse holder with 2A slow-blow fuse (5x20mm) on the live conductor
- [ ] Add MOV (Metal Oxide Varistor) across L-N for transient surge suppression (275V AC rating, e.g., EPCOS B72210S0271K101)
- [ ] Add X2-class capacitor (100nF 275VAC) across L-N for differential mode EMI filtering
- [ ] Add Y1-class capacitors (2x 2.2nF 250VAC) from L and N to PE (protective earth) for common mode filtering
- [ ] Add common mode choke (10mH, 3A rated) for EMI suppression
- [ ] Create a clearly marked **HAZARDOUS VOLTAGE AREA** on schematic with safety notes

### Bridge Rectifier and Bulk Capacitor
- [ ] Add full-bridge rectifier (GBU806 or KBU810, 600V/8A rating) with proper heatsinking consideration
- [ ] Add bulk capacitor: 470uF/400V electrolytic for energy storage (calculate ripple current rating)
- [ ] Add bleeder resistor (470k, 1W) across bulk capacitor for discharge when unplugged
- [ ] Calculate DC bus voltage: ~140V DC (100V AC) to ~340V DC (240V AC)

### Isolated DC-DC Converter (15V Output)
- [ ] Use isolated flyback or forward converter topology for 15V @ 3A output (e.g., based on TNY290PG or similar offline switcher)
- [ ] Alternatively, use a pre-built AC-DC module (Mean Well IRM-45-15 or similar) for safety and certification
- [ ] Add opto-isolator (PC817 or 4N35) for feedback loop isolation
- [ ] Add isolation transformer with proper creepage/clearance distances (reinforced isolation, 3kV minimum)
- [ ] Add output capacitors: 1000uF/25V electrolytic + 100nF ceramic for the 15V rail
- [ ] Create the 15V filtered output for FE-5680A with additional LC filter: 10uH inductor, 100uF output capacitor

### Zero-Crossing Detector (Mains Frequency Monitor)
- [ ] Create isolated zero-crossing detector circuit using optocoupler (H11AA1 or similar AC-input opto)
- [ ] Add 100k current-limiting resistors (2x 47k in series for voltage rating) from mains L-N to opto LED
- [ ] Output: 3.3V-compatible pulse on each zero crossing (100/120 Hz for 50/60 Hz mains)
- [ ] Connect opto collector to GPIO with 10k pull-up to 3.3V
- [ ] Add net label: AC_ZERO_CROSS (connects to Pico GPIO for frequency measurement)
- [ ] Add schematic note: "Pulse output at 2x mains frequency for firmware frequency measurement"

### Secondary Regulators (5V and 3.3V Rails)
- [ ] Create the 5V switching regulator section from 15V input using LM2596-5.0 or TPS5430
- [ ] Add input capacitors (100uF/25V), output inductor (68uH), output capacitors (220uF + 100nF), Schottky diode (SS34)
- [ ] Add output voltage test point for the 5V rail with silk screen label "5V_TEST"
- [ ] Create the 3.3V LDO section using AMS1117-3.3 or AP2112K-3.3 from 5V input
- [ ] Add input capacitor (10uF), output capacitors (10uF + 100nF)
- [ ] Add a second 3.3V LDO specifically for analog circuits (comparator supply) using low-noise regulator LP5907-3.3
- [ ] Create power-on LED indicators: green LED with 1k resistor from 3.3V rail, amber LED with 2.2k resistor from 5V rail

### Protection and Monitoring
- [ ] Add current sense resistor (0.1 ohm, 1W) in the 15V path for monitoring warmup current
- [ ] Add power good output signal using voltage supervisor circuit (TL7705) monitoring 5V rail
- [ ] Add power rail decoupling network: 100uF bulk + 10uF + 100nF + 10nF for each rail
- [ ] Add TVS diode (SMBJ18A) on 15V output for secondary-side surge protection

### Documentation and Labels
- [ ] Create power sequencing note: "15V powers up first (from AC-DC), then 5V, then 3.3V (cascaded regulators)"
- [ ] Add net labels: +15V_RB, +5V, +3V3_DIGITAL, +3V3_ANALOG, GND, PWR_GOOD, AC_ZERO_CROSS
- [ ] Add power dissipation calculations as text notes
- [ ] Add safety markings: "DANGER: HIGH VOLTAGE", isolation boundary markers
- [ ] Add hierarchical labels for power outputs connecting to other sheets
- [ ] Document creepage/clearance requirements for mains isolation (minimum 6mm creepage for reinforced isolation)

### Firmware Integration
- [ ] Add GPIO_AC_ZERO_CROSS pin definition to chronos_rb.h (recommend GP19 or GP20)
- [ ] Create ac_freq_monitor.c/h module to measure mains frequency from zero-crossing pulses
- [ ] Add CLI command "acfreq" to display measured mains frequency
- [ ] Display AC frequency in status output and web interface