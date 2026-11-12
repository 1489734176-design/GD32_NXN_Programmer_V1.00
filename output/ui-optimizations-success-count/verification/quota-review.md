# 成功后扣次：独立审查

结论：**未发现 P1/P2 问题。** 当前额度模块实现符合本轮用户明确授权的“开始不扣、失败不扣、未确认成功前断电不扣，确认成功才扣”。不将旧预扣要求作为审查标准。

## 范围与版本

- 对照 `original/APP/burn_quota.c/.h` 审查生产 `USER/APP/burn_quota.c/.h`，并追读未修改的 mirror、catalog、auth、主任务串行调用约束。
- `burn_quota.c` SHA256：`85e493f38ce69099e3325395a0b44f3352f70ee6d1457de97925441c3bc6a63f`
- `burn_quota.h` SHA256：`d701317a652fa458536293a5393f7f0368af854aa7109677a2ac9512f9f71776`
- UI/BSP 变更不在本次独立额度审查范围。未运行主 Keil 工程，未改产品源码。

## 核对结果

- `begin()` 先检查活动会话，再验证选中状态、故障状态及真实内外存储/HMAC/账本，最后检查零额度。成功只设置 RAM 标记；其读取链路不写媒体。
- 限次、不限次均持有会话标记；活动期间同名/异名/空名选择、SET、reimport、重复 begin 均返回 BUSY。
- `finish(0)` 清除会话后直接返回，不读媒体、不写媒体、不退款。启动初始化丢弃会话，重启后的 finish 不会虚构成功。
- `finish(1)` 重新认证存储，并使用一次既有 `append()` 更新 remaining 与 used。会话标记先消费，重复 finish 或未建立会话的 finish 不会再次提交。
- 限次旧版本未知历史仍减余额；未知历史保持未知。known used 饱和时不回绕，限次仍扣；不限次已饱和时无新状态需要保存。
- 原双副本顺序保持不变：先外部镜像，再内部提交字及回读验证。成功保存被打断后只能观察到完整旧值、完整新值或故障锁定；普通 begin/finish 不修复媒体，也不将混合值当作有效额度。
- 清除会话早于提交不会在现有串行主任务模型产生并发重入：USB额度请求只排队，实际服务与烧录流程均在主任务执行。

## 验证证据

独立执行：

`D:/Program Files/Python314/python.exe .planning/2026-10-29-ui-optimizations/quota/review_checks.py`

退出码 0，8 组检查通过；使用 ARMCC 编译真实 quota/catalog/mirror/auth Thumb 代码，仅物理媒体/FatFs 调用由既有模拟器替代。输出在 `quota/review-results.json`；编译副本的 c/h 哈希与上述生产文件一致。

覆盖 RAM-only begin、失败/重启媒体不变、BUSY 保护、成功单次事务、重复 finish、零额度、最后一次 1→0、BQT1/BQT2 迁移、已有 BQT3 未知历史、used 饱和及成功前篡改拒绝。

另已审阅实现者 `test_commit_faults.py` 及 `commit-fault-results.json`，结果为通过且生产源码哈希一致：有限、不限、页切换成功提交的每个写操作断电/写错、每次 FatFs 错误、短传输和回读损坏，以及 begin 验证失败。测试重启后检查 remaining/used 必须为完整旧/新元组，且只有双副本一致才能放行；没有放宽为任意旧余额。长故障枚举未重复运行。

已审阅 `test_reimport_faults.py` 与 `reimport-results.json`，同总数和等于当前余额的重新导入故障测试均通过。实现者完成后又核对 `task-2-report.md`、`regression-results.json` 与 `catalog-results.json`：BQT3、6 组历史、13 组认证、10 组账本及保留的 SET/切换/迁移故障结果均通过；结果源码哈希一致。审核过历史测试包装器，变更局限于已被用户覆盖的预扣/退款断言，并用新的成功提交故障测试替换对应枚举。

上述实现者结果合计 724 个故障注入案例，独立审查未重复执行这些长枚举。主工程整合验证由 root 执行；本结论不代替真实硬件断电验证。

维护性备注（非 P1/P2）：审查时 run.c 的 mode4 顶部仍写“预扣/正常失败退回”，已通知 root/UI 实现者按新语义更新。
