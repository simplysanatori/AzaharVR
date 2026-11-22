"""AzaharFrame kiuusa (see codemap.py): every EUR address the hooks use -> USA Rev 1, each checked.
Code: the code map, then the word: equal (non-branch) or the same branch to the mapped target.
Rodata/data: the literal pairs read at mapped code."""
import sys, re, pickle, collections
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from codemap import load, BASE, isbr, brtarget, mapper
E = load(sys.argv[1]); U = load(sys.argv[2]); segs = pickle.load(open(sys.argv[3], "rb"))
rows = pickle.load(open(sys.argv[4], "rb")); SRC = sys.argv[5]
m = mapper(segs)
lit = {e: d for e, d, n, t in rows}
def W(T, a): return T[(a-BASE)//4]
def code(a, orig=None):
    ua, how = m(a)
    e = W(E, a) if orig is None else orig
    u = W(U, ua)
    if isbr(e):
        t = brtarget(a, e); ut, _ = m(t)
        ok = isbr(u) and (u >> 24) == (e >> 24) and brtarget(ua, u) == ut
    else:
        ok = (u == e)
    return ua, u, ok, how
hk = open(SRC+"/input/kiu_hooks.h").read(); tn = open(SRC+"/input/kiu_turn.h").read()
consts = {mm[1]: int(mm[2], 16) for mm in re.finditer(r"(k\w+)\s*=\s*(0x[0-9A-Fa-f]+)u?;", hk+tn)}
bad = 0
print("== patches and context (addr, EUR word -> USA addr, USA word)")
for mm in re.finditer(r"\{Group::(\w+),\s*(0x[0-9A-Fa-f]+|k\w+),\s*(0x[0-9A-Fa-f]{8})", hk):
    a = int(mm[2], 16) if mm[2].startswith("0x") else consts[mm[2]]
    ua, u, ok, how = code(a, int(mm[3], 16))
    if not ok: bad += 1
    print(f"{mm[1]:15s} {a:#08x} {int(mm[3],16):08X} -> {ua:#08x} {u:08X} {'ok' if ok else 'DIFFERS'} {how}  d={ua-a:+#x}")
print("== code check")
for mm in re.finditer(r"\{(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]{8})\}", tn):
    a = int(mm[1], 16); ua, u, ok, how = code(a, int(mm[2], 16)); bad += not ok
    print(f"check {a:#08x} {int(mm[2],16):08X} -> {ua:#08x} {u:08X} {'ok' if ok else 'DIFFERS'} {how}")
print("== branch targets and stub literals (code addresses)")
for name, a in [("scene cull inside", 0x77DC4C), ("object cull visible", 0x773280), ("bullet view fn", 0x4EC640),
                ("sink continue", 0x6340A0), ("spawn", 0x485638), ("path restart", 0x3E8CDC), ("path vanish", 0x3E8D00),
                ("shadow continue", 0x57B058), ("fov continue", 0x45E050), ("follow input fn", 0x7CA280),
                ("reticle ease none", 0x6D216C), ("reticle box whole", 0x6D1704), ("shadow b target", 0x5B26AC),
                ("shadow bl target", 0x50533C), ("follow update", 0x7CAFC8), ("caller frame 0x503F8C", 0x503F8C),
                ("aim fn 0x7014C8", 0x7014C8), ("0x7019A0", 0x7019A0), ("0x4C14B0", 0x4C14B0),
                ("eased reticle fn 0x6D213C", 0x6D213C), ("aim box fn 0x6D163C", 0x6D163C),
                ("vtable fn land 0x76ED88", 0x76ED88), ("vtable fn air 0x76E9EC", 0x76E9EC)]:
    ua, how = m(a); print(f"{name:26s} {a:#08x} -> {ua:#08x} ({how}, d={ua-a:+#x}) EUR {W(E,a):08X} USA {W(U,ua):08X} {'same' if W(E,a)==W(U,ua) else 'DIFF'}")
print("== rodata / data / bss (nearest literal at or below, its shift)")
keys = sorted(lit)
import bisect
for name, a in [("aim vtable land", 0xCF1C34), ("aim vtable air", 0xCF1BE0), ("cam vtable", 0xCEEA74),
                ("owner vtable", 0xCEAC40), ("actor vtable", 0xCA046C), ("model vtable", 0xD1E990),
                ("model child vtable", 0xD1E880), ("range table vtable", 0xCFEDD8),
                ("playing", 0xE17514), ("aim root", 0xE14504), ("cam global", 0xD3A18C), ("scene node", 0xE14B88),
                ("round shadows", 0xD49490), ("range root slot", 0xE704C0), ("records ptr (kiu_scan)", 0xD636B4)]:
    exact = lit.get(a)
    j = bisect.bisect_right(keys, a) - 1; lo = keys[j]; hi = keys[j+1] if j+1 < len(keys) else None
    print(f"{name:24s} {a:#08x}: exact {('%+#x' % exact) if exact is not None else '-'}; below {lo:#x} {lit[lo]:+#x}; above {hi:#x} {lit[hi]:+#x}")
print("== cave (USA)")
start = 0xC950F0
nz = [hex(BASE+i*4) for i in range((start-BASE)//4, (0xC96000-BASE)//4) if U[i]]
print("USA words nonzero in 0xC950F0-0xC96000:", len(nz), nz[:5])
refs = [hex(BASE+i*4) for i, w in enumerate(U) if 0xC950F0 <= w < 0xC96000]
print("USA text words whose value lies in the cave:", len(refs), refs[:10])
print("mismatches:", bad)
