# Aggregates pilot_l3.json: per style x tank, span, rescue, debt clearing, opening, range.
import json, sys, statistics as st
d = json.load(open(sys.argv[1] if len(sys.argv) > 1 else 'pilot_l3.json'))
C = d['careers']
def med(v): return st.median(v) if v else None
def pct(v, q):
    if not v: return None
    v = sorted(v); return v[min(len(v) - 1, int(q * len(v)))]
out = {}
for style in ['hauler', 'crisis', 'pathfinder']:
  for tank in [3, 4, 6]:
    cs = [c for c in C if c['style'] == style and c['tank'] == tank]; n = len(cs)
    steps = []; ok = True
    for k in range(3):
      v = [c['step'][k] for c in cs if c['step'][k] >= 0]; steps.append((len(v), med(v)))
    reach3 = steps[2][0]; meds = [s[1] for s in steps]
    span = reach3 >= 19 and all(m is not None for m in meds) and meds[0] <= 3600 and meds[1] - meds[0] <= 3600 and meds[2] - meds[1] <= 3600
    rc = [x for c in cs for x in c['rescue']]; dc = [x for c in cs for x in c['debtClear']]; opn = [c['debtOpen'] for c in cs if c['debtOpen'] >= 0]
    allDebt = dc + opn
    o = dict(n=n, net=med([c['net'] for c in cs]), netMin=min(c['net'] for c in cs), steps=[(s[0], None if s[1] is None else round(s[1] / 360, 1)) for s in steps], span=span,
             strandings=sum(c['strand'] for c in cs), legs=sum(c['legs'] for c in cs), mis=sum(c['mis'] for c in cs), push=sum(c['push'] for c in cs), blind=sum(c['blind'] for c in cs),
             rescueMed=med(rc), rescueMax=max(rc) if rc else None, debtN=len(allDebt), debtMedY=None if not allDebt else round(med(allDebt) / 360, 2), debtP90Y=None if not allDebt else round(pct(allDebt, 0.9) / 360, 2),
             debtMaxY=None if not allDebt else round(max(allDebt) / 360, 2), debtOpen=len(opn), stuck=sum(c['stuck'] for c in cs), ledger=all(c['ledger'] for c in cs), maxIdle=max(c['maxIdle'] for c in cs),
             firstSurveyMed=med([c['firstSurvey'] for c in cs if c['firstSurvey'] >= 0]), surveyed=sum(c['firstSurvey'] >= 0 for c in cs),
             darkY1=sum(0 <= c['firstDark'] < 360 for c in cs), firstDarkMed=med([c['firstDark'] for c in cs if c['firstDark'] >= 0]),
             y1Jobs=[sum(c['y1Jobs'][k] for c in cs) for k in range(3)], y1Strand=sum(c['y1Strand'] for c in cs), y1Rescue=sum(c['y1Rescue'] for c in cs), idleY1=round(st.mean([c['idleY1'] for c in cs]) / 360 * 100, 1),
             advances=sum(c['advances'] for c in cs), fuelDebt=sum(c['fuelDebt'] for c in cs), lost=sum(c['lost'] for c in cs), ends=sorted(set(c['end'] for c in cs)))
    out[f'{style}/{tank}'] = o
    print(f"{style:10s} tank {tank}: net {o['net']} (min {o['netMin']}) steps {o['steps']} span {'PASS' if span else 'no'} | strand {o['strandings']} of {o['legs']} legs (mis {o['mis']}, push {o['push']}, blind {o['blind']}) rescue med {o['rescueMed']} max {o['rescueMax']} | debt n {o['debtN']} med {o['debtMedY']}y p90 {o['debtP90Y']}y max {o['debtMaxY']}y open {o['debtOpen']} stuck {o['stuck']} | ledger {o['ledger']} maxIdle {o['maxIdle']}")
    print(f"           opening: surveyed {o['surveyed']}/{n} med day {o['firstSurveyMed']}; dark out-and-back in year 1 {o['darkY1']}/{n} med day {o['firstDarkMed']}; y1 trips by distance <=1J/1-2J/>2J {o['y1Jobs']}; y1 strand {o['y1Strand']} cost {o['y1Rescue']}; y1 idle {o['idleY1']}%; advances {o['advances']} fuel debt {o['fuelDebt']} lost {o['lost']}")
R = d['range']
print('\nrange: share of homes with a dark system within stock range (no push / pushing), per tank, all maps and starts')
for tank in [3, 4, 6]:
  rs = [r for r in R if r['tank'] == tank]; h = sum(r['homes'] for r in rs)
  print(f"  tank {tank}: one jump {sum(r['eff'] for r in rs)}/{h} within effective range, {sum(r['push'] for r in rs)}/{h} pushing; out and back on one tank without pushing {sum(r['roundTrip'] for r in rs)}/{h}")
  out['range/%d' % tank] = dict(homes=h, eff=sum(r['eff'] for r in rs), push=sum(r['push'] for r in rs), roundTrip=sum(r['roundTrip'] for r in rs), noDark=sum(1 for r in rs if r['dark'] == 0))
print('  per map and start (tank 6): one jump / out and back:', [(r['seed'], r['start'], f"{r['eff']}/{r['roundTrip']}/{r['homes']}") for r in R if r['tank'] == 6])
print('  dark systems per map at start:', [(r['seed'], r['start'], r['dark']) for r in R if r['tank'] == 6])
cs6 = [c for c in C if c['tank'] == 6]
out['contested'] = dict(careers=len(cs6), pilotYears=40 * len(cs6), contested=sum(c['contested'] for c in cs6), bids=sum(c['bids'] for c in cs6), ties=sum(c['ties'] for c in cs6))
print('\ncontested faction clearings during the tank-6 careers (pilots present):', out['contested'])
json.dump(out, open('agg_l3.json', 'w'), indent=1)
