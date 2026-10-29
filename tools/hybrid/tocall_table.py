#!/usr/bin/env python3
"""Build the destination ("tocall") table for the TOCALL=1 variant of hybrid_layouts.c from the TRAINING stations only.

  tocall_table.py TRAIN.tsv tocalls.yaml OUT.txt [--top 192]
  tocall_table.py --base TABLE.txt --extra EXTRA.txt [--pattern-extra APGRW?,...] OUT.txt     add entries to an existing table (see below)

TRAIN.tsv is prep.py's output for the training stations (prep.py --train-from 3 ...). OUT.txt has, in order:
  E <destination>   the --top most frequent exact destination strings (1 byte each: codes 0x00 to 0xBF)
  P <pattern>       every registry pattern (aprsorg/aprs-deviceid, tocalls.yaml) of 6 characters with exactly one wildcard, limited to LoRa
                    devices by --pattern-prefix APL (most registry entries are VHF radios and software a LoRa device never sends) ('?' any
                    character, 'n' a digit); a destination that matches costs 2 bytes (pattern number + the wildcard character)
With --base the table is not rebuilt: the lines of TABLE.txt are kept in order (so every existing code stays the same) and the destinations in EXTRA.txt
(one per line, '#' starts a comment) are added as exact entries after the existing exact ones, and --pattern-extra patterns after the existing patterns.
Use it for destinations the training stations never show, for example the non-LoRa software a cross-band relay passes on.
Without --base it prints the coverage of the training packets: how many entries reach 90, 95 and 98%."""
import argparse, collections, re, sys
ap = argparse.ArgumentParser(); ap.add_argument("files", nargs="+", help="TRAIN.tsv tocalls.yaml OUT.txt, or just OUT.txt with --base"); ap.add_argument("--top", type=int, default=192)
ap.add_argument("--base"); ap.add_argument("--extra"); ap.add_argument("--pattern-extra", default="")
ap.add_argument("--pattern-prefix", default="APL", help="only registry entries starting with this (LoRa devices: APL); '' keeps the whole registry")
a = ap.parse_args()
if a.base:
    lines = [l.rstrip("\n") for l in open(a.base) if l.strip()]
    ex = [l.split("#")[0].strip() for l in open(a.extra)] if a.extra else []
    new_e = [d for d in dict.fromkeys(x for x in ex if x) if f"E {d}" not in lines]
    new_p = [p for p in a.pattern_extra.split(",") if p and f"P {p}" not in lines]
    E = [l for l in lines if l.startswith("E ")] + [f"E {d}" for d in new_e]; P = [l for l in lines if l.startswith("P ")] + [f"P {p}" for p in new_p]
    assert len(E) <= 192 and len(P) <= 256, (len(E), len(P))
    open(a.files[-1], "w").write("\n".join(E + P) + "\n")
    print(f"{len(E)} exact entries ({len(new_e)} new), {len(P)} patterns ({len(new_p)} new)", file=sys.stderr); sys.exit(0)
a.tsv, a.yaml, a.out = a.files
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
