# Phase 01: KiCad Project Setup and Core Schematic Structure

This phase establishes the complete KiCad project infrastructure for CHRONOS-Rb and creates the hierarchical schematic structure with all sheet organization. By the end of this phase, you will have a fully configured KiCad project with organized schematic sheets, custom symbol library foundations, and a working top-level block diagram that can be opened and viewed in KiCad. This provides the essential framework upon which all subsequent hardware design will be built.

## Tasks

- [x] Create the KiCad project directory structure at `hardware/kicad/` with subdirectories: `symbols/`, `footprints/`, `3dmodels/`, and `gerbers/`
  - Created directory structure: `hardware/kicad/` with `symbols/`, `footprints/`, `3dmodels/`, and `gerbers/` subdirectories
- [ ] Create the main KiCad project file `chronos_rb.kicad_pro` with project settings configured for 1-layer PCB, metric units, and design rules suitable for JLCPCB manufacturing (6mil trace/space minimum)
- [ ] Create the top-level schematic file `chronos_rb.kicad_sch` with project title block containing: "CHRONOS-Rb Rubidium Time Server", revision "1.0", date field, and author placeholder
- [ ] Add hierarchical sheet symbols to the top-level schematic for: "Power Supply" (power_supply.kicad_sch), "Signal Conditioning" (signal_conditioning.kicad_sch), "Pico Interface" (pico_interface.kicad_sch), and "Connectors" (connectors.kicad_sch)
- [ ] Create the Power Supply sub-schematic `power_supply.kicad_sch` with empty sheet and title block labeled "Power Supply - 15V/5V/3.3V Rails"
- [ ] Create the Signal Conditioning sub-schematic `signal_conditioning.kicad_sch` with empty sheet and title block labeled "Signal Conditioning - 10MHz Comparator & 1PPS Level Shifter"
- [ ] Create the Pico Interface sub-schematic `pico_interface.kicad_sch` with empty sheet and title block labeled "Raspberry Pi Pico 2-W Interface"
- [ ] Create the Connectors sub-schematic `connectors.kicad_sch` with empty sheet and title block labeled "FE-5680A & External Connectors"
- [ ] Create the custom symbol library file `symbols/chronos_rb.kicad_sym` with library header and metadata
- [ ] Create the custom footprint library file `footprints/chronos_rb.pretty/` directory (KiCad footprint library format)
- [ ] Create a `fp-lib-table` file in the project directory that references the custom footprint library
- [ ] Create a `sym-lib-table` file in the project directory that references the custom symbol library
- [ ] Add global power symbols (+15V, +5V, +3.3V, GND) and power flags to the top-level schematic as a power distribution reference
- [ ] Create net labels on the top-level schematic for critical signals: PPS_IN, CLK_10MHZ, RB_LOCK, and the GPIO bus connections
- [ ] Add text annotations to the top-level schematic documenting the signal flow: "FE-5680A → Signal Conditioning → Pico 2-W → Network Output"
- [ ] Create a `README.md` file in `hardware/kicad/` documenting the project structure, required KiCad version (7.0+), and basic usage instructions