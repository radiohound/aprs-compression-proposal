#!/usr/bin/env python3
"""Relay-without-the-model vectors (2.1.0-dev.1): <received frame hex> <pass-through relayed frame hex, or -> <relay that decodes, hex or ->.   python3 tools/hb21/gen_passthrough.py < test/hb_packets.txt > test/vectors_hb21_passthrough_v1.txt
First hop only: relay N0RLY-1, tuple 50 20 45 10. Frames the relay that decodes cannot read (a model it does not hold, a destination code it does not have) give '-' in the third column; the pass-through relay forwards them
with the content field copied byte for byte. Model 1 is a stand-in: the same tables under a second model number (the relay does not read them)."""
import importlib.util, os, sys
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('hb', os.path.join(here, 'hb21_py', 'hb21.py')); H = importlib.util.module_from_spec(spec); spec.loader.exec_module(H)
H.get_models(0); H.KNOWN_MODELS = (0, 1); H._models[1] = H._models[0]
frames = []
for i, ln in enumerate(l for l in sys.stdin.buffer.read().split(b'\n') if l and not l.startswith(b'#')):
    f1 = H.encode(ln, version=1)                       # a frame of model 1
    if f1 is not None: frames.append(f1)
    f0 = H.encode(ln)
    if f0 is None: continue
    if i % 5 == 0:                                      # an unassigned destination code
        d = H._parse(f0)
        if d['dest'] is None and f0[d['path_start'] - 1] < 126:
            b = bytearray(f0); b[d['path_start'] - 1] = 100; frames.append(bytes(b))
    if i % 4 == 0: frames.append(H.encode(ln, cap=H.pack_capability(i % 8, 0, 7, 1, 0, 10, 2)))      # a frame with capability bytes, model 0
H.KNOWN_MODELS = (0,)
out = ['# Relay-without-the-model vectors (proposal 2.1.0-dev.1, a line with - - is a damaged frame that no relay may forward): <received frame hex> <pass-through relayed frame hex, or -> <relay that decodes, hex or ->. Relay N0RLY-1, tuple 50 20 45 10, first hop.']
for k, f in enumerate(frames):
    if f is None: continue
    p = H.relay_passthrough(f); n = H.relay(f)
    if p is None: continue                              # no WIDE1-1 or WIDE2-n to use: not a relay case
    out.append('%s %s %s' % (f.hex(), p.hex(), n.hex() if n is not None else '-'))
    if k % 3 == 0:                                      # the same frame damaged in the content: the CRC is over the bytes as sent, so the relay without the model drops it too
        d = H._parse(f); b = bytearray(f); b[d['content_end'] - 1] ^= 0x04
        assert H.relay_passthrough(bytes(b)) is None and H.relay(bytes(b)) is None
        out.append('%s - -' % bytes(b).hex())
sys.stdout.write('\n'.join(out) + '\n')
