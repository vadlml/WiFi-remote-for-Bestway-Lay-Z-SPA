# Bestway Lay-Z-Spa Serial Communication Protocol
## Protocol Specification v1.0

**Document Version:** 1.0
**Date:** December 11, 2025
**Author:** Reverse-engineered from WiFi-remote-for-Bestway-Lay-Z-SPA project

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Protocol Overview](#2-protocol-overview)
3. [Physical Layer](#3-physical-layer)
4. [Data Link Layer](#4-data-link-layer)
5. [Packet Structure](#5-packet-structure)
6. [CIO Protocol (Pump Controller)](#6-cio-protocol-pump-controller)
7. [DSP Protocol (Display Panel)](#7-dsp-protocol-display-panel)
8. [Command Set](#8-command-set)
9. [Model Variants](#9-model-variants)
10. [Implementation Notes](#10-implementation-notes)
11. [Example Communication](#11-example-communication)

---

## 1. Introduction

This document describes the proprietary serial communication protocol used in Bestway Lay-Z-Spa hot tub systems for communication between the main pump controller (CIO - Controller I/O) and the display panel (DSP - Display).

### 1.1 Purpose

The protocol enables:
- Temperature monitoring and reporting
- Pump, heater, jets, and bubble control
- Error code transmission
- Display panel button input
- Status synchronization between controller and display

### 1.2 Document Scope

This specification covers:
- **4-wire models** (54123, 54138, 54144, 54149E, 54154, 54173)
- **6-wire models** (PRE2021, MIAMI2021, MALDIVES2021)

---

## 2. Protocol Overview

### 2.1 Architecture

The protocol implements a **bidirectional, half-duplex** serial communication system with two independent data paths:

```
┌─────────────────┐           ┌─────────────────┐
│  Pump           │           │   Display       │
│  Controller     │◄─────────►│   Panel         │
│  (CIO)          │   Serial  │   (DSP)         │
└─────────────────┘   9600    └─────────────────┘
```

### 2.2 Communication Characteristics

- **Topology:** Point-to-point
- **Mode:** Half-duplex, asynchronous
- **Host Model:** Dual-master (both devices can initiate)
- **Timing:** Event-driven with periodic keep-alive (≤2000ms intervals)

---

## 3. Physical Layer

### 3.1 Electrical Specifications

| Parameter | Value |
|-----------|-------|
| Voltage Level | TTL (0-5V) |
| Logic High | 3.3V - 5V |
| Logic Low | 0V - 0.8V |
| Connection Type | Single-ended |

### 3.2 Serial Configuration

| Parameter | Value |
|-----------|-------|
| Baud Rate | 9600 bps |
| Data Bits | 8 |
| Parity | None |
| Stop Bits | 1 |
| Flow Control | None |
| Frame Format | 8N1 |

### 3.3 Pin Configuration

#### 4-Wire Models
1. **VCC** - Power (+5V)
2. **CIO_TX** - Controller transmit / Display receive
3. **DSP_TX** - Display transmit / Controller receive
4. **GND** - Ground

#### 6-Wire Models
1. **VCC** - Power (+5V)
2. **CIO_CLK** - Controller clock
3. **CIO_DATA** - Controller data
4. **DSP_CLK** - Display clock
5. **DSP_DATA** - Display data
6. **GND** - Ground

---

## 4. Data Link Layer

### 4.1 Transmission Timing

- **Maximum Inter-Message Interval:** 2000ms
- **Timeout for Response:** 20ms
- **Typical Message Rate:** 500-1000ms

### 4.2 Error Detection

- **Method:** Simple checksum (8-bit modulo 256 sum)
- **Error Handling:** Discard invalid packets, increment error counter

### 4.3 Synchronization

Messages are synchronized through a simple request-response pattern:
1. One device sends a message
2. The other device responds immediately
3. If no message received within 2000ms, device sends unsolicited message

---

## 5. Packet Structure

### 5.1 Common Packet Format (4-Wire Models)

All packets are **7 bytes** in length:

```
┌──────┬──────┬──────┬──────┬──────┬──────┬──────┐
│ SoF  │ Hdr  │ Data │ Dat2 │ Flgs │ Chks │ EoF  │
│ [0]  │ [1]  │ [2]  │ [3]  │ [4]  │ [5]  │ [6]  │
└──────┴──────┴──────┴──────┴──────┴──────┴──────┘
```

| Byte | Name | Description |
|------|------|-------------|
| 0 | Start of Frame | Framing byte (copied from original) |
| 1 | Header | Protocol identifier (1=CIO, 2=DSP) |
| 2 | Data | Primary data byte (temp/command) |
| 3 | Data2 | Secondary data byte (error/flags) |
| 4 | Flags | Status/control flags |
| 5 | Checksum | Sum of bytes [1] through [4] |
| 6 | End of Frame | Framing byte (copied from original) |

### 5.2 Checksum Algorithm

```c
uint8_t checksum = byte[1] + byte[2] + byte[3] + byte[4];
// Result is naturally modulo 256 due to uint8_t overflow
```

**Validation:**
```c
bool valid = (received_checksum == calculated_checksum);
```

---

## 6. CIO Protocol (Pump Controller)

### 6.1 FROM Controller (Status Messages)

The pump controller continuously sends status updates to the display:

```
Byte    Content             Description
──────────────────────────────────────────────────
[0]     SoF                 Start marker
[1]     0x01                CIO identifier
[2]     Temperature         Current temp in °C (0-50)
[3]     Error Code          0 = no error, 1-99 = error
[4]     Unknown             Flags (purpose unclear)
[5]     Checksum            Sum of [1]+[2]+[3]+[4]
[6]     EoF                 End marker
```

**Example:**
```
[0xXX, 0x01, 0x26, 0x00, 0x30, 0x57, 0xXX]
                ^^^^  ^^^^
             38°C    No error
```

### 6.2 TO Controller (Command Messages)

Commands sent to the controller to control spa functions:

```
Byte    Content             Description
──────────────────────────────────────────────────
[0]     SoF                 Start marker (copied)
[1]     0x01                CIO identifier (copied)
[2]     Command Bitmask     Control bits (see 6.3)
[3]     0x30                Fixed value (usually 48/0x30)
[4]     0x00                Fixed value (usually 0)
[5]     Checksum            Sum of [1]+[2]+[3]+[4]
[6]     EoF                 End marker (copied)
```

### 6.3 Command Bitmask (Byte 2)

The command byte uses a bitmask to control multiple functions simultaneously. **Bit positions vary by model** (see Section 9).

**Example: Model 54123**

```
Bit 7  Bit 6  Bit 5  Bit 4  Bit 3  Bit 2  Bit 1  Bit 0
  -      -    BUBBL  PUMP   HEAT2   -     HEAT1  POWER
```

| Bit | Mask | Function | Description |
|-----|------|----------|-------------|
| 0 | 0x01 | POWER | Master power bit (auto-set if any feature on) |
| 1 | 0x02 | HEAT1 | Heater stage 1 (1500W typical) |
| 3 | 0x08 | HEAT2 | Heater stage 2 (1500W typical) |
| 4 | 0x10 | PUMP | Filter pump |
| 5 | 0x20 | BUBBLES | Air bubbles/jets |

**Command Composition:**
```c
command = (heat_stage1 * 0x02) |
          (heat_stage2 * 0x08) |
          (jets * 0x00) |        // No jets on 54123
          (bubbles * 0x20) |
          (pump * 0x10);

if (command > 0) {
    command |= 0x01;  // Auto-set power bit
}
```

**Examples:**
```
0x00 = Everything off
0x11 = Pump only (0x10 | 0x01)
0x21 = Bubbles + Pump (0x20 | 0x10 | 0x01)
0x13 = Pump + Heater Stage 1 (0x10 | 0x02 | 0x01)
0x1B = Pump + Both Heaters (0x10 | 0x08 | 0x02 | 0x01)
```

### 6.4 Temperature Encoding

- **Units:** Celsius
- **Range:** 0-50°C (displayed as 5-40°C typically)
- **Resolution:** 1°C
- **Conversion to Fahrenheit:** `F = C * 1.8 + 32`

### 6.5 Error Codes

When byte[3] is non-zero, an error condition exists:

| Code | Description |
|------|-------------|
| 0 | No error |
| 1-99 | Device-specific error codes |

**Error Display Format:** `E` + two-digit code (e.g., "E03", "E15")

---

## 7. DSP Protocol (Display Panel)

### 7.1 FROM Display (Button Press Messages)

The display panel sends button press information:

```
Byte    Content             Description
──────────────────────────────────────────────────
[0]     SoF                 Start marker
[1]     0x02                DSP identifier
[2]     Command Bitmask     Button state (same format as 6.3)
[3]     Unknown             Purpose unclear
[4]     Unknown             Possibly ready flags
[5]     Checksum            Sum of [1]+[2]+[3]+[4]
[6]     EoF                 End marker
```

The command bitmask in byte[2] reflects which buttons are currently active on the display.

### 7.2 TO Display (Status/Temperature Messages)

Status information sent to update the display:

```
Byte    Content             Description
──────────────────────────────────────────────────
[0]     SoF                 Start marker (copied)
[1]     0x02                DSP identifier (copied)
[2]     Temperature         Temp in °C to display
[3]     Error Code          Error to show (0 = none)
[4]     Ready Flags         Status indicators
[5]     Checksum            Sum of [1]+[2]+[3]+[4]
[6]     EoF                 End marker (copied)
```

### 7.3 Display Update Logic

In **Pass-Through Mode:**
- ESP copies `_raw_payload_from_cio` → `_raw_payload_to_dsp`
- Display shows actual controller state

In **God Mode:**
- ESP generates custom payload
- Can display custom temperature
- Can show custom error codes
- Enables WiFi control override

---

## 8. Command Set

### 8.1 Basic Operations

#### 8.1.1 Turn On Pump
```
Command: 0x11
Binary:  00010001
Bits:    PUMP=1, POWER=1
```

#### 8.1.2 Enable Heater (Stage 1)
```
Command: 0x13
Binary:  00010011
Bits:    PUMP=1, HEAT1=1, POWER=1
Note:    Pump must be on for heater
```

#### 8.1.3 Enable Full Heating (Both Stages)
```
Command: 0x1B
Binary:  00011011
Bits:    PUMP=1, HEAT2=1, HEAT1=1, POWER=1
```

#### 8.1.4 Enable Bubbles
```
Command: 0x31
Binary:  00110001
Bits:    BUBBLES=1, PUMP=1, POWER=1
```

#### 8.1.5 Everything Off
```
Command: 0x00
Binary:  00000000
Bits:    All off
```

### 8.2 Safety Interlocks

The protocol enforces several safety rules:

1. **Pump Must Run for Heater**
   - Heater commands automatically ensure pump is on
   - Prevents dry heating

2. **Heater Cooldown**
   - When turning off pump while heating, heater turns off first
   - 5-second cooldown delay before pump stops
   - Prevents thermal shock

3. **Staged Heater Start**
   - Stage 1 heater starts first
   - 10-second delay before Stage 2 activates
   - Prevents electrical surge

4. **Antifreeze Protection**
   - If temp < 10°C in God Mode, auto-start pump + heater
   - Target temperature set to 10°C
   - Prevents freeze damage

5. **Antiboil Protection**
   - If temp > 41°C, auto-stop heater
   - Pump continues to circulate
   - Prevents overheating

---

## 9. Model Variants

Different spa models use different bit assignments for the same functions. The protocol structure remains identical, but command bitmasks differ.

### 9.1 Model 54123 (Vegas)

```c
PUMP_BITMASK     = 0b00010000  // Bit 4
BUBBLES_BITMASK  = 0b00100000  // Bit 5
JETS_BITMASK     = 0b00000000  // No jets
HEAT_BITMASK1    = 0b00000010  // Bit 1
HEAT_BITMASK2    = 0b00001000  // Bit 3
POWER_BITMASK    = 0b00000001  // Bit 0
```

**Features:** Bubbles, no jets

### 9.2 Model 54138

```c
PUMP_BITMASK     = 0b00010000  // Bit 4
BUBBLES_BITMASK  = 0b00100000  // Bit 5
JETS_BITMASK     = 0b00000000  // No jets
HEAT_BITMASK1    = 0b00000010  // Bit 1
HEAT_BITMASK2    = 0b00001000  // Bit 3
POWER_BITMASK    = 0b00000001  // Bit 0
```

**Features:** Bubbles, no jets

### 9.3 Model 54144

```c
PUMP_BITMASK     = 0b00010000  // Bit 4
BUBBLES_BITMASK  = 0b00100000  // Bit 5
JETS_BITMASK     = 0b01000000  // Bit 6
HEAT_BITMASK1    = 0b00000010  // Bit 1
HEAT_BITMASK2    = 0b00001000  // Bit 3
POWER_BITMASK    = 0b00000001  // Bit 0
```

**Features:** Bubbles AND jets (Hydrojet models)

### 9.4 State Machine

Models implement a state machine to prevent invalid combinations:

**Example: Model 54123 Allowed States**

```
State  Bubbles  Jets  Pump  Heat
───────────────────────────────────
  0      0       0     0     0    (All off)
  1      1       0     0     0    (Bubbles only)
  2      0       0     1     0    (Pump only)
  3      0       0     1     2    (Pump + Both heaters)
```

**Jump Table:** Defines state transitions when buttons pressed

---

## 10. Implementation Notes

### 10.1 Message Timing

**Recommended implementation:**

```c
// Send command when:
// 1. Immediate response to received message
// 2. OR max interval exceeded

if (message_received || (elapsed_time > 2000ms)) {
    send_message();
    reset_timer();
}
```

### 10.2 Checksum Validation

**Always validate before processing:**

```c
uint8_t calc_checksum(uint8_t* packet) {
    return packet[1] + packet[2] + packet[3] + packet[4];
}

bool validate_packet(uint8_t* packet, size_t len) {
    if (len != 7) return false;
    if (packet[5] != calc_checksum(packet)) {
        bad_packet_count++;
        return false;
    }
    good_packet_count++;
    return true;
}
```

### 10.3 ESP8266 Integration

**Pin mapping for man-in-the-middle:**

```
ESP Pin    Function             Connect To
──────────────────────────────────────────────────
D1 (GPIO5) CIO RX (read pump)   Pump controller TX
D2 (GPIO4) CIO TX (write pump)  Pump controller RX
D4 (GPIO2) DSP RX (read display) Display panel TX
D5 (GPIO14) DSP TX (write display) Display panel RX
```

**Software serial setup:**

```cpp
// Reading from pump TX line
cio_serial->begin(9600, SWSERIAL_8N1, cio_tx_pin, cio_rx_pin);

// Reading from display TX line
dsp_serial->begin(9600, SWSERIAL_8N1, dsp_tx_pin, dsp_rx_pin);
```

### 10.4 God Mode vs Pass-Through

**Pass-Through Mode** (transparent interception):
```cpp
// Simply forward messages unchanged
dsp_out_buffer = cio_in_buffer;  // Controller → Display
cio_out_buffer = dsp_in_buffer;  // Display → Controller
```

**God Mode** (active control):
```cpp
// Generate custom commands
cio_out_buffer = generate_command(desired_state);
dsp_out_buffer = generate_display(temp, error);
```

---

## 11. Example Communication

### 11.1 Startup Sequence

```
Time   Direction  Packet                           Description
────────────────────────────────────────────────────────────────────
0ms    CIO→DSP   [XX 01 14 00 30 45 XX]          Temp=20°C, no error
10ms   DSP→CIO   [XX 02 00 00 00 02 XX]          No buttons pressed
520ms  CIO→DSP   [XX 01 14 00 30 45 XX]          Temp=20°C, no error
530ms  DSP→CIO   [XX 02 00 00 00 02 XX]          No buttons pressed
```

### 11.2 Turning On Pump

```
Time   Direction  Packet                           Description
────────────────────────────────────────────────────────────────────
0ms    DSP→CIO   [XX 02 11 00 00 13 XX]          Pump button pressed
10ms   CIO→DSP   [XX 01 14 00 30 45 XX]          Temp=20°C (pump starting)
20ms   DSP→CIO   [XX 02 11 00 00 13 XX]          Pump command confirmed
```

### 11.3 Enabling Heat

```
Time   Direction  Packet                           Description
────────────────────────────────────────────────────────────────────
0ms    DSP→CIO   [XX 02 13 00 00 15 XX]          Heat button pressed
10ms   CIO→DSP   [XX 01 14 00 30 45 XX]          Temp=20°C
20ms   DSP→CIO   [XX 02 13 00 00 15 XX]          Heat confirmed (stage 1)

10000ms DSP→CIO  [XX 02 1B 00 00 1D XX]          Stage 2 enabled after delay
```

### 11.4 Error Condition

```
Time   Direction  Packet                           Description
────────────────────────────────────────────────────────────────────
0ms    CIO→DSP   [XX 01 14 05 30 4A XX]          Temp=20°C, Error E05
10ms   DSP→CIO   [XX 02 00 00 00 02 XX]          Display showing error
20ms   CIO→DSP   [XX 01 14 05 30 4A XX]          Error persists
```

### 11.5 ESP8266 Intercept (God Mode)

```
Time   Device        Packet                       Description
────────────────────────────────────────────────────────────────────
0ms    Pump→ESP     [XX 01 14 00 30 45 XX]      ESP reads temp=20°C
5ms    ESP→Display  [XX 02 17 00 00 19 XX]      ESP shows temp=23°C
10ms   Display→ESP  [XX 02 00 00 00 02 XX]      ESP reads button state
15ms   ESP→Pump     [XX 02 1B 00 00 1D XX]      ESP commands heat+pump
```

---

## Appendix A: Bit Manipulation Examples

### A.1 Setting Individual Bits

```c
uint8_t command = 0x00;

// Turn on pump
command |= (1 << 4);  // Set bit 4 → command = 0x10

// Add heater stage 1
command |= (1 << 1);  // Set bit 1 → command = 0x12

// Add power bit
command |= (1 << 0);  // Set bit 0 → command = 0x13
```

### A.2 Reading Individual Bits

```c
uint8_t received = 0x1B;  // Pump + both heaters + power

bool pump_on = (received & 0x10) != 0;     // true
bool heat1_on = (received & 0x02) != 0;    // true
bool heat2_on = (received & 0x08) != 0;    // true
bool bubbles_on = (received & 0x20) != 0;  // false
```

### A.3 Clearing Individual Bits

```c
uint8_t command = 0x1B;  // All heating features on

// Turn off heat stage 2
command &= ~(1 << 3);  // Clear bit 3 → command = 0x13
```

---

## Appendix B: Model Detection

The firmware auto-detects spa model from configuration file:

```json
{
  "cio_model": "M54123",
  "dsp_model": "M54123",
  "pins": [5, 4, 2, 14, 12, 13, 15, 0]
}
```

Model enum values:
```c
enum Models {
    PRE2021,
    MIAMI2021,
    MALDIVES2021,
    M54149E,
    M54173,
    M54154,
    M54144,
    M54138,
    M54123
};
```

---

## Appendix C: Troubleshooting

### C.1 No Communication

**Check:**
- Baud rate is exactly 9600
- Pin connections correct (TX→RX, RX→TX)
- Voltage levels compatible (3.3V vs 5V)
- Serial buffer size adequate (≥7 bytes)

### C.2 Checksum Errors

**Common causes:**
- Wrong checksum bytes included in calculation
- Byte order reversed
- Using wrong packet indices
- Electrical noise on lines

**Fix:**
```c
// Correct checksum:
sum = packet[1] + packet[2] + packet[3] + packet[4];

// NOT:
sum = packet[0] + packet[1] + ... + packet[6];  // Wrong!
```

### C.3 Commands Ignored

**Verify:**
- POWER bit set when any feature active
- Pump on before enabling heater
- State machine allows this transition
- Message sent at correct timing (<2000ms)

---

## Appendix D: References

**Source Code:**
- [WiFi-remote-for-Bestway-Lay-Z-SPA](https://github.com/visualapproach/WiFi-remote-for-Bestway-Lay-Z-SPA)

**Key Files:**
- `Code/lib/cio/CIO_4W.cpp` - CIO protocol implementation
- `Code/lib/dsp/DSP_4W.cpp` - DSP protocol implementation
- `Code/lib/cio/CIO_4W_MODEL_SPECIFIC.h` - Model-specific bitmasks
- `Code/src/main.cpp` - Main control loop

**Hardware:**
- ESP8266 NodeMCU
- 8-channel bidirectional level converter
- JST-SM connectors (4 or 6 pin)

---

**END OF SPECIFICATION**
