# Phase 04: Pico Interface and Connectors

This phase designs the Raspberry Pi Pico 2-W interface circuitry and all external connectors. The Pico serves as the main processor running the NTP/PTP server firmware, receiving timing signals from the signal conditioning stage. This phase also includes the FE-5680A connector interface, status LEDs, debug headers, and optional expansion connectors for external timing outputs.

## Tasks

- [ ] Create or import a Raspberry Pi Pico 2-W symbol in `symbols/chronos_rb.kicad_sym` showing all 40 pins with correct GPIO assignments per RP2350 datasheet
- [ ] In `pico_interface.kicad_sch`, place the Pico 2-W symbol with proper orientation (USB connector facing board edge)
- [ ] Create two 20-pin female header footprints for Pico mounting (2.54mm pitch, standard Pico socket arrangement)
- [ ] Connect power pins: VSYS to 5V rail, 3V3_EN to 3.3V (via 10k pullup), GND pins to ground plane
- [ ] Connect timing inputs from signal conditioning: GP2 to PPS_3V3, GP3 to CLK_10MHZ_3V3, GP4 to RB_LOCK_3V3
- [ ] Add 33 ohm series resistors on timing inputs for trace impedance matching and ESD protection
- [ ] Create status LED circuit section with 4 LEDs: green (GP6/SYNC), blue (GP7/NETWORK), yellow (GP8/ACTIVITY), red (GP9/ERROR)
- [ ] Add 470 ohm current limiting resistors for each LED (approximately 5mA per LED at 3.3V)
- [ ] Connect optional RB_ENABLE output on GP5 with 10k series resistor for FE-5680A control
- [ ] Create debug/diagnostic output section: GP10 (PPS_OUT), GP11 (SYNC_PULSE) with series resistors and test points
- [ ] Connect UART debug pins: GP0 (TX) and GP1 (RX) to a 3-pin debug header (TX, RX, GND)
- [ ] Create I2C header for optional OLED display: GP12 (SDA), GP13 (SCL), 3.3V, GND on a 4-pin connector
- [ ] Add I2C pull-up resistors (4.7k) on SDA and SCL lines
- [ ] Create interval pulse output section on GP14-GP18 with individual 100 ohm series resistors
- [ ] Add a 6-pin header for interval pulse outputs: 0.5s, 1s, 6s, 30s, 60s, and GND
- [ ] In `connectors.kicad_sch`, create the FE-5680A connector interface using a DB-9 female connector
- [ ] Map DB-9 pins per FE-5680A pinout: pin 1 (+15V), pin 2 (GND), pin 3 (LOCK), pin 4 (+5V), pin 5 (GND), pin 7 (10MHz)
- [ ] Note: 1PPS may need to be derived from 10MHz using a divider circuit (not all FE-5680A units have 1PPS output)
- [ ] Create a 2-pin screw terminal for +15V power input as alternative to barrel jack
- [ ] Add a USB-C or micro-USB breakout connector for Pico programming access
- [ ] Create an SMA or BNC connector footprint for external 10MHz reference output (active buffer from Pico output)
- [ ] Add an SMA connector for external 1PPS output with 50-ohm driver circuit using 74LVC2G17 buffer
- [ ] Create mounting hole symbols (M3) at board corners, connected to GND for shielding
- [ ] Add board edge connector for stacking or enclosure integration (2x10 pin header with power and key signals)
- [ ] Create net labels for all GPIO connections matching firmware definitions in chronos_rb.h
- [ ] Add hierarchical labels connecting all sheets together for power and signals