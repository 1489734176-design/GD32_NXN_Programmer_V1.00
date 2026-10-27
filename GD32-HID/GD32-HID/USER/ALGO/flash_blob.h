/*
 * @Descripttion : 
 * @version      : 
 * @Author       : Kevincoooool
 * @Date         : 2020-08-22 17:55:57
 * @LastEditors  : Kevincoooool
 * @LastEditTime : 2020-12-02 13:39:56
 * @FilePath     : \Simple_TeenyUSB_TX\CMSIS-DAP\algo\flash_blob.h
 */
#ifndef FLASH_BLOB_H
#define FLASH_BLOB_H

#include <stdint.h>


typedef struct {
    uint32_t breakpoint;
    uint32_t static_base;
    uint32_t stack_pointer;
} program_syscall_t;

typedef struct {
    uint32_t  init;
    uint32_t  uninit;
    uint32_t  erase_chip;
    uint32_t  erase_sector;
    uint32_t  program_page;
    program_syscall_t sys_call_s;
    uint32_t  program_buffer;
    uint32_t  algo_start;
    uint32_t  program_buffer_size;
		uint32_t	flash_addr;
		uint32_t	flash_size;
    uint32_t  algo_size;
    uint32_t 	algo_blob[1024];
} program_target_t;

typedef struct {
    const uint32_t start;
    const uint32_t size;
} sector_info_t;

/* 带OPT字节支持的扩展编程目标结构（17个头部字段） */
typedef struct {
    uint32_t  init;
    uint32_t  uninit;
    uint32_t  erase_chip;
    uint32_t  erase_sector;
    uint32_t  program_page;
    program_syscall_t sys_call_s;
    uint32_t  program_buffer;
    uint32_t  algo_start;
    uint32_t  program_buffer_size;
    uint32_t  flash_addr;
    uint32_t  flash_size;
    uint32_t  algo_size;
    uint32_t  opt_program;    /* ProgramOpt函数地址 */
    uint32_t  opt_addr;       /* OPT基地址 */
    uint32_t  opt_size;       /* OPT字节数 */
} program_target_ext_t;

#define PROG_TARGET_EXT_HDR_SIZE  68   /* 17个字段 × 4字节 */

typedef struct {

	char * name;
    program_target_t algo;
} algo_info_t;


#endif
