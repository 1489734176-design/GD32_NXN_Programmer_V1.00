#ifndef __SWD_FLASH_H__
#define __SWD_FLASH_H__

#include <stdint.h>
#include "flash_blob.h"
#include "error.h"

error_t target_flash_init(program_target_t *algo, uint32_t flash_start);
error_t target_flash_uninit(program_target_t *algo);
error_t target_flash_program_page(program_target_t *algo, uint32_t addr, const uint8_t *buf, uint32_t size);
error_t target_flash_erase_sector(program_target_t *algo, uint32_t addr);
error_t target_flash_erase_chip(program_target_t *algo);


#endif // __SWD_FLASH_H__
