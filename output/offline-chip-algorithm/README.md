# 芯片自动配套算法更新（2026-10-28）

上位机选择芯片后，算法只读显示；烧录器显示“芯片型号：”与已导入程序使用的芯片，芯片行不可选择。新增逻辑已附中文注释。

| 芯片 | 固定算法 |
|---|---|
| MM32G0001A | MM32G001-OPT_ext.bin |
| MM32SPIN0230 | MM32SPIN0230-OPT_ext.bin |

## 使用

1. 打开 tools/run.bat。仓库内自动复用主工程的Python3.12环境；单独使用此文件夹时，在tools中创建Python3.12的.venv并安装 intelhex pyusb PyQt5 libusb-package。
2. 用上位机文件上传将 DAPlink.bin 放到设备 /update/DAPlink.bin，断电重启完成应用更新。DAPlink.hex 是地址从0x0800C000开始的应用镜像。
3. 将 algorithms/ 中两份文件保持原名上传到设备 /download/firmware/；上位机的“转到”输入该路径后即可上传。设备没有USB磁盘，使用上位机上传。
4. 选择芯片型号，设置OPT及烧录次数后导入目标程序；导入成功后，设备采用该配套算法并刷新芯片型号。只在上位机切换型号时，设备保留上次成功导入的配置。

设备手动菜单只在“烧录固件”和“开始烧录”之间导航。空算法显示“未选择”，历史未知算法显示“未知”；旧 algorithm_bindings.json 不再参与算法决定。

## 包内容与验证

DAPlink.bin/hex 为烧录器应用；algorithms/ 是在目标芯片RAM中运行的算法；tools/ 是更新的上位机。modified-source.zip 含本次修改的源文件，可在原工程对应路径覆盖。Bootloader和标准FLM工程无需更新。

本轮完整Keil编译零错误、零警告；核对BIN与HEX逐字节一致、应用不覆盖末尾次数保存页。上位机、实际Thumb界面、既有导入/OPT/SUM32/次数回归结果见verification/。尚未连接实物验证。
