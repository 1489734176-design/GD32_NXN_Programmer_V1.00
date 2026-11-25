#ifndef __SWD_FLASH_H__
#define __SWD_FLASH_H__

#include <stdint.h>
#include "flash_blob.h"
#include "error.h"

/* 初始化Flash编程：复位暂停内核、下装算法、执行算法init。
 * algo        - Flash算法描述（代码块、SRAM地址、各入口与系统调用表）
 * flash_start - 目标Flash起始地址，作为init的R0入参
 * 返回        - ERROR_SUCCESS / ERROR_RESET / ERROR_ALGO_DL / ERROR_INIT */
error_t target_flash_init(program_target_t *algo, uint32_t flash_start);
/* 结束Flash编程：执行算法uninit，把目标切回运行并释放SWD引脚。
 * algo - Flash算法描述
 * 返回 - 固定返回ERROR_SUCCESS */
error_t target_flash_uninit(program_target_t *algo);
/* 按算法缓冲区大小分块把数据写入目标Flash。
 * algo - Flash算法描述（program_buffer / program_buffer_size / program_page）
 * addr - 目标Flash起始地址
 * buf  - 源数据缓冲区
 * size - 待写入字节数
 * 返回 - ERROR_SUCCESS / ERROR_ALGO_DATA_SEQ / ERROR_WRITE */
error_t target_flash_program_page(program_target_t *algo, uint32_t addr, const uint8_t *buf, uint32_t size);
uint32_t target_flash_program_prepare(program_target_t *algo, uint32_t addr, const uint8_t *buf, uint32_t size);
uint8_t target_flash_program_start(program_target_t *algo, uint32_t addr);
uint8_t target_flash_program_wait(program_target_t *algo, uint32_t *r0);
/* 擦除目标Flash的一个扇区。
 * algo - Flash算法描述（erase_sector入口）
 * addr - 要擦除的扇区地址
 * 返回 - ERROR_SUCCESS / ERROR_ERASE_SECTOR */
error_t target_flash_erase_sector(program_target_t *algo, uint32_t addr);
extern uint32_t g_erase_diag;
extern uint32_t g_vfy_off;
extern uint32_t g_vfy_exp;
extern uint32_t g_vfy_act;
/* 整片擦除目标Flash；失败时把算法返回的R0存入g_erase_diag。
 * algo - Flash算法描述（erase_chip入口）
 * 返回 - ERROR_SUCCESS / ERROR_ERASE_ALL */
error_t target_flash_erase_chip(program_target_t *algo);
/* 逐块读回比对，校验目标Flash内容（不记录差异位置）。
 * algo     - Flash算法描述（本函数未使用其成员）
 * addr     - 目标Flash起始地址
 * expected - 期望数据缓冲区
 * size     - 校验字节数，内部按256字节分块
 * 返回     - ERROR_SUCCESS / ERROR_VERIFY */
error_t target_flash_verify(program_target_t *algo, uint32_t addr, const uint8_t *expected, uint32_t size);
/* 逐块读回比对，并记录第一处不一致的偏移与数据。
 * algo     - 扩展算法描述（本函数未使用其成员）
 * addr     - 目标Flash起始地址
 * expected - 期望数据缓冲区
 * size     - 校验字节数，内部按256字节分块
 * 返回     - ERROR_SUCCESS / ERROR_VERIFY；差异写入g_vfy_off/g_vfy_exp/g_vfy_act */
error_t target_flash_verify_ext(program_target_ext_t *algo, uint32_t addr, const uint8_t *expected, uint32_t size);


#endif // __SWD_FLASH_H__
