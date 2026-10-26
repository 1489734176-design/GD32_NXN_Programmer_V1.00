/* 中文注释补充（2026-10-26）：在线仿真总开关：默认0关闭专用HID/任务/缓冲区，1恢复；离线SWD/OPT与文件传输始终保留。 */
#ifndef FIRMWARE_FEATURES_H
#define FIRMWARE_FEATURES_H

/* 0: offline programmer + USB file transfer/CDC; 1: also enable CMSIS-DAP HID.
 * Keep SWD enabled in both configurations: offline Flash/OPT programming uses it.
 * May also be overridden in the Keil compiler preprocessor definitions.
 */
/* 工程未指定该宏时默认关闭在线仿真；工程预定义的值优先。 */
#ifndef DAP_ONLINE_ENABLE
#define DAP_ONLINE_ENABLE 0
#endif

/* 编译期检查：该开关仅允许0或1，其他数值报错。 */
#if (DAP_ONLINE_ENABLE != 0) && (DAP_ONLINE_ENABLE != 1)
#error "DAP_ONLINE_ENABLE must be 0 or 1"
#endif

#endif /* FIRMWARE_FEATURES_H */
