#ifndef AURADINE_H_
#define AURADINE_H_

#include "common.h"
#include "mining.h"

#define AURADINE_SERIALTX_DEBUG true
#define AURADINE_SERIALRX_DEBUG true
#define AURADINE_DEBUG_WORK false
#define AURADINE_DEBUG_JOBS false

// Uncomment to enable comprehensive register discovery scan (0x00-0xFF)
// WARNING: This takes ~30 seconds and generates a LOT of log output
// Only enable once you're getting RX responses from the chip
// #define AURADINE_REGISTER_DISCOVERY

typedef struct __attribute__((__packed__))
{
    uint8_t job_id;
    uint8_t num_midstates;
    uint8_t starting_nonce[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
    uint8_t merkle_root[32];
    uint8_t prev_block_hash[32];
    uint8_t version[4];
} AURADINE_job;

uint8_t AURADINE_TREASURE_init(float frequency, uint16_t asic_count, uint16_t difficulty);
void AURADINE_TREASURE_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void AURADINE_TREASURE_set_version_mask(uint32_t version_mask);
int AURADINE_TREASURE_set_max_baud(void);
int AURADINE_TREASURE_set_default_baud(void);
void AURADINE_TREASURE_send_hash_frequency(float frequency);
task_result * AURADINE_TREASURE_process_work(void * GLOBAL_STATE);
void AURADINE_TREASURE_read_registers(void);
void AURADINE_TREASURE_scan_chip_addresses(uint16_t delay_ms);
void AURADINE_TREASURE_scan_registers(uint8_t start_addr, uint8_t end_addr, uint16_t delay_ms);

#endif /* AURADINE_H_ */
