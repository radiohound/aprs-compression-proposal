#!/usr/bin/env python3
"""Spec 11 rejection vectors: frames that no encoder can produce (an impossible RXT block, header, content coding or address element) which every receiver must DROP.   python3 tools/hb/gen_reject.py > test/vectors_hb_reject_v1.txt
Each line: <frame hex> <reason>. Built from the first second-hop frame of test/vectors_hb_relay_chain_v1.txt (two tuples, mask 0x83) and its received frame."""
import importlib.util, os, sys
here = os.path.dirname(os.path.abspath(__file__)); test = os.path.join(here, '..', '..', 'test')
rows = [l.split() for l in open(os.path.join(test, 'vectors_hb_relay_chain_v1.txt')) if l.strip() and l[0] != '#']
f = bytes.fromhex(rows[0][1])                    # relayed twice: ends with t1 t2 mask
assert f[-1] == 0x83 and f[3] & 8
body, t1, t2 = f[:-9], f[-9:-5], f[-5:-1]
hdr = f[3]
out = []
def add(frame, why): out.append('%s %s' % (frame.hex(), why))
add(body + t1 + t2 + bytes([0x03]), 'mask has no marker bit (bit 7 clear)')
add(body + t1 + t2 + bytes([0x83 | 0x40]), 'mask bit 6 set but the path has fewer elements')
add(body + t1 + t2 + bytes([0x80]), 'mask with no tuple bit (k = 0)')
add(body + t2 + bytes([0x83]), 'mask names 2 tuples but only one is present')
add(body + t1 + t2, 'tuples without a mask byte')
for i, (name, v) in enumerate((('RSSI', 90), ('SNR', 89), ('frequency offset', 90), ('time to hop', 90))):
    b = bytearray(t2); b[i] = v; add(body + t1 + bytes(b) + bytes([0x83]), '%s byte %d is above its maximum' % (name, v))
add(f[:3] + bytes([hdr & ~8]) + f[4:], 'RXT bit cleared but the block is still there')
o = bytes.fromhex(rows[0][0])                    # the frame before the second relay: one tuple; take its origin by dropping the block
o = o[:3] + bytes([o[3] & ~8]) + o[4:-5]
add(o[:3] + bytes([o[3] | 8]) + o[4:], 'RXT bit set but no block')
spec = importlib.util.spec_from_file_location('hb', os.path.join(here, 'hb_py', 'hb.py')); H = importlib.util.module_from_spec(spec); spec.loader.exec_module(H)
w = H.encode(b'N0CALL-9>APLRG1,WIDE1-1,WIDE2-2:!3722.40N/12158.93W>LoRa APRS iGate on the hill')     # the worked example's frame (35 bytes): see spec 11 "Worked example"
assert len(w) == 35 and w[3] == 0x23
def mut(frame, i, v): b = bytearray(frame); b[i] = v; return bytes(b)
add(mut(w, 3, w[3] | 4), 'header bit 2 set (reserved)')
add(mut(w, 3, w[3] & ~3), 'LACF content with the kind changed to 0 (plain): the payload is not text, the CRC fails')
add(mut(w, 3, (w[3] & ~3) | 1), 'LACF content with the kind changed to 1 (plain with a position block): the payload is not text, the CRC fails')
add(mut(w, 3, (w[3] & ~3) | 2), 'kind 2 on a frame with a position block: the position block is read as content, the CRC fails')
add(mut(w, 9, 62), 'destination code 62 (reserved)')
add(mut(w, 21, 0x18), 'protocol version 3 is not held')
add(mut(w, 21, 0x08), 'protocol version 1 (test builds before the plain CRC) is not held')
add(mut(w, 21, 0x01), 'protocol version 0 (the first test builds, V = 1) is not held')
add(mut(w, 21, 0x11), 'table version 1 is not held')
add(mut(w, 22, 0), 'PPM content with L = 0')
add(mut(w, 12, 0x80), 'position flags bit 7 set')
add(mut(w, 12, 0x18), 'position flags: course/speed together with compressed')
add(mut(w, 12, 0x40), 'position flags: compressed-cs bit without compressed')
add(mut(w, 10, 0x01), 'path element WIDE with n = 0')
add(mut(w, 10, 0x0A), 'path element WIDE with N above n')
add(w[:10] + H.enc_elem('WIDE1-2') + w[11:], 'path element WIDE1-2 (N above n) written as a callsign element: not compliant, dropped')
add(w[:10] + H.enc_elem('WIDE2-3*') + w[11:], 'path element WIDE2-3* (N above n) written as a callsign element: not compliant, dropped')
add(w[:10] + H.enc_elem('WIDE1-0') + w[11:], 'path element WIDE1-0 written as a text element: not compliant, dropped')
add(w[:4] + bytes([0xFF, 0xFF, 0xFF, 0xFF]) + w[8:], 'source element value above 40^6 (no callsign)')
add(w[:4] + bytes([0x9F, 0x59, 0x94, 0xA6, 0x2C, 0x05]) + w[9:], 'SSID extra byte below 31')
add(w[:4] + bytes([0x89, 0x00, 0x00, 0x00, 0x00]) + w[9:], 'callsign element of six spaces (empty)')
r = H.encode(b'N0CALL>APLRG1:>x')                     # plain content (kind 2, cc = 3)
def raw_crc(hdr, src_s, dest_s, info):                  # plain content as an encoder that skipped the NUL rule would send it: V, CRC-16 over the whole text, text
    crc = H.crc16_x25(H.crc_input(hdr, 0x10, src_s, dest_s, info)); return bytes([0x10, crc & 255, crc >> 8]) + info
add(b'\x3c\xff\x02\x00' + H.enc_elem('N0CALL') + b'\x00' + raw_crc(0, 'N0CALL', H.get_destlist(0)[0], b'hi\x00there'), 'NUL byte in plain text (valid CRC)')
add(r[:-1] + bytes([r[-1] ^ 1]), 'plain content with one text bit flipped (CRC fails)')
add(r[:-1], 'plain content cut by one byte (CRC fails)')
add(mut(r, len(r) - 3, r[-3] ^ 0xFF), 'plain content with a wrong CRC byte')
# an element that would split the TNC2 packet in the wrong place (':' ',' '>'), and a NUL in an element or in PPM text
src6 = H.enc_elem('N0CALL')
def te(b): return bytes([0xC0 | len(b)]) + b                          # a text element
def rawf(npath, src, dest, path=b'', info=b'hi', names=('N0CALL', 'APLRG1')):    # kind 2, plain content with a valid CRC over the packet an encoder that accepted this element would have meant
    return b'\x3c\xff\x02' + bytes([(npath << 4) | 0]) + src + dest + path + raw_crc((npath << 4) | 0, names[0], names[1], info)
add(rawf(0, te(b'A:B'), b'\x00', names=('A:B', H.get_destlist(0)[0])), "source text element holds ':' (the decoded packet would split at the wrong colon)")
add(rawf(0, src6, b'\x3f' + te(b'A,B'), names=('N0CALL', 'A,B')), "destination text element holds ','")
add(rawf(1, src6, b'\x00', te(b'A>B'), names=('N0CALL', H.get_destlist(0)[0])), "path text element holds '>'")
add(rawf(0, src6, b'\x3f' + te(b'A\x00B'), names=('N0CALL', 'A\x00B')), 'destination text element holds a NUL byte')
orig = H.split_tnc2
H.split_tnc2 = lambda pkt: ('A:B', 'APLRG1', [], pkt[pkt.index(b':', pkt.index(b'>')) + 1:])     # let the encoder accept the source it refuses
colon_frame = H._encode(b'A:B>APLRG1:hello world this is a test of the english text', 0)
H.split_tnc2 = orig
assert colon_frame is not None
add(colon_frame, "PPM frame whose source text element holds ':' (what an encoder that accepts such a source would send)")
infom, comm = H.get_models(0)
txt = b'AB\x00' + b'hello world this is a test of the english text'
pay = H.ppm_encode(infom, txt)
assert 4 + len(pay) < 3 + len(txt)
hdr, dst = 2, H.get_destlist(0)[0]
def ppm_frame(crc_text):
    crc = H.crc16_x25(H.crc_input(hdr, 0x10, 'N0CALL', dst, crc_text))
    return b'\x3c\xff\x02' + bytes([hdr]) + src6 + b'\x00' + bytes([0x10, len(txt), crc & 255, crc >> 8]) + pay
add(ppm_frame(txt), 'PPM text holds a NUL byte (CRC over the whole text)')
add(ppm_frame(b'AB'), 'PPM text holds a NUL byte, with the CRC taken over the text up to the NUL only')
sys.stdout.write('# Spec 11 rejection vectors: <frame hex> <why it must be dropped>. Every receiver must drop each line.\n' + '\n'.join(out) + '\n')
