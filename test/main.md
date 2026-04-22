MODBUS Cellular Gateway 
User Manual (Refactored & Expanded)
1. Overview
1.1 Product Description
The MODBUS Cellular Gateway is an industrial communication device that converts Modbus commands (TCP/RTU) into SMS alerts over a cellular network.
In practical terms, it allows a PLC, SCADA system, or any Modbus master to trigger SMS notifications by simply writing values to Modbus registers.
This makes it useful for:
•	Alarm notifications (e.g., pump failure, over-temperature)
•	Remote alerts where internet/cloud is not required
•	Legacy system upgrades using Modbus

1.2 System Architecture
PLC / SCADA (Modbus Master)
            ↓
   Modbus TCP / RTU
            ↓					
     ESP32 Gateway
            ↓
        4G Modem
            ↓
        SMS Users

1.3 Key Features
•	Dual protocol support: Modbus TCP and Modbus RTU
•	SMS alerts to up to 5 recipients per event
•	CSV-based configuration (no firmware change required)
•	Built-in Wi-Fi Access Point for setup
•	One-time trigger mechanism (prevents duplicate SMS spam)
1.4 Hardware Components
Typical system includes:
•	ESP32 Controller (main processing unit)
•	4G Cellular Modem (SMS transmission)
•	Ethernet Module (W5500) for Modbus TCP
•	RS485 Interface for Modbus RTU
2. Quick Start Guide
Use this section for initial setup without reading the full manual.
1.	Power ON the device
2.	Turn ON the AP switch
3.	Connect to Wi-Fi:
o	SSID: ESP32_FileServer
o	Password: 12345678
4.	Open browser and go to: http://192.168.4.1
5.	Download configuration file (MBmapconf.csv)
6.	Edit phone numbers and message text
7.	Upload the updated CSV file
3. Configuration via Wi-Fi (AP Mode)
3.1 Purpose of AP Mode
The device creates its own Wi-Fi network to allow configuration without external infrastructure. This mode is used only for setup and configuration.
3.2 Detailed Connection Steps (Reference)
This section provides a detailed explanation of the Quick Start steps.
•	Turn ON AP switch
•	Connect to Wi-Fi:
o	SSID: ESP32_FileServer
o	Password: 12345678
•	Open browser and enter: http://192.168.4.1
This opens the configuration interface.
3.3 CSV Configuration File (MBmapconf.csv)
Concept
Each row in the CSV file represents one SMS event. The row number is mapped directly to a Modbus register.
Structure
Field	Description
Msg. No.	Unique identifier (mapped to register)
Phone1–Phone5	Up to 5 recipients
Message	SMS content


Example
Msg. No.	Phone1	Phone2	Phone3	Phone4	Phone5	Message
1	9876543210	9123456780	8134850923	8991381344	9487138810	Pump ON Alert
Important Rules (Critical)
•	Do NOT modify "Msg. No." values
•	Do NOT change column order
•	Do NOT add/remove columns
•	Leave unused phone fields blank
•	Save strictly in .csv format
Upload Process
1.	Download existing CSV from device
2.	Edit using Excel or any spreadsheet tool
3.	Save as .csv
4.	Upload through web interface
5.	Device immediately uses updated configuration
4. Working Principle
4.1 Register-Based Triggering
The gateway continuously monitors Modbus holding registers for trigger commands.
•	Registers 40001 – 40050 → Command registers
•	Each register corresponds to one CSV row
Example mapping:
•	Register 40001 → Message 1
•	Register 40002 → Message 2
4.2 Edge Trigger Mechanism (Important)
SMS is triggered only when the register value changes from 0 → 1.
Why this matters:
•	Prevents repeated SMS flooding
•	Ensures controlled alert behavior


Resending a Message
To resend the same message:
1.	Write 0 to the register
2.	Then write 1 again
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

