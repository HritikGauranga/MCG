# MSMSG Troubleshooting Guide

## Quick Reference: Error Codes

| Result Register Value | Error | Cause | Fix |
|---|---|---|---|
| **0** | STATUS_IDLE | Message not sent yet or cleared | Normal state, wait for trigger or check trigger write |
| **1-5** | SUCCESS (count) | SMS sent to N recipients | OK - Check SMS received on phones |
| **-1** | ERROR_SEND | SMS delivery failed | Check modem logs, network status, phone number format |
| **-2** | ERROR_SIM | SIM card not ready | Check SIM card, AT+CPIN? response |
| **-3** | ERROR_NETWORK | 4G network unavailable | Check signal, operator network, airplane mode |
| **-4** | ERROR_CONFIG | Message not configured | Upload MBmapconf.csv or configure in Web UI |
| **-5** | ERROR_EMPTY | No phone numbers in message | Add phone numbers to message config |
| **-6** | ERROR_MODEM | Modem not ready or queue full | Check modem init, power, queue depth (16 max) |

---

## STARTUP PHASE (First 20 seconds)

### Issue: Device boots but won't connect to Web UI

**Symptom**: Power LED on, but can't access `http://192.168.8.200` or `http://10.10.10.10`

**Steps**:
1. **Check Serial Monitor Output** (115200 baud)
   ```
   === MB Map RTOS SMS Controller ===
   [SYSTEM] Tasks started: SmsTask, RTUTask, TCPTask, ApTask
   [WEB] Config server started on port 80 (always active)
   ```
   - If missing: LittleFS mount failed or UART issue
   - If present: Device booted OK, proceed to network check

2. **Check Ethernet Cable**
   - Verify W5500 Ethernet module is connected
   - Look for TCP.cpp logs:
     ```
     [ETH] Starting Ethernet...
     [ETH] DHCP mode
     [ETH] IP: 192.168.8.200
     ```
   - If no IP shown: Check W5500 SPI pins (SCK:18, MISO:19, MOSI:23, CS:5)

3. **Check Modem Initialization** (blocking, takes ~15s)
   ```
   === Initializing 4G Modem (EC200U) ===
   [MODEM] Power ON triggered
   [MODEM] Modem initialized successfully
   ```
   - If blocked here: Core 0 stuck on modem init
   - If timeout: Core 1 Modbus tasks should run freely

4. **Test Web UI Access**
   - **Via Ethernet (recommended)**: Get IP from logs → `http://<IP>` in browser
   - **Via AP Mode**: 
     - Press GPIO 33 button (or GPIO 33 → GND)
     - Wait 2 seconds, Serial output should show: `[AP] AP Mode started`
     - Connect WiFi SSID `ESP32_FileServer` / password `12345678`
     - Access `http://10.10.10.10`

---

## MODBUS RTU CONNECTIVITY

### Issue: RTU Master can't communicate with ESP32

**Symptom**: No response to Modbus read/write commands from RTU master

**Diagnosis**:
1. **Check Hardware Connections**
   ```
   GPIO 16 (MODEM_RX) → RTU RX (or pull correct pin from logs)
   GPIO 17 (MODEM_TX) → RTU TX
   GND → RTU GND
   ```
   - **Note**: RTU uses Serial2 (pins 9/10 internally mapped to 16/17)

2. **Verify RTU Settings Match Configuration**
   - Serial settings in `gateway.conf`:
     ```
     baud_rate=9600
     data_bits=8
     parity=N
     stop_bits=1
     slave_id=1
     ```
   - Compare with RTU master settings
   - To change: Use Web UI → Gateway Configuration → Save & Reboot

3. **Check Serial Monitor for RTU Init Errors**
   - No logs from RTU? → Likely not initialized or wrong pins
   - Look in [RTU.cpp] for:
     ```cpp
     Serial2.begin(settings.baudRate, serialCfg, RXD2, TXD2);
     ```

4. **Test Register Access**
   - Read Input Register 0 (device status)
     - Should return `1` (STATE_READY) after boot
   - Write to Trigger Register 0 (slot 0)
     - Then read Result Register 50 (should populate with status)

5. **Common Serial Configuration Issues**
   | Setting | Common Values | Fix |
   |---|---|---|
   | Baud Rate Mismatch | 9600 vs 19200 | Use Web UI to match master |
   | Parity | N, E, O | Verify 7E1 / 8N1 / 8N2 combinations |
   | Stop Bits | 1 vs 2 | Check device datasheet |
   | Slave ID | 1 (default) | Match RTU master config |

---

## MODBUS TCP / ETHERNET CONNECTIVITY

### Issue: TCP Master can't reach Ethernet device

**Symptom**: Connection refused on port 502, or no response to Modbus TCP commands

**Diagnosis**:
1. **Test Network Connectivity**
   ```powershell
   ping 192.168.8.200
   ```
   - If timeout: Check Ethernet cable, W5500 module, SPI wiring

2. **Check Ethernet Initialization in Serial Logs**
   ```
   [ETH] Starting Ethernet...
   [ETH] DHCP mode
   [ETH] IP: 192.168.8.200
   [ETH] Subnet: 255.255.255.0
   [ETH] Gateway: 192.168.8.1
   [ETH] Modbus TCP Port: 502
   [ETH] Modbus TCP server ready
   ```
   - If missing "IP:" line: DHCP failed or static IP misconfigured

3. **DHCP Timeout (Falls back to Static IP)**
   - Check logs: `[ETH] DHCP timeout, switching to static IP fallback`
   - Static IP being used: `192.168.8.200` (default)
   - To change: Use Web UI → Gateway Configuration

4. **Test Port 502 Connectivity**
   ```powershell
   Test-NetConnection -ComputerName 192.168.8.200 -Port 502
   TcpTestSucceeded : True
   ```
   - If False: Firewall, subnet mismatch, or W5500 not working

5. **Check W5500 SPI Wiring**
   | Signal | ESP32 GPIO | Function |
   |---|---|---|
   | SCK | 18 | SPI Clock |
   | MISO | 19 | SPI In |
   | MOSI | 23 | SPI Out |
   | CS | 5 | Chip Select (W5500) |
   | RST | 14 | Reset (W5500) |

---

## SMS NOT SENDING (Result Register = -1, -2, -3, or -6)

### Issue: Trigger written but SMS doesn't arrive

**Error Code Diagnosis**:

#### `-2` (ERROR_SIM): SIM Not Ready
```
[SMS] SIM not ready
```
**Fix**:
1. Check SIM card inserted in correct orientation
2. Verify SIM not already in use on another device
3. Test manually via serial:
   - AT+CPIN? → Should return `+CPIN: READY`
4. Try different SIM card (carrier, plan)

#### `-3` (ERROR_NETWORK): No 4G Signal
```
[SMS] Network lost
```
**Fix**:
1. Check antenna connected to EC200U module
2. Move device to area with 4G coverage
3. Check `AT+CREG?` response should show:
   - `+CREG: 0,1` (registered, home network)
   - `+CREG: 0,5` (registered, roaming)
4. Verify not in airplane mode
5. Wait 30+ seconds after power-on (network registration takes time)

#### `-1` (ERROR_SEND): Delivery Failed
```
[SMS] Delivery failed
[SMS] No '>' prompt received
```
**Fixes**:
1. **Invalid Phone Number**
   - Check phone format: Must be 10-15 digits only (no +, spaces, dashes)
   - Update MBmapconf.csv with valid numbers
   - Web UI upload → MBmapconf.csv

2. **Modem Unresponsive Mid-Send**
   - Modem marked "not ready" after 2 consecutive failures
   - Device waits 12 seconds before retry: `[MODEM] Not ready - attempting reinit...`
   - Check EC200U power: MODEM_PWRKEY (GPIO 32) low for 1 second

3. **Message Too Long**
   - Max 512 characters per message
   - GSM encoding: Some special characters count as 2

#### `-6` (ERROR_MODEM): Modem Not Ready or Queue Full
```
[SMS] ERROR: Modem not ready
[MODEM] Queue full - job rejected
```
**Fix**:
1. **Modem Not Ready**
   - Check modem init logs at startup
   - If `[MODEM] Modem initialization failed`:
     - Check EC200U power connection
     - Check UART wiring: RX (GPIO 16), TX (GPIO 17)
     - Verify 115200 baud between ESP32 and EC200U

2. **Queue Full** (16 job slots)
   - Too many SMS triggered simultaneously
   - Reduce trigger rate or increase queue size (recompile)
   - Each SMS takes ~15 seconds to send

---

## CONFIGURATION ISSUES

### Issue: Message Config Won't Load

**Symptom**: Web UI upload succeeds but result register shows `-4` (ERROR_CONFIG)

**Diagnosis**:
1. **Check MBmapconf.csv Format**
   ```
   Msg.No., Phone number1, Phone number2, Phone number3, Phone number4, Phone number5, Text message
   1, 8149979689, 8655138978, , , , ALARM: Temperature HIGH!
   ```
   - Header line required
   - Exactly 6 commas per line
   - Message No: 1-50
   - Each phone: 10-15 digits only
   - Message text: not empty

2. **Verify File Upload via Web UI**
   - Go to `http://<IP>/` → Login (`admin`/`admin123`)
   - Click "Upload MBmapconf.csv"
   - Check response: `Loaded N message entries` ✓

3. **Test with Backup CSV**
   - Download current: `http://<IP>/api/download-csv/mbmapconf`
   - Download backup: `http://<IP>/api/download-csv/mbmapconf-backup`
   - Upload backup to test

4. **CSV Parse Error Handling**
   - Invalid lines are **skipped silently** (not rejected)
   - Check serial logs for parse errors during upload
   - Reload config manually: Web UI → Config reload button

5. **Manual Config via CSV**
   ```csv
   Msg.No., Phone1, Phone2, Phone3, Phone4, Phone5, Message
   1, 9876543210, 9876543211, , , , Test message 1
   2, 9876543212, , , , , Test message 2
   ```

### Issue: Gateway Settings Won't Save

**Symptom**: Web UI shows saved but IP doesn't change after reboot

**Fix**:
1. **Check Permissions**
   - Must be authenticated (login first)
   - Check: `Set-Cookie: MSMSG_AUTH=ok`

2. **Verify TCP Port Is Valid**
   - Must be 1-65535
   - Default: 502 (Modbus standard)

3. **Verify Slave ID Valid**
   - Must be 1-247
   - Default: 1

4. **Reboot Required**
   - Gateway settings only apply after device restart
   - Modbus RTU settings especially need reboot

---

## SYNCHRONIZATION & REGISTER ISSUES

### Issue: RTU and TCP Registers Out of Sync

**Symptom**: Same register shows different values when read from RTU vs TCP

**Root Cause**: Race condition between RTU and TCP tasks

**How It Works** (from Shared.cpp):
- `rtuLastSeenTriggers[]` - What RTU task last saw
- `tcpLastSeenTriggers[]` - What TCP task last saw
- Only write to shared if: `currentValue != lastSeenValue`

**Expected Behavior**:
- RTU writes to shared → TCP reads shared → TCP syncs back → result visible on both
- Takes ~5ms per sync cycle (task loop delay = 5ms)

**If Out of Sync**:
1. Wait 50-100ms (3-5 sync cycles)
2. If still mismatched, check stateMutex timeouts in logs
3. Force sync: Write same value twice to trigger register

### Issue: Lock Timeout Failures (Silent Fails)

**Symptom**: Register values not updating, but no error messages

**Problem**: `Shared_lockState()` timeout (default 50ms) causes silent failure

**Monitor For**:
- Check if tasks deadlocked: No more logs from RTU/TCP/Modem tasks
- Serial output frozen

**Prevention**:
- Timeouts prevent deadlock but operations fail silently
- Watch for pattern: Registers frozen for >5 seconds
- Add logging to detect mutex timeout failures

**Fix**:
- Reset device: Power cycle
- Check if multiple tasks acquiring mutex in wrong order
- Increase mutex timeout (requires code change)

---

## WEB UI LOGIN ISSUES

### Issue: Can't Access Web UI or Login Fails

**Symptom**: 401 Unauthorized or redirect loop

**Fixes**:
1. **Default Credentials** (if no serial number set)
   - Username: `admin`
   - Password: `admin123`

2. **Serial Number-Based Login** (if serial was set)
   - Username: `<ConfiguredUsername><SERIAL>`
   - Example: `AdminMCG001`
   - Password: Same (unless changed)

3. **Wrong Password**
   - Reset via LittleFS factory reset:
     - Erase flash: PlatformIO → Advanced → Erase Flash
     - Reupload firmware
   - Password stored in `/login_pass.txt`

4. **Cookie Issues**
   - Clear browser cookies
   - Try private/incognito window
   - Check: Cookie must have `MSMSG_AUTH=ok`

5. **Port 80 Blocked**
   - Try different computer on same network
   - Check firewall: Port 80 (HTTP)
   - Disable WiFi and use Ethernet directly

---

## AP MODE (CONFIG VIA WIFI)

### Issue: AP Mode Won't Start

**Symptom**: Button pressed but AP SSID not visible in WiFi list

**Diagnosis**:
1. **Check Button Press Detected**
   - Serial logs should show: `[AP] AP Mode started`
   - If not: Button GPIO 33 not connected correctly

2. **GPIO 33 Button Wiring**
   ```
   GPIO 33 → Button → GND
   (Press = LOW, No press = HIGH)
   Button debounce = 100ms
   ```

3. **Check WiFi SSID Name**
   - SSID: `ESP32_FileServer`
   - Password: `12345678`
   - AP IP: `10.10.10.10` (when active)

4. **Test AP Mode Access**
   - Connect to `ESP32_FileServer` WiFi
   - Open browser: `http://10.10.10.10`
   - Should show login page

### Issue: AP Mode but Can't Upload CSV

**Fix**:
1. Make sure authenticated (login first)
2. Check file size: Max 512KB for CSV
3. CSV format must match exactly (6 commas per line)
4. Check response in browser console for error message

---

## HARDWARE DIAGNOSTICS

### Power & Reset Issues

| Symptom | Likely Cause | Check |
|---|---|---|
| Device won't boot | No power to ESP32 | USB cable, 5V supply |
| Device resets constantly | Brownout, ESP32 reset loop | Power supply stability, capacitors |
| Modem won't init | EC200U not powered | GPIO 32 (MODEM_PWRKEY) wired correctly |
| W5500 not responding | SPI pins wrong or not connected | SCK:18, MISO:19, MOSI:23, CS:5, RST:14 |
| Button press ignored | GPIO 33 not pulled up | Wiring to GND through button, INPUT_PULLUP set |

### Serial Port Issues

| Symptom | Fix |
|---|---|
| Can't see serial output | Baud rate = 115200 |
| Serial output garbled | Baud rate = 115200 (check terminal settings) |
| UART2 (RTU) not working | GPIO 16/17 wired correctly, baud rate matches master |
| UART1 (Modem) not working | GPIO 16/17 for modem AT (shared with RTU on pins) |

---

## PERFORMANCE & TIMING

### Expected Timings

| Operation | Time | Notes |
|---|---|---|
| SMS sending per number | ~15 seconds | Network latency dominates |
| 5 SMS (5 numbers) | ~75 seconds | Sequential sending |
| Register read/write | ~1-10µs | Under mutex lock |
| RTU/TCP sync cycle | ~5ms | Task loop delay |
| Modbus response | 50-100ms | Per request from master |
| Modem reinit attempt | 12 seconds | After 2 consecutive failures |
| Modem startup init | ~15 seconds | Blocking on core 0 |

### Queue Management

- SMS queue size: 16 jobs
- If queue full: Returns `ERROR_MODEM (-6)`
- Enqueue time: <1µs (non-blocking)
- FIFO order: Jobs processed in order received

**Queue Full Scenario**:
```
PLC writes 16 triggers rapidly
→ All 16 enqueued instantly (1µs per)
→ Modem processes first one (~15s)
→ Write 17th trigger → ERROR_MODEM (-6)
→ After first completes (~15s), 17th can be queued
```

---

## DEBUGGING: SERIAL LOG EXAMPLES

### Good Boot Sequence
```
=== MB Map RTOS SMS Controller ===
[SYSTEM] Tasks started: SmsTask, RTUTask, TCPTask, ApTask

=== Initializing 4G Modem (EC200U) ===
[MODEM] Power ON triggered
[AT] >> AT
[AT] << OK
[AT] >> AT+CPIN?
[AT] << +CPIN: READY
[MODEM] Modem initialized successfully

[ETH] Starting Ethernet...
[ETH] DHCP mode
[ETH] IP: 192.168.8.200
[ETH] Modbus TCP server ready

[WEB] Config server started on port 80 (always active)

=== AP Mode Info ===
AP SSID: ESP32_FileServer
AP URL: http://10.10.10.10

[MBMAP] Loaded 6 message entries from MBmapconf.csv
```

### Good SMS Sending Sequence
```
[EDGE] Slot 0 rising edge detected → job enqueued
[SMS] Sending to 9876543210
[AT] >> AT+CMGS="9876543210"
[AT] << >
[SMS] CMGS result: +CMGS: 123
[SMS] SMS sent successfully (1/1)
[EDGE] Slot 0 cleared (trigger → 0)
```

### Modem Recovery Sequence
```
[SMS] No '>' prompt received - aborting
[SMS] Prompt missing once - keeping modem ready
[SMS] Sending to next number...
[MODEM] Not ready - reinit cooldown active
[MODEM] Not ready - attempting reinit...
[MODEM] Modem initialized successfully
```

---

## QUICK CHECKLIST

- [ ] Device boots (check serial 115200 output)
- [ ] Ethernet/WiFi IP assigned (check [ETH] logs)
- [ ] Web UI accessible (login with admin/admin123)
- [ ] MBmapconf.csv uploaded (shows message count in logs)
- [ ] RTU master connected and talking (check Modbus registers)
- [ ] TCP master connected (ping 192.168.8.200, port 502)
- [ ] Modem initialized (check [MODEM] logs)
- [ ] SIM card detected (AT+CPIN? = READY)
- [ ] 4G signal present (AT+CREG? = 0,1 or 0,5)
- [ ] Test SMS: Write to trigger register 0
- [ ] Check result register 50 for status code
- [ ] Verify SMS received on phone

---

## GETTING HELP

**Provide This Information**:
1. Full serial log (entire boot + error sequence)
2. Screenshot of Web UI dashboard
3. MBmapconf.csv content
4. gateway.conf settings
5. Result register values (all 50 slots)
6. Input registers (device/modem/SIM/network status)
7. Hardware description (what's connected where)

**Common Solutions**:
- 90%: Configuration, SIM, or network signal issues
- 8%: Wiring (SPI, serial, power)
- 2%: Firmware bugs

