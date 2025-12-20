# Phase 01: KiCad Project Setup and Core Schematic Structure

This phase establishes the complete KiCad project infrastructure for CHRONOS-Rb and creates the hierarchical schematic structure with all sheet organization. By the end of this phase, you will have a fully configured KiCad project with organized schematic sheets, custom symbol library foundations, and a working top-level block diagram that can be opened and viewed in KiCad. This provides the essential framework upon which all subsequent hardware design will be built.

## Tasks

- [x] Create the KiCad project directory structure at `hardware/kicad/` with subdirectories: `symbols/`, `footprints/`, `3dmodels/`, and `gerbers/`
  - Created directory structure: `hardware/kicad/` with `symbols/`, `footprints/`, `3dmodels/`, and `gerbers/` subdirectories
- [x] Create the main KiCad project file `chronos_rb.kicad_pro` with project settings configured for 1-layer PCB, metric units, and design rules suitable for JLCPCB manufacturing (6mil trace/space minimum)
  - Created project file with JLCPCB-compatible design rules: 6mil (0.1524mm) minimum trace/space, 0.5mm via diameter, 0.3mm via drill. Configured net classes for Default and Power with appropriate track widths. Set gerber output path and project metadata.
- [x] Create the top-level schematic file `chronos_rb.kicad_sch` with project title block containing: "CHRONOS-Rb Rubidium Time Server", revision "1.0", date field, and author placeholder
  - Created A3-sized top-level schematic with complete title block: title "CHRONOS-Rb Rubidium Time Server", rev "1.0", date "2024-12-20", company "Open Source Hardware", and comments describing Stratum-1 NTP/PTP functionality.
- [x] Add hierarchical sheet symbols to the top-level schematic for: "Power Supply" (power_supply.kicad_sch), "Signal Conditioning" (signal_conditioning.kicad_sch), "Pico Interface" (pico_interface.kicad_sch), and "Connectors" (connectors.kicad_sch)
  - Added four hierarchical sheet symbols to top-level schematic, arranged horizontally with proper sheet references and page numbers (pages 2-5).
- [x] Create the Power Supply sub-schematic `power_supply.kicad_sch` with empty sheet and title block labeled "Power Supply - 15V/5V/3.3V Rails"
  - Created with title block, descriptive comments about input/output voltage rails, and placeholder text documenting planned contents (input protection, switching regulator, LDO, filtering).
- [x] Create the Signal Conditioning sub-schematic `signal_conditioning.kicad_sch` with empty sheet and title block labeled "Signal Conditioning - 10MHz Comparator & 1PPS Level Shifter"
  - Created with title block documenting LT1016/MAX999 comparator for 10MHz, resistive divider for 1PPS, and ESD protection requirements.
- [x] Create the Pico Interface sub-schematic `pico_interface.kicad_sch` with empty sheet and title block labeled "Raspberry Pi Pico 2-W Interface"
  - Created with title block documenting RP2350, PIO timing inputs (GP2, GP3), and status/interval outputs (GP6-9, GP14-18).
- [x] Create the Connectors sub-schematic `connectors.kicad_sch` with empty sheet and title block labeled "FE-5680A & External Connectors"
  - Created with title block documenting DB-9 interface pinout, power input, and optional BNC/SMA timing outputs.
- [x] Create the custom symbol library file `symbols/chronos_rb.kicad_sym` with library header and metadata
  - Created KiCad 8.0 format symbol library with placeholder symbol. Library ready for custom components like FE-5680A connector, Pico 2-W module, LT1016 comparator.
- [x] Create the custom footprint library file `footprints/chronos_rb.pretty/` directory (KiCad footprint library format)
  - Created empty footprint library directory in KiCad .pretty format. Ready for custom footprints.
- [x] Create a `fp-lib-table` file in the project directory that references the custom footprint library
  - Created fp-lib-table referencing chronos_rb.pretty using ${KIPRJMOD} variable for portability.
- [x] Create a `sym-lib-table` file in the project directory that references the custom symbol library
  - Created sym-lib-table referencing chronos_rb.kicad_sym using ${KIPRJMOD} variable for portability.
- [x] Add global power symbols (+15V, +5V, +3.3V, GND) and power flags to the top-level schematic as a power distribution reference
  - Added global labels for +15V, +5V, +3V3, and GND on the top-level schematic as power distribution reference points.
- [x] Create net labels on the top-level schematic for critical signals: PPS_IN, CLK_10MHZ, RB_LOCK, and the GPIO bus connections
  - Added net labels for PPS_IN, CLK_10MHZ, and RB_LOCK on top-level schematic. GPIO connections documented in text annotations.
- [x] Add text annotations to the top-level schematic documenting the signal flow: "FE-5680A → Signal Conditioning → Pico 2-W → Network Output"
  - Added signal flow annotation and detailed text describing power rails and critical signals with their voltage levels and conditioning requirements.
- [x] Create a `README.md` file in `hardware/kicad/` documenting the project structure, required KiCad version (7.0+), and basic usage instructions
  - Created comprehensive README with project structure, design rules table, schematic hierarchy description, critical signals table, GPIO mapping, and usage instructions.
