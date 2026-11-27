#ifndef BURN_TIME_H
#define BURN_TIME_H

#include <stdint.h>

typedef enum {
    BT_OTHER, BT_CONNECT, BT_ERASE, BT_IO, BT_LOAD, BT_PAGE,
    BT_VERIFY, BT_OPT, BT_QUOTA, BT_SWD_LOCK, BT_FS_LOCK, BT_QUOTA_LOCK, BT_COUNT
} burn_stage_t;

typedef struct {
    uint64_t cycles[BT_COUNT];
    uint64_t started, ended, ready, miss_wait;
    uint8_t valid, ok, dual;
    uint64_t commit_hold, commit_wait;
} burn_time_result_t;

void burn_time_sample(void);
void burn_time_begin(uint8_t ch, uint8_t dual);
void burn_time_probe_begin(void);
void burn_time_probe_end(uint8_t ch, uint8_t found);
void burn_time_commit_begin(void);
void burn_time_commit_end(uint8_t ch);
burn_stage_t burn_time_stage(uint8_t ch, burn_stage_t next);
void burn_time_end(uint8_t ch, uint8_t ok);
void burn_time_rearmed(uint8_t ch);
uint32_t burn_time_revision(void);
uint32_t burn_time_snapshot(burn_time_result_t result[2]);
uint32_t burn_time_pair_ms(const burn_time_result_t result[2]);
uint32_t burn_time_ms(uint64_t cycles);

#endif
