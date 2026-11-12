# Task 1 显示层独立审查

审查范围：`task-1-brief.md`、`task-1-report.md`、`display/display.diff`，以 `portrait/original` 为基线。只读检查产品源码；未重跑已有通过的大测试，未修改产品文件。

**规格结论：通过（Task 1 BSP 范围）。** 默认应用方向 0、240×320；`LCD_BAREMETAL` 默认方向 1，显式 `LCD_DIRECTION` 覆盖仍优先。核对 Bootloader 工程确认其全局定义 `LCD_BAREMETAL`，并引用这份共享 `bsp_lcd.c`，方向守护适用于实际工程。

**质量结论：通过（可交给 root 集成）。** 未发现需修改的 P1/P2 问题。接口、边界判断及缓存逻辑与此任务要求相符；该结论不代表整机编译、实机显示或整个旧项目通过审计。

## 关键检查

- `UI_ProgressStatus` 签名和四个状态枚举符合 brief；固定 16 像素高，运行状态前缀百分比并填黄，失败/成功填满红/绿，空闲清空填充。百分比饱和到 100；零宽、屏外及底部不够 16 像素时拒绝绘制。
- 本地/静态文字缓存均按最大可见字节数分配，裁剪后字符串有终止符，GBK 两字节整体接收。中文缺字使用问号并保留完整字符占位；脏区背景覆盖负责清除占位剩余部分。
- 文字差异、长短变化、ASCII/GBK 宽度变化和填充端点位移合并为脏区。颜色变化使整条失效；填充变化即使穿过未变字模，也会重画所有相交字模。`OLED_Clear` 使新缓存失效。未变可见内容无需写屏。
- `LCD_BlitMonoSplit` 保持原始字模坐标，使用绝对裁剪区间；右边界/下边界先裁剪再计算地址。行排列和列分页点阵均直接发送 RGB565 高低字节，事务沿用 128 像素上限，无像素行缓存或帧缓冲。其软件反显操作与旧点阵 API 一致；当前主页面未调用反显切换。
- 字库增量为 17 个 16×16 SimSun 字模；测试核查表按 GBK 排序且无重复，并覆盖当前 `run.c` 的 GBK 转义文字。`UI_Logo` 与旧显示入口保留。
- fixture map 中 `bsp_oled.o` 的 `.bss` 从 `0x4e0` 增至 `0x50a`，与新增 42 字节缓存一致。31 字节局部文字数组符合 240 像素上限；最终优化构建的任务栈余量仍由 root 判断。

## 测试证据审查

`display/results.json` 记录 7/7 PASS：overlay_and_reset、state_color_and_shorten、dirty_fill_crossing、clipping、split_real_spi_colors、glyph_coverage、mixed_glyph_transitions。准确地说，前述六组绘制/裁剪/颜色回归执行 ARMCC 编译的真实 Thumb BSP 代码，`glyph_coverage` 是源码字库覆盖/排序检查。

夹具只替代窗口/SPI 端口，像素值来自真实渲染函数；0/37/100/reset 的预期填充位置独立指定，叠字 mask 使用既有文字渲染器。RGB565 测试覆盖黑、白、红、绿、蓝、黄、青、品红、灰，并明确比对字节组合后的像素。缓存重复调用、同百分比换色、填充穿字、长到短、GBK 裁剪、混合字宽、不可见尾部变化和清屏失效均有对应断言。

已核对五个修改 BSP 文件：当前源码、fixture 副本与 `source-audit.json` SHA-256 全部一致。`fixture.axf` 与 `results.json` 时间相邻；`compile.log` 只有空行，无编译/链接告警。已读失败基线与缺 API/缺字的 RED 记录。没有将这些记录扩展解释为 Bootloader 构建或 ST7789 实机验证。

## P1/P2 与剩余边界

P1：无。P2：无。

单个状态条缓存的区域所有权已在 handoff 说明；外部代码直接覆盖该区域后应清屏使缓存失效。完整应用/Bootloader 编译、主页面像素集成、任务栈余量和实机方向/观感由 root 后续验证；本次审查没有发现需扩大到旧项目审计的具名问题。
