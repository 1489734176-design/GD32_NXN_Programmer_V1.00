/* 中文注释：所选算法、固件和校验状态的掉电保存接口；次数由burn_quota独立管理。 */
#ifndef SELECTION_STORE_H
#define SELECTION_STORE_H
#include <stdint.h>

#define SELECTION_STORE_FLASH_BASE 0x0801F000UL
#define SELECTION_STORE_PAGE_SIZE 1024U
#define SELECTION_STORE_NAME_SIZE 32U

typedef struct {
    uint8_t algorithm_name[SELECTION_STORE_NAME_SIZE];
    uint8_t program_name[SELECTION_STORE_NAME_SIZE];
    uint32_t checksum;
    uint8_t opt_enabled;
    uint8_t checksum_valid;
} program_selection_t;

/* 扫描独立两页日志；未提交记录忽略，已提交损坏记录禁止恢复旧配置。 */
void selection_store_init(void);
/* 返回1表示已复制最新完整配置；空日志、存储异常或空指针返回0。 */
uint8_t selection_store_load(program_selection_t *out);
/* 保存新的完整/部分/空配置；相同配置不重复写入，成功返回1，失败返回0。 */
uint8_t selection_store_save(const program_selection_t *in);
/* 返回1表示日志或本次存储操作异常，须显式保存新配置才能恢复。 */
uint8_t selection_store_fault(void);

/* 存储适配层只访问0x0801F000～0x0801F7FF，不读写相邻次数日志。 */
uint32_t selection_store_read_word(uint32_t address);
uint8_t selection_store_program_word(uint32_t address, uint32_t value);
uint8_t selection_store_erase_page(uint32_t address);
#endif
