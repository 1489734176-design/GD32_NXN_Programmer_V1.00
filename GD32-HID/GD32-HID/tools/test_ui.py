#!/usr/bin/env python3
"""Check OLED pixels and compile C UI tests; --simulate additionally requires an FVP license."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import winreg

from PIL import Image, ImageFont
from gen_glyph import FONT, TARGET, fmt_array, pack_like_oled, render_char

ROOT = Path(__file__).resolve().parents[1]


def function(text, name):
    start = re.search(r"^(?:static )?(?:void|key_num) " + name + r"\([^;]*?\)\s*\{", text, re.M)
    if not start:
        raise ValueError(name)
    depth = 1
    end = start.end()
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start.start():end]


HARNESS = r'''
uint8_t OLED_GRAM[8][128], D[64];
struct { uint8_t Refresh_flag; } oled;
app_TypeDef app_data;
key_TypeDef key_val;
int usb_hid;
unsigned input_key, file_count, file_position;
struct { char fname[256]; } FileInfo;
int DirInfo;
#define FR_OK 0
#define RCU_USBD 0
#define read_key_left (input_key == key_left)
#define read_key_mid (input_key == key_mid)
#define read_key_right (input_key == key_right)
void usbd_connect(void *p) {}
void rcu_periph_clock_enable(int clock) {}
void show_top(uint8_t status) {}
void OLED_Refresh(void) {}
unsigned f_opendir(void *p, const char *path) { file_position = 0; return FR_OK; }
unsigned f_readdir(void *p, void *info) {
    if(file_position == file_count) FileInfo.fname[0] = 0;
    else sprintf(FileInfo.fname, "%03u.bin", file_position++);
    return FR_OK;
}
void f_closedir(void *p) {}
#define CHECK(test) do { if(!(test)) { printf("FAIL line %u: %s\n", __LINE__, #test); exit(1); } } while(0)
void raw_DrawPoint(uint8_t x, uint8_t y, uint8_t t);
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t t) {
    CHECK(x < 64 && y < 128);
    raw_DrawPoint(x, y, t);
}
'''

TESTS = r'''
static void quiet(unsigned scans) {
    input_key = key_null;
    while(scans--) CHECK(key_scan() == key_null);
}
static void press(unsigned key, unsigned scans) {
    input_key = key;
    while(scans--) CHECK(key_scan() == key_null);
}
static key_num hold_capture(unsigned key, unsigned scans) {
    key_num seen = key_null, event;
    input_key = key;
    while(scans--) {
        event = key_scan();
        if(event != key_null) {
            CHECK(seen == key_null);  /* at most one event while held */
            seen = event;
        }
    }
    return seen;
}
static void test_keys(void) {
    unsigned key, n, count, last;
    for(key = key_left; key <= key_right; key += 2) {
        press(key, 5);
        quiet(1);
        CHECK(key_scan() == key);
        quiet(20);
        input_key = key;
        count = 0; last = 0;
        for(n = 0; n < 1000; n++) {
            key_num event = key_scan();
            if(event != key_null) {
                CHECK(event == key);
                if(count) CHECK(n - last == key_repeat_th);
                else CHECK(n == key_long_th + 1);
                last = n; count++;
            }
        }
        CHECK(count > 100);
        quiet(40);
    }
    press(key_left, 1); quiet(20);
    press(key_mid, 5); quiet(2); quiet(key_dbl_th - 1);
    CHECK(key_scan() == key_mid); quiet(20);
    press(key_mid, 5); quiet(5); press(key_mid, 5); quiet(1);
    CHECK(key_scan() == key_d_mid); quiet(20);
    /* long press fires once while held, nothing on release or continued hold */
    CHECK(hold_capture(key_mid, key_mid_long_th + 10) == key_l_mid); quiet(20);
    /* short press then long press: pending single click is cancelled */
    press(key_mid, 5); quiet(5);
    CHECK(hold_capture(key_mid, key_mid_long_th + 10) == key_l_mid); quiet(20);
    press(key_mid, 5); quiet(3); press(key_right, 5); quiet(1);
    CHECK(key_scan() == key_right); quiet(40);
    puts("PASS key short/hold/repeat/release/debounce/middle gestures");
}
static void dump_frame(const char *name) {
    unsigned p, c;
    printf("FRAME %s ", name);
    for(p = 0; p < 8; p++) for(c = 0; c < 128; c++) printf("%02X", OLED_GRAM[p][c]);
    puts("");
}
static void test_oled(void) {
    uint8_t saved[128];
    unsigned p, c, row;
    OLED_Clear(); OLED_ShowChinese(48, 0, 5, 4, 1);
    for(p = 0; p < 8; p++) for(c = 0; c < 128; c++) {
        uint8_t expected = (p >= 6 && c < 64) ? string_5[c / 16][(p - 6) * 16 + c % 16] : 0;
        CHECK(OLED_GRAM[p][c] == expected);
    }
    dump_frame("glyphs");
    for(row = 0; row <= 48; row++) {
        OLED_Clear(); OLED_ShowChinese(row, 16, 3, 4, 1);
        for(p = 0; p < 64; p++) for(c = 16; c < 80; c++) {
            unsigned bit = (OLED_GRAM[p / 8][c] >> (p % 8)) & 1;
            unsigned expected = 0;
            if(p >= row && p < row + 16)
                expected = (string_3[(c - 16) / 16][(p - row) / 8 * 16 + (c - 16) % 16] >> ((p - row) % 8)) & 1;
            CHECK(bit == expected);
        }
    }
    OLED_Clear(); show_download_progress(73);
    memcpy(saved, OLED_GRAM[3], 128);
    show_download_status("OPT verify file error");
    show_download_status("OPT OK");
    CHECK(memcmp(saved, OLED_GRAM[3], 128) == 0);
    for(c = 36; c < 128; c++) CHECK(OLED_GRAM[5][c] == 0);
    for(p = 0; p < 8; p++) if(p != 3 && p != 5)
        for(c = 0; c < 128; c++) CHECK(OLED_GRAM[p][c] == 0);
    show_download_status("Programming..."); dump_frame("progress");
    for(p = 0; p <= 100; p++) show_download_progress(p);
    show_download_progress(0);
    for(c = 22; c < 82; c++) CHECK(OLED_GRAM[3][c] == 0);
    puts("PASS OLED glyph placement, bounds, progress and status clearing");
}
static void event(unsigned key, void (*page)(void)) {
    key_val.key = (key_num)key; key_val.mesg = 1; page();
    CHECK(key_val.mesg == 0);
}
static void test_pages(void) {
    unsigned sizes[] = {0, 1, 9, 130, 300}, s, i;
    char expected[32];
    app_data.mode = 2; app_data.head = 0;
    strcpy((char *)app_data.firmware_name, "algorithm-with-a-long-name.bin");
    strcpy((char *)app_data.bin_name, "firmware-with-a-long-name.bin");
    oled.Refresh_flag = 1; mode2(); dump_frame("selection");
    event(key_left, mode2); CHECK(app_data.head == 2);
    event(key_right, mode2); CHECK(app_data.head == 0);
    for(i = 0; i < 100; i++) event(key_right, mode2);
    CHECK(app_data.head == 1);
    event(key_right, mode2); event(key_mid, mode2); CHECK(app_data.mode == 4);
    app_data.mode = 2;
    event(key_d_mid, mode2); CHECK(app_data.mode == 0);
    for(s = 0; s < sizeof(sizes)/sizeof(sizes[0]); s++) {
        event(key_d_mid, mode3);
        file_count = sizes[s]; app_data.head = 0; app_data.mode = 3;
        oled.Refresh_flag = 1; mode3();
        if(file_count) CHECK(strcmp((char *)app_data.firmware_name, "000.bin") == 0);
        event(key_left, mode3);
        if(file_count) {
            sprintf(expected, "%03u.bin", file_count - 1);
            CHECK(strcmp((char *)app_data.firmware_name, expected) == 0);
        }
        event(key_right, mode3);
        for(i = 0; i < file_count + 2; i++) {
            if(file_count) {
                sprintf(expected, "%03u.bin", i % file_count);
                CHECK(strcmp((char *)app_data.firmware_name, expected) == 0);
            }
            event(key_right, mode3);
        }
        event(key_l_mid, mode3); CHECK(app_data.mode == 0);
    }
    puts("PASS selector wrap and file lists: 0/1/9/130/300 entries");
}
int main(void) {
    test_keys(); test_oled(); test_pages();
    puts("UI TESTS PASSED");
    return 0;
}
'''

STARTUP = '''
    AREA RESET, DATA, READONLY
    EXPORT __Vectors
__Vectors
    DCD 0x20010000
    DCD Reset_Handler
    AREA |.text|, CODE, READONLY
    THUMB
    EXPORT Reset_Handler
    IMPORT __main
Reset_Handler PROC
    LDR R0, =__main
    BX R0
    ENDP
    IMPORT __use_two_region_memory
    EXPORT __user_initial_stackheap
__user_initial_stackheap PROC
    LDR R0, =0x20020000
    LDR R1, =0x20010000
    LDR R2, =0x20030000
    LDR R3, =0x20008000
    BX LR
    ENDP
    END
'''


def model_frames(font_header):
    arrays = {}
    for name, body in re.findall(r"const unsigned char (\w+)\[[^;=]*=\s*\{(.*?)\};", font_header, re.S):
        arrays[name] = [[int(value, 16) for value in re.findall(r"0[xX]([0-9a-fA-F]+)", row)]
                        for row in re.findall(r"\{([^}]+)\}", body)]
    frame = bytearray(1024)

    def point(row, column, value):
        assert 0 <= row < 64 and 0 <= column < 128
        pos, mask = row // 8 * 128 + column, 1 << (row % 8)
        frame[pos] = (frame[pos] | mask) if value else (frame[pos] & ~mask)

    def chinese(row, column, index):
        for glyph in arrays[f"string_{index}"]:
            x1 = x2 = row
            y1 = column
            for j, value in enumerate(glyph):
                for bit in range(8):
                    point(x1, y1, value & (1 << bit))
                    x1 += 1
                if j == 15:
                    x2 += 8
                    y1 = column
                else:
                    y1 += 1
                x1 = x2
            column += 16

    def string(row, column, text, size=8, inverse=False):
        width = 6 if size == 8 else 8
        table = arrays["asc2_0806" if size == 8 else "asc2_1608"]
        for ch in text:
            for j, value in enumerate(table[ord(ch) - 32]):
                for bit in range(8):
                    point(row + j // width * 8 + bit, column + j % width,
                          bool(value & (1 << bit)) != inverse)
            column += width

    chinese(48, 0, 5)
    frames = {"glyphs": bytes(frame)}
    frame[:] = bytes(1024)
    chinese(0, 0, 3)
    string(16, 0, "firmware.bin")
    chinese(24, 0, 4)
    string(40, 0, "algorithm.bin")
    chinese(48, 0, 5)
    string(48, 100, "<-", 16)
    frames["selection"] = bytes(frame)
    frame[:] = bytes(1024)
    string(24, 16, "[          ] 73%")
    string(24, 22, " " * 7, inverse=True)
    string(40, 0, "Programming...")
    frames["progress"] = bytes(frame)
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulate", action="store_true", help="execute compiled C tests using a licensed FVP")
    parser.add_argument("--keil-root", type=Path)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    if args.keil_root:
        keil = args.keil_root
    else:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Keil\Products\MDK") as key:
            keil = Path(winreg.QueryValueEx(key, "Path")[0]).parent
    compiler = keil / "ARM/ARMCC/Bin"
    simulator = keil / "ARM/avh-fvp/bin/models/FVP_MPS2_Cortex-M3.exe"
    run = (ROOT / "USER/APP/run.c").read_text(encoding="utf-8")
    oled = (ROOT / "USER/BSP/bsp_oled.c").read_text(encoding="utf-8")
    key = (ROOT / "USER/BSP/bsp_key.c").read_text(encoding="utf-8")
    header = (ROOT / "USER/BSP/bsp_key.h").read_text(encoding="utf-8")
    header = re.sub(r"^#include.*$|^#define read_key_.*$", "", header, flags=re.M)
    app = (ROOT / "USER/APP/run.h").read_text(encoding="utf-8")
    app = re.search(r"typedef struct.*?}app_TypeDef;", app, re.S).group()
    font_header = (ROOT / "USER/BSP/font.h").read_text(encoding="utf-8")
    assert fmt_array() in font_header, "font.h does not match generator"
    grid = [[0] * 16 for _ in range(16)]
    for row, col in ((0, 2), (7, 3), (8, 4), (15, 5)):
        grid[row][col] = 1
    expected = [0] * 32
    expected[2], expected[3], expected[20], expected[21] = 1, 128, 1, 128
    assert pack_like_oled(grid) == expected
    flow = function(run, "mode4")
    assert "OLED_ShowChinese(" not in flow and "OLED_ShowString(" not in flow
    for status in re.findall(r'show_download_status\("([^"\n]*)"\)', run):
        assert len(status) <= 21, status
    source = '#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'
    source += "\n".join((header, app, font_header, HARNESS, function(key, "key_scan"))) + "\n"
    source += function(oled, "OLED_DrawPoint").replace("void OLED_DrawPoint(", "void raw_DrawPoint(")
    for name in ("OLED_Clear", "OLED_ShowChar", "OLED_ShowString", "OLED_ShowChinese"):
        source += "\n" + function(oled, name)
    for name in ("show_download_progress", "show_download_status", "mode2", "mode3"):
        source += "\n" + function(run, name)
    source += TESTS
    with tempfile.TemporaryDirectory(prefix="daplink-ui-test-") as tmp:
        temp = Path(tmp)
        (temp / "test.c").write_text(source, encoding="utf-8")
        (temp / "startup.s").write_text(STARTUP, encoding="ascii")
        (temp / "test.sct").write_text("LR_ROM 0x0 { ER_ROM 0x0 { startup.o (RESET, +First) *(+RO) } RW_RAM 0x20000000 { *(+RW,+ZI) } }", encoding="ascii")
        commands = [
            [compiler / "armcc.exe", "--cpu", "Cortex-M3", "--c99", "-O0", "-c", "test.c", "-o", "test.o"],
            [compiler / "armasm.exe", "--cpu", "Cortex-M3", "startup.s", "-o", "startup.o"],
            [compiler / "armlink.exe", "--scatter", "test.sct", "--entry", "Reset_Handler", "startup.o", "test.o", "-o", "test.axf"],
            [simulator, "-q", "-a", "test.axf", "--timelimit", "30", "-C", "fvp_mps2.mps2_visualisation.disable-visualisation=1",
             "-C", "fvp_mps2.telnetterminal0.start_telnet=0", "-C", "fvp_mps2.telnetterminal1.start_telnet=0", "-C", "fvp_mps2.telnetterminal2.start_telnet=0"],
        ]
        for command in commands if args.simulate else commands[:3]:
            result = subprocess.run([str(x) for x in command], cwd=temp, capture_output=True, text=True, errors="replace", timeout=60)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
        output = result.stdout + result.stderr
    print("PASS C regression harness compilation")
    if args.simulate:
        assert "UI TESTS PASSED" in output, output
        frames = {name: bytes.fromhex(data) for name, data in re.findall(r"FRAME (\w+) ([0-9A-F]{2048})", output)}
    else:
        print("SKIP C test execution (use --simulate with a licensed FVP)")
        frames = model_frames(font_header)
    font = ImageFont.truetype(str(FONT), 16)
    glyphs = frames["glyphs"]
    for index, ch in enumerate(TARGET):
        reference = render_char(ch, font)
        for row in range(16):
            for col in range(16):
                byte = glyphs[(48 + row) // 8 * 128 + index * 16 + col]
                assert (byte >> (row % 8)) & 1 == reference[row][col]
    if args.preview:
        canvas = Image.new("RGB", (128 * 4, 64 * 4 * 3 + 32), "#242424")
        for index, name in enumerate(("glyphs", "selection", "progress")):
            image = Image.new("RGB", (128, 64), "black")
            for row in range(64):
                for col in range(128):
                    if frames[name][row // 8 * 128 + col] & (1 << (row % 8)):
                        image.putpixel((col, row), (255, 255, 255))
            canvas.paste(image.resize((512, 256), Image.Resampling.NEAREST), (0, index * 272))
        canvas.save(args.preview)
    if args.simulate:
        print("\n".join(line for line in output.splitlines() if line.startswith("PASS") or line == "UI TESTS PASSED"))
    print("PASS generator bytes and framebuffer pixel mapping against upright reference")


if __name__ == "__main__":
    main()
