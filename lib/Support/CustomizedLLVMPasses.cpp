//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"

using namespace llvm;

//
// Customized version of SimplifyCFG, with a different set of default options.
//

class SimplifyCFGWithHoistAndSinkPass : public FunctionPass {
public:
  static char ID;
  SimplifyCFGWithHoistAndSinkPass() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {}

  bool runOnFunction(Function &F) override {
    FunctionPassManager FPM;
    FPM.addPass(SimplifyCFGPass(SimplifyCFGOptions()
                                  .convertSwitchRangeToICmp(true)
                                  .hoistCommonInsts(true)
                                  .sinkCommonInsts(true)));

    FunctionAnalysisManager FAM;

    PassBuilder PB;
    PB.registerFunctionAnalyses(FAM);

    FPM.run(F, FAM);
    return true;
  }
};

char SimplifyCFGWithHoistAndSinkPass::ID;

using RegisterCustomSimplifyCFG = RegisterPass<SimplifyCFGWithHoistAndSinkPass>;
static RegisterCustomSimplifyCFG
  RSCG("simplify-cfg-with-hoist-and-sink", "", false, false);

//
// Customized version of SROA, to disable flattening of arrays
//

class SROANoArraysPass : public FunctionPass {
public:
  static char ID;

  SROANoArraysPass() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {}

  bool runOnFunction(Function &F) override {
    FunctionPassManager FPM;
    FPM.addPass(SROAPass(SROAOptions::PreserveCFG));

    FunctionAnalysisManager FAM;

    PassBuilder PB;
    PB.registerFunctionAnalyses(FAM);

    FPM.run(F, FAM);
    return true;
  }
};

char SROANoArraysPass::ID = 0;

static RegisterPass<SROANoArraysPass> RSROA("sroa-noarrays", "", false, false);

//
// Customized InstructionCombiningPass, to disable flattening of arrays
//

class InstCombineNoArrays : public InstructionCombiningPass {
public:
  static char ID;

  InstCombineNoArrays() : InstructionCombiningPass() {}

  bool runOnFunction(Function &F) override {
    return InstructionCombiningPass::runOnFunction(F);
  }
};

char InstCombineNoArrays::ID;

using RegisterInstCombine = RegisterPass<InstCombineNoArrays>;
static RegisterInstCombine RIC("instcombine-noarrays", "", false, false);
