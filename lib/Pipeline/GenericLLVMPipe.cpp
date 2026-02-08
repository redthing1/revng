/// A llvm pipe is a pipe that operates on a llvm container.

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/PassRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"

#include "revng/Pipeline/GenericLLVMPipe.h"
#include "revng/Pipeline/LLVMContainer.h"
#include "revng/Support/IRHelpers.h"

using namespace std;
using namespace llvm;
using namespace pipeline;
using namespace cl;

void O2Pipe::registerPasses(llvm::legacy::PassManager &Manager) {
  StringMap<llvm::cl::Option *> &Options(getRegisteredOptions());
  getOption<bool>(Options, "disable-machine-licm")->setInitialValue(true);

  PassBuilder Builder;
  Builder.buildPerModuleDefaultPipeline(OptimizationLevel::O2);
}

namespace {

// System LLVM builds on some distributions disable the legacy pass registry for
// built-in passes. revng's pipelines still reference these pass names, so we
// execute them through the new pass manager.
class RunNewPMPipelinePass : public llvm::ModulePass {
public:
  static char ID;

private:
  std::string PipelineText;

public:
  explicit RunNewPMPipelinePass(std::string PipelineText) :
    llvm::ModulePass(ID), PipelineText(std::move(PipelineText)) {}

  bool runOnModule(llvm::Module &M) override {
    using namespace llvm;

    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager MPM;
    if (Error Err = PB.parsePassPipeline(MPM, PipelineText)) {
      std::string Message = "Could not parse LLVM pass pipeline '" + PipelineText
                            + "': " + toString(std::move(Err));
      report_fatal_error(StringRef(Message), false);
    }

    MPM.run(M, MAM);
    return true;
  }
};

char RunNewPMPipelinePass::ID = 0;

} // namespace

bool PureLLVMPassWrapper::passExists(llvm::StringRef PassName) {
  if (llvm::PassRegistry::getPassRegistry()->getPassInfo(PassName))
    return true;

  llvm::PassBuilder PB;
  llvm::ModulePassManager MPM;
  if (llvm::Error Err = PB.parsePassPipeline(MPM, PassName)) {
    llvm::consumeError(std::move(Err));
    return false;
  }

  return true;
}

std::unique_ptr<LLVMPassWrapperBase> PureLLVMPassWrapper::clone() const {
  return std::make_unique<PureLLVMPassWrapper>(*this);
}

class UpdateContract : public llvm::ModulePass {
public:
  static char ID;

public:
  Context &TheContext;
  llvm::ArrayRef<ContractGroup> Contract;
  ContainerToTargetsMap &Requested;
  llvm::ArrayRef<std::string> ContainersName;

public:
  UpdateContract(Context &TheContext,
                 llvm::ArrayRef<ContractGroup> Contract,
                 ContainerToTargetsMap &Requested,
                 const std::string &ContainersName) :
    llvm::ModulePass(ID),
    TheContext(TheContext),
    Contract(Contract),
    Requested(Requested),
    ContainersName(ContainersName) {}

public:
  bool runOnModule(llvm::Module &Module) override {
    for (auto &Entry : Contract)
      Entry.deduceResults(TheContext, Requested, { ContainersName });
    return false;
  }
};

template<typename T>
using RP = RegisterPass<T>;

char UpdateContract::ID = '_';

static RP<UpdateContract>
  X("advance-contract", "Advance pipeline contracts", true, false);

void GenericLLVMPipe::run(ExecutionContext &EC, LLVMContainer &Container) {
  llvm::legacy::PassManager Manager;
  Manager.add(new LoadExecutionContextPass(&EC, Container.name()));
  using ElementType = std::unique_ptr<LLVMPassWrapperBase>;
  for (const ElementType &Element : Passes) {
    Element->registerPasses(Manager);
    Manager.add(new UpdateContract(EC.getContext(),
                                   Element->getContract(),
                                   EC.getCurrentRequestedTargets(),
                                   Container.name()));
  }
  Manager.run(Container.getModule());
}

void PureLLVMPassWrapper::registerPasses(llvm::legacy::PassManager &Manager) {
  if (auto *PassInfo = llvm::PassRegistry::getPassRegistry()->getPassInfo(PassName)) {
    Manager.add(PassInfo->createPass());
    return;
  }

  Manager.add(new RunNewPMPipelinePass(PassName));
}
