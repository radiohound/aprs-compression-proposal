#!/usr/bin/env python3
"""Draws docs/img/lacf-frame-layout.svg: the layout of a LACF frame (spec 11) and a worked example with real bytes.

    python3 tools/lacf_diagram.py > docs/img/lacf-frame-layout.svg

The example bytes come from the Python reference code (tools/hb/hb_py/hb.py), so the picture cannot drift from the spec:
the frame is encoded and relayed by the reference, and its fields are read back from the parsed frame. Needs nothing else."""
import os, sys
from xml.sax.saxutils import escape
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "hb", "hb_py"))
import hb

PKT = b"N0CALL-9>APLRG1,WIDE1-1,WIDE2-2:!3722.40N/12158.93W>LoRa APRS iGate on the hill"
frame = hb.encode(PKT)
d = hb._parse(frame)
relayed = hb.relay_auto(frame)
e = hb._parse(relayed)
assert len(frame) == 35 and len(relayed) == 44 and hb.decode(frame) == PKT

W = 1100
SANS = "system-ui, -apple-system, 'Segoe UI', Helvetica, Arial, sans-serif"
MONO = "ui-monospace, Menlo, Consolas, 'DejaVu Sans Mono', monospace"
INK, MUTED, LINE = "#1f2328", "#59636e", "#8c959f"
C = dict(marker="#d9d9d9", header="#fdd0a2", source="#bcd7f7", dest="#b6e3dc", path="#d4c8f0", pos="#c9e8b4",
         vlc="#f6c1d9", payload="#eb9bc0", tuple="#f9e08a", mask="#e8c24a")
out = []
MAXY = 0
import textwrap
def add(s): out.append(s)
def text(x, y, s, size=12, weight="normal", fill=INK, anchor="start", family=SANS, extra=""):
    global MAXY
    MAXY = max(MAXY, y + 6)
    add(f'<text x="{x:.1f}" y="{y:.1f}" font-family="{family}" font-size="{size}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}" {extra}>{escape(s)}</text>')
def rect(x, y, w, h, fill, stroke=LINE, sw=1, rx=4, dash=""):
    add(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" {dash}/>')
def line(x1, y1, x2, y2, stroke=LINE, sw=1, dash=""):
    add(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{stroke}" stroke-width="{sw}" {dash}/>')

H = 0
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" role="img" aria-labelledby="t d">')
add('<title id="t">LACF frame layout</title>')
add('<desc id="d">The fields of a LACF frame (LoRa APRS Compact Frame, spec 11 draft) and a worked example: a position beacon of 82 bytes as plain text is 35 bytes as a LACF frame and 44 bytes after one relay hop.</desc>')
rect(0, 0, W, 0, "#ffffff", stroke="#ffffff", rx=0)
text(30, 38, "LACF frame layout", 24, "bold")
text(30, 60, "LoRa APRS Compact Frame, spec 11 draft. A draft in early development; the format may change. Sizes are in bytes.", 13, fill=MUTED)

# ---------------------------------------------------------------- A. the fields
text(30, 100, "1. The fields, in the order they are sent", 16, "bold")
fields = [
    ("Marker", "3 B", "3C FF 02", "marker", 104, ["Says this is a native frame. Other firmware ignores it."]),
    ("Header", "1 B", "bit by bit below", "header", 92, ["Number of path elements, RXT flag, form (LACF or plain), position block."]),
    ("Source", "5 B", "callsign + SSID", "source", 108, ["The sender, packed into 5 bytes; unusual names go as text."]),
    ("Destination", "1 B", "table code", "dest", 110, ["Which device or software sent it (a list of 62); 1 + 5 B if not listed."]),
    ("Path", "1 B each", "or 5 B", "path", 112, ["One byte for WIDE1-1, WIDE2-2 and so on; 5 bytes for a callsign."]),
    ("Position", "9 B", "kinds 1 and 3", "pos", 100, ["Latitude, longitude and symbol, exact (lossless)."]),
    ("Content", "V L CRC + n", "", "vlc", 222, []),
    ("RXT block", "4 B per hop + 1", "added by relays", "tuple", 148, ["What each relay heard: signal, noise, offset, time to hop."]),
]
gap, x = 6, 30
y0, bh = 112, 66
maxlines = 0
centers = []
for name, size, sub, key, w, notes in fields:
    dash = 'stroke-dasharray="5 4"' if key in ("tuple", "pos") else ""
    if key == "vlc":
        # three small boxes and the payload
        parts = [("V", "1 B", 34, C["vlc"]), ("L", "1 B", 34, C["vlc"]), ("CRC", "2 B", 50, C["vlc"]), ("PPM payload", "n bytes", w - 34 - 34 - 50, C["payload"])]
        px = x
        for lbl, sz, pw, col in parts:
            rect(px, y0, pw, bh, col)
            text(px + pw / 2, y0 + 28, lbl, 12 if lbl != "PPM payload" else 13, "bold", anchor="middle")
            text(px + pw / 2, y0 + 46, sz, 11, fill=MUTED, anchor="middle")
            px += pw
        text(x + w / 2, y0 - 6, "Content field", 13, "bold", anchor="middle")
        wr = textwrap.wrap("The text of the packet (or the comment after a position), coded by PPM. V = protocol and table version, L = length, CRC-16 over the text and addresses.", int(w / 6.1))
        maxlines = max(maxlines, len(wr))
        for i, s_ in enumerate(wr):
            text(x + w / 2, y0 + bh + 18 + 14 * i, s_, 11, fill=MUTED, anchor="middle")
    else:
        rect(x, y0, w, bh, C[key], dash=dash)
        text(x + w / 2, y0 + 25, name, 13, "bold", anchor="middle")
        text(x + w / 2, y0 + 43, size, 12, anchor="middle")
        if sub: text(x + w / 2, y0 + 58, sub, 10.5, fill=MUTED, anchor="middle")
        wr = textwrap.wrap(" ".join(notes), max(8, int(w / 6.1)))
        maxlines = max(maxlines, len(wr))
        for i, s_ in enumerate(wr):
            text(x + w / 2, y0 + bh + 18 + 14 * i, s_, 11, fill=MUTED, anchor="middle")
    x += w + gap
ynote = y0 + bh + 18 + 14 * maxlines + 12
text(30, ynote, "Dashed boxes are optional. The position block exists only in kinds 1 and 3 (9 B for a plain position; longer with a timestamp, course or altitude).", 11.5, fill=MUTED)
text(30, ynote + 16, "The RXT block only appears after a relay has added its measurements.", 11.5, fill=MUTED)
text(30, ynote + 32, "Plain content (header kind 0 or 1) is V, CRC and the text; the encoder uses it when LACF content (V L CRC payload) would not make the frame shorter.", 11.5, fill=MUTED)

# ---------------------------------------------------------------- B. bit fields
yb = ynote + 32 + 34
text(30, yb, "2. Two bytes in detail", 16, "bold")
def bits(x, y, spec, title):
    text(x, y, title, 13, "bold")
    cx = x
    for label, n, note, col in spec:
        w = 58 * n
        rect(cx, y + 10, w, 34, col)
        text(cx + w / 2, y + 32, label, 13, "bold", anchor="middle", family=MONO)
        for k, ln in enumerate(note):
            text(cx + w / 2, y + 62 + 14 * k, ln, 11, fill=MUTED, anchor="middle")
        cx += w + 3
    return cx
bits(30, yb + 28, [("7-4", 4, ["path elements", "(0 to 15)"], C["path"]), ("3", 1, ["RXT", "follows"], C["tuple"]), ("2", 1, ["reserved", "= 0"], "#eeeeee"), ("1-0", 2, ["kind: bit 1 = form", "(1 LACF, 0 plain);", "bit 0 = position block"], C["header"])], "Header byte")
bits(580, yb + 28, [("vvvvv", 5, ["protocol version", "(2 today)"], C["vlc"]), ("ttt", 3, ["table version", "(0 today)"], C["payload"])], "V, the first byte of the content field")
add_y = yb + 28 + 124
text(30, add_y, "RXT tuple (4 bytes per relay, then one mask byte at the very end):", 13, "bold")
tx = 30
for lbl, note in [("RSSI", "signal strength"), ("SNR", "signal to noise"), ("freq. offset", "of the sender"), ("time to hop", "wait in the relay")]:
    rect(tx, add_y + 10, 110, 34, C["tuple"]); text(tx + 55, add_y + 32, lbl, 12, "bold", anchor="middle"); text(tx + 55, add_y + 58, note, 11, fill=MUTED, anchor="middle"); tx += 113
rect(tx + 6, add_y + 10, 150, 34, C["mask"]); text(tx + 81, add_y + 32, "mask", 12, "bold", anchor="middle"); text(tx + 81, add_y + 58, "bit 7 set; bits 0 to 6 name", 11, fill=MUTED, anchor="middle"); text(tx + 81, add_y + 72, "the path elements measured", 11, fill=MUTED, anchor="middle")

# ---------------------------------------------------------------- C. worked example
yc = add_y + 134          # room below the RXT tuple notes, so section 3 does not crowd them
text(30, yc, "3. A worked example: one position beacon", 16, "bold")
text(30, yc + 24, PKT.decode(), 12, family=MONO)
text(30, yc + 42, "As an ordinary LoRa APRS frame that is 3 + 79 = 82 bytes of text.", 12, fill=MUTED)

CW, CH = 23, 32
X0 = (W - 44 * CW) / 2
def cells(y, data, groups, caption):
    text(X0, y - 10, caption, 13, "bold")
    for i, b in enumerate(data):
        for g in groups:
            if g[0] <= i < g[1]: col = g[3]; break
        rect(X0 + i * CW, y, CW, CH, col, rx=2)
        text(X0 + i * CW + CW / 2, y + 20, f"{b:02X}", 10.5, anchor="middle", family=MONO)
    # labels on a ladder: the leftmost group gets the deepest level, so no leader line crosses a label
    n = len(groups) - (1 if any(X0 + (g[0] + g[1]) / 2 * CW + 4 + 6.0 * len(g[2]) > W - 14 for g in groups) else 0)
    idx_gen = 0
    for g in sorted(groups, key=lambda g: g[0]):
        idx = idx_gen if X0 + (g[0] + g[1]) / 2 * CW + 4 + 6.0 * len(g[2]) <= W - 14 else 0
        if X0 + (g[0] + g[1]) / 2 * CW + 4 + 6.0 * len(g[2]) <= W - 14: idx_gen += 1
        s_, t_, label = g[0], g[1], g[2]
        level = n - 1 - idx
        cx = X0 + (s_ + t_) / 2 * CW
        yl = y + CH + 17 + level * 15
        if cx + 4 + 6.0 * len(label) <= W - 14:
            line(X0 + s_ * CW + 1, y + CH + 3, X0 + t_ * CW - 1, y + CH + 3, stroke=LINE, sw=1.5)
            line(cx, y + CH + 3, cx, yl - 9, stroke=LINE)
        if cx + 4 + 6.0 * len(label) > W - 14:
            # the rightmost label would cross the leader lines to its left: put it above the row instead
            line(X0 + s_ * CW + 1, y - 3, X0 + t_ * CW - 1, y - 3, stroke=LINE, sw=1.5)
            line(cx, y - 3, cx, y - 14, stroke=LINE)
            text(min(cx + 4, W - 14), y - 17, label, 11, fill=INK, anchor="end")
            continue
        text(cx + 4, yl + 3, label, 11, fill=INK)
    return y + CH + 17 + (n - 1) * 15 + 16

g1 = [
    (0, 3, "marker 3C FF 02", None, None), (3, 4, "header 23: 2 path elements, kind 3", None, None), (4, 9, "source N0CALL-9", None, None),
    (9, 10, "destination code 0 = APLRG1", None, None), (10, 12, "path: WIDE1-1, WIDE2-2", None, None),
    (12, 21, "position block: ! 37°22.40'N 121°58.93'W, symbol >", None, None), (21, 25, "V = 10 (protocol 2, table 0), L = 27 characters, CRC-16", None, None),
    (25, 35, "PPM payload = \"LoRa APRS iGate on the hill\"", None, None)]
cols1 = [C["marker"], C["header"], C["source"], C["dest"], C["path"], C["pos"], C["vlc"], C["payload"]]
g1 = [(a, b, l, cols1[i]) for i, (a, b, l, _, _) in enumerate(g1)]
ya = yc + 86
yend = cells(ya, list(frame), g1, "As a LACF frame: 35 bytes")

# the relayed frame
rel_path_a, rel_path_b = e["path_start"], e["path_end"]
g2 = [(0, 3, "marker", C["marker"]), (3, 4, "header 2B: RXT flag now set", C["header"]), (4, 9, "source (unchanged)", C["source"]), (9, 10, "destination (unchanged)", C["dest"]),
      (rel_path_a, rel_path_a + 5, "N0RLY-1*: the relay's callsign replaces WIDE1-1", C["path"]), (rel_path_a + 5, rel_path_b, "WIDE2-2", C["path"]),
      (e["pos_start"], e["pos_end"], "position block, copied unchanged", C["pos"]), (e["cstart"], e["cstart"] + 4, "V, L, CRC: copied", C["vlc"]),
      (e["cstart"] + 4, e["content_end"], "PPM payload, copied", C["payload"]),
      (e["content_end"], 44, "RXT block added by the relay: tuple 50 20 45 10, mask 81", C["tuple"])]
yb2 = yend + 36
yend2 = cells(yb2, list(relayed), g2, "After one relay hop: 44 bytes (the content is copied, the RXT block is appended)")

# size bars
ys = yend2 + 30
text(30, ys, "Sizes at a glance", 13, "bold")
bars = [("plain LoRa APRS text frame", 82, "#d9d9d9", "82 B"), ("LACF frame", 35, C["source"], "35 B, 57% smaller for this packet (real packets average about 44%)"),
        ("LACF after one relay hop, with the RXT block", 44, C["tuple"], "44 B, against 83 B as text: 47% smaller for this packet")]
for i, (lbl, n, col, note) in enumerate(bars):
    yy = ys + 14 + i * 30
    rect(30, yy, n * 8, 20, col)
    text(30 + n * 8 + 10, yy + 15, f"{lbl}: {note}", 12)
text(30, ys + 14 + 3 * 30 + 12, "Colours only group the bytes; every group is also named. Frame bytes come from the reference code (tools/hb) and a different packet gives different bytes and sizes.", 11.5, fill=MUTED)
text(30, ys + 14 + 3 * 30 + 30, "Spec: spec/11-native-ppm-frame-byte-plan.md. Developed with Fabrice Morel, F4MLV (moricef): his native LoRa APRS frame with K6ATV's PPM compression.", 11.5, fill=MUTED)
add("</svg>")
body = "\n".join(out)
H = int(MAXY + 24)
body = body.replace('width="1100" height="0" viewBox="0 0 1100 0"', f'width="1100" height="{H}" viewBox="0 0 1100 {H}"').replace('<rect x="0.0" y="0.0" width="1100.0" height="0.0"', f'<rect x="0.0" y="0.0" width="1100.0" height="{H}.0"')
print(body)
