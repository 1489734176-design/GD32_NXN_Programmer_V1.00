#ifndef ERROR_H
#define ERROR_H


// 本枚举顺序必须与 error.c 里的 error_message[]、error_type[] 保持一致
typedef enum
{
    /* 通用错误 */
    ERROR_SUCCESS = 0,
    ERROR_FAILURE,
    ERROR_INTERNAL,

    /* 目标Flash相关错误 */
    ERROR_RESET,
    ERROR_ALGO_DL,
    ERROR_ALGO_DATA_SEQ,
    ERROR_INIT,
    ERROR_SECURITY_BITS,
    ERROR_UNLOCK,
    ERROR_ERASE_SECTOR,
    ERROR_ERASE_ALL,
    ERROR_WRITE,
    ERROR_VERIFY,

    // 新增错误码请加在这一行之前

    ERROR_COUNT
} error_t;


typedef unsigned char error_type_t;

#define ERROR_TYPE_INTERNAL 	0x1
#define ERROR_TYPE_TRANSIENT 	0x2
#define ERROR_TYPE_USER 		0x4
#define ERROR_TYPE_TARGET 		0x8

#define ERROR_TYPE_MASK 		0xF


/* 按错误码取出可读的错误描述字符串（英文原文，上位机依赖）。
 * error - 错误码，0 ~ ERROR_COUNT-1
 * 返回  - 静态字符串指针；越界返回NULL */
const char *error_get_string(error_t error);

/* 按错误码取出错误类型。
 * error - 错误码，0 ~ ERROR_COUNT-1
 * 返回  - ERROR_TYPE_INTERNAL / _TRANSIENT / _USER / _TARGET；越界按INTERNAL处理 */
error_type_t error_get_type(error_t error);


#endif
