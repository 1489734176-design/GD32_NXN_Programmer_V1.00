#ifndef BURN_QUOTA_H
#define BURN_QUOTA_H
#include <stdint.h>

/* Global device quota. 0..999 allows fixed three-digit LCD presentation.
 * The last two 1KB MCU Flash pages are excluded from the app linker region. */
#define BURN_QUOTA_MAX 999U
#define BURN_QUOTA_FLASH_BASE 0x0801F800UL
#define BURN_QUOTA_PAGE_SIZE 1024U
enum { BURN_QUOTA_OK = 0, BURN_QUOTA_EMPTY, BURN_QUOTA_STORAGE, BURN_QUOTA_BUSY };

void burn_quota_init(void);
uint8_t burn_quota_enabled(void);
uint8_t burn_quota_fault(void);
uint32_t burn_quota_remaining(void);
uint8_t burn_quota_set(uint8_t enabled, uint32_t remaining);
/* Reserve durably BEFORE powering/programming the target. Normal failures
 * refund; interrupted power retains the reservation. Success needs no write. */
uint8_t burn_quota_begin(void);
uint8_t burn_quota_finish(uint8_t success);

/* Storage adapter: the journal is portable and fault-testable independently. */
uint32_t burn_quota_read_word(uint32_t address);
uint8_t burn_quota_program_word(uint32_t address, uint32_t value);
uint8_t burn_quota_erase_page(uint32_t address);
#endif
