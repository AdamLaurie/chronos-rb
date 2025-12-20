# Phase 02: Signal Conditioning Circuits

This phase implements the critical signal conditioning circuits that convert the FE-5680A rubidium oscillator outputs into Pico-compatible logic levels. The 10MHz sine wave requires conversion to 3.3V LVCMOS using a high-speed comparator, and the 5V TTL 1PPS signal needs level shifting with ESD protection. These circuits are the heart of the timing interface and must be designed for minimum jitter and maximum signal integrity.

## Tasks

- [ ] Create a custom KiCad symbol for the LT1016 high-speed comparator in `symbols/chronos_rb.kicad_sym` with all pins: +IN (pin 2), -IN (pin 3), V+ (pin 8), V- (pin 4), OUT (pin 7), GND (pin 1), LATCH (pin 5), Q_bar (pin 6)
- [ ] Create an alternative symbol for MAX999 comparator as a drop-in option with compatible pinout
- [ ] In `signal_conditioning.kicad_sch`, create the 10MHz comparator circuit section with LT1016, input AC coupling capacitor (100nF), bias resistor divider (10k/10k) to set DC operating point at V+/2
- [ ] Add 50-ohm input termination resistor for the 10MHz signal to match FE-5680A output impedance
- [ ] Add decoupling capacitors for the LT1016: 100nF ceramic on V+ to GND, 10uF tantalum on V+ to GND
- [ ] Add a hysteresis feedback network to the comparator (100k from output to +IN, optional 1M to threshold) to prevent oscillation on slow edges
- [ ] Add output series resistor (33 ohm) on comparator output to limit edge rate and reduce EMI
- [ ] Create the 1PPS level shifter section using resistive divider: 1.8k from 5V input, 3.3k to GND, achieving 3.08V output from 5V input
- [ ] Add BAT54S Schottky diode clamp from divider output to 3.3V rail and GND for overvoltage/ESD protection
- [ ] Add 100pF capacitor across the lower resistor of the divider to preserve fast PPS edges while filtering noise
- [ ] Create input protection on the 1PPS line: 100 ohm series resistor before the divider to limit surge current
- [ ] Add a buffer stage after the 1PPS divider using a 74LVC1G17 Schmitt trigger for clean digital edges (optional but recommended section)
- [ ] Create the RB_LOCK status input circuit: 10k pullup to 3.3V (since FE-5680A lock output is open-collector/active-low), 100nF filter capacitor
- [ ] Add net labels to all signal conditioning outputs: CLK_10MHZ_3V3, PPS_3V3, RB_LOCK_3V3
- [ ] Add test points (TP symbols) at critical nodes: 10MHz input, 10MHz output, 1PPS input, 1PPS output, comparator threshold
- [ ] Create hierarchical labels for signals that connect to the Pico Interface sheet
- [ ] Add component annotations and values for all parts with designator prefixes: U for ICs, R for resistors, C for capacitors, D for diodes, TP for test points
- [ ] Add text notes on the schematic explaining signal levels: "Input: 1Vpp sine @ 10MHz" and "Output: 3.3V LVCMOS @ 10MHz, <1ns jitter"