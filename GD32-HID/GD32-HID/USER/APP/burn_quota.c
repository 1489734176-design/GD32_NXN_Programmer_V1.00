#include "burn_quota.h"

#define QUOTA_MAGIC 0x31545142UL /* BQT1, journal format version 1 */
#define QUOTA_COMMIT 0x54494D43UL
#define QUOTA_WORDS 8U
#define QUOTA_RECORD_SIZE (QUOTA_WORDS * 4U)
#define QUOTA_SLOTS (BURN_QUOTA_PAGE_SIZE / QUOTA_RECORD_SIZE)

static uint32_t quota_sequence;
static uint32_t quota_remaining;
static uint8_t quota_enabled, quota_fault, quota_reserved;
static uint8_t quota_page, quota_next, quota_valid;

static uint32_t record_address(uint8_t page, uint8_t slot)
{
    return BURN_QUOTA_FLASH_BASE + page * BURN_QUOTA_PAGE_SIZE + slot * QUOTA_RECORD_SIZE;
}

static uint32_t checksum(const uint32_t *words)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint8_t i, byte, bit;
    for(i = 0; i < 5; i++)
        for(byte = 0; byte < 4; byte++)
        {
            crc ^= (words[i] >> (byte * 8)) & 0xFFU;
            for(bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
        }
    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t record_valid(const uint32_t *r)
{
    return r[0] == QUOTA_MAGIC && r[2] <= 1 && r[3] <= BURN_QUOTA_MAX &&
           (r[2] || r[3] == 0) && r[4] == ~r[3] && r[5] == checksum(r) &&
           r[6] == ~r[1] && r[7] == QUOTA_COMMIT;
}

void burn_quota_init(void)
{
    uint32_t r[QUOTA_WORDS];
    uint8_t page, slot, i, empty, any_data = 0, occupied[2] = {0, 0};
    quota_sequence = quota_remaining = 0;
    quota_enabled = quota_fault = quota_reserved = quota_valid = quota_page = quota_next = 0;
    for(page = 0; page < 2; page++)
        for(slot = 0; slot < QUOTA_SLOTS; slot++)
        {
            empty = 1;
            for(i = 0; i < QUOTA_WORDS; i++)
            {
                r[i] = burn_quota_read_word(record_address(page, slot) + i * 4U);
                if(r[i] != 0xFFFFFFFFUL) empty = 0;
            }
            if(empty) continue;
            any_data = 1;
            occupied[page] = slot + 1;
            if(!record_valid(r))
            {
                /* A torn uncommitted append is ignored. A committed record
                 * with damaged data fails closed instead of restoring credit. */
                if(r[7] == QUOTA_COMMIT) quota_fault = 1;
                continue;
            }
            if(!quota_valid || (int32_t)(r[1] - quota_sequence) > 0)
            {
                quota_valid = 1;
                quota_sequence = r[1]; quota_enabled = (uint8_t)r[2];
                quota_remaining = r[3]; quota_page = page;
            }
        }
    quota_next = occupied[quota_page];
    if(any_data && !quota_valid) quota_fault = 1;
}

uint8_t burn_quota_enabled(void) { return quota_enabled; }
uint8_t burn_quota_fault(void) { return quota_fault; }
uint32_t burn_quota_remaining(void) { return quota_remaining; }

static uint8_t append(uint8_t enabled, uint32_t remaining, uint8_t repair)
{
    uint32_t r[QUOTA_WORDS], address;
    uint8_t i, page = quota_page, slot = quota_next;
    if(repair || slot >= QUOTA_SLOTS)
    {
        page ^= 1U; slot = 0;
        /* Keep the current committed page intact until the replacement commits. */
        if(!burn_quota_erase_page(record_address(page, 0))) goto storage_error;
    }
    address = record_address(page, slot);
    r[0] = QUOTA_MAGIC; r[1] = quota_sequence + 1U; r[2] = enabled;
    r[3] = remaining; r[4] = ~remaining; r[5] = checksum(r);
    r[6] = ~r[1]; r[7] = QUOTA_COMMIT;
    for(i = 0; i < QUOTA_WORDS; i++)
    {
        if(!burn_quota_program_word(address + i * 4U, r[i]) ||
           burn_quota_read_word(address + i * 4U) != r[i]) goto storage_error;
    }
    if(repair && !burn_quota_erase_page(record_address(quota_page, 0))) goto storage_error;
    quota_sequence = r[1]; quota_enabled = enabled; quota_remaining = remaining;
    quota_page = page; quota_next = slot + 1; quota_valid = 1; quota_fault = 0;
    return BURN_QUOTA_OK;
storage_error:
    quota_fault = 1;
    return BURN_QUOTA_STORAGE;
}

uint8_t burn_quota_set(uint8_t enabled, uint32_t remaining)
{
    if(quota_reserved) return BURN_QUOTA_BUSY;
    if(enabled > 1 || remaining > BURN_QUOTA_MAX || (!enabled && remaining))
        return BURN_QUOTA_STORAGE;
    if(!quota_fault && quota_valid && enabled == quota_enabled && remaining == quota_remaining)
        return BURN_QUOTA_OK;
    return append(enabled, remaining, quota_fault);
}

uint8_t burn_quota_begin(void)
{
    uint8_t result;
    if(quota_reserved) return BURN_QUOTA_BUSY;
    if(quota_fault) return BURN_QUOTA_STORAGE;
    if(!quota_enabled) return BURN_QUOTA_OK;
    if(!quota_remaining) return BURN_QUOTA_EMPTY;
    result = append(1, quota_remaining - 1U, 0);
    if(result == BURN_QUOTA_OK) quota_reserved = 1;
    return result;
}

uint8_t burn_quota_finish(uint8_t success)
{
    if(!quota_reserved) return BURN_QUOTA_OK;
    quota_reserved = 0;
    if(success) return BURN_QUOTA_OK;
    return append(1, quota_remaining + 1U, 0);
}
