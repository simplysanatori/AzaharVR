"""AzaharFrame kiuusa (see codemap.py): the USA Rev 1 rows of kiu_hooks.h's kContext and kiu_turn.h's kCodeCheck, written from
the EUR rows (comments kept) with each address and word read from the USA Rev 1 dump at the
mapped place (each checked as resolve.py does)."""
import sys, re, pickle
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from codemap import load, BASE, isbr, brtarget, mapper
E = load(sys.argv[1]); U = load(sys.argv[2]); segs = pickle.load(open(sys.argv[3], "rb")); SRC = sys.argv[4]
m = mapper(segs)
def usa(a, e):
    ua, how = m(a); assert how == "in", (hex(a), how)
    u = U[(ua-BASE)//4]
    if isbr(e):
        t = brtarget(a, e); ut, how2 = m(t); assert how2 == "in"
        assert isbr(u) and (u >> 24) == (e >> 24) and brtarget(ua, u) == ut, hex(a)
        note = " (EUR %08X: b%s 0x%X)" % (e, "l" if (e >> 24) & 1 else "", ut) if u != e else ""
    else:
        assert u == e, hex(a); note = ""
    return ua, u, note
hk = open(SRC + "/input/kiu_hooks.h").read()
blk = hk[hk.index("inline constexpr std::array<Context, 75> kContext{{"):]
blk = blk[:blk.index("}};")]
out = []
for line in blk.splitlines()[1:]:
    mm = re.match(r"(\s*)\{Group::(\w+), (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]{8})\},(\s*)(//.*)?$", line)
    if not mm:
        out.append(line); continue
    a = int(mm[3], 16); e = int(mm[4], 16); ua, u, note = usa(a, e)
    out.append(f"{mm[1]}{{Group::{mm[2]}, 0x{ua:06X}, 0x{u:08X}}},{mm[5]}{(mm[6] or '').rstrip()}{note}".rstrip())
print("// kContextUsaRev1"); print("\n".join(out))
tn = open(SRC + "/input/kiu_turn.h").read()
blk = tn[tn.index("kCodeCheck{{"):]; blk = blk[:blk.index("}};")]
print("// kCodeCheckUsaRev1")
for line in blk.splitlines()[1:]:
    mm = re.match(r"(\s*)\{(0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]{8})\},(.*)$", line)
    a = int(mm[2], 16); e = int(mm[3], 16); ua, u, note = usa(a, e)
    print(f"{mm[1]}{{0x{ua:06X}, 0x{u:08X}}},{mm[4]}{note}")
for name, a in [("SINK_CONTINUE", 0x6340A0), ("SPAWN", 0x485638), ("PATH_RESTART", 0x3E8CDC), ("PATH_VANISH", 0x3E8D00),
                ("SHADOW_CONTINUE", 0x57B058), ("FOV_CONTINUE", 0x45E050), ("bullet view fn", 0x4EC640),
                ("follow input fn", 0x7CA280)]:
    print(f"// {name} 0x{a:06X} -> 0x{m(a)[0]:06X}")
