# Auradine ASIC Implementor's Guide

## Overview

This guide provides detailed instructions for completing the Auradine ASIC driver implementation in ESP-Miner. The skeleton driver has been created based on the BM1370 reference implementation, but requires actual chip-specific values and initialization sequences from the Auradine datasheet.

**Current Status**: Driver skeleton is complete and integrated with the build system. All function stubs exist but use placeholder/BM1370-derived values.

**Your Task**: Using the Auradine datasheet, replace placeholder values with actual chip-specific constants and implement the correct initialization sequence.

## Quick Reference

### Files Created/Modified in Sprint 2

**New Files:**
- `/components/asic/auradine.c` - ASIC driver implementation (SKELETON)
- `/components/asic/include/auradine.h` - ASIC driver header
- `/IMPLEMENTORS_AURADINE.md` - This document

**Modified Files:**
- `/components/asic/asic.c` - Added Auradine to abstraction layer
- `/components/asic/CMakeLists.txt` - Added auradine.c to build
- `/main/device_config.h` - Added AURADINE enum, configs, board version 900

### Board Configuration: Version 900

The Auradine implementation targets board version **900** with the following hardware:

```c
{
    .board_version = "900",
    .family = FAMILY_AURADINE_GAMMA,
    .EMC2103 = true,           // Dual-channel temperature sensor
    .temp_offset = -10,
    .TPS546 = true,            // PMBus voltage regulator
    .INA260 = true,            // Power monitor
    .power_consumption_target = 22,  // Watts
}
```

**Hardware Peripherals:**
- **Thermal**: EMC2103 (I2C 0x4C) - dual external temperature sensors
- **Voltage**: TPS546 (I2C 0x24) - PMBus voltage regulator
- **Power**: INA260 (I2C 0x40) - power/current monitor
- **Serial**: UART1 (TX: GPIO17, RX: GPIO18) - ASIC communication
- **I2C**: GPIO47 (SDA), GPIO48 (SCL) - peripheral bus

## Section 1: Datasheet Information Required

Before you begin implementing, gather the following information from the Auradine datasheet:

### 1.1 Chip Identification

| Information | Location in Code | Current Value | Your Value |
|-------------|------------------|---------------|------------|
| Chip ID | `auradine.c:21` | `0xAD00` | __________ |
| Chip ID Response Length | `auradine.c:22` | `11` bytes | __________ |
| Chip ID Register Address | `auradine.c:40` | `0x00` | __________ |

**Action**: Read the chip ID register section of the datasheet and fill in actual values.

### 1.2 Communication Protocol

| Parameter | Current Value | Your Value |
|-----------|---------------|------------|
| Preamble Bytes | `0x55 0xAA` | __________ |
| Job Packet Type | `0x20` | __________ |
| Cmd Packet Type | `0x40` | __________ |
| Group All | `0x10` | __________ |
| Group Single | `0x00` | __________ |
| CRC Type (Job) | CRC16 | __________ |
| CRC Type (Cmd) | CRC5 | __________ |

**Action**: Verify these values match Auradine's protocol specification.

### 1.3 Register Map

The skeleton driver uses BM1370 register addresses. You must replace these with Auradine-specific addresses:

| Register | Function | Skeleton Addr | Your Addr | Notes |
|----------|----------|---------------|-----------|-------|
| Chip ID | Chip identification | 0x00 | _____ | Read to detect chip |
| Misc Control | General settings | 0x18 | _____ | Initialization |
| PLL Parameter | Frequency control | 0x08 | _____ | Hash frequency |
| Core Control | Core configuration | 0x3C | _____ | Power/performance |
| Difficulty | Mining difficulty | 0x14 | _____ | Set via get_difficulty_mask() |
| UART Config | Baud rate | 0x28 | _____ | Fast UART |
| Version Rolling | Version mask | 0xA4 | _____ | If supported |
| Error Count | Error counter | 0x4C | _____ | Monitoring |
| Domain 0 Count | Hash counter | 0x88 | _____ | If hash domains exist |
| Domain 1 Count | Hash counter | 0x89 | _____ | If hash domains exist |
| Domain 2 Count | Hash counter | 0x8A | _____ | If hash domains exist |
| Domain 3 Count | Hash counter | 0x8B | _____ | If hash domains exist |
| Total Count | Total hash counter | 0x8C | _____ | Hashrate calculation |

**Action**: Update `REGISTER_MAP[]` in `auradine.c:39-46` with correct addresses.

### 1.4 ASIC Specifications

Update `/main/device_config.h` with actual values:

| Parameter | Location | Current Value | Your Value |
|-----------|----------|---------------|------------|
| Core Count | device_config.h:95 | 128 | __________ |
| Small Core Count | device_config.h:95 | 2040 | __________ |
| Hash Domains | device_config.h:95 | 4 | __________ |
| Default Frequency | device_config.h:95 | 600 MHz | __________ |
| Default Voltage | device_config.h:95 | 1200 mV | __________ |
| Frequency Range | device_config.h:83 | 400-700 MHz | __________ |
| Voltage Range | device_config.h:89 | 1000-1300 mV | __________ |

**Action**: Replace placeholder values with actual specifications from datasheet.

### 1.5 PLL Configuration

The frequency is set via PLL parameters. Current code in `AURADINE_send_hash_frequency()` uses:

```c
pll_get_parameters(target_freq, 160, 239, ...);
```

| Parameter | Description | Current Value | Your Value |
|-----------|-------------|---------------|------------|
| fb_divider min | Feedback divider minimum | 160 | __________ |
| fb_divider max | Feedback divider maximum | 239 | __________ |
| VCO threshold | VDO scale threshold | 2400 MHz | __________ |
| Reference clock | PLL reference frequency | 25 MHz | __________ |
| FREQ_MULT | Frequency multiplier | (from pll.h) | __________ |

**Action**: Review PLL configuration section of datasheet and update if different.

### 1.6 Job Packet Format

Current job packet structure (`auradine.h:12-22`):

```c
typedef struct {
    uint8_t job_id;
    uint8_t num_midstates;
    uint8_t starting_nonce[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
    uint8_t merkle_root[32];
    uint8_t prev_block_hash[32];
    uint8_t version[4];
} AURADINE_job;
```

**Questions to Answer:**
1. Does Auradine use the same job packet format?
2. Are fields in the same order?
3. Does it support multiple midstates for version rolling?
4. Are there any additional fields?

**Action**: Compare with datasheet and modify `AURADINE_job` structure if needed.

### 1.7 Result Packet Format

Current result packet structures (`auradine.c:48-76`):

**Job Result:**
```c
typedef struct {
    uint32_t nonce;
    uint8_t midstate_num;
    uint8_t id;
    uint16_t version;
} auradine_asic_result_job_t;
```

**Command Result:**
```c
typedef struct {
    uint32_t value;
    uint8_t asic_address;
    uint8_t register_address;
    uint16_t : 16;  // padding
} auradine_asic_result_cmd_t;
```

**Wrapper:**
```c
typedef struct {
    uint16_t preamble;
    union {
        auradine_asic_result_job_t job;
        auradine_asic_result_cmd_t cmd;
    };
    uint8_t crc : 5;
    uint8_t : 2;
    uint8_t is_job_response : 1;
} auradine_asic_result_t;
```

**Questions to Answer:**
1. What is the result packet preamble? (Currently: not set in skeleton)
2. How is nonce encoded (which bits contain address/core ID)?
3. Does it use the same CRC format?
4. How to distinguish job vs command responses?

**Action**: Update result packet structures based on datasheet.

## Section 2: Implementation Checklist

Work through these tasks in order, using the datasheet as your primary reference.

### Task 2.1: Update Chip ID and Detection

**File**: `auradine.c`

**Lines to Modify**: 21-22, 40

1. Set correct `AURADINE_CHIP_ID` value
2. Set correct `AURADINE_CHIP_ID_RESPONSE_LENGTH`
3. Verify chip ID register address is correct

**Testing**: After flashing, check serial output for chip detection. Should see:
```
auradine: Initializing Auradine ASIC
auradine: Detected X Auradine ASIC chip(s)
```

If chip_counter == 0, verify:
- UART connections (TX/RX not swapped)
- Chip power is on
- Chip ID and register address are correct

### Task 2.2: Update Register Map

**File**: `auradine.c`

**Lines to Modify**: 39-46

Replace the REGISTER_MAP array with Auradine-specific addresses:

```c
static const register_type_t REGISTER_MAP[] = {
    [0xXX] = REGISTER_ERROR_COUNT,      // Your error count register
    [0xXX] = REGISTER_DOMAIN_0_COUNT,   // Your domain 0 register
    [0xXX] = REGISTER_DOMAIN_1_COUNT,
    [0xXX] = REGISTER_DOMAIN_2_COUNT,
    [0xXX] = REGISTER_DOMAIN_3_COUNT,
    [0xXX] = REGISTER_TOTAL_COUNT,      // Your total hash count register
};
```

If Auradine doesn't have hash domains, simplify to:
```c
static const register_type_t REGISTER_MAP[] = {
    [0xXX] = REGISTER_ERROR_COUNT,
    [0xXX] = REGISTER_TOTAL_COUNT,
};
```

### Task 2.3: Implement Initialization Sequence

**File**: `auradine.c`

**Function**: `AURADINE_init()` starting at line 147

The current initialization sequence is copied from BM1370. Replace it with the Auradine-specific sequence from the datasheet.

**Standard Initialization Pattern:**

1. **Set Version Mask** (if version rolling supported)
   ```c
   for (int i = 0; i < 3; i++) {
       AURADINE_set_version_mask(STRATUM_DEFAULT_VERSION_MASK);
   }
   ```

2. **Detect Chips**
   ```c
   _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_READ),
                  (uint8_t[]){0x00, AURADINE_CHIP_ID_REG}, 2,
                  AURADINE_SERIALTX_DEBUG);

   int chip_counter = count_asic_chips(asic_count, AURADINE_CHIP_ID,
                                       AURADINE_CHIP_ID_RESPONSE_LENGTH);
   ```

3. **Initialize Registers** (from datasheet)
   ```c
   // Example - replace with actual init sequence
   _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE),
                  (uint8_t[]){0x00, REG_ADDR, DATA_BYTES}, LENGTH,
                  AURADINE_SERIALTX_DEBUG);
   ```

4. **Deactivate Chain**
   ```c
   _send_chain_inactive();
   ```

5. **Assign Chip Addresses**
   ```c
   address_interval = 256 / chip_counter;
   for (uint8_t i = 0; i < chip_counter; i++) {
       _set_chip_address(i * address_interval);
   }
   ```

6. **Configure Core Registers** (from datasheet)

7. **Set Difficulty Mask**
   ```c
   uint8_t difficulty_mask[6];
   get_difficulty_mask(difficulty, difficulty_mask);
   _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE),
                  difficulty_mask, 6, AURADINE_SERIALTX_DEBUG);
   ```

8. **Set Frequency**
   ```c
   do_frequency_transition(frequency, AURADINE_send_hash_frequency);
   ```

9. **Upgrade Baud Rate** (if supported)
   ```c
   AURADINE_set_max_baud();
   ```

**Action**: Replace the placeholder init sequence (lines 153-215) with Auradine-specific commands from datasheet.

**Critical**: Each `_send_AURADINE()` call must use correct register addresses and data values for Auradine.

### Task 2.4: Implement Frequency Configuration

**File**: `auradine.c`

**Function**: `AURADINE_send_hash_frequency()` starting at line 137

Current implementation:
```c
void AURADINE_send_hash_frequency(float target_freq)
{
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float frequency;

    pll_get_parameters(target_freq, 160, 239, &fb_divider, &refdiv,
                       &postdiv1, &postdiv2, &frequency);

    uint8_t vdo_scale = (fb_divider * FREQ_MULT / refdiv >= 2400) ? 0x50 : 0x40;
    uint8_t postdiv = (((postdiv1 - 1) & 0xf) << 4) | ((postdiv2 - 1) & 0xf);
    uint8_t freqbuf[6] = {0x00, 0x08, vdo_scale, fb_divider, refdiv, postdiv};

    _send_AURADINE(TYPE_CMD | GROUP_ALL | CMD_WRITE, freqbuf, 6,
                   AURADINE_SERIALTX_DEBUG);
}
```

**Questions to Answer:**
1. Does Auradine use a PLL with fb_divider, refdiv, postdiv?
2. What are the valid ranges for each divider?
3. What register address controls PLL? (Currently: 0x08)
4. What is the data format for PLL configuration?

**Action**: Modify based on Auradine PLL specification.

### Task 2.5: Implement Baud Rate Configuration

**File**: `auradine.c`

**Functions**:
- `AURADINE_set_default_baud()` - line 228
- `AURADINE_set_max_baud()` - line 234

Current max baud is 1,000,000. Verify Auradine supports this rate.

**Baud Rate Calculation**: Check datasheet for formula. BM1370 uses:
```
baud = 25MHz / ((divider + 1) * 8)
```

**Action**: Update baud rate register address and values if different.

### Task 2.6: Implement Work Distribution

**File**: `auradine.c`

**Function**: `AURADINE_send_work()` starting at line 244

Current implementation copies job data into `AURADINE_job` structure and sends it.

**Questions to Answer:**
1. Is the job packet format correct for Auradine?
2. Do field sizes and order match?
3. Does Auradine support version rolling (multiple midstates)?
4. Should `num_midstates` be set differently?

**Job ID Management**: Note the job ID cycling logic:
```c
id = (id + 24) % 128;
```

This avoids overlap with active jobs. Verify Auradine uses 7-bit job IDs (0-127).

**Action**: Modify job packet structure if needed, ensure all fields are copied correctly.

### Task 2.7: Implement Result Processing

**File**: `auradine.c`

**Function**: `AURADINE_process_work()` starting at line 270

This is the most critical function - it must correctly parse nonce results from the ASIC.

**Current Implementation Decoding:**
```c
uint8_t job_id = (asic_result.job.id & 0xf0) >> 1;
uint32_t nonce_h = ntohl(asic_result.job.nonce);
uint8_t asic_nr = (uint8_t)((nonce_h >> 17) & 0xff) / address_interval;
uint8_t core_id = (uint8_t)((nonce_h >> 25) & 0x7f);
uint8_t small_core_id = asic_result.job.id & 0x0f;
uint32_t version_bits = (ntohs(asic_result.job.version) << 13);
```

**Questions to Answer from Datasheet:**
1. **Preamble**: What value indicates a valid result packet?
2. **Nonce Encoding**: Which bits of the nonce contain:
   - Chip address?
   - Core ID?
   - Small core ID?
   - Actual nonce value?
3. **Job ID Location**: Where is job_id in the result packet?
4. **Version Bits**: If version rolling supported, how are version bits encoded?
5. **CRC Validation**: How to validate result packet CRC?

**Action**: Rewrite nonce decoding logic based on Auradine result packet format.

**Critical**: Incorrect decoding will cause all nonces to be rejected as invalid!

### Task 2.8: Implement Version Rolling (If Supported)

**File**: `auradine.c`

**Function**: `AURADINE_set_version_mask()` starting at line 127

Current implementation:
```c
void AURADINE_set_version_mask(uint32_t version_mask)
{
    int versions_to_roll = version_mask >> 13;
    uint8_t version_byte0 = (versions_to_roll >> 8);
    uint8_t version_byte1 = (versions_to_roll & 0xFF);
    uint8_t version_cmd[] = {0x00, 0xA4, 0x90, 0x00, version_byte0, version_byte1};
    _send_AURADINE(TYPE_CMD | GROUP_ALL | CMD_WRITE, version_cmd, 6,
                   AURADINE_SERIALTX_DEBUG);
}
```

**Questions to Answer:**
1. Does Auradine support version rolling?
2. If yes, what register controls it? (Currently: 0xA4)
3. How is the version mask encoded?

**Action**: If not supported, leave as stub. If supported, update register address and encoding.

### Task 2.9: Update Device Configuration

**File**: `/main/device_config.h`

**Lines**: 95, 83, 89

Update placeholder values with actual specifications:

1. **Core Counts** (line 95):
   ```c
   .core_count = XXX,        // From datasheet
   .small_core_count = XXXX, // From datasheet
   ```

2. **Hash Domains** (line 95):
   ```c
   .hash_domains = X,  // 0 if none, else actual count
   ```

3. **Frequency Options** (line 83):
   ```c
   static const uint16_t AURADINE_FREQUENCY_OPTIONS[] = {
       XXX, XXX, XXX, XXX, 0  // Valid frequencies in MHz
   };
   ```

4. **Voltage Options** (line 89):
   ```c
   static const uint16_t AURADINE_VOLTAGE_OPTIONS[] = {
       XXXX, XXXX, XXXX, 0  // Valid voltages in mV
   };
   ```

5. **Defaults** (line 95):
   ```c
   .default_frequency_mhz = XXX,  // Recommended operating frequency
   .default_voltage_mv = XXXX,    // Recommended operating voltage
   ```

**Action**: Replace all XXX placeholders with actual values from datasheet.

## Section 3: Testing and Validation

### 3.1 Compilation Test

After completing implementation:

```bash
cd /home/mcelrath/Projects/ESP-Miner/.worktrees/auradine
idf.py build
```

**Expected**: Clean build with no errors or warnings related to Auradine code.

**Common Errors:**
- Undefined references: Missing function implementations
- Type mismatches: Check structure definitions
- Array size mismatches: Verify packet sizes

### 3.2 Chip Detection Test

Flash the firmware and check serial output:

```bash
idf.py flash monitor
```

**Expected Output:**
```
auradine: Initializing Auradine ASIC
auradine: Detected 1 Auradine ASIC chip(s)
auradine: Setting Frequency to 600 MHz (600.xx)
auradine: Auradine ASIC initialization sequence complete
```

**If chip_counter == 0:**
1. Enable debug: Set `AURADINE_SERIALTX_DEBUG` to `true` in `auradine.h:7`
2. Check TX/RX packets in serial log
3. Verify chip ID value matches datasheet
4. Check UART connections (not swapped)
5. Verify chip is powered and out of reset

### 3.3 Register Read Test

After initialization, verify you can read registers:

```c
void AURADINE_read_registers(void)
{
    int size = sizeof(REGISTER_MAP) / sizeof(REGISTER_MAP[0]);
    for (int reg = 0; reg < size; reg++) {
        if (REGISTER_MAP[reg] != REGISTER_INVALID) {
            _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_READ),
                           (uint8_t[]){0x00, reg}, 2,
                           AURADINE_SERIALTX_DEBUG);
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
    }
}
```

**Expected**: Responses with register values. Check for:
- Total hash count increasing over time
- Error count staying low (< 1% of total)
- Domain counts (if applicable)

### 3.4 Work Submission Test

Enable work debug: Set `AURADINE_DEBUG_WORK` to `true` in `auradine.h:9`

**Expected Output:**
```
auradine: Send Job: XX
auradine: Send Job: XX
...
```

Jobs should be sent regularly based on `ASIC_get_asic_job_frequency_ms()`.

For Auradine with 1 chip: 500ms interval (from `asic.c:146`).

### 3.5 Nonce Reception Test

Enable job debug: Set `AURADINE_DEBUG_JOBS` to `true` in `auradine.h:10`

**Expected Output:**
```
auradine: Job ID: XX, Asic nr: 0, Core: XX/XX, Ver: XXXXXXXX
```

**If no nonces received:**
1. Verify difficulty is not too high (should be 256)
2. Check job packet format is correct
3. Enable `AURADINE_SERIALRX_DEBUG` to see raw responses
4. Verify result packet parsing is correct

**If nonces received but all rejected:**
1. Check job ID extraction from result packet
2. Verify nonce byte order (endianness)
3. Check version bits extraction (if applicable)
4. Enable stratum debug to see validation errors

### 3.6 Hashrate Validation

After stable operation for 60+ seconds, check hashrate:

**Expected Hashrate Calculation:**
```
hashrate_GH/s = (core_count + small_core_count) * frequency_MHz / 1000
```

For example, with 128 + 2040 = 2168 cores at 600 MHz:
```
hashrate_GH/s = 2168 * 600 / 1000 = 1300.8 GH/s
```

**Actual Hashrate** (from web interface or serial log):
```
Should be ~85% of theoretical: 1100-1200 GH/s
```

**If hashrate is significantly lower:**
1. Check if overheat protection is throttling frequency
2. Verify voltage is adequate for frequency
3. Check error count register
4. Verify all chips are responding (if multiple chips)

### 3.7 Power and Thermal Validation

**Power Consumption** (from INA260):
- Should be close to `power_consumption_target` (22W for board 900)
- If significantly higher: reduce frequency or voltage
- If lower: can potentially increase performance

**Temperature** (from EMC2103):
- Both channels should read reasonable values (< 75°C)
- Fan should activate based on temperature
- If overheating: check heatsink, increase fan speed, reduce power

## Section 4: Advanced Topics

### 4.1 Multi-Chip Support

If your board has multiple Auradine chips:

1. Update board config `asic_count`:
   ```c
   { .board_version = "900", .family = FAMILY_AURADINE_GAMMA,
     .asic_count = X, ... }  // Change from 1 to actual count
   ```

2. Verify addressing:
   ```c
   address_interval = 256 / chip_counter;
   ```
   For 2 chips: addresses 0x00, 0x80
   For 4 chips: addresses 0x00, 0x40, 0x80, 0xC0

3. Update job frequency:
   ```c
   return 500 / GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
   ```

4. Verify all chips respond during initialization

### 4.2 Frequency Tuning

To optimize performance:

1. **Start Conservative**: Use datasheet recommended frequency and voltage

2. **Increase Gradually**:
   - Increase frequency by 25 MHz increments
   - Verify hashrate increases proportionally
   - Monitor temperature and power

3. **Voltage Adjustment**:
   - If unstable (high error count), increase voltage by 50mV
   - If too hot, reduce voltage or frequency

4. **Sweet Spot**:
   - Balance: hashrate, power, temperature
   - Typical efficiency: 0.85-0.90 of theoretical hashrate

5. **Update Defaults**: Once stable, update `device_config.h` with optimal values

### 4.3 Debugging Tips

**Enable All Debug Output:**
```c
// In auradine.h
#define AURADINE_SERIALTX_DEBUG true
#define AURADINE_SERIALRX_DEBUG true
#define AURADINE_DEBUG_WORK true
#define AURADINE_DEBUG_JOBS true
```

**Serial Output Format:**
```
tx: 55 AA 51 09 00 08 50 28 19 08 1D    // Transmitted packet
rx: AA 55 13 70 00 00 00 00 00 00 0F   // Received packet
```

**Packet Decoding:**
- `55 AA` = Preamble
- `51` = Header (TYPE_CMD | GROUP_ALL | CMD_WRITE)
- `09` = Length
- `00 08 ...` = Data
- Last byte = CRC

**Common Issues:**

| Symptom | Likely Cause | Solution |
|---------|-------------|----------|
| No chip detected | Wrong chip ID or UART issue | Check ID value, swap TX/RX |
| No nonces | Job packet format wrong | Verify job structure |
| Nonces rejected | Result parsing wrong | Check nonce decoding |
| Low hashrate | Voltage too low | Increase voltage |
| High errors | Frequency too high | Reduce frequency |
| Overheating | Insufficient cooling | Check fan, heatsink |

### 4.4 Reference Implementation Comparison

The driver is based on BM1370. Key differences to watch for:

| Feature | BM1370 | Auradine | Action |
|---------|--------|----------|--------|
| Chip ID | 0x1370 | ??? | Update from datasheet |
| PLL Range | fb: 160-239 | ??? | Update if different |
| Version Rolling | Yes | ??? | Implement if supported |
| Hash Domains | 4 | ??? | Update count or remove |
| Job Packet | 80 bytes | ??? | Verify size matches |

**If Auradine is very different from BM1370**, you may need to:
1. Redesign packet structures
2. Rewrite init sequence completely
3. Change result parsing logic significantly

## Section 5: Next Steps (Future Sprints)

After completing the Auradine driver:

### Priority 2: W5500 Ethernet Support
- Add ESP-Miner-Lan as remote
- Port W5500 driver component
- Integrate with network stack
- Test Stratum over Ethernet

### Priority 3: Dual Serial Interfaces
- Configure USB serial (UART0) for control
- Configure BAM header for ASIC or external device
- Implement mode switching

### Priority 4: Enhanced I2C Support
- Add runtime I2C scanning
- Support additional sensors
- Aggregate multi-sensor data

### Priority 5: WiFi Disabling
- Conditional WiFi compilation
- Power optimization
- Explicit network interface selection

### Priority 6: HTML Interface Removal
- Disable HTTP/WebSocket servers
- RPC-only operation
- Reduce memory footprint

## Section 6: Troubleshooting Reference

### Problem: Chip Not Detected

**Symptoms**: `chip_counter == 0` after init

**Debug Steps:**
1. Enable `AURADINE_SERIALTX_DEBUG` and `AURADINE_SERIALRX_DEBUG`
2. Check for TX packets: should see chip ID read command
3. Check for RX packets: should see chip ID response
4. If no RX: hardware issue (UART, power, reset)
5. If RX but wrong format: check chip ID value and response length

**Hardware Checklist:**
- [ ] ASIC powered (check voltage regulator output)
- [ ] ASIC out of reset (ASIC_RESET high)
- [ ] UART TX/RX not swapped
- [ ] UART pins correct (GPIO17/18)
- [ ] Chip clock running (if external clock required)

### Problem: Nonces Not Received

**Symptoms**: Jobs sent, but no result packets

**Debug Steps:**
1. Verify chip is hashing: read total count register
   - Should increase over time
2. Check difficulty is appropriate (start with 256)
3. Enable `AURADINE_SERIALRX_DEBUG` to see raw responses
4. Verify job packet format matches datasheet exactly

**Data Validation:**
- [ ] Job packet size correct
- [ ] All fields in correct byte order (endianness)
- [ ] Starting nonce set
- [ ] nbits (difficulty) set correctly
- [ ] ntime current
- [ ] Merkle root and prev hash valid

### Problem: All Nonces Rejected

**Symptoms**: Results received, but validation fails

**Debug Steps:**
1. Check job ID extraction:
   ```c
   uint8_t job_id = ... // Must match sent job_id
   ```
2. Verify nonce byte order:
   ```c
   uint32_t nonce_h = ntohl(asic_result.job.nonce);
   ```
3. Check version bits (if rolling enabled):
   ```c
   uint32_t version_bits = ...
   uint32_t rolled_version = base_version | version_bits;
   ```
4. Enable stratum debug to see exact validation error

**Common Causes:**
- Job ID field in wrong position
- Nonce endianness incorrect
- Version bits shifted wrong amount
- Wrong job marked as valid

### Problem: Low Hashrate

**Symptoms**: Hashrate < 70% of expected

**Debug Steps:**
1. Check error count register:
   - If high (> 5%): voltage too low or frequency too high
2. Check temperature:
   - If overheating: thermal throttling active
3. Verify all chips responding (if multiple chips)
4. Check if power limiting active

**Optimization:**
- Increase voltage by 50-100mV
- Reduce frequency by 25-50 MHz
- Improve cooling
- Check PLL configuration is correct

### Problem: System Crashes or Reboots

**Symptoms**: ESP32 watchdog resets or crashes

**Possible Causes:**
1. **Stack overflow**: ASIC task stack too small
2. **Memory leak**: Not freeing job structures
3. **Invalid pointer**: Corrupted result packet parsing
4. **Power supply**: Insufficient current for ASIC + ESP32

**Debug:**
```bash
idf.py monitor
```
Look for:
- Task watchdog errors
- Stack overflow messages
- Brownout detector resets

**Solutions:**
- Increase task stack sizes in FreeRTOS config
- Add null pointer checks
- Verify power supply can handle peak current

## Section 7: Resources and References

### Documentation
- Main Implementor's Guide: `/IMPLEMENTORS.md`
- Sprint Plan: `/SPRINT_2.md`
- ESP-IDF Documentation: https://docs.espressif.com/projects/esp-idf/

### Code References
- BM1370 Reference Driver: `/components/asic/bm1370.c`
- ASIC Abstraction Layer: `/components/asic/asic.c`
- Device Configuration: `/main/device_config.h`
- Serial Communication: `/components/asic/serial.c`
- CRC Functions: `/components/asic/crc.c`

### Hardware References
- EMC2103 Datasheet: Dual temperature sensor
- TPS546 Datasheet: PMBus voltage regulator
- INA260 Datasheet: Power monitor
- ESP32-S3 Technical Reference Manual

### Community
- ESP-Miner GitHub: https://github.com/bitaxeorg/ESP-Miner
- Bitaxe Discord: Community support

## Appendix A: Complete Register Initialization Example

This is a **template** - replace all values with actual Auradine values:

```c
uint8_t AURADINE_init(float frequency, uint16_t asic_count, uint16_t difficulty)
{
    ESP_LOGI(TAG, "Initializing Auradine ASIC");

    // Step 1: Set version mask (if supported)
    for (int i = 0; i < 3; i++) {
        AURADINE_set_version_mask(STRATUM_DEFAULT_VERSION_MASK);
    }

    // Step 2: Detect chips
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_READ),
                   (uint8_t[]){0x00, CHIP_ID_REG}, 2,
                   AURADINE_SERIALTX_DEBUG);

    int chip_counter = count_asic_chips(asic_count, AURADINE_CHIP_ID,
                                        AURADINE_CHIP_ID_RESPONSE_LENGTH);

    if (chip_counter == 0) {
        ESP_LOGE(TAG, "No Auradine chips detected");
        return 0;
    }

    ESP_LOGI(TAG, "Detected %d Auradine chip(s)", chip_counter);

    // Step 3: Init register A (from datasheet)
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE),
                   (uint8_t[]){0x00, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX}, 6,
                   AURADINE_SERIALTX_DEBUG);

    // Step 4: Init register B (from datasheet)
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE),
                   (uint8_t[]){0x00, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX}, 6,
                   AURADINE_SERIALTX_DEBUG);

    // Step 5: Deactivate all chips
    _send_chain_inactive();

    // Step 6: Assign addresses
    address_interval = 256 / chip_counter;
    for (uint8_t i = 0; i < chip_counter; i++) {
        _set_chip_address(i * address_interval);
    }

    // Step 7: Per-chip configuration
    for (uint8_t i = 0; i < chip_counter; i++) {
        uint8_t addr = i * address_interval;

        // Configure register C
        _send_AURADINE((TYPE_CMD | GROUP_SINGLE | CMD_WRITE),
                       (uint8_t[]){addr, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX}, 6,
                       AURADINE_SERIALTX_DEBUG);
    }

    // Step 8: Set difficulty
    uint8_t difficulty_mask[6];
    get_difficulty_mask(difficulty, difficulty_mask);
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE),
                   difficulty_mask, 6, AURADINE_SERIALTX_DEBUG);

    // Step 9: Set frequency
    do_frequency_transition(frequency, AURADINE_send_hash_frequency);

    // Step 10: Final config registers
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE),
                   (uint8_t[]){0x00, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX}, 6,
                   AURADINE_SERIALTX_DEBUG);

    // Step 11: Upgrade baud rate
    AURADINE_set_max_baud();

    // Step 12: Clear buffer
    SERIAL_clear_buffer();

    ESP_LOGI(TAG, "Auradine initialization complete");
    return chip_counter;
}
```

Replace all `0xXX` values with actual register addresses and data from the Auradine datasheet.

## Appendix B: Debug Output Examples

### Successful Initialization

```
I (1234) auradine: Initializing Auradine ASIC
tx: 55 AA 52 05 00 00 00 1C          // Read chip ID
rx: AA 55 00 A0 AD 00 00 00 00 00 0F // Chip ID response (0xAD00)
I (1245) auradine: Detected 1 Auradine chip(s)
tx: 55 AA 51 09 00 18 F0 00 C1 00 04 // Misc control
tx: 55 AA 53 05 00 00 03              // Chain inactive
tx: 55 AA 40 05 00 00 1C              // Set address 0x00
tx: 55 AA 51 09 00 3C 80 00 8B 00 12 // Core config
tx: 55 AA 51 09 00 14 FF FF FF FF 1F // Difficulty
tx: 55 AA 51 09 00 08 50 28 19 08 1D // PLL config (600 MHz)
I (1290) auradine: Setting Frequency to 600 MHz (600.00)
I (1295) auradine: Auradine initialization complete
```

### Work Submission and Results

```
I (5000) auradine: Send Job: 00
I (5500) auradine: Send Job: 18
rx: AA 55 00 A0 12 34 56 78 03 00 00 // Job result
I (5505) auradine: Job ID: 00, Asic nr: 0, Core: 45/3, Ver: 00000000
I (6000) auradine: Send Job: 30
rx: AA 55 00 A0 AB CD EF 01 18 00 00 // Job result
I (6005) auradine: Job ID: 18, Asic nr: 0, Core: 67/1, Ver: 00000000
```

## Conclusion

This guide provides a complete roadmap for implementing the Auradine ASIC driver. The skeleton is in place - your task is to fill in the chip-specific details from the datasheet.

**Summary of Required Actions:**
1. ✅ Gather all specifications from Auradine datasheet (Section 1)
2. ✅ Update chip ID and register addresses (Task 2.1-2.2)
3. ✅ Implement initialization sequence (Task 2.3)
4. ✅ Configure frequency/PLL (Task 2.4)
5. ✅ Verify work submission (Task 2.6)
6. ✅ Implement result parsing (Task 2.7)
7. ✅ Update device configuration (Task 2.9)
8. ✅ Test and validate (Section 3)

**Success Criteria:**
- Chip detected at boot
- Jobs submitted regularly
- Nonces received and validated
- Hashrate matches specifications
- System stable for 24+ hours

Good luck with your implementation!
