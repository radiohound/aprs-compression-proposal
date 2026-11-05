#!/usr/bin/env python3
"""Second, independent implementation of spec 11 (Hybrid B native PPM frame), standard library only.

    python3 -I hb.py gen < packets.txt          '<frame hex or -> <packet hex>' per packet line
    python3 -I hb.py check vectors.txt          encode and decode against '<frame> <packet>' vectors
    python3 -I hb.py recv                       stdin frame hex lines -> accepted packet hex or '-'
    python3 -I hb.py relaycheck relay_vectors   '<received> <relayed> <relayed packet>' vectors
"""
import bisect
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
TABLE_DIR = os.path.join(ROOT, 'tools', 'hybrid', 'tables')
INFO_MODEL_PATH = os.path.join(TABLE_DIR, 'model_hybridB_info_native.c')
COMMENT_MODEL_PATH = os.path.join(TABLE_DIR, 'model_hybridB_comment.c')
DEST_LIST_PATH = os.path.join(TABLE_DIR, 'tocall_set1.txt')

# Versions. SPEC_VERSION is the semantic version of spec 11. MAJOR = the protocol version, the top five bits of V (WIRE_VERSION); MINOR = a backward-compatible
# addition; PATCH = wording only. The low three bits of V are the table version (tocall list and PPM tables together; table 0 is the first). Protocol 2: plain
# content carries a CRC-16 and the plain/LACF form is kind bit 1; protocol 0 and 1 were only ever in test builds and are dropped.
SPEC_VERSION = '2.0.0'
WIRE_VERSION = 2
TABLE_VERSION = 0
KNOWN_TABLES = (0,)
def make_v(proto=WIRE_VERSION, table=TABLE_VERSION): return (proto << 3) | table
RELAY_CALL = 'N0RLY-1'
RELAY_TUPLE = bytes([50, 20, 45, 10])  # decimal: the spec writes "50 20 45 10"
TUPLE_MAX = (89, 88, 89, 89)


class Drop(Exception):
    """A frame is not acceptable; args[0] is the error name."""


# ---------------------------------------------------------------- CRC

def crc16_x25(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF


# ---------------------------------------------------------------- model

class Model:
    """Order-0..3 PPM table parsed as data out of a model .c file."""

    def __init__(self, path):
        with open(path, encoding='latin-1') as fh:
            txt = fh.read()

        def arr(name):
            m = re.search(r'\b' + name + r'\[\]\s*=\s*\{(.*?)\}\s*;', txt, re.S)
            if not m:
                raise ValueError('%s: array %s not found' % (path, name))
            return [int(x) for x in re.findall(r'\d+', m.group(1))]
        self.symbols = arr('aprs_ppm_symbols')
        self.counts = arr('aprs_ppm_counts')
        self.ctx = []
        for k in range(4):
            keys = arr('KEYS%d' % k)
            offs = arr('OFFS%d' % k)
            n = len(keys) // 3
            assert len(offs) == n + 1, (path, k)
            d = {}
            for j in range(n):
                key = (keys[3 * j] << 16) | (keys[3 * j + 1] << 8) | keys[3 * j + 2]
                d[key] = (offs[j], offs[j + 1])
            self.ctx.append(d)
        assert len(self.symbols) == len(self.counts)
        self._cache = {}

    def cum(self, hist):
        """Cumulative frequency list (257 entries) for the given history (last <=3 bytes)."""
        c = self._cache.get(hist)
        if c is not None:
            return c
        k = len(hist)
        rem = 0xFFFFFFFF
        pr = [0] * 256
        for kk in range(k, -1, -1):
            if rem <= 0:
                break
            key = int.from_bytes(hist[k - kk:], 'big') if kk else 0
            e = self.ctx[kk].get(key)
            if e is None:
                continue
            s, t = e
            cnts = self.counts[s:t]
            denom = sum(cnts) + (t - s)
            for b, cn in zip(self.symbols[s:t], cnts):
                pr[b] += rem * cn // denom
            rem = rem * (t - s) // denom
        share = rem >> 8
        c = [0] * 257
        acc = 0
        for s in range(256):
            acc += 1 + ((pr[s] + share) * (65536 - 256)) // (1 << 32)
            c[s + 1] = acc
        self._cache[hist] = c
        return c


def _carry(out):
    i = len(out) - 1
    while i >= 0:
        if out[i] == 255:
            out[i] = 0
            i -= 1
        else:
            out[i] += 1
            return
    # carry out of the front cannot happen with a valid coder state


def ppm_encode(model, text):
    low, rng = 0, 0xFFFFFFFF
    out = bytearray()
    for i, b in enumerate(text):
        c = model.cum(bytes(text[max(0, i - 3):i]))
        total = c[256]
        r = rng // total
        low += r * c[b]
        rng = r * (c[b + 1] - c[b])
        while rng < (1 << 24):
            if low >= (1 << 32):
                _carry(out)
                low -= 1 << 32
            out.append(low >> 24)
            low = (low << 8) & 0xFFFFFFFF
            rng <<= 8
    if low >= (1 << 32):
        _carry(out)
        low -= 1 << 32
    hi = low + rng - 1
    for nb in (1, 2, 3, 4):
        blk = 1 << (8 * (4 - nb))
        v = -(-low // blk) * blk
        if v + blk - 1 <= hi:
            break
    if v >= (1 << 32):
        _carry(out)
        v -= 1 << 32
    for i in range(nb):
        out.append((v >> (24 - 8 * i)) & 0xFF)
    return bytes(out)


def ppm_decode(model, payload, length):
    p = bytes(payload) + b'\0' * 4
    code = int.from_bytes(p[:4], 'big')
    pos = 4
    rng = 0xFFFFFFFF
    text = bytearray()
    for _ in range(length):
        c = model.cum(bytes(text[-3:]))
        total = c[256]
        r = rng // total
        v = min(code // r, total - 1)
        s = bisect.bisect_right(c, v) - 1
        code -= r * c[s]
        rng = r * (c[s + 1] - c[s])
        text.append(s)
        while rng < (1 << 24):
            nxt = payload[pos] if pos < len(payload) else 0
            code = ((code << 8) | nxt) & 0xFFFFFFFF
            pos += 1
            rng <<= 8
    return bytes(text)


# ---------------------------------------------------------------- tables

_models = {}
_destlist = {}


def get_models(version):
    if version not in KNOWN_TABLES:
        raise Drop('unknown_version')
    if version not in _models:
        _models[version] = (Model(INFO_MODEL_PATH), Model(COMMENT_MODEL_PATH))
    return _models[version]


def get_destlist(version):
    if version not in KNOWN_TABLES:
        raise Drop('unknown_version')
    if version not in _destlist:
        d = {}
        with open(DEST_LIST_PATH, encoding='latin-1') as fh:
            for line in fh:
                m = re.match(r'(\d+)\s+(\S+)\s*$', line)
                if m:
                    d[int(m.group(1))] = m.group(2)
        _destlist[version] = d
    return _destlist[version]


# ---------------------------------------------------------------- address elements

B40 = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'


def enc_elem(t):
    """TNC2 element text -> draft-0 address element bytes, or None if the element cannot be coded."""
    star = t.endswith('*')
    core = t[:-1] if star else t
    m = re.fullmatch(r'WIDE([1-7])(?:-([1-9]))?', core)
    if m:
        n = int(m.group(1))
        N = int(m.group(2)) if m.group(2) else 0
        if N <= n:
            return bytes([(0x40 if star else 0) | (n << 3) | N])
    m = re.fullmatch(r'([A-Z0-9]{1,6})(?:-([1-9][0-9]*))?', core)
    if m and (not m.group(2) or int(m.group(2)) <= 255):
        ssid = int(m.group(2)) if m.group(2) else 0
        v = 0
        for ch in m.group(1).ljust(6):
            v = v * 40 + B40.index(ch)
        head = 0x80 | (0x20 if star else 0) | (ssid if ssid <= 30 else 31)
        out = bytes([head]) + v.to_bytes(4, 'big')
        if ssid > 30:
            out += bytes([ssid])
        return out
    try:
        b = t.encode('latin-1')
    except UnicodeEncodeError:
        return None
    if 1 <= len(b) <= 31:
        return bytes([0xC0 | len(b)]) + b
    return None


def dec_elem(buf, pos):
    """-> (text, new_pos). Raises Drop."""
    if pos >= len(buf):
        raise Drop('truncated')
    h = buf[pos]
    if h < 0x80:
        n, N = (h >> 3) & 7, h & 7
        if n == 0 or N > n:
            raise Drop('bad_wide')
        return 'WIDE%d%s%s' % (n, '-%d' % N if N else '', '*' if h & 0x40 else ''), pos + 1
    if h < 0xC0:
        if pos + 5 > len(buf):
            raise Drop('truncated')
        v = int.from_bytes(buf[pos + 1:pos + 5], 'big')
        if v >= 40 ** 6:
            raise Drop('bad_callsign')
        chars = []
        for _ in range(6):
            v, d = divmod(v, 40)
            if d > 36:
                raise Drop('bad_callsign')
            chars.append(B40[d])
        call = ''.join(reversed(chars)).rstrip(' ')
        if not call or ' ' in call:
            raise Drop('bad_callsign')
        ssid = h & 31
        npos = pos + 5
        if ssid == 31:
            if npos >= len(buf):
                raise Drop('truncated')
            ssid = buf[npos]
            npos += 1
            if ssid < 31:
                raise Drop('bad_callsign')
        return call + ('-%d' % ssid if ssid else '') + ('*' if h & 0x20 else ''), npos
    ln = h & 31
    if ln == 0 or pos + 1 + ln > len(buf):
        raise Drop('truncated' if ln else 'bad_text_element')
    raw = buf[pos + 1:pos + 1 + ln]
    if b'\x00' in raw or any(c in raw for c in b':,>'):          # strict: a NUL, or a character that would split the TNC2 packet in the wrong place
        raise Drop('bad_text_element')
    return raw.decode('latin-1'), pos + 1 + ln


# ---------------------------------------------------------------- position block

def _b91(s):
    v = 0
    for ch in s:
        if not 33 <= ch <= 123:
            return None
        v = v * 91 + ch - 33
    return v


def _b91s(v):
    s = ''
    for _ in range(4):
        s = chr(33 + v % 91) + s
        v //= 91
    return s.encode('latin-1')


def build_pos(info):
    """information field -> (position block bytes, remaining comment) or None (no position)."""
    if not info or info[0:1] not in (b'!', b'=', b'/', b'@'):
        return None
    d = b'!=/@'.index(info[0:1])
    flags = d
    block = bytearray()
    i = 1
    ts = b''
    if d >= 2:
        t, z = info[1:7], info[7:8]
        if len(t) != 6 or not t.isdigit() or z not in (b'z', b'h', b'/'):
            return None
        flags |= 4
        ts = (int(t) * 4 + b'zh/'.index(z)).to_bytes(3, 'big')
        i = 8
    body = info[i:]
    try:
        if body[:1].isdigit():
            m = re.fullmatch(rb'(\d\d)(\d\d)\.(\d\d)([NS])(.)(\d\d\d)(\d\d)\.(\d\d)([EW])(.)', body[:19], re.S)
            if not m:
                return None
            lat = (int(m.group(1)) * 6000 + int(m.group(2)) * 100 + int(m.group(3))) * 2 + (m.group(4) == b'S')
            lon = (int(m.group(6)) * 6000 + int(m.group(7)) * 100 + int(m.group(8))) * 2 + (m.group(9) == b'W')
            pos = ((lat << 22) + lon).to_bytes(6, 'big') + m.group(5) + m.group(10)
            rest = body[19:]
            if re.fullmatch(rb'\d{3}/\d{3}', rest[:7]):
                flags |= 0x10
                pos += (int(rest[:3]) * 1024 + int(rest[4:7])).to_bytes(3, 'big')
                rest = rest[7:]
        else:
            if len(body) < 13 or not re.fullmatch(rb'[/\\A-Za-j]', body[:1]):
                return None
            la, lo = _b91(body[1:5]), _b91(body[5:9])
            if la is None or lo is None:
                return None
            flags |= 8
            pos = ((la << 27) + lo).to_bytes(7, 'big') + body[0:1] + body[9:10]
            cst = body[10:13]
            if cst[:2] == b'  ':
                flags |= 0x40
                pos += cst[2:3]
            else:
                pos += cst
            rest = body[13:]
        alt = b''
        m = re.search(rb'/A=(-\d{5}|\d{6})', rest[:255])
        if m:
            flags |= 0x20
            alt = bytes([m.start()]) + (int(m.group(1)) & 0xFFFFFF).to_bytes(3, 'big')
            rest = rest[:m.start()] + rest[m.end():]
        return bytes([flags]) + ts + pos + alt, rest
    except OverflowError:
        return None


def parse_pos(buf, p):
    """Position block at buf[p:] -> (prefix bytes, alt_offset or None, alt_text, new_p). Raises Drop."""
    try:
        flags = buf[p]
        p += 1
        if flags & 0x80:
            raise Drop('bad_flags')
        comp, cs, alt, cscomp = flags & 8, flags & 0x10, flags & 0x20, flags & 0x40
        if (cs and comp) or (cscomp and not comp):
            raise Drop('bad_flags')
        out = b'!=/@'[flags & 3:(flags & 3) + 1]
        if flags & 4:
            v = int.from_bytes(buf[p:p + 3], 'big')
            p += 3
            if v & 3 == 3 or v >> 2 > 999999 or len(buf) < p or flags & 3 < 2:
                raise Drop('bad_timestamp')
            out += b'%06d' % (v >> 2) + b'zh/'[v & 3:(v & 3) + 1]
        if comp:
            if p + 9 > len(buf):
                raise Drop('truncated')
            v = int.from_bytes(buf[p:p + 7], 'big')
            la, lo = v >> 27, v & ((1 << 27) - 1)
            if la >= 91 ** 4 or lo >= 91 ** 4:
                raise Drop('bad_position')
            out += buf[p + 7:p + 8] + _b91s(la) + _b91s(lo) + buf[p + 8:p + 9]
            p += 9
            if cscomp:
                out += b'  ' + buf[p:p + 1]
                p += 1
            else:
                out += buf[p:p + 3]
                p += 3
        else:
            if p + 8 > len(buf):
                raise Drop('truncated')
            v = int.from_bytes(buf[p:p + 6], 'big')
            lat, lon = v >> 22, v & ((1 << 22) - 1)
            parts = []
            for val, wid, hem in ((lat, 2, b'NS'), (lon, 3, b'EW')):
                h, s = val >> 1, val & 1
                dd, r = divmod(h, 6000)
                if h > (540000 if wid == 2 else 1080000):
                    raise Drop('bad_position')
                parts.append(b'%0*d%02d.%02d%s' % (wid, dd, r // 100, r % 100, hem[s:s + 1]))
            out += parts[0] + buf[p + 6:p + 7] + parts[1] + buf[p + 7:p + 8]
            p += 8
            if cs:
                v = int.from_bytes(buf[p:p + 3], 'big')
                p += 3
                if v >> 10 > 999 or (v & 1023) > 999:
                    raise Drop('bad_course')
                out += b'%03d/%03d' % (v >> 10, v & 1023)
        off, atext = None, b''
        if alt:
            off = buf[p]
            v = int.from_bytes(buf[p + 1:p + 4], 'big')
            p += 4
            if v & 0x800000:
                v -= 1 << 24
            if not -99999 <= v <= 999999:
                raise Drop('bad_altitude')
            atext = b'/A=%06d' % v if v >= 0 else b'/A=-%05d' % -v
        if p > len(buf):
            raise Drop('truncated')
        return out, off, atext, p
    except IndexError:
        raise Drop('truncated')


def rebuild_info(prefix, off, atext, comment):
    if off is None:
        return prefix + comment
    if off > len(comment):
        raise Drop('bad_altitude_offset')
    return prefix + comment[:off] + atext + comment[off:]


# ---------------------------------------------------------------- frame parse (receiver)

def _parse(frame):
    """Parse a frame down to its parts. Raises Drop. Returns a dict."""
    f = bytes(frame)
    if len(f) < 5 or f[:3] != b'\x3c\xff\x02':
        raise Drop('not_native')
    h = f[3]
    kind = h & 3
    npath = h >> 4
    rxt = bool(h & 8)
    if h & 4:
        raise Drop('reserved_bit')
    plain = kind < 2                        # kinds 0 and 1 carry plain content, kinds 2 and 3 LACF content; kinds 1 and 3 have a position block
    p = 4
    src, p = dec_elem(f, p)
    dest_codeless = None
    if p >= len(f):
        raise Drop('truncated')
    code = f[p] & 63
    p += 1
    if code == 62:
        raise Drop('reserved_dest_code')
    if code == 63:
        dest, p = dec_elem(f, p)
    else:
        dest_codeless = code
        dest = None
    path = []
    path_start = p
    for _ in range(npath):
        t, p = dec_elem(f, p)
        if not wide_ok(t):
            raise Drop('wide_not_compliant')
        path.append(t)
    path_end = p
    d = dict(frame=f, header=h, kind=kind, plain=plain, src=src, path=path, path_start=path_start,
             path_end=path_end, rxt=rxt, dest=dest, dest_code=dest_codeless,
             front_end=path_start)
    # destination bytes (between source and path) are copied for relays
    # position block (kinds 1 and 3)
    pfx = off = None
    atext = b''
    cstart = p
    if kind in (1, 3):
        pfx, off, atext, p = parse_pos(f, p)
        cstart = p
    d.update(pos_start=path_end, pos_end=p, pfx=pfx, alt_off=off, alt_text=atext)
    # RXT block
    end = len(f)
    tuples = []
    mask = 0
    if rxt:
        if end - p < 1:
            raise Drop('truncated')
        mask = f[-1]
        if not mask & 0x80:
            raise Drop('rxt_no_marker')
        bits = [j for j in range(7) if mask >> j & 1]
        k = len(bits)
        if k < 1:
            raise Drop('rxt_empty')
        if bits[-1] >= npath:
            raise Drop('rxt_bit_beyond_path')
        # "at least 4k+1 bytes remain after the position block and after V (plain: V and the CRC, 3 bytes; LACF: V, L and the CRC, 4 bytes)"
        if cstart >= len(f):
            raise Drop('truncated')
        minstart = cstart + (3 if plain else 4)
        if end - minstart < 4 * k + 1:
            raise Drop('rxt_truncated')
        blk = f[end - 4 * k - 1:end - 1]
        for i in range(k):
            for fld in range(4):
                if blk[4 * i + fld] > TUPLE_MAX[fld]:
                    raise Drop('rxt_out_of_range')
        tuples = [(bits[i], blk[4 * i:4 * i + 4]) for i in range(k)]
        end = end - 4 * k - 1
    d.update(tuples=tuples, mask=mask, content_end=end, cstart=cstart)
    return d


def decode(frame):
    """Receiver of spec 11. Returns the TNC2 packet (bytes) or an error name (str)."""
    try:
        r = _decode(frame)
        if b'\x00' in r:
            return 'nul'
        return r
    except Drop as e:
        return e.args[0]
    except IndexError:
        return 'truncated'


def _decode(frame):
    d = _parse(frame)
    f, kind = d['frame'], d['kind']
    p, end = d['cstart'], d['content_end']
    if p >= end:
        raise Drop('truncated')
    V = f[p]
    proto, ver = V >> 3, V & 7
    if proto != WIRE_VERSION:
        raise Drop('unknown_version')
    infom, comm = get_models(ver)
    dest = d['dest']
    if dest is None:
        dl = get_destlist(ver)
        if d['dest_code'] not in dl:
            raise Drop('dest_code_unknown')
        dest = dl[d['dest_code']]
    p += 1
    if d['plain']:
        if p + 2 > end:
            raise Drop('truncated')
        crc = f[p] | f[p + 1] << 8
        text = f[p + 2:end]
        if kind & 1:
            info = rebuild_info(d['pfx'], d['alt_off'], d['alt_text'], text)
        else:
            info = text
        if crc16_x25(crc_input(d['header'], V, d['src'], dest, info)) != crc:
            raise Drop('crc')
        return _tnc2(d['src'], dest, d['path'], info)
    if p + 3 > end:
        raise Drop('truncated')
    L = f[p]
    crc = f[p + 1] | f[p + 2] << 8
    payload = f[p + 3:end]
    if L == 0:
        raise Drop('zero_length')
    model = infom if kind == 2 else comm
    text = ppm_decode(model, payload, L)
    if kind == 3:
        info = rebuild_info(d['pfx'], d['alt_off'], d['alt_text'], text)
    else:
        info = text
    if crc16_x25(crc_input(d['header'], V, d['src'], dest, info)) != crc:
        raise Drop('crc')
    if ppm_encode(model, text) != payload:
        raise Drop('not_canonical')
    if not 4 + len(payload) < 3 + L:
        raise Drop('should_be_raw')
    return _tnc2(d['src'], dest, d['path'], info)


def crc_input(header, V, src, dest, info):
    return (bytes([header & 7, V]) + src.encode('latin-1') + b'>' + dest.encode('latin-1')
            + b':' + info)


def _tnc2(src, dest, path, info):
    return (src + '>' + ','.join([dest] + path)).encode('latin-1') + b':' + info


# ---------------------------------------------------------------- encoder

def split_tnc2(pkt):
    s = bytes(pkt)
    gt = s.find(b'>')
    if gt < 1:
        return None
    co = s.find(b':', gt + 1)
    if co < 0:
        return None
    head = s[gt + 1:co].decode('latin-1')
    if b'>' in s[:gt] or ':' in s[:gt].decode('latin-1'):
        return None
    elems = head.split(',')
    return s[:gt].decode('latin-1'), elems[0], elems[1:], s[co + 1:]


_WIDE_SHAPE = re.compile(r'^WIDE(\d+)(?:-(\d+))?\*?$')


def wide_ok(t):
    """A path element shaped like a WIDE alias must follow the APRS rule (n 1 to 7, N 1 to n). One that does not is not carried and not repeated on RF."""
    m = _WIDE_SHAPE.match(t)
    if not m:
        return True
    n, N = m.group(1), m.group(2)
    if len(n) != 1 or not 1 <= int(n) <= 7:
        return False
    return N is None or (len(N) == 1 and 1 <= int(N) <= int(n))


def path_ok(tnc2):
    """False if the packet's path holds a non-compliant WIDEn-N; the caller must then drop the packet, not send it in any other form."""
    sp = split_tnc2(bytes(tnc2))
    return sp is None or all(wide_ok(t) for t in sp[2])


def encode(tnc2, version=TABLE_VERSION):
    try:
        fr = _encode(bytes(tnc2), version)
    except (Drop, OverflowError):
        return None
    if fr is None or len(fr) > 255:
        return None
    if decode(fr) != bytes(tnc2):
        return None
    return fr


def _encode(pkt, version):
    sp = split_tnc2(pkt)
    if sp is None:
        return None
    src, dest, path, info = sp
    if len(path) > 15 or len(info) > 255:
        return None
    se = enc_elem(src)
    if se is None or not src:
        return None
    pe = []
    for t in path:
        if not wide_ok(t):
            return None
        e = enc_elem(t)
        if e is None:
            return None
        pe.append(e)
    dl = get_destlist(version)
    code = None
    for c, name in dl.items():
        if name == dest:
            code = c
            break
    if code is not None and c < 62:
        de = bytes([code])
    else:
        e = enc_elem(dest)
        if e is None:
            return None
        de = b'\x3f' + e
    infom, comm = get_models(version)
    bp = build_pos(info)
    kind, block, text, model = 2, b'', info, infom
    if bp is not None:
        pfx, off, atext, _ = parse_pos(bp[0], 0)
        if rebuild_info(pfx, off, atext, bp[1]) == info:
            kind, block, text, model = 3, bp[0], bp[1], comm
    L = len(text)
    if L > 255:
        return None
    ppm = None
    if L:
        payload = ppm_encode(model, text)
        if 4 + len(payload) < 3 + L:                 # plain content is V, CRC-16 and the text: 3 + L bytes
            ppm = payload
    V = make_v(WIRE_VERSION, version)
    if ppm is not None:                              # LACF content: kind 2 (whole information field) or 3 (position block + comment)
        header = (len(path) << 4) | kind
        crc = crc16_x25(crc_input(header, V, src, dest, info))
        content = bytes([V, L, crc & 255, crc >> 8]) + ppm
    else:                                            # plain content: kind 0 or 1 (the same two with the LACF flag clear)
        header = (len(path) << 4) | (kind - 2)
        crc = crc16_x25(crc_input(header, V, src, dest, info))
        content = bytes([V, crc & 255, crc >> 8]) + text
    return b'\x3c\xff\x02' + bytes([header]) + se + de + b''.join(pe) + block + content


# ---------------------------------------------------------------- relay

def relay(frame, relay_call=RELAY_CALL, tuple_bytes=RELAY_TUPLE):
    """Relay a received frame (spec 11 Relay and Conformance). Returns the new frame or None."""
    try:
        d = _parse(frame)
        if not isinstance(_decode_ok(frame), bytes):
            return None
    except Drop:
        return None
    f = d['frame']
    path = d['path']
    j = next((i for i, t in enumerate(path) if not t.endswith('*')), None)
    if j is None or path[j] not in ('WIDE1-1', 'WIDE2-1', 'WIDE2-2'):
        return None
    if any(b >= j for b, _ in d['tuples']):                      # a tuple on the relay's own element or a later one: tuples belong to used elements, so no conforming relay made this frame
        return None
    me = enc_elem(relay_call + '*')
    if me is None or len(tuple_bytes) != 4:
        return None
    expand = path[j] == 'WIDE2-2'
    newpath = [enc_elem(t) for t in path]
    newpath[j:j + 1] = [me, enc_elem('WIDE2-1')] if expand else [me]
    if len(newpath) > 15:
        return None
    old = [(b + 1 if expand and b >= j else b, t) for b, t in d['tuples']]
    old = [(b, t) for b, t in old if b != j or expand]  # WIDE1-1 case: relay tuple replaces one at j
    if any(b > 6 for b, _ in old):
        return None
    front = f[4:d['path_start']]
    middle = f[d['path_end']:d['content_end']]

    def build(tup):
        if not tup:
            header = (d['header'] & 0x07) | (len(newpath) << 4)
        else:
            header = (d['header'] & 0x07) | 8 | (len(newpath) << 4)
        mask = 0x80
        for b, _ in tup:
            mask |= 1 << b
        return (b'\x3c\xff\x02' + bytes([header]) + front + b''.join(newpath) + middle
                + b''.join(t for _, t in tup) + (bytes([mask]) if tup else b''))
    tup = old[:]
    if j <= 6:
        tup = [(b, t) for b, t in old if b != j] + [(j, bytes(tuple_bytes))]
        tup.sort(key=lambda x: x[0])
    out = build(tup)
    if len(out) > 255 and j <= 6:                                  # too long with the relay's own tuple: forwarded without it, the earlier tuples stay
        out = build(old)
    return out if len(out) <= 255 else None


def relay_auto(frame):
    """The relay of the conformance vectors: callsign N0RLY-(k+1) and tuple 50+k 20+k 45+k 10+k, k = tuples already in the frame."""
    try:
        k = len(_parse(frame)['tuples'])
    except Drop:
        return None
    return relay(frame, 'N0RLY-%d' % (k + 1), bytes([50 + k, 20 + k, 45 + k, 10 + k]))


def _decode_ok(frame):
    r = decode(frame)
    return r


# ---------------------------------------------------------------- command line

def _read(path):
    with open(path, 'rb') as fh:
        return fh.read()


def _lines(data):
    for ln in data.split(b'\n'):
        ln = ln.rstrip(b'\r')
        if ln and not ln.startswith(b'#'):
            yield ln


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    out = sys.stdout
    if cmd == 'gen':
        for ln in _lines(sys.stdin.buffer.read()):
            fr = encode(ln)
            out.write('%s %s\n' % (fr.hex() if fr is not None else '-', ln.hex()))
        return 0
    if cmd == 'recv':
        for ln in sys.stdin.read().split('\n'):
            ln = ln.strip()
            if not ln or ln.startswith('#'):
                continue
            try:
                r = decode(bytes.fromhex(ln.split()[0]))
            except ValueError:
                r = 'bad_hex'
            out.write((r.hex() if isinstance(r, bytes) else '-') + '\n')
        return 0
    if cmd == 'check':
        n = e = dd = 0
        for ln in _lines(_read(argv[2])):
            fh, ph = ln.split()
            pkt = bytes.fromhex(ph.decode())
            want = None if fh == b'-' else bytes.fromhex(fh.decode())
            n += 1
            ok_e = encode(pkt) == want
            ok_d = want is None or decode(want) == pkt
            e += ok_e
            dd += ok_d
            if not (ok_e and ok_d):
                print('MISMATCH line %d: encode %s decode %s' % (n, ok_e, ok_d))
        print('encode %d/%d  decode %d/%d' % (e, n, dd, n))
        return 0 if e == n and dd == n else 1
    if cmd == 'relaycheck':
        n = r = dd = 0
        for ln in _lines(_read(argv[2])):
            a, b, c = ln.split()
            a, b, c = (bytes.fromhex(x.decode()) for x in (a, b, c))
            n += 1
            ok_r = relay_auto(a) == b
            ok_d = decode(b) == c
            r += ok_r
            dd += ok_d
            if not (ok_r and ok_d):
                print('MISMATCH line %d: relay %s decode %s' % (n, ok_r, ok_d))
        print('relay %d/%d  decode-relayed %d/%d' % (r, n, dd, n))
        return 0 if r == n and dd == n else 1
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
