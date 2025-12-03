#include "frequency_transition_bmXX.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define EPSILON 0.0001f
#define STEP_SIZE 6.25 // MHz step size

static const char * TAG = "frequency_transition";

static float current_frequency = 50; // Mhz
static float current_frequency_auradine = 1600; // MHz - Auradine minimum

void do_frequency_transition_auradine(float target_frequency, set_hash_frequency_fn set_frequency_fn)
{
    const float MAX_STEP_PERCENT = 0.10f; // 10% maximum step size
    const int DELAY_MS = 100; // Delay between frequency steps

    if (fabs(current_frequency_auradine - target_frequency) < EPSILON) {
        return;
    }

    ESP_LOGI(TAG, "Ramping frequency from %g MHz to %g MHz (max 10%% steps)",
             current_frequency_auradine, target_frequency);

    while (fabs(current_frequency_auradine - target_frequency) > EPSILON) {
        float difference = target_frequency - current_frequency_auradine;
        float max_step = current_frequency_auradine * MAX_STEP_PERCENT;

        float step;
        if (fabs(difference) <= max_step) {
            // Final step - go directly to target
            step = difference;
        } else {
            // Step by 10% in the appropriate direction
            step = (difference > 0) ? max_step : -max_step;
        }

        current_frequency_auradine += step;

        ESP_LOGI(TAG, "  Setting frequency to %g MHz (step: %+g MHz, %.1f%%)",
                 current_frequency_auradine, step, (step / (current_frequency_auradine - step)) * 100);

        set_frequency_fn(current_frequency_auradine);
        vTaskDelay(DELAY_MS / portTICK_PERIOD_MS);
    }

    // Ensure we're exactly at target
    current_frequency_auradine = target_frequency;
    set_frequency_fn(current_frequency_auradine);

    ESP_LOGI(TAG, "Successfully transitioned to %g MHz", target_frequency);
}

void do_frequency_transition(float target_frequency, set_hash_frequency_fn set_frequency_fn)
{
    if (fabs(current_frequency - target_frequency) < EPSILON) {
        return;
    }

    if (fabs(target_frequency - current_frequency) < STEP_SIZE) {
        current_frequency = target_frequency;
        set_frequency_fn(current_frequency);
        return;
    }

    ESP_LOGI(TAG, "Ramping up frequency from %g MHz to %g MHz", current_frequency, target_frequency);

    int current_step = (target_frequency > current_frequency) ? (int)floor(current_frequency / STEP_SIZE) : (int)ceil(current_frequency / STEP_SIZE);
    int target_step = (target_frequency > current_frequency) ? (int)floor(target_frequency / STEP_SIZE) : (int)ceil(target_frequency / STEP_SIZE);

    if (current_step != target_step) {
        int signum = (target_frequency > current_frequency) ? 1 : -1;
        
        while ((signum > 0 && current_step < target_step) ||
               (signum < 0 && current_step > target_step)) {
            current_step += signum;

            current_frequency = current_step * STEP_SIZE;
            set_frequency_fn(current_frequency);
            
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
    
    if (fabs(current_frequency - target_frequency) > EPSILON) {
        current_frequency = target_frequency;
        set_frequency_fn(current_frequency);
    }
    
    ESP_LOGI(TAG, "Successfully transitioned to %g MHz", target_frequency);
}
