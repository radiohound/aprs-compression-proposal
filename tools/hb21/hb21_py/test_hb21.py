"""Checks of the proposal 2.1.0-dev.1 (spec 12, frame type 3C FF 03) on the Python reference. Run from the repository root:  python3 tools/hb21/hb21_py/test_hb21.py"""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hb21 as H
ROOT = H.ROOT
res = []
def ok(name, cond, note=''): res.append((name, bool(cond), note))
PKT = b'N0CALL-9>APLRG1,WIDE1-1,WIDE2-2:!3722.40N/12158.93W>LoRa APRS iGate on the hill'
RAW = b'F4MLV-2>APLRG1,WIDE1-1:>ok'      # short enough that the encoder sends plain content
fr = H.encode(PKT); raw = H.encode(RAW)
cs = lambda f: H._parse(f)['cstart']
ok('frame type byte is 03 (the 2.0.0 type 02 frame is not read by this codec)', fr[:3] == b'\x3c\xff\x03' and H.decode(b'\x3c\xff\x02' + fr[3:]) == 'not_native')
ok('V of a normal frame is 0x00 (model 0, no capability)', fr[cs(fr)] == 0x00)
ok('example frame is 35 bytes and decodes', len(fr) == 35 and H.decode(fr) == PKT)
def fix_crc(frame, V=None):
    d = H._parse(frame); v, cap, L, crc, body = H._wire(d, frame); V = v if V is None else V
    c = H.crc_wire(d['header'], V, cap, frame[4:d['path_start']], frame[d['pos_start']:d['pos_end']], L, body)
    b = bytearray(frame); b[d['cstart']] = V; q = d['cstart'] + 1 + len(cap) + (1 if L is not None else 0); b[q] = c & 255; b[q + 1] = c >> 8
    return bytes(b)
ok('plain frame with model bits 0x1F is accepted (plain uses no model)', H.decode(fix_crc(raw, 0x1F)) == RAW)
ok('plain frame with spare bits ss = 3 is accepted (ignored)', H.decode(fix_crc(raw, 0x60)) == RAW)
ok('LACF frame with model 1 is dropped as unknown_model (correct CRC)', H.decode(fix_crc(fr, 0x01)) == 'unknown_model')
ok('...and with a wrong CRC the CRC fails first', H.decode(bytes(fr[:cs(fr)]) + bytes([0x01]) + fr[cs(fr) + 1:]) == 'crc')
d = H._parse(fr); dp = d['path_start'] - 1
ok('destination byte is 0x00 for APLRG1', fr[dp] == 0x00)
b = bytearray(fr); b[dp] |= 0x80
ok('destination bit c is covered by the CRC (changed in flight: dropped)', H.decode(bytes(b)) == 'crc')
ok('destination bit c set by the sender (CRC recomputed) is ignored', H.decode(fix_crc(bytes(b))) == PKT)
b = bytearray(fr); b[dp] = 126
ok('destination code 126 is dropped (reserved)', H.decode(bytes(b)) == 'reserved_dest_code')
ok('destination code 100 (unassigned) is dropped', H.decode(fix_crc(bytes(bytearray(fr[:dp]) + bytes([100]) + fr[dp + 1:]))) == 'dest_code_unknown')
other = H.encode(b'N0CALL-9>APXYZ9,WIDE1-1:>hi')
ok('an unlisted destination uses the escape 0x7F', other is not None and other[H._parse(other)['path_start'] - 6] == 0x7F and H.decode(other).startswith(b'N0CALL-9>APXYZ9'))
# capability bytes: four, power in dBm
cap = H.pack_capability(5, 0, 7, 1, 0, 22, 2)
fc = H.encode(PKT, cap=cap)
ok('capability frame: V bit 7 set, +4 bytes, decodes', fc[cs(fc)] == 0x80 and len(fc) == len(fr) + 4 and H.decode(fc) == PKT)
rng = random.Random(1)
vals = [tuple(rng.randrange(1 << bits) for _, bits in H._CAP_FIELDS[:-1]) for _ in range(5000)]
ok('capability bytes round trip (5,000 random combinations, 32 bits, spare bits 0)', all(H.parse_capability(H.pack_capability(*v)) == dict(zip(('role', 'model', 'sf', 'bw', 'cr', 'power', 'preset'), v), spare=0) for v in vals))
ok('power is the dBm itself: 10, 12, 20, 22, 30, 33, 37, 40 dBm are exact', all(H.parse_capability(H.pack_capability(0, 0, 0, 0, 0, p, 0))['power'] == p for p in (10, 12, 20, 22, 30, 33, 37, 40, 63)))
b = bytearray(fc); b[cs(fc) + 2] ^= 0x04
ok('a damaged capability byte fails the CRC', H.decode(bytes(b)) == 'crc')
ok('V bit c set with fewer than 4 bytes after V is dropped', H.decode(fc[:cs(fc) + 4]) != PKT)
fcr = H.encode(RAW, cap=cap)
ok('capability bytes on a plain frame', fcr is not None and H.decode(fcr) == RAW and fcr[cs(fcr)] == 0x80)
rl = H.relay(fc)
ok('a relay copies the capability bytes unchanged', rl is not None and rl[cs(rl):cs(rl) + 5] == fc[cs(fc):cs(fc) + 5] and isinstance(H.decode(rl), bytes))
# the CRC is over the bytes as sent
d = H._parse(fr)
for name, i in (('a position block byte', d['pos_start'] + 3), ('a payload byte', len(fr) - 3), ('L', d['cstart'] + 1), ('a source byte', 6)):
    b = bytearray(fr); b[i] ^= 0x01
    ok('damage to %s fails the CRC' % name, H.decode(bytes(b)) == 'crc')
b = bytearray(fr); b[d['path_start']] ^= 0x01
ok('the path is not covered (a relay changes it): damage there is not a CRC failure', H.decode(bytes(b)) != 'crc')
# RXT unknown values
rl1 = H.relay(raw)
t93 = H.relay(raw, 'N0RLY-1', bytes([93, 93, 45, 10]))
ok('a tuple with 93 (not measured) in two fields is accepted', isinstance(H.decode(t93), bytes) and H._parse(t93)['tuples'][0][1] == bytes([93, 93, 45, 10]))
ok('tuple values 90 to 127 are unknown and the frame is kept', isinstance(H.decode(H.relay(raw, 'N0RLY-1', bytes([91, 120, 45, 10]))), bytes))
t80 = H.relay(raw, 'N0RLY-1', bytes([0x80, 20, 45, 10]))
ok('a tuple byte of 0x80 or more is dropped', t80 is not None and H.decode(t80) == 'rxt_out_of_range')
# relay without the model: the CRC is checked, the content is not decoded
pk = [l.rstrip(b'\n') for l in open(os.path.join(ROOT, 'build', 'hb_packets.txt'), 'rb') if l.strip()]
n = same = down = dmg = 0
for p in pk:
    f = H.encode(p, cap=cap if len(p) % 2 else None)
    if not f: continue
    a = H.relay(f); b2 = H.relay_passthrough(f)
    if a is None: continue
    n += 1; same += a == b2; down += H.decode(b2) == H.decode(a)
    c = bytearray(f); c[H._parse(f)['content_end'] - 1] ^= 0x10
    dmg += H.relay_passthrough(bytes(c)) is None
ok('pass-through relay is byte for byte the relay of a station that holds the model (%d frames, some with capability bytes)' % n, n > 1000 and same == n)
ok('downstream decode of the pass-through frame is the same packet', down == n)
ok('a frame damaged BEFORE the pass-through relay is refused by the relay itself (it checks the CRC)', dmg == n)
H.KNOWN_MODELS = (0, 1); H._models[1] = H._models[0]
f1 = fix_crc(H.encode(PKT), 0x01)
H.KNOWN_MODELS = (0,)
ok('a model-1 frame: the old receiver drops it, the normal relay drops it, the pass-through relay forwards it', H.decode(f1) == 'unknown_model' and H.relay(f1) is None and H.relay_passthrough(f1) is not None)
cf = bytearray(f1); cf[len(cf) - 2] ^= 0x01
ok('...but not a damaged one', H.relay_passthrough(bytes(cf)) is None)
H.KNOWN_MODELS = (0, 1)
ok('...and a station that holds model 1 decodes the relayed frame', isinstance(H.decode(H.relay_passthrough(f1)), bytes))
H.KNOWN_MODELS = (0,)
# the vector files
def vec(name): return [l.split() for l in open(os.path.join(ROOT, 'test', name)) if l.strip() and not l.startswith('#')]
v = vec('vectors_hb21_cap_v1.txt')
ok('capability vectors (%d) agree' % len(v), all(H.encode(bytes.fromhex(p), cap=bytes.fromhex(c)) == bytes.fromhex(f) and H.decode(bytes.fromhex(f)) == bytes.fromhex(p) for f, p, c in v))
v = vec('vectors_hb21_passthrough_v1.txt')
def pt_ok(r, p, nn):
    r = bytes.fromhex(r); a = H.relay_passthrough(r); b = H.relay(r)
    return (a.hex() if a is not None else '-') == p and (b.hex() if b is not None else '-') == nn
ok('pass-through vectors (%d) agree' % len(v), all(pt_ok(*x) for x in v))
v = vec('vectors_hb21_reject_v1.txt')
ok('rejection vectors (%d) are all dropped' % len(v), all(not isinstance(H.decode(bytes.fromhex(x[0])), bytes) for x in v))
for n_, c, note in res: print('PASS' if c else 'FAIL', n_, note)
print('%d/%d pass' % (sum(c for _, c, _ in res), len(res)))
sys.exit(0 if all(c for _, c, _ in res) else 1)
