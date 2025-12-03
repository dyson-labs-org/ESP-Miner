#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define GPIO_ASIC_RESET CONFIG_GPIO_ASIC_RESET
#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE
#define GPIO_VDD_HASH CONFIG_GPIO_VDD_HASH

static const char *TAG = "asic_reset";

esp_err_t asic_reset(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          ASIC RESET SEQUENCE                           ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");

    // Enable ASIC power (GPIO10 LOW = power ON)
    ESP_LOGI(TAG, "Enabling ASIC power (GPIO_%d LOW)...", GPIO_ASIC_ENABLE);
    esp_rom_gpio_pad_select_gpio(GPIO_ASIC_ENABLE);
    ESP_RETURN_ON_ERROR(gpio_set_direction(GPIO_ASIC_ENABLE, GPIO_MODE_OUTPUT), TAG, "Can't set direction");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_ASIC_ENABLE, 0), TAG, "Can't set to LOW");
    ESP_LOGI(TAG, "  Readback: %s", gpio_get_level(GPIO_ASIC_ENABLE) ? "HIGH (FAILED)" : "LOW (OK)");
    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Drive RESET low briefly, then hold high for command phase
    ESP_LOGI(TAG, "Driving ASIC RESET (GPIO_%d) LOW -> HIGH", GPIO_ASIC_RESET);
    esp_rom_gpio_pad_select_gpio(GPIO_ASIC_RESET);
    ESP_RETURN_ON_ERROR(gpio_set_direction(GPIO_ASIC_RESET, GPIO_MODE_OUTPUT), TAG, "Can't set GPIO_ASIC_RESET direction");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_ASIC_RESET, 0), TAG, "Can't pull RESET LOW");
    vTaskDelay(20 / portTICK_PERIOD_MS);
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_ASIC_RESET, 1), TAG, "Can't release RESET HIGH");
    vTaskDelay(50 / portTICK_PERIOD_MS);
    int level = gpio_get_level(GPIO_ASIC_RESET);
    ESP_LOGI(TAG, "  RESET readback: %s", level ? "HIGH (OK)" : "LOW (FAILED)");
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "");
    return ESP_OK;
}

esp_err_t asic_hold_reset_low(void) {
    esp_rom_gpio_pad_select_gpio(GPIO_ASIC_RESET);
    ESP_RETURN_ON_ERROR(gpio_set_direction(GPIO_ASIC_RESET, GPIO_MODE_OUTPUT), TAG, "Can't set GPIO_ASIC_RESET direction");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_ASIC_RESET, 0), TAG, "Can't set GPIO_ASIC_RESET level to LOW");
    return ESP_OK;
}

esp_err_t vdd_hash_enable(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          VDD_HASH ENABLE (AFTER PLL)                   ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");

    esp_rom_gpio_pad_select_gpio(GPIO_VDD_HASH);
    ESP_RETURN_ON_ERROR(gpio_set_direction(GPIO_VDD_HASH, GPIO_MODE_OUTPUT), TAG, "Can't set GPIO_VDD_HASH direction");

    ESP_LOGI(TAG, "Enabling VDD_HASH (GPIO_%d HIGH)...", GPIO_VDD_HASH);
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_VDD_HASH, 1), TAG, "Can't set GPIO_VDD_HASH to HIGH");

    int level = gpio_get_level(GPIO_VDD_HASH);
    ESP_LOGI(TAG, "  Readback: %s", level ? "HIGH (OK)" : "LOW (FAILED - continuing anyway)");

    vTaskDelay(100 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "");
    return ESP_OK;
}

esp_err_t vdd_hash_disable(void) {
    esp_rom_gpio_pad_select_gpio(GPIO_VDD_HASH);
    ESP_RETURN_ON_ERROR(gpio_set_direction(GPIO_VDD_HASH, GPIO_MODE_OUTPUT), TAG, "Can't set GPIO_VDD_HASH direction");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_VDD_HASH, 0), TAG, "Can't set GPIO_VDD_HASH to LOW");
    return ESP_OK;
}
