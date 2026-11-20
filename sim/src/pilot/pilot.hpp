// Layer 3: the pilot engine (spec B1 jumps in J, B2 ships, B6 rescue, C3 flight leg by leg) and the input channel.
#pragma once
#include "../world/sim.hpp"
namespace apsis::world {
enum Act : int32_t { A_START = 0, A_REFIT = 1, A_REPAIR = 2, A_SELL_CHART = 3, A_TAKE = 4, A_SURVEY = 5, A_FLY = 6, A_REPAY = 7, A_LAW = 8, A_POLICY = 9, A_DROP = 10 };   // canonical order
// DROP: abandon the job in hand (it fails, no charge)
// LAW fields: f[0] 0 pay, 1 inform, 2 debt; f[1] house.  POLICY fields: f[0] law policy (0 pay when served, 1 payer, 2 forgetful, 3 informer), f[1] run from scans (0/1)
// REPAY fields: f[0] amount (paid from the wallet against debt, oldest creditor first)
// FLY fields: f[0] to, f[1] good, f[2] units, f[3] push (allow legs past effective range), f[4] purpose (0 haul/move, 1 chart run)
// REFIT fields: f[0] part, f[1] drive index (for P_DRIVE).  TAKE: f[0] job id.  SURVEY: f[0] body.  SELL_CHART: f[0] house.  START: f[0] station.
struct Plan { std::vector<Leg2> legs; i64 days = 0, riskPm = 0; bool ok = false; };
struct Board { std::vector<Job> jobs; };
i64 driveRatingPm(int idx);
i64 shipSpace(const World& W, const Ship& s);
i64 shipMass(const World& W, const Ship& s);
i64 rEffPm(const World& W, const Ship& s);
i64 rangeMap(const World& W, const Ship& s, bool push);           // map units
i64 legRiskPm(const World& W, i64 len, bool lit, i64 rEff, i64 dh);
i64 legDays(const World& W, i64 len, bool blind, int dest);
bool dockable(const World& W, int s);
bool lit(const World& W, int s);
struct Routes { std::vector<i64> cost, days, jumps, risk; std::vector<int> via; std::vector<char> pushed; };
Routes routesFrom(const World& W, int from, i64 rEff, bool push, i64 dh);   // cheapest-in-jumps-then-risk paths to every system
Plan planOf(const World& W, const Routes& R, int from, int to, i64 rEff, i64 dh);
i64 lotValue(const World& W, const Station& s, int k, i64 n);
Board board(const World& W);
i64 refitPrice(const World& W, int part, int driveIdx);
bool refitShip(const World& W, const Ship& cur, int part, int driveIdx, Ship& out);
i64 debtOf(const Pilot& p);
i64 lsDays(const World& W, const Ship& s, i64 lsh);   // days of life support left at this health (hull, cut-off decks, refit points, suit)
void submit(World& W, const Action& a);
void tick(World& W);   // inputs (canonical order), the world day, then the pilot's day
bool ledgerAll(const World& W);   // the world ledger including the pilot's hold and losses
}  // namespace apsis::world
