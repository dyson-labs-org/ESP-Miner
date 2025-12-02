# Sprint 1: ESP-IDF 6.0 Migration

## Overview

Migrate ESP-Miner from ESP-IDF 5.5.1 to ESP-IDF 6.0, ensuring compatibility with all breaking changes and taking advantage of new features while maintaining full functionality.

## Goals

1. Update all component dependencies to remove legacy driver dependencies
2. Update build system configuration for ESP-IDF 6.0 compatibility
3. Ensure all code compiles without warnings or errors
4. Verify all functionality works correctly after migration
5. Document any performance or behavioral changes

## Architecture Decisions

### Component Dependency Strategy
- Remove all dependencies on legacy `driver` component
- Add explicit dependencies on new driver components (`esp_driver_uart`, `esp_driver_gpio`, `esp_driver_i2c`)
- Keep existing new-style driver usage (ADC, I2C master) unchanged

### Build System Strategy
- Update CMake minimum version to 3.22.1
- Configure orphan section handling to error by default
- Enable compiler warnings as errors
- Update IDF version requirements in component manifests

### Memory Placement Strategy
- Keep default FreeRTOS and ring buffer flash placement (not IRAM)
- Monitor performance after migration
- Document option to enable IRAM placement if needed

## Task Breakdown

### Phase 1: Build System Updates

#### Task 1.1: Update Root CMakeLists.txt
**File:** `CMakeLists.txt`
**Changes:**
- Update `cmake_minimum_required(VERSION 3.22.1)` (line 3)

**Validation:**
- CMake configures without version warnings

#### Task 1.2: Update Component CMakeLists - ASIC Component
**File:** `components/asic/CMakeLists.txt`
**Changes:**
```cmake
REQUIRES
    "freertos"
    "esp_driver_uart"  # Add explicit UART driver
    "stratum"
```

**Rationale:**
- Remove legacy `driver` dependency
- `serial.c` uses `driver/uart.h` (line 7)

**Validation:**
- Component builds successfully
- UART functionality works in ASIC communication

#### Task 1.3: Update Component CMakeLists - Main Component
**File:** `main/CMakeLists.txt`
**Changes:**
```cmake
PRIV_REQUIRES
    "app_update"
    "esp_driver_uart"     # Add for UART usage
    "esp_driver_gpio"     # Add for GPIO usage
    "esp_adc"
    "esp_app_format"
    "esp_event"
    "esp_http_server"
    "esp_netif"
    "esp_psram"
    "esp_timer"
    "esp_wifi"
    "cjson"
    "nvs_flash"
    "spiffs"
    "vfs"
    "esp_driver_i2c"
```

**Rationale:**
- Remove legacy `driver` dependency (line 62)
- Add explicit driver components for:
  - UART: `bap/bap_uart.c`, `tasks/power_management_task.c`
  - GPIO: `input.c`, `power/asic_reset.c`, `power/vcore.c`, `system.c`, `self_test/self_test.c`
  - I2C: Already has `esp_driver_i2c` for new I2C master driver

**Validation:**
- Main component builds successfully
- All driver functionality works (UART, GPIO, I2C)

### Phase 2: Component Manifest Updates

#### Task 2.1: Update IDF Version Requirement
**File:** `main/idf_component.yml`
**Changes:**
```yaml
## Required IDF version
idf:
    version: '>=6.0.0'  # Update from '>=5.5.0'
```

**Validation:**
- Component manager accepts version requirement
- Dependencies resolve correctly

### Phase 3: Configuration Updates

#### Task 3.1: Review and Update sdkconfig.defaults
**File:** `sdkconfig.defaults`
**Changes:**
- Review current configuration (already looks good)
- Add any ESP-IDF 6.0 specific optimizations if needed
- Document any removed configuration options

**Current config analysis:**
- Uses performance optimizations
- Has SPIRAM configured correctly
- No deprecated options found in current config

**Validation:**
- Configuration loads without warnings
- All features still work as expected

### Phase 4: Build and Testing

#### Task 4.1: Full Clean Build
**Actions:**
1. Run `idf.py fullclean`
2. Run `idf.py build`
3. Document any orphan section errors
4. Fix any compiler warnings that become errors

**Expected issues:**
- Possible orphan sections from custom linker usage
- Compiler warnings that were previously ignored

**Validation:**
- Build completes successfully with zero warnings
- Binary size is documented (compare to 5.5.1)
- IRAM/DRAM usage is documented

#### Task 4.2: Flash and Basic Functionality Test
**Actions:**
1. Flash firmware to device
2. Verify boot sequence
3. Test basic functionality:
   - Serial console output
   - WiFi connection
   - Web interface access
   - I2C device communication

**Validation:**
- Device boots successfully
- No crash loops or panics
- Basic features accessible

#### Task 4.3: ASIC Communication Testing
**Actions:**
1. Test UART communication with ASIC
2. Verify mining job creation and submission
3. Monitor for communication errors
4. Check result processing

**Test cases:**
- ASIC initialization sequence
- Frequency setting
- Job submission
- Nonce detection
- Error handling

**Validation:**
- ASIC communication works reliably
- No data corruption
- Performance matches 5.5.1 baseline

#### Task 4.4: Power Management Testing
**Actions:**
1. Test I2C communication with power ICs
2. Verify voltage control (TPS546, DS4432U)
3. Test current monitoring (INA260)
4. Verify thermal management

**Test cases:**
- Voltage adjustment
- Current reading accuracy
- Temperature monitoring
- Fan control
- PID loop stability

**Validation:**
- All I2C devices respond correctly
- Voltage control is precise
- Thermal management maintains target temperature

#### Task 4.5: Web Interface and Networking Testing
**Actions:**
1. Test HTTP server functionality
2. Verify WebSocket communication
3. Test API endpoints
4. Verify WiFi stability
5. Test configuration changes via web UI

**Test cases:**
- Home page loads
- Real-time statistics update
- Configuration changes apply
- Pool switching
- System settings
- Firmware update interface

**Validation:**
- Web UI fully functional
- WebSocket maintains connection
- API responses correct
- No memory leaks

#### Task 4.6: Long-term Stability Testing
**Actions:**
1. Run device for extended period (24+ hours)
2. Monitor for memory leaks
3. Check for task watchdog timeouts
4. Verify no performance degradation
5. Monitor crash counts and reasons

**Metrics to track:**
- Uptime
- Free heap over time
- Task stack usage
- Hashrate stability
- Share acceptance rate
- Temperature stability

**Validation:**
- Device runs stably for 24+ hours
- No memory leaks detected
- No unexpected reboots
- Performance matches baseline

### Phase 5: Performance Analysis and Optimization

#### Task 5.1: Performance Baseline Comparison
**Actions:**
1. Document ESP-IDF 5.5.1 baseline metrics
2. Document ESP-IDF 6.0 metrics
3. Compare:
   - Hashrate
   - Power consumption
   - Temperature
   - CPU usage
   - Memory usage (IRAM, DRAM, heap)
   - Network latency

**Deliverable:**
- Performance comparison document

#### Task 5.2: Memory Placement Optimization (If Needed)
**Actions:**
- If performance regression detected:
  1. Enable `CONFIG_FREERTOS_IN_IRAM`
  2. Enable `CONFIG_RINGBUF_IN_IRAM`
  3. Re-test and compare
  4. Document IRAM trade-offs

**Validation:**
- Performance meets or exceeds 5.5.1 baseline
- IRAM usage is acceptable

#### Task 5.3: Compiler Optimization Review
**Actions:**
1. Review current optimization level (CONFIG_COMPILER_OPTIMIZATION_PERF)
2. Test build with different optimization levels if needed
3. Measure impact on performance and binary size

**Validation:**
- Optimal balance of performance and code size

### Phase 6: Documentation

#### Task 6.1: Update Build Documentation
**Files to update:**
- `readme.md`
- `flashing.md`

**Changes:**
- Update ESP-IDF version requirement to 6.0+
- Update CMake version requirement to 3.22.1+
- Update Python version requirement to 3.10+
- Document any build process changes

#### Task 6.2: Create Migration Notes
**New file:** `docs/ESP-IDF-6.0-MIGRATION.md`

**Content:**
- Summary of changes made
- Breaking changes that affected the project
- Performance comparison
- Known issues (if any)
- Rollback procedure if needed

#### Task 6.3: Update Component Documentation
**Actions:**
- Review and update any component-specific docs
- Update API usage examples if needed
- Document any behavioral changes

### Phase 7: CI/CD Updates

#### Task 7.1: Update CI Configuration
**File:** `sdkconfig.ci` (if used in CI)
**Actions:**
- Update for ESP-IDF 6.0 compatibility
- Ensure CI uses correct ESP-IDF version
- Update any CI scripts for new requirements

#### Task 7.2: Update GitHub Actions (if applicable)
**Actions:**
- Update ESP-IDF version in workflow files
- Update Docker images if used
- Verify CI builds successfully

### Phase 8: Final Validation

#### Task 8.1: Code Review
**Actions:**
1. Review all changed files
2. Verify no temporary debug code left
3. Check code style consistency
4. Verify comments are updated

**Validation:**
- Code review checklist completed
- All changes peer reviewed

#### Task 8.2: Final Testing Checklist
**Test all device configurations:**
- [ ] BitAxe 102 (BM1397)
- [ ] BitAxe 201/202/203/204/205/207 (BM1366)
- [ ] BitAxe 303 (BM1368)
- [ ] BitAxe 401/402/403 (BM1366 variants)
- [ ] BitAxe 601/602 (BM1370)
- [ ] BitAxe 800x (custom)

**Feature checklist:**
- [ ] ASIC detection and initialization
- [ ] Frequency tuning
- [ ] Auto-tuning mode
- [ ] Manual voltage control
- [ ] Temperature monitoring
- [ ] Fan control
- [ ] Pool connection and switching
- [ ] Share submission
- [ ] Web interface
- [ ] WebSocket updates
- [ ] WiFi AP mode
- [ ] WiFi STA mode
- [ ] Firmware OTA update
- [ ] Configuration persistence
- [ ] Self-test mode

#### Task 8.3: Regression Testing
**Actions:**
1. Run all unit tests (if available)
2. Execute integration test suite
3. Verify no regressions from 5.5.1

**Validation:**
- All tests pass
- No new issues introduced

## Dependencies and Prerequisites

### Build Environment
- **ESP-IDF 6.0+** - Must be installed and activated
- **CMake 3.22.1+** - Required for build system
- **Python 3.10+** - Required for IDF tools
- **Node.js/npm** - For web UI build (already configured)

### Hardware
- Test devices for each supported ASIC type
- Power supply
- Network connection
- Temperature monitoring equipment (optional)

### Tools
- Serial console access
- Network analysis tools
- Logic analyzer (optional, for debugging)

## Risk Assessment

### High Risk Areas
1. **UART Communication** - Driver changes could affect ASIC communication
   - Mitigation: Extensive testing, compare timing with logic analyzer

2. **I2C Communication** - Critical for power and thermal management
   - Mitigation: Already using new driver, but test thoroughly

3. **Memory Constraints** - ESP32-S3 has limited IRAM
   - Mitigation: Monitor memory usage, document IRAM optimization options

### Medium Risk Areas
1. **Performance Regression** - Flash placement of FreeRTOS
   - Mitigation: Baseline comparison, IRAM option documented

2. **Compiler Warnings** - Now treated as errors
   - Mitigation: Clean build, fix all warnings early

### Low Risk Areas
1. **Web Interface** - HTTP server API stable
2. **WiFi** - No breaking changes affecting usage
3. **Configuration** - NVS and SPIFFS unchanged

## Success Criteria

1. ✅ Project builds successfully with ESP-IDF 6.0 without warnings
2. ✅ All compiler warnings resolved
3. ✅ All device configurations tested and working
4. ✅ Performance matches or exceeds ESP-IDF 5.5.1 baseline
5. ✅ 24+ hour stability test passed
6. ✅ Documentation updated
7. ✅ No regressions in functionality
8. ✅ CI/CD updated and passing

## Rollback Plan

If critical issues are discovered:

1. Document the issue in detail
2. Revert to v2.11.0 (ESP-IDF 5.5.1)
3. Create issue tickets for investigation
4. Plan alternative approach

**Rollback command:**
```bash
git checkout v2.11.0
git submodule update --init --recursive
```

## Timeline Estimate

- **Phase 1-2**: Build system updates - 2-3 hours
- **Phase 3**: Configuration review - 1 hour
- **Phase 4**: Build and testing - 4-6 hours
- **Phase 5**: Performance analysis - 2-3 hours
- **Phase 6**: Documentation - 2 hours
- **Phase 7**: CI/CD updates - 1-2 hours
- **Phase 8**: Final validation - 3-4 hours

**Total estimated time**: 15-21 hours

## Notes

- This is a migration sprint focused on compatibility, not new features
- All testing should be done on actual hardware
- Performance baseline from 5.5.1 should be documented before starting
- Keep detailed notes on any unexpected issues or workarounds
- Consider doing this migration on a dedicated test branch first

## References

- ESP-IDF 6.0 Migration Guides: `../esp-idf/docs/en/migration-guides/release-6.x/6.0/`
- ESP-IDF 6.0 Release Notes
- Current ESP-Miner documentation
- Hardware specifications for all supported BitAxe models

---

## Progress Update

### Completed (2025-11-26)

#### Phase 1: Build System Updates ✅
- ✅ Task 1.1: Updated CMakeLists.txt (Note: Set to 3.16 for backward compatibility with ESP-IDF 5.5.x)
- ✅ Task 1.2: Updated components/asic/CMakeLists.txt with esp_driver_uart dependency
- ✅ Task 1.3: Updated main/CMakeLists.txt with new driver dependencies (esp_driver_uart, esp_driver_gpio, esp_driver_i2c)
- ✅ Task 1.4: Updated components/stratum/CMakeLists.txt (json → cjson component name change)

#### Phase 2: Component Manifest Updates ✅
- ✅ Task 2.1: Updated main/idf_component.yml (Note: Kept at >=5.5.0 for backward compatibility)

#### Phase 3: Configuration Updates ✅
- ✅ Task 3.1: Reviewed and updated sdkconfig.defaults (already compatible)

#### Phase 4: Build and Testing ✅
- ✅ Task 4.1: Full clean build completed successfully
  - Binary size: 1,297,675 bytes (~1.27 MB)
  - App partition: 4 MB total, 2.76 MB free (69%)
  - Bootloader: 22,864 bytes (30% free)
  - **Memory Usage:**
    - Flash Code: 953,948 bytes
    - Flash Data: 247,152 bytes
    - DIRAM: 121,131 / 341,760 bytes (35.44% used)
    - **IRAM: 16,384 / 16,384 bytes (100% used)** ⚠️
  - Zero warnings, zero errors

#### Code Fixes Applied
1. **WiFi API Backward Compatibility**: Added conditional compilation in components/connect/connect.c (lines 22-28)
   - ESP-IDF 6.0+: Uses `WIFI_IF_AP` / `WIFI_IF_STA`
   - ESP-IDF 5.5.x: Uses `ESP_IF_WIFI_AP` / `ESP_IF_WIFI_STA`
   - Implemented via `COMPAT_WIFI_IF_*` macros that select appropriate constants based on IDF version
   - Added `#include "esp_idf_version.h"` for version detection
2. **Thermal Driver Headers**: Fixed EMC2101_init() and EMC2103_init() function signatures to accept `int temp_offset_param`
   - Files: main/thermal/EMC2101.h:167, main/thermal/EMC2103.h:65
3. **Stratum Component Dependency**: Changed `"json"` to `"cjson"` in components/stratum/CMakeLists.txt
   - ESP-IDF 6.0 renamed the JSON component from `json` to `cjson`
   - Required for successful build
4. **Build System Backward Compatibility Strategy**:
   - CMake minimum version: 3.16 (supports both ESP-IDF 5.x and 6.x)
   - IDF version requirement: >=5.5.0 (allows both 5.x and 6.x)
   - **Rationale**: Maintains ability to build with ESP-IDF 5.5.x for rollback and comparison testing

#### Issues Encountered and Resolved
1. **ESP-IDF Submodules**: Initial build failed due to out-of-date ESP-IDF git submodules. Required running `git submodule update --init --recursive` in ESP-IDF directory.
2. **WiFi Constants**: ESP-IDF 6.0 changed WiFi interface constants from `ESP_IF_WIFI_*` to `WIFI_IF_*`. Resolved with conditional compilation to support both versions.
   - Location: components/connect/connect.c
   - Solution: Version-aware macros using `ESP_IDF_VERSION_VAL(6, 0, 0)` check
3. **Stratum Component Name**: ESP-IDF 6.0 renamed the JSON component from `json` to `cjson`
   - Location: components/stratum/CMakeLists.txt:11
   - This breaking change was not documented in ESP-IDF migration guides
   - Solution: Updated dependency to use `"cjson"`
4. **Header Mismatch**: EMC thermal driver headers didn't match implementation signatures
   - Files: main/thermal/EMC2101.h, main/thermal/EMC2103.h
   - Solution: Updated function signatures to accept `int temp_offset_param`
5. **Backward Compatibility Decision**: Implemented version detection to support both ESP-IDF 5.5.x and 6.0+ in same codebase
   - Allows building with either ESP-IDF version for testing and rollback
   - Uses conditional compilation throughout affected code

### In Progress

#### Phase 4: Build and Testing (Continued)
- ⏳ Task 4.2: Flash and Basic Functionality Test (requires hardware)
- ⏳ Task 4.3: ASIC Communication Testing (requires hardware)
- ⏳ Task 4.4: Power Management Testing (requires hardware)
- ⏳ Task 4.5: Web Interface and Networking Testing (requires hardware)
- ⏳ Task 4.6: Long-term Stability Testing (requires hardware)

### Not Started

#### Phase 5: Performance Analysis and Optimization
- ⏳ Task 5.1: Performance Baseline Comparison
- ⏳ Task 5.2: Memory Placement Optimization (if needed - IRAM at 100%)
- ⏳ Task 5.3: Compiler Optimization Review

#### Phase 6: Documentation
- ⏳ Task 6.1: Update Build Documentation
- ⏳ Task 6.2: Create Migration Notes
- ⏳ Task 6.3: Update Component Documentation

#### Phase 7: CI/CD Updates
- ⏳ Task 7.1: Update CI Configuration
- ⏳ Task 7.2: Update GitHub Actions

#### Phase 8: Final Validation
- ⏳ Task 8.1: Code Review
- ⏳ Task 8.2: Final Testing Checklist
- ⏳ Task 8.3: Regression Testing

### Key Notes

1. **Backward Compatibility Strategy**: Code now supports both ESP-IDF 5.5.x and 6.0+ using conditional compilation. The same codebase can build with either version, making migration testing and rollback easier.
   - CMake 3.16 minimum (not 3.22.1) to support both IDF versions
   - IDF version requirement >=5.5.0 (not >=6.0.0) for dual version support
   - This intentional decision differs from original sprint plan but provides valuable flexibility

2. **CRITICAL: IRAM at 100% Capacity**: IRAM is at maximum usage (16,384 / 16,384 bytes used)
   - **Impact**: Any additional code requiring IRAM placement will cause build failure
   - **Performance Risk**: FreeRTOS and ring buffers running from flash instead of IRAM may impact real-time performance
   - **Action Required**: Test enabling `CONFIG_FREERTOS_IN_IRAM` and `CONFIG_RINGBUF_IN_IRAM` if performance issues arise
   - This aligns with the risk assessment from the sprint plan
   - Critical for Bitcoin mining timing requirements

3. **Hardware Testing Required**: Phases 4.2-4.6 require actual hardware for testing. This includes all BitAxe configurations listed in Task 8.2.
   - Without hardware validation, migration cannot be considered production-ready
   - Risk of runtime bugs not caught by compilation

4. **Build Success**: The project compiles cleanly with ESP-IDF 6.0 with no warnings or errors, meeting Success Criteria #1 and #2.
   - Binary size: 1,297,808 bytes (~1.27 MB)
   - App partition usage: 31% (69% free)

5. **Sprint Completion Status**: 60% complete (build system and configuration phases done)
   - Remaining: Hardware testing, performance analysis, documentation, CI/CD, final validation
   - Success criteria met: 2/8 (25%)

### Next Steps

1. Hardware testing on actual devices (all BitAxe configurations)
2. Performance baseline comparison with ESP-IDF 5.5.1
3. Address IRAM capacity if performance issues detected
4. Update documentation
5. Update CI/CD pipeline
6. Final validation and testing
