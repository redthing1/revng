//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <unordered_map>

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include "revng/ABI/ModelHelpers.h"
#include "revng/HeadersGeneration/PTMLHeaderBuilder.h"
#include "revng/Model/Binary.h"
#include "revng/Model/Helpers.h"
#include "revng/Model/IRHelpers.h"
#include "revng/Model/TypeDefinition.h"
#include "revng/Pipeline/Location.h"
#include "revng/Pipes/Ranks.h"
#include "revng/Support/Assert.h"
#include "revng/Support/Debug.h"
#include "revng/Support/YAMLTraits.h"

static Logger Log{ "model-to-header" };

bool ptml::HeaderBuilder::printModelHeader(const llvm::Module *ModuleForCallSiteOverrides) {

  auto Scope = B.getScopeTag(ptml::tags::Div);

  std::string Includes = B.getPragmaOnce() + "\n"
                         + B.getIncludeAngle("stdint.h")
                         + B.getIncludeAngle("stdbool.h")
                         + B.getIncludeQuote("primitive-types.h")
                         + B.getIncludeQuote("attributes.h") + "\n";
  B.append(std::move(Includes));

  if (not Configuration.PostIncludeSnippet.empty())
    B.append(Configuration.PostIncludeSnippet + "\n"s);

  std::string Defines = B.getDirective(CBuilder::Directive::IfNotDef) + " "
                        + B.getNullTag() + "\n"
                        + B.getDirective(CBuilder::Directive::Define) + " "
                        + B.getNullTag() + " (" + B.getZeroTag() + ")\n"
                        + B.getDirective(CBuilder::Directive::EndIf) + "\n";
  B.append(std::move(Defines));

  if (not B.Binary.TypeDefinitions().empty()) {
    auto Foldable = B.getScopeTag(CBuilder::Scopes::TypeDeclarations,
                                  /* Newline = */ true);

    B.appendLineComment("\\defgroup Type definitions");
    B.appendLineComment("\\{");
    B.append("\n");

    B.printTypeDefinitions();

    B.append("\n");
    B.appendLineComment("\\}");
    B.append("\n");
  }

  if (not B.Binary.Functions().empty()) {
    auto Foldable = B.getScopeTag(CBuilder::Scopes::FunctionDeclarations,
                                  /* Newline = */ true);

    // If requested, try to pick a more precise prototype for functions that
    // still use the model default prototype by looking at call-site prototype
    // metadata in the current LLVM module.
    //
    // This is primarily meant to make the "recompilable archive" artifact more
    // self-consistent: EnforceABI injects call-site prototypes that can be
    // narrower than the model's DefaultPrototype.
    std::unordered_map<MetaAddress, const model::TypeDefinition *> Overrides;
    std::set<MetaAddress> Conflicts;
    const model::TypeDefinition *DefaultPrototype = B.Binary.defaultPrototype();
    if (ModuleForCallSiteOverrides != nullptr and DefaultPrototype != nullptr) {
      for (const llvm::Function &F : *ModuleForCallSiteOverrides) {
        for (const llvm::BasicBlock &BB : F) {
          for (const llvm::Instruction &I : BB) {
            auto *Call = llvm::dyn_cast<llvm::CallInst>(&I);
            if (Call == nullptr)
              continue;

            // We only override with C ABI prototypes.
            const model::TypeDefinition *CallSiteFT = getCallSitePrototype(B.Binary, Call);
            if (not llvm::isa<model::CABIFunctionDefinition>(CallSiteFT))
              continue;

            llvm::Function *Callee = getCalledFunction(Call);
            if (Callee == nullptr)
              continue;

            const model::Function *ModelF = llvmToModelFunction(B.Binary, *Callee);
            if (ModelF == nullptr)
              continue;

            // Only override functions still using the default prototype.
            const model::TypeDefinition *CurrentFT = B.Binary.prototypeOrDefault(ModelF->prototype());
            if (CurrentFT != DefaultPrototype)
              continue;

            MetaAddress Entry = ModelF->Entry();
            auto It = Overrides.find(Entry);
            if (It == Overrides.end()) {
              Overrides.emplace(Entry, CallSiteFT);
            } else if (It->second->key() != CallSiteFT->key()) {
              Conflicts.insert(Entry);
            }
          }
        }
      }
    }

    B.appendLineComment("\\defgroup Functions");
    B.appendLineComment("\\{");
    B.append("\n");

    for (const model::Function &MF : B.Binary.Functions()) {
      if (Configuration.FunctionsToOmit.contains(MF.Entry()))
        continue;

      const model::TypeDefinition *FTPtr = B.Binary.prototypeOrDefault(MF.prototype());
      if (DefaultPrototype != nullptr and FTPtr == DefaultPrototype) {
        auto It = Overrides.find(MF.Entry());
        if (It != Overrides.end() and not Conflicts.contains(MF.Entry()))
          FTPtr = It->second;
      }
      const auto &FT = *FTPtr;
      if (B.Configuration.TypesToOmit.contains(FT.key()))
        continue;

      if (Log.isEnabled()) {
        helpers::BlockComment CommentScope = B.getBlockCommentScope();
        B.append("Emitting a model function `" + B.NameBuilder.name(MF) + "`:\n"
                 + MF.toString() + "Its prototype is:\n" + FT.toString());
      }

      B.printFunctionPrototype(FT, MF, /* SingleLine = */ false);
      B.append(";\n\n");
    }

    B.appendLineComment("\\}");
    B.append("\n");
  }

  if (not B.Binary.ImportedDynamicFunctions().empty()) {
    auto F = B.getScopeTag(CBuilder::Scopes::DynamicFunctionDeclarations,
                           /* Newline = */ true);

    B.appendLineComment("\\defgroup Imported dynamic functions");
    B.appendLineComment("\\{");
    B.append("\n");

    for (const auto &MF : B.Binary.ImportedDynamicFunctions()) {
      const auto &FT = *B.Binary.prototypeOrDefault(MF.prototype());
      if (B.Configuration.TypesToOmit.contains(FT.key()))
        continue;

      if (Log.isEnabled()) {
        helpers::BlockComment CommentScope = B.getBlockCommentScope();
        B.append("Emitting a dynamic function `" + B.NameBuilder.name(MF)
                 + "`:\n" + MF.toString() + "Its prototype is:\n"
                 + FT.toString());
      }

      B.printFunctionPrototype(FT, MF, /* SingleLine = */ false);
      B.append(";\n\n");
    }

    B.appendLineComment("\\}");
    B.append("\n");
  }

  if (not B.Binary.Segments().empty()) {
    auto Foldable = B.getScopeTag(CBuilder::Scopes::SegmentDeclarations,
                                  /* Newline = */ true);

    B.appendLineComment("/// \\defgroup Segments");
    B.appendLineComment("/// \\{");

    for (const model::Segment &Segment : B.Binary.Segments())
      B.printSegmentType(Segment);

    B.append("\n");
    B.appendLineComment("\\}");
    B.append("\n");
  }

  return true;
}
