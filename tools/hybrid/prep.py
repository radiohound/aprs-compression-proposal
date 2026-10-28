#!/usr/bin/env python3
"""Turn raw packet captures into the TSV that hybrid_layouts and mixed_reception read.

  prep.py OUT.tsv FILE... [--dest-prefix APL] [--test-buckets 3] [--all-rf]

Input lines are APRS-IS style "SRC>DST,PATH,qAR,IGATE:info" or plain TNC2 "SRC>DST,PATH:info" (text or .gz). Everything from the
q-construct on is dropped, as it is not sent over the air. Output row:
    src <TAB> rf(0/1) <TAB> kind(S/M/O) <TAB> path <TAB> text      text = SRC>DST[,PATH]:INFO
  rf    1 if the packet reached APRS-IS through qAR/qAO (heard over RF); --all-rf sets 1 for every row (use it for plain TNC2 logs)
  kind  S stationary, M moving, O other, from the spread of the station's decoded positions
With --train-from N the other side of the split is written (buckets N..9, the training stations; the harness reads it only to build tables).
Only stations whose callsign hash falls in buckets 0..(test-buckets-1) of 10 are written: the same split the model trainer
(train_layout_model.py --holdout 0.3) keeps out of training, so test stations are never in a training set. Consecutive duplicate
texts from one station are dropped. --dest-prefix '' accepts every destination (default APL = LoRa APRS devices)."""
import argparse, collections, gzip, hashlib, os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from delta_sim_pos import position

LINE = re.compile(rb"^([^>]+)>([^,:]+)((?:,[^,:]+)*):(.*)$", re.S)
bucket = lambda s: int(hashlib.md5(s).hexdigest(), 16) % 10

ap = argparse.ArgumentParser()
ap.add_argument("out"); ap.add_argument("files", nargs="+")
ap.add_argument("--dest-prefix", default="APL"); ap.add_argument("--test-buckets", type=int, default=3); ap.add_argument("--all-rf", action="store_true")
ap.add_argument("--train-from", type=int, default=0, help="write the training stations instead: callsign-hash buckets N..9 (3 matches --test-buckets 3)")
a = ap.parse_args()
prefix = a.dest_prefix.encode()
seqs = collections.OrderedDict(); pos = collections.defaultdict(list)
for fn in a.files:
    for line in (gzip.open if fn.endswith(".gz") else open)(fn, "rb"):
        line = line.rstrip(b"\r\n")
        if not line or line.startswith(b"#"): continue
        m = LINE.match(line)
        if not m: continue
        src, dst, path_, info = m.groups()
        if not dst.startswith(prefix): continue
        if (bucket(src) < a.train_from) if a.train_from else (bucket(src) >= a.test_buckets): continue
        keep, rf = [], 1 if a.all_rf else 0
        for p in path_.split(b",")[1:]:
            if p in (b"qAR", b"qAO"): rf = 1
            if p.startswith(b"q"): break
            keep.append(p)
        if not info or len(info) > 250 or b"\t" in line or b"\n" in info: continue
        path = b",".join(keep)
        text = src + b">" + dst + (b"," + path if path else b"") + b":" + info
        if len(text) > 255: continue
        l = seqs.setdefault(src, [])
        if l and l[-1][2] == text: continue
        l.append((rf, path, text))
        pp = position(info)
        if pp: pos[src].append(pp)

def kind(src):
    pl = pos.get(src, [])
    if len(pl) < 3: return b"O"
    d = max(max(x for x, y in pl) - min(x for x, y in pl), max(y for x, y in pl) - min(y for x, y in pl))
    return b"S" if d <= 0.002 else b"M" if d >= 0.005 else b"O"

n = 0
with open(a.out, "wb") as out:
    for src, l in seqs.items():
        k = kind(src)
        for rf, path, text in l:
            out.write(src + b"\t" + str(rf).encode() + b"\t" + k + b"\t" + path + b"\t" + text + b"\n"); n += 1
print(n, "packets,", len(seqs), "stations", file=sys.stderr)
