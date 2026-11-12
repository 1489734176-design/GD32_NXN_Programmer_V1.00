# 次数统计独立审查

结论：限定范围内未发现需修复的 P1/P2 问题。未修改产品代码。

审查对象为 `burn_quota.c/.h`、`burn_quota_catalog.c` 与 `usbd_custombulk.c`，以 `portrait/original/` 下的原始备份为差异基线。阅读了 task-2-report、生产实现、真实 GUI ImportWorker 上传顺序、测试实现及现有结果。显示与 APP 状态机不重复审查。

## 核对结果

- **成功、失败与掉电语义**：`burn_quota_begin()` 只持久化有限次数的预约扣除；`burn_quota_finish(1)` 在确认当前认证双副本后单独递增 used。失败退款保留 used，不限次成功也写入 used。重启不从 total−remaining 推算成功数；旧数据 known=0 时继续保持历史未知。used 达到 UINT32_MAX 饱和，不回绕。
- **事务与固件隔离**：96 字节 BQT4 记录同时认证 total/used/remaining；外部镜像先同步、关闭、回读，内部日志最后提交。未完成双写沿用禁止烧录策略，不通过普通 begin/finish 修复。切换固件前把旧固件完整统计追加到受 HMAC 根保护的 48 字节账本记录，回读成功后才发布新根；未提交尾部不参与恢复。
- **同额度重新导入**：`set_quota(..., reimport=1)` 不走相同余额的短路，明确建立 total=导入值、used=0 的新统计；普通 SET 保留原批次历史。按已确认的产品规则，管理员调整 remaining 后不要求 total=used+remaining。
- **真实 GUI 上传顺序**：`daplink_manager.py:450–490` 确认为 stage→主 BIN→可选 OPT→stage→具名 SET→PREPARE。USB 在完整上传且成功关闭的第一个 BIN 上记录名称；第二次 stage 保留标记；具名 SET 按大小写无关的主文件名匹配并重置，成功后消费一次。GET 不触发重置；重复 SET、单独 stage、失败 CLOSE 不产生新的重置；失败 SET 保留标记以便重试。取消/重置或上传关闭失败使 stage 失效，下一次 stage 清除旧标记。
- **旧格式与认证边界**：BQT3/BQC1 保留余额但不制造 total/used；BQT1/BQT2 走已有保守迁移路径。`burn_quota_catalog.c:112` 新增的 legacy_crc 分支限定只接受 BQC1，阻止 CRC-only 旧根把 BQC2 统计升级为认证历史。常规 BQC2 加载仍须验证完整已提交前缀的 HMAC，字段 CRC 不能单独授权。

## 验证证据

- 本次独立执行 `python .planning/2026-10-29-screen-ui/portrait/quota/test_usb_import.py`，退出码 0，约 2.6 秒；两个 PASS 覆盖真实 BIN+OPT/重复 stage/GET/重复 SET/失败关闭、失败 SET 重试与不限次重新导入。
- 核对现有 `stats-results.json`、`stats-fault-results.json`、`catalog-regression-results.json`：均 passed=true，记录的 quota/catalog/auth/mirror 源码哈希与当前文件一致。统计故障集包含 6 类事务 × 68 个掉电、写错误、FatFs 错误及短 I/O 注入案例，共 408 个；另有双副本统计字段被同步篡改仍拒绝授权的用例。
- `regression-results.json` 的 catalog 哈希属于第 112 行限制加入之前的版本，因此仅作为此前广泛回归证据，不将它表述为最终 catalog 源码的完整重跑。最终版本已由上述统计及账本结果覆盖。
- 未重跑长故障测试，未编译主 Keil 工程。真机断电、Flash 耐久及 USB 中断时序未在本审查中验证。

## 审查时源码 SHA-256

| 文件 | SHA-256 |
|---|---|
| burn_quota.c | `36dc21ebf0def23d032ef6c78d5a1ee2d000db2f797ecea81bd1d2bac4dbfe22` |
| burn_quota.h | `5c96fc9977eba1a33ff2b88a4545fe1c533f84693a8641a5229a036260db8a96` |
| burn_quota_catalog.c | `3e8ddd9d735c124e802ecc3bcb28d56264a8673928c9bd113fbcc58e437585c7` |
| usbd_custombulk.c | `302140df0e645111feb562dd12121a3506c342738aa26ea01f479aa6d7b985e2` |
