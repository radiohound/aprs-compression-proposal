"""Independent Mic-E decoder (APRS101 chapter 10), used to cross-check hybrid_layouts.c (MICE2NATIVE).

  mice_check.py TEST.tsv            counts Mic-E packets by outcome (ok / ambiguous / invalid / mixed) and prints the message status values
  mice_check.py TEST.tsv dump       one line per decoded packet: status lat_idx lon_idx speed course   (compare with MICEDUMP=1 ./hl ... 2>dump.txt)

TEST.tsv is prep.py's output. status = 3 message bits, plus 8 when the message is one of the 7 custom ones; lat_idx and lon_idx are hundredths of a minute from -90 and -180."""
import sys, re, collections as C
def dig(ch):
    if '0'<=ch<='9': return ord(ch)-48, 0, 0          # digit, custom-bit, standard-bit
    if 'A'<=ch<='J': return ord(ch)-65, 1, 0
    if 'P'<=ch<='Y': return ord(ch)-80, 0, 1
    return None                                        # K, L, Z: a space (position ambiguity)
def decode(dst, info):
    if not info or info[0] not in "`'\x1c\x1d": return 'notmice'
    if len(dst)>6:                                      # -SSID after the 6 latitude characters is the path code, not part of the position
        if dst[6]!='-' or len(dst)>9 or not dst[7:].isdigit(): return 'invalid'
        dst=dst[:6]
    if len(dst)!=6 or len(info)<9: return 'invalid'
    if not re.fullmatch(r"[0-9A-LP-Z]{6}", dst): return 'invalid'
    d=[dig(c) for c in dst]
    if any(x is None for x in d): return 'ambiguous'
    cust=[d[i][1] for i in range(3)]; std=[d[i][2] for i in range(3)]
    if any(cust) and any(std): return 'mixed'
    if dst[3]=='K': return 'invalid'
    bits=(cust[0]|std[0])<<2 | (cust[1]|std[1])<<1 | (cust[2]|std[2])
    custom=1 if any(cust) else 0
    north=dst[3]>='P'; off=dst[4]>='P'; west=dst[5]>='P'
    dv=[x[0] for x in d]
    if dst[3] in 'ABCDEFGHIJ' or dst[4] in 'ABCDEFGHIJ' or dst[5] in 'ABCDEFGHIJ': return 'invalid'
    lat_h=(dv[0]*10+dv[1])*6000+(dv[2]*10+dv[3])*100+dv[4]*10+dv[5]
    if dv[2]*10+dv[3]>=60 or lat_h>90*6000: return 'invalid'
    b=[ord(c) for c in info[:9]]
    if any(not 28<=b[i]<=127 for i in range(1,7)) or not 33<=b[7]<=126: return 'invalid'
    lo=b[1]-28
    if off: lo+=100
    if 190<=lo<=199: lo-=190
    elif 180<=lo<=189: lo-=80
    m=b[2]-28
    if m>=60: m-=60
    h=b[3]-28
    sp=b[4]-28; dc=b[5]-28; se=b[6]-28
    speed=sp*10+dc//10
    if speed>=800: speed-=800
    course=(dc%10)*100+se
    if course>=400: course-=400
    if lo>180 or m>59 or h>99 or course>360: return 'invalid'
    lat_idx=90*6000+(lat_h if north else -lat_h)
    lon_h=lo*6000+m*100+h
    lon_idx=(180*6000+(-lon_h if west else lon_h))%2160000
    return dict(status=bits+8*(custom if bits else 0), bits=bits, custom=custom, lat=lat_idx, lon=lon_idx, speed=speed, course=course, code=info[7], tbl=info[8], rest=info[9:])
if __name__=="__main__":
    tsv=sys.argv[1]; cnt=C.Counter(); st=C.Counter(); csn=0; n=0
    for l in open(tsv,errors='replace'):
        f=l.rstrip('\n').split('\t')
        if len(f)<5: continue
        m=re.match(r"^[^>]+>([^,:]+)[^:]*:(.*)$",f[4],re.S)
        if not m: continue
        r=decode(m[1],m[2])
        if r=='notmice': continue
        n+=1
        if isinstance(r,dict):
            cnt['ok']+=1; st[r['status']]+=1
            if r['speed'] or r['course']: csn+=1
            if len(sys.argv)>2: print(r['status'],r['lat'],r['lon'],r['speed'],r['course'])
        else: cnt[r]+=1
    if len(sys.argv)<=2:
        print(tsv.split('/')[-1],'Mic-E packets',n,dict(cnt),'| with speed or course',csn)
        names={0:'Emergency',7:'M0 Off Duty',6:'M1 En Route',5:'M2 In Service',4:'M3 Returning',3:'M4 Committed',2:'M5 Special',1:'M6 Priority'}
        print('  status values (bits; +8 = custom):',{('C' if k>=8 else 'M/E')+str(k%8):v for k,v in st.most_common()})
