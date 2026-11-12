# PC 端独立审查

审查范围：`host-review.diff`、`test_host.py`、`task_plan.md`，以及主工程工具目录的 `opt_config.py`、`daplink_manager.py` 当前源码和 `host-original` 快照。本审查未修改生产代码。

## 结论

- **需求符合性：通过。** PC 端已实现指定的固定映射、只读显示和成功导入时提交设备算法。
- **代码质量：通过。** 未发现新增的实际缺陷或需要返工的需求遗漏。
- **可操作问题：无。**

## 需求核对

1. `opt_config.py:12–15` 固定定义 `MM32G0001A → MM32G001-OPT_ext.bin`、`MM32SPIN0230 → MM32SPIN0230-OPT_ext.bin`。`get_algorithm()` 只读取该映射；旧 `algorithm_bindings.json` 不再读取、不能覆盖关系，也不会被此次读取改写。空名和未知型号返回空名。
2. `daplink_manager.py:624–629` 使用 `QLabel` 显示算法文件名；原输入框、保存按钮及 `_bind_algorithm()` 均已移除。工具目录没有剩余的旧绑定读写或控件引用。`QLineEdit` 仍用于其他输入项，保留导入合理。
3. `_on_chip_changed()` 只更新当前型号、算法标签和 OPT 界面，不发送 USB 指令。`_on_import_program():1439` 按固定映射取得算法并传入 `ImportWorker`，不会把标签文字作为导入依据。
4. 新增的行为说明注释为中文；保留 `bindings_path` 形参兼容旧调用的用途已有中文说明。
5. 导入交易依旧先暂存算法并锁住导入，上传主程序及可选 OPT，重载配额，最后通过带 SUM32 的 `prepare_download()` 提交；异常时取消暂存。因此 PC 只切换芯片不会修改设备已导入程序的算法。

## 验证证据及边界

- 本次审查直接执行 AST 比较，确认 `DAPLinkDevice`、`ImportWorker`、`_set_import_state`、`_start_quota`、`_build_opt_bytes`、`_generate_opt_bin`、`_import_done` 与原始快照一致；导入锁、SUM32、配额和 OPT 的实现未因本改动更改。
- 新增三个测试覆盖旧绑定隔离、两款芯片及空选择的只读算法显示，以及真实导入 worker/驱动发送的算法暂存请求。显示文字被故意改为错误文件名后，导入仍采用固定映射。
- 参考主任务已执行的结果：新增测试 3/3 通过；两款芯片在 900×600、1000×650、1280×800 的 offscreen 布局检查通过。本审查按要求未重复相同测试。
- 原生 Windows GUI 字体及文字呈现仍由主任务检查；offscreen 截图缺少文字不能作为原生显示缺陷。物理 USB 设备提交/烧录结果不在本次 PC 源码审查范围内。
