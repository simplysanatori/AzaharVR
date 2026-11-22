"""AzaharFrame kiuusa (see codemap.py): dump a running game's text segment through the emulator's RPC
server (the RPC server on). Its output is the game's code: keep it local, never commit it.
dumptext.py TITLE_HEX OUT [START END]"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../dist/scripting"))
from AzaharRPC import AzaharRPC
title = int(sys.argv[1], 16); out = sys.argv[2]
lo = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x100000
hi = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0xC96000
c = AzaharRPC(timeout=2.0, retries=3)
end = time.time() + 90
while True:
    try:
        procs = c.process_list()
        pids = [p for p, (t, _n) in procs.items() if t == title]
        if pids:
            c.set_process(pids[0]); break
    except Exception:
        pass
    if time.time() > end: sys.exit("no process")
    time.sleep(0.5)
time.sleep(12)  # past the boot, code loaded
buf = bytearray(); a = lo; step = 0x4000
while a < hi:
    n = min(step, hi - a); d = None
    for _ in range(6):
        try:
            d = c.read_memory(a, n)
            if d is not None and len(d) == n: break
        except Exception: pass
        time.sleep(0.05)
    if d is None or len(d) != n:
        d = b"\0" * n  # unmapped
    buf += d; a += n
open(out, "wb").write(buf)
print("dumped", hex(lo), hex(hi), len(buf), "nonzero words", sum(1 for i in range(0, len(buf), 4) if buf[i:i+4] != b"\0\0\0\0"))
