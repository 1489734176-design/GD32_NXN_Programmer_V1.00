# 最终集成边界审查

**规格结论：通过。质量结论：限定的跨组件边界通过，未发现新的 P1/P2。**

依据 `design.md`、`application.diff`、Task 1/2 handoff、现有 display/app/quota reviews，并局部核对当前 `run.c`、BSP 头文件、Keil 宏和 USB 次数服务的接口边界。本轮未修改产品代码，未运行主 Keil 构建或重跑已通过的大测试；显示内部、配额存储内部及历史 SWD 实现不重复审计。

## 单页入口与错误收尾

- `run.c:324` 是该应用唯一的 `UI_EnterPage`，固定 page 10；它只在主页面首次绘制时执行。当前 `run.c` 没有 `OLED_Clear`、`UI_BootLogo` 或旧 `UI_Progress` 调用。
- `start_page()` 恢复选择后直接绘制同一页；文件系统挂载失败只改变第 12 行状态。USB PREPARE 更新选择后返回 mode 2。原 mode 3 只把焦点送回固件行并调用 `draw_main()`；内部 mode 4 不创建新页面。
- 手动轮选、空目录、坏固件/OPT、保存失败、零次数、次数记录故障、连接/算法/烧录/校验错误都使用同一组主页面字段和状态条。结果由 `finish_burn_ui()` 返回 mode 2；既有 app-review 中的 OPT 降级及坏候选卡住问题已关闭，本次不重新展开其内部审查。
- 自动模式仍走 RESULT→HOLD→确认取出→WAIT，取出后才重置反馈；手动重新开始清 `g_burn_done`。最终 10 组回归已包含实际自动状态机的等待取出/重新插入，以及手动重复开始和连接失败退款。

## 进度、颜色与成功提交

- `draw_main()`、`show_download_progress()`、`show_download_status()` 统一调用 `UI_ProgressStatus(80, 228, 152, ...)`，与 Task 1 的固定 16 像素高接口、枚举顺序及覆盖区域一致。
- 新任务从 RUNNING/0 开始。连接、加载、擦除等阶段只提高进度；主固件已完成字节百分比映射到 15–85，校验/OPT 阶段到 85/90/95，避免仅写完主固件就显示最终 100。终态的满红/满绿由 BSP 的 FAILED/SUCCESS 契约实现。
- `run.c:365` 是应用唯一设置 `UI_PROGRESS_SUCCESS` 的位置。正常完成路径先在 `run.c:1247` 调用 `burn_quota_finish(g_burn_ok)`；提交返回非 OK 时先清 `g_burn_ok` 并保存“次数记录异常”，然后才调用 `finish_burn_ui()`。早退路径已先把 `g_burn_ok` 清零。因此绿色/100 只在目标流程成功且配额提交返回成功之后显示。中途的“校验通过/烧录成功”文字不会自行设置绿色。

## 次数显示与当前固件绑定

| 条件 | 本次烧录 | 已用次数 | 剩余次数 |
|---|---|---|---|
| dirty 或没有当前固件 | `--` | `--` | `--` |
| 当前配额故障 | `--` | `--` | 记录异常 |
| 历史未知、有限额 | `--` | `--` | 实际保留余量 |
| 历史未知、不限额 | `--` | `--` | 无限 |
| 历史已知、有限额 | `burn_quota_total()` | `burn_quota_used()` | `burn_quota_remaining()` |
| 历史已知、不限额 | 无限 | `burn_quota_used()` | 无限 |

`burn_quota_stats_known()` 是历史有效性门禁，显示层没有用 total−remaining 推导 used。未知旧记录优先保留“历史未知”，与迁移契约一致；不限额已知记录继续显示实际成功数。`%08lu` 是最小宽度，uint32 已用数最多 10 位，16 字节局部缓冲及 152 像素字段可容纳。

主循环的 USB 文件/次数/PREPARE 服务后，`save_program_selection()` 在 USB 空闲时先重新 `burn_quota_select(app_data.bin_name)`，再进入绘页；`mode4()` 在目标上电前再次绑定当前固件。已检查 staged 导入入口会先使旧选择失效，busy 阶段不把临时查询固件的统计作为一个仍有效的旧选择显示。dirty 候选在下轮保存、绑定完成前均显示未知；坏候选同时阻止保存与启动。

## 方向隔离及最终证据

- 应用工程未覆盖 `LCD_DIRECTION`，共享头文件默认方向 0，即 240×320；Bootloader 工程显式定义 `LCD_BAREMETAL`，同一头文件默认方向 1，即 320×240。显式方向覆盖仍保留。未把应用布局修改传播成 Bootloader 竖屏要求。
- 已读最终 `offline-build.log`：0 Error(s)、0 Warning(s)。本轮只读取日志，没有触发编译。
- `screen-verification.json` 为 10 项通过、失败数组为空；`power-restore-verification.json` 为 13 项当前完整 Keil Thumb 镜像回归。两者 SHA-256 与独立计算的 `portrait/offline.bin` 一致：`443afb261cdf303659c21b3fd7faa5767b903d758ed7c216816a76acaa6d2a0c`。这更新了旧 app-review 复核时记录的较早镜像哈希。
- 两份最终记录均标明 `hardware_tested=false`。本审查不声称完成实机方向、显示观感或硬件断电验证。

P1：无。P2：无。
