#ifndef __SWD_OPT_H__
#define __SWD_OPT_H__

#include <stdint.h>
#include "flash_blob.h"
#include "error.h"

error_t target_opt_init(program_target_t *algo);
error_t target_opt_uninit(program_target_t *algo);
error_t target_opt_program_page(program_target_t *algo, uint32_t addr, const uint8_t *buf, uint32_t size);
error_t target_opt_erase_sector(program_target_t *algo, uint32_t addr);
error_t target_opt_erase_chip(program_target_t *algo);

/* Extended OPT programming using program_target_ext_t */
error_t target_opt_program_ext(program_target_ext_t *algo, const uint8_t *opt_data);


#endif // __SWD_OPT_H__
