#include "seadsa/SeaDsaAliasAnalysis.hh"

#include "seadsa/AllocWrapInfo.hh"
#include "seadsa/DsaLibFuncInfo.hh"
#include "seadsa/Global.hh"
#include "seadsa/Graph.hh"
#include "seadsa/InitializePasses.hh"
#include "seadsa/support/Debug.h"
#include "llvm/Analysis/CFLAliasAnalysisUtils.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"

#define DEBUG_TYPE "sea-aa"
using namespace llvm;
using namespace seadsa;
namespace dsa = seadsa;

namespace seadsa {

SeaDsaAAResult::SeaDsaAAResult(TargetLibraryInfoWrapperPass &tliWrapper,
                               AllocWrapInfo &awi, DsaLibFuncInfo &dlfi)
    : m_tliWrapper(tliWrapper), m_awi(awi), m_dlfi(dlfi), m_fac(nullptr),
      m_cg(nullptr), m_dsa(nullptr) {}

SeaDsaAAResult::SeaDsaAAResult(SeaDsaAAResult &&RHS)
    : AAResultBase(std::move(RHS)), m_tliWrapper(RHS.m_tliWrapper),
      m_dl(nullptr), m_awi(RHS.m_awi), m_dlfi(RHS.m_dlfi),
      m_fac(std::move(RHS.m_fac)), m_cg(std::move(RHS.m_cg)),
      m_dsa(std::move(RHS.m_dsa)) {}

SeaDsaAAResult::~SeaDsaAAResult() = default;

static Module *getModuleFromQuery(Value *ValA, Value *ValB) {
  Function *MaybeFnA =
      const_cast<Function *>(llvm::cflaa::parentFunctionOfValue(ValA));
  Function *MaybeFnB =
      const_cast<Function *>(llvm::cflaa::parentFunctionOfValue(ValB));
  if (!MaybeFnA && !MaybeFnB) {
    // The only times this is known to happen are when globals + InlineAsm are
    // involved
    DOG(llvm::errs()
        << "SeaDsaAA: could not extract parent function information.\n");
    return nullptr;
  }
  Module *M = nullptr;
  if (MaybeFnA) { M = MaybeFnA->getParent(); }
  if (MaybeFnB) {
    if (M != MaybeFnB->getParent()) {
      DOG(llvm::errs()
          << "SeaDsaAA: cannot handle functions in different modules.\n");
      return nullptr;
    }
  }
  return M;
}

static uint64_t storageSize(const Type *t, const DataLayout &dl) {
  return dl.getTypeStoreSize(const_cast<Type *>(t));
}

static Optional<uint64_t> sizeOf(const Graph::Set &types,
                                 const DataLayout &dl) {
  if (types.isEmpty()) {
    return 0;
  } else {
    uint64_t sz = storageSize(*(types.begin()), dl);
    if (types.isSingleton()) {
      return sz;
    } else {
      auto it = types.begin();
      ++it;
      if (std::all_of(it, types.end(), [dl, sz](const Type *t) {
            return (storageSize(t, dl) == sz);
          })) {
        return sz;
      } else {
        return None;
      }
    }
  }
}

namespace {
// Inclusive byte range [lo, hi] of an access; hi = none means unbounded.
struct ByteRange {
  uint64_t lo;
  Optional<uint64_t> hi;
};

// The range touched by an access of size \p sz through cell \p c. Uses the
// query size when the client provides one; otherwise falls back to the size
// of the type(s) accessed at the cell (the historical heuristic), or to an
// unbounded range when even that is unknown. If the cell lies inside a
// collapsed interval of the node (I-DSA), the whole interval may be touched.
ByteRange accessRange(const Cell &c, LocationSize sz, const DataLayout &dl) {
  const Node *n = c.getNode();
  const unsigned lo = c.getRawOffset();
  ByteRange r{lo, None};
  if (sz.hasValue()) {
    if (sz.getValue() > 0) r.hi = lo + sz.getValue() - 1;
    else r.hi = lo;
  } else if (n->hasAccessedType(lo)) {
    if (auto tySz = sizeOf(n->getAccessedType(lo), dl)) {
      r.hi = lo + (tySz.getValue() > 0 ? tySz.getValue() - 1 : 0);
    }
  }
  // widen to any collapsed interval overlapping the access
  for (const Cell &ck : n->getCollapsedCells()) {
    const unsigned s = ck.getStartOffset();
    const auto e = ck.getEndOffset(); // none = +oo
    const bool overlaps =
        (!r.hi || s <= r.hi.getValue()) && (!e || e.get() >= r.lo);
    if (!overlaps) continue;
    r.lo = std::min<uint64_t>(r.lo, s);
    if (!e || !r.hi) r.hi = None;
    else r.hi = std::max<uint64_t>(r.hi.getValue(), e.get());
  }
  return r;
}

bool rangesOverlap(const ByteRange &a, const ByteRange &b) {
  const bool aBeforeB = a.hi && a.hi.getValue() < b.lo;
  const bool bBeforeA = b.hi && b.hi.getValue() < a.lo;
  return !(aBeforeB || bBeforeA);
}
} // namespace

// Sound may-alias test between two memory accesses: same node, and the byte
// ranges they can touch overlap. Sizes come from the alias query; collapsed
// (I-DSA interval) cells widen the ranges; array nodes wrap offsets modulo the
// element size.
static llvm::cl::opt<bool> PrintUnsafeReasons(
    "sea-dsa-aa-print-unsafe",
    llvm::cl::desc("Print why a node is treated as unsafe by SeaDsaAA"),
    llvm::cl::Hidden, llvm::cl::init(false));

static bool mayAlias(const Cell &c1, LocationSize s1, const Cell &c2,
                     LocationSize s2, const DataLayout &dl) {
  // A node reached through an int->ptr cast may point anywhere (like SVF's
  // black-hole object); a node whose address merely escaped to an integer
  // (ptr->int) is not by itself treated as aliasing everything.
  auto maybeUnsafe = [](const Node *n) {
    return n->isIntToPtr() || n->isIncomplete() || n->isUnknown();
  };

  if (c1.isNull() || c2.isNull()) { return true; }

  const Node *n1 = c1.getNode();
  const Node *n2 = c2.getNode();

  if (maybeUnsafe(n1) || maybeUnsafe(n2)) {
    if (PrintUnsafeReasons) {
      for (const Node *n : {n1, n2}) {
        if (!maybeUnsafe(n)) continue;
        llvm::errs() << "SeaDsaAA unsafe node: sites="
                     << n->getAllocSites().size() << " I=" << n->isIncomplete()
                     << " U=" << n->isUnknown() << " I2P=" << n->isIntToPtr()
                     << " X=" << n->isExternal()
                     << "\n";
      }
    }
    return true;
  }

  if (n1 != n2) {
    // different nodes cannot alias
    return false;
  }

  if (n1->isOffsetCollapsed()) { return true; }

  ByteRange r1 = accessRange(c1, s1, dl);
  ByteRange r2 = accessRange(c2, s2, dl);

  if (n1->isArray()) {
    // offsets are modulo the array stride; an access spanning a whole
    // stride (or unbounded) can touch any element position
    const uint64_t stride = n1->size();
    if (stride == 0) return true;
    auto reduce = [stride](ByteRange &r) -> bool {
      if (!r.hi) return false;
      if (r.hi.getValue() - r.lo + 1 >= stride) return false;
      const uint64_t lo = r.lo % stride;
      const uint64_t hi = r.hi.getValue() % stride;
      if (hi < lo) return false; // wraps around
      r.lo = lo;
      r.hi = hi;
      return true;
    };
    if (!reduce(r1) || !reduce(r2)) return true;
  }

  return rangesOverlap(r1, r2);
}

llvm::AliasResult SeaDsaAAResult::alias(const llvm::MemoryLocation &LocA,
                                        const llvm::MemoryLocation &LocB,
                                        llvm::AAQueryInfo &AAQI) {
  DOG(llvm::errs() << "SeaDsaAA --- Alias query: " << *LocA.Ptr << " and "
                   << *LocB.Ptr << "\n\n";);

  auto *ValA = const_cast<Value *>(LocA.Ptr);
  auto *ValB = const_cast<Value *>(LocB.Ptr);

  if (!ValA->getType()->isPointerTy() || !ValB->getType()->isPointerTy()) {
    return AliasResult(AliasResult::NoAlias);
  }

  if (ValA == ValB) { return AliasResult(AliasResult::MustAlias); }

  // Run seadsa if we have not done it yet
  if (!m_dsa) {
    if (Module *M = getModuleFromQuery(ValA, ValB)) {
      m_fac = std::make_unique<Graph::SetFactory>();
      m_dl = &(M->getDataLayout());
      m_cg = std::make_unique<CallGraph>(*M);
      // same set-up as DsaAnalysis::runOnModule (alloc wrappers + library
      // function specs); without it library calls are unknown externals and
      // the graphs differ from the ones the seadsa tool prints/dumps.
      m_awi.initialize(*M, nullptr);
      m_dlfi.initialize(*M);
      const GlobalAnalysisKind kind = getDsaGlobalAnalysisKindOption();
      if (kind == GlobalAnalysisKind::CONTEXT_INSENSITIVE ||
          kind == GlobalAnalysisKind::FLAT_MEMORY) {
        m_dsa = std::make_unique<ContextInsensitiveGlobalAnalysis>(
            *m_dl, m_tliWrapper, m_awi, m_dlfi, *m_cg, *m_fac,
            kind == GlobalAnalysisKind::FLAT_MEMORY);
        llvm::errs() << "SeaDsaAA: global analysis = "
                     << (kind == GlobalAnalysisKind::FLAT_MEMORY ? "flat"
                                                                  : "ci")
                     << "\n";
      } else {
        m_dsa = std::make_unique<BottomUpTopDownGlobalAnalysis>(
            *m_dl, m_tliWrapper, m_awi, m_dlfi, *m_cg, *m_fac);
        llvm::errs() << "SeaDsaAA: global analysis = butd-cs\n";
      }
      DOG(llvm::errs() << "Running SeaDsaAA.\n");
      m_dsa->runOnModule(*M);
    }
  }

  // We tried to run seadsa but we couldn't
  if (!m_dsa) { return AAResultBase::alias(LocA, LocB, AAQI); }

  // Globals (and constant expressions over them) have no parent function;
  // their cells are implicitly present in every function graph, so resolve
  // the query in the graph of whichever operand is function-local.
  auto FnA = const_cast<Function *>(llvm::cflaa::parentFunctionOfValue(ValA));
  auto FnB = const_cast<Function *>(llvm::cflaa::parentFunctionOfValue(ValB));
  if (!FnA && !FnB) {
    // Two globals: they alias only if some function graph that references
    // both has merged them (BU/TD propagate such merges to every function
    // that can observe them). GlobalAlias and constant expressions are left
    // to the default implementation.
    auto *GA = dyn_cast<GlobalValue>(ValA->stripPointerCasts());
    auto *GB = dyn_cast<GlobalValue>(ValB->stripPointerCasts());
    if (!GA || !GB || isa<GlobalAlias>(GA) || isa<GlobalAlias>(GB)) {
      return AAResultBase::alias(LocA, LocB, AAQI);
    }
    bool seenBoth = false;
    for (Function &F : *GA->getParent()) {
      if (F.isDeclaration() || !m_dsa->hasGraph(F)) continue;
      Graph &g = m_dsa->getGraph(F);
      if (!g.hasScalarCell(*ValA) || !g.hasScalarCell(*ValB)) continue;
      seenBoth = true;
      if (mayAlias(g.getCell(*ValA), LocA.Size, g.getCell(*ValB), LocB.Size,
                   *m_dl)) {
        return AAResultBase::alias(LocA, LocB, AAQI);
      }
    }
    return seenBoth ? AliasResult(AliasResult::NoAlias)
                    : AAResultBase::alias(LocA, LocB, AAQI);
  }
  if (FnA && FnB && FnA != FnB) {
    DOG(llvm::errs() << "SeaDsaAA does not handle inter-procedural queries at "
                        "the moment.\n");
    return AAResultBase::alias(LocA, LocB, AAQI);
  }
  Function *Fn = FnA ? FnA : FnB;

  assert(m_dsa);
  assert(m_dl);

  if (!m_dsa->hasGraph(*Fn)) { return AAResultBase::alias(LocA, LocB, AAQI); }
  auto &g = m_dsa->getGraph(*Fn);

  if (g.hasCell(*ValA) && g.hasCell(*ValB)) {
    const Cell &c1 = g.getCell(*ValA);
    const Cell &c2 = g.getCell(*ValB);
    if (!mayAlias(c1, LocA.Size, c2, LocB.Size, *m_dl)) {
      return AliasResult(AliasResult::NoAlias);
    }
  }

  // -- fall back to default implementation
  return AAResultBase::alias(LocA, LocB, AAQI);
}

char SeaDsaAAWrapperPass::ID = 0;

ImmutablePass *createSeaDsaAAWrapperPass() { return new SeaDsaAAWrapperPass(); }

SeaDsaAAWrapperPass::SeaDsaAAWrapperPass() : ImmutablePass(ID) {
  initializeSeaDsaAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

void SeaDsaAAWrapperPass::initializePass() {
  DOG(errs() << "initializing SeaDsaAAWrapperPass\n");
  auto &tliWrapper = this->getAnalysis<TargetLibraryInfoWrapperPass>();
  auto &awi = this->getAnalysis<AllocWrapInfo>();
  auto &dlfi = this->getAnalysis<DsaLibFuncInfo>();
  Result.reset(new SeaDsaAAResult(tliWrapper, awi, dlfi));
}

void SeaDsaAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.addRequired<AllocWrapInfo>();
  AU.addRequired<DsaLibFuncInfo>();
}
} // namespace seadsa

INITIALIZE_PASS(SeaDsaAAWrapperPass, "seadsa-aa", "SeaDsa-Based Alias Analysis",
                false, true)
