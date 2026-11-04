"""Checks of the proposal 2.1.0-dev.1 on the Python reference (spec 11). Run:  python3 tools/hb/hb_py/test_dev1.py   (from the repository root).
The C reference and the vector files are still 2.0.0 and do not agree with this file yet."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hb as H
ROOT = H.ROOT
res = []
def ok(name, cond, note=''): res.append((name, bool(cond), note))
PKT = b'N0CALL-9>APLRG1,WIDE1-1,WIDE2-2:!3722.40N/12158.93W>LoRa APRS iGate on the hill'
RAW = b'F4MLV-2>APLRG1,WIDE1-1:>TEST'
fr = H.encode(PKT); raw = H.encode(RAW)
cs = lambda f: H._parse(f)['cstart']
# V byte c ss mmmmm
ok('V of a normal frame is 0x00 (model 0, no capability)', fr[cs(fr)] == 0x00)
ok('example frame is 35 bytes and decodes', len(fr) == 35 and H.decode(fr) == PKT)
# a plain frame built with model bits set is accepted (the CRC is computed over that V)
def plain_with_v(V):
    src, dest, path, info = H.split_tnc2(RAW)
    hdr = (len(path) << 4)
    crc = H.crc16_x25(H.crc_input(hdr, V, src, dest, info))
    return b'\x3c\xff\x02' + bytes([hdr]) + H.enc_elem(src) + b'\x00' + b''.join(H.enc_elem(t) for t in path) + bytes([V, crc & 255, crc >> 8]) + info
ok('plain frame with model bits 0x1F is accepted (plain uses no model)', H.decode(plain_with_v(0x1F)) == RAW)
ok('plain frame with spare bits ss = 3 is accepted (ignored)', H.decode(plain_with_v(0x60)) == RAW)
ok('LACF frame with an unallocated model (V = 0x01) is dropped as unknown_model', H.decode(bytes(fr[:cs(fr)]) + bytes([0x01]) + fr[cs(fr)+1:]) == 'unknown_model')
ok('a 2.0.0 frame (V = 0x10 = model 16) is dropped', H.decode(bytes(fr[:cs(fr)]) + bytes([0x10]) + fr[cs(fr)+1:]) == 'unknown_model')
# destination byte c iiiiiii
d = H._parse(fr); dp = d['path_start'] - 1
ok('destination byte is 0x00 for APLRG1', fr[dp] == 0x00)
b = bytearray(fr); b[dp] |= 0x80
ok('destination bit c set is ignored by the receiver (CRC unchanged)', H.decode(bytes(b)) == PKT)
b = bytearray(fr); b[dp] = 126
ok('destination code 126 is dropped (reserved)', H.decode(bytes(b)) == 'reserved_dest_code')
b = bytearray(fr); b[dp] = 100
ok('destination code 100 (unassigned) is dropped', H.decode(bytes(b)) == 'dest_code_unknown')
other = H.encode(b'N0CALL-9>APXYZ9,WIDE1-1:>hi')
ok('an unlisted destination uses the escape 0x7F', other is not None and other[H._parse(other)['path_start'] - 6] == 0x7F and H.decode(other).startswith(b'N0CALL-9>APXYZ9'))
# capability bytes
cap = H.pack_capability(5, 0, 7, 1, 0, 10, 2)
fc = H.encode(PKT, cap=cap)
ok('capability frame: V bit 7 set, +3 bytes, decodes', fc[cs(fc)] == 0x80 and len(fc) == len(fr) + 3 and H.decode(fc) == PKT)
ok('capability bytes round trip (2,000 random combinations, 24 bits)', all(H.parse_capability(H.pack_capability(*v)) == dict(zip(('role','model','sf','bw','cr','power','preset'), v)) for v in [tuple(__import__('random').Random(i).randrange(1 << bits) for _, bits in H._CAP_FIELDS) for i in range(2000)]))
b = bytearray(fc); b[cs(fc) + 2] ^= 0x04
ok('a damaged capability byte fails the content CRC', H.decode(bytes(b)) == 'crc')
ok('V bit c set with fewer than 3 bytes after V is dropped', H.decode(fc[:cs(fc) + 3]) != PKT)
fcr = H.encode(RAW, cap=cap)
ok('capability bytes on a plain frame', fcr is not None and H.decode(fcr) == RAW and fcr[cs(fcr)] == 0x80)
rl = H.relay(fc)
ok('a relay copies the capability bytes unchanged', rl is not None and rl[cs(rl):cs(rl) + 4] == fc[cs(fc):cs(fc) + 4] and H.decode(rl) is not None and isinstance(H.decode(rl), bytes))
# RXT unknown values
def with_tuple(frame, tup):
    d = H._parse(frame); f = bytes(frame)
    hdr = (f[3] & 0x07) | 8 | (len(d['path']) << 4)
    return f[:3] + bytes([hdr]) + f[4:d['content_end']] + bytes(tup) + bytes([0x81])
rl1 = H.relay(raw)
t93 = H.relay(raw, 'N0RLY-1', bytes([93, 93, 45, 10]))
ok('a tuple with 93 (not measured) in two fields is accepted', isinstance(H.decode(t93), bytes) and H._parse(t93)['tuples'][0][1] == bytes([93, 93, 45, 10]))
t91 = H.relay(raw, 'N0RLY-1', bytes([91, 120, 45, 10]))
ok('tuple values 90 to 127 are unknown and the frame is kept', isinstance(H.decode(t91), bytes))
t80 = H.relay(raw, 'N0RLY-1', bytes([0x80, 20, 45, 10]))
ok('a tuple byte of 0x80 or more is dropped', t80 is not None and H.decode(t80) == 'rxt_out_of_range')
# pass-through relay
pk = [l.rstrip(b'\n') for l in open(os.path.join(ROOT, 'build', 'hb_packets.txt'), 'rb') if l.strip()]
n = same = down = dmg = 0
for p in pk:
    f = H.encode(p, cap=cap if len(p) % 2 else None)
    if not f: continue
    a = H.relay(f); b2 = H.relay_passthrough(f)
    if a is None: continue
    n += 1; same += a == b2; down += H.decode(b2) == H.decode(a)
    c = bytearray(b2); k = H._parse(b2)['cstart'] + 2; c[k] ^= 0x10
    dmg += not isinstance(H.decode(bytes(c)), bytes)
ok('pass-through relay is byte for byte the relay of a station that holds the model (%d frames, some with capability bytes)' % n, n > 1000 and same == n)
ok('downstream decode of the pass-through frame is the same packet', down == n)
ok('damage after the pass-through relay is caught by the CRC', dmg == n)
# a frame whose model the relay does not hold: forwarded by pass-through, dropped by a normal relay, decoded downstream by a station that holds it
H.KNOWN_MODELS = (0, 1); H._models[1] = H._models[0]
f1 = H.encode(PKT, version=1)
H.KNOWN_MODELS = (0,)
ok('a model-1 frame: the old receiver drops it, the normal relay drops it, the pass-through relay forwards it', H.decode(f1) == 'unknown_model' and H.relay(f1) is None and H.relay_passthrough(f1) is not None)
H.KNOWN_MODELS = (0, 1)
ok('...and a station that holds model 1 decodes the relayed frame', H.decode(H.relay_passthrough(f1)) is not None and isinstance(H.decode(H.relay_passthrough(f1)), bytes))
H.KNOWN_MODELS = (0,)
for n_, c, note in res: print('PASS' if c else 'FAIL', n_, note)
print('%d/%d pass' % (sum(c for _, c, _ in res), len(res)))
sys.exit(0 if all(c for _, c, _ in res) else 1)
