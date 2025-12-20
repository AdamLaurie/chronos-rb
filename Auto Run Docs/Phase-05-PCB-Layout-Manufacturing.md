# Phase 05: PCB Layout and Manufacturing Files

This phase transforms the completed schematics into a manufacturable PCB design. The layout requires careful attention to signal integrity for the high-speed 10MHz clock, proper grounding for noise immunity, thermal management for power components, and adherence to JLCPCB design rules for automated fabrication. The final deliverables include Gerber files, drill files, BOM, and pick-and-place data ready for ordering.

## Tasks

- [ ] Create the PCB file `chronos_rb.kicad_pcb` with board outline: 100mm x 80mm rectangle with 3mm corner radius, 4-layer stackup (Signal-GND-PWR-Signal)
- [ ] Define layer stackup in board settings: 0.035mm copper, 0.2mm prepreg, 1.0mm core, matching JLCPCB 4-layer standard
- [ ] Set design rules for JLCPCB capabilities: 0.15mm (6mil) min trace, 0.15mm min space, 0.3mm min drill, 0.45mm min via
- [ ] Add mounting holes at four corners (M3, 3.2mm diameter) with keepout zones
- [ ] Run "Update PCB from Schematic" to import all components with netlist
- [ ] Place power supply components in lower-left quadrant with input connector at board edge
- [ ] Place FE-5680A DB-9 connector at board edge near power input section
- [ ] Place Pico 2-W headers in central area with USB connector accessible at board edge
- [ ] Place signal conditioning components between FE-5680A connector and Pico, minimizing trace length
- [ ] Place status LEDs along one board edge for visibility
- [ ] Place debug and expansion headers along remaining board edges
- [ ] Create ground plane on layer 2 (inner) as solid copper pour connected to GND net
- [ ] Create power plane on layer 3 (inner) with split planes for +3.3V and +5V zones
- [ ] Route critical 10MHz signal with controlled impedance: 50-ohm microstrip, keep trace short and direct
- [ ] Route 1PPS signal with ground guard traces on either side for noise immunity
- [ ] Route power traces with appropriate width: 15V traces at 1mm (2A capacity), 5V at 0.5mm, 3.3V at 0.3mm
- [ ] Add via stitching around board perimeter for EMI containment (every 5mm)
- [ ] Add via stitching around high-speed signal areas connecting top and bottom ground
- [ ] Place decoupling capacitors as close as possible to IC power pins with direct via to ground plane
- [ ] Add thermal relief on power plane connections to allow hand soldering
- [ ] Create copper pour on top and bottom layers connected to GND, with appropriate clearances
- [ ] Add silk screen labels for all connectors, test points, and component designators
- [ ] Add silk screen text: "CHRONOS-Rb v1.0", website/project URL, and license text
- [ ] Add polarity markings near electrolytic capacitors and diodes
- [ ] Run Design Rule Check (DRC) and resolve all errors and warnings
- [ ] Run Electrical Rules Check (ERC) on schematic and resolve all issues
- [ ] Generate Gerber files in `gerbers/` subdirectory: F.Cu, B.Cu, In1.Cu, In2.Cu, F.SilkS, B.SilkS, F.Mask, B.Mask, Edge.Cuts
- [ ] Generate drill files (Excellon format) for PTH and NPTH holes
- [ ] Generate pick-and-place file (CPL) in CSV format with component XY coordinates and rotations
- [ ] Generate Bill of Materials (BOM) in CSV format with: designator, value, footprint, manufacturer, part number, quantity
- [ ] Create ZIP archive of Gerber files ready for JLCPCB upload: `chronos_rb_gerbers_v1.0.zip`
- [ ] Verify Gerber files using an online Gerber viewer (document the verification step in README)
- [ ] Update `hardware/kicad/README.md` with manufacturing instructions: layer count, board thickness, surface finish (HASL or ENIG), and recommended JLCPCB options