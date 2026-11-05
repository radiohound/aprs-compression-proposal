#!/usr/bin/env python3
"""Capability-byte vectors (3.0.0-dev.1): <frame hex> <TNC2 packet hex> <capability bytes hex>.   python3 tools/hb3/gen_cap.py < test/hb_packets.txt > test/vectors_hb3_cap_v1.txt
Every packet the encoder accepts is sent with capability bytes that run through the roles, models and presets (a deterministic pattern); the frame is the Python reference's."""
import importlib.util, os, sys
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('hb', os.path.join(here, 'hb3_py', 'hb3.py')); H = importlib.util.module_from_spec(spec); spec.loader.exec_module(H)
out = ['# Capability-byte vectors (proposal 3.0.0-dev.1): <frame hex> <TNC2 packet hex> <capability bytes hex>. V bit 7 is set and the three bytes follow V; they are in the CRC input.']
i = 0
for ln in sys.stdin.buffer.read().split(b'\n'):
    if not ln or ln.startswith(b'#'): continue
    cap = H.pack_capability(i % 8, (i * 7) % 32, (i * 3) % 8, i % 4, (i * 5) % 4, (i * 11) % 64, [0, 1, 2, 3, 4, 5, 29, 30, 31][i % 9])
    fr = H.encode(ln, cap=cap)
    i += 1
    if fr is None: continue
    out.append('%s %s %s' % (fr.hex(), ln.hex(), cap.hex()))
sys.stdout.write('\n'.join(out) + '\n')
