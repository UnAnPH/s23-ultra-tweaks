"""Pass the charging notification's text through S23Watts.text (smali, in place): ChargingNotification$show$1.run()
gets the text from ChargingNotificationTextFormatter.getContentText(model) and hands it to buildNotification, so
the call goes right after that result, and the notification reads "44% (45 m until full) • 11.4 / 27 W (phone warm)".
usage: noticehook.py 'ChargingNotification$show$1.smali'"""
import re, sys
p = sys.argv[1]; L = open(p).read().split('\n')
start = next(i for i, l in enumerate(L) if l.startswith('.method') and ' run()V' in l)
end = next(i for i in range(start, len(L)) if L[i].startswith('.end method'))
assert not any('S23Watts' in l for l in L[start:end]), 'already hooked'
get = re.compile(r'\s*invoke-virtual(/range)? \{.*\}, Lcom/android/systemui/power/feature/charginginfo/ChargingNotificationTextFormatter;'
                 r'->getContentText\(Lcom/android/systemui/power/feature/charginginfo/ChargingInfoRenderModel;\)Ljava/lang/String;')
hits = [i for i in range(start, end) if get.match(L[i])]
assert len(hits) == 1, 'expected one getContentText call in run(), found %d' % len(hits)
i = hits[0]
mr = next(k for k in range(i + 1, end) if L[k].strip())
m = re.match(r'\s*move-result-object ([vp]\d+)$', L[mr]); assert m, 'getContentText result not moved'
reg = m.group(1)
assert any('->access$buildNotification(' in L[k] and (', %s}' % reg) in L[k] for k in range(mr, end)), 'text not passed to buildNotification'
num = int(reg[1:]) if reg[0] == 'v' else None
inv, args = ('invoke-static', '{%s}' % reg) if num is not None and num < 16 else ('invoke-static/range', '{%s .. %s}' % (reg, reg))
L[mr + 1:mr + 1] = ['', '    %s %s, Lcom/android/systemui/statusbar/S23Watts;->text(Ljava/lang/String;)Ljava/lang/String;' % (inv, args),
                    '', '    move-result-object %s' % reg]
open(p, 'w').write('\n'.join(L)); print('hooked notification text at line', mr + 1, 'register', reg)
