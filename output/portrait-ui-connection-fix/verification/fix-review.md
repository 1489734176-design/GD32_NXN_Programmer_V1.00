# DATA_BUFFER 对齐修复最终复核

2026-10-29，限范围只读复核。结论：**通过；未发现本次最小修复范围内的可操作问题。** 未改产品文件、未再次编译、未重复SPI大探针。

## 修复是否覆盖已证实根因

原定义 `uint8_t DATA_BUFFER[4096]` 的最低对齐要求只有1。`USER/APP/run.c` 将该地址用作由uint32_t组成的 `program_target_t` / `program_target_ext_t`，并由真实ARMCC产物使用LDRD读取头字段，因此实际要求至少4字节对齐。

`USER/BSP/bsp_gd25qxx.c:209` 改为 `__align(4) uint8_t DATA_BUFFER[4096];` 在对象定义处声明了正确约束，消除其地址随其他UI静态变量大小变化而失去对齐的根因。不是仅把本次地址偶然移动到对齐位置。Cortex-M3的该LDRD要求字对齐，不需要把本对象强制提高到8字节。

检查了全部产品侧DATA_BUFFER引用：

- 标准算法头和扩展算法头都从基址强转；其成员仅uint32_t及由uint32_t构成的sys_call_s，成员偏移都是4的倍数。
- 扩展blob压缩使用源`+68`、目的`+56`；两个偏移均为4的倍数，且memmove本身允许字节地址，修复不改变容器布局或搬移长度。
- 固件页和OPT字节区使用`+2048`，仍为4字节对齐。算法blob传给SWD时起于标准头`+56`，符合后续按字传输对源缓冲区的既有假设。
- 校验和循环、FatFs文件读入及GD25QXX扇区暂存继续按字节使用；增加对齐不改变数组长度、字节内容、指针类型或这些调用约定。
- `extern uint8_t DATA_BUFFER[4096]` 可以保留：对象存储对齐由此唯一的定义和链接器保证；消费者最终形成的地址已在产物中确认。无须改外部声明或打包头格式。

本结论针对已有正常算法数据的已证实对齐问题；未将历史上的算法长度验证等其他问题混入修复。

## 编译器和Bootloader

本工程构建日志明确使用 **ARMCC V5.06 update 7 build 960**；`__align(4)` 是该编译器支持的变量对齐语法，当前GD32库的 `gd32f10x_enet.c:43` 等也使用相同语法。重建日志结尾为0 errors / 0 warnings，链接符号确认约束生效。没有引入本工程工具链兼容问题；本次没有声称支持未使用的GCC工具链。

Bootloader工程 `DAPlink/MDK-ARM/DAPlink.uvprojx:635` 引用其自己的 `../../USER/BSP/bsp_gd25qxx.c`，该文件的扇区缓冲名为 `GD25QXX_BUFFER`，不使用应用的DATA_BUFFER。虽然Bootloader共享LCD/SPI0驱动，但此次未改这些共享文件。因此无须修改或重新构建Bootloader，也没有理由连带改其旧扇区缓冲。

## 独立产物核对

重新运行已有纯读取检查（未重编译）：

```powershell
& 'D:\Program Files\Python314\python.exe' -I '.planning/2026-10-29-portrait-freeze/test_algorithm_alignment.py' --image '.planning/2026-10-29-portrait-freeze' --tag review-green
```

Exit 0；详细结果 `algorithm-alignment-review-green.json`：

- 新AXF SHA256：`411cb2cbf7cbf444681da4fcbdaa99ded6d615a467830998d4fd3c54739cdada`。
- DATA_BUFFER：`0x200026b8`，大小4096。
- `0x08013dee` 的literal load常量池仍对应真实DATA_BUFFER，值为`0x200026b8`。
- 原故障指令仍为 `0x08013e00: LDRD r10,r1,[r0,#56]`，bytes=`d0e90ea1`。
- 实际访问地址变为 `0x200026f0`，模4=0。原始失败镜像则为`0x200026ee`，模4=2。

此检查验证已交付架构约束，不把Unicorn允许未对齐LDRD误当作通过依据。尚需实机安装后确认自动/手动实际烧录完成，不能把此代码审查称为实机复测。

## 修改边界核对

- 对比本次 `original/bsp_gd25qxx.c`，唯一文本差异是第208行注释和第209行定义（以及该定义行末旧空白）。
- 对照原竖屏发布 `output/portrait-ui/manifest.json`，其中全部11个产品源码哈希仍一致：显示5文件、quota3文件、USB bulk1文件、run.c/run.h。
- 补充对照竖屏原始快照：SPI0 c/h、Flash h、power c/h、key c/h、timer c等8个文件及Keil uvprojx均字节相同。
- 所有可用发布清单和相关硬件快照内，未发现除该定义/注释外的产品源码改动。仓库没有Git或完整修复前全库manifest，因此这是一项对可用基线及本次相关文件的核对，不伪称全仓库历史差异已被完整证明。
