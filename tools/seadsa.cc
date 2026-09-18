///
// seadsda -- Print heap graphs and call graph computed by sea-dsa
///

#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CallPrinter.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"

#include "seadsa/CompleteCallGraph.hh"
#include "seadsa/DsaAnalysis.hh"
#include "seadsa/DsaLibFuncInfo.hh"
#include "seadsa/InitializePasses.hh"
#include "seadsa/SeaDsaAliasAnalysis.hh"
#include "seadsa/ShadowMem.hh"
#include "seadsa/support/Debug.h"
#include "seadsa/support/RemovePtrToInt.hh"

static llvm::cl::opt<std::string>
    InputFilename(llvm::cl::Positional,
                  llvm::cl::desc("<input LLVM bitcode file>"),
                  llvm::cl::Required, llvm::cl::value_desc("filename"));

static llvm::cl::opt<std::string>
    OutputDir("outdir", llvm::cl::desc("Output directory for oll"),
              llvm::cl::init(""), llvm::cl::value_desc("DIR"));

static llvm::cl::opt<std::string>
    AsmOutputFilename("oll", llvm::cl::desc("Output analyzed bitcode"),
                      llvm::cl::init(""), llvm::cl::value_desc("filename"));

static llvm::cl::opt<std::string> DefaultDataLayout(
    "data-layout",
    llvm::cl::desc("data layout string to use if not specified by module"),
    llvm::cl::init(""), llvm::cl::value_desc("layout-string"));

static llvm::cl::opt<bool> MemDot(
    "sea-dsa-dot",
    llvm::cl::desc("Print SeaDsa memory graph of each function to dot format"),
    llvm::cl::init(false));

static llvm::cl::opt<bool> MemViewer(
    "sea-dsa-viewer",
    llvm::cl::desc("View SeaDsa memory graph of each function to dot format"),
    llvm::cl::init(false));

static llvm::cl::opt<bool> CallGraphDot(
    "sea-dsa-callgraph-dot",
    llvm::cl::desc("Print SeaDsa complete call graph to dot format"),
    llvm::cl::init(false));

static llvm::cl::opt<bool> RunShadowMem("sea-dsa-shadow-mem",
                                        llvm::cl::desc("Run ShadowMemPass"),
                                        llvm::cl::Hidden,
                                        llvm::cl::init(false));
static llvm::cl::opt<bool>
    AAEval("sea-dsa-aa-eval",
           llvm::cl::desc(
               "Exhaustive Alias Analaysis Precision Evaluation using seadsa"),
           llvm::cl::init(false));

static llvm::cl::opt<bool> AAPairs(
    "sea-dsa-aa-pairs",
    llvm::cl::desc("Print the alias verdict of every pair of pointer values of "
                   "each function (AliasAnalysisEvaluator's pointer universe, "
                   "sizes and line format; alias queries only, no mod/ref)"),
    llvm::cl::init(false));

static llvm::cl::opt<bool> NoSeaDsaAA(
    "sea-dsa-no-aa",
    llvm::cl::desc("Do not register sea-dsa's alias analysis and do not "
                   "rewrite ptrtoint/inttoptr: alias queries are answered "
                   "by LLVM's BasicAA on the module as read"),
    llvm::cl::init(false));

namespace seadsa {
SeaDsaLogOpt loc;
extern bool PrintDsaStats;
extern bool PrintCallGraphStats;
} // namespace seadsa

static llvm::cl::opt<seadsa::SeaDsaLogOpt, true, llvm::cl::parser<std::string>>
    LogClOption("log", llvm::cl::desc("Enable specified log level"),
                llvm::cl::location(seadsa::loc), llvm::cl::value_desc("string"),
                llvm::cl::ValueRequired, llvm::cl::ZeroOrMore);

/// Changes the \p path to be inside specified \p outdir
///
/// Creates output directory if necessary.
/// Returns input \p path if directory cannot be changed
static std::string withDir(const std::string &outdir, const std::string &path) {
  if (!outdir.empty()) {
    if (!llvm::sys::fs::create_directories(outdir)) {
      auto fname = llvm::sys::path::filename(path);
      return outdir + "/" + fname.str();
    }
  }
  return path;
}

namespace {
/// Alias verdicts of every pair of pointer values of a function.
///
/// Same pointer universe (arguments, pointer-typed instructions, pointer
/// operands; callee operands of indirect calls), same pair order, same
/// memory-location sizes and same per-pair output line as LLVM's
/// AliasAnalysisEvaluator (-aa-eval -print-all-alias-modref-info), so its
/// output can be consumed by the same parsers. Unlike the evaluator it asks
/// alias questions only (no mod/ref queries per call site, no call-site pairs)
/// and computes each value's operand string once instead of printing both
/// values of every pair through the IR printer.
class AliasPairsPass : public llvm::FunctionPass {
public:
  static char ID;
  AliasPairsPass() : llvm::FunctionPass(ID) {}
  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
    AU.addRequired<llvm::AAResultsWrapperPass>();
    AU.setPreservesAll();
  }
  llvm::StringRef getPassName() const override { return "sea-dsa alias pairs"; }
  bool runOnFunction(llvm::Function &F) override;
};
char AliasPairsPass::ID = 0;

// AliasAnalysisEvaluator::isInterestingPointer
static bool isInterestingPointer(llvm::Value *V) {
  return V->getType()->isPointerTy() && !llvm::isa<llvm::ConstantPointerNull>(V);
}

bool AliasPairsPass::runOnFunction(llvm::Function &F) {
  using namespace llvm;
  AAResults &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
  const DataLayout &DL = F.getParent()->getDataLayout();

  // -- the pointer universe, in AliasAnalysisEvaluator's insertion order
  SetVector<Value *> Pointers;
  unsigned nCalls = 0;
  for (auto &A : F.args())
    if (A.getType()->isPointerTy()) Pointers.insert(&A);
  for (Instruction &I : instructions(F)) {
    if (I.getType()->isPointerTy()) Pointers.insert(&I);
    if (auto *Call = dyn_cast<CallBase>(&I)) {
      Value *Callee = Call->getCalledOperand();
      if (!isa<Function>(Callee) && isInterestingPointer(Callee))
        Pointers.insert(Callee);
      for (Use &DataOp : Call->data_ops())
        if (isInterestingPointer(DataOp)) Pointers.insert(DataOp);
      ++nCalls;
    } else {
      for (Use &Op : I.operands())
        if (isInterestingPointer(Op)) Pointers.insert(Op);
    }
  }

  // -- operand strings ("<type> <operand>", as printAsOperand(os, true, M))
  //    and memory-location sizes, once per value
  std::vector<std::string> names;
  std::vector<LocationSize> sizes;
  names.reserve(Pointers.size());
  sizes.reserve(Pointers.size());
  for (Value *V : Pointers) {
    std::string s;
    raw_string_ostream os(s);
    V->getType()->print(os);
    os << ' ';
    V->printAsOperand(os, /*PrintType=*/false, F.getParent());
    names.push_back(os.str());
    Type *ElTy = V->getType()->getPointerElementType();
    sizes.push_back(ElTy->isSized()
                        ? LocationSize::precise(DL.getTypeStoreSize(ElTy))
                        : LocationSize::afterPointer());
  }

  // -- every unordered pair, later value first, as the evaluator does
  std::string out;
  raw_string_ostream os(out);
  os << "Function: " << F.getName() << ": " << Pointers.size()
     << " pointers, " << nCalls << " call sites\n";
  unsigned nNo = 0, nMay = 0, nPartial = 0, nMust = 0;
  for (unsigned i = 0, n = Pointers.size(); i < n; ++i) {
    for (unsigned j = 0; j < i; ++j) {
      AliasResult AR = AA.alias(Pointers[i], sizes[i], Pointers[j], sizes[j]);
      switch (AR) {
      case AliasResult::NoAlias: ++nNo; break;
      case AliasResult::MayAlias: ++nMay; break;
      case AliasResult::PartialAlias: ++nPartial; break;
      case AliasResult::MustAlias: ++nMust; break;
      }
      // the evaluator prints the lexicographically smaller operand first
      const std::string &a = names[i], &b = names[j];
      const bool swap = b < a;
      os << "  " << AR << ":\t" << (swap ? b : a) << ", " << (swap ? a : b)
         << "\n";
    }
  }
  os << "  alias queries: " << (nNo + nMay + nPartial + nMust) << " (NoAlias "
     << nNo << ", MayAlias " << nMay << ", PartialAlias " << nPartial
     << ", MustAlias " << nMust << ")\n";
  llvm::errs() << os.str();
  return false;
}
} // namespace

int main(int argc, char **argv) {
  llvm::llvm_shutdown_obj shutdown; // calls llvm_shutdown() on exit
  llvm::cl::ParseCommandLineOptions(argc, argv, "Heap Analysis");

  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  llvm::PrettyStackTraceProgram PSTP(argc, argv);
  llvm::EnableDebugBuffering = true;

  llvm::SMDiagnostic err;
  llvm::LLVMContext context;
  std::unique_ptr<llvm::Module> module;
  std::unique_ptr<llvm::ToolOutputFile> asmOutput;

  module = llvm::parseIRFile(InputFilename, err, context);
  if (module.get() == 0) {
    if (llvm::errs().has_colors())
      llvm::errs().changeColor(llvm::raw_ostream::RED);
    llvm::errs() << "error: "
                 << "Bitcode was not properly read; " << err.getMessage()
                 << "\n";
    if (llvm::errs().has_colors()) llvm::errs().resetColor();
    return 3;
  }

  if (!AsmOutputFilename.empty()) {
    std::error_code error_code;
    asmOutput = std::make_unique<llvm::ToolOutputFile>(
        withDir(OutputDir, AsmOutputFilename), error_code,
        llvm::sys::fs::OF_Text);
    if (error_code) {
      if (llvm::errs().has_colors())
        llvm::errs().changeColor(llvm::raw_ostream::RED);
      llvm::errs() << "error: Could not open " << AsmOutputFilename << ": "
                   << error_code.message() << "\n";
      if (llvm::errs().has_colors()) llvm::errs().resetColor();
      return 3;
    }
  }

  ///////////////////////////////
  // initialise and run passes //
  ///////////////////////////////

  llvm::legacy::PassManager pass_manager;

  llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
  llvm::initializeAnalysis(Registry);

  /// call graph and other IPA passes
  // llvm::initializeIPA (Registry);
  // XXX: porting to 3.8
  llvm::initializeCallGraphWrapperPassPass(Registry);
  // XXX: porting to 5.0
  //  llvm::initializeCallGraphPrinterPass(Registry);
  llvm::initializeCallGraphViewerPass(Registry);
  // XXX: not sure if needed anymore
  // llvm::initializeGlobalsAAWrapperPassPass(Registry);

  llvm::initializeRemovePtrToIntPass(Registry);
  llvm::initializeDsaAnalysisPass(Registry);
  llvm::initializeAllocWrapInfoPass(Registry);
  llvm::initializeDsaLibFuncInfoPass(Registry);
  llvm::initializeAllocSiteInfoPass(Registry);
  llvm::initializeCompleteCallGraphPass(Registry);

  llvm::initializeSeaDsaAAWrapperPassPass(Registry);

  // add an appropriate DataLayout instance for the module
  const llvm::DataLayout *dl = &module->getDataLayout();
  if (!dl && !DefaultDataLayout.empty()) {
    module->setDataLayout(DefaultDataLayout);
    dl = &module->getDataLayout();
  }

  assert(dl && "Could not find Data Layout for the module");

  if (!NoSeaDsaAA) pass_manager.add(seadsa::createRemovePtrToIntPass());
  // ==--== Alias Analysis Passes ==--==/

  // -- add to pass manager
  pass_manager.add(seadsa::createDsaLibFuncInfoPass());
  if (!NoSeaDsaAA) {
    pass_manager.add(seadsa::createSeaDsaAAWrapperPass());
    // -- make available through AAResultsWrapperPass via ExternalAAWrapperPass
    pass_manager.add(llvm::createExternalAAWrapperPass(
        [](llvm::Pass &P, llvm::Function &, llvm::AAResults &AAR) {
          if (auto *WrapperPass =
                  P.getAnalysisIfAvailable<seadsa::SeaDsaAAWrapperPass>())
            AAR.addAAResult(WrapperPass->getResult());
        }));
  }

  // XXX Comment other alias analyses for now to make sure that we
  // XXX get to SeaDsa one first. Enable them once there are
  // XXX tests that exercise alias analysis
  // pass_manager.add(llvm::createTypeBasedAAWrapperPass());
  // pass_manager.add(llvm::createScopedNoAliasAAWrapperPass());

  // compute mod-ref information about functions . Good idea to (re)run it after
  // indirect calls have been resolved See comments in PassManagerBuilder.cpp on
  // how it interacts with Function passes
  // pass_manager.add(llvm::createGlobalsAAWrapperPass());
  // ==--== End of Alias Analysis Passes ==--==/

  if (RunShadowMem) {
    pass_manager.add(seadsa::createShadowMemPass());
  } else {
    if (MemDot) { pass_manager.add(seadsa::createDsaPrinterPass()); }

    if (MemViewer) { pass_manager.add(seadsa::createDsaViewerPass()); }

    if (seadsa::PrintDsaStats && !MemDot && !MemViewer) {
      pass_manager.add(seadsa::createDsaPrintStatsPass());
    }

    if (seadsa::PrintCallGraphStats) {
      pass_manager.add(seadsa::createDsaPrintCallGraphStatsPass());
    }

    if (CallGraphDot) {
      pass_manager.add(seadsa::createDsaCallGraphPrinterPass());
    }

    if (AAEval || AAPairs) {
      // Module-pass barrier: without it the legacy pass manager batches the
      // preceding function passes (RemovePtrToInt, ...) with AAEval, so the
      // first alias query builds the whole-module DSA graphs while later
      // functions are still untransformed.
      pass_manager.add(llvm::createBarrierNoopPass());
      if (AAEval) pass_manager.add(llvm::createAAEvalPass());
      if (AAPairs) pass_manager.add(new AliasPairsPass());
    }

    if (!MemDot && !MemViewer && !seadsa::PrintDsaStats &&
        !seadsa::PrintCallGraphStats && !CallGraphDot && !AAEval && !AAPairs) {
      llvm::errs() << "No option selected: choose one option between "
                   << "{sea-dsa-dot, sea-dsa-viewer, sea-dsa-stats, "
                   << "sea-dsa-callgraph-dot, sea-dsa-callgraph-stats, "
                      "sea-dsa-aa-eval, sea-dsa-aa-pairs}\n";
    }
  }

  if (!AsmOutputFilename.empty())
    pass_manager.add(createPrintModulePass(asmOutput->os()));

  pass_manager.run(*module.get());

  if (!AsmOutputFilename.empty()) asmOutput->keep();

  return 0;
}
