#pragma once

#include <cstdint>
#include <string>

#include "seadsa/config.h"

#include "llvm/Support/raw_ostream.h"

namespace seadsa {

extern bool SeaDsaStatsFlag;
void SeaDsaEnableStats(bool v = true);
void SeaDsaStatsBeginAnalysis();
void SeaDsaStatsEndAnalysis(llvm::raw_ostream &out = llvm::errs());

class SeaDsaStats {
  static unsigned &getCounter(const std::string &name);
  static llvm::raw_ostream &getOutput();

public:
  static void reset();
  static void count(const std::string &name);
  static void count_max(const std::string &name, unsigned value);
  static void start(const std::string &name);
  static void stop(const std::string &name);
  static void resume(const std::string &name);
  static void Print(llvm::raw_ostream &out);
};

class ScopedSeaDsaStats {
  std::string m_name;

public:
  ScopedSeaDsaStats(std::string &&name, const char *suffix,
                    bool use_count = true);
  ScopedSeaDsaStats(const char *name, bool use_count = true);
  ~ScopedSeaDsaStats();
};

// Backward-compatible aliases for the local analysis code while we migrate.
using LocalTransferStats = SeaDsaStats;
using ScopedLocalTransferStat = ScopedSeaDsaStats;

} // namespace seadsa

#define SEADSA_STATS_JOIN_IMPL(a, b) a##b
#define SEADSA_STATS_JOIN(a, b) SEADSA_STATS_JOIN_IMPL(a, b)

#ifdef SEADSA_STATS
#define SEADSA_SCOPED_STATS(name, active)                                     \
  SEADSA_SCOPED_STATS_(name, active)
#define SEADSA_SCOPED_STATS_(name, active)                                    \
  SEADSA_SCOPED_STATS_##active(name)
#define SEADSA_SCOPED_STATS_0(name)
#define SEADSA_SCOPED_STATS_1(name)                                           \
  seadsa::ScopedSeaDsaStats SEADSA_STATS_JOIN(__seadsa_scoped_stats_,         \
                                              __LINE__)(name)

#define SEADSA_COUNT_STATS(name, active)                                      \
  SEADSA_COUNT_STATS_(name, active)
#define SEADSA_COUNT_STATS_(name, active)                                     \
  SEADSA_COUNT_STATS_##active(name)
#define SEADSA_COUNT_STATS_0(name)
#define SEADSA_COUNT_STATS_1(name) seadsa::SeaDsaStats::count(name)

#define SEADSA_SCOPED_TIMER_STATS(name, active)                               \
  SEADSA_SCOPED_TIMER_STATS_(name, active)
#define SEADSA_SCOPED_TIMER_STATS_(name, active)                              \
  SEADSA_SCOPED_TIMER_STATS_##active(name)
#define SEADSA_SCOPED_TIMER_STATS_0(name)
#define SEADSA_SCOPED_TIMER_STATS_1(name)                                     \
  seadsa::ScopedSeaDsaStats SEADSA_STATS_JOIN(__seadsa_scoped_stats_,         \
                                              __LINE__)(name, false)
#else
#define SEADSA_SCOPED_STATS(name, active)
#define SEADSA_COUNT_STATS(name, active)
#define SEADSA_SCOPED_TIMER_STATS(name, active)
#endif
