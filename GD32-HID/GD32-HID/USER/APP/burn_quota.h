/* 按固件保存烧录次数：当前额度在内外Flash双存，HMAC一致才允许烧录。 */
#ifndef BURN_QUOTA_H
#define BURN_QUOTA_H
#include <stdint.h>
#define BURN_QUOTA_MAX 999U
#define BURN_QUOTA_FLASH_BASE 0x0801F800UL
#define BURN_QUOTA_PAGE_SIZE 1024U
#define BURN_QUOTA_NAME_SIZE 32U
enum { BURN_QUOTA_OK = 0, BURN_QUOTA_EMPTY, BURN_QUOTA_STORAGE, BURN_QUOTA_BUSY };
typedef struct { uint32_t length; uint8_t tag[16]; } burn_quota_catalog_t;
void burn_quota_init(void);
/* 只在启动时调用：旧全局额度归属最后保存的固件；无可恢复文件时清除归属。 */
uint8_t burn_quota_migrate_legacy(const uint8_t *name);
/* 主任务中选择固件；先保存原额度再切换，空名仅隐藏当前额度。 */
uint8_t burn_quota_select(const uint8_t *name);
uint8_t burn_quota_enabled(void);
uint8_t burn_quota_fault(void);
uint32_t burn_quota_remaining(void);
uint8_t burn_quota_set(uint8_t enabled, uint32_t remaining);
uint8_t burn_quota_begin(void);
uint8_t burn_quota_finish(uint8_t success);
uint32_t burn_quota_read_word(uint32_t address);
uint8_t burn_quota_program_word(uint32_t address, uint32_t value);
uint8_t burn_quota_erase_page(uint32_t address);
/* 账本长度和HMAC由双份记录保护；缺失已提交账本必须失败，不能当作不限。 */
uint8_t burn_quota_catalog_load(const burn_quota_catalog_t *catalog, const uint8_t *name,
                              uint8_t *enabled, uint32_t *remaining);
uint8_t burn_quota_catalog_save(const burn_quota_catalog_t *catalog, const uint8_t *name,
                              uint8_t enabled, uint32_t remaining, burn_quota_catalog_t *updated);
uint8_t burn_quota_catalog_migrate(uint32_t length, uint32_t crc, burn_quota_catalog_t *updated);
#endif
