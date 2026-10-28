#!/usr/bin/env python3
"""Train a static order-3 APRS-PPM table for one frame layout: tools/train_model.py plus --mode and --dest-prefix.

  --mode whole    text "SRC>DST[,PATH]:INFO"   model for whole-packet PPM (what v1 was trained on)
  --mode nopath   text "SRC>DST:INFO"          model for hybrid A (the path is native, not compressed)
  --mode info     text ":INFO"                 model for hybrid B (source, destination and path are native)
Train the three modes on the same files with the same --min-count and --holdout so they differ only in the text form.
Train with --holdout 0.3: stations in hash buckets 0..2 stay out of training (prep.py writes exactly those stations).
The 16-bit offset limit caps the table at 65,535 symbol entries; raise --min-count until it fits (about 128 for ~175,000 packets).

Merging with other data (a shared table from several networks):
  --dump-counts FILE.gz   also write the training counts (order, context, symbol, count), pruned at --dump-min, so others can add to them
  --base-counts FILE.gz   start from such a counts file (ours, published in tables/) and add your own packets on top
  --base-weight W         scale the base counts (1.0 = as published); your packets count 1 each, times --boost for matching stations
  --boost W --boost-re R  count packets from sources matching the regex R W times (for example a country's prefixes)
  --texts FILE            train on ready-made texts, one per line (hybrid_layouts DUMPTEXT=1 writes the text hybrid B hands to PPM)
  --max-kb N              raise --min-count until the table is at most N KB and fits the 16-bit offsets
Counts files hold only byte-context counts at or above --dump-min, no packet text.

Original description of the trainer:
Train the static order-3 model for APRS-PPM and write the C table (model/aprs_ppm_model_v1.c).

Input is raw APRS-IS text (one packet per line). The training text is what goes over the air:
SRC>DEST,PATH:info with the APRS-IS q-construct and everything after it removed.

  train_model.py --lora lora.txt --vhf vhf.txt --out model/aprs_ppm_model_v1.c
  train_model.py ... --holdout 0.3 --test-out test_packets.txt     (train on 70% of stations, save the rest)
"""
import argparse, collections, hashlib, re, sys

LINE = re.compile(rb"^([^>]+)>([^,:]+)((?:,[^,:]+)*):(.*)$", re.S)

import gzip
MODE = "whole"
DEST_PREFIX = b"APL"
def load(path, rf_only):
    rows = {}
    for line in (gzip.open if path.endswith(".gz") else open)(path, "rb"):
        line = line.rstrip(b"\r\n")
        if not line or line.startswith(b"#"):
            continue
        m = LINE.match(line)
        if not m:
            continue
        src, dst, path_, info = m.groups()
        keep, rf = [], False
        for p in path_.split(b",")[1:]:
            if p in (b"qAR", b"qAO"):
                rf = True
            if p.startswith(b"q"):
                break
            keep.append(p)
        if (rf_only and not rf) or not info or len(info) > 250:
            continue
        if not dst.startswith(DEST_PREFIX): continue
        if b"\t" in line or b"\n" in info: continue
        whole = src + b">" + dst + (b"," + b",".join(keep) if keep else b"") + b":" + info
        if len(whole) > 255: continue
        text = whole if MODE == "whole" else src + b">" + dst + b":" + info if MODE == "nopath" else b":" + info
        rows[text] = src
    return rows

def station_bucket(src):
    return int(hashlib.md5(src).hexdigest(), 16) % 10

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lora", nargs="*", default=[])
    ap.add_argument("--vhf", nargs="*", default=[])
    ap.add_argument("--out", required=True)
    ap.add_argument("--min-count", type=int, default=2)
    ap.add_argument("--version", type=int, default=1)
    ap.add_argument("--mode", default="whole", choices=("whole", "nopath", "info"))
    ap.add_argument("--dest-prefix", default="APL")
    ap.add_argument("--texts", help="train on the texts in this file (one per line, for example from hybrid_layouts DUMPTEXT=1) instead of --lora/--vhf captures")
    ap.add_argument("--dump-counts")
    ap.add_argument("--dump-min", type=int, default=8)
    ap.add_argument("--base-counts")
    ap.add_argument("--base-weight", type=float, default=1.0)
    ap.add_argument("--boost", type=float, default=1.0)
    ap.add_argument("--boost-re", default="")
    ap.add_argument("--max-kb", type=float, default=0)
    ap.add_argument("--holdout", type=float, default=0.0)
    ap.add_argument("--test-out")
    a = ap.parse_args()
    global MODE, DEST_PREFIX
    MODE = a.mode
    DEST_PREFIX = a.dest_prefix.encode()

    rows = {}
    if a.texts:
        for line in open(a.texts, "rb"):
            line = line.rstrip(b"\n")
            if line and len(line) <= 255: rows[line] = b""
    for f in a.lora: rows.update(load(f, False))
    for f in a.vhf: rows.update(load(f, True))
    cut = int(round(a.holdout * 10))
    train = [t for t, s in rows.items() if station_bucket(s) >= cut]
    test = [t for t, s in rows.items() if station_bucket(s) < cut]
    print(f"{len(rows)} unique packets: {len(train)} train, {len(test)} held out", file=sys.stderr)
    if a.test_out:
        with open(a.test_out, "wb") as f:
            for t in test: f.write(t + b"\n")

    ORD = 3
    tabs = [collections.defaultdict(collections.Counter) for _ in range(ORD + 1)]
    boost_re = re.compile(a.boost_re.encode()) if a.boost_re else None
    for t in train:
        w = a.boost if boost_re and boost_re.match(rows[t]) else 1
        for i, ch in enumerate(t):
            for k in range(min(ORD, i) + 1):
                key = int.from_bytes(t[i - k:i], "big") if k else 0
                tabs[k][key][ch] += w
    if a.base_counts:
        n_base = 0
        for line in gzip.open(a.base_counts, "rt"):
            if line.startswith("#"): continue
            k, key, ch, c = line.split()
            tabs[int(k)][int(key, 16)][int(ch)] += a.base_weight * int(c); n_base += 1
        print(f"added {n_base} base count entries from {a.base_counts} (weight {a.base_weight})", file=sys.stderr)
    if a.dump_counts:
        with gzip.open(a.dump_counts, "wt") as f:
            f.write(f"# order context(hex) symbol count; byte-context counts >= {a.dump_min}, from {len(train)} training packets\n")
            for k in range(ORD + 1):
                for key in sorted(tabs[k]):
                    for sy, c in sorted(tabs[k][key].items()):
                        if c >= a.dump_min: f.write(f"{k} {key:x} {sy} {int(round(c))}\n")

    def build(min_count):
        keys, offs, tots, syms, cnts, ctx_counts = [], [], [], [], [], []
        for k in range(ORD + 1):
            ks, os_, ts = [], [], []
            for key in sorted(tabs[k]):
                kept = sorted((s, round(c)) for s, c in tabs[k][key].items() if c >= min_count)
                kept = [(s, c) for s, c in kept if c > 0]
                if not kept:
                    continue
                top = max(c for _, c in kept)
                if top > 255:                         # counts are stored in one byte; keep the proportions
                    kept = [(s, max(1, c * 255 // top)) for s, c in kept]
                tot = sum(c for _, c in kept)
                ks.append(key); os_.append(len(syms)); ts.append(tot)
                for s, c in kept:
                    syms.append(s); cnts.append(c)
            os_.append(len(syms))
            keys.append(ks); offs.append(os_); tots.append(ts); ctx_counts.append(len(ks))
        return keys, offs, tots, syms, cnts, ctx_counts

    min_count = a.min_count
    while True:
        keys, offs, tots, syms, cnts, ctx_counts = build(min_count)
        size = sum(ctx_counts) * 5 + len(syms) * 2  # flash estimate
        fits = len(syms) < 65536 and (not a.max_kb or size / 1024 <= a.max_kb)
        if fits or not (a.max_kb or len(syms) >= 65536): break
        min_count = min_count + 1 if min_count < 20 else int(min_count * 1.05) + 1
    assert len(syms) < 65536, "offsets are 16-bit; model too large for this format"
    if min_count != a.min_count: print(f"min count raised from {a.min_count} to {min_count} to fit", file=sys.stderr)
    a.min_count = min_count

    def arr(ctype, name, vals):
        body = ",".join(str(v) for v in vals)
        lines = [body[i:i + 100] for i in range(0, len(body), 100)] if False else None
        out, line = [], ""
        for v in vals:
            s = str(v) + ","
            if len(line) + len(s) > 110:
                out.append(line); line = ""
            line += s
        out.append(line)
        return f"static const {ctype} {name}[] = {{\n" + "\n".join(out) + "\n};\n"

    with open(a.out, "w") as f:
        f.write("/* Generated by tools/train_model.py - do not edit. */\n")
        f.write(f"/* {len(train)} training packets, min count {a.min_count}, contexts per order {ctx_counts}, {len(syms)} symbol entries. */\n")
        f.write('#include "aprs_ppm_model.h"\n\n')
        f.write(f"const uint8_t aprs_ppm_model_version_value = {a.version};\n\n")
        for k in range(ORD + 1):
            f.write(arr("uint8_t", f"KEYS{k}", [b for key in keys[k] for b in key.to_bytes(3, "big")]))
            f.write(arr("uint16_t", f"OFFS{k}", offs[k]))
        f.write(arr("uint8_t", "SYM", syms).replace("static const", "const").replace("SYM[]", "aprs_ppm_symbols[]"))
        f.write(arr("uint8_t", "CNT", cnts).replace("static const", "const").replace("CNT[]", "aprs_ppm_counts[]"))
        f.write("const aprs_ppm_order_t aprs_ppm_orders[APRS_PPM_ORDER + 1] = {\n")
        for k in range(ORD + 1):
            f.write(f"    {{ {ctx_counts[k]}, KEYS{k}, OFFS{k} }},\n")
        f.write("};\n")
    size = sum(ctx_counts) * 5 + len(syms) * 2  # flash estimate
    print(f"wrote {a.out}; approx flash {size/1024:.0f} KB", file=sys.stderr)

main()
