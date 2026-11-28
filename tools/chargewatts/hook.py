"""Insert the S23Watts call into KeyguardSecIndicationController.addInitialIndication() (smali, in place).
The charging branch ends with addIndicationTimeout(BATTERY, text, ...) and addIndication(BATTERY_RESTING,
text); the call goes right before the first, so both get the text with the watts.
usage: hook.py KeyguardSecIndicationController.smali"""
import re, sys
p = sys.argv[1]; L = open(p).read().split('\n')
start = next(i for i, l in enumerate(L) if l.startswith('.method') and ' addInitialIndication()V' in l)
end = next(i for i in range(start, len(L)) if L[i].startswith('.end method'))
assert not any('S23Watts' in l for l in L[start:end]), 'already hooked'
call = re.compile(r'\s*invoke-virtual \{(\w+), (\w+), (\w+), (\w+), (\w+)\}, Lcom/android/systemui/statusbar/KeyguardSecIndicationController;'
                  r'->addIndicationTimeout\(Lcom/android/systemui/statusbar/IndicationEventType;Ljava/lang/CharSequence;')
for i in range(start, end):
    m = call.match(L[i])
    if not m: continue
    ev, text = m.group(2), m.group(3)
    # the event register holds BATTERY, and the next addIndication passes BATTERY_RESTING with the same text
    assert any(re.search(r'sget-object %s, .*->BATTERY:' % ev, L[k]) for k in range(i - 30, i)), 'not the BATTERY call'
    nxt = next(k for k in range(i + 1, end) if '->addIndication(' in L[k])
    assert any('->BATTERY_RESTING:' in L[k] for k in range(i, nxt)) and (', %s}' % text) in L[nxt], 'unexpected shape'
    reg = int(text[1:]) if text[0] == 'v' else None
    inv = 'invoke-static' if reg is not None and reg < 16 else 'invoke-static/range'
    args = '{%s}' % text if inv == 'invoke-static' else '{%s .. %s}' % (text, text)
    L[i:i] = ['    %s %s, Lcom/android/systemui/statusbar/S23Watts;->add(Ljava/lang/CharSequence;)Ljava/lang/CharSequence;' % (inv, args),
              '', '    move-result-object %s' % text, '']
    open(p, 'w').write('\n'.join(L)); print('hooked at line', i + 1, 'text register', text); break
else:
    sys.exit('addIndicationTimeout(BATTERY, ...) not found in addInitialIndication()')
