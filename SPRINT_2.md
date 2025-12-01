# Sprint 2: Auradine Mining Chip Integration

## Sprint Goal
Add support for the Auradine mining chip to ESP-Miner with hardware configuration based on BitAxe Gamma 602, including optional W5500 Ethernet support, dual serial interfaces, enhanced I2C sensor support, and streamlined RPC-only operation.

## Overview
This sprint focuses on integrating the Auradine mining chip into the ESP-Miner firmware. The implementation follows the established architecture pattern used for BM1366/1368/1370 chips. Hardware will be based on the Gamma 602 board with EMC2103 temperature monitoring, TPS546 voltage regulation, and additional support for W5500 Ethernet and dual serial interfaces.

## Architecture Context

### Current ASIC Integration Pattern
- ASIC drivers located in `/components/asic/`
- Each ASIC has `.c` and `.h` files implementing standard interface
- Device configuration in `/main/device_config.h` defines board variants
- ASIC abstraction layer in `/components/asic/asic.c` routes calls to chip-specific drivers
- Serial communication via UART1 (TX: GPIO17, RX: GPIO18)
- I2C peripherals on GPIO47 (SDA) and GPIO48 (SCL)

### Hardware Configuration (Based on Gamma 602)
- **ASIC**: Auradine chip (1 chip configuration)
- **Temperature Sensor**: EMC2103 (dual channel, I2C address 0x4C)
- **Voltage Regulator**: TPS546 (PMBus, I2C address 0x24)
- **Power Monitor**: INA260 (I2C address 0x40) - assumed present
- **Serial**: BAM header (selectable between UART or W5500)
- **USB Serial**: Primary interface for configuration and control
- **Network**: W5500 Ethernet OR WiFi (mutually exclusive, configurable)

### New Features for This Sprint
1. **Dual Serial Support**: USB serial + BAM header serial (UART1)
2. **W5500 Ethernet**: Alternative to WiFi, connected via BAM header
3. **Enhanced I2C**: Support for additional sensors beyond standard config
4. **RPC-Only Operation**: Disable HTML/WebSocket interfaces
5. **Auradine ASIC**: New chip driver following BM1370 pattern

## Task Breakdown

### Phase 1: ASIC Driver Foundation (Priority 1)

#### Task 1.1: Create Auradine ASIC Driver Skeleton
**Files to Create:**
- `/components/asic/auradine.c`
- `/components/asic/include/auradine.h`

**Implementation Details:**
- Copy BM1370 driver structure as template
- Define Auradine-specific constants:
  - Chip ID (from datasheet)
  - Register addresses (from datasheet)
  - Command packet types
  - Debug flags (SERIALTX_DEBUG, SERIALRX_DEBUG, DEBUG_WORK, DEBUG_JOBS)
- Implement packet structures:
  - `auradine_asic_result_job_t` - nonce results from chip
  - `auradine_asic_result_cmd_t` - register read responses
  - `auradine_asic_result_t` - unified result packet
  - `auradine_job_packet` - work submission format
- Implement function stubs:
  - `uint8_t AURADINE_init(float frequency, uint16_t asic_count, uint16_t difficulty)`
  - `void AURADINE_send_work(void * GLOBAL_STATE, bm_job * next_bm_job)`
  - `task_result * AURADINE_process_work(void * GLOBAL_STATE)`
  - `void AURADINE_set_version_mask(uint32_t version_mask)`
  - `int AURADINE_set_max_baud(void)`
  - `int AURADINE_set_default_baud(void)`
  - `void AURADINE_send_hash_frequency(float frequency)`
  - `void AURADINE_read_registers(void)`
- Static helper functions:
  - `_send_AURADINE()` - packet transmission with CRC
  - `_send_chain_inactive()` - deactivate chip chain
  - `_set_chip_address()` - assign chip addresses

**Validation:**
- Code compiles without errors
- All function signatures match ASIC interface pattern
- Header file exports public API correctly

#### Task 1.2: Integrate Auradine Driver with ASIC Abstraction Layer
**Files to Modify:**
- `/components/asic/asic.c`
- `/components/asic/CMakeLists.txt`

**Implementation Details:**

In `asic.c`:
- Add `#include "auradine.h"` to headers
- Add `case AURADINE:` branches to all switch statements:
  - `ASIC_init()`
  - `ASIC_process_work()`
  - `ASIC_set_max_baud()`
  - `ASIC_send_work()`
  - `ASIC_set_version_mask()`
  - `ASIC_set_frequency()` - call `do_frequency_transition(frequency, AURADINE_send_hash_frequency)`
  - `ASIC_get_asic_job_frequency_ms()` - determine appropriate job interval
  - `ASIC_read_registers()`

In `CMakeLists.txt`:
- Add `"auradine.c"` to SRCS list

**Validation:**
- Build succeeds with Auradine driver included
- No compiler warnings for missing case labels

#### Task 1.3: Add Auradine to Device Configuration
**Files to Modify:**
- `/main/device_config.h`

**Implementation Details:**
- Add `AURADINE` to `Asic` enum (after BM1370)
- Define frequency options array:
  ```c
  static const uint16_t AURADINE_FREQUENCY_OPTIONS[] = {400, 500, 600, 700, 0};
  ```
  (Adjust values based on datasheet)
- Define voltage options array:
  ```c
  static const uint16_t AURADINE_VOLTAGE_OPTIONS[] = {1000, 1100, 1200, 1300, 0};
  ```
  (Adjust values based on datasheet)
- Create `ASIC_AURADINE` config structure:
  ```c
  static const AsicConfig ASIC_AURADINE = {
      .id = AURADINE,
      .name = "Auradine",
      .chip_id = 0xXXXX,  // From datasheet
      .default_frequency_mhz = 600,
      .frequency_options = AURADINE_FREQUENCY_OPTIONS,
      .default_voltage_mv = 1200,
      .voltage_options = AURADINE_VOLTAGE_OPTIONS,
      .difficulty = 256,
      .core_count = XXX,  // From datasheet
      .small_core_count = XXXX,  // From datasheet
      .hash_domains = 4,  // Verify from datasheet
      .hashrate_test_percentage_target = 0.85,
  };
  ```
- Add `ASIC_AURADINE` to `default_asic_configs[]` array
- Add `AURADINE_GAMMA` to `Family` enum
- Create `FAMILY_AURADINE_GAMMA` config:
  ```c
  static const FamilyConfig FAMILY_AURADINE_GAMMA = {
      .id = AURADINE_GAMMA,
      .name = "AuradineGamma",
      .asic = ASIC_AURADINE,
      .asic_count = 1,
      .max_power = 40,
      .power_offset = 5,
      .nominal_voltage = 5,
      .voltage_domains = 1,
      .swarm_color = "yellow",
  };
  ```
- Add `FAMILY_AURADINE_GAMMA` to `default_families[]` array
- Add board version "900" to `default_configs[]`:
  ```c
  { .board_version = "900",
    .family = FAMILY_AURADINE_GAMMA,
    .EMC2103 = true,
    .temp_offset = -10,
    .TPS546 = true,
    .INA260 = true,
    .power_consumption_target = 22,
  },
  ```

**Validation:**
- Configuration compiles
- Board version 900 is recognized
- Auradine ASIC enum value is valid

### Phase 2: Implementor's Guide (Priority 1)

#### Task 2.1: Create IMPLEMENTORS_AURADINE.md
**File to Create:**
- `/IMPLEMENTORS_AURADINE.md`

**Content Structure:**
1. **Overview**
   - Purpose of this guide
   - Auradine chip specifications (from datasheet)
   - Hardware requirements
   - Reference to main IMPLEMENTORS.md

2. **Auradine Chip Specifications**
   - Chip ID and detection
   - Communication protocol specifics
   - Register map
   - Initialization sequence
   - PLL configuration
   - Version rolling support (if applicable)
   - Nonce return format
   - Differences from BM1370

3. **Hardware Configuration**
   - Board version 900 specifications
   - EMC2103 temperature sensor configuration
   - TPS546 voltage regulator settings
   - Power budget and thermal limits
   - BAM header pinout and usage

4. **Implementation Checklist**
   - Step-by-step guide for completing the driver
   - Register values to configure (from datasheet)
   - Initialization sequence with exact commands
   - Work packet format
   - Result packet parsing
   - Frequency and voltage tuning

5. **Testing and Validation**
   - How to verify chip detection
   - How to validate communication
   - Expected hashrate calculations
   - Debugging tips specific to Auradine
   - Common issues and solutions

6. **Integration Notes**
   - Files modified in this sprint
   - Configuration options added
   - Future enhancement opportunities

**Validation:**
- Document is complete and well-structured
- All sections reference actual code locations
- Provides clear guidance for completing driver implementation

### Phase 3: W5500 Ethernet Support (Priority 2)

#### Task 3.1: Add ESP-Miner-Lan as Git Remote
**Commands to Execute:**
```bash
git remote add esp-miner-lan https://github.com/tbshfr/ESP-Miner-Lan.git
git fetch esp-miner-lan
```

**Validation:**
- Remote added successfully
- Can view remote branches

#### Task 3.2: Analyze W5500 Implementation
**Research Tasks:**
- Review ESP-Miner-Lan commits related to W5500
- Identify files added/modified for W5500 support:
  - W5500 driver component
  - Network initialization changes
  - Stratum connection modifications
  - Configuration options
- Document differences from mainline ESP-Miner
- Determine integration approach

**Deliverable:**
- Summary document of W5500 changes needed

#### Task 3.3: Create W5500 Component
**Files to Create/Modify:**
- `/components/w5500/` (new directory)
- `/components/w5500/CMakeLists.txt`
- `/components/w5500/w5500.c`
- `/components/w5500/include/w5500.h`
- SPI configuration for W5500 (BAM header pins)

**Implementation Details:**
- Port W5500 driver from ESP-Miner-Lan
- Adapt to BAM header SPI interface
- Implement initialization sequence
- Implement network configuration API
- Integrate with ESP-IDF networking stack

**Hardware Details:**
- W5500 connected via SPI to BAM header
- CS, MOSI, MISO, SCK pins (define in Kconfig)
- Interrupt pin for link status
- Reset pin control

**Validation:**
- W5500 component compiles
- SPI interface initializes correctly

#### Task 3.4: Add W5500 Configuration Options
**Files to Modify:**
- `/main/Kconfig.projbuild`
- `/main/device_config.h`

**Implementation Details:**

In `Kconfig.projbuild`:
- Add menu for network interface selection:
  ```
  choice NETWORK_INTERFACE
      prompt "Network Interface"
      default NETWORK_WIFI
      config NETWORK_WIFI
          bool "WiFi"
      config NETWORK_W5500
          bool "W5500 Ethernet"
      config NETWORK_SERIAL
          bool "Serial Only"
  endchoice
  ```
- Add W5500 SPI pin configuration
- Add W5500 network settings (static IP, DHCP, etc.)

In `device_config.h`:
- Add `w5500` boolean flag to `DeviceConfig`
- Update board version 900 config to support W5500

**Validation:**
- Configuration options appear in menuconfig
- Mutually exclusive network selection works

#### Task 3.5: Integrate W5500 with Network Stack
**Files to Modify:**
- `/main/main.c`
- `/components/stratum/stratum_api.c`
- `/components/connect/connect.c`

**Implementation Details:**
- Conditional compilation based on NETWORK_INTERFACE config
- Initialize W5500 instead of WiFi when selected
- Route Stratum connection through W5500 network interface
- Disable WiFi radio when W5500 is active
- Handle network events (link up/down, DHCP)

**Validation:**
- Stratum connection works via W5500
- Pool communication is stable
- Network interface switches correctly based on config

### Phase 4: Serial Interface Enhancements (Priority 3)

#### Task 4.1: Dual Serial Architecture Design
**Design Decisions:**
- **USB Serial** (UART0): Primary interface for control and configuration
  - Always enabled
  - RPC interface
  - Debug logging
- **BAM Header Serial** (UART1): Alternative use of BAM header
  - Mutually exclusive with W5500
  - Can be used for ASIC communication OR external device
  - Configurable via Kconfig

**Files to Analyze:**
- `/components/asic/serial.c` - Current UART1 usage
- `/main/bap/` - BAM Protocol implementation

**Deliverable:**
- Design document describing dual serial architecture
- Pin assignment table
- Configuration strategy

#### Task 4.2: BAM Header Configuration
**Files to Modify:**
- `/main/Kconfig.projbuild`
- `/main/device_config.h`

**Implementation Details:**
- Add configuration for BAM header mode:
  ```
  choice BAM_HEADER_MODE
      prompt "BAM Header Configuration"
      depends on !NETWORK_W5500
      default BAM_ASIC_SERIAL
      config BAM_ASIC_SERIAL
          bool "ASIC Serial Communication"
      config BAM_EXTERNAL_SERIAL
          bool "External Serial Device"
  endchoice
  ```
- Document pin mapping for different modes
- Ensure mutual exclusivity with W5500

**Validation:**
- Configuration is consistent
- No pin conflicts

#### Task 4.3: Implement Serial Mode Switching
**Files to Modify:**
- `/components/asic/serial.c`
- `/main/main.c`

**Implementation Details:**
- Initialize UART based on BAM header configuration
- Support dynamic switching (if needed)
- Ensure proper serial port cleanup when switching
- Handle baud rate configuration per mode

**Validation:**
- ASIC communication works on UART1
- Alternative serial devices can be connected
- No conflicts with USB serial

### Phase 5: Enhanced I2C Support (Priority 4)

#### Task 5.1: Extensible I2C Sensor Framework
**Files to Modify:**
- `/main/i2c_bitaxe.c`
- `/main/i2c_bitaxe.h`
- `/main/device_config.h`

**Implementation Details:**
- Add support for additional I2C sensors beyond standard config
- Create sensor detection and enumeration
- Add configuration for sensor addresses
- Support up to 8 I2C sensors total

**New Features:**
- Runtime I2C device scanning
- Dynamic sensor registration
- Sensor data aggregation
- Multiple temperature sensors

**Validation:**
- Additional sensors are detected
- Sensor data is read correctly
- No I2C bus conflicts

#### Task 5.2: EMC2103 Dual-Chip Temperature Monitoring
**Files to Verify/Enhance:**
- `/main/thermal/EMC2103.c`
- `/main/thermal/EMC2103.h`

**Implementation Details:**
- Verify EMC2103 driver supports dual external temp sensors
- Read both ASIC temperatures (if multi-chip board)
- Average or max temperature for control decisions
- Expose both temperatures via RPC

**Validation:**
- Both temperature channels read correctly
- Fan control responds to hottest sensor
- Temperature data available via RPC API

### Phase 6: WiFi Disabling (Priority 5)

#### Task 6.1: Conditional WiFi Compilation
**Files to Modify:**
- `/main/main.c`
- `/components/connect/connect.c`
- `/main/Kconfig.projbuild`

**Implementation Details:**
- Wrap WiFi initialization in `#ifdef NETWORK_WIFI`
- Disable WiFi radio when W5500 or Serial-only selected
- Reduce power consumption by turning off WiFi completely
- Remove WiFi dependencies from build when not needed

**Validation:**
- WiFi radio is off when W5500 selected
- Power consumption reduced (measure via INA260)
- No WiFi tasks running

#### Task 6.2: Network Fallback Strategy
**Design Decision:**
- If W5500 fails to initialize, should WiFi be available as fallback?
- Recommendation: No fallback, fail explicitly
- Rationale: Clear failure mode, forces hardware debugging

**Implementation:**
- Log clear error if selected network interface fails
- Do not silently fall back to alternative interface
- Provide diagnostic information via USB serial

**Validation:**
- Clear error messages on network init failure
- No unexpected fallback behavior

### Phase 7: HTML Interface Removal (Priority 6)

#### Task 7.1: Disable HTTP Server
**Files to Modify:**
- `/main/main.c`
- `/main/http_server/http_server.c`
- `/main/Kconfig.projbuild`

**Implementation Details:**
- Add `CONFIG_DISABLE_HTTP_SERVER` option
- Conditionally compile HTTP server code
- Remove HTTP server task from initialization
- Disable DNS server (used for captive portal)
- Keep minimal HTTP for firmware updates (optional)

**Validation:**
- HTTP server does not start
- No HTTP tasks in task list
- Memory footprint reduced

#### Task 7.2: Disable WebSocket Interface
**Files to Modify:**
- `/main/http_server/websocket.c`
- `/main/http_server/websocket.h`

**Implementation Details:**
- Conditionally compile WebSocket code
- Remove WebSocket task initialization
- Ensure RPC interface remains functional

**Validation:**
- No WebSocket connections possible
- RPC over serial still works

#### Task 7.3: RPC-Only Operation Verification
**Files to Verify:**
- `/main/bap/` - BAM Protocol (RPC interface)

**Validation Tasks:**
- Verify all configuration accessible via RPC
- Verify all monitoring data available via RPC
- Verify firmware update possible via RPC
- Test complete mining operation without web interface

**Deliverable:**
- Documentation of RPC-only operation
- List of all RPC commands and their usage

### Phase 8: Testing and Integration (Priority 7)

#### Task 8.1: Unit Tests
**Files to Create:**
- `/components/asic/test/test_auradine.c`

**Tests to Implement:**
- Packet construction and CRC calculation
- Register address mapping
- Job packet format validation
- Result packet parsing

**Validation:**
- All unit tests pass

#### Task 8.2: Hardware Bring-Up Checklist
**Create Document:**
- Step-by-step bring-up procedure
- Expected serial output at each stage
- Voltage measurements to verify
- Temperature sensor verification
- I2C device detection confirmation

#### Task 8.3: Integration Testing
**Test Scenarios:**
1. USB Serial + Auradine ASIC on UART1
2. USB Serial + W5500 Ethernet + Auradine on separate serial
3. Serial-only mode (no network)
4. Multiple I2C sensors
5. RPC-only operation (no HTTP/WebSocket)

**Validation:**
- All configurations work correctly
- No resource conflicts
- Stable mining operation

#### Task 8.4: Performance Validation
**Metrics to Measure:**
- Hashrate vs expected (from datasheet)
- Power consumption (via INA260)
- Temperature stability
- Network latency (W5500 vs WiFi comparison)
- Memory usage (with/without HTTP)

**Validation:**
- Performance meets specifications
- System is stable under load

## Dependencies

### External Dependencies
1. **Auradine Chip Datasheet** - Required for Tasks 1.1, 1.3, 2.1
   - Chip ID value
   - Register map
   - Initialization sequence
   - PLL configuration
   - Communication protocol

2. **ESP-Miner-Lan Repository** - Required for Task 3.1-3.5
   - W5500 driver code
   - Network integration examples

3. **Hardware** - Required for Task 8.2-8.4
   - Board version 900 with Auradine chip
   - W5500 Ethernet module
   - I2C sensors for testing
   - Power measurement equipment

### Internal Dependencies
- Task 1.2 depends on Task 1.1
- Task 1.3 depends on Task 1.1
- Task 2.1 depends on Task 1.1, 1.2, 1.3
- Task 3.2 depends on Task 3.1
- Task 3.3 depends on Task 3.2
- Task 3.4 depends on Task 3.3
- Task 3.5 depends on Task 3.4
- All Phase 8 tasks depend on completion of Phases 1-7

## Implementation Sequence

### Sprint Execution Order
1. **Phase 1** (Tasks 1.1-1.3): ASIC Driver Skeleton - establishes foundation
2. **Phase 2** (Task 2.1): Implementor's Guide - documents next steps with datasheet
3. **PAUSE**: User implements actual Auradine chip communication using datasheet
4. **Phase 3** (Tasks 3.1-3.5): W5500 Ethernet - adds networking option
5. **Phase 4** (Tasks 4.1-4.3): Serial Interfaces - BAM header flexibility
6. **Phase 5** (Tasks 5.1-5.2): I2C Sensors - enhanced monitoring
7. **Phase 6** (Tasks 6.1-6.2): WiFi Disabling - power optimization
8. **Phase 7** (Tasks 7.1-7.3): HTML Removal - RPC-only operation
9. **Phase 8** (Tasks 8.1-8.4): Testing - validation and bring-up

### Immediate Sprint Focus
This sprint will complete **Phases 1 and 2 only**:
- Create Auradine driver skeleton
- Integrate with build system and device configuration
- Write comprehensive implementor's guide
- Stop and wait for user to implement chip communication with datasheet

Phases 3-8 are documented for future sprints but will not be executed in Sprint 2.

## Files to Create

### New Files
1. `/components/asic/auradine.c` - Auradine ASIC driver implementation
2. `/components/asic/include/auradine.h` - Auradine ASIC driver header
3. `/IMPLEMENTORS_AURADINE.md` - Auradine-specific implementation guide
4. `/components/w5500/` - W5500 component (future)
5. `/components/asic/test/test_auradine.c` - Unit tests (future)

### Files to Modify
1. `/components/asic/asic.c` - Add Auradine to abstraction layer
2. `/components/asic/CMakeLists.txt` - Add auradine.c to build
3. `/main/device_config.h` - Add Auradine ASIC and board configs
4. `/main/Kconfig.projbuild` - Add configuration options (future phases)
5. `/main/main.c` - Network interface selection (future phases)
6. `/components/stratum/stratum_api.c` - W5500 integration (future)
7. `/main/i2c_bitaxe.c` - Enhanced I2C support (future)
8. `/main/thermal/EMC2103.c` - Dual sensor support (future)

## Configuration Options to Add

### Sprint 2 (Phase 1-2)
- None (uses existing Gamma 602 configuration)

### Future Sprints (Phase 3-7)
```kconfig
# Network Interface Selection
choice NETWORK_INTERFACE
    prompt "Network Interface"
    default NETWORK_WIFI
    config NETWORK_WIFI
        bool "WiFi"
    config NETWORK_W5500
        bool "W5500 Ethernet"
    config NETWORK_SERIAL
        bool "Serial Only"
endchoice

# BAM Header Configuration
choice BAM_HEADER_MODE
    prompt "BAM Header Configuration"
    depends on !NETWORK_W5500
    default BAM_ASIC_SERIAL
    config BAM_ASIC_SERIAL
        bool "ASIC Serial Communication"
    config BAM_EXTERNAL_SERIAL
        bool "External Serial Device"
endchoice

# HTTP Server
config DISABLE_HTTP_SERVER
    bool "Disable HTTP/WebSocket Server"
    default n
    help
        Disable the HTTP and WebSocket server for RPC-only operation.
        Reduces memory usage and attack surface.

# W5500 Configuration (when enabled)
config W5500_SPI_HOST
    int "SPI Host"
    depends on NETWORK_W5500
    default 2

config W5500_CS_GPIO
    int "CS GPIO"
    depends on NETWORK_W5500
    default 10

# Additional I2C Sensors
config I2C_SCAN_ENABLED
    bool "Enable I2C Device Scanning"
    default y
    help
        Scan I2C bus at startup and detect all connected devices.
```

## Success Criteria

### Sprint 2 (Phase 1-2) Completion
- [ ] Auradine driver skeleton compiles without errors
- [ ] Driver integrates with ASIC abstraction layer
- [ ] Board version 900 configuration exists
- [ ] Auradine enum values compile correctly
- [ ] IMPLEMENTORS_AURADINE.md is comprehensive and detailed
- [ ] All function stubs are present and match interface
- [ ] Build system includes auradine.c
- [ ] Code follows existing patterns (BM1370 reference)

### Future Sprint Completion (Phase 3-8)
- [ ] W5500 Ethernet connection established
- [ ] Stratum mining works via W5500
- [ ] BAM header configurable for serial or W5500
- [ ] Additional I2C sensors detected and read
- [ ] WiFi disabled when not selected
- [ ] HTTP/WebSocket disabled, RPC functional
- [ ] All test scenarios pass
- [ ] Hashrate meets specifications
- [ ] System stable for 24+ hours

## Known Challenges and Risks

### Sprint 2 Risks
1. **Auradine Datasheet Availability**: Driver skeleton is generic; actual implementation requires complete datasheet
   - Mitigation: Create comprehensive implementor's guide to document requirements

2. **BM1370 Pattern Compatibility**: Auradine chip may differ significantly from BM1370
   - Mitigation: Skeleton is flexible; user will adapt with datasheet

### Future Sprint Risks
3. **W5500 Integration Complexity**: ESP-Miner-Lan may have diverged from mainline
   - Mitigation: Thorough analysis phase before integration

4. **Pin Conflicts**: BAM header shared between serial and W5500
   - Mitigation: Clear configuration and runtime checks

5. **I2C Address Conflicts**: Additional sensors may conflict with existing devices
   - Mitigation: I2C scanning and conflict detection

6. **Performance Impact**: RPC-only operation may lack monitoring capabilities
   - Mitigation: Ensure RPC API is comprehensive

## Notes

### Development Environment
- ESP-IDF version: (check sdkconfig for version)
- Target: ESP32-S3
- Build system: CMake
- Already in worktree: `/home/mcelrath/Projects/ESP-Miner/.worktrees/auradine`
- Git branch: `auradine`

### Code Style Guidelines
- Follow existing ESP-Miner patterns
- Use ESP_LOG for all logging
- CamelCase for types, snake_case for functions
- Static functions prefixed with underscore
- Comprehensive comments for complex logic
- Include debug flags for serial debugging

### Testing Strategy
- Unit tests for packet generation/parsing
- Hardware tests with actual Auradine chip
- Integration tests for each configuration mode
- Performance benchmarking against specifications
- Long-duration stability testing (24h+)

## References

- Main Implementor's Guide: `/IMPLEMENTORS.md`
- BM1370 Reference Driver: `/components/asic/bm1370.c`
- Device Config Reference: `/main/device_config.h`
- Gamma 602 Config: `config-602.cvs`
- ESP-Miner-Lan Repo: https://github.com/tbshfr/ESP-Miner-Lan
- ESP-IDF Documentation: https://docs.espressif.com/projects/esp-idf/

## Sprint Review Notes

This sprint plan is intentionally comprehensive to document the full scope of Auradine integration. However, **Sprint 2 execution will stop after Phase 2** (Tasks 1.1-2.1) to allow the user to implement the actual chip communication using the datasheet. Phases 3-8 are documented for planning purposes and will be executed in future sprints.
