// Layer 4: law and logs (spec A law, B3 logs read by interest, C4 encounters, C7 arrival judged from the last logged position), pirates vs pilots,
// courier and spy work, rendezvous, and the recovery watch. Hooks called by the pilot engine; state lives in Pilot.
#pragma once
#include "pilot.hpp"
namespace apsis::world {
enum LogClass : int { LC_OWN = 0, LC_TOLERATED = 1, LC_STANDS = 2, LC_ODD = 3 };
int logClass(const World& W, int origin, const Job& j, bool cargoOfJobAboard, bool contraband);   // B3 lookup
bool isAlly(const World& W, int a, int b);
bool isRival(const World& W, int a, int b);
HouseLaw* lawOf(Pilot& p, int house);
const HouseLaw* lawOf(const Pilot& p, int house);
bool wanted(const Pilot& p, int house);
bool exiled(const World& W, int house);
bool hidden(const Pilot& p);                          // package or papers hide only in a compartment
i64 depthJ(const World& W, int s);                    // per mille J from the nearest unclaimed system
i64 pirateThreatPm(const World& W, int s);            // pirate encounter chance on arrival at s with something worth taking
i64 scanChancePm(const World& W, int s);              // patrol scan chance on arrival at s (0 if unpatrolled)
void lawDepart(World& W, int origin, int dest);       // C3/B3: the departure log
bool lawArriveSystem(World& W, const Leg2& l);        // a leg's arrival: beacon log, pirates, scan; true = the trip ended (jail)
bool lawDock(World& W, int s, int prevSys);           // docking: breach, served warrant, arrival suspicion (C7: prevSys = the previous leg's system); true = held
void lawDay(World& W);                                // heat fade, lapses, exile terms, recoveries
void lawAction(World& W, const Action& a);            // A_LAW: 0 pay, 1 inform, 2 debt; f[1] house
void underJobs(const World& W, std::vector<Job>& out);// courier and spy offers (B3 issuers), with rendezvous
void settleUnder(World& W, int at, bool meetArrival); // courier/spy delivery at the destination or the meet
i64 worthOf(const Pilot& p);
}  // namespace apsis::world
namespace apsis::world {
// engine internals shared with the law module (defined in pilot.cpp)
Agent* liveAgent(World& W, int o);
void pay(World& W, i64 amt, int creditor, i64* book);
void income(World& W, i64 amt, bool delivery);
}  // namespace apsis::world
