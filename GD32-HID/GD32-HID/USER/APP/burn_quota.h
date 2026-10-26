/* 中文注释补充（2026-10-26）：次数限制接口：0～999，全设备共用；Flash最后两个1KB页保存日志，开始预扣、普通失败退款、掉电保留。 */
#ifndef BURN_QUOTA_H
#define BURN_QUOTA_H
#include <stdint.h>

/* Global device quota. 0..999 allows fixed three-digit LCD presentation.
 * The last two 1KB MCU Flash pages are excluded from the app linker region. */
#define BURN_QUOTA_MAX 999U
#define BURN_QUOTA_FLASH_BASE 0x0801F800UL
#define BURN_QUOTA_PAGE_SIZE 1024U
enum { BURN_QUOTA_OK = 0, BURN_QUOTA_EMPTY, BURN_QUOTA_STORAGE, BURN_QUOTA_BUSY };

/* 扫描两页次数日志，选择最新完整记录；未提交记录忽略，已提交损坏记录禁止烧录。 */
void burn_quota_init(void);
/* 查询是否启用次数限制。 */
uint8_t burn_quota_enabled(void);
/* 查询次数记录是否异常；异常时拒绝开始烧录。 */
uint8_t burn_quota_fault(void);
/* 查询当前剩余次数，界面以三位数字补零显示。 */
uint32_t burn_quota_remaining(void);
/* 保存上位机设置的0～999额度或不限；烧录预扣尚未结束时拒绝重设，相同设置不重复擦写。 */
uint8_t burn_quota_set(uint8_t enabled, uint32_t remaining);
/* Reserve durably BEFORE powering/programming the target. Normal failures
 * refund; interrupted power retains the reservation. Success needs no write. */
/* 目标芯片上电/烧录前持久化预扣一次；额度为零或存储异常时拒绝开始。 */
uint8_t burn_quota_begin(void);
/* 结束本次预扣：成功不再写Flash，普通失败退回一次；掉电未执行本函数时保留扣减。 */
uint8_t burn_quota_finish(uint8_t success);

/* Storage adapter: the journal is portable and fault-testable independently. */
/* 读取次数日志中的32位数据；越界地址不读。 */
uint32_t burn_quota_read_word(uint32_t address);
/* 在短临界区内解锁FMC、写入一个日志字并重新锁定；由日志层读回验证。 */
uint8_t burn_quota_program_word(uint32_t address, uint32_t value);
/* 擦除预留次数页；地址检查保证不擦应用或启动程序。 */
uint8_t burn_quota_erase_page(uint32_t address);
#endif
