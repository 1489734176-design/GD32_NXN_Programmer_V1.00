#!/usr/bin/env python3
import sys, os

def detect_le(c):
    return b"\r\n" if c.count(b"\r\n") >= c.count(b"\n") - c.count(b"\r\n") else b"\n"

def norm_le(d, le):
    d = d.replace(b"\r\n", b"\n")
    return d.replace(b"\n", b"\r\n") if le == b"\r\n" else d

def gbk_edit(fp, old, new):
    ob = old.encode("gbk")
    nb = new.encode("gbk")
    with open(fp, "rb") as f:
        c = f.read()
    le = detect_le(c)
    ob = norm_le(ob, le)
    nb = norm_le(nb, le)
    n = c.count(ob)
    if n == 0:
        print(f"NOT FOUND\n  LE={le!r}\n  ob={ob[:120]!r}")
        return False
    if n > 1:
        print(f"WARNING: {n} matches, replacing all")
    c = c.replace(ob, nb)
    with open(fp, "wb") as f:
        f.write(c)
    print(f"OK: {n} replaced, LE={le!r}, GBK")
    return True

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: gbk_edit.py <file> <old> <new>")
        sys.exit(1)
    sys.exit(0 if gbk_edit(sys.argv[1], sys.argv[2], sys.argv[3]) else 1)
