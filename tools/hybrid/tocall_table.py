#!/usr/bin/env python3
"""Build the destination ("tocall") table for the TOCALL=1 variant of hybrid_layouts.c from the TRAINING stations only.

  tocall_table.py TRAIN.tsv tocalls.yaml OUT.txt [--top 192]

TRAIN.tsv is prep.py's output for the training stations (prep.py --train-from 3 ...). OUT.txt has, in order:
  E <destination>   the --top most frequent exact destination strings (1 byte each: codes 0x00 to 0xBF)
  P <pattern>       every registry pattern (aprsorg/aprs-deviceid, tocalls.yaml) of 6 characters with exactly one wildcard, limited to LoRa
                    devices by --pattern-prefix APL (most registry entries are VHF radios and software a LoRa device never sends) ('?' any
                    character, 'n' a digit); a destination that matches costs 2 bytes (pattern number + the wildcard character)
and prints the coverage of the training packets: how many entries reach 90, 95 and 98%."""
import argparse, collections, re, sys
ap = argparse.ArgumentParser(); ap.add_argument("tsv"); ap.add_argument("yaml"); ap.add_argument("out"); ap.add_argument("--top", type=int, default=192)
ap.add_argument("--pattern-prefix", default="APL", help="only registry entries starting with this (LoRa devices: APL); '' keeps the whole registry")
a = ap.parse_args()
cnt = collections.Counter(); n = 0
for line in open(a.tsv, "rb"):
    f = line.rstrip(b"\n").split(b"\t")
    if len(f) < 5: continue
    m = re.match(rb"^[^>]+>([^,:]+)", f[4])
    if m: cnt[m.group(1).decode("latin1")] += 1; n += 1
ranked = cnt.most_common()
acc = 0; reach = {}
for i, (d, c) in enumerate(ranked, 1):
    acc += c
    for p in (0.90, 0.95, 0.98):
        if p not in reach and acc >= p * n: reach[p] = i
pats = [p for p in re.findall(r"^\s*-?\s*tocall:\s*(\S+)", open(a.yaml).read(), re.M)
        if len(p) == 6 and p.startswith(a.pattern_prefix) and sum(c in "?n" for c in p) == 1 and not set(p) - set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789?n")]
assert len(pats) <= 256
top = [d for d, _ in ranked[:a.top]]
with open(a.out, "w") as f:
    for d in top: f.write(f"E {d}\n")
    for p in pats: f.write(f"P {p}\n")
cov = sum(c for _, c in ranked[:a.top])
print(f"{n} training packets, {len(ranked)} distinct destinations; top {a.top} cover {100*cov/n:.1f}%; entries for 90% / 95% / 98%: {reach.get(0.90)} / {reach.get(0.95)} / {reach.get(0.98)}; {len(pats)} one-wildcard registry patterns", file=sys.stderr)
