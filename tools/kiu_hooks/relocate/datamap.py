"""AzaharFrame kiuusa (see codemap.py): EUR -> USA Rev 1 pairs of literal words that point into ro (0xC96000-), data
(0xD32000-) and bss, read at mapped code places; the shift per region."""
import sys, pickle, collections
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from codemap import load, BASE
E = load(sys.argv[1]); U = load(sys.argv[2]); segs = pickle.load(open(sys.argv[3], "rb"))
pairs = collections.defaultdict(collections.Counter)
for a, b, s in segs:
    for i in range(a, b+1):
        e = E[i]
        if 0xC96000 <= e < 0xF40000:
            pairs[e][U[i+s] - e] += 1
# Region shifts in EUR value order.
rows = []
for e in sorted(pairs):
    d, n = pairs[e].most_common(1)[0]
    rows.append((e, d, n, sum(pairs[e].values())))
runs = []
for e, d, n, t in rows:
    if runs and runs[-1][2] == d: runs[-1][1] = e; runs[-1][3] += 1
    else: runs.append([e, e, d, 1])
def nm(x): return ("-" if x < 0 else "+") + hex(abs(x))
print("distinct EUR literal targets:", len(rows), "runs:", len(runs))
for a, b, d, k in runs:
    if k >= 3 or True: print(hex(a), hex(b), nm(d), k)
pickle.dump(rows, open(sys.argv[4], "wb"))
