#ifndef FIRMWARE_FEATURES_H
#define FIRMWARE_FEATURES_H

/* 0: offline programmer + USB file transfer/CDC; 1: also enable CMSIS-DAP HID.
 * Keep SWD enabled in both configurations: offline Flash/OPT programming uses it.
 * May also be overridden in the Keil compiler preprocessor definitions.
 */
#ifndef DAP_ONLINE_ENABLE
#define DAP_ONLINE_ENABLE 0
#endif

#if (DAP_ONLINE_ENABLE != 0) && (DAP_ONLINE_ENABLE != 1)
#error "DAP_ONLINE_ENABLE must be 0 or 1"
#endif

#endif /* FIRMWARE_FEATURES_H */
