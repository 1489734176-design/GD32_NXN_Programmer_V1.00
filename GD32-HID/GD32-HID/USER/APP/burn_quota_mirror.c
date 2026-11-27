/* 中文说明：外部Flash保存当前次数的完整副本。同步、关闭并回读后才算写入成功。 */
#include "burn_quota_mirror.h"
#include "quota_auth.h"
#include "ff.h"

#define MIRROR_PATH "0:/system/burnquota.chk"
/* 只在主任务串行调用，避免把带扇区缓冲的FIL放到任务栈上。 */
static FIL mirror_file;

uint8_t burn_quota_mirror_probe(void)
{
    FRESULT result = f_open(&mirror_file, MIRROR_PATH, FA_READ);
    if(result == FR_NO_FILE || result == FR_NO_PATH) return 0;
    if(result != FR_OK) return 2;
    return f_close(&mirror_file) == FR_OK ? 1 : 2;
}

uint8_t burn_quota_mirror_read(void *record)
{
    UINT received = 0;
    uint8_t valid;
    if(f_open(&mirror_file, MIRROR_PATH, FA_READ) != FR_OK) return 0;
    valid = f_size(&mirror_file) == BURN_QUOTA_RECORD_SIZE &&
            f_read(&mirror_file, record, BURN_QUOTA_RECORD_SIZE, &received) == FR_OK &&
            received == BURN_QUOTA_RECORD_SIZE;
    if(f_close(&mirror_file) != FR_OK) valid = 0;
    return valid;
}

uint8_t burn_quota_mirror_write(const void *record, uint8_t rebuild)
{
    uint32_t check[BURN_QUOTA_RECORD_SIZE / 4U];
    UINT written = 0;
    uint8_t valid, reuse = 0;
    FRESULT result = FR_OK;
    if(!rebuild) {
        result = f_open(&mirror_file, MIRROR_PATH, FA_WRITE);
        if(result == FR_OK) {
            reuse = f_size(&mirror_file) == BURN_QUOTA_RECORD_SIZE;
            if(!reuse && f_close(&mirror_file) != FR_OK) return 0;
        } else if(result != FR_NO_PATH && result != FR_NO_FILE) return 0;
    }
    if(!reuse) {
        if(rebuild || result == FR_NO_PATH) {
            result = f_mkdir("0:/system");
            if(result != FR_OK && result != FR_EXIST) return 0;
        }
        if(f_open(&mirror_file, MIRROR_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return 0;
    }
    /* 先更新外部副本；此时掉电会与旧内部记录不一致，重启后必须禁烧。 */
    valid = f_write(&mirror_file, record, BURN_QUOTA_RECORD_SIZE, &written) == FR_OK &&
            written == BURN_QUOTA_RECORD_SIZE && f_sync(&mirror_file) == FR_OK;
    if(f_close(&mirror_file) != FR_OK) valid = 0;
    return valid && burn_quota_mirror_read(check) &&
           quota_auth_equal(check, record, BURN_QUOTA_RECORD_SIZE);
}
