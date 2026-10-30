#!/usr/bin/env python3
"""Make the two Mic-E test sets from raw APRS-IS captures (the hourly captures keep every destination).

  mice_sets.py OUT_DIR CAPTURE.txt[.gz]...

Writes, in TNC2 lines (real callsigns: keep them private, never commit them):
  OUT_DIR/lora_gated.txt   every packet that a LoRa iGate (a station whose own packets use APLRG*, APLDG*, APLG*, APLHB9 or APLIG*) gated as heard over RF
                           (q-construct qAR or qAO naming that station), with all destinations: the Mic-E that reaches LoRa
  OUT_DIR/mice_any.txt     every Mic-E packet in the captures, whoever gated it (mostly VHF): many more Mic-E, for checking the decoding
Then make the held-out test sets (callsign-hash buckets 0 to 2) with:
  prep.py OUT_DIR/lora_gated.tsv OUT_DIR/lora_gated.txt --dest-prefix '' --test-buckets 3
  prep.py OUT_DIR/mice_any.tsv OUT_DIR/mice_any.txt --dest-prefix '' --test-buckets 3"""
import gzip, os, re, sys
LINE = re.compile(rb"^([^>]+)>([^,:]+)((?:,[^,:]+)*):(.*)$", re.S)
Q = re.compile(rb",(qA[RO]),([^,:]+)")
IGATE = re.compile(rb"^(APLRG|APLDG|APLG|APLHB9|APLIG)")
is_mice = lambda info: info[:1] in (b"`", b"'", b"\x1c", b"\x1d")
if len(sys.argv) < 3: sys.exit(__doc__)
out, rows, igates = sys.argv[1], [], set()
for fn in sys.argv[2:]:
    for line in (gzip.open if fn.endswith(".gz") else open)(fn, "rb"):
        line = line.rstrip(b"\r\n"); m = LINE.match(line)
        if not m: continue
        src, dst, path, info = m.groups(); rows.append((line, path, info))
        if IGATE.match(dst): igates.add(src)
os.makedirs(out, exist_ok=True)
with open(os.path.join(out, "lora_gated.txt"), "wb") as f:
    for line, path, info in rows:
        q = Q.search(path)
        if q and q.group(2) in igates: f.write(line + b"\n")
with open(os.path.join(out, "mice_any.txt"), "wb") as f:
    for line, path, info in rows:
        if is_mice(info): f.write(line + b"\n")
print(f"{len(rows)} packets read, {len(igates)} LoRa iGate stations found; wrote lora_gated.txt and mice_any.txt in {out}", file=sys.stderr)
