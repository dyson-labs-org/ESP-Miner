#include "asic_init.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "asic.h"
#include "serial.h"
#include "asic_reset.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "sdkconfig.h"
#include "vcore.h"
#include "nvs_config.h"
#include "adc.h"

static const char *TAG = "asic_init";
// Placeholder flag; WDT disabled via sdkconfig during bring-up
static bool asic_init_wdt_registered = false;

// GPIO pins from Kconfig
#define GPIO_ASIC_RST   CONFIG_GPIO_ASIC_RESET
#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE

static void verify_gpio_states(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          GPIO STATE VERIFICATION                       ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");

    // Read GPIO states
    int rst_level = gpio_get_level(GPIO_ASIC_RST);
    int enable_level = gpio_get_level(GPIO_ASIC_ENABLE);

    ESP_LOGI(TAG, "GPIO_%d (ASIC_RST):    %s (should be HIGH for normal operation)",
             GPIO_ASIC_RST, rst_level ? "HIGH" : "LOW");
    ESP_LOGI(TAG, "GPIO_%d (ASIC_ENABLE): %s (should be LOW for power ON)",
             GPIO_ASIC_ENABLE, enable_level ? "HIGH (OFF)" : "LOW (ON)");

    if (rst_level == 0 && enable_level == 0) {
        ESP_LOGI(TAG, "✓ GPIO states look correct for ASIC operation");
    }
    if (rst_level == 1) {
        ESP_LOGW(TAG, "⚠ ASIC is in RESET! Chip will not respond.");
    }
    if (enable_level == 1) {
        ESP_LOGW(TAG, "⚠ ASIC power is DISABLED! Chip is powered off.");
    }

    ESP_LOGI(TAG, "");
}

static bool uart_loopback_test(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          UART LOOPBACK TEST                            ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "Testing UART TX/RX functionality...");
    ESP_LOGI(TAG, "NOTE: This requires TX and RX pins to be physically jumpered!");
    ESP_LOGI(TAG, "");

    // Clear any existing data
    uart_flush(UART_NUM_1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Test pattern
    const uint8_t test_data[] = {0xAA, 0x55, 0x01, 0x02, 0x03, 0x04, 0x05};
    const int test_len = sizeof(test_data);
    uint8_t rx_buffer[32];

    ESP_LOGI(TAG, "TX: Sending %d test bytes...", test_len);
    int tx_bytes = uart_write_bytes(UART_NUM_1, test_data, test_len);
    ESP_LOGI(TAG, "    Wrote %d bytes", tx_bytes);

    // Wait for transmission and potential loopback
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Try to read back
    ESP_LOGI(TAG, "RX: Attempting to read...");
    int rx_bytes = uart_read_bytes(UART_NUM_1, rx_buffer, sizeof(rx_buffer), 500 / portTICK_PERIOD_MS);

    if (rx_bytes > 0) {
        ESP_LOGI(TAG, "    Received %d bytes:", rx_bytes);
        ESP_LOG_BUFFER_HEX(TAG, rx_buffer, rx_bytes);

        if (rx_bytes == test_len && memcmp(test_data, rx_buffer, test_len) == 0) {
            ESP_LOGI(TAG, "✓ LOOPBACK TEST PASSED - TX and RX working!");
            ESP_LOGI(TAG, "");
            return true;
        } else {
            ESP_LOGW(TAG, "⚠ Received data doesn't match (possible noise or partial loopback)");
            ESP_LOGI(TAG, "");
            return false;
        }
    } else {
        ESP_LOGW(TAG, "    Received 0 bytes - RX not working or no loopback");
        ESP_LOGI(TAG, "If TX/RX are NOT jumpered: This is EXPECTED");
        ESP_LOGI(TAG, "If TX/RX ARE jumpered: RX path has a problem");
        ESP_LOGI(TAG, "");
        return false;
    }
}

static void test_gpio_clock_signal(uint8_t gpio_num, const char *name) {
    // Configure GPIO as input without pulls to detect external signals
    gpio_config_t conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&conf);
    vTaskDelay(1 / portTICK_PERIOD_MS);

    // Sample the GPIO rapidly to detect toggling (reduced samples to avoid watchdog)
    int samples = 100; // keep short to avoid long blocking
    int changes = 0;
    int last_level = gpio_get_level(gpio_num);

    for (int i = 0; i < samples; i++) {
        int level = gpio_get_level(gpio_num);
        if (level != last_level) {
            changes++;
            last_level = level;
        }
        esp_rom_delay_us(5); // small gap between samples

        // Yield periodically to avoid starving other tasks
        if (i % 50 == 0) {
            vTaskDelay(1);
        }
    }

    // Analyze results
    if (changes > 50) {
        // Many transitions = likely clock or data signal
        float est_freq_khz = (changes / 2.0) / (samples * 10.0 / 1000.0);
        ESP_LOGI(TAG, "GPIO_%d (%s): ACTIVE SIGNAL (~%.1f kHz, %d transitions)",
                 gpio_num, name, est_freq_khz, changes);
    } else if (changes > 0) {
        ESP_LOGI(TAG, "GPIO_%d (%s): OCCASIONAL ACTIVITY (%d transitions)",
                 gpio_num, name, changes);
    } else {
        int level = gpio_get_level(gpio_num);
        ESP_LOGI(TAG, "GPIO_%d (%s): STATIC %s (no transitions)",
                 gpio_num, name, level ? "HIGH" : "LOW");
    }
}

uint8_t asic_initialize(GlobalState *GLOBAL_STATE, asic_init_mode_t mode, uint32_t stabilization_delay_ms)
{
    const char *mode_str = (mode == ASIC_INIT_COLD_BOOT) ? "cold boot" : "recovery";

    // Task WDT is disabled via sdkconfig during bring-up; no WDT handling needed here
    asic_init_wdt_registered = false;

    ESP_LOGI(TAG, "Starting ASIC initialization (%s mode)", mode_str);
    ESP_LOGI(TAG, "Initialization order: 1) Serial clock, 2) GPIO1 reset, 3) Config, 4) VDD_HASH");

    // STEP 0: GPIO DIAGNOSTIC TEST (BEFORE UART init)
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          GPIO CLOCK/SIGNAL DETECTION TEST              ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");
    bool run_gpio_scan = false;
    if (!run_gpio_scan) {
        ESP_LOGW(TAG, "Skipping extended GPIO scan to avoid WDT/panic during bring-up");
    } else {
        // Test critical GPIOs
        ESP_LOGI(TAG, "Critical GPIOs (short scan):");

        // Special test for GPIO 1 - may be ASIC ready signal
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "=== GPIO 1 (RST_3V3) DETAILED TEST ===");
        gpio_config_t rst_test = {
            .pin_bit_mask = (1ULL << 1),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
        };
        gpio_config(&rst_test);
        vTaskDelay(5 / portTICK_PERIOD_MS);
        int high_count = 0, low_count = 0;
        for (int i = 0; i < 20; i++) {
            int level = gpio_get_level(1);
            if (level) high_count++; else low_count++;
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
        // Configure GPIO1 as input with pull-up; skip further scanning to avoid disturbing reset
        gpio_config_t rst_conf = {
            .pin_bit_mask = (1ULL << 1),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
        };
        gpio_config(&rst_conf);
        ESP_LOGI(TAG, "GPIO1 set to input with pull-up (no scan)");
        
        const uint8_t critical_pins[] = {10, 12, 17, 18};
        for (size_t i = 0; i < sizeof(critical_pins); i++) {
            test_gpio_clock_signal(critical_pins[i], "critical");
            if (i % 2 == 1) vTaskDelay(1);
        }

        const uint8_t optional_pins[] = {3, 4, 5, 6, 7, 8, 9, 14, 15, 16, 33, 34, 35, 36, 37, 38, 45, 46};
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "Optional GPIOs (short scan)...");
        for (size_t i = 0; i < sizeof(optional_pins); i++) {
            test_gpio_clock_signal(optional_pins[i], "test");
            if (i % 4 == 3) vTaskDelay(1);
        }

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "Clock signal scan complete.");
        ESP_LOGI(TAG, "");

        // Measure GPIO2 (ADC1_CH1) voltage
        uint16_t gpio2_mv = ADC_get_vcore();
        ESP_LOGI(TAG, "GPIO2 ADC1_CH1 reading: %u mV", gpio2_mv);
    }

    // Command-enable sequence before UART init
    ESP_LOGI(TAG, "Preparing GPIO17/1 for command enable sequence");
    gpio_config_t tx_hold_cfg = {
        .pin_bit_mask = (1ULL << 17),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&tx_hold_cfg);
    gpio_set_level(17, 1);  // assert GPIO17 high
    for (int i = 0; i < 4; i++) { esp_rom_delay_us(10); } // wait ≥4 clocks

    gpio_config_t rst_drive_cfg = {
        .pin_bit_mask = (1ULL << GPIO_ASIC_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&rst_drive_cfg);
    gpio_set_level(GPIO_ASIC_RST, 1); // assert GPIO1
    for (int i = 0; i < 4; i++) { esp_rom_delay_us(10); }
    gpio_set_level(GPIO_ASIC_RST, 0); // deassert GPIO1
    for (int i = 0; i < 4; i++) { esp_rom_delay_us(10); }

    // Return pins to UART control
    ESP_LOGI(TAG, "Releasing GPIO17/18 for UART use");
    gpio_reset_pin(17);
    gpio_reset_pin(18);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // STEP 1: Establish serial clock signal (UART initialization)
    // Check actual UART state for safety
    bool uart_initialized = SERIAL_is_initialized();
    ESP_LOGI(TAG, "UART initialized check: %s", uart_initialized ? "YES" : "NO");

    // Verify mode matches actual state
    if (mode == ASIC_INIT_COLD_BOOT && uart_initialized) {
        ESP_LOGW(TAG, "Cold boot mode but UART already initialized - will reset baud only");
    } else if (mode == ASIC_INIT_RECOVERY && !uart_initialized) {
        ESP_LOGW(TAG, "Recovery mode but UART not initialized - will do full init");
    }

    // Use actual state for decision, not just mode
    if (!uart_initialized) {
        // Fresh boot - full UART initialization
        ESP_LOGI(TAG, "Step 1: Performing full UART initialization (serial clock)");
        ESP_LOGI(TAG, "About to call SERIAL_init()...");
        esp_err_t serial_ret = SERIAL_init();
        ESP_LOGI(TAG, "SERIAL_init() returned: %d", serial_ret);
        if (serial_ret != ESP_OK) {
            ESP_LOGE(TAG, "SERIAL_init() failed!");
            return 0;
        }

        // Run loopback test (will show expected failure if not jumpered)
        uart_loopback_test();
    } else {
        // Live recovery - ASIC was reset, UART needs baud reset to 115200
        // This preserves the running system and avoids reboot
        ESP_LOGI(TAG, "Step 1: UART already initialized, resetting baud to %d", UART_FREQ);
        SERIAL_set_baud(UART_FREQ);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // STEP 2: Reset ASIC (GPIO1: LOW then HIGH)
    ESP_LOGI(TAG, "Step 2: Performing ASIC reset (GPIO1 sequence)");
    if (asic_reset() != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "ASIC reset failed";
        ESP_LOGE(TAG, "ASIC reset failed!");
        return 0;
    }

    // Verify GPIO states after reset
    verify_gpio_states();

    // STEP 3: Send configuration commands and enable VDD_HASH
    // This happens inside ASIC_init() -> AURADINE_TREASURE_init()
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Step 3: Detecting ASIC chips and sending config...");
    uint8_t chip_count = ASIC_init(GLOBAL_STATE);
    
    if (chip_count == 0) {
        ESP_LOGE(TAG, "ASIC initialization failed - chip count 0");
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "Chip count 0";
        return 0;
    }

    ESP_LOGI(TAG, "Setting max baud rate and clearing buffers");
    SERIAL_set_baud(ASIC_set_max_baud(GLOBAL_STATE));
    SERIAL_clear_buffer();

    GLOBAL_STATE->ASIC_initalized = true;
    
    if (stabilization_delay_ms > 0) {
        ESP_LOGI(TAG, "Waiting %u ms for tasks to stabilize...", stabilization_delay_ms);
        vTaskDelay(stabilization_delay_ms / portTICK_PERIOD_MS);
    }

    // Enable core voltage after serial/reset tests are complete
    //uint16_t target_mv = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
    //float target_v = target_mv / 1000.0f;
    //ESP_LOGI(TAG, "Powering ASIC core after reset tests: %u mV (%.3f V)", target_mv, target_v);
    //GLOBAL_STATE->SYSTEM_MODULE.allow_core_voltage = true;
    //VCORE_set_voltage(GLOBAL_STATE, target_v);

    ESP_LOGI(TAG, "ASIC initialized successfully with %d chip(s) (%s mode)", chip_count, mode_str);
    return chip_count;
}
