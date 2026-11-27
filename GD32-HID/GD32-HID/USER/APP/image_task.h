/* 镜像任务索引：100 槽，索引放在 SPI Flash 顶部 64KB 的裸扇区。
 * bank A 0x00FF0000、bank B 0x00FF4000，各预留 16KB；不经过 FAT。
 * 记录与 bank 均为小端、packed；禁止在 RAM 实例化整表（11,220B）。 */
#ifndef IMAGE_TASK_H
#define IMAGE_TASK_H

#include <stdint.h>
#include "selection_store.h"
#include "ff.h"

#define IMAGE_SLOTS               100U
#define IMAGE_ID_NONE             0xFFU
#define IMAGE_RECORD_SIZE         112U
#define IMAGE_BANK_HEADER_SIZE    16U
#define IMAGE_BANK_DATA_SIZE      11216U   /* header + 100 records，CRC 覆盖范围 */
#define IMAGE_BANK_SIZE           11220U   /* + 4B CRC */
#define IMAGE_BANK_A_ADDR         0x00FF0000UL
#define IMAGE_BANK_B_ADDR         0x00FF4000UL
#define IMAGE_BANK_RESERVE        0x4000UL /* 每 bank 预留 16KB（4 个 4KB 扇区） */
#define IMAGE_BANK_MAGIC          0x31474D49UL /* "IMG1" */
#define IMAGE_BANK_VERSION        0x0001U
#define IMAGE_BANK_FLAG_VALID     0x01U
#define IMAGE_REC_FLAG_VALID      0x01U
#define IMAGE_REC_FLAG_OPT        0x02U
#define IMAGE_REC_FLAG_SUM        0x04U
#define IMAGE_REC_FLAG_CHANNEL_SET 0x08U  /* no bit on legacy records */
#define IMAGE_REC_FLAG_CHANNEL_B   0x10U
#define IMAGE_FW_MAX              102400UL  /* 每槽固件上限 100KB */
#define IMAGE_STAGE_FILE          "0:/download/stg.dat"

/* 镜像应用层错误码（run.c image_apply / 协议 detail 共用）。 */
enum {
    IMG_OK = 0,
    IMG_ERR_CRC,
    IMG_ERR_FMT,
    IMG_ERR_FILE,
    IMG_ERR_OPT,
    IMG_ERR_SUM,
    IMG_ERR_QUOTA,
    IMG_ERR_SAVE,
    IMG_ERR_BUSY,
    IMG_ERR_STORAGE,
    IMG_ERR_TABLE
};

typedef struct __attribute__((packed))
{
    uint8_t  id;              /* 0..99，必须等于数组下标 */
    uint8_t  flags;           /* IMAGE_REC_FLAG_*; legacy records have no channel setting */
    uint32_t checksum;        /* 主固件 SUM32；偏移 2，不可按 uint32_t* 解引用 */
    char     algo[32];        /* 算法 basename，ASCII，NUL 结尾 */
    char     bin[32];         /* 主固件 basename，ASCII，NUL 结尾 */
    char     chip[16];        /* 设备显示芯片名，ASCII */
    char     desc[24];        /* 短说明，ASCII；设备最多显示 14 字符 */
    uint8_t  reserved[2];
} image_record_t;

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint16_t slots;
    uint32_t seq;
    uint8_t  current;         /* 0..99 或 0xFF=NONE */
    uint8_t  flags;           /* bit0=表有效 */
    uint16_t reserved;
} image_bank_header_t;

/* 编译期布局检查：ARMCC 5.06 支持 packed 属性。 */
typedef char image_record_size_check[(sizeof(image_record_t) == IMAGE_RECORD_SIZE) ? 1 : -1];
typedef char image_header_size_check[(sizeof(image_bank_header_t) == IMAGE_BANK_HEADER_SIZE) ? 1 : -1];
typedef char image_bank_size_check[
    (IMAGE_BANK_HEADER_SIZE + IMAGE_SLOTS * IMAGE_RECORD_SIZE + 4U == IMAGE_BANK_SIZE) ? 1 : -1];

/* 上电读取 A/B，选 seq 更新且 CRC 有效的一份；只读裸 Flash，不写。 */
void image_store_init(void);
/* 返回 1 表示存在有效索引表。 */
uint8_t image_store_valid(void);
/* 返回 1 表示芯片容量不足/裸区读写失败等存储故障。 */
uint8_t image_store_fault(void);
/* 当前镜像号；无有效表/无当前记录返回 IMAGE_ID_NONE。 */
uint8_t image_store_current(void);
/* 当前记录只读指针；无有效当前记录返回 NULL。 */
const image_record_t *image_store_current_record(void);
/* 只改 RAM 当前镜像号（启动对账/匹配用），不写 bank。返回 1 表示记录可用。 */
uint8_t image_store_select_ram(uint8_t id);
/* 只清 RAM 当前镜像号（手动选择/USB 导入后显示 ---），不写 bank。 */
void image_store_clear_current_ram(void);
/* 读取指定槽记录；id 越界或无有效表返回 0。不要求记录 flags 有效。 */
uint8_t image_store_read_record(uint8_t id, image_record_t *out);
/* 按算法/固件/OPT/SUM 匹配唯一记录；多义时优先当前镜像。返回 1 表示唯一匹配。 */
uint8_t image_store_find_match(const uint8_t *algo, const uint8_t *bin, uint8_t opt,
                               uint32_t checksum, uint8_t checksum_valid, uint8_t *id_out);
/* 首次升级迁移：无有效表时把当前选择写成镜像 000。返回 1 表示迁移/已有表成功。 */
uint8_t image_store_migrate_from_selection(const program_selection_t *sel);
/* 设备端切换当前镜像：把活动 bank 复制到另一份并更新 current/seq。返回 1 成功。 */
uint8_t image_store_set_current(uint8_t id);

/* Replace one record, preserving the other 99, and make it current. */
uint8_t image_store_upsert(const image_record_t *rec);
/* Main-task validation before selecting a stored or newly imported image. */
uint8_t image_validate_record(const image_record_t *rec, uint32_t *sum_out);

/* 表信息（无有效表时 seq=0、crc=0）。 */
uint32_t image_store_seq(void);
uint32_t image_store_crc(void);
/* 从上位机逐字节提交的暂存表（11,220B）写入非活动 bank 并校验。
 * fil       - 已打开在 0:/download/stg.dat 的 FIL，函数内部 f_lseek(0)
 * expected_seq - 上位机读到的 active seq；必须等于当前 active seq
 * expected_crc - 上位机计算的整表 CRC32
 * detail    - 失败细节：1=尺寸/格式/CRC，2=文件读失败，3=seq冲突，4=写后表无效
 * 返回 1 表示新 bank 已成为有效活动表。 */
uint8_t image_store_commit_from_file(FIL *fil, uint32_t expected_seq,
                                     uint32_t expected_crc, uint8_t *detail);

#endif
