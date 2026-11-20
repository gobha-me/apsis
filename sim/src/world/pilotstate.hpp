// Layer 3: the pilot and the ship (spec B1, B2, B6). One pilot per world.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <vector>
namespace apsis::world {
using i64 = int64_t;
enum Part : int { P_HULL_LIGHT = 0, P_HULL_MEDIUM, P_HULL_HEAVY, P_DRIVE, P_TANK, P_HOLD, P_COMP, P_SHIELD, P_WEAPON, P_LS };
// End states (owner, 2026-11-20): death lost in space, death by hazard, bankruptcy. Prison is deferred.
enum EndState : int { E_NONE = 0, E_LOST = 1, E_HAZARD = 2, E_BANKRUPT = 3 };
// Pilot events (the log a player reads): day, kind, argument
enum EvKind : int { EV_DEBT_WARN = 1, EV_BANKRUPT = 2, EV_DEATH_LOST = 3, EV_DEATH_HAZARD = 4, EV_RESCUED = 5 };
struct Ship { int hull = 1, drive = 1; i64 tank = 6, hold = 80, comp = 0, shields = 0, weapons = 0, ls = 0; };   // drive index: 0.5, 1.0, 1.5, 2.0, 3.0
struct Leg2 { int from = 0, to = 0; i64 len = 0; int blind = 0; };
// kind: 0 emergency, 1 blockade, 2 floor, 3 courier (package), 4 spy (papers). issuer: the house that posted it; meet: rendezvous system or -1
struct Job { i64 id = -1; int kind = -1, to = -1, good = -1; i64 units = 0, reward = 0, deadline = 0; int issuer = -1, about = -1, meet = -1, from = -1; i64 taken = -1; };
// Law (spec A law, B3, C4, C7): per-house heat and record
struct HouseLaw { int house = 0; i64 heat = 0, rem = 0, off = 0, exileUntil = -1, evidence = -1, evSerious = 0; };
struct Warrant { int house = 0, serious = 0; i64 day = 0, fine = 0; };
struct Recov { int tier = 0, house = 0; i64 day = 0, w0 = 0; };
struct Trip { std::vector<Leg2> legs; int idx = 0; i64 legDue = 0; int dest = -1, purpose = 0, active = 0; };   // purpose: 0 haul/move, 1 chart run (out), 2 rescue tow
struct Debt { int house = 0; i64 owed = 0; };
struct Pilot {
  int alive = 0, at = -1, sys = 0; i64 born = 0, wallet = 0, busy = 0, dh = 1000, fuel = 0, advance = 0;
  i64 lsh = 1000; int end = E_NONE; i64 endDay = -1, debtFrom = -1, debtWarned = 0;   // life-support health; end state; the bankruptcy clock
  std::vector<std::array<i64, 3>> ev;
  // law: per-house heat and offences, warrants on file, policies (set by A_POLICY), recoveries
  std::vector<HouseLaw> law; std::vector<Warrant> warrants; std::vector<Recov> recOpen; std::vector<std::array<i64, 3>> recDone;
  i64 informUntil = -1, under = 0, lastLog = -1, tripWorth = 0, jailUntil = -1; int lawPolicy = 0, runPolicy = 0, dockAnyway = 0; std::map<int, i64> delivTo;
  Ship ship; int holdK = -1; i64 holdN = 0; Job job; Trip trip;
  std::vector<Debt> debt; i64 rescueDebtFrom = -1;   // day the open rescue debt began, or -1
  std::vector<int> charts; std::vector<int> charted; std::vector<i64> surveyed;   // surveyed: station*64 + body
  i64 refitSpend = 0; std::array<i64, 3> stepDay{-1, -1, -1};
  std::map<i64, i64> closed;   // job ids already done or failed
  // the books and counters (stats, not decisions)
  struct Books { i64 fines = 0, informPay = 0, underPay = 0, payDirect = 0, daysDirect = 0, payMeet = 0, daysMeet = 0, sales = 0, buys = 0, tolls = 0, rewards = 0, charts = 0, bodies = 0, upkeep = 0, fuel = 0, advFees = 0, repairs = 0, rescue = 0, refits = 0, garnished = 0, lostUnits = 0; } bk;
  struct N { i64 deps = 0, smugDeps = 0, smugLogged = 0, logsStand = 0, logsTol = 0, logsOdd = 0, logsLost = 0, oddCharges = 0, scans = 0, charges1 = 0, charges2 = 0, charges3 = 0, warrants = 0, seized = 0, runs = 0, escapes = 0, breaches = 0, informs = 0, served = 0,
    pirates = 0, resisted = 0, looted = 0, meets = 0, meetsMissed = 0, under = 0, trips = 0, legs = 0, blindLegs = 0, pushLegs = 0, mis = 0, bad = 0, strand = 0, surveys = 0, darkRuns = 0, done = 0, failed = 0, refits = 0, advances = 0, fuelDebt = 0, repairs = 0, dockedIdle = 0; } n;
  std::vector<i64> rescueCost, debtClear;   // per rescue: price; per rescue-debt episode: days to clear
  i64 firstSurvey = -1, firstDarkRun = -1; std::array<i64, 3> y1Jobs{}; i64 y1Strand = 0, y1Rescue = 0, y1Idle = 0;
};
}  // namespace apsis::world
