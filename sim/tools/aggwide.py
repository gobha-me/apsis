# Wide-seed rescue/debt sample (pilot_wide.json): debts per style and tank, clearing time median / p90 / max in years; flag p90 > 3 y.
import json, math, statistics as st
d = json.load(open('pilot_wide.json')); C = d['careers']
def q(v, p): v = sorted(v); return v[min(len(v) - 1, math.ceil(p * len(v)) - 1)]
out = {}; print('careers', len(C), 'pilot-years', d['pilotYears'])
print('%-11s %-5s %7s %7s %6s %6s %6s %6s %6s %5s' % ('style', 'tank', 'rescues', 'debts', 'med', 'p90', 'max', 'open*', 'stuck', 'flag'))
print('(* debts still open at the end of the 40 years; they are in the distribution with their age so far, a lower bound)')
for s in ('hauler', 'crisis', 'pathfinder', 'careful', 'bold', 'reckless', 'payer', 'forgetful', 'double agent'):
    for tk in ((3, 4, 6, 'all') if s in ('hauler', 'crisis', 'pathfinder') else (6,)):
        cs = [c for c in C if c['style'] == s and (tk == 'all' or c['tank'] == tk)]
        dc = [x / 360 for c in cs for x in c['debtClear']] + [c['debtOpen'] / 360 for c in cs if c['debtOpen'] > 0]; rc = [x for c in cs for x in c['rescue']]   # debts still open at the end count with their age so far (a lower bound)
        op = sum(1 for c in cs if c['debtOpen'] > 0); stuck = sum(1 for c in cs if c['stuck']); led = all(c['ledger'] for c in cs)
        row = dict(careers=len(cs), rescues=len(rc), debts=len(dc), med=round(st.median(dc), 2) if dc else None, p90=round(q(dc, .9), 2) if dc else None, max=round(max(dc), 2) if dc else None, openAtEnd=op, stuck=stuck, ledger=led)
        out[f'{s}/{tk}'] = row
        print('%-11s %-5s %7d %7d %6s %6s %6s %6d %6d %5s' % (s, tk, len(rc), len(dc), row['med'], row['p90'], row['max'], op, stuck, 'FLAG' if dc and row['p90'] > 3 else ''))
json.dump(out, open('agg_wide.json', 'w'), indent=1)
