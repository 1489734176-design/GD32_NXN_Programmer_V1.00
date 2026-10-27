#ifndef __SWD_OPT_H__
#define __SWD_OPT_H__

#include <stdint.h>
#include "flash_blob.h"
#include "error.h"

/* 初始化OPT编程：复位暂停内核、下装算法、执行算法init（入参全为0）。
 * algo - OPT算法描述
 * 返回 - ERROR_SUCCESS / ERROR_RESET / ERROR_ALGO_DL / ERROR_INIT */
error_t target_opt_init(program_target_t *algo);
/* 结束OPT编程：执行算法uninit（不改变目标运行状态）。
 * algo - OPT算法描述
 * 返回 - ERROR_SUCCESS / ERROR_INIT */
error_t target_opt_uninit(program_target_t *algo);
/* 把一段数据整段写入目标OPT区。
 * algo - OPT算法描述（program_buffer / program_page）
 * addr - 目标OPT区地址
 * buf  - 源数据缓冲区
 * size - 待写入字节数，须不超过program_buffer容量
 * 返回 - ERROR_SUCCESS / ERROR_ALGO_DATA_SEQ / ERROR_WRITE */
error_t target_opt_program_page(program_target_t *algo, uint32_t addr, const uint8_t *buf, uint32_t size);
/* 擦除OPT区所在扇区。
 * algo - OPT算法描述（erase_sector入口）
 * addr - 要擦除的扇区地址
 * 返回 - ERROR_SUCCESS / ERROR_ERASE_SECTOR */
error_t target_opt_erase_sector(program_target_t *algo, uint32_t addr);
/* 擦除整片OPT区。
 * algo - OPT算法描述（erase_chip入口）
 * 返回 - ERROR_SUCCESS / ERROR_ERASE_ALL */
error_t target_opt_erase_chip(program_target_t *algo);

/* 用扩展算法写入OPT字节，地址与长度取自算法描述表。
 * algo     - 扩展算法描述（opt_addr / opt_size / opt_program / program_buffer）
 * opt_data - OPT数据缓冲区，长度不小于algo->opt_size
 * 返回     - ERROR_SUCCESS / ERROR_ALGO_DATA_SEQ / ERROR_WRITE */
error_t target_opt_program_ext(program_target_ext_t *algo, const uint8_t *opt_data);


#endif // __SWD_OPT_H__
