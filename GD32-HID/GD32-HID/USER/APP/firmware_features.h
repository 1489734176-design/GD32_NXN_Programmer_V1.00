/* 中文注释补充（2026-10-26）：在线仿真总开关：默认0关闭专用HID/任务/缓冲区，1恢复；离线SWD/OPT与文件传输始终保留。 */
#ifndef FIRMWARE_FEATURES_H
#define FIRMWARE_FEATURES_H

/* 在线仿真总开关：0=仅离线烧录器+USB文件传输/CDC；1=额外启用CMSIS-DAP HID。
 * 两种配置都保留SWD，离线Flash/OPT编程依赖它。
 * 可在Keil编译器预定义宏里覆盖；工程未指定时默认0（关闭在线仿真）。 */
#ifndef DAP_ONLINE_ENABLE
#define DAP_ONLINE_ENABLE 0
#endif

/* 编译期检查：该开关仅允许0或1，其他数值报错。 */
#if (DAP_ONLINE_ENABLE != 0) && (DAP_ONLINE_ENABLE != 1)
#error "DAP_ONLINE_ENABLE must be 0 or 1"
#endif

#endif /* FIRMWARE_FEATURES_H */
