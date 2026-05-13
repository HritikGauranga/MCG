# MSMSG Gateway Test Procedure

## 1. Purpose
This procedure validates that firmware, communications, UI, and failover behavior are production-ready for the MSMSG gateway (ESP32 + W5500 + Modbus RTU/TCP + 4G SMS modem).

## 2. Scope
This test plan covers:
- Boot and task startup
- Ethernet (Static and DHCP)
- Modbus TCP server behavior
- Modbus RTU server behavior
- RTU/TCP shared register synchronization
- AP mode and Web configuration UI
- CSV configuration handling
- Serial number and authentication flows
- SMS trigger flow and modem state handling
- Error handling (SIM missing, network loss, link loss)
- Long-run stability and recovery

## 3. Required Test Setup
- DUT: MSMSG hardware flashed with release candidate firmware
- 24V/5V/3.3V supply per hardware design
- Ethernet switch/router with DHCP server enabled
- PC-1 with:
  - Modbus Poll (or equivalent Modbus TCP/RTU client)
  - Serial monitor (115200)
  - Web browser
- RS485 USB converter + Modbus RTU master tool
- Valid SIM card (for positive SMS tests)
- No-SIM condition (for negative SIM tests)
- At least one reachable mobile number for SMS delivery verification

## 4. Pre-Test Checklist
- Confirm firmware version/build recorded
- Confirm wiring:
  - W5500: `SCK=18`, `MISO=19`, `MOSI=23`, `CS=5`, `RST=14`
  - Modem UART: `RX=16`, `TX=17`, `PWRKEY=32`
  - AP button: `GPIO33`
  - AP status LED: `GPIO2` (ON when AP mode active)
- Confirm `MBmapconf.csv` exists or is uploaded
- Confirm gateway settings known (DHCP/static IP, TCP port, RTU params)
- Factory reset / clean power cycle performed

## 5. Register Map Reference
- Holding Registers `0..49`: trigger registers
- Holding Registers `50..99`: result registers
- Input Registers:
  - `0`: device status
  - `1`: modem status
  - `2`: SIM status
  - `3`: network status

Status expectations:
- Runtime state: `1=READY`, `2=BUSY`, `-1=ERROR`, `0=IDLE`
- Result status examples:
  - `0` idle
  - `-1` send failure
  - `-2` SIM error
  - `-3` network error
  - `-4` config error
  - `-5` empty config
  - `-6` modem error

## 6. Test Cases

### A. Boot and Basic Health

**TC-A01: Cold boot startup**
- Steps:
  1. Power off DUT for 10s.
  2. Power on DUT.
  3. Observe serial logs for startup sequence.
- Expected:
  - System banner prints.
  - Tasks start (`SmsTask`, `RTUTask`, `TCPTask`, `ApTask`).
  - No reboot loop / panic.
- Pass/Fail: System reaches steady operation within 60s.

**TC-A02: Warm reboot**
- Steps: Press reset/reboot.
- Expected: Same as TC-A01.

### B. Ethernet and IP Behavior

**TC-B01: Static IP mode startup**
- Precondition: DHCP unchecked in gateway settings.
- Steps: Reboot DUT.
- Expected:
  - Ethernet starts with configured static IP.
  - Modbus TCP endpoint reachable.
  - No continuous W5500 error flood.

**TC-B02: DHCP mode startup**
- Precondition: DHCP checked.
- Steps: Reboot DUT with DHCP server active.
- Expected:
  - DUT obtains DHCP lease.
  - IP shown in UI matches router lease table.

**TC-B03: DHCP timeout fallback to static**
- Precondition: DHCP checked.
- Steps:
  1. Disable DHCP server.
  2. Reboot DUT.
- Expected:
  - DHCP attempt fails/timeouts.
  - DUT falls back to static IP.
  - Modbus TCP still reachable.

**TC-B04: DHCP re-acquire from static fallback**
- Precondition: DUT in static fallback because DHCP server was down.
- Steps:
  1. Re-enable DHCP server.
  2. Wait 30-90s.
- Expected:
  - DUT switches to DHCP lease.
  - IP changes from fallback static IP to DHCP assigned IP.
  - TCP communication remains operational.

**TC-B05: Link loss and recovery**
- Steps:
  1. While connected, unplug Ethernet cable for 15s.
  2. Replug cable.
- Expected:
  - Link loss detected.
  - Recovery path runs.
  - Modbus TCP reconnects without power cycle.

### C. Modbus TCP Functionality

**TC-C01: TCP connect/disconnect**
- Steps:
  1. Connect Modbus Poll to DUT IP and configured TCP port.
  2. Disconnect/reconnect 5 times.
- Expected:
  - Connections accepted consistently.
  - No lockup or crash.

**TC-C02: Read holding/input registers**
- Steps: Read holding `0..99` and input `0..3`.
- Expected:
  - Values return consistently.
  - Status registers reflect real system state.

**TC-C03: Trigger write and result update**
- Steps:
  1. Write `1` to trigger register `0`.
  2. Observe result register `50`.
  3. Write trigger `0` again.
- Expected:
  - Slot processes once per rising edge.
  - Result updates.
  - Result clears to idle after trigger returns to `0`.

### D. Modbus RTU Functionality

**TC-D01: RTU connectivity**
- Precondition: RTU params match tool config (baud/parity/data/stop/slave ID).
- Steps: Connect RTU master and read holding/input registers.
- Expected: Stable RTU communication, no framing errors.

**TC-D02: RTU trigger and clear**
- Steps:
  1. Write `1` to trigger register `0` via RTU.
  2. Observe result.
  3. Write trigger back to `0`.
- Expected: Same behavior as TCP trigger path.

### E. RTU/TCP Synchronization

**TC-E01: RTU write reflected on TCP**
- Steps:
  1. Write trigger via RTU.
  2. Read same register via TCP.
- Expected: TCP sees updated value.

**TC-E02: TCP write reflected on RTU**
- Steps:
  1. Write trigger via TCP.
  2. Read same register via RTU.
- Expected: RTU sees updated value.

**TC-E03: No clobber race**
- Steps:
  1. Alternate RTU/TCP writes on different slots quickly.
  2. Observe final values from both interfaces.
- Expected: No random overwrite by mirror sync.

### F. AP Mode and Web UI

**TC-F01: AP mode ON/OFF via button**
- Steps:
  1. Press GPIO33 to enable AP mode.
  2. Release/disable AP mode.
- Expected:
  - AP status LED turns ON when AP is enabled.
  - AP SSID appears (`MSys` or `MSys-<Serial>`).
  - AP IP is `10.10.10.10` when active.
  - AP status LED turns OFF when AP is disabled.
  - AP stops cleanly when disabled.

**TC-F02: Web login**
- Steps:
  1. Open web UI.
  2. Verify unauthenticated redirect to `/login`.
  3. Login with configured credentials.
- Expected: Auth enforced, dashboard accessible after login.

**TC-F03: Logout**
- Steps: Trigger logout and revisit dashboard URL.
- Expected: Session cookie cleared, redirected to login.

**TC-F04: Dashboard values**
- Steps: Compare dashboard values with actual system config/state.
- Expected: Endpoint/IP/mode/RTU/TCP fields are accurate.

### G. Gateway Settings UI

**TC-G01: Save valid settings**
- Steps: Change TCP port, RTU params, DHCP/static fields, save.
- Expected:
  - Save success response.
  - Settings persist after reboot.

**TC-G02: Input validation**
- Steps: Submit invalid IP/port/slave ID/parity combinations.
- Expected: Request rejected with error, previous config retained.

### H. CSV Config Management

**TC-H01: Upload valid CSV**
- Steps: Upload valid `MBmapconf.csv`.
- Expected:
  - Upload success.
  - Loaded entry count updates.
  - Table reflects uploaded data.

**TC-H02: Download CSV and backup**
- Steps: Download both active and backup CSV.
- Expected: Files download and content matches expectations.

**TC-H03: Delete active CSV**
- Steps: Delete active CSV from UI.
- Expected: File removed, table clears, no crash.

**TC-H04: Invalid CSV format**
- Steps: Upload malformed CSV.
- Expected: Graceful failure, no reboot, existing config not corrupted.

### I. Serial Number Workflow

**TC-I01: First-time serial number set**
- Steps: Set serial number from `/serialnumber/`.
- Expected: Save succeeds and serial appears in AP/dashboard.

**TC-I02: Lock behavior**
- Steps: Attempt overwrite according to lock rules for same build.
- Expected: Lock behavior enforced per firmware design.

### J. Modem/SMS Positive and Negative Flows

**TC-J01: Modem init with valid SIM**
- Precondition: SIM inserted, network available.
- Steps: Reboot and observe init.
- Expected:
  - `AT` responds.
  - SIM ready.
  - Network registered.
  - Modem status input register shows ready.

**TC-J02: SMS send success**
- Steps:
  1. Configure one valid phone/message slot.
  2. Trigger slot (rising edge).
- Expected:
  - SMS received on phone.
  - Result register shows positive sent-count.

**TC-J03: SIM missing behavior**
- Precondition: Remove SIM.
- Steps:
  1. Reboot and trigger any slot.
  2. Monitor logs and Modbus availability.
- Expected:
  - SIM error state reported.
  - Slot result reports SIM/modem error.
  - No aggressive modem reinit loop causing Ethernet collapse.
  - RTU/TCP remain responsive.

**TC-J04: Invalid phone handling**
- Steps: Put invalid numbers in CSV and trigger.
- Expected: Invalid numbers skipped, no crash, proper result/error.

### K. Stability and Soak

**TC-K01: 2-hour idle soak**
- Steps: Leave system running with Ethernet connected and no triggers.
- Expected: No reboot, no continuous error floods, services reachable.

**TC-K02: 2-hour mixed traffic soak**
- Steps:
  1. Poll Modbus TCP continuously.
  2. Poll Modbus RTU continuously.
  3. Trigger random slots every 30-60s.
- Expected:
  - No deadlock, reset, or memory exhaustion.
  - Register coherence maintained.

**TC-K03: AP toggle under load**
- Steps: While TCP/RTU polling is active, toggle AP mode repeatedly.
- Expected: No crash; comms recover quickly after each toggle.

### L. Power and Recovery

**TC-L01: Power interruption**
- Steps:
  1. Run normal traffic.
  2. Remove and restore power.
- Expected:
  - Clean restart.
  - Config retained.
  - Services recover automatically.

## 7. Pass/Fail Criteria
- **Pass:** All critical tests (A, B, C, D, E, J, K, L) pass with no crash/reboot and no persistent communication outage.
- **Conditional Pass:** Minor non-blocking UI issue only, with no data loss/communication failure.
- **Fail:** Any reproducible crash, Modbus unavailability, corrupted settings, or incorrect SMS trigger behavior.

## 8. Defect Reporting Template
For each failure record:
- Test Case ID
- Firmware build tag/date
- Hardware serial number
- Preconditions
- Exact steps
- Expected result
- Actual result
- Serial log snippet
- Screenshots (UI/Modbus tool)
- Reproducibility (`Always`, `Intermittent`, `%`)

## 9. Execution Record (Tester to Fill)
- Tester name:
- Date:
- DUT serial:
- Firmware build:
- Total cases executed:
- Passed:
- Failed:
- Blocked:
- Final recommendation: `Release / Re-test required`
