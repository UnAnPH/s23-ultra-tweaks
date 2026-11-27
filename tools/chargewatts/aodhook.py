"""chargewatts, AOD: the Always On Display's charging info has a second line, the time to full ("45 m until full").
Pass every value its builder returns through s23tweaks.S23Watts.add (smali, in place), so it reads
"45 m until full • 10.7 W (25 W charger)". The builder is found by its log tag, not its obfuscated name:
a method (Context) -> String in ChargingInfoManager that logs "getDeviceRemainingTime: ".
usage: aodhook.py <apktool dir>"""
import os, re, sys
SIG = re.compile(r'^\.method public final \w+\(Landroid/content/Context;\)Ljava/lang/String;$')
CALL = ['    invoke-static/range {%s .. %s}, Ls23tweaks/S23Watts;->add(Ljava/lang/CharSequence;)Ljava/lang/CharSequence;', '',
        '    move-result-object %s', '', '    check-cast %s, Ljava/lang/String;', '']
hits = []
for root, _, files in os.walk(sys.argv[1]):
    for f in files:
        if not f.endswith('.smali'): continue
        p = os.path.join(root, f); src = open(p).read()
        if 'getDeviceRemainingTime: ' not in src: continue
        L = src.split('\n'); i = 0
        while i < len(L):
            if SIG.match(L[i]):
                e = next(k for k in range(i, len(L)) if L[k].startswith('.end method'))
                if any('"getDeviceRemainingTime: ' in l for l in L[i:e]):
                    assert not any('S23Watts' in l for l in L[i:e]), 'already hooked'
                    body, n = [], 0
                    for l in L[i:e]:
                        m = re.match(r'\s*return-object ([vp]\d+)$', l)
                        if m:
                            r = m.group(1); body += [c % ((r, r) if c.count('%s') == 2 else r) if '%s' in c else c for c in CALL]; n += 1
                        body.append(l)
                    L[i:e] = body; hits.append((p, n)); i += len(body) - (e - i)
            i += 1
        if hits and hits[-1][0] == p: open(p, 'w').write('\n'.join(L))
if len(hits) != 1: sys.exit('expected one time-to-full method, found %d' % len(hits))
print('hooked', hits[0][1], 'returns in', hits[0][0])
