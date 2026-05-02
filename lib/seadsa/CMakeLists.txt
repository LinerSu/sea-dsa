if (SEA_BUILD_IDSA)
  set(SEADSA_GRAPH_SOURCE Interval_Graph.cc)
else()
  set(SEADSA_GRAPH_SOURCE Classic_Graph.cc)
endif()

message(STATUS "Using ${SEADSA_GRAPH_SOURCE} as the graph implementation")

add_llvm_library (SeaDsaAnalysis DISABLE_LLVM_LINK_LLVM_DYLIB
  ${SEADSA_GRAPH_SOURCE}
  FieldType.cc
  DsaLocal.cc
  DsaGlobal.cc
  DsaCallSite.cc
  Cloner.cc
  DsaInfo.cc
  DsaStats.cc
  Mapper.cc
  DsaBottomUp.cc
  DsaTopDown.cc
  DsaAnalysis.cc
  DsaPrinter.cc
  CallGraphWrapper.cc
  TypeUtils.cc
  Debug.cc
  NameValues.cc
  RemovePtrToInt.cc
  AllocWrapInfo.cc
  AllocSiteInfo.cc
  DsaAllocSite.cc
  DsaCompleteCallGraph.cc
  DsaCallGraphPrinter.cc
  DsaColor.cc
  ShadowMem.cc
  SeaMemorySSA.cc
  SeaDsaAliasAnalysis.cc
  DsaLibFuncInfo.cc
  )

if (SEA_BUILD_IDSA)
  target_compile_definitions(SeaDsaAnalysis PUBLIC SEA_BUILD_IDSA)
endif()
