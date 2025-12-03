#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "vcore.h"
#include "adc.h"
#include "DS4432U.h"
#include "TPS546.h"
#include "INA260.h"
#include "driver/gpio.h"

#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE
#define GPIO_PLUG_SENSE  CONFIG_GPIO_PLUG_SENSE

static const char *TAG = "vcore";

static TPS546_CONFIG TPS546_CONFIG_DEFAULT = {
    /* vin voltage */
    .TPS546_INIT_VIN_ON = 4.8,
    .TPS546_INIT_VIN_OFF = 4.5,
    .TPS546_INIT_VIN_UV_WARN_LIMIT = 0, //Set to 0 to ignore. TI Bug in this register
    .TPS546_INIT_VIN_OV_FAULT_LIMIT = 6.5,
    /* vout voltage */
    .TPS546_INIT_SCALE_LOOP = 0.25,
    .TPS546_INIT_VOUT_MIN = 1,
    .TPS546_INIT_VOUT_MAX = 2,
    .TPS546_INIT_VOUT_COMMAND = 1.2,
    /* iout current */
    .TPS546_INIT_IOUT_OC_WARN_LIMIT = 25.00, /* A */
    .TPS546_INIT_IOUT_OC_FAULT_LIMIT = 30.00 /* A */
};

static TPS546_CONFIG TPS546_CONFIG_GAMMATURBO = {
    /* vin voltage */
    .TPS546_INIT_VIN_ON = 11.0,
    .TPS546_INIT_VIN_OFF = 10.5,
    .TPS546_INIT_VIN_UV_WARN_LIMIT = 11.0,
    .TPS546_INIT_VIN_OV_FAULT_LIMIT = 14.0,
    /* vout voltage */
    .TPS546_INIT_SCALE_LOOP = 0.25,
    .TPS546_INIT_VOUT_MIN = 1,
    .TPS546_INIT_VOUT_MAX = 3,
    .TPS546_INIT_VOUT_COMMAND = 1.2,
    /* iout current */
    .TPS546_INIT_IOUT_OC_WARN_LIMIT = 50.00, /* A */
    .TPS546_INIT_IOUT_OC_FAULT_LIMIT = 55.00 /* A */
};

static TPS546_CONFIG TPS546_CONFIG_HEX = {
    /* vin voltage */
    .TPS546_INIT_VIN_ON = 11.5,
    .TPS546_INIT_VIN_OFF = 11.0,
    .TPS546_INIT_VIN_UV_WARN_LIMIT = 11.0,
    .TPS546_INIT_VIN_OV_FAULT_LIMIT = 14.0,
    /* vout voltage */
    .TPS546_INIT_SCALE_LOOP = 0.125,
    .TPS546_INIT_VOUT_MIN = 2.5,
    .TPS546_INIT_VOUT_MAX = 4.5,
    .TPS546_INIT_VOUT_COMMAND = 3.6,
    /* iout current */
    .TPS546_INIT_IOUT_OC_WARN_LIMIT = 25.00, /* A */
    .TPS546_INIT_IOUT_OC_FAULT_LIMIT = 30.00 /* A */
};

static TPS546_CONFIG TPS546_CONFIG_AURA = {
    /* vin voltage */
    .TPS546_INIT_VIN_ON = 11.0,
    .TPS546_INIT_VIN_OFF = 10.5,
    .TPS546_INIT_VIN_UV_WARN_LIMIT = 11.0,
    .TPS546_INIT_VIN_OV_FAULT_LIMIT = 14.0,
    /* vout voltage - LOW voltage for Auradine chips */
    .TPS546_INIT_SCALE_LOOP = 0.25,
    .TPS546_INIT_VOUT_MIN = 0.2,      // TESTING: 200mV total (100mV per chip) - 0V rejected by TPS546
    .TPS546_INIT_VOUT_MAX = 0.8,      // 800mV max (350mV × 2 domains = 700mV, allow margin)
    .TPS546_INIT_VOUT_COMMAND = 0.2,  // TESTING: 200mV total (100mV per chip) for serial testing
    /* iout current */
    .TPS546_INIT_IOUT_OC_WARN_LIMIT = 25.00, /* A */
    .TPS546_INIT_IOUT_OC_FAULT_LIMIT = 30.00 /* A */
};

esp_err_t VCORE_init(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "════════════════════════════════════════");
    ESP_LOGI(TAG, "  VCORE Initialization");
    ESP_LOGI(TAG, "════════════════════════════════════════");

    ESP_RETURN_ON_FALSE(GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains != 0, ESP_FAIL, TAG, "voltage_domains not defined");

    ESP_LOGI(TAG, "Board: %s", GLOBAL_STATE->DEVICE_CONFIG.family.name);
    ESP_LOGI(TAG, "ASIC: %s", GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
    ESP_LOGI(TAG, "Voltage domains: %d", GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains);
    ESP_LOGI(TAG, "ASIC count: %d", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count);

    if (GLOBAL_STATE->DEVICE_CONFIG.DS4432U) {
        ESP_LOGI(TAG, "Initializing DS4432U regulator...");
        ESP_RETURN_ON_ERROR(DS4432U_init(), TAG, "DS4432 init failed!");
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.INA260) {
        ESP_LOGI(TAG, "Initializing INA260 power monitor...");
        ESP_RETURN_ON_ERROR(INA260_init(), TAG, "INA260 init failed!");
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        const char* config_name;
        TPS546_CONFIG config;

        switch (GLOBAL_STATE->DEVICE_CONFIG.family.id) {
            case GAMMA_TURBO:
                config = TPS546_CONFIG_GAMMATURBO;
                config_name = "GAMMA_TURBO";
                break;
            case HEX:
            case SUPRA_HEX:
                config = TPS546_CONFIG_HEX;
                config_name = "HEX";
                break;
            case AURA:
                config = TPS546_CONFIG_AURA;
                config_name = "AURA (LOW VOLTAGE)";
                break;
            default:
                config = TPS546_CONFIG_DEFAULT;
                config_name = "DEFAULT";
                break;
        }

        ESP_LOGI(TAG, "Initializing TPS546 with %s config...", config_name);
        ESP_LOGI(TAG, "  VOUT range: %.3fV - %.3fV", config.TPS546_INIT_VOUT_MIN, config.TPS546_INIT_VOUT_MAX);
        ESP_LOGI(TAG, "  VOUT command: %.3fV", config.TPS546_INIT_VOUT_COMMAND);
        ESP_LOGI(TAG, "  Current limit: %.1fA (warn), %.1fA (fault)",
                 config.TPS546_INIT_IOUT_OC_WARN_LIMIT, config.TPS546_INIT_IOUT_OC_FAULT_LIMIT);

        ESP_RETURN_ON_ERROR(TPS546_init(config), TAG, "TPS546 init failed!");
        ESP_LOGI(TAG, "TPS546 initialized successfully");
    }

    if (GLOBAL_STATE->DEVICE_CONFIG.plug_sense) {
        gpio_config_t barrel_jack_conf = {
            .pin_bit_mask = (1ULL << GPIO_PLUG_SENSE),
            .mode = GPIO_MODE_INPUT,
        };
        gpio_config(&barrel_jack_conf);
        int barrel_jack_plugged_in = gpio_get_level(GPIO_PLUG_SENSE);

        gpio_set_direction(GPIO_ASIC_ENABLE, GPIO_MODE_OUTPUT);
        if (barrel_jack_plugged_in == 1 || GLOBAL_STATE->DEVICE_CONFIG.asic_enable) {
            gpio_set_level(GPIO_ASIC_ENABLE, 0);
        } else {
            // turn ASIC off
            gpio_set_level(GPIO_ASIC_ENABLE, 1);
        }
    }

    return ESP_OK;
}

esp_err_t VCORE_set_voltage(GlobalState * GLOBAL_STATE, float core_voltage)
{
    ESP_LOGI(TAG, "────────────────────────────────────────");
    ESP_LOGI(TAG, "Setting ASIC Core Voltage");
    ESP_LOGI(TAG, "────────────────────────────────────────");
    ESP_LOGI(TAG, "Target per-chip voltage: %.3fV", core_voltage);

    if (GLOBAL_STATE->DEVICE_CONFIG.DS4432U) {
        if (core_voltage != 0.0f) {
            ESP_LOGI(TAG, "Setting DS4432U to %.3fV", core_voltage);
            ESP_RETURN_ON_ERROR(DS4432U_set_voltage(core_voltage), TAG, "DS4432U set voltage failed!");
        }
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        uint16_t voltage_domains = GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains;
        float tps546_voltage = core_voltage * voltage_domains;

        ESP_LOGI(TAG, "Voltage domains: %d (chips in series)", voltage_domains);
        ESP_LOGI(TAG, "TPS546 output voltage: %.3fV (%.3fV × %d)",
                 tps546_voltage, core_voltage, voltage_domains);
        ESP_LOGI(TAG, "Voltage per chip: %.3fV (%.3fV ÷ %d)",
                 tps546_voltage / voltage_domains, tps546_voltage, voltage_domains);

        // Safety check
        if (tps546_voltage > 1.5f) {
            ESP_LOGW(TAG, "⚠ WARNING: TPS546 voltage %.3fV seems high! Verify voltage_domains=%d is correct",
                     tps546_voltage, voltage_domains);
        }
        if (tps546_voltage < 0.3f && core_voltage != 0.0f) {
            ESP_LOGW(TAG, "⚠ WARNING: TPS546 voltage %.3fV seems low! Verify configuration",
                     tps546_voltage);
        }

        ESP_RETURN_ON_ERROR(TPS546_set_vout(tps546_voltage), TAG, "TPS546 set voltage failed!");
        ESP_LOGI(TAG, "✓ TPS546 voltage set successfully");

        // Readback verification
        vTaskDelay(50 / portTICK_PERIOD_MS); // Allow voltage to settle
        float actual_voltage = TPS546_get_vout();
        ESP_LOGI(TAG, "TPS546 readback: %.3fV (target: %.3fV)", actual_voltage, tps546_voltage);
        if (fabs(actual_voltage - tps546_voltage) > 0.05f) {
            ESP_LOGW(TAG, "⚠ Voltage readback mismatch: %.3fV vs %.3fV (diff: %.3fV)",
                     actual_voltage, tps546_voltage, fabs(actual_voltage - tps546_voltage));
        }
    }
    if (core_voltage == 0.0f && GLOBAL_STATE->DEVICE_CONFIG.asic_enable) {
        ESP_LOGI(TAG, "Disabling ASIC power (voltage = 0)");
        gpio_set_level(GPIO_ASIC_ENABLE, 1);
    }

    ESP_LOGI(TAG, "────────────────────────────────────────");
    return ESP_OK;
}

int16_t VCORE_get_voltage_mv(GlobalState * GLOBAL_STATE) 
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        return TPS546_get_vout() / GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains * 1000;
    }
    return ADC_get_vcore();
}

esp_err_t VCORE_check_fault(GlobalState * GLOBAL_STATE) 
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        ESP_RETURN_ON_ERROR(TPS546_check_status(GLOBAL_STATE), TAG, "TPS546 check status failed!");
    }
    return ESP_OK;
}

const char* VCORE_get_fault_string(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        return TPS546_get_error_message();
    }
    return NULL;
}
