# Phase 03: Power Supply Design

This phase creates the complete power supply subsystem for CHRONOS-Rb. The FE-5680A rubidium oscillator requires 15V at 2-3A during warmup (dropping to ~1A steady-state), while the signal conditioning and Pico require regulated 5V and 3.3V rails. The power supply must handle the high inrush current, provide clean rails for precision timing circuits, and include protection features for reliable operation.

## Tasks

- [ ] In `power_supply.kicad_sch`, create the main DC input section with barrel jack connector (2.1mm or 2.5mm), rated for 15-18V @ 3A input
- [ ] Add input reverse polarity protection using P-channel MOSFET (SI2301 or equivalent) in high-side configuration
- [ ] Add input bulk capacitance: 470uF/25V electrolytic for energy storage, 100nF ceramic for high-frequency bypass
- [ ] Create the 15V pass-through section for FE-5680A with LC filter: 10uH inductor, 100uF output capacitor to reduce switching noise coupling
- [ ] Add a current sense resistor (0.1 ohm, 1W) in the 15V path with test points for monitoring warmup current
- [ ] Create the 5V switching regulator section using LM2596-5.0 or TPS5430: input capacitors, output inductor (68uH), output capacitors (220uF + 100nF), feedback network
- [ ] Add Schottky catch diode (SS34 or equivalent) for the buck regulator if using external diode topology
- [ ] Add output voltage test point for the 5V rail with silk screen label "5V_TEST"
- [ ] Create the 3.3V LDO section using AMS1117-3.3 or AP2112K-3.3: input capacitor (10uF), output capacitors (10uF + 100nF)
- [ ] Add a second 3.3V LDO specifically for analog circuits (comparator supply) using low-noise regulator like LP5907-3.3
- [ ] Create power-on LED indicators: green LED with 1k resistor from 3.3V rail, amber LED with 2.2k resistor from 5V rail
- [ ] Add power rail decoupling network near power supply output: 100uF bulk + 10uF + 100nF + 10nF for each rail
- [ ] Create soft-start circuit for the 15V output using NTC thermistor (5 ohm cold) in series to limit inrush current
- [ ] Add transient voltage suppressor (TVS) diode (SMBJ18A) on input for surge protection
- [ ] Add power good output signal from the 5V regulator (if available) or create voltage supervisor circuit using TL7705
- [ ] Create power sequencing note on schematic: "15V powers up first, then 5V, then 3.3V (natural sequencing via cascaded regulators)"
- [ ] Add net labels for all power rails: +15V_RB, +5V, +3V3_DIGITAL, +3V3_ANALOG, GND, PWR_GOOD
- [ ] Add power dissipation calculations as text notes: LDO dissipation = (Vin-Vout) * Iout, verify thermal limits
- [ ] Create copper pour zones on schematic symbol for power planes annotation (KiCad 7 feature)
- [ ] Add hierarchical labels for power outputs connecting to other sheets