# Layer 4a aggregation: per style at the stock tank: net, span, end states, law (logs, charges, recovery), rendezvous, pirates.
import json, math, statistics as st, sys
d = json.load(open(sys.argv[1] if len(sys.argv) > 1 else 'pilot_l3.json')); C = d['careers']
def med(v): return round(st.median(v), 1) if v else None
names = {0: 'alive', 1: 'lost', 2: 'hazard', 3: 'bankrupt'}
out = {}
for s in ['hauler', 'crisis', 'pathfinder', 'careful', 'bold', 'reckless', 'payer', 'forgetful', 'double agent']:
    cs = [c for c in C if c['style'] == s and c['tank'] == 6]
    if not cs: continue
    L = lambda k: sum(c['law'][k] for c in cs)
    steps = [[c['step'][i] for c in cs if c['step'][i] >= 0] for i in range(3)]
    reach3 = len(steps[2]); meds = [med([x / 360 for x in v]) for v in steps]
    span = reach3 >= 19 and all(m is not None for m in meds) and meds[0] <= 10 and all(meds[i + 1] - meds[i] <= 10 for i in range(2))
    rec = {1: [], 2: [], 3: []}; recOpen = {1: 0, 2: 0, 3: 0}
    for c in cs:
        for t, days in c['law']['rec']:
            if days >= 0: rec[t].append(days)
            else: recOpen[t] += 1
    ends = {}
    for c in cs: ends[names[c['endState']]] = ends.get(names[c['endState']], 0) + 1
    row = dict(careers=len(cs), net=med([c['net'] for c in cs]), span='PASS' if span else 'FAIL', reach3=reach3, stepYears=meds, ends=ends,
               smugDeps=L('smugDeps'), smugLogged=L('smugLogged'), loggedPct=round(100 * L('smugLogged') / L('smugDeps'), 1) if L('smugDeps') else None,
               logsLost=L('lost'), odd=L('odd'), oddCharges=L('oddCharges'), under=L('under'), underPay=L('underPay'), meets=L('meets'), meetsMissed=L('meetsMissed'),
               scans=L('scans'), warrants=L('warrants'), minor=L('minor'), serious=L('serious'), grave=L('grave'), breaches=L('breaches'), informs=L('informs'), runs=L('runs'), escapes=L('escapes'),
               fines=L('fines'), pirates=L('pirates'), resisted=L('resisted'), looted=L('looted'),
               recoveryMedianDays={t: med(rec[t]) for t in rec}, recoveryN={t: len(rec[t]) for t in rec}, recoveryOpen=recOpen,
               softlock=sum(c['stuck'] for c in cs), ledger=all(c['ledger'] for c in cs))
    out[s] = row
    print(f"{s:12s} net {row['net']:>7} span {row['span']} {meds} ends {ends} | smuggling deps {row['smugDeps']} logged {row['loggedPct']}% lost {row['logsLost']} | under {row['under']} meets {row['meets']}/{row['meets'] + row['meetsMissed']} | "
          f"scans {row['scans']} warrants {row['warrants']} minor {row['minor']} serious {row['serious']} grave {row['grave']} breach {row['breaches']} inform {row['informs']} runs {row['runs']}/{row['escapes']} | "
          f"recovery days med m/s/g {row['recoveryMedianDays'][1]}/{row['recoveryMedianDays'][2]}/{row['recoveryMedianDays'][3]} (n {row['recoveryN'][1]}/{row['recoveryN'][2]}/{row['recoveryN'][3]}, open {recOpen}) | pirates {row['pirates']} resisted {row['resisted']} | softlock {row['softlock']} ledger {row['ledger']}")
json.dump(out, open('agg_l4.json', 'w'), indent=1)
