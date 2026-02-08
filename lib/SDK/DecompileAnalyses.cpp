//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "DecompileInternal.h"

#include "revng/Pipeline/AnalysesList.h"
#include "revng/Pipes/PipelineManager.h"
#include "revng/Support/Error.h"

namespace revng::sdk::detail {

llvm::Error runAnalysesList(revng::pipes::PipelineManager &Manager,
                            llvm::StringRef Name,
                            llvm::ArrayRef<std::string> FunctionEntries,
                            bool RestrictToSelectedFunctions) {
  auto &Runner = Manager.getRunner();
  if (not Runner.hasAnalysesList(Name)) {
    return revng::createError("analysis list '%s' is not available",
                              Name.str().c_str());
  }

  pipeline::AnalysesList AL = Runner.getAnalysesList(Name);

  // Default behavior: use the upstream semantics for AnalysesLists, i.e. run
  // each analysis on all its accepted targets (whole-program).
  if (not RestrictToSelectedFunctions or FunctionEntries.empty()) {
    pipeline::TargetInStepSet InvMap;
    auto MaybeDiffs = Manager.runAnalyses(AL, InvMap);
    if (not MaybeDiffs)
      return MaybeDiffs.takeError();
    return llvm::Error::success();
  }

  // Selected-functions behavior: run the list analysis-by-analysis, and try to
  // restrict function-scoped analyses (kinds with depth 1) to the selected
  // function targets.
  for (const pipeline::AnalysisReference &Ref : AL) {
    const pipeline::Step &Step = Runner.getStep(Ref.getStepName());
    const pipeline::AnalysisWrapper &Analysis =
      Step.getAnalysis(Ref.getAnalysisName());

    pipeline::ContainerToTargetsMap Map;
    const std::vector<std::string> &Containers =
      Analysis->getRunningContainersNames();
    for (size_t I = 0; I < Containers.size(); ++I) {
      for (const pipeline::Kind *K : Analysis->getAcceptedKinds(I)) {
        if (K == nullptr)
          continue;

        if (K->depth() == 1) {
          auto MaybeTargets = computeSelectedTargetsForKind(Manager,
                                                           *K,
                                                           FunctionEntries);
          if (not MaybeTargets)
            return MaybeTargets.takeError();
          Map.add(Containers[I], MaybeTargets.get());
        } else {
          Map.add(Containers[I],
                  pipeline::TargetsList::allTargets(Manager.context(), *K));
        }
      }
    }

    pipeline::TargetInStepSet InvMap;
    auto MaybeDiff = Manager.runAnalysis(Ref.getAnalysisName(),
                                         Step.getName(),
                                         Map,
                                         InvMap);
    if (not MaybeDiff)
      return MaybeDiff.takeError();
  }

  return llvm::Error::success();
}

} // namespace revng::sdk::detail

