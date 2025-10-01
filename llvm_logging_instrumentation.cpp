// LLVM 14+ pass plugin: instrumentation + def-use graph + CFG dump
// Файлы в этом документе:
// 1) llvm_logging_instrumentation.cpp  - плагин
// 2) logger_runtime.cpp                - простая реализация логера, записывает в файл log.txt
// 3) Makefile                          - сборка плагина и рантайма

#include <llvm/IR/PassManager.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/Support/raw_ostream.h>

#include "llvm/IR/CFG.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"

#include "llvm/IR/Instructions.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"

#include <fstream>
#include <unordered_map>
#include <vector>
#include <string>

using namespace llvm;
using u64 = uint64_t;

namespace {

struct InstrumentPass : public PassInfoMixin<InstrumentPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {

        LLVMContext &Ctx = M.getContext();

        // Функция логгера
        FunctionCallee LogFunc = M.getOrInsertFunction(
            "__log_instr",
            FunctionType::get(Type::getVoidTy(Ctx),
                              {Type::getInt8PtrTy(Ctx),
                               Type::getInt64Ty(Ctx),
                               Type::getInt8PtrTy(Ctx),
                               Type::getInt8PtrTy(Ctx)},
                              false));

        std::error_code EC;

        // def-use граф
        raw_fd_ostream DefUseFile("defuse.dot", EC, sys::fs::OF_Text);
        DefUseFile << "digraph DefUse {\n";

        auto getInstrLabel = [](const Instruction *I) {
            std::string Str;
            raw_string_ostream RSO(Str);
            I->print(RSO);
            return RSO.str();
        };

        for (Function &F : M) {
            if (F.isDeclaration()) continue;

            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    if (!I.getType()->isVoidTy()) {
                        std::string FromLabel = getInstrLabel(&I);
                        for (auto *U : I.users()) {
                            if (auto *UI = dyn_cast<Instruction>(U)) {
                                std::string ToLabel = getInstrLabel(UI);
                                DefUseFile << "  \"" << FromLabel << "\" -> \"" << ToLabel << "\";\n";
                            }
                        }
                    }
                }
            }
        }
        DefUseFile << "}\n";

        // CFG
        raw_fd_ostream CFGFile("cfg.dot", EC, sys::fs::OF_Text);
        CFGFile << "digraph InstrCFG {\n";

        for (Function &F : M) {
            if (F.isDeclaration()) continue;

            // Для ссылок: первая инструкция блока
            std::unordered_map<BasicBlock*, Instruction*> FirstInstr;
            for (BasicBlock &BB : F) {
                if (!BB.empty())
                    FirstInstr[&BB] = &*BB.begin();
            }

            for (BasicBlock &BB : F) {
                Instruction *Prev = nullptr;
                for (Instruction &I : BB) {
                    std::string InstrLabel = getInstrLabel(&I);
                    CFGFile << "  \"" << &I << "\" [label=\"" << InstrLabel << "\"];\n";

                    if (Prev) {
                        CFGFile << "  \"" << Prev << "\" -> \"" << &I << "\";\n";
                    }
                    Prev = &I;
                }

                // переходы из последней инструкции
                if (!BB.empty()) {
                    Instruction *Last = BB.getTerminator();
                    if (auto *Br = dyn_cast<BranchInst>(Last)) {
                        for (unsigned i = 0; i < Br->getNumSuccessors(); i++) {
                            BasicBlock *Succ = Br->getSuccessor(i);
                            if (FirstInstr.count(Succ)) {
                                CFGFile << "  \"" << Last << "\" -> \"" << FirstInstr[Succ] << "\";\n";
                            }
                        }
                    } else if (auto *Sw = dyn_cast<SwitchInst>(Last)) {
                        for (auto &Case : Sw->cases()) {
                            BasicBlock *Succ = Case.getCaseSuccessor();
                            if (FirstInstr.count(Succ)) {
                                CFGFile << "  \"" << Last << "\" -> \"" << FirstInstr[Succ] << "\";\n";
                            }
                        }
                        // default
                        BasicBlock *Def = Sw->getDefaultDest();
                        if (FirstInstr.count(Def)) {
                            CFGFile << "  \"" << Last << "\" -> \"" << FirstInstr[Def] << "\";\n";
                        }
                    }
                }
            }
        }

        CFGFile << "}\n";

        // Инструментирование
        for (Function &F : M) {
            if (F.isDeclaration()) continue;

            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {

                    // Пропускаю терминаторы и PHI
                    if (I.isTerminator() || isa<PHINode>(&I))
                        continue;

                    if (I.getType()->isVoidTy())
                        continue;
                    if (isa<SExtInst>(&I))
                        continue;

                    Instruction *Next = I.getNextNode();
                    IRBuilder<> Builder(Ctx);
                    if (Next)
                        Builder.SetInsertPoint(Next);
                    else
                        Builder.SetInsertPoint(&BB); // конец блока

                    // Текст инструкции
                    std::string InstrStr;
                    raw_string_ostream RSO(InstrStr);
                    I.print(RSO);
                    RSO.flush();
                    Value *InstrName = Builder.CreateGlobalStringPtr(InstrStr);
                    // ID инструкции
                    Value *Id = Builder.getInt32(reinterpret_cast<uintptr_t>(&I) & 0xFFFFFFFF);

                    // Значение инструкции
                    Value *Val = nullptr;
                    if (I.getType()->isIntegerTy())
                        Val = Builder.CreateSExtOrTrunc(&I, Type::getInt64Ty(Ctx));
                    else if (I.getType()->isFloatingPointTy())
                        Val = Builder.CreateFPToSI(&I, Type::getInt64Ty(Ctx));
                    else
                        Val = ConstantInt::get(Type::getInt64Ty(Ctx), 0);

                    Value *FuncName = Builder.CreateGlobalStringPtr(F.getName());
                    Value *BBName   = Builder.CreateGlobalStringPtr(BB.getName());

                    Builder.CreateCall(LogFunc, {InstrName, Val, FuncName, BBName});
                }
            }
        }

        errs() << "InstrumentPass: defuse.dot, cfg.dot generated, module instrumented.\n";
        return PreservedAnalyses::none();
    }
};

}

// Регистрация плагина
extern "C" ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "instrumentation-plugin", "v0", [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name, ModulePassManager &MPM, ArrayRef<PassBuilder::PipelineElement>) {
                if (Name == "instrument-pass") {
                    MPM.addPass(InstrumentPass());
                    return true;
                }
                return false;
            }
        );
    }};
}
