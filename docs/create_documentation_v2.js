const { Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell, ImageRun,
        Header, Footer, AlignmentType, HeadingLevel, BorderStyle, WidthType, 
        ShadingType, PageNumber, LevelFormat, PageBreak } = require('docx');
const fs = require('fs');

// Load images
const basePath = '/home/addy/work/claude-code/chronos-rb/hardware';
const img10MHz = fs.readFileSync(`${basePath}/10mhz_comparator.png`);
const img1PPS = fs.readFileSync(`${basePath}/1pps_levelshifter.png`);
const imgLock = fs.readFileSync(`${basePath}/lock_detector.png`);
const imgPower = fs.readFileSync(`${basePath}/power_supply.png`);
const imgSystem = fs.readFileSync(`${basePath}/system_wiring.png`);
const imgLEDs = fs.readFileSync(`${basePath}/led_circuit.png`);
const imgPulses = fs.readFileSync(`${basePath}/interval_pulses.png`);

// Helper function to create specification tables
function createSpecTable(data) {
    const tableBorder = { style: BorderStyle.SINGLE, size: 1, color: "CCCCCC" };
    const cellBorders = { top: tableBorder, bottom: tableBorder, left: tableBorder, right: tableBorder };
    const rows = data.map((row, rowIndex) => {
        return new TableRow({
            tableHeader: rowIndex === 0,
            children: row.map((cell) => {
                const isHeader = rowIndex === 0;
                const width = Math.floor(9360 / row.length);
                return new TableCell({
                    borders: cellBorders,
                    width: { size: width, type: WidthType.DXA },
                    shading: isHeader ? { fill: "e94560", type: ShadingType.CLEAR } : undefined,
                    children: [new Paragraph({ 
                        alignment: AlignmentType.LEFT,
                        children: [new TextRun({ text: cell, bold: isHeader, color: isHeader ? "FFFFFF" : "000000", size: 20 })]
                    })]
                });
            })
        });
    });
    return new Table({ columnWidths: data[0].map(() => Math.floor(9360 / data[0].length)), rows: rows });
}

const doc = new Document({
    styles: {
        default: { document: { run: { font: "Arial", size: 22 } } },
        paragraphStyles: [
            { id: "Title", name: "Title", basedOn: "Normal", run: { size: 56, bold: true, color: "1a1a2e" }, paragraph: { spacing: { before: 240, after: 120 }, alignment: AlignmentType.CENTER } },
            { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true, run: { size: 32, bold: true, color: "e94560" }, paragraph: { spacing: { before: 360, after: 120 }, outlineLevel: 0 } },
            { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true, run: { size: 26, bold: true, color: "0f3460" }, paragraph: { spacing: { before: 240, after: 120 }, outlineLevel: 1 } },
            { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true, run: { size: 24, bold: true, color: "16213e" }, paragraph: { spacing: { before: 200, after: 100 }, outlineLevel: 2 } }
        ]
    },
    numbering: {
        config: [
            { reference: "bullet-list", levels: [{ level: 0, format: LevelFormat.BULLET, text: "•", alignment: AlignmentType.LEFT, style: { paragraph: { indent: { left: 720, hanging: 360 } } } }] },
            { reference: "numbered-list", levels: [{ level: 0, format: LevelFormat.DECIMAL, text: "%1.", alignment: AlignmentType.LEFT, style: { paragraph: { indent: { left: 720, hanging: 360 } } } }] }
        ]
    },
    sections: [{
        properties: { page: { margin: { top: 1440, right: 1440, bottom: 1440, left: 1440 } } },
        headers: { default: new Header({ children: [new Paragraph({ alignment: AlignmentType.RIGHT, children: [new TextRun({ text: "CHRONOS-Rb Hardware Documentation", italics: true, size: 20, color: "666666" })] })] }) },
        footers: { default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER, children: [new TextRun({ text: "Page ", size: 20 }), new TextRun({ children: [PageNumber.CURRENT], size: 20 }), new TextRun({ text: " of ", size: 20 }), new TextRun({ children: [PageNumber.TOTAL_PAGES], size: 20 })] })] }) },
        children: [
            // Title Page
            new Paragraph({ heading: HeadingLevel.TITLE, children: [new TextRun("CHRONOS-Rb")] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 200 }, children: [new TextRun({ text: "Compact High-precision Rubidium Oscillator Network Operating System", size: 28, italics: true })] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 400 }, children: [new TextRun({ text: "Hardware Design & Assembly Guide", size: 24 })] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 200 }, children: [new TextRun({ text: "Raspberry Pi Pico 2-W NTP/PTP Server", size: 22 })] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 200 }, children: [new TextRun({ text: "Synchronized to FE-5680A Rubidium Frequency Standard", size: 22 })] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 400, after: 200 }, children: [new TextRun({ text: "Version 1.0", size: 20 })] }),
            new Paragraph({ alignment: AlignmentType.CENTER, children: [new TextRun({ text: "Open Source Hardware Project", size: 20, color: "666666" })] }),
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 1
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("1. Project Overview")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("CHRONOS-Rb is a high-precision NTP/PTP time server based on the Raspberry Pi Pico 2-W microcontroller, disciplined by an FE-5680A rubidium atomic frequency standard. This combination provides Stratum-1 quality time distribution over WiFi with sub-microsecond stability.")] }),
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("1.1 Key Features")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("Stratum-1 NTP server with rubidium reference (10⁻¹¹ stability)")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("IEEE 1588 PTP support for precision timing applications")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("WiFi connectivity via Pico 2-W's CYW43439 chip")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("Web-based status and configuration interface")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("PIO-based precision timing for sub-microsecond accuracy")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("Automatic holdover during reference signal loss")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("Five interval pulse outputs (0.5s, 1s, 6s, 30s, 60s) synchronized to atomic reference")] }),
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 2
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("2. FE-5680A Rubidium Oscillator")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("The FE-5680A is a compact rubidium atomic frequency standard originally designed for telecommunications. It provides an extremely stable 10MHz output signal locked to the hyperfine transition of rubidium-87 atoms.")] }),
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("2.1 Specifications")] }),
            createSpecTable([["Parameter", "Value", "Notes"], ["Output Frequency", "10 MHz", "Sine wave"], ["Output Level", "~1 Vpp", "Into 50Ω"], ["Short-term Stability", "< 3×10⁻¹¹", "At 1 second"], ["Long-term Stability", "< 5×10⁻¹²", "At 1 day"], ["1PPS Output", "NOT AVAILABLE", "Derive from 10MHz"], ["Supply (+15V)", "15V DC", "Physics heater"], ["Supply (+5V)", "5V DC", "Electronics"], ["Warmup Current", "~2A @ 15V", "First 3-5 min"], ["Operating Current", "~0.7A @ 15V", "After warmup"], ["Warmup Time", "3-5 minutes", "To lock"], ["Lock Indicator", "Active LOW", "Open collector"]]),
            new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("2.2 FE-5680A Pinout")] }),
            createSpecTable([["Pin", "Signal", "Description"], ["1", "+15V", "Main power input"], ["2", "GND", "Ground"], ["3", "LOCK", "Lock status (4.8V/0.8V)"], ["4", "+5V", "Logic power"], ["5", "GND", "Ground"], ["7", "10MHz OUT", "Sine wave (~1Vpp)"], ["-", "1PPS", "NOT AVAILABLE - derive from 10MHz"]]),
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 3
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("3. Signal Conditioning Circuit")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("The FE-5680A outputs a 10MHz sine wave at approximately 1Vpp, which must be converted to a 3.3V digital signal for the Pico 2-W. Note: Most FE-5680A units do NOT have a native 1PPS output - it must be derived from the 10MHz or sourced externally.")] }),
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("3.1 10MHz Sine to Square Converter")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("A high-speed comparator converts the 10MHz sine wave to a clean 3.3V square wave. The LT1016 or MAX999 are recommended for their fast propagation delay (<10ns).")] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 200, after: 200 }, children: [new ImageRun({ type: "png", data: img10MHz, transformation: { width: 500, height: 316 }, altText: { title: "10MHz Comparator", description: "Sine to square converter", name: "10MHz" } })] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("The circuit uses AC coupling (C1) to remove DC offset. R3 sets the threshold at ~1.65V. R1 provides pull-up for the open-drain output.")] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("3.2 1PPS Signal (from divider or external)")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("Since most FE-5680A units don't have native 1PPS, derive it from 10MHz or use external GPS. If using a 5V external source, a voltage divider with Schottky diode provides level shifting and ESD protection.")] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 200, after: 200 }, children: [new ImageRun({ type: "png", data: img1PPS, transformation: { width: 420, height: 252 }, altText: { title: "1PPS Level Shifter", description: "5V to 3.3V divider", name: "1PPS" } })] }),

            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("3.3 Lock Status Level Shifter")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("The FE-5680A lock output (Pin 3) is 4.8V when unlocked and 0.8V when locked. An NPN transistor (2N3904) inverts and level-shifts this signal to 3.3V logic, where HIGH indicates locked status.")] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 200, after: 200 }, children: [new ImageRun({ type: "png", data: imgLock, transformation: { width: 468, height: 360 }, altText: { title: "Lock Detector", description: "NPN level shifter for lock status with LEDs", name: "Lock" } })] }),
            new Paragraph({ children: [new PageBreak()] }),

            // Section 4
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("4. Power Supply Design")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("CHRONOS-Rb uses a universal AC mains input (100-240V AC, 50/60Hz) with isolated DC-DC conversion. The power supply provides +15V for the FE-5680A physics heater, +5V for logic, and +3.3V for digital/analog sections. An integrated zero-crossing detector enables AC mains frequency monitoring.")] }),
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("4.1 Power Specifications")] }),
            createSpecTable([["Parameter", "Value", "Notes"], ["AC Input", "100-240V AC", "Universal mains"], ["Frequency", "50/60 Hz", "Auto-sensing"], ["+15V Output", "3A max", "FE-5680A heater"], ["+5V Output", "1A max", "Logic supply"], ["+3.3V Digital", "500mA max", "Pico, peripherals"], ["+3.3V Analog", "100mA max", "Low-noise, comparator"], ["Isolation", "3kV reinforced", "Mains to LV"]]),

            new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("4.2 AC to DC Conversion Chain")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("The power conversion follows this path from mains AC to regulated DC outputs:")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun({ text: "IEC C14 Inlet: ", bold: true }), new TextRun("Standard fused inlet connector for 100-240VAC mains input")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun({ text: "Fuse (2A slow-blow): ", bold: true }), new TextRun("Overcurrent protection on live conductor")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun({ text: "EMI Filter + MOV: ", bold: true }), new TextRun("X2/Y1 capacitors, common-mode choke, MOV surge suppressor")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun({ text: "Bridge Rectifier (GBU806): ", bold: true }), new TextRun("Full-wave rectification, 600V/8A rating")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun({ text: "Bulk Capacitor (470µF/400V): ", bold: true }), new TextRun("Energy storage, produces 140-340V DC bus")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun({ text: "Isolated Flyback Converter: ", bold: true }), new TextRun("Galvanic isolation, steps down to +15V @ 3A")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun({ text: "LM2596 Buck Converter: ", bold: true }), new TextRun("15V to 5V @ 1A switching regulator")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun({ text: "AMS1117/LP5907 LDOs: ", bold: true }), new TextRun("5V to 3.3V linear regulators (digital and low-noise analog)")] }),

            new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("4.3 Power Supply Block Diagram")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("The following diagram shows the complete AC-to-DC power conversion chain with isolation barrier between mains-voltage and low-voltage sections:")] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 200, after: 200 }, children: [new ImageRun({ type: "png", data: imgPower, transformation: { width: 560, height: 400 }, altText: { title: "Power Supply Block Diagram", description: "AC mains to DC power distribution with isolation", name: "Power" } })] }),

            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("4.4 Zero-Crossing Detector Circuit")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("The H11AA1 is an AC-input optocoupler with back-to-back LEDs that detects both positive and negative zero crossings of the AC waveform. This provides galvanic isolation while allowing the Pico to measure mains frequency.")] }),
            new Paragraph({ spacing: { after: 100 }, children: [new TextRun({ text: "Circuit Description:", bold: true })] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("AC Live connects through R1+R2 (2× 47kΩ in series, 94kΩ total) to H11AA1 pin 1")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("AC Neutral connects to H11AA1 pin 2")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("H11AA1 pin 4 (collector) connects to GP19 with 10kΩ pull-up to 3.3V")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("H11AA1 pin 3 (emitter) connects to GND on the low-voltage side")] }),
            new Paragraph({ spacing: { before: 200, after: 200 }, children: [new TextRun("The series resistors (R1+R2) limit LED current to ~2.5mA at 240VAC peak. Using two resistors in series provides adequate voltage rating for mains. The output produces a LOW pulse at each zero crossing.")] }),
            createSpecTable([["Component", "Value", "Function"], ["R1", "47kΩ 1/2W", "Current limit (mains side)"], ["R2", "47kΩ 1/2W", "Current limit (mains side)"], ["U1", "H11AA1", "AC-input opto, 5.3kV isolation"], ["R3", "10kΩ", "Pull-up to 3.3V (LV side)"], ["Output", "GP19", "AC_ZERO_CROSS to Pico"]]),
            new Paragraph({ spacing: { before: 200, after: 200 }, children: [new TextRun({ text: "Output Signal: ", bold: true }), new TextRun("Pulses at 2× mains frequency (100Hz for 50Hz mains, 120Hz for 60Hz mains). Firmware measures period between pulses to calculate actual mains frequency with ~0.001Hz resolution.")] }),

            new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("4.5 Safety Notes")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun({ text: "DANGER: ", bold: true, color: "FF0000" }), new TextRun("This design involves hazardous mains voltage. Risk of electric shock or death.")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("Use properly rated enclosure with strain relief for mains cable.")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("Maintain 6mm minimum creepage between mains and low-voltage sections.")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("The zero-crossing detector provides 5.3kV isolation - do not bypass or modify.")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("FE-5680A physics package runs HOT. Ensure adequate ventilation.")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("Never service with mains connected. Use isolation transformer for testing.")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun("Follow all local electrical codes and regulations.")] }),
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 5
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("5. Complete Wiring Guide")] }),
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("5.1 GPIO Assignments")] }),
            createSpecTable([["GPIO", "Function", "Connection"], ["GP2", "1PPS Input", "From level shifter"], ["GP3", "10MHz Input", "From comparator"], ["GP4", "Lock Status", "From FE-5680A pin 9"], ["GP6", "LED Sync", "Green LED"], ["GP7", "LED Network", "Blue LED"], ["GP8", "LED Activity", "Yellow LED"], ["GP9", "LED Error", "Red LED"], ["GP10", "Debug PPS", "Test output"], ["GP12/13", "I2C", "Optional OLED"], ["GP14", "Pulse 0.5s", "500ms interval"], ["GP15", "Pulse 1s", "1 second interval"], ["GP16", "Pulse 6s", "6 second interval"], ["GP17", "Pulse 30s", "30 second interval"], ["GP18", "Pulse 60s", "60 second interval"], ["GP19", "AC Zero-Cross", "Mains frequency monitor"]]),
            new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("5.2 System Schematic")] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 200, after: 200 }, children: [new ImageRun({ type: "png", data: imgSystem, transformation: { width: 580, height: 435 }, altText: { title: "System Wiring", description: "Complete system", name: "System" } })] }),
            new Paragraph({ children: [new PageBreak()] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("5.3 Status LED Circuit")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("Four LEDs provide visual feedback. Each is driven through a 330Ω current-limiting resistor.")] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 200, after: 200 }, children: [new ImageRun({ type: "png", data: imgLEDs, transformation: { width: 460, height: 234 }, altText: { title: "LED Circuit", description: "Status LEDs", name: "LEDs" } })] }),
            
            new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("5.4 Interval Pulse Outputs")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("CHRONOS-Rb provides five interval pulse outputs synchronized to the rubidium 1PPS reference. These outputs generate 10ms active-high pulses at precise intervals, useful for triggering external equipment, calibration, or timing distribution.")] }),
            new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 200, after: 200 }, children: [new ImageRun({ type: "png", data: imgPulses, transformation: { width: 500, height: 283 }, altText: { title: "Interval Pulses", description: "Timing outputs", name: "Pulses" } })] }),
            createSpecTable([["Output", "GPIO", "Interval", "Use Case"], ["500ms", "GP14", "0.5 seconds", "High-rate timing, servo sync"], ["1s", "GP15", "1 second", "PPS distribution"], ["6s", "GP16", "6 seconds", "GPS-compatible timing"], ["30s", "GP17", "30 seconds", "Calibration triggers"], ["60s", "GP18", "60 seconds", "Minute markers"]]),
            new Paragraph({ spacing: { before: 200, after: 200 }, children: [new TextRun("All outputs are phase-locked to the atomic 1PPS reference and maintain sub-microsecond accuracy. The 0.5s output pulses twice per second (at 0ms and 500ms after each PPS edge). Use external buffers (e.g., 74LVC1G125) if driving cables or loads requiring more than 12mA.")] }),
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 6
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("6. Bill of Materials")] }),
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("6.1 Main Components")] }),
            createSpecTable([["Qty", "Description", "Part Number", "Notes"], ["1", "Raspberry Pi Pico 2-W", "SC1623", "With WiFi"], ["1", "FE-5680A Rubidium", "FE-5680A", "Surplus/eBay"], ["1", "15V 3A PSU", "-", "Min 45W"], ["1", "LT1016/MAX999", "LT1016CN8", "DIP-8"]]),
            new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("6.2 Signal Conditioning")] }),
            createSpecTable([["Qty", "Description", "Value"], ["2", "Resistor", "10kΩ 1%"], ["1", "Resistor", "100Ω"], ["1", "Resistor", "2.2kΩ"], ["1", "Resistor", "3.3kΩ"], ["4", "Resistor", "330Ω"], ["1", "Capacitor", "100nF ceramic"], ["1", "Diode", "BAT54"], ["4", "LEDs", "3mm assorted"]]),
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 7
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("7. Assembly Instructions")] }),
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("7.1 Preparation")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun("Gather all components from the BOM")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun("Test FE-5680A separately before integration")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun("Program Pico with CHRONOS-Rb firmware")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun("Prepare workspace with ESD protection")] }),
            new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("7.2 Assembly Steps")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun("Build 10MHz comparator circuit on perfboard")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun("Add bypass caps near comparator power pins")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun("Build 1PPS voltage divider")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun("Test each section before connecting to Pico")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun("Connect D-sub to FE-5680A")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun("Apply power and wait for lock (~5 min)")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun("Verify signals with oscilloscope")] }),
            new Paragraph({ numbering: { reference: "numbered-list", level: 0 }, children: [new TextRun("Connect signal conditioning to Pico GPIOs")] }),
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 8
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("8. Firmware Overview")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("Build with Pico SDK 2.0+, CMake 3.13+, ARM GCC. Edit chronos_rb.h for WiFi credentials before building. Flash the UF2 file while holding BOOTSEL.")] }),
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("8.1 Module Overview")] }),
            createSpecTable([["Module", "File", "Function"], ["Main", "main.c", "Entry, init, loop"], ["CLI", "cli.c", "Serial command interface"], ["PPS", "pps_capture.c", "1PPS timing"], ["Freq", "freq_counter.c", "10MHz measurement"], ["AC Freq", "ac_freq_monitor.c", "Mains frequency monitor"], ["Sync", "rubidium_sync.c", "State machine"], ["Discipline", "time_discipline.c", "PI controller"], ["Pulse", "pulse_output.c", "Configurable GPIO pulses"], ["NTP", "ntp_server.c", "NTPv4 server"], ["PTP", "ptp_server.c", "IEEE 1588"], ["WiFi", "wifi_manager.c", "Connection mgmt"], ["Web", "web_interface.c", "HTTP interface"]]),
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 9
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("9. Using the Time Server")] }),
            new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("9.1 NTP Client Setup")] }),
            new Paragraph({ spacing: { after: 100 }, children: [new TextRun({ text: "Linux: ", bold: true }), new TextRun("Add NTP=<ip> to /etc/systemd/timesyncd.conf")] }),
            new Paragraph({ spacing: { after: 100 }, children: [new TextRun({ text: "Windows: ", bold: true }), new TextRun("w32tm /config /manualpeerlist:<ip> /syncfromflags:manual /update")] }),
            new Paragraph({ spacing: { after: 100 }, children: [new TextRun({ text: "macOS: ", bold: true }), new TextRun("System Preferences > Date & Time, or: sudo sntp -sS <ip>")] }),
            new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("9.2 Web Interface")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun("Access http://<device-ip>/ for real-time status. JSON API at /api/status for monitoring integration.")] }),
            new Paragraph({ children: [new PageBreak()] }),
            
            // Section 10
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("10. Troubleshooting")] }),
            createSpecTable([["Symptom", "Cause", "Solution"], ["No 10MHz", "Power issue", "Check +15V/+5V"], ["Won't lock", "Warmup", "Wait 5+ minutes"], ["No 1PPS", "Not locked", "Wait for lock LED"], ["WiFi fail", "Credentials", "Check config"], ["High offset", "Signal issue", "Check circuits"], ["NTP timeout", "Firewall", "Open UDP 123"]]),
            new Paragraph({ children: [new PageBreak()] }),
            
            // Appendix
            new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("Appendix: Project Name")] }),
            new Paragraph({ spacing: { after: 200 }, children: [new TextRun({ text: "CHRONOS-Rb ", bold: true, size: 28 }), new TextRun("= ")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun({ text: "C", bold: true }), new TextRun("ompact ")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun({ text: "H", bold: true }), new TextRun("igh-precision ")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun({ text: "R", bold: true }), new TextRun("ubidium ")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun({ text: "O", bold: true }), new TextRun("scillator ")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun({ text: "N", bold: true }), new TextRun("etwork ")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun({ text: "O", bold: true }), new TextRun("perating ")] }),
            new Paragraph({ numbering: { reference: "bullet-list", level: 0 }, children: [new TextRun({ text: "S", bold: true }), new TextRun("ystem")] }),
            new Paragraph({ spacing: { before: 200 }, children: [new TextRun("The \"-Rb\" suffix denotes rubidium (chemical symbol Rb). Chronos (Χρόνος) is the Greek personification of time.")] }),
            new Paragraph({ spacing: { before: 400 }, alignment: AlignmentType.CENTER, children: [new TextRun({ text: "— End of Document —", italics: true, color: "666666" })] })
        ]
    }]
});

Packer.toBuffer(doc).then(buffer => {
    fs.writeFileSync("/home/addy/work/claude-code/chronos-rb/docs/CHRONOS-Rb_Hardware_Guide.docx", buffer);
    console.log("Document created with embedded circuit diagrams!");
});
