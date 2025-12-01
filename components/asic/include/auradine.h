#ifndef AURADINE_H_
#define AURADINE_H_

#include "common.h"
#include "mining.h"

#define AURADINE_SERIALTX_DEBUG false
#define AURADINE_SERIALRX_DEBUG false
#define AURADINE_DEBUG_WORK false
#define AURADINE_DEBUG_JOBS false

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

uint8_t AURADINE_init(float frequency, uint16_t asic_count, uint16_t difficulty);
void AURADINE_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void AURADINE_set_version_mask(uint32_t version_mask);
int AURADINE_set_max_baud(void);
int AURADINE_set_default_baud(void);
void AURADINE_send_hash_frequency(float frequency);
task_result * AURADINE_process_work(void * GLOBAL_STATE);
void AURADINE_read_registers(void);

#endif /* AURADINE_H_ */
