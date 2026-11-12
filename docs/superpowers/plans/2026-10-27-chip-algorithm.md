# Chip Algorithm Binding Implementation Plan

> **For agentic workers:** Follow scoped parallel assignments and preserve actual file encodings.

**Goal:** 固定两款芯片的配套算法，上位机和烧录器均不允许选择算法。
**Architecture:** 沿用导入事务下发算法名；烧录器以成功应用的算法名反查芯片名，并移除算法导航。
**Tech Stack:** Python 3.12 / PyQt5, C / ARMCC5, ST7789。
**Spec:** ../../../.planning/2026-10-27-chip-algorithm/task_plan.md

## Global Constraints
- MM32G0001A: MM32G001-OPT_ext.bin; MM32SPIN0230: MM32SPIN0230-OPT_ext.bin。
- 中文标签“芯片型号：”，新增逻辑配中文注释，保留实际编码。
- 不改USB协议、算法二进制布局、Bootloader或标准版。

## Review Focus
- 旧绑定不可覆盖固定关系。
- 未选芯片不可导入；两款芯片显示和ImportWorker参数一致。
- 按键不能进入算法目录或修改firmware_name；固件选择及开始烧录正常。
- 空/未知算法不可显示成已知芯片。
- 新字库支持中文及全角冒号；手动、自动和烧录页一致。

### Task 1: Host
- [ ] 添加固定映射行为测试并确认旧实现失败。
- [ ] 修改 tools/opt_config.py 为固定映射，去掉可写绑定。
- [ ] 修改 tools/daplink_manager.py 为只读算法显示，移除保存入口并更新提示。
- [ ] 验证切换/清空/旧绑定/导入参数及既有配额OPT校验流程。

### Task 2: Firmware
- [ ] 备份并核对 run.c、run.h、字库的实际编码。
- [ ] 修改 mode2/mode3 导航只允许固件选择和烧录。
- [ ] 更新所有算法显示为芯片信息并补中文字模。
- [ ] 编译检查按键路径及显示布局。

### Task 3: Delivery
- [ ] 适用回归测试和独立审查。
- [ ] 完整构建固件，打包bin和上位机，记录hash及实机验证边界。
