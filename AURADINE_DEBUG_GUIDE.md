# Auradine ASIC Debugging and Instrumentation Guide

## Current Error Investigation

### Error Messages
```
E (1362) vcore: VCORE_init(67): voltage_domains not defined
E (1368) system: SYSTEM_init_peripherals(107): VCORE init failed!
I (1375) power_management: Starting
I (1380) power_management: ASIC Frequency: 525 MHz, Expected hashrate: 0H/s
```

## Root Cause

The device configuration is not being initialized because:
1. Board version defaults to "000" (not in known configs)
2. Device model defaults to "unknown" (not in known families)
3. `GLOBAL_STATE->DEVICE_CONFIG.family` remains zero-initialized
4. `voltage_domains` field is 0, causing VCORE_init to fail at line 67

**Fix Location**: `main/device_config.c:11-82`

## Understanding voltage_domains

### What It Is
`voltage_domains` is a **hardware board design parameter** that defines the power delivery architecture.

### How It's Used
```c
// Setting voltage (vcore.c:120-121)
uint16_t voltage_domains = GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains;
TPS546_set_vout(core_voltage * voltage_domains);

// Reading voltage (vcore.c:133)
return TPS546_get_vout() / voltage_domains * 1000;
```

### Values
- **voltage_domains = 1**: Chips powered in parallel (each chip gets full voltage)
- **voltage_domains = 3**: HEX boards with 6 chips in 3 domains (2 chips in series per domain)

### AURA Configuration
```c
static const FamilyConfig FAMILY_AURA = {
    .asic_count = 2,           // 2 Auradine chips
    .voltage_domains = 1,      // Chips in PARALLEL (assumption - verify with schematic!)
    .nominal_voltage = 12,
    // ...
};
```

### How to Determine Correct Value
**YOU CANNOT DISCOVER THIS FROM THE CHIP** - Check your hardware:

1. **Examine board schematic**
2. **Look at power delivery**:
   - If both chips share the same power rail → voltage_domains = 1
   - If chips are daisy-chained in series → voltage_domains = 2
3. **Measure with multimeter** (if hardware available):
   - Set TPS546 to output 1.2V
   - If each chip sees 1.2V → parallel (voltage_domains = 1)
   - If each chip sees 0.6V → series (voltage_domains = 2)

## Immediate Fix: Configure NVS

### Option 1: Set Board Version to "900"
```bash
# Using idf.py
idf.py nvs-set boardversion "900"

# Or manually in menuconfig
idf.py menuconfig
# Navigate to: Component config → ESP-Miner → Board Version
# Set to: 900
```

### Option 2: Set Device Model to "Aura"
```bash
# Using idf.py
idf.py nvs-set devicemodel "Aura"

# Or manually if you have NVS tools
```

### Verify Configuration
After setting, you should see in boot logs:
```
I (xxxx) device_config: Device Model: Aura
I (xxxx) device_config: Board Version: 900
I (xxxx) device_config: ASIC: 2x Auradine Treasure (128 cores)
```

## What CAN Be Discovered from Chip

The following can be read from the Auradine ASIC:

### 1. Chip ID
- **Register**: 0x00
- **Expected Value**: 0xAD00
- **Read in**: `AURADINE_TREASURE_init()` at line 157

### 2. Chip Count
- **Method**: Send broadcast chip ID read, count responses
- **Function**: `count_asic_chips()` in `components/asic/common.c:39`
- **Returns**: Number of chips detected in chain

### 3. Core Count
- **Location**: Byte 4 of chip ID response
- **Logged by**: `count_asic_chips()` at line 79:
  ```c
  ESP_LOGI(TAG, "Chip %d detected: CORE_NUM: 0x%02x ADDR: 0x%02x",
           chip_counter, buffer[4], buffer[5]);
  ```

### 4. Register Values
Available registers (from REGISTER_MAP at auradine.c:39-46):
- **0x4C**: Error count
- **0x88**: Domain 0 hash count
- **0x89**: Domain 1 hash count
- **0x8A**: Domain 2 hash count
- **0x8B**: Domain 3 hash count
- **0x8C**: Total hash count

## Comprehensive Instrumentation Plan

### Step 1: Enable All Debug Flags

Edit `components/asic/include/auradine.h`:
```c
#define AURADINE_SERIALTX_DEBUG true   // Log all TX packets
#define AURADINE_SERIALRX_DEBUG true   // Log all RX packets
#define AURADINE_DEBUG_WORK true       // Log work packets
#define AURADINE_DEBUG_JOBS true       // Log job processing
```

### Step 2: Add Detailed Init Logging

Create file: `components/asic/auradine_debug.c`

```c
#include "auradine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "serial.h"

static const char * TAG = "auradine_debug";

// Comprehensive register probe
void AURADINE_probe_all_registers(void)
{
    ESP_LOGI(TAG, "=== AURADINE REGISTER PROBE ===");

    // Known register candidates (from BM1370 and documentation)
    uint8_t test_registers[] = {
        0x00,  // Chip ID
        0x04,  // Version/Status (guess)
        0x08,  // PLL Parameters
        0x10,  // Hash counting config
        0x14,  // Difficulty mask
        0x18,  // Misc control
        0x28,  // Fast UART
        0x3C,  // Core control
        0x4C,  // Error count
        0x54,  // Analog mux
        0x58,  // IO driver
        0x88,  // Domain 0 count
        0x89,  // Domain 1 count
        0x8A,  // Domain 2 count
        0x8B,  // Domain 3 count
        0x8C,  // Total count
        0xA4,  // Version rolling
        0xA8,  // Unknown config
        0xB9,  // Unknown config
    };

    for (int i = 0; i < sizeof(test_registers); i++) {
        uint8_t reg = test_registers[i];
        ESP_LOGI(TAG, "Probing register 0x%02X...", reg);

        // Send read command
        _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_READ),
                       (uint8_t[]){0x00, reg}, 2, true);

        // Wait for response
        vTaskDelay(50 / portTICK_PERIOD_MS);

        // Try to receive response
        uint8_t buffer[11];
        int received = SERIAL_rx(buffer, sizeof(buffer), 100);

        if (received > 0) {
            ESP_LOGI(TAG, "Register 0x%02X response (%d bytes):", reg, received);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, received, ESP_LOG_INFO);
        } else {
            ESP_LOGW(TAG, "No response from register 0x%02X", reg);
        }
    }

    ESP_LOGI(TAG, "=== REGISTER PROBE COMPLETE ===");
}

// Discover chip capabilities
void AURADINE_discover_capabilities(uint8_t chip_count)
{
    ESP_LOGI(TAG, "=== CHIP CAPABILITY DISCOVERY ===");
    ESP_LOGI(TAG, "Detected %d chip(s) in chain", chip_count);

    // Re-read chip ID with detailed logging
    ESP_LOGI(TAG, "Reading chip ID from each chip...");
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_READ),
                   (uint8_t[]){0x00, 0x00}, 2, true);

    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Read all responses
    uint8_t buffer[11];
    for (int i = 0; i < chip_count; i++) {
        int received = SERIAL_rx(buffer, sizeof(buffer), 1000);
        if (received == 11) {
            uint16_t chip_id = (buffer[2] << 8) | buffer[3];
            uint8_t core_num = buffer[4];
            uint8_t addr = buffer[5];
            uint32_t chip_data = (buffer[6] << 24) | (buffer[7] << 16) |
                                 (buffer[8] << 8) | buffer[9];

            ESP_LOGI(TAG, "Chip %d:", i);
            ESP_LOGI(TAG, "  Chip ID: 0x%04X", chip_id);
            ESP_LOGI(TAG, "  Core Count: %d", core_num);
            ESP_LOGI(TAG, "  Address: 0x%02X", addr);
            ESP_LOGI(TAG, "  Data: 0x%08X", chip_data);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, received, ESP_LOG_INFO);
        }
    }

    ESP_LOGI(TAG, "=== CAPABILITY DISCOVERY COMPLETE ===");
}

// Power domain detection attempt
void AURADINE_analyze_power_domains(void)
{
    ESP_LOGI(TAG, "=== POWER DOMAIN ANALYSIS ===");
    ESP_LOGW(TAG, "NOTE: voltage_domains CANNOT be discovered from chip!");
    ESP_LOGW(TAG, "This is a HARDWARE DESIGN property.");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "To determine voltage_domains:");
    ESP_LOGI(TAG, "1. Check your board schematic");
    ESP_LOGI(TAG, "2. Look at how TPS546 output connects to chips");
    ESP_LOGI(TAG, "3. Are chips powered in SERIES or PARALLEL?");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Current AURA config: voltage_domains = 1 (PARALLEL)");
    ESP_LOGI(TAG, "  This means: Both chips get full TPS546 voltage");
    ESP_LOGI(TAG, "  Alternative: voltage_domains = 2 (SERIES)");
    ESP_LOGI(TAG, "    Would mean: Each chip gets 1/2 TPS546 voltage");
    ESP_LOGI(TAG, "=== END POWER DOMAIN ANALYSIS ===");
}
```

### Step 3: Instrument AURADINE_TREASURE_init()

Add to the function (after chip detection):

```c
// After line 164 in auradine.c
if (chip_counter > 0) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔═══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  CHIP DETECTION SUCCESS!                  ║");
    ESP_LOGI(TAG, "╠═══════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║  Detected: %2d chip(s)                     ║", chip_counter);
    ESP_LOGI(TAG, "║  Expected: %2d chip(s)                     ║", asic_count);
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════╝");

    // Run discovery functions
    AURADINE_discover_capabilities(chip_counter);
    AURADINE_probe_all_registers();
    AURADINE_analyze_power_domains();
}
```

### Step 4: Add Register Monitoring

Create periodic register dump function:

```c
void AURADINE_monitor_status(void)
{
    // Read error count
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_READ),
                   (uint8_t[]){0x00, 0x4C}, 2, false);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // Read total hash count
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_READ),
                   (uint8_t[]){0x00, 0x8C}, 2, false);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // Process responses via AURADINE_TREASURE_process_work()
}
```

## Testing Procedure

### 1. Fix NVS Config
```bash
cd /home/mcelrath/Projects/ESP-Miner/.worktrees/auradine
idf.py nvs-set boardversion "900"
```

### 2. Build with Debug Enabled
```bash
# Edit components/asic/include/auradine.h first (enable debug flags)
idf.py build
```

### 3. Flash and Monitor
```bash
idf.py flash monitor
```

### 4. Expected Output
```
I (xxx) device_config: Device Model: Aura
I (xxx) device_config: Board Version: 900
I (xxx) device_config: ASIC: 2x Auradine Treasure (128 cores)
I (xxx) vcore: voltage_domains = 1
I (xxx) system: VCORE init success
I (xxx) auradine: Initializing Auradine ASIC
I (xxx) auradine: Detected 2 chip(s) in chain
I (xxx) auradine_debug: === CHIP CAPABILITY DISCOVERY ===
[detailed chip information]
```

## Summary

### What We Know
1. **voltage_domains = 1** (AURA config) assumes parallel power delivery
2. **asic_count = 2** (2 Auradine chips expected)
3. **Chip ID = 0xAD00** (expected)
4. Current code is skeleton based on BM1370

### What Needs Verification
1. Actual Auradine chip ID (might not be 0xAD00)
2. Register map (currently copied from BM1370)
3. Initialization sequence (currently BM1370's sequence)
4. Power delivery architecture (parallel vs series)

### Next Steps
1. Fix NVS config to set board version "900"
2. Enable debug flags to see all communication
3. Run chip with real hardware to capture actual responses
4. Compare with Auradine datasheet
5. Update register addresses and init sequence
6. Verify voltage_domains from schematic

### Files to Modify
- `components/asic/include/auradine.h` - Enable debug flags
- `components/asic/auradine.c` - Add instrumentation
- NVS settings - Configure board version
- Potentially `main/device_config.h` - Adjust voltage_domains if needed
