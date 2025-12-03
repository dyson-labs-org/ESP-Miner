#include "auradine.h"

#include "crc.h"
#include "global_state.h"
#include "serial.h"
#include "utils.h"
#include "../../main/power/asic_reset.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "frequency_transition_bmXX.h"
#include "pll.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define AURADINE_CHIP_ID 0xAD00
#define AURADINE_CHIP_ID_RESPONSE_LENGTH 11

#define TYPE_JOB 0x20
#define TYPE_CMD 0x40

#define GROUP_SINGLE 0x00
#define GROUP_ALL 0x10

#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03

// Known register addresses (from BM1370 - may be wrong for Auradine!)
#define AURADINE_CHIP_ID_REG 0x00
#define MISC_CONTROL 0x18
#define FAST_UART_CONFIGURATION 0x28

// TODO: Discover actual hex addresses for these Auradine registers:
// - PLL_CONFIG (PLL configuration)
// - PLL_FREQ (PLL frequency setting)
// - VCO (voltage-controlled oscillator)
// Currently using 0x08 from BM1370 as a guess

static const register_type_t REGISTER_MAP[] = {
    [0x4C] = REGISTER_ERROR_COUNT,
    [0x88] = REGISTER_DOMAIN_0_COUNT,
    [0x89] = REGISTER_DOMAIN_1_COUNT,
    [0x8A] = REGISTER_DOMAIN_2_COUNT,
    [0x8B] = REGISTER_DOMAIN_3_COUNT,
    [0x8C] = REGISTER_TOTAL_COUNT
};

typedef struct __attribute__((__packed__))
{
    uint32_t nonce;
    uint8_t midstate_num;
    uint8_t id;
    uint16_t version;
} auradine_asic_result_job_t;

typedef struct __attribute__((__packed__))
{
    uint32_t value;
    uint8_t asic_address;
    uint8_t register_address;
    uint16_t                  : 16;
} auradine_asic_result_cmd_t;

typedef struct __attribute__((__packed__))
{
    uint16_t preamble;
    union {
        auradine_asic_result_job_t job;
        auradine_asic_result_cmd_t cmd;
    };
    uint8_t crc             : 5;
    uint8_t                 : 2;
    uint8_t is_job_response : 1;
} auradine_asic_result_t;

static const char * TAG = "auradine";

static task_result result;

static int address_interval;

static void _send_AURADINE(uint8_t header, const uint8_t * data, uint8_t data_len, bool debug)
{
    packet_type_t packet_type = (header & TYPE_JOB) ? JOB_PACKET : CMD_PACKET;
    const uint8_t total_length = (packet_type == JOB_PACKET) ? (data_len + 6) : (data_len + 5);

    uint8_t buf[total_length];

    buf[0] = 0x55;
    buf[1] = 0xAA;

    buf[2] = header;

    buf[3] = (packet_type == JOB_PACKET) ? (data_len + 4) : (data_len + 3);

    memcpy(buf + 4, data, data_len);

    if (packet_type == JOB_PACKET) {
        uint16_t crc16_total = crc16_false(buf + 2, data_len + 2);
        buf[4 + data_len] = (crc16_total >> 8) & 0xFF;
        buf[5 + data_len] = crc16_total & 0xFF;
    } else {
        buf[4 + data_len] = crc5(buf + 2, data_len + 2);
    }

    if (SERIAL_send(buf, total_length, debug) == 0) {
        ESP_LOGE(TAG, "Failed to send data to Auradine ASIC");
    }
}

static void _send_chain_inactive(void)
{
    unsigned char read_address[] = {0x00, 0x00};
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_INACTIVE), read_address, 2, AURADINE_SERIALTX_DEBUG);
}

static void _set_chip_address(uint8_t chipAddr)
{
    unsigned char read_address[] = {chipAddr, 0x00};
    _send_AURADINE((TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS), read_address, 2, AURADINE_SERIALTX_DEBUG);
}

void AURADINE_TREASURE_set_version_mask(uint32_t version_mask)
{
    int versions_to_roll = version_mask >> 13;
    uint8_t version_byte0 = (versions_to_roll >> 8);
    uint8_t version_byte1 = (versions_to_roll & 0xFF);
    uint8_t version_cmd[] = {0x00, 0xA4, 0x90, 0x00, version_byte0, version_byte1};
    _send_AURADINE(TYPE_CMD | GROUP_ALL | CMD_WRITE, version_cmd, 6, AURADINE_SERIALTX_DEBUG);
}

void AURADINE_TREASURE_send_hash_frequency(float target_freq)
{
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float frequency;

    pll_get_parameters(target_freq, 160, 239, &fb_divider, &refdiv, &postdiv1, &postdiv2, &frequency);

    uint8_t vdo_scale = (fb_divider * FREQ_MULT / refdiv >= 2400) ? 0x50 : 0x40;
    uint8_t postdiv = (((postdiv1 - 1) & 0xf) << 4) | ((postdiv2 - 1) & 0xf);

    // NOTE: Register 0x08 is a GUESS from BM1370!
    // Auradine has PLL_CONFIG, PLL_FREQ, and VCO registers but we don't know their addresses yet.
    // This might be PLL_FREQ, PLL_CONFIG, or VCO - will discover via register scan.
    uint8_t freqbuf[6] = {0x00, 0x08, vdo_scale, fb_divider, refdiv, postdiv};

    _send_AURADINE(TYPE_CMD | GROUP_ALL | CMD_WRITE, freqbuf, 6, AURADINE_SERIALTX_DEBUG);

    ESP_LOGI(TAG, "Setting Frequency to %g MHz (%g) [Register 0x08 - may be wrong!]", target_freq, frequency);
}

uint8_t AURADINE_TREASURE_init(float frequency, uint16_t asic_count, uint16_t difficulty)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "   AURADINE TREASURE INITIALIZATION");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "Expected CHIP_ID: 0x%04X (MAY BE WRONG!)", AURADINE_CHIP_ID);
    ESP_LOGI(TAG, "Expected response length: %d bytes (MAY BE WRONG!)", AURADINE_CHIP_ID_RESPONSE_LENGTH);
    ESP_LOGW(TAG, "Auradine driver is a SKELETON - actual chip communication must be implemented from datasheet");
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "Step 1: Setting version mask...");
    for (int i = 0; i < 3; i++) {
        AURADINE_TREASURE_set_version_mask(STRATUM_DEFAULT_VERSION_MASK);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "Step 2: Discovering chip address...");
    ESP_LOGI(TAG, "  CRITICAL: Chips may not respond to address 0x00 by default!");
    ESP_LOGI(TAG, "  Testing multiple addresses to find which one works...");
    AURADINE_TREASURE_scan_chip_addresses(150);

    ESP_LOGI(TAG, "Step 3: Sending CHIP_ID read command...");
    ESP_LOGI(TAG, "  Register: 0x%02X", AURADINE_CHIP_ID_REG);
    ESP_LOGI(TAG, "  Using address 0x00 (update if chip responds to different address!)");
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_READ), (uint8_t[]){0x00, AURADINE_CHIP_ID_REG}, 2, true);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Step 4: Detecting chips...");
    ESP_LOGI(TAG, "  Watch for CHIP_ID mismatch warnings below!");
    ESP_LOGI(TAG, "  The actual chip ID will be in the hex dump.");
    int chip_counter = count_asic_chips(asic_count, AURADINE_CHIP_ID, AURADINE_CHIP_ID_RESPONSE_LENGTH);

    if (chip_counter == 0) {
        ESP_LOGW(TAG, "No Auradine chips detected - continuing anyway to program PLL and enable VDD_HASH");
        ESP_LOGW(TAG, "Per docs: PLL must be programmed BEFORE VDD_HASH is enabled");
        ESP_LOGW(TAG, "Assuming 2 chips for configuration purposes...");
        chip_counter = 2;  // Assume 2 chips to continue initialization
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Step 5: Discovering ASIC architecture...");
    ESP_LOGI(TAG, "  Hash engines (from datasheet): 238");
    ESP_LOGI(TAG, "  Frequency range: 1600-4800 MHz (MUST ramp in 10%% steps!)");
    ESP_LOGI(TAG, "  Target frequency: %d MHz", (int)frequency);
    ESP_LOGI(TAG, "  Expected hashrate: %.2f GH/s @ %d MHz",
             (frequency * 238 * chip_counter) / 1000.0, (int)frequency);
    ESP_LOGI(TAG, "  Hashrate at min (1600 MHz): %.2f GH/s", (1600 * 238 * chip_counter) / 1000.0);
    ESP_LOGI(TAG, "  Hashrate at max (4800 MHz): %.2f GH/s", (4800 * 238 * chip_counter) / 1000.0);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Step 6: Probing hash domain registers...");
    ESP_LOGI(TAG, "  Trying to read registers 0x88-0x8B (DOMAIN_0-3_COUNT)");
    ESP_LOGI(TAG, "  and 0x8C (TOTAL_COUNT)...");

    // Try reading domain registers to discover how many exist
    for (uint8_t reg = 0x88; reg <= 0x8C; reg++) {
        _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_READ), (uint8_t[]){0x00, reg}, 2, true);
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "  Watch RX debug output to see if hash domain registers respond");
    ESP_LOGI(TAG, "  If registers respond, hash_domains should be updated accordingly");
    ESP_LOGI(TAG, "");

    // OPTIONAL: Comprehensive register scan to discover Auradine register map
    // Uncomment this once you're getting RX responses from the chip
    // This will try reading ALL registers from 0x00 to 0xFF to see which ones exist
    #ifdef AURADINE_REGISTER_DISCOVERY
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Step 6b: Comprehensive register discovery scan...");
    ESP_LOGI(TAG, "  This will take a few minutes - scanning 0x00 to 0xFF");
    AURADINE_TREASURE_scan_registers(0x00, 0xFF, 100);
    #endif

    AURADINE_TREASURE_set_version_mask(STRATUM_DEFAULT_VERSION_MASK);

    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0xA8, 0x00, 0x07, 0x00, 0x00}, 6, AURADINE_SERIALTX_DEBUG);

    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x18, 0xF0, 0x00, 0xC1, 0x00}, 6, AURADINE_SERIALTX_DEBUG);

    _send_chain_inactive();

    address_interval = 256 / chip_counter;
    for (uint8_t i = 0; i < chip_counter; i++) {
        _set_chip_address(i * address_interval);
    }

    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x3C, 0x80, 0x00, 0x8B, 0x00}, 6, AURADINE_SERIALTX_DEBUG);

    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x3C, 0x80, 0x00, 0x80, 0x0C}, 6, AURADINE_SERIALTX_DEBUG);

    uint8_t difficulty_mask[6];
    get_difficulty_mask(difficulty, difficulty_mask);
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), difficulty_mask, 6, AURADINE_SERIALTX_DEBUG);

    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x58, 0x00, 0x01, 0x11, 0x11}, 6, AURADINE_SERIALTX_DEBUG);

    for (uint8_t i = 0; i < chip_counter; i++) {
        unsigned char set_a8_register[6] = {i * address_interval, 0xA8, 0x00, 0x07, 0x01, 0xF0};
        _send_AURADINE((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_a8_register, 6, AURADINE_SERIALTX_DEBUG);
        unsigned char set_18_register[6] = {i * address_interval, 0x18, 0xF0, 0x00, 0xC1, 0x00};
        _send_AURADINE((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_18_register, 6, AURADINE_SERIALTX_DEBUG);
        unsigned char set_3c_register_first[6] = {i * address_interval, 0x3C, 0x80, 0x00, 0x8B, 0x00};
        _send_AURADINE((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_first, 6, AURADINE_SERIALTX_DEBUG);
        unsigned char set_3c_register_second[6] = {i * address_interval, 0x3C, 0x80, 0x00, 0x80, 0x0C};
        _send_AURADINE((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_second, 6, AURADINE_SERIALTX_DEBUG);
        unsigned char set_3c_register_third[6] = {i * address_interval, 0x3C, 0x80, 0x00, 0x82, 0xAA};
        _send_AURADINE((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_third, 6, AURADINE_SERIALTX_DEBUG);
    }

    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0xB9, 0x00, 0x00, 0x44, 0x80}, 6, AURADINE_SERIALTX_DEBUG);
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x54, 0x00, 0x00, 0x00, 0x02}, 6, AURADINE_SERIALTX_DEBUG);
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0xB9, 0x00, 0x00, 0x44, 0x80}, 6, AURADINE_SERIALTX_DEBUG);
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x3C, 0x80, 0x00, 0x8D, 0xEE}, 6, AURADINE_SERIALTX_DEBUG);

    ESP_LOGI(TAG, "Setting hash frequency (with 10%% ramping for safety)...");
    do_frequency_transition_auradine(frequency, AURADINE_TREASURE_send_hash_frequency);

    unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x1E, 0xB5};
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), set_10_hash_counting, 6, AURADINE_SERIALTX_DEBUG);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Step 4: Enabling VDD_HASH (hash core power)...");
    if (vdd_hash_enable() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable VDD_HASH!");
        ESP_LOGW(TAG, "Continuing anyway, but hash cores may not be powered...");
    }

    ESP_LOGI(TAG, "Auradine ASIC initialization sequence complete");
    return chip_counter;
}

int AURADINE_TREASURE_set_default_baud(void)
{
    unsigned char baudrate[] = {0x00, MISC_CONTROL, 0x00, 0x00, 0b01111010, 0b00110001};
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), baudrate, 6, AURADINE_SERIALTX_DEBUG);
    return 115749;
}

int AURADINE_TREASURE_set_max_baud(void)
{
    ESP_LOGI(TAG, "Setting max baud of 1000000");

    unsigned char fast_uart[] = {0x00, FAST_UART_CONFIGURATION, 0x11, 0x30, 0x02, 0x00};
    _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_WRITE), fast_uart, 6, AURADINE_SERIALTX_DEBUG);
    return 1000000;
}

static uint8_t id = 0;

void AURADINE_TREASURE_send_work(void * pvParameters, bm_job * next_bm_job)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    AURADINE_job job;
    id = (id + 24) % 128;
    job.job_id = id;
    job.num_midstates = 0x01;
    memcpy(&job.starting_nonce, &next_bm_job->starting_nonce, 4);
    memcpy(&job.nbits, &next_bm_job->target, 4);
    memcpy(&job.ntime, &next_bm_job->ntime, 4);
    memcpy(job.merkle_root, next_bm_job->merkle_root, 32);
    memcpy(job.prev_block_hash, next_bm_job->prev_block_hash, 32);
    memcpy(&job.version, &next_bm_job->version, 4);

    if (GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id] != NULL) {
        free_bm_job(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id]);
    }

    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id] = next_bm_job;

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    GLOBAL_STATE->valid_jobs[job.job_id] = 1;
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

    #if AURADINE_DEBUG_JOBS
    ESP_LOGI(TAG, "Send Job: %02X", job.job_id);
    #endif

    _send_AURADINE((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), (uint8_t *)&job, sizeof(AURADINE_job), AURADINE_DEBUG_WORK);
}

task_result * AURADINE_TREASURE_process_work(void * pvParameters)
{
    auradine_asic_result_t asic_result = {0};

    memset(&result, 0, sizeof(task_result));

    if (receive_work((uint8_t *)&asic_result, sizeof(asic_result)) == ESP_FAIL) {
        return NULL;
    }

    if (!asic_result.is_job_response) {
        result.register_type = REGISTER_MAP[asic_result.cmd.register_address];
        if (result.register_type == REGISTER_INVALID) {
            ESP_LOGW(TAG, "Unknown register read: %02x", asic_result.cmd.register_address);
            return NULL;
        }
        result.asic_nr = asic_result.cmd.asic_address / address_interval;
        result.value = ntohl(asic_result.cmd.value);

        return &result;
    }

    uint8_t job_id = (asic_result.job.id & 0xf0) >> 1;
    uint32_t nonce_h = ntohl(asic_result.job.nonce);
    uint8_t asic_nr = (uint8_t)((nonce_h >> 17) & 0xff) / address_interval;
    uint8_t core_id = (uint8_t)((nonce_h >> 25) & 0x7f);
    uint8_t small_core_id = asic_result.job.id & 0x0f;
    uint32_t version_bits = (ntohs(asic_result.job.version) << 13);
    ESP_LOGI(TAG, "Job ID: %02X, Asic nr: %d, Core: %d/%d, Ver: %08" PRIX32, job_id, asic_nr, core_id, small_core_id, version_bits);

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    if (GLOBAL_STATE->valid_jobs[job_id] == 0) {
        ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
        return NULL;
    }

    uint32_t rolled_version = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->version | version_bits;

    result.job_id = job_id;
    result.nonce = asic_result.job.nonce;
    result.rolled_version = rolled_version;
    result.asic_nr = asic_nr;

    return &result;
}

void AURADINE_TREASURE_read_registers(void)
{
    int size = sizeof(REGISTER_MAP) / sizeof(REGISTER_MAP[0]);
    for (int reg = 0; reg < size; reg++) {
        if (REGISTER_MAP[reg] != REGISTER_INVALID) {
            _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_READ), (uint8_t[]){0x00, reg}, 2, AURADINE_SERIALTX_DEBUG);
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
    }
}

void AURADINE_TREASURE_scan_chip_addresses(uint16_t delay_ms)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          CHIP ADDRESS DISCOVERY                        ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "Board has 2 chips with fixed IDs:");
    ESP_LOGI(TAG, "  - Chip L2R: ID 0x00");
    ESP_LOGI(TAG, "  - Chip R2L: ID 0x80");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Testing these addresses plus common alternatives...");
    ESP_LOGI(TAG, "");

    // Known addresses plus common alternatives
    uint8_t test_addresses[] = {0x00, 0x80, 0x01, 0x02, 0x10, 0x20, 0x40, 0xFF};

    for (int i = 0; i < sizeof(test_addresses); i++) {
        uint8_t chip_addr = test_addresses[i];
        const char *label = "";
        if (chip_addr == 0x00) label = " (L2R chip)";
        else if (chip_addr == 0x80) label = " (R2L chip)";

        ESP_LOGI(TAG, "Trying chip address 0x%02X%s: Reading CHIP_ID register...", chip_addr, label);
        _send_AURADINE((TYPE_CMD | GROUP_SINGLE | CMD_READ), (uint8_t[]){chip_addr, 0x00}, 2, true);

        // EXPLICIT RX test - try to read response
        uint8_t rx_buf[32];
        ESP_LOGI(TAG, "  Waiting for RX response...");
        int rx_bytes = SERIAL_rx(rx_buf, sizeof(rx_buf), delay_ms);
        if (rx_bytes > 0) {
            ESP_LOGI(TAG, "  ✓ GOT RX RESPONSE! %d bytes", rx_bytes);
            ESP_LOG_BUFFER_HEX(TAG, rx_buf, rx_bytes);
        } else {
            ESP_LOGW(TAG, "  ✗ No RX response (timeout or 0 bytes)");
        }
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Chip address scan complete.");
    ESP_LOGI(TAG, "Expected: RX responses from 0x00 (L2R) and 0x80 (R2L)");
    ESP_LOGI(TAG, "");
}

void AURADINE_TREASURE_scan_registers(uint8_t start_addr, uint8_t end_addr, uint16_t delay_ms)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          REGISTER DISCOVERY SCAN                       ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "Scanning registers 0x%02X to 0x%02X", start_addr, end_addr);
    ESP_LOGI(TAG, "Using chip address 0x00 (broadcast)");
    ESP_LOGI(TAG, "Watch RX debug output for responses...");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Legend:");
    ESP_LOGI(TAG, "  - If you see RX data: Register exists and returned a value");
    ESP_LOGI(TAG, "  - If you see TIMEOUT: Register doesn't exist or doesn't respond");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "NOTE: If you get no responses, the chip might not respond to address 0x00!");
    ESP_LOGI(TAG, "      Run AURADINE_TREASURE_scan_chip_addresses() first to find the right address.");
    ESP_LOGI(TAG, "");

    for (uint8_t reg = start_addr; reg <= end_addr; reg++) {
        ESP_LOGI(TAG, "Reading register 0x%02X...", reg);
        _send_AURADINE((TYPE_CMD | GROUP_ALL | CMD_READ), (uint8_t[]){0x00, reg}, 2, true);

        // Give time for response
        vTaskDelay(delay_ms / portTICK_PERIOD_MS);

        // Note: Actual response detection happens in SERIAL_rx debug output
        // We're just spacing out the reads to make logs readable
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Register scan complete. Check RX logs above.");
    ESP_LOGI(TAG, "Registers that responded are candidates for the register map.");
    ESP_LOGI(TAG, "");
}
