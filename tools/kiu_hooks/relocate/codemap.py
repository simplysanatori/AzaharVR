"""AzaharFrame kiuusa: the EUR -> USA Rev 1 code map from the two text dumps (0x100000-0xC96000,
read from the running games through the emulator's RPC server; never committed). Not part of the
build. The steps for another build (its text end below in __main__, the cave in resolve.py):
  codemap.py text-eur.bin text-other.bin segs.pkl     the map, its branch check
  datamap.py text-eur.bin text-other.bin segs.pkl data.pkl     rodata/data shifts
  resolve.py text-eur.bin text-other.bin segs.pkl data.pkl src/frame_xr     every address, checked
  gen_usa.py text-eur.bin text-other.bin segs.pkl src/frame_xr     kContext / kCodeCheck rows
Anchors: 4-word runs of non-branch words unique in both; the shift is piecewise constant.
Then every EUR branch is checked against the USA one (its target mapped), and literal words
pointing into ro/data/bss are paired."""
import struct, sys, collections, bisect, pickle
BASE = 0x100000
def load(p):
    b = open(p, "rb").read(); return struct.unpack("<%dI" % (len(b)//4), b)
def isbr(w): return (w >> 25) & 7 == 5 and (w >> 28) != 0xF
def brtarget(at, w):
    off = w & 0xFFFFFF
    if off & 0x800000: off -= 0x1000000
    return at + 8 + off*4
def build(E, U, eur_text_end, usa_text_end):
    nE = (eur_text_end - BASE)//4; nU = (usa_text_end - BASE)//4
    def keys(W, n):
        d = collections.defaultdict(list)
        for i in range(n-3):
            k = W[i:i+4]
            if any(isbr(w) for w in k) or k.count(0) > 1: continue
            d[k].append(i)
        return d
    kE = keys(E, nE); kU = keys(U, nU)
    anchors = []
    for k, li in kE.items():
        if len(li) == 1 and len(kU.get(k, [])) == 1:
            anchors.append((li[0], kU[k][0]-li[0]))
    anchors.sort()
    # Smooth: keep anchors whose shift agrees with a neighbour (drop lone outliers).
    good = [a for j, a in enumerate(anchors)
            if (j > 0 and anchors[j-1][1] == a[1]) or (j+1 < len(anchors) and anchors[j+1][1] == a[1])]
    # Segments: runs of equal shift.
    segs = []  # (eur_start_idx, eur_end_idx, shift_words)
    for i, s in good:
        if segs and segs[-1][2] == s: segs[-1][1] = i
        else: segs.append([i, i, s])
    return segs, len(anchors), len(good)
def mapper(segs):
    starts = [s[0] for s in segs]
    def m(addr):
        i = (addr - BASE)//4
        j = bisect.bisect_right(starts, i) - 1
        if j < 0: return None, "before"
        a, b, s = segs[j]
        if i <= b: return BASE + (i+s)*4, "in"
        # gap between segment j and j+1: unknown unless both shifts agree
        if j+1 < len(segs) and segs[j+1][2] == s: return BASE + (i+s)*4, "gap-same"
        return BASE + (i+s)*4, "gap"
    return m
if __name__ == "__main__":
    E = load(sys.argv[1]); U = load(sys.argv[2])
    segs, na, ng = build(E, U, 0xC95370, 0xC950F0)
    print("anchors", na, "kept", ng, "segments", len(segs))
    sh = collections.Counter()
    for a, b, s in segs: sh[s*4] += (b-a+1)
    print("words covered by shift:", {("-" if k<0 else "")+hex(abs(k)): v for k, v in sh.most_common(20)})
    m = mapper(segs)
    # Branch check over the covered words.
    ok = bad = unk = 0; badl = []
    for a, b, s in segs:
        for i in range(a, b+1):
            w = E[i]
            if not isbr(w): continue
            at = BASE + i*4; t = brtarget(at, w)
            ua, how = m(at); ut, how2 = m(t)
            if how2 not in ("in", "gap-same") or not (BASE <= t < 0xC95370): unk += 1; continue
            uw = U[(ua-BASE)//4]
            if isbr(uw) and (uw >> 24) == (w >> 24) and brtarget(ua, uw) == ut: ok += 1
            else: bad += 1; badl.append((hex(at), hex(w), hex(ua), hex(uw)))
    print("branches: ok", ok, "bad", bad, "target unknown", unk, badl[:10])
    # Non-branch word equality over covered words.
    eq = ne = 0
    for a, b, s in segs:
        for i in range(a, b+1):
            if isbr(E[i]): continue
            if E[i] == U[i+s]: eq += 1
            else: ne += 1
    print("non-branch words equal", eq, "differ", ne)
    pickle.dump(segs, open(sys.argv[3], "wb"))
