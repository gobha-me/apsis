// Layer-1 test world. It exists to exercise the core: parallel decisions from a snapshot, delayed
// intents, both kinds of clearing, the input channel, ledgers, checkpoints and the state hash.
// Layer 2 replaces it with the real world, economy and factions.
#pragma once
#include <array>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>
#include "../core/clear.hpp"
#include "../core/input.hpp"
#include "../core/ledger.hpp"
#include "../core/params.hpp"
#include "../core/pool.hpp"

namespace apsis::proto {

constexpr int G = 4;                        // goods; ledger quantity G is money
enum ActKind : int32_t { SELL = 0, BUY = 1, MOVE = 2 };      // canonical order of player actions
enum IntentKind : int32_t { I_MOVE = 0, I_HUB = 1, I_CONTRACT = 2 };

struct Station { int32_t owner = 0, prod = 0; int64_t rate = 0; std::array<int64_t, G> stock{}, cap{}; };
struct Faction { int64_t treasury = 0; };
struct Contract { int32_t holder = -1; };
struct Pilot { int32_t at = 0; int64_t wallet = 0; std::array<int64_t, G> hold{}; };

struct World {
  Params P;
  uint32_t seed = 0;
  int64_t day = 0;
  std::vector<Station> S;
  std::vector<Faction> F;
  std::vector<Contract> C;
  Pilot pl;
  std::vector<Intent> queue;                // pending intents, canonical order
  Ledger led{G + 1};
  // not state: the input channel, the log of what was applied, the thread count
  Inbox inbox;
  std::vector<LogEntry> log;
  int threads = 1;
  std::shared_ptr<Pool> pool;               // made on first use for threads > 1
  // not state: coverage counters for the gates (what actually happened)
  struct Cov { int64_t multiDecideDays = 0, intents = 0, moveGranted = 0, hubGranted = 0, hubContested = 0, contractWins = 0, contractTies = 0, actsApplied = 0, poolDays = 0; } cov;
};

World create(uint32_t seed, int stations = 24, int factions = 8, int contracts = 4);
void submit(World& w, const Action& a);
void tick(World& w);
std::vector<Action> bot(const World& w);    // a scripted pilot: reads state, never writes it
std::vector<uint8_t> checkpoint(const World& w);
bool restore(const std::vector<uint8_t>& b, World& out);   // false on a damaged checkpoint
std::string hashState(const World& w);
bool ledgerOk(const World& w);

}  // namespace apsis::proto
