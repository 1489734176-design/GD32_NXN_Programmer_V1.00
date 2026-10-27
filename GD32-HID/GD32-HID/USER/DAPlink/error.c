/**
 * @file    error.c
 * @brief   错误码表，以及错误描述、错误类型的查询接口
 */

#include "error.h"

static const char *const error_message[] =
{

    /* 通用错误 */

    // ERROR_SUCCESS
    "Operation was successful",
    // ERROR_FAILURE
    "An error has occurred",
    // ERROR_INTERNAL
    "An internal error has occurred",


    /* 目标Flash相关错误 */

    // ERROR_RESET
    "The interface firmware FAILED to reset/halt the target MCU",
    // ERROR_ALGO_DL
    "The interface firmware FAILED to download the flash programming algorithms to the target MCU",
    // ERROR_ALGO_DATA_SEQ
    "The interface firmware FAILED to download the flash data contents to be programmed",
    // ERROR_INIT
    "The interface firmware FAILED to initialize the target MCU",
    // ERROR_SECURITY_BITS
    "The interface firmware ABORTED programming. Image is trying to set security bits",
    // ERROR_UNLOCK
    "The interface firmware FAILED to unlock the target for programming",
    // ERROR_ERASE_SECTOR
    "Flash algorithm erase sector command FAILURE",
    // ERROR_ERASE_ALL
    "Flash algorithm erase all command FAILURE",
    // ERROR_WRITE
    "Flash algorithm write command FAILURE",
};

static error_type_t error_type[] =
{

    /* 各错误码的类型固定不变，请勿随意调整顺序。 */

    // ERROR_SUCCESS
    0,
    // ERROR_FAILURE
    ERROR_TYPE_INTERNAL,
    // ERROR_INTERNAL
    ERROR_TYPE_INTERNAL,


    /* 目标Flash相关错误 */

    // ERROR_RESET
    ERROR_TYPE_TARGET,
    // ERROR_ALGO_DL
    ERROR_TYPE_TARGET,
    // ERROR_ALGO_DATA_SEQ
    ERROR_TYPE_TARGET,
    // ERROR_INIT
    ERROR_TYPE_TARGET,
    // ERROR_SECURITY_BITS
    ERROR_TYPE_USER,
    // ERROR_UNLOCK
    ERROR_TYPE_TARGET,
    // ERROR_ERASE_SECTOR
    ERROR_TYPE_TARGET,
    // ERROR_ERASE_ALL
    ERROR_TYPE_TARGET,
    // ERROR_WRITE
    ERROR_TYPE_TARGET,
};

/* 按错误码取出可读的错误描述字符串。
 * 表中的英文原文直接沿用DAPLink协议，上位机与调试器依赖这些文本，不做汉化。
 * error - 错误码，取值0 ~ ERROR_COUNT-1
 * 返回  - 指向静态字符串常量的指针；错误码越界时返回NULL */
const char *error_get_string(error_t error)
{
    const char *msg = 0;

    if(error < ERROR_COUNT)
        {
            msg = error_message[error];
        }

    return msg;
}

/* 按错误码取出错误类型，供上层区分是内部错误、目标错误还是用户操作错误。
 * error - 错误码，取值0 ~ ERROR_COUNT-1
 * 返回  - 对应错误类型；错误码越界时按ERROR_TYPE_INTERNAL处理 */
error_type_t error_get_type(error_t error)
{
    error_type_t type = ERROR_TYPE_INTERNAL;

    if(error < ERROR_COUNT)
        {
            type = error_type[error];
        }

    return type;
}
