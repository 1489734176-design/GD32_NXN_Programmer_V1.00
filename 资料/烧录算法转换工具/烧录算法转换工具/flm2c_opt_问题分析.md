# flm2c_opt.py 无法给 MM32G0001 加密 — 问题分析

分析对象：`D:\mym\NXN\6.Programmer\DAPlink\烧录算法转换工具\烧录算法转换工具\flm2c_opt.py`
关联固件：`GD32-HID/GD32-HID/USER/APP/run.c`、`USER/DAPlink/SWD_opt.c`、`USER/ALGO/flash_blob.h`

---

## 一、结论（TL;DR）

**算法机器码本身没问题，`ProgramOpt` 是好的。问题出在"打包"环节：**

1. 你用的是 `MM32G0001_16.FLM` —— 这个 FLM **根本没有 `ProgramOpt` 符号**；
2. 脚本发现没有 `ProgramOpt` 时**不报错**，悄悄把 `[14] opt_program` 填成了 `ProgramPage`；
3. 固件照样去调这个"假 OPT 函数"，而 `ProgramPage` 不会解锁 OPT 区、也不会擦 OPT 块，
   写 `0x1FFFF800` 必然失败 → 芯片永远进不了读保护。

一句话：**换 FLM 就能加密**（`MM32G001-OPT.FLM`），脚本则必须把这个静默回退改成硬报错。

---

## 二、证据链

### 2.1 两个 FLM 的符号表对比

| FLM | PrgCode | ProgramOpt | 结果 |
|---|---|---|---|
| `MM32G0001_16.FLM` | 0x200 (512B) | **没有** | ❌ 无法加密 |
| `MM32G001-OPT.FLM` | 0x298 (664B) | **有，@0x213 (98B)** | ✅ 可以加密 |

两者 Flash 参数完全一样（16KB @0x08000000，页 1024B），可以互换，差别只在 OPT 支持。

### 2.2 生成的 BIN 里字段重复了

`MM32G0001_16_ext.bin`（2026-10-12 生成，616 字节）：

```
[ 4] program_page = 0x200001A7
...
[14] opt_program  = 0x200001A7   <-- 和 [4] 一模一样
```

对照 `flm2c_opt.py:181`：

```python
opt_prog_addr = algo_start + HEADER_SIZE + I.get('ProgramOpt', I.get('ProgramPage', 0))
```

`dict.get()` 的默认值把 `ProgramPage` 顶了上去。C 文件里其实打了警告注释
（`⚠ FLM不含ProgramOpt, 用ProgramPage填充`），但**命令行只输出一行提示就继续生成**，
很容易被忽略。

### 2.3 为什么 ProgramPage 写不进 OPT（反汇编依据）

对 `MM32G001-OPT.FLM` 里的 `ProgramOpt` 做反汇编（Thumb），它的流程是：

```
ProgramOpt(adr, sz, buf):
    FLASH_Unlock()              ; bl 0x20000020  (符号表 offset 0)
    FLASH_EraseOptionBlock()    ; bl 0x20000068  (offset 0x48) —— 擦整个 OPT 块
    if (error) return 1
    for (i = 0; i < sz; i += 2):
        CR |= 0x10              ; 打开 OPT 写使能 (OPTWRE)
        halfword = buf[i] | (buf[i+1] << 8)
        *(uint16*)(adr + i) = halfword
        FLASH_WaitBusy(); CR &= ~0x10
        if (FLASH_CheckError()) return 1
    return 0
```

再看 `ProgramPage`：它只置 `CR.PG(bit0)`，然后 `strh` 写目标地址，
**没有 `FLASH_OPTB_Unlock`、没有擦 OPT 块、没有 OPTWRE**。

而 MM32 的 OPT 区在未解锁 / 未擦除状态下是写不进去的，控制器会置错误标志。
所以固件 `run.c:654` 的 `if(... && opt_prog_addr)` 判断通过 → 真的去调了 →
`ProgramOpt` 位置跑的其实是 `ProgramPage` → 返回 1 → 屏幕报 `OPT program failed`。

---

## 三、脚本自身的问题清单

| # | 严重度 | 位置 | 问题 | 后果 |
|---|---|---|---|---|
| 1 | **致命** | `:181`, `:259-265` | 缺 `ProgramOpt` 时静默回退到 `ProgramPage` | 生成的"加密算法"根本加不了密 |
| 2 | **致命** | `:344-350` | `expect = algo_size // 4` 向下取整，非 4 字节对齐时 BIN 长度 ≠ `68 + algo_size` | 固件 `run.c:454` 的 `fw_size == algo_size + 68` 判定失败 → 整个 bin 被当 14 字段解析 → `is_ext_algo = 0` → **OPT 一步都不执行，且不报错**（就是"无法加密"又没提示的那种） |
| 3 | 严重 | `:184` | `static_base = algo_start + prgdata_addr`，**少加 32 字节 CMSIS 头** | PrgData 实际落在 `algo_start+32+offset`。本例 PrgData 是 4 个 `0x00`、代码里无绝对引用，暂时不发作；换个用了静态变量的 FLM 就会静默写飞 |
| 4 | 严重 | `:332`, `:336-351` | BIN 是"生成 C 文本 → 正则回读 `0x%08X` → 再打包" | 注释里出现 `0x12345678`、格式改成 `%X`、字段改成十进制，全都会错位。纯属自找麻烦 |
| 5 | 严重 | `:358-398` | `verify()` 与 `generate()` 用同一个（错误）公式，还只校验自己写过的字段 | 自证清白，等于没校验；也**没检查 `opt_program != program_page`** |
| 6 | 易错 | `:452-462` | 说明文件里的 OPT 布局是 **STM32F1 的**：`RDP / DATA0 / DATA1 / WRP0..` | MM32G0001 实际是 `RDP / USER / DATA0 / DATA1 / WRP0..WRP3`。照文档填 `_opt.bin` 会整体错位 2 字节，加密值写进 USER 里 |
| 7 | 缺失 | 全文 | 不生成、不校验 `_opt.bin`，也没写清命名规则和小端字节序 | 固件读不到 `0:/download/bin/<固件名>_opt.bin` 就报 `OPT file missing` |

---

## 四、固件侧另外三个坑（不在这份脚本里，但会让你以为"没加密"）

1. **文件名必须严格匹配**
   `run.c:599-612` 用 `strrchr(bin_name, '.')` 把扩展名换成 `_opt.bin`。
   固件叫 `app.bin` → 必须存在 `app_opt.bin`，少一个字符都不行。

2. **`_opt.bin` 必须是小端原始二进制**
   `ProgramOpt` 里是 `buf[i] | (buf[i+1] << 8)` 再 `strh`，所以：

   ```
   加密    -> 7F 80 FF FF FF FF FF FF FF FF FF FF FF FF FF FF
   不加密  -> A5 5A FF FF ...
   ```

   界面上显示 `0x807F`，写进文件是 **`7F 80`**。按大端存成 `80 7F` 就废了。

3. **加密值可能对不上，这条要单独核对**
   - 厂商 XW16Pro 工具里，勾"加密芯片"后 `RDP 目标值 = 0x807F`；
   - 你上位机 `chip_config.json` 里 `RPO` 的"读保护开启"是 `value: 0`，
     `daplink_manager.py:1341` 会算成 `(0xFF<<8)|0x00 = 0xFF00`。

   两个值不一样。如果 MM32G0001 只认 `0x807F`（低字节 `0x7F`，高字节取反 `0x80`），
   那 `0xFF00` 写下去就是不加密。**建议直接照厂商工具填 `0x807F`。**

4. **OPT 校验可能误报**
   `run.c:687` 写完 OPT 后立刻 `swd_read_memory` 回读 `0x1FFFF800` 比对。
   RDP 一旦生效，调试口可能读回全 0 或直接失败 → 报 `Verify failed`、`i=0`，
   最终停在失败页。**这时候芯片其实已经加密了**，别被提示误导。

---

## 五、怎么改（三步）

1. **换 FLM**：用 `MM32G001-OPT.FLM`（同目录就有，Flash 参数一致），不要用 `MM32G0001_16.FLM`。
2. **用 `flm2c_opt_v2.py`** 重新生成（已放在同目录，非破坏性，不动原脚本）：
   ```bash
   python flm2c_opt_v2.py MM32G001-OPT.FLM --emit-opt-bin
   ```
   产出 `MM32G001-OPT_ext.bin`（768B）+ `MM32G001-OPT_ext_opt.bin`（16B，`7F 80 FF...`）。
3. **推文件到烧录器**，两个都要：
   ```
   /download/firmware/MM32G001-OPT_ext.bin
   /download/bin/<你的固件名>_opt.bin
   ```
   注意第二个文件名必须跟固件名对齐。

---

## 六、v2 改了什么

- FLM 缺 `ProgramOpt` → **直接 `exit(2)` 报错**（要占位得显式加 `--allow-fallback`）
- 算法镜像按**字节流**拼装并补齐到 4 字节，`algo_size` 严格等于实际长度，
  `len(bin) == 68 + algo_size` 恒成立
- `static_base` 修正为 `algo_start + 32 + PrgData.offset`
  （本例：`0x20000298` → `0x200002B8`；机器码一字节未改，只有这个头字段变了）
- BIN 直接由字段打包，不再正则回读 C 文本
- `verify()` 真正独立校验：长度、`opt_program != program_page`、
  `opt_program` 是否落在 PrgCode 范围内、`opt_addr/opt_size` 是否一致
- `--emit-opt-bin` 生成小端 `_opt.bin` 模板（`--rdp` 可改，默认 `0x807F`）
- 说明文件里的 OPT 布局改成 MM32G0001 的 8 个半字，并写明字节序与文件命名

验证结果：

```
✓ 校验通过  static_base=0x200002B8  algo_size=700B
v1 768B / v2 768B，算法机器码完全一致，仅 [6] static_base 不同
```
