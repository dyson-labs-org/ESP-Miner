#ifndef TREASURE_H_
#define TREASURE_H_

#include "common.h"
#include "mining.h"

#define TREASURE_SERIALTX_DEBUG true
#define TREASURE_SERIALRX_DEBUG true

uint8_t TREASURE_init(float frequency, uint16_t asic_count, uint16_t difficulty);
void TREASURE_read_registers(void);

#endif /* TREASURE_H_ */
