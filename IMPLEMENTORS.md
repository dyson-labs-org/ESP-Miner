# ESP-Miner ASIC Implementor's Guide

## Table of Contents
1. [Overview](#overview)
2. [Architecture Summary](#architecture-summary)
3. [Adding a New ASIC: Step-by-Step Guide](#adding-a-new-asic-step-by-step-guide)
4. [Communication Protocol Details](#communication-protocol-details)
5. [Hardware Integration](#hardware-integration)
6. [Power Management](#power-management)
7. [Thermal Management](#thermal-management)
8. [Testing and Validation](#testing-and-validation)

---

## Overview

The ESP-Miner firmware is an ESP32-based Bitcoin mining controller designed for the Bitaxe hardware platform. It provides a complete abstraction layer for managing Bitcoin ASIC chips via UART communication, with integrated power management, thermal control, and mining pool connectivity.

**Target Hardware:**
- Microcontroller: ESP32-S3-WROOM-1 (N16R8: 16MB Flash, 8MB PSRAM)
- Build System: ESP-IDF with CMake
- Primary Language: C

**Current ASIC Support:**
- BM1397 (Bitaxe Max)
- BM1366 (Bitaxe Ultra & Hex)
- BM1368 (Bitaxe Supra)
- BM1370 (Bitaxe Gamma & Gamma Turbo)

---

## Architecture Summary

### System Components

```
┌─────────────────────────────────────────────────────────────┐
│                        ESP32-S3                              │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │           FreeRTOS Task Layer                         │   │
│  │  ┌──────────┐  ┌──────────┐  ┌─────────────────┐   │   │
│  │  │ Stratum  │  │  Create  │  │  ASIC Result    │   │   │
│  │  │   Task   │  │Jobs Task │  │  Task (Pri:15)  │   │   │
│  │  └──────────┘  └──────────┘  └─────────────────┘   │   │
│  │  ┌──────────┐  ┌──────────┐  ┌─────────────────┐   │   │
│  │  │   ASIC   │  │ Hashrate │  │ Power Mgmt Task │   │   │
│  │  │   Task   │  │ Monitor  │  │                 │   │   │
│  │  └──────────┘  └──────────┘  └─────────────────┘   │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │         ASIC Hardware Abstraction Layer              │   │
│  │    ASIC_init() | ASIC_send_work() | ASIC_process()  │   │
│  └──────────────────────────────────────────────────────┘   │
│                            │                                  │
│  ┌────────────────────────┴───────────────────────────┐     │
│  │        ASIC Driver Implementations                  │     │
│  │   bm1397.c  bm1366.c  bm1368.c  bm1370.c           │     │
│  └─────────────────────────────────────────────────────┘     │
│                            │                                  │
│  ┌────────────────────────┴───────────────────────────┐     │
│  │            Serial Communication Layer               │     │
│  │    UART1 @ 115200 baud (upgradable per ASIC)      │     │
│  │    TX: GPIO 17  |  RX: GPIO 18                     │     │
│  └─────────────────────────────────────────────────────┘     │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │          I2C Peripheral Management                    │   │
│  │    SDA: GPIO 47  |  SCL: GPIO 48  |  400 kHz         │   │
│  │  ┌────────────┐  ┌───────────┐  ┌────────────┐     │   │
│  │  │  Voltage   │  │  Thermal  │  │   Power    │     │   │
│  │  │ Regulator  │  │  Sensors  │  │  Monitor   │     │   │
│  │  │ (DS4432/   │  │ (EMC2101/ │  │  (INA260)  │     │   │
│  │  │  TPS546)   │  │  EMC2103) │  │            │     │   │
│  │  └────────────┘  └───────────┘  └────────────┘     │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │             GPIO Control Signals                      │   │
│  │    ASIC_RESET (GPIO 1)  |  ASIC_ENABLE (GPIO 10)    │   │
│  └──────────────────────────────────────────────────────┘   │
└───────────────────────────────────────────────────────────────┘
                            │
                            │ UART (Serial)
                            ▼
            ┌───────────────────────────────┐
            │      Bitcoin ASIC Chip(s)     │
            │    (BM1397/66/68/70 or new)   │
            └───────────────────────────────┘
```

### Data Flow

1. **Work Reception**: Stratum task receives mining jobs from pool
2. **Job Processing**: Create jobs task converts Stratum work to ASIC-specific job packets
3. **Work Distribution**: ASIC task sends job packets via UART to ASIC chip(s)
4. **Result Collection**: ASIC result task (highest priority) processes nonces returned by ASIC
5. **Validation & Submission**: Valid shares are sent back to mining pool

---

## Adding a New ASIC: Step-by-Step Guide

This guide will walk through adding the **Auradine Treasure ASIC** to the ESP-Miner codebase.

### Step 1: Create ASIC Driver Files

Create two new files in the ASIC component directory:

**File**: `/components/asic/treasure.c`
**File**: `/components/asic/include/treasure.h`

### Step 2: Define ASIC Header Structure

**`/components/asic/include/treasure.h`**:

```c
#ifndef TREASURE_H_
#define TREASURE_H_

#include "common.h"
#include "mining.h"

// Debug flags
#define TREASURE_SERIALTX_DEBUG false
#define TREASURE_SERIALRX_DEBUG false
#define TREASURE_DEBUG_WORK false
#define TREASURE_DEBUG_JOBS false

// Job packet structure - adapt to Treasure ASIC protocol
typedef struct __attribute__((__packed__))
{
    uint8_t job_id;
    uint8_t num_midstates;
    uint8_t starting_nonce[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
    uint8_t merkle4[4];
    uint8_t midstate[32];
    // Add additional midstates if Treasure supports version rolling
    uint8_t midstate1[32];
    uint8_t midstate2[32];
    uint8_t midstate3[32];
} treasure_job_packet;

// Public API functions
uint8_t TREASURE_init(float frequency, uint16_t asic_count, uint16_t difficulty);
void TREASURE_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void TREASURE_set_version_mask(uint32_t version_mask);
int TREASURE_set_max_baud(void);
int TREASURE_set_default_baud(void);
void TREASURE_send_hash_frequency(float frequency);
task_result * TREASURE_process_work(void * GLOBAL_STATE);
void TREASURE_read_registers(void);

#endif /* TREASURE_H_ */
```

### Step 3: Implement Core Driver Functions

**`/components/asic/treasure.c`** - Key sections to implement:

#### 3.1 Protocol Constants

```c
#include "treasure.h"
#include "crc.h"
#include "global_state.h"
#include "serial.h"
#include "utils.h"
#include "esp_log.h"
#include "pll.h"

// Define Treasure-specific constants
#define TREASURE_CHIP_ID 0xXXXX  // Replace with actual chip ID
#define TREASURE_CHIP_ID_RESPONSE_LENGTH 11  // Adjust based on Treasure response

// Command packet types (may differ for Treasure)
#define TYPE_JOB 0x20
#define TYPE_CMD 0x40

#define GROUP_SINGLE 0x00
#define GROUP_ALL 0x10

#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03

// Treasure register addresses - obtain from datasheet
#define TREASURE_REG_CHIP_ID 0x00
#define TREASURE_REG_MISC_CONTROL 0x18
#define TREASURE_REG_PLL_PARAMETER 0x08
#define TREASURE_REG_CORE_CONTROL 0x3C
#define TREASURE_REG_DIFFICULTY 0x14
// Add more registers as documented

// Register map for reading counters and status
static const register_type_t REGISTER_MAP[] = {
    [0x4C] = REGISTER_ERROR_COUNT,
    [0x88] = REGISTER_DOMAIN_0_COUNT,  // If Treasure has hash domains
    [0x89] = REGISTER_DOMAIN_1_COUNT,
    [0x8A] = REGISTER_DOMAIN_2_COUNT,
    [0x8B] = REGISTER_DOMAIN_3_COUNT,
    [0x8C] = REGISTER_TOTAL_COUNT
};

static const char * TAG = "treasure";
static task_result result;
static int address_interval;
```

#### 3.2 Result Packet Structures

```c
// Define result packet format based on Treasure datasheet
typedef struct __attribute__((__packed__))
{
    uint32_t nonce;                   // 2-5
    uint8_t midstate_num;             // 6
    uint8_t id;                       // 7
    uint16_t version;                 // 8-9 (if version rolling supported)
} treasure_asic_result_job_t;

typedef struct __attribute__((__packed__))
{
    uint32_t value;                   // 2-5
    uint8_t asic_address;             // 6
    uint8_t register_address;         // 7
    uint16_t                  : 16;   // 8-9
} treasure_asic_result_cmd_t;

typedef struct __attribute__((__packed__))
{
    uint16_t preamble;                // 0-1 (typically 0xAA55 or 0x55AA)
    union {
        treasure_asic_result_job_t job;
        treasure_asic_result_cmd_t cmd;
    };
    uint8_t crc             : 5;      // CRC field
    uint8_t                 : 2;
    uint8_t is_job_response : 1;      // Flag to distinguish job vs cmd response
} treasure_asic_result_t;
```

#### 3.3 Packet Sending Function

```c
static void _send_TREASURE(uint8_t header, const uint8_t * data, uint8_t data_len, bool debug)
{
    packet_type_t packet_type = (header & TYPE_JOB) ? JOB_PACKET : CMD_PACKET;
    const uint8_t total_length = (packet_type == JOB_PACKET) ? (data_len + 6) : (data_len + 5);

    uint8_t buf[total_length];

    // Preamble - check Treasure datasheet for correct values
    buf[0] = 0x55;
    buf[1] = 0xAA;

    // Header field
    buf[2] = header;

    // Length field
    buf[3] = (packet_type == JOB_PACKET) ? (data_len + 4) : (data_len + 3);

    // Data payload
    memcpy(buf + 4, data, data_len);

    // CRC calculation - verify Treasure uses CRC16 for jobs, CRC5 for commands
    if (packet_type == JOB_PACKET) {
        uint16_t crc16_total = crc16_false(buf + 2, data_len + 2);
        buf[4 + data_len] = (crc16_total >> 8) & 0xFF;
        buf[5 + data_len] = crc16_total & 0xFF;
    } else {
        buf[4 + data_len] = crc5(buf + 2, data_len + 2);
    }

    // Send via UART
    if (SERIAL_send(buf, total_length, debug) == 0) {
        ESP_LOGE(TAG, "Failed to send data to Treasure ASIC");
    }
}
```

#### 3.4 Initialization Function

```c
uint8_t TREASURE_init(float frequency, uint16_t asic_count, uint16_t difficulty)
{
    ESP_LOGI(TAG, "Initializing Treasure ASIC at %.2f MHz", frequency);

    // Step 1: Set version mask if Treasure supports version rolling
    for (int i = 0; i < 3; i++) {
        TREASURE_set_version_mask(STRATUM_DEFAULT_VERSION_MASK);
    }

    // Step 2: Read chip ID from all chips to detect count
    _send_TREASURE((TYPE_CMD | GROUP_ALL | CMD_READ),
                   (uint8_t[]){0x00, TREASURE_REG_CHIP_ID}, 2,
                   TREASURE_SERIALTX_DEBUG);

    int chip_counter = count_asic_chips(asic_count, TREASURE_CHIP_ID,
                                        TREASURE_CHIP_ID_RESPONSE_LENGTH);

    if (chip_counter == 0) {
        ESP_LOGE(TAG, "No Treasure chips detected!");
        return 0;
    }

    ESP_LOGI(TAG, "Detected %d Treasure ASIC chip(s)", chip_counter);

    // Step 3: Send initialization sequence (from Treasure datasheet)

    // Example: Misc Control Register
    _send_TREASURE((TYPE_CMD | GROUP_ALL | CMD_WRITE),
                   (uint8_t[]){0x00, TREASURE_REG_MISC_CONTROL, 0xF0, 0x00, 0xC1, 0x00},
                   6, TREASURE_SERIALTX_DEBUG);

    // Set all chips to inactive before addressing
    _send_TREASURE((TYPE_CMD | GROUP_ALL | CMD_INACTIVE),
                   (uint8_t[]){0x00, 0x00}, 2,
                   TREASURE_SERIALTX_DEBUG);

    // Step 4: Assign chip addresses
    address_interval = 256 / chip_counter;
    for (uint8_t i = 0; i < chip_counter; i++) {
        uint8_t chip_addr = i * address_interval;
        ESP_LOGI(TAG, "Setting chip %d address to 0x%02X", i, chip_addr);

        _send_TREASURE((TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS),
                       (uint8_t[]){chip_addr, 0x00}, 2,
                       TREASURE_SERIALTX_DEBUG);
    }

    // Step 5: Configure core registers (consult Treasure datasheet)
    _send_TREASURE((TYPE_CMD | GROUP_ALL | CMD_WRITE),
                   (uint8_t[]){0x00, TREASURE_REG_CORE_CONTROL, 0x80, 0x00, 0x8B, 0x00},
                   6, TREASURE_SERIALTX_DEBUG);

    // Step 6: Set difficulty mask
    uint8_t difficulty_mask[6];
    get_difficulty_mask(difficulty, difficulty_mask);
    _send_TREASURE((TYPE_CMD | GROUP_ALL | CMD_WRITE), difficulty_mask,
                   6, TREASURE_SERIALTX_DEBUG);

    // Step 7: Set frequency
    TREASURE_send_hash_frequency(frequency);

    // Step 8: Upgrade UART baud rate if supported
    int max_baud = TREASURE_set_max_baud();
    if (max_baud > 0) {
        ESP_LOGI(TAG, "UART baud rate set to %d", max_baud);
    }

    // Step 9: Clear UART buffer
    SERIAL_clear_buffer();

    return chip_counter;
}
```

#### 3.5 Frequency Configuration

```c
void TREASURE_send_hash_frequency(float target_freq)
{
    // Calculate PLL parameters for Treasure ASIC
    // Consult Treasure datasheet for valid PLL parameter ranges
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float actual_freq;

    // Example: assuming similar PLL structure to BM1370
    pll_get_parameters(target_freq, 160, 239, &fb_divider, &refdiv,
                       &postdiv1, &postdiv2, &actual_freq);

    // Construct PLL parameter packet (format from Treasure datasheet)
    uint8_t vdo_scale = (fb_divider * 25.0 / refdiv >= 2400) ? 0x50 : 0x40;
    uint8_t postdiv = (((postdiv1 - 1) & 0xf) << 4) | ((postdiv2 - 1) & 0xf);
    uint8_t freqbuf[6] = {0x00, TREASURE_REG_PLL_PARAMETER, vdo_scale,
                          fb_divider, refdiv, postdiv};

    _send_TREASURE(TYPE_CMD | GROUP_ALL | CMD_WRITE, freqbuf, 6,
                   TREASURE_SERIALTX_DEBUG);

    ESP_LOGI(TAG, "Set frequency to %.2f MHz (actual: %.2f MHz)", target_freq, actual_freq);
}
```

#### 3.6 Work Sending Function

```c
void TREASURE_send_work(void * GLOBAL_STATE, bm_job * next_bm_job)
{
    GlobalState * g = (GlobalState *) GLOBAL_STATE;

    // Construct job packet for Treasure ASIC
    treasure_job_packet job;
    job.job_id = next_bm_job->job_id;
    job.num_midstates = next_bm_job->num_midstates;

    // Copy job data (convert from network byte order if necessary)
    memcpy(job.starting_nonce, next_bm_job->starting_nonce, 4);
    memcpy(job.nbits, next_bm_job->nbits, 4);
    memcpy(job.ntime, next_bm_job->ntime, 4);
    memcpy(job.merkle4, next_bm_job->merkle_root_be, 4);
    memcpy(job.midstate, next_bm_job->midstate, 32);

    // Include additional midstates if version rolling is enabled
    if (job.num_midstates > 1) {
        memcpy(job.midstate1, next_bm_job->midstate1, 32);
        memcpy(job.midstate2, next_bm_job->midstate2, 32);
        memcpy(job.midstate3, next_bm_job->midstate3, 32);
    }

    // Send job packet
    _send_TREASURE(TYPE_JOB | GROUP_ALL | CMD_WRITE,
                   (uint8_t *) &job, sizeof(job),
                   TREASURE_DEBUG_WORK);
}
```

#### 3.7 Result Processing Function

```c
task_result * TREASURE_process_work(void * GLOBAL_STATE)
{
    GlobalState * g = (GlobalState *) GLOBAL_STATE;
    treasure_asic_result_t asic_result;

    int received = SERIAL_rx((uint8_t *) &asic_result, sizeof(asic_result), 1000);

    if (received < 0 || received != sizeof(asic_result)) {
        return NULL;
    }

    // Validate preamble
    if (asic_result.preamble != 0xAAFF) {  // Adjust based on Treasure spec
        ESP_LOGE(TAG, "Invalid preamble: 0x%04X", asic_result.preamble);
        return NULL;
    }

    // Validate CRC
    uint8_t calc_crc = crc5((uint8_t *) &asic_result.job, 8);
    if (calc_crc != asic_result.crc) {
        ESP_LOGE(TAG, "CRC mismatch");
        return NULL;
    }

    if (asic_result.is_job_response) {
        // Process nonce result
        result.job_id = asic_result.job.id;
        result.nonce = asic_result.job.nonce;
        result.rolled_version = asic_result.job.version;  // If version rolling supported

        ESP_LOGI(TAG, "Nonce found: 0x%08X (job_id: %d)", result.nonce, result.job_id);
        return &result;
    } else {
        // Process register read response
        uint8_t reg_addr = asic_result.cmd.register_address;
        if (reg_addr < sizeof(REGISTER_MAP) / sizeof(REGISTER_MAP[0])) {
            result.register_type = REGISTER_MAP[reg_addr];
            result.asic_nr = asic_result.cmd.asic_address / address_interval;
            result.value = asic_result.cmd.value;
            return &result;
        }
    }

    return NULL;
}
```

#### 3.8 Version Mask and Baud Rate Functions

```c
void TREASURE_set_version_mask(uint32_t version_mask)
{
    // If Treasure supports version rolling, configure it here
    // Example (adjust register and format per datasheet):
    int versions_to_roll = version_mask >> 13;
    uint8_t version_byte0 = (versions_to_roll >> 8);
    uint8_t version_byte1 = (versions_to_roll & 0xFF);
    uint8_t version_cmd[] = {0x00, 0xA4, 0x90, 0x00, version_byte0, version_byte1};
    _send_TREASURE(TYPE_CMD | GROUP_ALL | CMD_WRITE, version_cmd, 6,
                   TREASURE_SERIALTX_DEBUG);
}

int TREASURE_set_max_baud(void)
{
    // Set maximum baud rate supported by Treasure
    // Consult datasheet for max baud rate and register configuration
    int max_baud = 1000000;  // Example: 1 Mbaud

    // Send baud rate configuration to ASIC
    uint8_t baud_cmd[] = {0x00, 0x28, 0x00, 0x10};  // Example values
    _send_TREASURE(TYPE_CMD | GROUP_ALL | CMD_WRITE, baud_cmd, 4,
                   TREASURE_SERIALTX_DEBUG);

    // Update ESP32 UART baud rate
    SERIAL_set_baud(max_baud);

    return max_baud;
}

int TREASURE_set_default_baud(void)
{
    SERIAL_set_baud(115200);
    return 115200;
}

void TREASURE_read_registers(void)
{
    // Read status/hashrate registers periodically
    // Example: read total hash count
    _send_TREASURE((TYPE_CMD | GROUP_ALL | CMD_READ),
                   (uint8_t[]){0x00, 0x8C}, 2,  // Total count register
                   TREASURE_SERIALRX_DEBUG);
}
```

### Step 4: Add ASIC Configuration to Device Config

**`/main/device_config.h`**:

```c
typedef enum
{
    BM1397,
    BM1366,
    BM1368,
    BM1370,
    TREASURE,  // Add new ASIC type
} Asic;

// Define Treasure frequency and voltage options
static const uint16_t TREASURE_FREQUENCY_OPTIONS[] = {400, 500, 600, 700, 0};  // Adjust to actual
static const uint16_t TREASURE_VOLTAGE_OPTIONS[] = {1000, 1100, 1200, 1300, 0};  // Adjust to actual

// Add Treasure ASIC configuration
static const AsicConfig ASIC_TREASURE = {
    .id = TREASURE,
    .name = "Treasure",
    .chip_id = 0xXXXX,  // Replace with actual chip ID
    .default_frequency_mhz = 600,  // Set appropriate default
    .frequency_options = TREASURE_FREQUENCY_OPTIONS,
    .default_voltage_mv = 1200,  // Set appropriate default
    .voltage_options = TREASURE_VOLTAGE_OPTIONS,
    .difficulty = 256,
    .core_count = 128,  // Replace with actual core count
    .small_core_count = 2048,  // Replace with actual small core count
    .hash_domains = 4,  // Number of hash domains (0 if none)
    .hashrate_test_percentage_target = 0.85,
};

// Add to default ASIC configs array
static const AsicConfig default_asic_configs[] = {
    ASIC_BM1397,
    ASIC_BM1366,
    ASIC_BM1368,
    ASIC_BM1370,
    ASIC_TREASURE,  // Add here
};

// Define a new board family for Treasure
typedef enum
{
    MAX,
    ULTRA,
    HEX,
    SUPRA,
    GAMMA,
    GAMMA_TURBO,
    AURADINE_TREASURE,  // New family
} Family;

static const FamilyConfig FAMILY_AURADINE_TREASURE = {
    .id = AURADINE_TREASURE,
    .name = "Auradine",
    .asic = ASIC_TREASURE,
    .asic_count = 1,  // Adjust based on your board
    .max_power = 50,  // Adjust based on power requirements
    .power_offset = 10,
    .nominal_voltage = 12,
    .voltage_domains = 1,
    .swarm_color = "yellow",
};

static const FamilyConfig default_families[] = {
    FAMILY_MAX,
    FAMILY_ULTRA,
    FAMILY_HEX,
    FAMILY_SUPRA,
    FAMILY_GAMMA,
    FAMILY_GAMMA_TURBO,
    FAMILY_AURADINE_TREASURE,  // Add here
};

// Add board version configuration (use next available version number)
static const DeviceConfig default_configs[] = {
    // ... existing configs ...
    {
        .board_version = "900",  // New board version
        .family = FAMILY_AURADINE_TREASURE,
        .EMC2101 = true,  // Set based on your board's thermal sensor
        .TPS546 = true,   // Set based on your board's voltage regulator
        .power_consumption_target = 40,  // Adjust as needed
    },
};
```

### Step 5: Integrate with ASIC Abstraction Layer

**`/components/asic/asic.c`**:

Add includes and case statements:

```c
#include "treasure.h"  // Add include

uint8_t ASIC_init(float frequency, uint16_t asic_count, uint16_t difficulty, Asic chip)
{
    switch (chip) {
        case BM1397:
            return BM1397_init(frequency, asic_count, difficulty);
        case BM1366:
            return BM1366_init(frequency, asic_count, difficulty);
        case BM1368:
            return BM1368_init(frequency, asic_count, difficulty);
        case BM1370:
            return BM1370_init(frequency, asic_count, difficulty);
        case TREASURE:  // Add new case
            return TREASURE_init(frequency, asic_count, difficulty);
        default:
            return 0;
    }
}

void ASIC_send_work(void * GLOBAL_STATE, bm_job * next_bm_job, Asic chip)
{
    switch (chip) {
        case BM1397:
            BM1397_send_work(GLOBAL_STATE, next_bm_job);
            break;
        case BM1366:
            BM1366_send_work(GLOBAL_STATE, next_bm_job);
            break;
        case BM1368:
            BM1368_send_work(GLOBAL_STATE, next_bm_job);
            break;
        case BM1370:
            BM1370_send_work(GLOBAL_STATE, next_bm_job);
            break;
        case TREASURE:  // Add new case
            TREASURE_send_work(GLOBAL_STATE, next_bm_job);
            break;
    }
}

task_result * ASIC_process_work(void * GLOBAL_STATE, Asic chip)
{
    switch (chip) {
        case BM1397:
            return BM1397_process_work(GLOBAL_STATE);
        case BM1366:
            return BM1366_process_work(GLOBAL_STATE);
        case BM1368:
            return BM1368_process_work(GLOBAL_STATE);
        case BM1370:
            return BM1370_process_work(GLOBAL_STATE);
        case TREASURE:  // Add new case
            return TREASURE_process_work(GLOBAL_STATE);
        default:
            return NULL;
    }
}

// Add similar cases for other ASIC functions:
// ASIC_set_frequency(), ASIC_read_registers(), etc.
```

### Step 6: Update CMakeLists.txt

**`/components/asic/CMakeLists.txt`**:

```cmake
idf_component_register(
    SRCS
        "asic.c"
        "bm1397.c"
        "bm1366.c"
        "bm1368.c"
        "bm1370.c"
        "treasure.c"  # Add new source file
        "common.c"
        "serial.c"
        "pll.c"
        "frequency_transition_bmXX.c"
        "stratum.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_timer
        driver
        nvs_flash
)
```

---

## Communication Protocol Details

### UART Configuration

The ESP32 communicates with ASIC chips via **UART1**:

| Parameter | Value | Note |
|-----------|-------|------|
| **UART Port** | UART_NUM_1 | Hardware UART1 |
| **TX Pin** | GPIO 17 | Transmit to ASIC |
| **RX Pin** | GPIO 18 | Receive from ASIC |
| **Initial Baud Rate** | 115,200 | Standard initialization rate |
| **Max Baud Rate** | 1,000,000+ | Varies by ASIC model |
| **Data Bits** | 8 | |
| **Parity** | None | |
| **Stop Bits** | 1 | |
| **Flow Control** | Disabled | |
| **Buffer Size** | 2048 bytes | TX and RX each |

**Key Files:**
- `/components/asic/serial.c`: UART management
- `/components/asic/include/serial.h`: Serial interface

### Packet Structure

All ASIC communication uses a packet-based protocol with the following structure:

```
┌──────────┬────────┬────────┬────────────────┬─────────┐
│ Preamble │ Header │ Length │     Data       │   CRC   │
│  2 bytes │ 1 byte │ 1 byte │  N bytes       │ 1-2 bytes│
└──────────┴────────┴────────┴────────────────┴─────────┘
   0x55AA                                      CRC5 or CRC16
```

#### Packet Types

1. **JOB_PACKET (0x20)**: Sends mining work to ASIC
   - Contains job_id, midstates, nonce range, difficulty
   - CRC: CRC16 (2 bytes)

2. **CMD_PACKET (0x40)**: Register read/write commands
   - Contains register address and data
   - CRC: CRC5 (1 byte)

#### Header Byte Format

```
Bit 7-6: Packet Type
         00 = Reserved
         01 = JOB_PACKET (0x20)
         10 = CMD_PACKET (0x40)
         11 = Reserved

Bit 4:   Group Addressing
         0 = GROUP_SINGLE (0x00) - single chip
         1 = GROUP_ALL (0x10) - broadcast to all

Bit 3-0: Command Type
         0000 = CMD_SETADDRESS (0x00)
         0001 = CMD_WRITE (0x01)
         0010 = CMD_READ (0x02)
         0011 = CMD_INACTIVE (0x03)
```

**Examples:**
- `0x51` = TYPE_CMD | GROUP_ALL | CMD_WRITE (broadcast write)
- `0x41` = TYPE_CMD | GROUP_SINGLE | CMD_WRITE (single chip write)
- `0x52` = TYPE_CMD | GROUP_ALL | CMD_READ (broadcast read)

### Initialization Sequence

When the ESP32 initializes an ASIC, it follows this sequence:

1. **Hardware Reset**
   - Assert ASIC_RESET (GPIO 1) low for 100ms
   - Release to high to bring ASIC out of reset
   - Wait 100ms for ASIC to stabilize

2. **UART Initialization**
   - Configure UART1 at 115,200 baud
   - Clear any pending data in buffers

3. **Chip Detection**
   - Send broadcast read command to chip ID register (0x00)
   - Count responses to determine number of chips in chain
   - Each chip responds with its chip ID (e.g., 0x1397, 0x1366, 0x1368, 0x1370)

4. **Set All Chips Inactive**
   - Send INACTIVE command to all chips
   - Prevents chips from responding until addressed

5. **Chip Addressing**
   - Divide 256-address space evenly among detected chips
   - Send SETADDRESS command to each chip sequentially
   - Example: 4 chips → addresses 0x00, 0x40, 0x80, 0xC0

6. **Register Configuration**
   - Write misc control register (0x18)
   - Configure core register control (0x3C)
   - Set difficulty mask register (0x14)
   - Configure I/O driver strength (0x58)
   - Set PLL parameters for frequency (0x08)

7. **Version Mask Setup** (if version rolling supported)
   - Configure version rolling register (0xA4)
   - Enables ASIC to test multiple block versions

8. **Baud Rate Upgrade**
   - Configure ASIC UART speed register (0x28)
   - Switch ESP32 UART to matching baud rate
   - Clear buffers after baud change

9. **Validation**
   - Read back registers to confirm configuration
   - Send test job packet
   - Wait for response to verify communication

**Reference Implementation:** `/main/power/asic_init.c`

### Register Map

ASIC chips use memory-mapped registers accessed via CMD_READ and CMD_WRITE:

| Register | Address | Purpose | Access |
|----------|---------|---------|--------|
| **Chip ID** | 0x00 | Contains chip model identification | Read-only |
| **PLL Parameter** | 0x08 | PLL frequency configuration | R/W |
| **Misc Control** | 0x18 | General control settings | R/W |
| **Difficulty Mask** | 0x14 | Mining difficulty target | Write |
| **UART Config** | 0x28 | UART baud rate settings | R/W |
| **Core Control** | 0x3C | Core voltage and enable | R/W |
| **Error Count** | 0x4C | Hardware error counter | Read-only |
| **I/O Strength** | 0x58 | Driver strength control | R/W |
| **Domain 0 Count** | 0x88 | Hash counter for domain 0 | Read-only |
| **Domain 1 Count** | 0x89 | Hash counter for domain 1 | Read-only |
| **Domain 2 Count** | 0x8A | Hash counter for domain 2 | Read-only |
| **Domain 3 Count** | 0x8B | Hash counter for domain 3 | Read-only |
| **Total Count** | 0x8C | Total hash counter | Read-only |
| **Version Rolling** | 0xA4 | Version mask configuration | R/W |

**Note**: Actual register addresses will vary for Treasure ASIC. Consult the Auradine datasheet.

---

## Hardware Integration

### ESP32 GPIO Pinout

| GPIO | Function | Direction | Description |
|------|----------|-----------|-------------|
| **1** | ASIC_RESET | Output | Active-low reset signal to ASIC |
| **10** | ASIC_ENABLE | Output | Enable signal for ASIC core voltage regulator |
| **17** | UART1_TX | Output | UART transmit to ASIC |
| **18** | UART1_RX | Input | UART receive from ASIC |
| **47** | I2C_SDA | Bidir | I2C data line for peripherals |
| **48** | I2C_SCL | Output | I2C clock line for peripherals |
| **0** | BOOT_BUTTON | Input | Boot mode selection (pullup) |
| **12** | PLUG_SENSE | Input | USB power detect (optional) |

**Configuration File:** `/main/Kconfig.projbuild`

### Reset and Enable Sequence

The ASIC requires proper power sequencing to avoid damage and ensure reliable operation:

```
Time →

Step 1: I2C Init
   └─> Initialize I2C bus for voltage regulator access

Step 2: Assert Reset (hold ASIC off)
   ASIC_RESET: ─────┐
                     └────────────────────────────
   └─> Minimizes ASIC power consumption during init

Step 3: ADC Init
   └─> Initialize analog input voltage monitoring

Step 4: Configure Voltage Regulator (via I2C)
   └─> Set target core voltage (e.g., 1200mV)
   └─> Enable regulator output

Step 5: Enable Core Power
   ASIC_ENABLE: ─────┐
                      └────────────────────────────
   └─> Turns on voltage regulator via enable pin

Step 6: Wait for Power Good (50ms)
   └─> Allow voltage to stabilize

Step 7: Release Reset
   ASIC_RESET: ──────────────────┐
                                  └─────────────────
   └─> ASIC begins internal power-on self-test

Step 8: Wait for ASIC Ready (100ms)
   └─> ASIC initializes internal logic

Step 9: Begin UART Communication
   └─> Send chip detection commands
```

**Implementation:** `/main/power/asic_init.c`, `/main/power/asic_reset.c`

### Power Considerations

#### Voltage Regulators

The ESP-Miner supports two types of voltage regulators via I2C:

##### 1. DS4432U+ (Older Boards)

**I2C Address:** 0x48
**Type:** 4-channel current DAC
**Usage:** Controls external TPS40305 buck converter via analog feedback

**Configuration:**
- Output voltage: 0.046V - 2.39V range
- Resolution: 7-bit DAC (0-127)
- Transfer function compensates for resistor divider (RA=4750Ω, RB=3320Ω)

**File:** `/main/power/DS4432U.c`

##### 2. TPS546 (Newer Boards)

**I2C Address:** 0x24
**Type:** PMBus-compatible synchronous buck converter
**Usage:** Direct digital voltage control

**Configuration:**
- Output voltage: 1.0V - 4.5V (board-dependent)
- Current limit: 25A or 50A (board-dependent)
- Built-in protections: OV, UV, OC, OT
- Hiccup retry mode for fault recovery

**Key Registers (PMBus Commands):**
- `VOUT_COMMAND`: Set output voltage
- `IOUT_OC_FAULT_LIMIT`: Set overcurrent threshold
- `STATUS_WORD`: Read fault status
- `CLEAR_FAULTS`: Clear latched faults

**Files:** `/main/power/TPS546.c`, `/main/power/TPS546.h`

#### Voltage Domains

Boards with multiple ASICs may use multiple voltage domains:

- **Single-chip boards**: 1 domain, direct voltage control
- **Hex (303)**: 3 domains, 2 chips per domain in series
  - Effective voltage per chip = Domain voltage / 2
- **Gamma Turbo (800x)**: 1 domain, 2 chips in parallel

**Implementation:** `/main/power/vcore.c`

#### Power Monitoring

**INA260** (I2C address: 0x40)
- Measures: Bus voltage (mV), current (mA), power (mW)
- Used for: Efficiency calculations, power limiting
- Polling rate: 1 Hz (power management task)

**File:** `/main/power/INA260.c`

### I2C Bus Architecture

```
ESP32 (I2C Master @ 400 kHz)
    │
    ├─ SDA (GPIO 47) ────┬─ DS4432U (0x48) or TPS546 (0x24) [Voltage]
    │                    │
    ├─ SCL (GPIO 48) ────┼─ EMC2101 (0x4C) [Temp + Fan]
                         │  or EMC2103 (0x4C) [2x Temp + Fan]
                         │  or EMC2302 (0x2E) [2x Temp + Fan]
                         │
                         ├─ INA260 (0x40) [Power Monitor]
                         │
                         └─ TMP1075 (optional, 0x48) [Extra Temp Sensor]
```

**Note:** I2C address conflicts are resolved by board design (not all sensors present on all boards).

**Configuration:** `/main/i2c_bitaxe.c`

---

## Power Management

### Dynamic Frequency and Voltage Scaling (DVFS)

The power management task (`/main/tasks/power_management_task.c`) continuously monitors and adjusts ASIC operating parameters:

#### Monitoring Loop (1 Hz)

```c
while (1) {
    // 1. Read power consumption
    float power_watts = INA260_get_power();

    // 2. Read ASIC temperature
    float asic_temp_c = EMC2101_get_external_temp();

    // 3. Check for thermal throttling
    if (asic_temp_c > OVERHEAT_THRESHOLD) {
        GLOBAL_STATE.overheat_mode = true;
        // Reduce frequency by 10 MHz
        target_frequency -= 10;
        ASIC_set_frequency(target_frequency);
    }

    // 4. Check voltage regulator faults
    if (TPS546_check_status() != ESP_OK) {
        // Handle fault (clear, retry, or shutdown)
        TPS546_clear_faults();
    }

    // 5. Auto-tune power consumption
    if (power_watts > target_power) {
        // Reduce voltage or frequency
    } else if (power_watts < target_power - margin) {
        // Increase voltage or frequency
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);  // 1 second
}
```

#### Voltage Adjustment

```c
esp_err_t VCORE_set_voltage(float core_voltage_mv, GlobalState * GLOBAL_STATE)
{
    // 1. Enable ASIC power if needed
    if (ASIC_ENABLE_PIN supported) {
        gpio_set_level(GPIO_ASIC_ENABLE, 1);
    }

    // 2. Set voltage via regulator
    if (using DS4432U) {
        DS4432U_set_vcore(core_voltage_mv);
    } else if (using TPS546) {
        TPS546_set_vout(core_voltage_mv / 1000.0);  // Convert mV to V
    }

    // 3. Calculate effective voltage for multi-chip domains
    if (voltage_domains > 1) {
        // Hex board: 2 chips in series per domain
        effective_voltage = core_voltage_mv / 2;
    }

    // 4. Wait for voltage to stabilize
    vTaskDelay(50 / portTICK_PERIOD_MS);

    return ESP_OK;
}
```

**File:** `/main/power/vcore.c`

### Frequency Transition (Runtime)

Modern ASICs (BM1366/68/70) support runtime frequency changes without full reinitialization:

```c
void ASIC_set_frequency(float new_frequency_mhz)
{
    // 1. Calculate new PLL parameters
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float actual_freq;
    pll_get_parameters(new_frequency_mhz, min_fb, max_fb,
                       &fb_divider, &refdiv, &postdiv1, &postdiv2, &actual_freq);

    // 2. Send frequency update command
    BMXXXX_send_hash_frequency(actual_freq);

    // 3. Wait for PLL to lock
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // 4. Clear any pending work results
    SERIAL_clear_buffer();
}
```

**Note:** BM1397 does NOT support runtime frequency changes. It requires full reinitialization.

**Files:** `/components/asic/frequency_transition_bmXX.c`, `/components/asic/pll.c`

---

## Thermal Management

### Temperature Sensors

#### EMC2101 (Single Channel)

**I2C Address:** 0x4C
**Features:**
- External temperature diode monitoring (ASIC temperature)
- Internal die temperature
- Single PWM fan output
- Configurable ideality factor and beta compensation

**Gamma Board Calibration:**
```c
EMC2101_init(0x24,  // ideality_factor (0x24 for Gamma ASIC diode)
             0x00); // beta_compensation
```

**File:** `/main/thermal/EMC2101.c`

#### EMC2103 (Dual Channel - Gamma Turbo)

**I2C Address:** 0x4C
**Features:**
- 2x external temperature sensors (one per ASIC)
- Single PWM fan output
- Average or max temperature mode

**File:** `/main/thermal/EMC2103.c`

#### EMC2302 (Dual Fan - Hex)

**I2C Address:** 0x2E
**Features:**
- External temperature monitoring
- 2x PWM fan outputs
- RPM feedback

**File:** `/main/thermal/EMC2302.c`

#### TMP1075 (Auxiliary - Hex)

**I2C Address:** 0x48
**Features:**
- High-accuracy temperature sensor
- Used as supplementary sensor on Hex boards

**File:** `/main/thermal/TMP1075.c`

### Fan Control

#### PWM Configuration

```c
// Fan speed: 0-100%
void EMC2101_set_fan_speed(uint8_t speed_percent)
{
    // Convert percentage to PWM duty cycle (0-63)
    uint8_t pwm_duty = (speed_percent * 63) / 100;

    // Write to fan setting register
    i2c_bitaxe_register_write_byte(emc_dev_handle, 0x4C, pwm_duty);
}

// Read fan RPM
uint16_t EMC2101_get_fan_speed(void)
{
    uint8_t tach_reading[2];
    i2c_bitaxe_register_read(emc_dev_handle, 0x46, tach_reading, 2);

    uint16_t tach_count = (tach_reading[0] << 8) | tach_reading[1];

    // RPM = 5,400,000 / tach_count (for 2-pole fan)
    return 5400000 / tach_count;
}
```

#### PID Controller

For auto-fan mode, a PID controller maintains target temperature:

```c
typedef struct {
    float Kp;  // Proportional gain
    float Ki;  // Integral gain
    float Kd;  // Derivative gain
    float integral;
    float prev_error;
} PID_t;

float PID_update(PID_t * pid, float setpoint, float measured_value)
{
    float error = setpoint - measured_value;

    // Proportional term
    float P_out = pid->Kp * error;

    // Integral term (accumulated error)
    pid->integral += error;
    float I_out = pid->Ki * pid->integral;

    // Derivative term (rate of change)
    float derivative = error - pid->prev_error;
    float D_out = pid->Kd * derivative;

    pid->prev_error = error;

    // Calculate output
    float output = P_out + I_out + D_out;

    // Clamp to 0-100% fan speed
    if (output > 100) output = 100;
    if (output < 0) output = 0;

    return output;
}
```

**Usage:**
```c
// Initialize PID with tuned parameters
PID_t fan_pid = {
    .Kp = 2.0,   // Proportional gain
    .Ki = 0.5,   // Integral gain
    .Kd = 0.1,   // Derivative gain
};

// In control loop
float target_temp = 65.0;  // °C
float current_temp = EMC2101_get_external_temp();
float fan_speed = PID_update(&fan_pid, target_temp, current_temp);
EMC2101_set_fan_speed((uint8_t)fan_speed);
```

**File:** `/main/thermal/PID.c`

### Overheat Protection

The system implements multi-level thermal protection:

```c
#define TEMP_WARN_THRESHOLD   75.0  // °C - Start reducing frequency
#define TEMP_CRITICAL_THRESHOLD 85.0  // °C - Emergency shutdown

if (asic_temp > TEMP_CRITICAL_THRESHOLD) {
    // Emergency: Disable ASIC immediately
    gpio_set_level(GPIO_ASIC_ENABLE, 0);
    ESP_LOGE(TAG, "CRITICAL TEMP: Emergency shutdown!");

} else if (asic_temp > TEMP_WARN_THRESHOLD) {
    // Warning: Enter overheat mode
    if (!GLOBAL_STATE.overheat_mode) {
        GLOBAL_STATE.overheat_mode = true;
        ESP_LOGW(TAG, "Overheat detected, throttling frequency");
    }

    // Reduce frequency by 10 MHz per second
    target_frequency -= 10;
    ASIC_set_frequency(target_frequency);

    // Set fan to 100%
    EMC2101_set_fan_speed(100);

} else if (GLOBAL_STATE.overheat_mode && asic_temp < (TEMP_WARN_THRESHOLD - 5.0)) {
    // Hysteresis: Exit overheat mode
    GLOBAL_STATE.overheat_mode = false;
    ESP_LOGI(TAG, "Temperature normalized, resuming normal operation");
}
```

---

## Testing and Validation

### Self-Test Mode

The firmware includes an optional hardware self-test that runs on boot:

**Enable in NVS configuration:**
```c
nvs_config_set_u16(NVS_CONFIG_SELF_TEST, 1);
```

**Test Sequence** (`/main/self_test/self_test.c`):

1. **I2C Device Detection**
   - Scan I2C bus for expected devices
   - Verify voltage regulator, temp sensor, power monitor presence
   - Report missing devices

2. **Voltage Regulator Test**
   - Set voltage to minimum
   - Read back and verify
   - Set to default voltage
   - Verify stable output

3. **Temperature Sensor Test**
   - Read ambient temperature
   - Verify reasonable range (0-50°C)
   - Check for sensor faults

4. **Fan Test**
   - Set fan to 50%
   - Read RPM feedback
   - Verify fan is spinning

5. **Power Monitor Test**
   - Read input voltage
   - Verify within expected range (5V or 12V ±10%)

6. **GPIO Test**
   - Toggle ASIC_RESET
   - Toggle ASIC_ENABLE
   - Verify control signals working

**Output:** Test results logged via UART, failures prevent boot.

### ASIC Communication Verification

After initialization, verify ASIC communication:

```c
// 1. Send test job
bm_job test_job = { /* populate with test data */ };
ASIC_send_work(&GLOBAL_STATE, &test_job);

// 2. Read registers
ASIC_read_registers();

// 3. Wait for response
task_result * result = ASIC_process_work(&GLOBAL_STATE);

if (result != NULL) {
    if (result->register_type == REGISTER_TOTAL_COUNT) {
        ESP_LOGI(TAG, "ASIC communication verified!");
        ESP_LOGI(TAG, "Hash count: %u", result->value);
    }
} else {
    ESP_LOGE(TAG, "ASIC communication FAILED!");
}
```

### Hashrate Validation

The hashrate monitor task validates ASIC performance:

**Expected Hashrate Calculation:**
```c
// Expected hashrate = cores × frequency × efficiency
float expected_hashrate_ghs = (core_count + small_core_count)
                               * frequency_mhz
                               * 1e-3  // Convert MHz to GHz
                               * efficiency_factor;  // Typically 0.85-0.90
```

**Actual Hashrate Measurement:**
```c
// Count valid nonces over time period
uint32_t nonces_found = 0;
uint32_t duration_seconds = 60;

// After 60 seconds:
float actual_hashrate_ghs = (nonces_found * difficulty * 2^32)
                             / (duration_seconds * 1e9);
```

**Validation:**
```c
float hashrate_ratio = actual_hashrate_ghs / expected_hashrate_ghs;

if (hashrate_ratio < 0.7) {
    ESP_LOGW(TAG, "Hashrate below target: %.2f%%", hashrate_ratio * 100);
    // Possible issues: voltage too low, thermal throttling, ASIC defect
} else {
    ESP_LOGI(TAG, "Hashrate validated: %.2f Gh/s (%.1f%% of expected)",
             actual_hashrate_ghs, hashrate_ratio * 100);
}
```

**File:** `/main/tasks/hashrate_monitor_task.c`

### Debugging Tools

#### Serial Debug Output

Enable verbose logging for ASIC communication:

```c
// In treasure.h
#define TREASURE_SERIALTX_DEBUG true  // Log all transmitted packets
#define TREASURE_SERIALRX_DEBUG true  // Log all received packets
#define TREASURE_DEBUG_WORK true      // Log work distribution
#define TREASURE_DEBUG_JOBS true      // Log job details
```

Output format:
```
tx: 55 AA 51 09 00 08 50 28 19 08 1D
rx: AA 55 13 70 00 00 00 00 00 00 0F [0]
```

#### Register Dump

Periodically dump all ASIC registers:

```c
void TREASURE_dump_registers(void)
{
    uint8_t registers[] = {0x00, 0x08, 0x18, 0x3C, 0x4C, 0x88, 0x8C};

    for (int i = 0; i < sizeof(registers); i++) {
        uint8_t reg = registers[i];
        _send_TREASURE((TYPE_CMD | GROUP_ALL | CMD_READ),
                       (uint8_t[]){0x00, reg}, 2, true);

        vTaskDelay(50 / portTICK_PERIOD_MS);

        // Process response
        task_result * result = TREASURE_process_work(&GLOBAL_STATE);
        if (result && result->register_type != REGISTER_INVALID) {
            ESP_LOGI(TAG, "Register 0x%02X = 0x%08X", reg, result->value);
        }
    }
}
```

#### Web Interface (AxeOS)

The ESP-Miner includes a web-based configuration interface:

**Access:** `http://<device-ip>/`

**Available Data:**
- Real-time hashrate
- ASIC temperature
- Fan speed (RPM)
- Power consumption
- Voltage and frequency settings
- Pool connection status
- System logs

**REST API Endpoints:**
- `GET /api/system/info` - System information
- `GET /api/swarm` - Mining statistics
- `POST /api/system/restart` - Reboot device
- `POST /api/system/frequency` - Set frequency
- `POST /api/system/voltage` - Set voltage

**File:** `/main/http_server/`

---

## Hardware Design Considerations for New Board

When designing a board for the Treasure ASIC, consider:

### 1. Power Supply

- **Input Voltage**: 5V or 12V via USB-C or barrel jack
- **Core Voltage**: 0.8V - 1.3V (verify with Treasure datasheet)
- **Core Current**: Estimate: frequency (MHz) × core_count × 0.5mA
  - Example: 600 MHz × 2048 cores = ~60A peak
- **Regulator Selection**:
  - **TPS546** (recommended): 25A or 50A, PMBus control
  - **Alternative**: TPS40305 + DS4432U DAC
- **Decoupling**: 10x 100µF ceramic + 4x 470µF bulk near ASIC VDD
- **Voltage Domains**: Single domain for 1-2 chips, multiple domains for more

### 2. ASIC Communication

- **UART Interface**:
  - Connect ESP32 UART1_TX (GPIO 17) to ASIC RX pin
  - Connect ASIC TX pin to ESP32 UART1_RX (GPIO 18)
  - Add 100Ω series resistors on both lines for ESD protection
  - 3.3V signal level (check if Treasure requires level shifters)

- **Reset Line**:
  - Connect ESP32 GPIO 1 to ASIC RESET_N (active low)
  - Add 10kΩ pullup to 3.3V
  - Add 100nF capacitor to GND for debounce

### 3. Thermal Design

- **Temperature Sensor**:
  - **EMC2101**: Single ASIC, integrated fan control
  - **EMC2103**: Dual ASIC (Turbo variant)
  - Connect sensor diode to ASIC temperature output
  - Place sensor thermally coupled to ASIC heatsink

- **Cooling**:
  - Heatsink: >30°C/W thermal resistance for 40W ASIC
  - Fan: 40mm, 12V, 2-wire or 4-wire PWM
  - Airflow: Ensure direct path over ASIC and VRM

### 4. I2C Bus

Connect to ESP32 I2C bus (GPIO 47/48):
- Voltage regulator (TPS546 @ 0x24 or DS4432U @ 0x48)
- Temp sensor (EMC2101 @ 0x4C)
- Power monitor (INA260 @ 0x40)
- Add 2.2kΩ pullups to 3.3V on SDA and SCL

### 5. Clocking

- If ASIC requires external clock: 25 MHz crystal oscillator
- If PLL-based: Verify reference clock requirements in datasheet
- Keep clock traces short, differential if high-speed

### 6. PCB Layout Guidelines

- **Layer Stack**: 4-layer minimum (Signal/GND/Power/Signal)
- **Power Planes**: Dedicated planes for 3.3V, 5V/12V input, ASIC core voltage
- **Trace Width**:
  - Core voltage: 100 mils minimum (for 50A)
  - I2C: 10 mils
  - UART: 10 mils, keep < 6 inches
- **Ground**: Solid ground plane, no splits under ASIC
- **Thermal Vias**: Grid of vias under ASIC package to conduct heat to bottom layer

---

## Additional Resources

### Key Source Files Reference

| Component | File Path | Description |
|-----------|-----------|-------------|
| **Main Entry** | `/main/main.c` | Application entry point and initialization sequence |
| **ASIC Abstraction** | `/components/asic/asic.c` | Hardware abstraction layer for all ASIC types |
| **BM1397 Driver** | `/components/asic/bm1397.c` | Reference implementation for BM1397 |
| **BM1370 Driver** | `/components/asic/bm1370.c` | Latest generation reference (version rolling) |
| **Serial Comms** | `/components/asic/serial.c` | UART communication layer |
| **Device Config** | `/main/device_config.h` | Board and ASIC configuration definitions |
| **Power Management** | `/main/tasks/power_management_task.c` | DVFS and thermal control loop |
| **Voltage Control** | `/main/power/vcore.c` | Voltage regulator interface |
| **TPS546 Driver** | `/main/power/TPS546.c` | PMBus voltage regulator driver |
| **Thermal Sensor** | `/main/thermal/EMC2101.c` | Temperature monitoring and fan control |
| **ASIC Init** | `/main/power/asic_init.c` | Hardware initialization sequence |
| **Stratum Protocol** | `/components/stratum/stratum_api.c` | Mining pool communication |
| **Global State** | `/main/global_state.h` | System-wide state structure |

### ESP-IDF Documentation

- **UART Driver**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/uart.html
- **I2C Driver**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/i2c.html
- **GPIO**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/gpio.html
- **FreeRTOS**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/freertos.html

### Datasheets

When implementing the Treasure ASIC driver, you will need:

1. **Auradine Treasure ASIC Datasheet**
   - Chip ID value
   - Register map and bit definitions
   - UART protocol specification (packet format, CRC type)
   - PLL configuration parameters (VCO range, divider ranges)
   - Voltage and frequency operating ranges
   - Temperature sensor specifications
   - Initialization sequence

2. **Board Component Datasheets**
   - Voltage regulator (TPS546 or equivalent)
   - Temperature sensor (EMC2101 or equivalent)
   - Power monitor (INA260 or equivalent)

### Community Resources

- **Bitaxe GitHub**: https://github.com/skot/bitaxe
- **Discord**: Active community for Bitaxe hardware
- **BitcoinTalk Forum**: Mining hardware development discussions

---

## Troubleshooting Common Issues

### ASIC Not Detected

**Symptoms:** `chip_counter == 0` during initialization

**Checks:**
1. Verify UART connections (TX/RX not swapped)
2. Check ASIC_RESET signal (should be high after init)
3. Verify core voltage is present and stable
4. Check crystal oscillator is running (if required)
5. Enable SERIALRX_DEBUG to see if responses are received
6. Try lower baud rate (115200) if communication fails

### No Nonces Returned

**Symptoms:** ASIC communicates but produces no valid nonces

**Checks:**
1. Verify job packet format matches ASIC expectations
2. Check endianness of multi-byte fields (ntime, nbits, etc.)
3. Confirm difficulty mask is set correctly
4. Verify frequency is within ASIC operating range
5. Check core voltage is adequate for frequency
6. Read error count register - high value indicates hardware issues
7. Send simpler test job with known solution

### Thermal Runaway

**Symptoms:** Temperature continuously rises despite fan

**Checks:**
1. Verify fan is spinning (check RPM reading)
2. Check heatsink is properly mounted with thermal paste
3. Ensure airflow is not blocked
4. Reduce frequency or voltage temporarily
5. Check if overheat protection is activating (check logs)
6. Verify EMC2101 is configured correctly (ideality factor, beta)

### Voltage Regulator Faults

**Symptoms:** TPS546 status shows OV, UV, OC, or OT faults

**Checks:**
1. Read TPS546 status registers for fault details
2. Check input voltage is within range
3. Verify current limit is appropriate for ASIC
4. Check for short circuits on core voltage rail
5. Clear faults with `TPS546_clear_faults()`
6. Inspect hiccup retry count - repeated faults indicate hardware issue

### Hashrate Lower Than Expected

**Symptoms:** `hashrate_ratio < 0.8`

**Checks:**
1. Check if overheat_mode is active (thermal throttling)
2. Verify voltage is at target setting (measure with multimeter)
3. Check for high error count in ASIC registers
4. Confirm frequency setting took effect
5. Verify all chips in chain are responding
6. Check if voltage regulator is current-limiting
7. Test with lower frequency to isolate voltage vs. thermal issues

---

## Conclusion

This guide provides a comprehensive foundation for adding the Auradine Treasure ASIC to the ESP-Miner firmware. The modular architecture allows for clean integration of new ASIC types with minimal changes to existing code.

**Key Takeaways:**

1. **Follow the Pattern**: Use existing ASIC drivers (especially BM1370) as templates
2. **Read the Datasheet**: Critical specifications come from the ASIC datasheet
3. **Test Incrementally**: Verify each initialization step before proceeding
4. **Debug Thoroughly**: Enable serial debug output to visualize communication
5. **Validate Hardware**: Use self-test and register reads to confirm proper operation

**Next Steps:**

1. Obtain Treasure ASIC datasheet from Auradine
2. Create `treasure.c` and `treasure.h` based on templates above
3. Update `device_config.h` with Treasure specifications
4. Integrate with ASIC abstraction layer (`asic.c`)
5. Test with development board
6. Optimize frequency, voltage, and difficulty settings
7. Validate hashrate and thermal performance

For questions or assistance, consult the Bitaxe community or ESP-Miner GitHub repository.

**Good luck with your implementation!**
