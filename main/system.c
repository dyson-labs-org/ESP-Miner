#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"

#include "system.h"
#include "i2c_bitaxe.h"
#include "driver/i2c_master.h"
#include "INA260.h"
#include "adc.h"
#include "connect.h"
#include "nvs_config.h"
#include "display.h"
#include "input.h"
#include "screen.h"
#include "vcore.h"
#include "thermal.h"
#include "utils.h"
#include "power.h"
#include "esp_system.h"

static const char * TAG = "system";

//local function prototypes
static esp_err_t ensure_overheat_mode_config();

void SYSTEM_init_system(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->screen_page = 0;
    module->shares_accepted = 0;
    module->shares_rejected = 0;
    module->best_nonce_diff = nvs_config_get_u64(NVS_CONFIG_BEST_DIFF);
    module->best_session_nonce_diff = 0;
    module->start_time = esp_timer_get_time();
    module->lastClockSync = 0;
    module->block_found = false;
    
    // Initialize network address strings
    strcpy(module->ip_addr_str, "");
    strcpy(module->ipv6_addr_str, "");
    strcpy(module->wifi_status, "Initializing...");
    
    // set the pool url
    module->pool_url = nvs_config_get_string(NVS_CONFIG_STRATUM_URL);
    module->fallback_pool_url = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL);

    // set the pool port
    module->pool_port = nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT);
    module->fallback_pool_port = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT);

    // set the pool user
    module->pool_user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER);
    module->fallback_pool_user = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER);

    // set the pool password
    module->pool_pass = nvs_config_get_string(NVS_CONFIG_STRATUM_PASS);
    module->fallback_pool_pass = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_PASS);

    // set the pool difficulty
    module->pool_difficulty = nvs_config_get_u16(NVS_CONFIG_STRATUM_DIFFICULTY);
    module->fallback_pool_difficulty = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY);

    // set the pool extranonce subscribe
    module->pool_extranonce_subscribe = nvs_config_get_bool(NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE);
    module->fallback_pool_extranonce_subscribe = nvs_config_get_bool(NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE);

    // use fallback stratum
    module->use_fallback_stratum = nvs_config_get_bool(NVS_CONFIG_USE_FALLBACK_STRATUM);

    // set based on config
    module->is_using_fallback = module->use_fallback_stratum;

    // Initialize pool address family
    module->pool_addr_family = 0;

    // Initialize overheat_mode
    module->overheat_mode = nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);
    ESP_LOGI(TAG, "Initial overheat_mode value: %d", module->overheat_mode);

    //Initialize power_fault fault mode
    module->power_fault = 0;
    module->allow_core_voltage = false;

    // set the best diff string
    suffixString(module->best_nonce_diff, module->best_diff_string, DIFF_STRING_SIZE, 0);
    suffixString(module->best_session_nonce_diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);
}

// PMBus standard registers
#define PMBUS_MFR_ID          0x99
#define PMBUS_MFR_MODEL       0x9A
#define PMBUS_MFR_REVISION    0x9B
#define PMBUS_IC_DEVICE_ID    0xAD

// Helper to read a block from PMBus device
static esp_err_t pmbus_read_block(i2c_master_bus_handle_t bus, uint8_t addr, uint8_t reg, uint8_t* buf, size_t max_len) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    // PMBus block read: send register, receive length byte + data
    uint8_t len_byte;
    ret = i2c_master_transmit_receive(dev_handle, &reg, 1, &len_byte, 1, 100);
    if (ret == ESP_OK && len_byte > 0 && len_byte <= max_len) {
        ret = i2c_master_receive(dev_handle, buf, len_byte, 100);
        if (ret == ESP_OK) {
            buf[len_byte] = '\0';  // Null terminate for string data
        }
    }

    i2c_master_bus_rm_device(dev_handle);
    return ret;
}

// Try to identify a PMBus device
static void probe_pmbus_device(i2c_master_bus_handle_t bus, uint8_t addr) {
    uint8_t buf[32];

    ESP_LOGI(TAG, "    Probing PMBus registers...");

    // Read manufacturer ID
    if (pmbus_read_block(bus, addr, PMBUS_MFR_ID, buf, sizeof(buf) - 1) == ESP_OK) {
        ESP_LOGI(TAG, "      MFR_ID: %s", buf);
    }

    // Read model
    if (pmbus_read_block(bus, addr, PMBUS_MFR_MODEL, buf, sizeof(buf) - 1) == ESP_OK) {
        ESP_LOGI(TAG, "      MFR_MODEL: %s", buf);
    }

    // Read revision
    if (pmbus_read_block(bus, addr, PMBUS_MFR_REVISION, buf, sizeof(buf) - 1) == ESP_OK) {
        ESP_LOGI(TAG, "      MFR_REVISION: %s", buf);
    }
}

// Scan a single I2C bus
static void scan_i2c_bus(i2c_master_bus_handle_t bus, const char* bus_name, bool found_addresses[128]) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Scanning %s (0x08-0x77)...", bus_name);

    int found_count = 0;

    // Scan all valid I2C addresses
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        esp_err_t ret = i2c_master_probe(bus, addr, 50);
        if (ret == ESP_OK) {
            found_addresses[addr] = true;
            found_count++;

            // Identify known device types
            const char* device_type = "Unknown";
            bool is_pmbus = false;

            if (addr >= 0x20 && addr <= 0x27) {
                device_type = "PMBus Voltage Regulator (TPS546/TPSM861253)";
                is_pmbus = true;
            } else if (addr == 0x0C) {
                device_type = "SMBus Alert Response Address";
            } else if (addr == 0x2C) {
                device_type = "EMC2302 Fan Controller";
            } else if (addr == 0x2E) {
                device_type = "EMC2103 Fan/Temp Controller";
            } else if (addr == 0x40) {
                device_type = "INA260 Power Monitor";
                is_pmbus = true;
            } else if (addr == 0x48) {
                device_type = "TMP1075 Temperature Sensor";
            } else if (addr == 0x4C) {
                device_type = "EMC2101 Fan/Temp Controller";
            } else if (addr == 0x60) {
                device_type = "DS4432U DAC";
            }

            ESP_LOGI(TAG, "  [0x%02X] %s", addr, device_type);

            // Try to identify PMBus devices
            if (is_pmbus) {
                probe_pmbus_device(bus, addr);
            }
        }
    }

    if (found_count == 0) {
        ESP_LOGW(TAG, "  No devices found on this bus");
    } else {
        ESP_LOGI(TAG, "  Total: %d device(s)", found_count);
    }
}

static void SYSTEM_scan_i2c_bus(void) {
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          I2C BUS DISCOVERY                             ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");

    bool found_addresses[128] = {0};

    // Scan I2C Bus 0 (existing bus)
    i2c_master_bus_handle_t bus0;
    if (i2c_bitaxe_get_master_bus_handle(&bus0) == ESP_OK) {
        scan_i2c_bus(bus0, "I2C Bus 0 (GPIO47/48)", found_addresses);
    }

    // Try to initialize and scan I2C Bus 1
    // Optional Bus 1 probe only if pins are valid for this target
    const int bus1_scl = 21;
    const int bus1_sda = 22;
    if (GPIO_IS_VALID_GPIO(bus1_scl) && GPIO_IS_VALID_GPIO(bus1_sda)) {
        i2c_master_bus_config_t bus1_config = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = I2C_NUM_1,
            .scl_io_num = bus1_scl,  // Try common alternate pins
            .sda_io_num = bus1_sda,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };

        i2c_master_bus_handle_t bus1;
        if (i2c_new_master_bus(&bus1_config, &bus1) == ESP_OK) {
            scan_i2c_bus(bus1, "I2C Bus 1 (GPIO21/22)", found_addresses);
            i2c_del_master_bus(bus1);
        } else {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "I2C Bus 1: Not configured or not available");
        }
    } else {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "I2C Bus 1: Skipped (GPIO %d/%d not valid on this target)", bus1_sda, bus1_scl);
    }

    // Summary
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Configuration Check:");

    if (found_addresses[0x24]) {
        ESP_LOGI(TAG, "  TPS546 #1 (0x24): Found ✓");
    } else {
        ESP_LOGW(TAG, "  TPS546 #1 (0x24): NOT FOUND");
    }

    bool found_second_regulator = false;
    for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
        if (addr != 0x24 && found_addresses[addr]) {
            ESP_LOGI(TAG, "  TPS546/TPSM861253 (0x%02X): Found ✓", addr);
            found_second_regulator = true;
        }
    }
    if (!found_second_regulator) {
        ESP_LOGW(TAG, "  TPS546 #2/TPSM861253: NOT FOUND (may be in pin-strap mode)");
    }

    if (found_addresses[0x2E]) {
        ESP_LOGI(TAG, "  EMC2103 (0x2E): Found ✓");
    } else {
        ESP_LOGW(TAG, "  EMC2103 (0x2E): NOT FOUND");
    }

    if (found_addresses[0x40]) {
        ESP_LOGI(TAG, "  INA260 (0x40): Found ✓");
    } else {
        ESP_LOGW(TAG, "  INA260 (0x40): NOT FOUND");
    }

    ESP_LOGI(TAG, "");
}

static void SYSTEM_discover_hardware(GlobalState * GLOBAL_STATE) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          HARDWARE DISCOVERY & DIAGNOSTICS              ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");

    // I2C Bus Scan
    SYSTEM_scan_i2c_bus();

    // Board identification
    ESP_LOGI(TAG, "Board Configuration:");
    ESP_LOGI(TAG, "  Family: %s", GLOBAL_STATE->DEVICE_CONFIG.family.name);
    ESP_LOGI(TAG, "  Board Version: %s", GLOBAL_STATE->DEVICE_CONFIG.board_version);
    ESP_LOGI(TAG, "  ASIC: %s", GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
    ESP_LOGI(TAG, "  ASIC Count: %d", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count);
    ESP_LOGI(TAG, "  Voltage Domains: %d", GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains);
    ESP_LOGI(TAG, "");

    // Peripheral detection
    ESP_LOGI(TAG, "Configured Peripherals:");
    ESP_LOGI(TAG, "  EMC2101: %s", GLOBAL_STATE->DEVICE_CONFIG.EMC2101 ? "YES" : "NO");
    ESP_LOGI(TAG, "  EMC2103: %s", GLOBAL_STATE->DEVICE_CONFIG.EMC2103 ? "YES" : "NO");
    ESP_LOGI(TAG, "  EMC2302: %s", GLOBAL_STATE->DEVICE_CONFIG.EMC2302 ? "YES" : "NO");
    ESP_LOGI(TAG, "  TMP1075: %s", GLOBAL_STATE->DEVICE_CONFIG.TMP1075 ? "YES" : "NO");
    ESP_LOGI(TAG, "  TPS546:  %s", GLOBAL_STATE->DEVICE_CONFIG.TPS546 ? "YES" : "NO");
    ESP_LOGI(TAG, "  INA260:  %s", GLOBAL_STATE->DEVICE_CONFIG.INA260 ? "YES" : "NO");
    ESP_LOGI(TAG, "  DS4432U: %s", GLOBAL_STATE->DEVICE_CONFIG.DS4432U ? "YES" : "NO");
    ESP_LOGI(TAG, "");

    // Power readings
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546 || GLOBAL_STATE->DEVICE_CONFIG.INA260) {
        ESP_LOGI(TAG, "Initial Power Readings:");
        float input_voltage = Power_get_input_voltage(GLOBAL_STATE);
        float current = Power_get_current(GLOBAL_STATE);
        float power = Power_get_power(GLOBAL_STATE);
        int16_t core_voltage_mv = VCORE_get_voltage_mv(GLOBAL_STATE);

        ESP_LOGI(TAG, "  Input Voltage: %.2fV", input_voltage / 1000.0);
        ESP_LOGI(TAG, "  Core Voltage: %.3fV", core_voltage_mv / 1000.0);
        ESP_LOGI(TAG, "  Current: %.2fA", current / 1000.0);
        ESP_LOGI(TAG, "  Power: %.2fW", power);

        if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
            float vreg_temp = Power_get_vreg_temp(GLOBAL_STATE);
            ESP_LOGI(TAG, "  VReg Temperature: %.1f°C", vreg_temp);
        }
        ESP_LOGI(TAG, "");
    }

    // Temperature readings
    float temp1 = Thermal_get_chip_temp(GLOBAL_STATE);
    float temp2 = Thermal_get_chip_temp2(GLOBAL_STATE);

    ESP_LOGI(TAG, "Temperature Sensors:");
    if (temp1 >= 0) {
        ESP_LOGI(TAG, "  Sensor 1: %.1f°C", temp1);
    } else {
        ESP_LOGI(TAG, "  Sensor 1: Not available");
    }
    if (temp2 >= 0) {
        ESP_LOGI(TAG, "  Sensor 2: %.1f°C", temp2);
    } else {
        ESP_LOGI(TAG, "  Sensor 2: Not available");
    }
    ESP_LOGI(TAG, "");

    // Fan status
    uint16_t fan1_rpm = Thermal_get_fan_speed(&GLOBAL_STATE->DEVICE_CONFIG);
    uint16_t fan2_rpm = Thermal_get_fan2_speed(&GLOBAL_STATE->DEVICE_CONFIG);

    ESP_LOGI(TAG, "Fan Status:");
    ESP_LOGI(TAG, "  Fan 1: %d RPM", fan1_rpm);
    if (fan2_rpm > 0) {
        ESP_LOGI(TAG, "  Fan 2: %d RPM", fan2_rpm);
    }
    ESP_LOGI(TAG, "");

    // Memory
    ESP_LOGI(TAG, "Memory:");
    ESP_LOGI(TAG, "  PSRAM: %s", GLOBAL_STATE->psram_is_available ? "Available" : "Not available");
    ESP_LOGI(TAG, "  Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          HARDWARE DISCOVERY COMPLETE                   ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
}

esp_err_t SYSTEM_init_peripherals(GlobalState * GLOBAL_STATE) {

    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "Error installing ISR service");

    // Initialize the core voltage regulator
    ESP_RETURN_ON_ERROR(VCORE_init(GLOBAL_STATE), TAG, "VCORE init failed!");

    ESP_RETURN_ON_ERROR(Thermal_init(&GLOBAL_STATE->DEVICE_CONFIG), TAG, "Thermal init failed!");

    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Ensure overheat_mode config exists
    ESP_RETURN_ON_ERROR(ensure_overheat_mode_config(), TAG, "Failed to ensure overheat_mode config");

    ESP_RETURN_ON_ERROR(display_init(GLOBAL_STATE), TAG, "Display init failed!");

    ESP_RETURN_ON_ERROR(input_init(screen_button_press, toggle_wifi_softap), TAG, "Input init failed!");

    ESP_RETURN_ON_ERROR(screen_start(GLOBAL_STATE), TAG, "Screen start failed!");

    // Comprehensive hardware discovery
    SYSTEM_discover_hardware(GLOBAL_STATE);

    return ESP_OK;
}

void SYSTEM_notify_accepted_share(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->shares_accepted++;
}

static int compare_rejected_reason_stats(const void *a, const void *b) {
    const RejectedReasonStat *ea = a;
    const RejectedReasonStat *eb = b;
    return (eb->count > ea->count) - (ea->count > eb->count);
}

void SYSTEM_notify_rejected_share(GlobalState * GLOBAL_STATE, char * error_msg)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->shares_rejected++;

    for (int i = 0; i < module->rejected_reason_stats_count; i++) {
        if (strncmp(module->rejected_reason_stats[i].message, error_msg, sizeof(module->rejected_reason_stats[i].message) - 1) == 0) {
            module->rejected_reason_stats[i].count++;
            return;
        }
    }

    if (module->rejected_reason_stats_count < sizeof(module->rejected_reason_stats)) {
        strncpy(module->rejected_reason_stats[module->rejected_reason_stats_count].message, 
                error_msg, 
                sizeof(module->rejected_reason_stats[module->rejected_reason_stats_count].message) - 1);
        module->rejected_reason_stats[module->rejected_reason_stats_count].message[sizeof(module->rejected_reason_stats[module->rejected_reason_stats_count].message) - 1] = '\0'; // Ensure null termination
        module->rejected_reason_stats[module->rejected_reason_stats_count].count = 1;
        module->rejected_reason_stats_count++;
    }

    if (module->rejected_reason_stats_count > 1) {
        qsort(module->rejected_reason_stats, module->rejected_reason_stats_count, 
            sizeof(module->rejected_reason_stats[0]), compare_rejected_reason_stats);
    }    
}

void SYSTEM_notify_new_ntime(GlobalState * GLOBAL_STATE, uint32_t ntime)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    // Hourly clock sync
    if (module->lastClockSync + (60 * 60) > ntime) {
        return;
    }
    ESP_LOGI(TAG, "Syncing clock");
    module->lastClockSync = ntime;
    struct timeval tv;
    tv.tv_sec = ntime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
}

void SYSTEM_notify_found_nonce(GlobalState * GLOBAL_STATE, double diff, uint8_t job_id)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    if ((uint64_t) diff > module->best_session_nonce_diff) {
        module->best_session_nonce_diff = (uint64_t) diff;
        suffixString((uint64_t) diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);
    }

    double network_diff = networkDifficulty(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->target);
    if (diff > network_diff) {
        module->block_found = true;
        ESP_LOGI(TAG, "FOUND BLOCK!!!!!!!!!!!!!!!!!!!!!! %f > %f", diff, network_diff);
    }

    if ((uint64_t) diff <= module->best_nonce_diff) {
        return;
    }
    module->best_nonce_diff = (uint64_t) diff;

    nvs_config_set_u64(NVS_CONFIG_BEST_DIFF, module->best_nonce_diff);

    // make the best_nonce_diff into a string
    suffixString((uint64_t) diff, module->best_diff_string, DIFF_STRING_SIZE, 0);

    ESP_LOGI(TAG, "Network diff: %f", network_diff);
}

static esp_err_t ensure_overheat_mode_config() {
    bool overheat_mode = nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);

    ESP_LOGI(TAG, "Existing overheat_mode value: %d", overheat_mode);

    return ESP_OK;
}
