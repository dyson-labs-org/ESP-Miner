#ifndef ASIC_RESET_H_
#define ASIC_RESET_H_

esp_err_t asic_reset(void);
esp_err_t asic_hold_reset_low(void);

esp_err_t vdd_hash_enable(void);
esp_err_t vdd_hash_disable(void);

#endif /* ASIC_RESET_H_ */
