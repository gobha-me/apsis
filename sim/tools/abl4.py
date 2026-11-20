# Layer 4a ablations (E4): median net per style at the stock tank, base vs each switch off; and the rendezvous pay-per-day check (E3).
import json, statistics as st, glob, os
S = ['hauler', 'crisis', 'pathfinder', 'careful', 'bold', 'reckless', 'payer', 'forgetful', 'double agent']
def nets(f):
    C = json.load(open(f))['careers']; return {s: st.median([c['net'] for c in C if c['style'] == s and c['tank'] == 6]) for s in S}, C
base, CB = nets('abl4/base.json'); out = {'base': base}
print('%-12s' % 'switch off' + ''.join('%13s' % s[:12] for s in S))
print('%-12s' % 'base' + ''.join('%13d' % base[s] for s in S))
for f in sorted(glob.glob('abl4/off_*.json')):
    r = os.path.basename(f)[4:-5]; n, _ = nets(f); out[r] = n
    print('%-12s' % r + ''.join('%7d %+4.0f%%' % (n[s], 100 * (n[s] - base[s]) / max(1, abs(base[s]))) for s in S))
# rendezvous vs direct, per style: pay per day of underworld jobs
print('\nrendezvous vs direct courier/spy jobs: pay per day (all careers of the style)')
rv = {}
for s in S[3:]:
    cs = [c for c in CB if c['style'] == s and c['tank'] == 6]; L = lambda k: sum(c['law'][k] for c in cs)
    d = L('payDirect') / max(1, L('daysDirect')); m = L('payMeet') / max(1, L('daysMeet')); rv[s] = dict(direct=round(d, 1), meet=round(m, 1), meets=L('meets'), ok=m >= d or L('meets') == 0)
    print(f"  {s:12s} direct {d:6.1f}/day  meet {m:6.1f}/day  ({L('meets')} meets)  {'PASS' if rv[s]['ok'] else 'FAIL'}")
smug = ['careful', 'bold', 'reckless', 'payer', 'forgetful', 'double agent']
tot = sum(sum(c['law']['smugDeps'] for c in CB if c['style'] == s and c['tank'] == 6) for s in smug); lg = sum(sum(c['law']['smugLogged'] for c in CB if c['style'] == s and c['tank'] == 6) for s in smug)
print(f"\nsmuggling departures logged, all law styles: {lg} of {tot} = {100 * lg / max(1, tot):.1f}% (target 25-50%)")
it = {s: round(100 * (out['interest'][s] - base[s]) / max(1, abs(base[s])), 1) for s in smug}
print('interest off moves smugglers\' net by:', it, '(target: at least 5%)')
json.dump(dict(nets=out, rendezvous=rv, logged=dict(logged=lg, deps=tot), interestMove=it), open('abl_l4.json', 'w'), indent=1)
