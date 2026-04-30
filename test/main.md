# MODBUS SMS Gateway (ESP32) – Comprehensive Technical Manual

## 1. System Overview

### 1.1 Product Description
The MODBUS Cellular Gateway is an industrial-grade communication device built on the ESP32 microcontroller that converts Modbus commands (TCP/RTU) into SMS alerts over a 4G cellular network. It bridges the gap between industrial PLCs/SCADA systems using Modbus and remote notification requirements through SMS.

**Primary Use Cases:**
- Industrial alarm notifications (pump failure, over-temperature, pressure alerts)
- Remote machine monitoring without internet/cloud infrastructure
- Legacy system upgrades using standard Modbus protocols
- Multi-recipient alert distribution (up to 5 recipients per event)

### 1.2 System Architecture Diagram
```
PLC / SCADA (Modbus Master)
            ↓
    Modbus TCP / RTU
    (Port 502, 9600 baud)
            ↓
      ESP32 Gateway
      (Holding Registers)
            ↓
    4G/GSM Modem (SIM Card)
            ↓
    SMS Recipients (Up to 5 per message)
```

### 1.3 Key Features
- **Dual Protocol Support**: Modbus TCP (Ethernet) and Modbus RTU (RS485) simultaneously
- **Multiple Recipients**: Up to 5 phone numbers per SMS message
- **No Firmware Recompile**: CSV-based configuration for messages
- **Wi-Fi Configuration Portal**: Built-in AP mode for easy setup
- **Edge-Trigger Logic**: Prevents SMS flooding (detects 0→1 transitions only)
- **Status Feedback**: Result registers confirm success/failure for each SMS attempt
- **Hot Configuration**: Changes apply immediately without restart
- **Industrial Robustness**: DHCP fallback, error recovery, comprehensive logging

### 1.4 Hardware Components
The complete system consists of:
- **ESP32 Development Board** (NodeMCU-32S) – Main processing unit
- **4G/GSM Modem** (connected via UART) – SMS transmission
- **W5500 Ethernet Module** (SPI connection) – Modbus TCP interface
- **RS485 Interface** (UART) – Modbus RTU interface
- **Power Supply**: 5V for ESP32, 9V DC for 4G Modem
- **Latching Switch**: Manual control for AP mode activation

---

## 2. Hardware Configuration & Pin Connections

### 2.1 ESP32 (NodeMCU-32S) Pin Assignments

#### **4G Modem Connection (UART1)**
| ESP32 GPIO | Modem Pin | Function | Notes |
|-----------|-----------|----------|-------|
| GPIO 32 | Pin 16 | MODEM_PWRKEY | Power control pin (pull high to enable modem) |
| GPIO 16 | Pin 8 | RX from Modem | UART1 receive |
| GPIO 17 | Pin 9 | TX to Modem | UART1 transmit |
| GND | Pin 6/7 | Ground | Common ground reference |
| — | Pin 1 | +9V Power | External 9V supply required |

#### **W5500 Ethernet Module (SPI)**
| ESP32 GPIO | W5500 Pin | Function | Notes |
|-----------|-----------|----------|-------|
| GPIO 3V3 | V-Pin | 3.3V Power | Direct ESP32 3.3V supply |
| GND | G-Pin | Ground | Common ground |
| GPIO 23 | MO | SPI MOSI | Master Out, Slave In |
| GPIO 19 | MI | SPI MISO | Master In, Slave Out |
| GPIO 18 | SCK | SPI Clock | Serial Clock |
| GPIO 5 | CS | Chip Select | Active low |
| GPIO 14 | RST | Reset Pin | Active low reset |

#### **RS485 Interface (UART2)**
- Uses default UART2 (GPIO 16 RX, GPIO 17 TX on standard ESP32)
- Note: Actual RS485 converter IC required for differential signaling
- Baud Rate: 9600 bps
- Configuration: 8N1 (8 data bits, No parity, 1 stop bit)
- Slave ID: 1

#### **Configuration Button (Latching Switch)**
| ESP32 GPIO | Switch Terminal | Function | Notes |
|-----------|-----------------|----------|-------|
| GPIO 33 | Terminal 2 | AP Mode Control | Active high; GND to Terminal 1 |
| GND | Terminal 1 | Ground | Reference |

---

## 3. Software Configuration

### 3.1 PlatformIO Build Configuration (platformio.ini)
```ini
[env:nodemcu-32s]
platform = espressif32
board = nodemcu-32s
framework = arduino
monitor_speed = 115200

# Framework Version
platform_packages =
    framework-arduinoespressif32 @ https://github.com/espressif/arduino-esp32.git#3.0.2
    framework-arduinoespressif32-libs @ https://github.com/espressif/arduino-esp32/releases/download/3.0.2/esp32-arduino-libs-3.0.2.zip

# Required Libraries
lib_deps = 
    arduino-libraries/ArduinoModbus@^1.0.9
    emelianov/modbus-esp8266@^4.1.0
    esp32async/ESPAsyncWebServer@^3.10.3

# File System Configuration
board_build.filesystem = littlefs
board_build.partitions = min_spiffs.csv

# Build Scripts
extra_scripts = pre:scripts/build_tag.py
```

### 3.2 System Constants (Shared.h)
| Constant | Value | Purpose |
|----------|-------|---------|
| MESSAGE_SLOT_COUNT | 50 | Maximum configurable SMS messages |
| PHONE_SLOTS_PER_MESSAGE | 5 | Recipients per message |
| PHONE_NUMBER_LENGTH | 20 | Max digits per phone number |
| MESSAGE_TEXT_LENGTH | 512 | Max SMS content length |
| HOLDING_REGISTER_COUNT | 100 | Total Modbus registers |
| INPUT_REGISTER_COUNT | 4 | Device status input registers |

### 3.3 Register Layout
| Register Type | Start | Count | Purpose |
|---------------|-------|-------|---------|
| Command Registers | 40001 (0) | 50 | User trigger inputs (write 1 to activate) |
| Status Registers | 40051 (50) | 50 | Operation result feedback |
| Input Registers | — | 4 | Device status (read-only) |

### 3.4 Input Registers (Read-Only Status)
| Register | Modbus Address | Purpose | Values |
|----------|---|---------|--------|
| Device Status | 40:0 | Overall system state | 0=Unknown, 1=Ready, 2=Busy, -1=Error |
| Modem Status | 40:1 | Modem connectivity | 0=Unknown, 1=Ready, 2=Busy, -1=Error |
| SIM Status | 40:2 | SIM card status | 0=Unknown, 1=Ready, 2=Busy, -1=Error |
| Network Status | 40:3 | Cellular network | 0=Unknown, 1=Ready, 2=Busy, -1=Error |

---

## 4. Initialization & Startup Sequence

### 4.1 Power-On Sequence
1. **Shared Memory Initialization**: Mutexes and data structures initialized
2. **File System Mount**: LittleFS mounted on flash memory
3. **Configuration Check**: Verifies MBmapconf.csv exists; creates default if missing
4. **Ethernet Interface**: DHCP attempted; defaults to 192.168.8.200 if fails
5. **Modbus TCP Server**: Starts on port 502
6. **Modbus RTU Interface**: Initializes RS485 at 9600 bps, Slave ID 1
7. **4G Modem Initialization**: Powers on modem via GPIO 32, establishes SIM connection
8. **FreeRTOS Task Launch**: Background tasks for RTU, TCP, modem, and AP control
9. **System Ready**: Awaits Modbus commands or button input

### 4.2 Ethernet Connectivity
- **Primary**: DHCP discovery (requests IP automatically)
- **Fallback**: Static IP 192.168.8.200 if DHCP unavailable
- **Subnet Mask**: 255.255.255.0
- **Gateway**: Configurable via settings
- **Port**: 502 (standard Modbus TCP)

### 4.3 AP Mode Activation
**Trigger**: Press latching switch (GPIO 33) or toggle via Modbus
**WiFi Credentials:**
- SSID: `ESP32_FileServer`
- Password: `12345678`
- URL: `http://192.168.4.1`
- Available Only During AP Mode

---

## 5. Configuration Management

### 5.1 CSV Configuration File (MBmapconf.csv)
**Format**: Comma-separated values (strict format required)

**Column Structure:**
| Column | Field Name | Max Length | Notes |
|--------|-----------|-----------|-------|
| 1 | Msg. No. | N/A | 1–50; MUST match row number |
| 2–6 | Phone1–Phone5 | 20 chars | Recipient numbers; leave blank if unused |
| 7 | Message | 512 chars | SMS text content |

**Example File:**
```csv
Msg. No.,Phone1,Phone2,Phone3,Phone4,Phone5,Message
1,9876543210,9123456780,8134850923,8991381344,9487138810,Pump ON Alert
2,919876543210,,,,Motor Overload Detected
3,918765432109,919876543210,,,System Maintenance Required
```

### 5.2 Critical Rules for CSV Configuration
⚠️ **MUST DO:**
- Save file as `.csv` (plain text comma-separated)
- Keep "Msg. No." sequential (1, 2, 3, ..., 50)
- Leave blank phone fields empty (no spaces or zeros)
- Use valid 10–13 digit phone numbers (country code optional)
- One message per row

⚠️ **MUST NOT DO:**
- Modify "Msg. No." values
- Change column order or count
- Add extra columns
- Rename the file (must be `MBmapconf.csv`)
- Use spreadsheet-specific formats (.xlsx, .xls)

### 5.3 Configuration Upload via Web Portal
1. Press latching switch or activate AP mode
2. Connect WiFi to `ESP32_FileServer` (password: `12345678`)
3. Open browser: `http://192.168.4.1`
4. **Download**: Click to save current configuration
5. **Edit**: Open CSV in Excel, WPS Office, or text editor
6. **Delete Old Config**: Remove existing MBmapconf.csv from device
7. **Upload New Config**: Upload modified file (must be named `MBmapconf.csv`)
8. **Verify**: Webpage shows "Loaded message entries: X" at bottom
9. **Reconnect**: Device returns to normal mode; changes active immediately

---

## 6. Modbus Register Map

### 6.1 Command Registers (Holding Registers 40001–40050)
**Purpose**: Write SMS trigger commands here

**Behavior:**
- Write `1` → Initiates SMS sending (if message configured)
- Write `0` → Resets trigger state
- Read response → Check status register for result
- **Edge Trigger**: Only 0→1 transition triggers SMS (prevents repeated sends)

| Register | Modbus Address | Data Type | R/W | Scale | Message |
|----------|---|-----------|-----|-------|---------|
| CMD_Reg-1 | 40001 | INT16 | R/W | 0/1 | Row 1 of CSV |
| CMD_Reg-2 | 40002 | INT16 | R/W | 0/1 | Row 2 of CSV |
| ... | ... | INT16 | R/W | 0/1 | ... |
| CMD_Reg-50 | 40050 | INT16 | R/W | 0/1 | Row 50 of CSV |

### 6.2 Status Registers (Holding Registers 40051–40100)
**Purpose**: Device responds with operation results here

**Result Codes:**
| Value | Meaning | Action |
|-------|---------|--------|
| 0 | Idle/No action | Waiting for trigger |
| 1–5 | Success | SMS sent to N recipients |
| -1 | SMS Send Failure | Modem issue or network error |
| -2 | SIM Card Error | SIM not detected or invalid |
| -3 | Network Error | No cellular coverage/registration |
| -4 | Configuration Error | Message configuration invalid |
| -5 | No Valid Recipients | All phone fields blank in CSV |
| -6 | Modem/Queue Error | Device busy or hardware fault |

| Register | Modbus Address | Data Type | R/W | Scale | Purpose |
|----------|---|-----------|-----|-------|---------|
| Status_Reg-51 | 40051 | INT16 | R | -6 to 5 | Result of Message 1 |
| Status_Reg-52 | 40052 | INT16 | R | -6 to 5 | Result of Message 2 |
| ... | ... | INT16 | R | -6 to 5 | ... |
| Status_Reg-100 | 40100 | INT16 | R | -6 to 5 | Result of Message 50 |

### 6.3 Register Access Example (Pseudo-Code)
```
// Trigger SMS message #1
WRITE Modbus Register 40001 = 1

// Wait for modem processing (typically 5–30 seconds)
SLEEP 2 seconds

// Check result
READ Modbus Register 40051
IF value > 0:
    SMS sent successfully to value recipients
ELSE IF value < 0:
    Error code returned; check table above
ELSE (value == 0):
    Still processing or idle
```

---

## 7. Working Principles

### 7.1 Edge-Triggered SMS Sending
The system detects a **rising edge** (transition from 0 → 1) on trigger registers to prevent SMS flooding.

**Sequence:**
```
Time:    0s      5s      10s     15s     20s
Value:   0   →   1   →   1   →   0   →   1   → ...
Action:  ----  SEND ---- WAIT ---- HOLD -- SEND ---
SMS:          ✓                          ✓
```

**Why This Matters:**
- **Without edge detection**: System would send SMS continuously while register = 1 (flooding)
- **With edge detection**: SMS only when value transitions from 0 to 1 (controlled)

### 7.2 Resending a Message
To send the same message again:
1. Write `0` to the command register
2. Write `1` again (triggers new 0→1 edge)
3. Monitor corresponding status register for result

### 7.3 Register State Persistence
- **Command Registers**: Retain value until explicitly reset
- **Status Registers**: Update after SMS processing; persist until next operation
- **Memory**: All registers persist in RAM; lost on power cycle

---

## 8. Dual-Interface Unified Operation

### 8.1 Modbus TCP Interface
**Connection Details:**
- **Protocol**: Modbus TCP/IP (RFC 1006)
- **IP Address**: DHCP or 192.168.8.200 (static fallback)
- **Port**: 502 (standard Modbus)
- **Function Codes Supported**: 3 (Read Holding Registers), 16 (Write Holding Registers)
- **Communication**: Ethernet (W5500 module SPI connected)

**Typical Client Connection:**
```python
# Example: Python Modbus TCP client
import pymodbus.client.serial_async as client

# Connect to device
client = client.ModbusTcpClient('192.168.8.200', port=502)

# Write trigger to register 40001
client.write_register(0, 1)  # Address 0 = Modbus 40001

# Read result from register 40051
result = client.read_holding_registers(50, 1)  # Address 50 = Modbus 40051
```

### 8.2 Modbus RTU Interface
**Connection Details:**
- **Protocol**: Modbus RTU over RS485
- **Baud Rate**: 9600 bps
- **Configuration**: 8 data bits, No parity, 1 stop bit (8N1)
- **Slave ID**: 1 (fixed)
- **Physical**: RS485 differential pair (A, B)

**Typical Serial Connection:**
```
PLC RTU Master ←→ USB/Serial Adapter ←→ ESP32 RS485
                                       (GPIO 16/17 via RS485 IC)
```

### 8.3 Unified Register Behavior
Both TCP and RTU interfaces interact with **identical register memory**:
- Write to register via TCP → Immediately visible via RTU
- Write to register via RTU → Immediately visible via TCP
- **Single Source of Truth**: One register map for both protocols

**Advantage**: Seamless integration into mixed-protocol industrial systems

---

## 9. Testing Procedures

### 9.1 Pre-Deployment Checklist
- [ ] ESP32 powers on successfully
- [ ] W5500 Ethernet module recognized
- [ ] 4G/GSM Modem powered and SIM card inserted
- [ ] RS485 interface connected to PLC/slave device
- [ ] Configuration file (MBmapconf.csv) loaded with test messages
- [ ] Wi-Fi AP mode activates with latching switch
- [ ] Web portal accessible at http://192.168.4.1
- [ ] Configuration can be downloaded and re-uploaded
- [ ] Ethernet connection assigned (DHCP or static)

### 9.2 Modbus TCP Test
1. **Install Modbus Tester**: Use QModbus, ModbusView, or custom client
2. **Connect**: Point to device IP (192.168.8.200) on port 502
3. **Write Test**: Send value 1 to register 40001 (message #1)
4. **Monitor**: Check register 40051 within 30 seconds
5. **Verify**: Result should be 1–5 (success) or negative (error)
6. **Check Phone**: Verify SMS received on configured recipients

### 9.3 Modbus RTU Test
1. **Connect Serial Port**: USB adapter to RS485 module (9600 baud)
2. **Use RTU Client**: QModbus, Modbus Poll, or custom script
3. **Set Slave ID**: 1 (matching device setting)
4. **Write Trigger**: Send FC 16 (write) to register 0 (40001), value 1
5. **Monitor Status**: Read register 50 (40051) for result
6. **Verify SMS**: Confirm message on recipient phones

### 9.4 SMS Verification
- **Check Recipient Lists**: Ensure phone numbers in CSV are correct
- **Test All Recipients**: Send to each configured phone number
- **Verify Message Content**: Confirm SMS text matches CSV
- **Check Timestamps**: Verify delivery times in modem logs
- **Test Error States**: Trigger with invalid/missing phone numbers to verify error codes

### 9.5 Configuration Change Testing
1. Download current CSV from portal
2. Add new message or modify recipients
3. Delete old configuration
4. Upload new CSV
5. Verify loaded entries update on web page
6. Test new message via Modbus to confirm changes applied

---

## 10. Troubleshooting Guide

### 10.1 Device Not Booting
| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| No power LED | Power supply issue | Check USB cable; verify 5V on ESP32 |
| Constant reboot | Stack overflow or crash | Check Serial monitor output; rebuild |
| Modem not initializing | GPIO 32 misconfigured | Verify Pin 32 connects to MODEM_PWRKEY |

### 10.2 Ethernet Not Connecting
| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| DHCP timeout | DHCP server unavailable | Device falls back to 192.168.8.200 |
| W5500 not detected | SPI wiring fault | Check GPIO 5, 18, 19, 23, 14 connections |
| No IP assigned | Ethernet cable issue | Test W5500 with loopback; verify cable |

### 10.3 Modbus Registers Not Responding
| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| Read timeout | Device not listening on port 502 | Verify Ethernet connection; restart device |
| Wrong values | Register address mismatch | Confirm using address 0 for register 40001 |
| Register locked | Mutex contention | Check if other task accessing register |

### 10.4 SMS Not Sending
| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| Status = -2 (SIM Error) | SIM card not detected | Remove/reseat SIM; check contacts |
| Status = -3 (Network Error) | No cellular coverage | Move device; check signal strength |
| Status = -5 (No Recipients) | Empty phone field in CSV | Edit CSV; ensure phone1 not blank |
| Status = -6 (Modem Error) | Modem hardware fault | Check GPIO 16/17 connections; power cycle |

### 10.5 CSV Upload Issues
| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| Upload fails silently | File format not .csv | Save strictly as CSV (not Excel native) |
| Only partial config loads | Column/row mismatch | Verify 7 columns and correct sequence |
| Old config still active | Cache not cleared | Delete old file before uploading new |

### 10.6 Serial Monitoring (Debugging)
Connect to device at 115200 baud to see real-time logs:
```bash
# Using Arduino IDE Serial Monitor or PlatformIO
monitor_speed = 115200
```

Key debug outputs:
- `[INIT]` – Startup sequence
- `[CONFIG]` – CSV loading results
- `[MODBUS]` – Register access events
- `[MODEM]` – SMS sending activity
- `[ERROR]` – System errors with codes

---

## 11. System Limits & Performance

### 11.1 Specifications
| Parameter | Value | Notes |
|-----------|-------|-------|
| Maximum Messages | 50 | Configured via CSV rows |
| Max Recipients/Message | 5 | Phone number slots per row |
| Max Phone Number Length | 20 characters | Includes country code |
| Max Message Length | 512 characters | SMS body text |
| Register Count (Holding) | 100 | 50 command + 50 status |
| Modbus TCP Port | 502 | Standard; not configurable |
| Modbus RTU Baud | 9600 bps | Fixed; matches industrial standard |
| Processing Delay | 5–30 seconds | SMS queuing and sending |
| Ethernet DHCP Timeout | Automatic fallback | Falls back to 192.168.8.200 |
| LittleFS Size | Min_SPIFFS partition | ~1 MB (configurable) |

### 11.2 Performance Considerations
- **Concurrent SMS Requests**: Queued internally; processed sequentially
- **Register Access**: Lock-free reads; mutex-protected writes (50ms timeout)
- **Configuration Changes**: Applied immediately; no restart required
- **File I/O**: Mutex-protected; blocks other I/O up to 500ms
- **Task Priority**: Modem task lower than Modbus tasks to ensure responsiveness

---

## 12. Safety & Best Practices

### 12.1 Operational Guidelines
✅ **DO:**
- Test configuration thoroughly before deployment
- Monitor status registers to verify successful operations
- Implement application-level retry logic in PLC/SCADA
- Keep SIM cards active with valid plans
- Position device for optimal cellular coverage

❌ **DON'T:**
- Modify firmware during operation
- Power cycle while active SMS transfer
- Share device across multiple independent systems
- Leave AP mode permanently enabled
- Reset via button during normal operation

### 12.2 Maintenance
- **Monthly**: Verify SIM card still active; check SMS balance
- **Quarterly**: Rotate backup configuration; test full failure scenarios
- **Annually**: Review logs; update phone numbers if needed

---

## 13. Quick Reference

### Modbus Address Quick Map
```
Command Registers:
  Register 40001 = Address 0  →  Message 1
  Register 40002 = Address 1  →  Message 2
  ...
  Register 40050 = Address 49 →  Message 50

Status Registers:
  Register 40051 = Address 50 →  Result Message 1
  Register 40052 = Address 51 →  Result Message 2
  ...
  Register 40100 = Address 99 →  Result Message 50
```

### GPIO Pin Quick Reference
| Function | GPIO | Notes |
|----------|------|-------|
| Modem Power | 32 | Pull HIGH to enable |
| Modem RX | 16 | UART1 receive |
| Modem TX | 17 | UART1 transmit |
| SPI MOSI | 23 | W5500 data out |
| SPI MISO | 19 | W5500 data in |
| SPI CLK | 18 | W5500 clock |
| SPI CS | 5 | W5500 chip select |
| W5500 RST | 14 | Reset pin |
| AP Button | 33 | Latching switch input |

### Error Code Reference
```
Status Register Values:
  0  = Idle
  1–5 = Success (N recipients)
  -1 = Send failed
  -2 = SIM error
  -3 = Network error
  -4 = Config error
  -5 = No recipients
  -6 = Modem error
```

---

## Revision History
| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-04-30 | Initial comprehensive configuration document |

**For Support**: Refer to project source code or contact development team.
4.3 Internal Processing Flow
When a trigger occurs, the system performs:
1.	Detects rising edge (0 → 1)
2.	Maps register to CSV row
3.	Reads message and phone numbers
4.	Validates phone numbers
5.	Sends SMS via modem
6.	Updates result register
5. Modbus Register Map
5.1 Overview Table
Type	Range	Purpose
Input Registers	30001 – 30004	Device status monitoring
Holding Registers (Command)	40001 – 40050	SMS trigger control
Holding Registers (Result)	40051 – 40100	SMS result feedback

5.2 Status Registers (Input Registers)
Register	Description	Typical Use
30001	Device Status	Overall system health
30002	Modem Status	Modem ready/busy
30003	SIM Status	SIM detection/validity
30004	Network Status	Signal/network availability
These registers help diagnose issues before triggering SMS.

5.3 Result Registers (Feedback)
Each command register has a corresponding result register:
•	Message 1 → 40051
•	Message 2 → 40052

Result Interpretation:
Value	Meaning
0	No action yet
1–5	SMS sent successfully (count of recipients)
-1	SMS sending failed
-2	SIM error
-3	Network error
-4	Configuration error
-5	No valid recipients
-6	Modem or queue error

Important Note
Some Modbus tools display signed values incorrectly:
Displayed	Actual
65535	-1
65534	-2
65533	-3

6. Operational Behavior
•	SMS is triggered only once per rising edge
•	Registers are NOT auto-reset by device
•	External controller (PLC/user) must manage reset logic
•	Device behavior is fully controlled by CSV configuration
6.1 Recommended PLC Logic
To avoid issues:
•	Always reset register after triggering
7. Troubleshooting Guide
7.1 SMS Not Sent
Check systematically:
1.	SIM inserted correctly
2.	SIM detected (check register 30003)
3.	Network available (check register 30004)
4.	Modem ready (check register 30002)
5.	Phone numbers valid in CSV
6.	CSV formatting correct
7.2 SMS Sent to Fewer Numbers
Possible causes:
•	Some phone fields are empty
•	Invalid number format
7.3 Cannot Access Web Interface
•	Ensure AP switch is ON
•	Reconnect Wi-Fi
•	Verify IP: 192.168.4.1
•	Disable mobile data (to avoid routing issues)
7.4 No Response from Device
•	Check power supply
•	Verify Modbus wiring (RS485 polarity, Ethernet link)
•	Confirm Modbus settings (baud rate, IP, slave ID)
8. Summary
The MODBUS Cellular Gateway provides a reliable bridge between industrial Modbus systems and GSM-based SMS alerts. With a simple register-based triggering mechanism and CSV-driven configuration, it offers a flexible and low-maintenance solution for industrial alerting systems.

