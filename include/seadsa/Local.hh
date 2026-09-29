#pragma once

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "seadsa/Graph.hh"

namespace llvm {
class DataLayout;
class TargetLibraryInfoWrapperPass;
class TargetLibraryInfo;
class raw_ostream;
} // namespace llvm

namespace seadsa {
class AllocWrapInfo;
class Cloner;

/// I-DSA: close \p g under the pointer-arithmetic rules (GEPs,
/// inttoptr(ptrtoint(p) + C), the pointer fields of extractvalue/insertvalue,
/// and, with --sea-dsa-lazy-mem-transfer, the lazy memcpy/memmove copies). The
/// local pass builds a graph in one pass and never recomputes a result whose
/// base cell later becomes part of a collapsed interval, nor re-copies links
/// that reach a copy's source later; this re-applies the rules with interval
/// widening until a run changes nothing. \p fns are the functions whose
/// instructions built \p g (their field accesses and copies are re-applied;
/// GEP and inttoptr sites come from \p g's scalar map). No-op unless
/// --sea-dsa-partial-collapse and --sea-dsa-partial-collapse-closure (default
/// on) are set, and in the classic (non-interval) build. \p phase and \p name
/// only label the dsa-closure log and the run-cap warning, which is printed on
/// stderr unconditionally. Returns true if \p g changed (it may then contain
/// forwarding nodes).
bool closeUnderPointerArithmetic(Graph &g, const llvm::DataLayout &dl,
                                 llvm::ArrayRef<const llvm::Function *> fns,
                                 llvm::StringRef phase, llvm::StringRef name);

/// I-DSA closure (butd-cs): appends to \p callerG the pointer-arithmetic rules
/// of \p calleeG -- its own sites (GEPs and inttoptr of its scalar map, the
/// fields of \p calleeFns) and the rules carried into it -- mapped through the
/// cloner \p C of one call site (Graph::CarriedSite), so that the caller's
/// closures re-check the callee's arithmetic under the caller's intervals.
/// Rules on nodes the call does not clone are dropped. No-op unless the closed
/// model and --sea-dsa-partial-collapse-callee-sites are selected, and in the
/// classic build.
void carryClosureSites(Graph &calleeG,
                       llvm::ArrayRef<const llvm::Function *> calleeFns,
                       Graph &callerG, Cloner &C, const llvm::DataLayout &dl);

/// Resets the totals of closeUnderPointerArithmetic printed by
/// printClosureStats.
void resetClosureStats();

/// Prints one line with the totals of closeUnderPointerArithmetic since the
/// last resetClosureStats() (graphs, maximum runs, repairs, run-cap hits and
/// the nodes collapsed after a cap hit, sequence nodes collapsed by the closed
/// model). Prints nothing unless the closed I-DSA model is selected.
void printClosureStats(llvm::raw_ostream &o);

class LocalAnalysis {
  const llvm::DataLayout &m_dl;
  llvm::TargetLibraryInfoWrapperPass &m_tliWrapper;
  const seadsa::AllocWrapInfo &m_allocInfo;
  bool m_track_callsites;

public:
  LocalAnalysis(const llvm::DataLayout &dl,
                llvm::TargetLibraryInfoWrapperPass &tliWrapper,
                const AllocWrapInfo &allocInfo, bool track_callsites = false)
      : m_dl(dl), m_tliWrapper(tliWrapper), m_allocInfo(allocInfo),
        m_track_callsites(track_callsites) {}

  void runOnFunction(llvm::Function &F, Graph &g);
};

class Local : public llvm::ModulePass {
  Graph::SetFactory m_setFactory;
  typedef std::shared_ptr<Graph> GraphRef;
  llvm::DenseMap<const llvm::Function *, GraphRef> m_graphs;

  const llvm::DataLayout *m_dl;
  const AllocWrapInfo *m_allocInfo;

public:
  static char ID;

  Local();
  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
  bool runOnModule(llvm::Module &M) override;
  bool runOnFunction(llvm::Function &F);
  llvm::StringRef getPassName() const override { return "SeaDsa local pass"; }
  bool hasGraph(const llvm::Function &F) const;
  const Graph &getGraph(const llvm::Function &F) const;
};
} // namespace seadsa
