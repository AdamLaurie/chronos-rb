const { Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell, 
        Header, Footer, AlignmentType, HeadingLevel, BorderStyle, WidthType, 
        ShadingType, PageNumber, LevelFormat, PageBreak } = require('docx');
const fs = require('fs');

// Create the CHRONOS-Rb Hardware Documentation
const doc = new Document({
    styles: {
        default: { document: { run: { font: "Arial", size: 22 } } },
        paragraphStyles: [
            { id: "Title", name: "Title", basedOn: "Normal",
              run: { size: 56, bold: true, color: "1a1a2e" },
              paragraph: { spacing: { before: 240, after: 120 }, alignment: AlignmentType.CENTER } },
            { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
              run: { size: 32, bold: true, color: "e94560" },
              paragraph: { spacing: { before: 360, after: 120 }, outlineLevel: 0 } },
            { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
              run: { size: 26, bold: true, color: "0f3460" },
              paragraph: { spacing: { before: 240, after: 120 }, outlineLevel: 1 } },
            { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
              run: { size: 24, bold: true, color: "16213e" },
              paragraph: { spacing: { before: 200, after: 100 }, outlineLevel: 2 } },
            { id: "Code", name: "Code", basedOn: "Normal",
              run: { font: "Courier New", size: 18 },
              paragraph: { spacing: { before: 100, after: 100 } } }
        ]
    },
    numbering: {
        config: [
            { reference: "bullet-list",
              levels: [{ level: 0, format: LevelFormat.BULLET, text: "•", alignment: AlignmentType.LEFT,
                style: { paragraph: { indent: { left: 720, hanging: 360 } } } }] },
            { reference: "numbered-list",
              levels: [{ level: 0, format: LevelFormat.DECIMAL, text: "%1.", alignment: AlignmentType.LEFT,
                style: { paragraph: { indent: { left: 720, hanging: 360 } } } }] },
            { reference: "parts-list",
              levels: [{ level: 0, format: LevelFormat.DECIMAL, text: "%1.", alignment: AlignmentType.LEFT,
                style: { paragraph: { indent: { left: 720, hanging: 360 } } } }] }
        ]
    },
    sections: [{
        properties: {
            page: { margin: { top: 1440, right: 1440, bottom: 1440, left: 1440 } }
        },
        headers: {
            default: new Header({ children: [new Paragraph({ 
                alignment: AlignmentType.RIGHT,
                children: [new TextRun({ text: "CHRONOS-Rb Hardware Documentation", italics: true, size: 20, color: "666666" })]
            })] })
        },
        footers: {
            default: new Footer({ children: [new Paragraph({ 
                alignment: AlignmentType.CENTER,
                children: [new TextRun({ text: "Page ", size: 20 }), new TextRun({ children: [PageNumber.CURRENT], size: 20 }), new TextRun({ text: " of ", size: 20 }), new TextRun({ children: [PageNumber.TOTAL_PAGES], size: 20 })]
            })] })
        },
        children: [
            // Title Page
            new Paragraph({ heading: HeadingLevel.TITLE, children: [new TextRun("CHRONOS-Rb")] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 200 },
                children: [new TextRun({ text: "Compact High-precision Rubidium Oscillator Network Operating System", size: 28, italics: true })] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 400 },
                children: [new TextRun({ text: "Hardware Design & Assembly Guide", size: 24 })] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 200 },
                children: [new TextRun({ text: "Raspberry Pi Pico 2-W NTP/PTP Server", size: 22 })] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 200 },
                children: [new TextRun({ text: "Synchronized to FE-5680A Rubidium Frequency Standard", size: 22 })] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 400, after: 200 },
                children: [new TextRun({ text: "Version 1.0", size: 20 })] }),
            new Paragraph({ alignment: AlignmentType.CENTER,
                children: [new TextRun({ text: "Open Source Hardware Project", size: 20, color: "666666" })] }),
            
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 1: Overview
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("1. Project Overview")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("CHRONOS-Rb is a high-precision NTP/PTP time server based on the Raspberry Pi Pico 2-W microcontroller, disciplined by an FE-5680A rubidium atomic frequency standard. This combination provides Stratum-1 quality time distribution over WiFi with sub-microsecond stability.")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("1.1 Key Features")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun("Stratum-1 NTP server with rubidium reference (10⁻¹¹ stability)")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun("IEEE 1588 PTP support for precision timing applications")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun("WiFi connectivity via Pico 2-W's CYW43439 chip")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun("Web-based status and configuration interface")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun("PIO-based precision timing for sub-microsecond accuracy")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun("Automatic holdover during reference signal loss")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("1.2 System Architecture")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("The system consists of three main subsystems: the FE-5680A rubidium oscillator providing the 10MHz reference and 1PPS timing signals, the signal conditioning circuitry that converts these signals to Pico-compatible levels, and the Pico 2-W running the CHRONOS-Rb firmware.")] }),
            
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 2: FE-5680A Overview
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("2. FE-5680A Rubidium Oscillator")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("The FE-5680A is a compact rubidium atomic frequency standard originally designed for telecommunications applications. It provides an extremely stable 10MHz output signal locked to the hyperfine transition of rubidium-87 atoms.")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("2.1 Specifications")] }),
            
            // FE-5680A Specifications Table
            createSpecTable([
                ["Parameter", "Value", "Notes"],
                ["Output Frequency", "10 MHz", "Sine wave output"],
                ["Output Level", "~1 Vpp", "Into 50Ω load"],
                ["Short-term Stability", "< 3×10⁻¹¹", "At 1 second"],
                ["Long-term Stability", "< 5×10⁻¹²", "At 1 day"],
                ["Aging Rate", "< 5×10⁻¹¹/month", "After 30 days"],
                ["1PPS Output", "TTL compatible", "~100µs pulse width"],
                ["Supply Voltage (+15V)", "15V DC", "Physics package heater"],
                ["Supply Voltage (+5V)", "5V DC", "Electronics"],
                ["Warmup Current", "~2A @ 15V", "First 3-5 minutes"],
                ["Operating Current", "~0.7A @ 15V", "After warmup"],
                ["Warmup Time", "3-5 minutes", "To frequency lock"],
                ["Lock Indicator", "Active LOW", "Open collector"]
            ]),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("2.2 FE-5680A Pinout")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("The FE-5680A uses a 15-pin D-sub connector. The following pins are used in this project:")] }),
            
            createSpecTable([
                ["Pin", "Signal", "Description"],
                ["1", "+15V", "Main power supply input"],
                ["2", "GND", "Ground reference"],
                ["3", "+5V", "Logic power supply"],
                ["4", "GND", "Ground reference"],
                ["5", "10MHz OUT", "Sine wave output (1Vpp)"],
                ["6", "GND", "10MHz ground reference"],
                ["7", "1PPS OUT", "1 pulse per second output"],
                ["8", "GND", "1PPS ground reference"],
                ["9", "LOCK", "Lock indicator (active LOW)"],
                ["10", "GND", "Lock indicator ground"]
            ]),
            
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 3: Signal Conditioning
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("3. Signal Conditioning Circuit")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("The FE-5680A outputs a 10MHz sine wave at approximately 1Vpp, which must be converted to a 3.3V digital signal for the Pico 2-W. Additionally, the 1PPS signal may need level shifting.")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("3.1 10MHz Sine to Square Converter")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("A high-speed comparator circuit converts the 10MHz sine wave to a clean 3.3V square wave. The LT1016 or MAX999 comparators are recommended for their fast propagation delay (<10ns).")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun("Circuit Description")] }),
            new Paragraph({ style: "Code", spacing: { after: 200 },
                children: [new TextRun(`
                    +3.3V
                      │
                      R1 (10k)
                      │
    10MHz ──┬── R2 ──┤+
    Sine    │  (100)  │      LT1016
            C1        │        or       ──── 10MHz Square
           (100nF)    │      MAX999          to Pico GP3
            │    ┌────┤-
           GND   │    │
                 R3   │
                (10k) │
                 │    │
                GND  GND
`)] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun("Component Values")] }),
            createSpecTable([
                ["Component", "Value", "Purpose"],
                ["R1", "10kΩ", "Pull-up for comparator output"],
                ["R2", "100Ω", "Input series resistor"],
                ["R3", "10kΩ", "Sets threshold at ~1.65V"],
                ["C1", "100nF", "AC coupling capacitor"],
                ["U1", "LT1016/MAX999", "High-speed comparator"]
            ]),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("3.2 1PPS Signal Conditioning")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("The FE-5680A 1PPS output is typically TTL-compatible (0-5V). A simple voltage divider with Schottky diode protection converts this to a Pico-safe 3.3V level.")] }),
            
            new Paragraph({ style: "Code", spacing: { after: 200 },
                children: [new TextRun(`
    1PPS ──── R4 ────┬──── D1 ────┬──── 1PPS to Pico GP2
    Input   (2.2k)   │   (BAT54)  │
                     │            │
                     R5          GND
                    (3.3k)
                     │
                    GND
`)] }),
            
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 4: Power Supply
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("4. Power Supply Design")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("The FE-5680A requires both +15V (for the physics package heater) and +5V (for the electronics). The Pico 2-W requires 3.3V which can be supplied via its onboard regulator from 5V USB or VSYS.")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("4.1 Power Requirements Summary")] }),
            createSpecTable([
                ["Rail", "Voltage", "Current (Warmup)", "Current (Running)"],
                ["+15V", "15V DC", "2.0A", "0.7A"],
                ["+5V", "5V DC", "0.3A", "0.2A"],
                ["+3.3V (Pico)", "3.3V DC", "0.3A", "0.15A"]
            ]),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("4.2 Recommended Power Supply")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("Option 1 (Simplest): Use a dedicated 15V 3A power supply for the FE-5680A and power the Pico via USB. The FE-5680A's internal regulator provides its own 5V.")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("Option 2 (Integrated): Use a 15V 3A supply with a DC-DC buck converter (LM2596 or similar) to generate 5V for both the FE-5680A logic and Pico VSYS input.")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun("Power Supply Schematic (Option 2)")] }),
            new Paragraph({ style: "Code", spacing: { after: 200 },
                children: [new TextRun(`
    AC/DC Adapter          DC-DC Buck
    ┌──────────┐          ┌──────────┐
    │  15V 3A  │──+15V───>│ FE-5680A │
    │          │          │   +15V   │
    └────┬─────┘          └──────────┘
         │
         │    ┌──────────────┐
         └───>│  LM2596 5V   │──+5V──>┬── FE-5680A +5V
              │  Buck Module │        │
              └──────────────┘        └── Pico VSYS
`)] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("4.3 Important Safety Notes")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "WARNING: ", bold: true }), new TextRun("The FE-5680A physics package operates at high temperature. Ensure adequate ventilation.")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun("Add a 3A fuse in series with the +15V supply for protection.")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun("Use a power supply with current limiting during initial testing.")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun("Never connect or disconnect signals while the FE-5680A is powered.")] }),
            
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 5: Complete Wiring Guide
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("5. Complete Wiring Guide")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("5.1 Pico 2-W GPIO Assignments")] }),
            createSpecTable([
                ["GPIO", "Function", "Connection"],
                ["GP0", "UART TX", "Debug output (optional)"],
                ["GP1", "UART RX", "Debug input (optional)"],
                ["GP2", "1PPS Input", "From signal conditioning circuit"],
                ["GP3", "10MHz Input", "From comparator output"],
                ["GP4", "Rb Lock Status", "From FE-5680A pin 9"],
                ["GP5", "Rb Enable", "To FE-5680A enable (if available)"],
                ["GP6", "LED Sync", "Green LED - synchronized"],
                ["GP7", "LED Network", "Blue LED - WiFi connected"],
                ["GP8", "LED Activity", "Yellow LED - NTP/PTP traffic"],
                ["GP9", "LED Error", "Red LED - error condition"],
                ["GP10", "Debug PPS Out", "Regenerated 1PPS for testing"],
                ["GP11", "Debug Sync", "Sync pulse indicator"],
                ["GP12", "I2C SDA", "Optional OLED display"],
                ["GP13", "I2C SCL", "Optional OLED display"]
            ]),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("5.2 Complete System Schematic")] }),
            new Paragraph({ style: "Code", spacing: { after: 200 },
                children: [new TextRun(`
┌─────────────────────────────────────────────────────────────────────────────┐
│                           CHRONOS-Rb SYSTEM SCHEMATIC                        │
└─────────────────────────────────────────────────────────────────────────────┘

                              ┌─────────────────────┐
     +15V DC ────────────────>│ Pin 1: +15V         │
                              │                     │
     +5V DC ─────────────────>│ Pin 3: +5V          │
                              │                     │
     GND ────────────────────>│ Pin 2,4: GND        │
                              │                     │
                              │    FE-5680A         │
                              │    RUBIDIUM         │
                              │    OSCILLATOR       │
                              │                     │
     10MHz Sine <─────────────│ Pin 5: 10MHz OUT    │
                              │                     │
     1PPS Out <───────────────│ Pin 7: 1PPS OUT     │
                              │                     │
     Lock Status <────────────│ Pin 9: LOCK         │
                              └─────────────────────┘
                                        │
         ┌──────────────────────────────┼──────────────────────────────┐
         │                              │                              │
         ▼                              ▼                              ▼
┌─────────────────┐          ┌─────────────────┐          ┌─────────────────┐
│  10MHz SIGNAL   │          │   1PPS SIGNAL   │          │  LOCK STATUS    │
│  CONDITIONING   │          │  CONDITIONING   │          │  CONDITIONING   │
│                 │          │                 │          │                 │
│ +3.3V──R1(10k)  │          │  1PPS──R4(2.2k) │          │ LOCK──R6(10k)──┐│
│         │       │          │         │       │          │                ││
│ 10MHz───┼──R2───│          │         ├──D1───│          │                ▼│
│  Sine   │ (100) │          │         │(BAT54)│          │         ┌──────┤│
│         │       │          │         │       │          │         │   To ││
│    C1───┤+ U1   │          │    R5───┤       │          │    R7───┤  GP4 ││
│ (100nF) │  (LT  │          │  (3.3k) │       │          │  (10k)  │      ││
│         │  1016)│          │         │       │          │         │      ││
│    R3───┤-      │──>GP3    │        GND      │──>GP2    │        GND     ││
│  (10k)  │       │          │                 │          │                ││
│         │       │          │                 │          │                ││
│   GND───┴───────│          │                 │          │                ││
└─────────────────┘          └─────────────────┘          └─────────────────┘

                    ┌─────────────────────────────────────────┐
                    │         RASPBERRY PI PICO 2-W           │
                    │                                         │
        UART TX <───│ GP0                             GP28 │
        UART RX ───>│ GP1                             GP27 │
       1PPS In ────>│ GP2                             GP26 │
      10MHz In ────>│ GP3                              ADC │
      Rb Lock ─────>│ GP4                             3V3  │───> +3.3V Out
      Rb Enable <───│ GP5                             GND  │───> GND
      LED Sync <────│ GP6                             VSYS │<─── +5V In
      LED Net <─────│ GP7                             VBUS │
      LED Act <─────│ GP8                              RUN │
      LED Err <─────│ GP9                               EN │
      PPS Debug <───│ GP10                           GP22 │
      Sync Debug <──│ GP11                           GP21 │
      I2C SDA <────>│ GP12                           GP20 │
      I2C SCL <────>│ GP13                           GP19 │
                    │ GP14                           GP18 │
                    │ GP15                           GP17 │
                    │ GND                            GP16 │
                    │ GP16                            GND │
                    └─────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                              STATUS LEDs                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   GP6 ──── R(330Ω) ────│>├──── GND    (GREEN - Sync Status)                │
│   GP7 ──── R(330Ω) ────│>├──── GND    (BLUE - Network)                     │
│   GP8 ──── R(330Ω) ────│>├──── GND    (YELLOW - Activity)                  │
│   GP9 ──── R(330Ω) ────│>├──── GND    (RED - Error)                        │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
`)] }),
            
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 6: Bill of Materials
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("6. Bill of Materials")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("6.1 Main Components")] }),
            createBOMTable([
                ["Qty", "Description", "Part Number", "Notes"],
                ["1", "Raspberry Pi Pico 2-W", "SC1623", "With WiFi"],
                ["1", "FE-5680A Rubidium Oscillator", "FE-5680A", "Surplus/eBay"],
                ["1", "15V 3A DC Power Supply", "-", "Minimum 45W"],
                ["1", "LT1016 or MAX999 Comparator", "LT1016CN8", "DIP-8 package"],
                ["1", "15-pin D-sub connector", "-", "Male, for FE-5680A"]
            ]),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("6.2 Signal Conditioning Components")] }),
            createBOMTable([
                ["Qty", "Description", "Value", "Notes"],
                ["2", "Resistor", "10kΩ 1%", "For comparator"],
                ["1", "Resistor", "100Ω 1%", "Input series"],
                ["1", "Resistor", "2.2kΩ", "1PPS divider"],
                ["1", "Resistor", "3.3kΩ", "1PPS divider"],
                ["4", "Resistor", "330Ω", "LED current limit"],
                ["1", "Capacitor", "100nF ceramic", "AC coupling"],
                ["1", "Capacitor", "100µF electrolytic", "Power filter"],
                ["4", "Capacitor", "100nF ceramic", "Bypass caps"],
                ["1", "Schottky Diode", "BAT54", "ESD protection"],
                ["1", "LED Green", "3mm", "Sync status"],
                ["1", "LED Blue", "3mm", "Network status"],
                ["1", "LED Yellow", "3mm", "Activity"],
                ["1", "LED Red", "3mm", "Error"]
            ]),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("6.3 Optional Components")] }),
            createBOMTable([
                ["Qty", "Description", "Part Number", "Notes"],
                ["1", "SSD1306 OLED Display", "0.96\" I2C", "Status display"],
                ["1", "LM2596 Buck Module", "-", "5V from 15V"],
                ["1", "Enclosure", "-", "Hammond 1590B or similar"],
                ["1", "SMA connector", "-", "10MHz output (optional)"],
                ["1", "BNC connector", "-", "1PPS output (optional)"]
            ]),
            
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 7: Assembly Instructions
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("7. Assembly Instructions")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("7.1 Preparation")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Gather all components from the Bill of Materials")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Test the FE-5680A separately before integration")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Program the Pico 2-W with the CHRONOS-Rb firmware")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Prepare your workspace with proper ESD protection")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("7.2 Signal Conditioning Board Assembly")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Build the 10MHz comparator circuit on a small perfboard or PCB")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Add bypass capacitors (100nF) close to the comparator power pins")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Build the 1PPS voltage divider circuit")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Add the lock status level shifter")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Test each section individually before connecting to Pico")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("7.3 System Integration")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Connect the D-sub connector to the FE-5680A")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Connect power supply (15V) to the FE-5680A - wait for warmup")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Verify 10MHz output with oscilloscope (~1Vpp sine wave)")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Verify 1PPS output (pulse once per second)")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Verify lock indicator goes LOW when locked (~3-5 min)")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Connect signal conditioning board outputs to Pico")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Connect status LEDs to appropriate GPIOs")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Power up and monitor serial debug output")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("7.4 Testing Procedure")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Connect to the Pico via USB and open serial terminal (115200 baud)")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Verify startup messages and initialization")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Wait for WiFi connection (or configure via web interface)")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Access web interface at http://<ip-address>/")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Test NTP: ntpdate -q <ip-address>")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 },
                children: [new TextRun("Verify sync state reaches LOCKED after several minutes")] }),
            
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 8: Firmware
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("8. Firmware Overview")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("8.1 Building the Firmware")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("Prerequisites: Pico SDK 2.0+, CMake 3.13+, ARM GCC toolchain")] }),
            
            new Paragraph({ style: "Code", spacing: { after: 200 },
                children: [new TextRun(`# Clone the repository
git clone https://github.com/yourname/chronos-rb
cd chronos-rb/firmware

# Create build directory
mkdir build && cd build

# Configure and build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
make -j4

# Flash to Pico (hold BOOTSEL while connecting USB)
cp chronos_rb.uf2 /media/RPI-RP2/`)] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("8.2 Configuration")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("Edit include/chronos_rb.h to configure WiFi credentials and other parameters before building:")] }),
            
            new Paragraph({ style: "Code", spacing: { after: 200 },
                children: [new TextRun(`#define WIFI_SSID_DEFAULT    "YourNetwork"
#define WIFI_PASS_DEFAULT    "YourPassword"
#define WIFI_COUNTRY         "US"   // Two-letter country code`)] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("8.3 Module Overview")] }),
            createSpecTable([
                ["Module", "File", "Description"],
                ["Main", "main.c", "Entry point, initialization, main loop"],
                ["PPS Capture", "pps_capture.c/.pio", "PIO-based 1PPS timing capture"],
                ["Freq Counter", "freq_counter.c/.pio", "10MHz frequency measurement"],
                ["Rb Sync", "rubidium_sync.c", "Rubidium sync state machine"],
                ["Discipline", "time_discipline.c", "PI controller for time steering"],
                ["NTP Server", "ntp_server.c", "NTPv4 server implementation"],
                ["PTP Server", "ptp_server.c", "IEEE 1588 PTP implementation"],
                ["WiFi", "wifi_manager.c", "WiFi connection management"],
                ["Web UI", "web_interface.c", "HTTP status/config interface"]
            ]),
            
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 9: Using the Time Server
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("9. Using the Time Server")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("9.1 NTP Client Configuration")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun("Linux (systemd-timesyncd)")] }),
            new Paragraph({ style: "Code", spacing: { after: 200 },
                children: [new TextRun(`# Edit /etc/systemd/timesyncd.conf
[Time]
NTP=192.168.1.100
FallbackNTP=pool.ntp.org

# Restart service
sudo systemctl restart systemd-timesyncd`)] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun("Linux (chrony)")] }),
            new Paragraph({ style: "Code", spacing: { after: 200 },
                children: [new TextRun(`# Add to /etc/chrony/chrony.conf
server 192.168.1.100 iburst prefer

# Restart chrony
sudo systemctl restart chronyd`)] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun("Windows")] }),
            new Paragraph({ style: "Code", spacing: { after: 200 },
                children: [new TextRun(`# Set NTP server
w32tm /config /manualpeerlist:192.168.1.100 /syncfromflags:manual /update

# Force sync
w32tm /resync`)] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun("macOS")] }),
            new Paragraph({ style: "Code", spacing: { after: 200 },
                children: [new TextRun(`# System Preferences > Date & Time > Set date and time automatically
# Enter: 192.168.1.100

# Or via command line:
sudo sntp -sS 192.168.1.100`)] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("9.2 Web Interface")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("Access the web interface by navigating to http://<device-ip>/ in a browser. The interface shows real-time status including sync state, offset, frequency correction, and NTP statistics. The page auto-refreshes every 5 seconds.")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("9.3 JSON API")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("A JSON API is available at http://<device-ip>/api/status for integration with monitoring systems.")] }),
            
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 10: Troubleshooting
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("10. Troubleshooting")] }),
            
            createSpecTable([
                ["Symptom", "Possible Cause", "Solution"],
                ["No 10MHz signal", "FE-5680A not powered", "Check +15V and +5V connections"],
                ["Rb won't lock", "Insufficient warmup", "Wait 5+ minutes after power-on"],
                ["Rb won't lock", "Physics package fault", "Check for error codes, may need service"],
                ["No 1PPS output", "Not locked yet", "Wait for lock indicator"],
                ["Pico not booting", "Power issue", "Check VSYS voltage (3.3-5.5V)"],
                ["WiFi won't connect", "Wrong credentials", "Check SSID/password in config"],
                ["WiFi won't connect", "Wrong country code", "Set correct WIFI_COUNTRY"],
                ["High offset", "Signal conditioning", "Check comparator and divider circuits"],
                ["NTP not responding", "Firewall blocking", "Open UDP port 123"],
                ["LOCKED then HOLDOVER", "Intermittent signal", "Check cable connections"]
            ]),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("10.1 Debug Output")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun("Connect to the Pico's USB serial port at 115200 baud for detailed debug output. The firmware prints status messages every 10 seconds and logs all state transitions.")] }),
            
            new Paragraph({ children: [new PageBreak()] }),
            
            // Appendix
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("Appendix A: Project Name")] }),
            new Paragraph({ spacing: { after: 200 },
                children: [new TextRun({ text: "CHRONOS-Rb", bold: true, size: 28 }), new TextRun(" stands for:")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "C", bold: true }), new TextRun("ompact")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "H", bold: true }), new TextRun("igh-precision")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "R", bold: true }), new TextRun("ubidium")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "O", bold: true }), new TextRun("scillator")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "N", bold: true }), new TextRun("etwork")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "O", bold: true }), new TextRun("perating")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "S", bold: true }), new TextRun("ystem")] }),
            new Paragraph({ spacing: { before: 200, after: 200 },
                children: [new TextRun("The \"-Rb\" suffix denotes the rubidium (chemical symbol Rb) atomic reference. In Greek mythology, Chronos (Χρόνος) is the personification of time.")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("Alternative Project Names")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "AtomSync", bold: true }), new TextRun(" - Simple and descriptive")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "RubiClock", bold: true }), new TextRun(" - Rubidium + Clock")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "Stratum-Rb", bold: true }), new TextRun(" - Reference to NTP stratum levels")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "PicoAtom", bold: true }), new TextRun(" - Pico + Atomic reference")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 },
                children: [new TextRun({ text: "TimeForge", bold: true }), new TextRun(" - Precision time creation")] }),
            
            new Paragraph({ spacing: { before: 400 }, alignment: AlignmentType.CENTER,
                children: [new TextRun({ text: "— End of Document —", italics: true, color: "666666" })] })
        ]
    }]
});

// Helper function to create specification tables
function createSpecTable(data) {
    const tableBorder = { style: BorderStyle.SINGLE, size: 1, color: "CCCCCC" };
    const cellBorders = { top: tableBorder, bottom: tableBorder, left: tableBorder, right: tableBorder };
    
    const rows = data.map((row, rowIndex) => {
        return new TableRow({
            tableHeader: rowIndex === 0,
            children: row.map((cell, cellIndex) => {
                const isHeader = rowIndex === 0;
                const width = Math.floor(9360 / row.length);
                return new TableCell({
                    borders: cellBorders,
                    width: { size: width, type: WidthType.DXA },
                    shading: isHeader ? { fill: "e94560", type: ShadingType.CLEAR } : undefined,
                    children: [new Paragraph({ 
                        alignment: AlignmentType.LEFT,
                        children: [new TextRun({ 
                            text: cell, 
                            bold: isHeader,
                            color: isHeader ? "FFFFFF" : "000000",
                            size: 20 
                        })]
                    })]
                });
            })
        });
    });
    
    return new Table({
        columnWidths: data[0].map(() => Math.floor(9360 / data[0].length)),
        rows: rows
    });
}

// Helper function for BOM tables
function createBOMTable(data) {
    return createSpecTable(data);
}

// Save the document
Packer.toBuffer(doc).then(buffer => {
    fs.writeFileSync("/mnt/user-data/outputs/CHRONOS-Rb_Hardware_Guide.docx", buffer);
    console.log("Document created successfully!");
});
