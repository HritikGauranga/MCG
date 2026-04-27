MODBUS SMS Gateway (ESP32) – Technical Description
1. Introduction
The MODBUS Cellular Gateway is an embedded system built using the ESP32 microcontroller, designed to enable industrial machines to send SMS alerts based on MODBUS communication. In many industrial environments, machines and controllers communicate using MODBUS protocols, but they lack direct capabilities to notify users remotely when critical events occur. This system bridges that gap by converting MODBUS register events into SMS notifications using a 4G cellular modem.
The gateway supports both Modbus RTU over RS485 and Modbus TCP over Ethernet, allowing it to integrate seamlessly into a wide range of industrial setups. In addition to communication and alerting, the system also provides a Wi-Fi-based configuration interface, making it possible to manage message settings without modifying firmware.
2. System Overview
At its core, the system acts as a central bridge between MODBUS communication interfaces, a GSM modem for SMS transmission, and a configuration interface accessible through Wi-Fi. The ESP32 continuously listens for changes in MODBUS registers and reacts to trigger events.
The same internal data is shared across both Modbus RTU and Modbus TCP interfaces, ensuring identical behavior regardless of which interface is used.

2.1 System Architecture
PLC / SCADA (Modbus Master)
            ↓
   Modbus TCP / RTU
            ↓					
     ESP32 Gateway
            ↓
        4G Modem
            ↓
        SMS Users


3. System Startup and Initialization Flow
When the system is powered on, the ESP32 performs a sequence of initialization steps. Understanding this flow helps in diagnosing issues and correctly interacting with the device.
The system begins by initializing internal resources such as shared memory and the file system. Once the file system is mounted, it checks for the presence of the configuration file (MBmapconf.csv). If the file does not exist, a default file is created with sample entries.
After configuration loading, the communication interfaces are initialized:
•	The Ethernet interface is started first. The system attempts to obtain an IP address using DHCP.
•	If DHCP fails or no gateway is assigned, the system automatically falls back to a static IP address: 192.168.8.200.
•	The Modbus TCP server is then started on port 502. 
•	The Modbus RTU interface is initialized over RS485 with predefined communication settings. 
At this stage, the system is ready to accept MODBUS communication. In parallel, the modem subsystem is prepared and FreeRTOS tasks are started. These tasks handle RTU communication, TCP communication, SMS processing, and access point control independently.
4. Access Point Mode and Configuration
The device includes a Wi-Fi Access Point (AP) mode for configuration. This mode is controlled using a physical button connected to the ESP32.
When the button is activated, the device starts an access point with the following credentials:
•	SSID: ESP32_FileServer 
•	Password: 12345678 
•	URL: http://192.168.4.1 
A user can connect WiFi to this network using a mobile phone or computer and open the web interface in a browser.
The web interface allows the user to:
•	Download the current configuration file, Ex.:
Msg. No.	Phone1	Phone2	Phone3	Phone4	Phone5	Message
1	9876543210	9123456780	8134850923	8991381344	9487138810	Pump ON Alert
•	Open the file using any .csv opener/editor (WPS Office Preferable), Edit the file with your desired Mobile number and Alert Text Messages and save.
•	Delete the default MBmapconf.csv file AND upload the modified configuration file.(Keep in mind the uploaded file name is exactly as “MBmapconf.csv”)
•	View the number of loaded message entries at the bottom of the Block as “Loaded message entries: <number of active Msg. no.>” on the web page.
Any changes made through this interface are applied immediately, as the system reloads the configuration after each update.
5. Connecting to the Device (MODBUS Usage)
Once the system is powered and initialized, it can be accessed using standard MODBUS tools or PLC systems.
5.1 Modbus TCP Connection
To connect over Ethernet:
•	Ensure the device is connected to the network 
•	Use the assigned IP address (DHCP or fallback 192.168.8.200) 
•	Connect to port 502

Any Modbus TCP client software (such as a PLC, SCADA system, or Modbus testing tool) can communicate with the device using standard Modbus function codes.




5.2 Modbus RTU Connection
To connect over RS485:
•	Use the configured baud rate (9600) 
•	Slave ID: 1
•	Standard 8N1 configuration
•	Note: Check Device manager before selecting the COMM PORT for RTU connection as “USB Serial port” under Ports (COM& LPT).
 


The device behaves as a Modbus slave and responds to register read/write requests.
5.3 Unified Behavior
Both Modbus TCP and RTU interfaces interact with the same internal register map. This means:
•	Writing from TCP reflects in RTU 
•	Writing from RTU reflects in TCP 
This shared-memory approach ensures consistent system behavior.
6. Working Principle
The system operates on a trigger-based mechanism. Each message is mapped to a MODBUS register. When a register is written with a value “1”, the system detects a transition and processes it as an event.
The system specifically detects a rising edge (0 → 1), ensuring that messages are not repeatedly sent while a register remains active.
Once detected, the request is queued and handled by the modem task. After processing, the result is written back to a corresponding register.

7. Register Architecture and Behavior
The MODBUS register map is divided into three logical sections: trigger registers, result registers, and input registers.

MODBUS Register MAP

1. Command Register MAP (USER INTERACTION POINT):
These are the primary interaction points for a MODBUS master. Writing a value 1 to a Command register initiates an SMS operation.
Example behavior:
•	Write 1 → triggers SMS 
•	Keep 1 → no repeated SMS 
•	Write 0 then 1 → triggers again 
Register
Type	Register Add	Data 
Type	Function 
Code	Scale
CMD_Reg-1	40001	INT16	03(Read/Write)	0/1
CMD_Reg-2	40002	INT16	03(Read/Write)	0/1
CMD_Reg-3	40003	INT16	03(Read/Write)	0/1
CMD_Reg-4	40004	INT16	03(Read/Write)	0/1
CMD_Reg-5	40005	INT16	03(Read/Write)	0/1
CMD_Reg-6	40006	INT16	03(Read/Write)	0/1
CMD_Reg-7	40007	INT16	03(Read/Write)	0/1
CMD_Reg-8	40008	INT16	03(Read/Write)	0/1
CMD_Reg-9	40009	INT16	03(Read/Write)	0/1
CMD_Reg-10	40010	INT16	03(Read/Write)	0/1
CMD_Reg-11	40011	INT16	03(Read/Write)	0/1
CMD_Reg-12	40012	INT16	03(Read/Write)	0/1
CMD_Reg-13	40013	INT16	03(Read/Write)	0/1
CMD_Reg-14	40014	INT16	03(Read/Write)	0/1
CMD_Reg-15	40015	INT16	03(Read/Write)	0/1
CMD_Reg-16	40016	INT16	03(Read/Write)	0/1
CMD_Reg-17	40017	INT16	03(Read/Write)	0/1
CMD_Reg-18	40018	INT16	03(Read/Write)	0/1
CMD_Reg-19	40019	INT16	03(Read/Write)	0/1
CMD_Reg-20	40020	INT16	03(Read/Write)	0/1
CMD_Reg-21	40021	INT16	03(Read/Write)	0/1
CMD_Reg-22	40022	INT16	03(Read/Write)	0/1
CMD_Reg-23	40023	INT16	03(Read/Write)	0/1
CMD_Reg-24	40024	INT16	03(Read/Write)	0/1
CMD_Reg-25	40025	INT16	03(Read/Write)	0/1
CMD_Reg-26	40026	INT16	03(Read/Write)	0/1
CMD_Reg-27	40027	INT16	03(Read/Write)	0/1
CMD_Reg-28	40028	INT16	03(Read/Write)	0/1
CMD_Reg-29	40029	INT16	03(Read/Write)	0/1
CMD_Reg-30	40030	INT16	03(Read/Write)	0/1
CMD_Reg-31	40031	INT16	03(Read/Write)	0/1
CMD_Reg-32	40032	INT16	03(Read/Write)	0/1
CMD_Reg-33	40033	INT16	03(Read/Write)	0/1
CMD_Reg-34	40034	INT16	03(Read/Write)	0/1
CMD_Reg-35	40035	INT16	03(Read/Write)	0/1
CMD_Reg-36	40036	INT16	03(Read/Write)	0/1
CMD_Reg-37	40037	INT16	03(Read/Write)	0/1
CMD_Reg-38	40038	INT16	03(Read/Write)	0/1
CMD_Reg-39	40039	INT16	03(Read/Write)	0/1
CMD_Reg-40	40040	INT16	03(Read/Write)	0/1
CMD_Reg-41	40041	INT16	03(Read/Write)	0/1
CMD_Reg-42	40042	INT16	03(Read/Write)	0/1
CMD_Reg-43	40043	INT16	03(Read/Write)	0/1
CMD_Reg-44	40044	INT16	03(Read/Write)	0/1
CMD_Reg-45	40045	INT16	03(Read/Write)	0/1
CMD_Reg-46	40046	INT16	03(Read/Write)	0/1
CMD_Reg-47	40047	INT16	03(Read/Write)	0/1
CMD_Reg-48	40048	INT16	03(Read/Write)	0/1
CMD_Reg-49	40049	INT16	03(Read/Write)	0/1
CMD_Reg-50	40050	INT16	03(Read/Write)	0/1
2. Status Register

Each Command register has a corresponding Status register where the outcome is stored.
•	0 → Idle 
•	Positive value → Number of SMS sent 
•	Negative value → Error 
2.1 Outcome Value and Meaning
Value	Meaning
0	No action yet
1–5	SMS sent successfully (count of recipients)
-1	SMS sending failed
-2	SIM error
-3	Network error
-4	Configuration error
-5	No valid recipients
-6	Modem or queue error
This allows external systems to confirm whether the operation succeeded or not.

2.2 Status Register MAP:
Register Type	Register Add	Data Type	Function Code	Scale
Status_Reg-51	40051	INT16	03(Read only)	-6 to 5
Status_Reg-52	40052	INT16	03(Read only)	-6 to 5
Status_Reg-53	40053	INT16	03(Read only)	-6 to 5
Status_Reg-54	40054	INT16	03(Read only)	-6 to 5
Status_Reg-55	40055	INT16	03(Read only)	-6 to 5
Status_Reg-56	40056	INT16	03(Read only)	-6 to 5
Status_Reg-57	40057	INT16	03(Read only)	-6 to 5
Status_Reg-58	40058	INT16	03(Read only)	-6 to 5
Status_Reg-59	40059	INT16	03(Read only)	-6 to 5
Status_Reg-60	40060	INT16	03(Read only)	-6 to 5
Status_Reg-61	40061	INT16	03(Read only)	-6 to 5
Status_Reg-62	40062	INT16	03(Read only)	-6 to 5
Status_Reg-63	40063	INT16	03(Read only)	-6 to 5
Status_Reg-64	40064	INT16	03(Read only)	-6 to 5
Status_Reg-65	40065	INT16	03(Read only)	-6 to 5
Status_Reg-66	40066	INT16	03(Read only)	-6 to 5
Status_Reg-67	40067	INT16	03(Read only)	-6 to 5
Status_Reg-68	40068	INT16	03(Read only)	-6 to 5
Status_Reg-69	40069	INT16	03(Read only)	-6 to 5
Status_Reg-70	40070	INT16	03(Read only)	-6 to 5
Status_Reg-71	40071	INT16	03(Read only)	-6 to 5
Status_Reg-72	40072	INT16	03(Read only)	-6 to 5
Status_Reg-73	40073	INT16	03(Read only)	-6 to 5
Status_Reg-74	40074	INT16	03(Read only)	-6 to 5
Status_Reg-75	40075	INT16	03(Read only)	-6 to 5
Status_Reg-76	40076	INT16	03(Read only)	-6 to 5
Status_Reg-77	40077	INT16	03(Read only)	-6 to 5
Status_Reg-78	40078	INT16	03(Read only)	-6 to 5
Status_Reg-79	40079	INT16	03(Read only)	-6 to 5
Status_Reg-80	40080	INT16	03(Read only)	-6 to 5
Status_Reg-81	40081	INT16	03(Read only)	-6 to 5
Status_Reg-82	40082	INT16	03(Read only)	-6 to 5
Status_Reg-83	40083	INT16	03(Read only)	-6 to 5
Status_Reg-84	40084	INT16	03(Read only)	-6 to 5
Status_Reg-85	40085	INT16	03(Read only)	-6 to 5
Status_Reg-86	40086	INT16	03(Read only)	-6 to 5
Status_Reg-87	40087	INT16	03(Read only)	-6 to 5
Status_Reg-88	40088	INT16	03(Read only)	-6 to 5
Status_Reg-89	40089	INT16	03(Read only)	-6 to 5
Status_Reg-90	40090	INT16	03(Read only)	-6 to 5
Status_Reg-91	40091	INT16	03(Read only)	-6 to 5
Status_Reg-92	40092	INT16	03(Read only)	-6 to 5
Status_Reg-93	40093	INT16	03(Read only)	-6 to 5
Status_Reg-94	40094	INT16	03(Read only)	-6 to 5
Status_Reg-95	40095	INT16	03(Read only)	-6 to 5
Status_Reg-96	40096	INT16	03(Read only)	-6 to 5
Status_Reg-97	40097	INT16	03(Read only)	-6 to 5
Status_Reg-98	40098	INT16	03(Read only)	-6 to 5
Status_Reg-99	40099	INT16	03(Read only)	-6 to 5
Status_Reg-100	40100	INT16	03(Read only)	-6 to 5

3. Input Registers (System Monitoring)

These registers provide real-time system status:
Register Type	Register Add	Data Type	Function Code	Scale
Device Status	30001	INT16	04(Read only)	
Modem Status	30002	INT16	04(Read only)	0/1
Sim Status	30003	INT16	04(Read only)	0/1
Network Status	30004	INT16	04(Read only)	0/1

Register	Description	Typical Use
30001	Device Status	Overall system health
30002	Modem Status	Modem ready/busy
30003	SIM Status	SIM detection/validity
30004	Network Status	Signal/network availability
These values help in diagnosing issues such as network failure or modem unavailability.
8. Message Configuration
The system uses a CSV file (MBmapconf.csv) to define SMS behavior.
Each row maps:
•	A trigger register index 
•	Up to five phone numbers 
•	A message text 
Example:
Msg. No.	Phone1	Phone2	Phone3	Phone4	Phone5	Message
1	9876543210	9123456780	8134850923	8991381344	9487138810	Pump ON Alert
When trigger register 0 is activated, this message is sent.
9. Software Architecture
The system is built using multiple FreeRTOS tasks that run concurrently and share a common memory layer. The RTU task handles serial communication, the TCP task manages Ethernet communication, the modem task processes SMS requests, and the AP task manages configuration access. The system uses queues and mutexes to ensure stable operation. SMS requests are processed sequentially, and shared data is protected against concurrent access issues.
The system continuously updates status registers, allowing external monitoring of its health.
10. Real-Time Operation
The use of FreeRTOS allows the system to handle multiple operations simultaneously. Communication tasks are prioritized to ensure reliable MODBUS interaction, while SMS processing and configuration tasks operate independently without blocking the system.
11. Hardware Components
The system includes an ESP32 microcontroller, W5500 Ethernet module, 4G modem, RS485 interface, and a control button for AP mode.
12. Limitations and Scope
The system supports a single Modbus TCP client and depends on network availability for SMS delivery. Proper trigger handling is required for correct operation.
13. Conclusion
The MODBUS SMS Gateway provides a reliable and flexible solution for integrating industrial systems with SMS-based alerting. Its design ensures consistent behavior across communication interfaces while providing an easy-to-use configuration mechanism.

