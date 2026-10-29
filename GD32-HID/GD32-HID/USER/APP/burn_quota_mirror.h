#ifndef BURN_QUOTA_MIRROR_H
#define BURN_QUOTA_MIRROR_H
#include <stdint.h>
#define BURN_QUOTA_RECORD_SIZE 96U
uint8_t burn_quota_mirror_read(void *record);
uint8_t burn_quota_mirror_write(const void *record);
/* 0 = missing, 1 = present, 2 = I/O error. Used only for legacy migration. */
uint8_t burn_quota_mirror_probe(void);
#endif
