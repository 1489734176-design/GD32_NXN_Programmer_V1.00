# 设备芯片只读界面

已按确认需求修改主GD32-HID工程的两个文件：

- `GD32-HID/GD32-HID/USER/APP/run.c`
- `GD32-HID/GD32-HID/USER/BSP/ui_chinese_font.h`

`font.h`和`bsp_oled.c`无需产品源码修改。现有ST7789渲染器从`ui_chinese_font.h`查汉字，因此新增字模写入该表。四个字模“型、号、：、知”由本机SimSun 16px生成，与现有字模相同的行式、MSB在左、每行两字节格式，按GBK编码有序插入。

## 行为

- 已提交算法`MM32G001-OPT_ext.bin`显示`MM32G0001A`；`MM32SPIN0230-OPT_ext.bin`显示`MM32SPIN0230`。
- 芯片标题显示“芯片型号：”，空算法显示“未选择”，其他/旧文件名显示“未知”；不做前缀猜测。
- 手动、自动待机/结果刷新以及初次进入烧录进度页全部使用相同反查函数。
- 手动焦点仅在`head=1`固件选择与`head=2`开始烧录之间循环，芯片信息无箭头。`mode2`修正旧算法焦点/异常值；自动模式仍固定开始烧录焦点。
- `mode3`固定浏览`0:/download/bin`，只改主固件名称，无法浏览或改写算法；长按中键返回固件焦点。
- USB成功提交的既有`apply_prepare_download`更新算法/固件并设置刷新标记，下一次刷新显示新芯片；未提交的stage不改变算法名，既有busy判断继续阻止自动探测及手动开始。
- 原有OPT、次数、SUM32和目标SWD烧录流程未修改。文件选择仍保留原来的OPT开关及校验失效行为。

## 验证

执行命令：

```powershell
& 'C:\Users\mym02\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' '.planning/2026-10-27-chip-algorithm/test_device_chip_ui.py' --preview
```

七组测试全部通过，夹具ARMCC编译/链接无警告：手动双项导航、旧/异常焦点与自动默认焦点、固件目录不修改算法、所有待机刷新准确显示型号、USB busy阻止烧录及成功提交后更新、烧录初页显示及安全返回、实际中文点阵渲染。

测试从当前`run.c`抽取完整的真实界面函数及`mode4`到目标SWD烧录边界前的导航/绘制代码，编译完整的当前`bsp_oled.c`；使用Unicorn执行Thumb机器码。边界替身仅用于FatFs目录、USB busy/已确认checksum、LCD硬件写入及本任务未更改的硬件检测。LCD_BlitMono的真实位图在Python侧恢复为逐像素屏幕。

测试先在修改前运行，七组均按功能缺失失败，记录在`device-chip-ui-red.json`。首次修改后五组通过；另两组发现测试装载器未恢复ARM链接器的ER_RW初始化数据，已按ELF执行节装载修正并重新跑全组。最终记录在`device-chip-ui-verification.json`。

四张像素预览包含两个芯片的手动/自动页面；已目视确认标题、型号、固件、次数和Checksum行无重叠或裁切。这些预览来自实际渲染逻辑，不包含被夹具替代的底部连接、电量/温度硬件采样。

## 文件与范围

- 原件在`device-original/`保留原路径结构。
- `device-chip-ui-manifest.json`记录修改前后SHA256；两文件原有UTF-8无BOM与CRLF均保持。
- `device-chip-ui.diff`为最终产品源码差异。
- 本子任务未编译完整Keil工程，未写入硬件。完整编译、既有全体回归及硬件范围说明由根任务完成。
- 主固件名可在设备上独立切换，配套算法保持上次PC提交值，这是已确认设计；设备不额外识别目标芯片型号或添加协议字段。
