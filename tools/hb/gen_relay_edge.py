#!/usr/bin/env python3
"""Spec 11 relay edge vectors: cases where the relay's behaviour is easy to get wrong.   python3 tools/hb/gen_relay_edge.py > test/vectors_hb_relay_edge_v1.txt
Each line: <received frame hex> <relayed frame hex, or - if the relay must NOT relay it> <relayed packet hex, or ->. The relay is N0RLY-(k+1) with tuple bytes 50+k 20+k 45+k 10+k,
k = the tuples already in the frame (the same rule as the other relay vectors). The relayed frames are those of the Python implementation, which copies the source and destination bytes verbatim."""
import importlib.util, os, sys
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('hb', os.path.join(here, 'hb_py', 'hb.py')); H = importlib.util.module_from_spec(spec); spec.loader.exec_module(H)
INFO = b'!3722.40N/12158.93W>LoRa APRS iGate on the hill'
out = []
def add(received, why):
    r = H.relay_auto(received)
    out.append((received, r, why))
def with_tuples(f, tuples, idxs):
    """Append an RXT block (tuples at the given path-element indices) to a frame that has none."""
    mask = 0x80
    for i in idxs: mask |= 1 << i
    return f[:3] + bytes([f[3] | 8]) + f[4:] + b''.join(tuples) + bytes([mask])
base = H.encode(b'N0CALL-9>APLRG1,WIDE1-1,WIDE2-2:' + INFO)
d = H._parse(base); dest_at = d['path_start'] - 1
# 1. the reserved ff bits of the destination byte describe the originating station: a relay copies them as received
f = bytearray(base); f[dest_at] |= 0xC0; add(bytes(f), 'ff bits set on the destination byte are copied')
# 2. a listed destination sent in escape form (code 63 and the element) stays in escape form
se_end = dest_at
name = H.get_destlist(0)[base[dest_at] & 0x3F]
add(base[:dest_at] + b'\x3f' + H.enc_elem(name) + base[dest_at + 1:], 'a listed destination in escape form stays in escape form')
# 3. seven tuples already in the frame and the first unused element is the eighth: it relays and adds no tuple of its own
seven = H.encode(b'N0CALL-9>APLRG1,A1*,A2*,A3*,A4*,A5*,A6*,A7*,WIDE1-1:' + INFO)
add(with_tuples(seven, [bytes([10 + i, 20 + i, 30 + i, 40 + i]) for i in range(7)], range(7)), 'seven tuples, relay at the eighth element: relayed without an own tuple')
# 4. a tuple on an element that is not used yet, or on the relay's own element: tuples belong to used elements, so such a frame is not relayed
two = H.encode(b'N0CALL-9>APLRG1,WIDE1-1,WIDE2-2:' + INFO)
add(with_tuples(two, [bytes([60, 30, 50, 20])], [1]), 'a tuple on the second, unused element: not relayed')
add(with_tuples(two, [bytes([60, 30, 50, 20])], [0]), "a tuple on the relay's own (first unused) element: not relayed")
three = H.encode(b'N0CALL-9>APLRG1,K6LY-3*,WIDE1-1,WIDE2-2:' + INFO)
add(with_tuples(three, [bytes([60, 30, 50, 20]), bytes([61, 31, 51, 21])], [0, 2]), 'a used element with a tuple and a later unused one with a tuple: not relayed')
# 5. a long packet: with the relay's own tuple the frame would be longer than 255 bytes, so it is relayed without it; one that is too long even then is not relayed
def packet_of_length(n, path=b'WIDE1-1'):
    for m in range(40, 260):
        f = H.encode(b'N0CALL-9>APLRG1,' + path + b':>' + bytes((33 + (i * 7) % 90) for i in range(m)))
        if f is not None and len(f) == n:
            return f
    raise SystemExit('no packet of %d bytes' % n)
add(packet_of_length(249), 'long packet: the relay\'s own tuple would make it longer than 255 bytes, relayed without it')
add(packet_of_length(253), 'long packet: too long even without the relay\'s own tuple: not relayed')
for recv, r, why in out:
    if r is not None:
        assert isinstance(H.decode(r), bytes), why
sys.stdout.write('# Spec 11 relay edge vectors: <received frame hex> <relayed frame hex, or - when the relay must not relay it> <relayed packet hex, or ->. Relay N0RLY-(k+1), tuple bytes 50+k 20+k 45+k 10+k.\n')
for recv, r, why in out:
    sys.stdout.write('%s %s %s\n' % (recv.hex(), r.hex() if r is not None else '-', H.decode(r).hex() if r is not None else '-'))
