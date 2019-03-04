//    Copyright (c) 2019 University of Paderborn 
//                         (Gavin Vaz <gavin.vaz@uni-paderborn.de>,
//                          Heinrich Riebler <heinrich.riebler@uni-paderborn.de>)

//    Permission is hereby granted, free of charge, to any person obtaining a copy
//    of this software and associated documentation files (the "Software"), to deal
//    in the Software without restriction, including without limitation the rights
//    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//    copies of the Software, and to permit persons to whom the Software is
//    furnished to do so, subject to the following conditions:

//    The above copyright notice and this permission notice shall be included in
//    all copies or substantial portions of the Software.

//    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//    THE SOFTWARE.

#include "scopdependency.h"
#include "../../common/math_parser.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/DependenceAnalysis.h"

#include "polly/CodeGen/IslAst.h"
#include "polly/CodeGen/IslNodeBuilder.h"
#include "polly/CodeGen/Utils.h"
#include "polly/DependenceInfo.h"
#include "polly/LinkAllPasses.h"
#include "polly/ScopInfo.h"
#include "polly/Support/ScopHelper.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Debug.h"

// #include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Support/CommandLine.h"
#if LLVM_VERSION == 3 && LLVM_MINOR_VERSION < 5
#include "llvm/Support/InstIterator.h"
#else
#include "llvm/IR/InstIterator.h"
#endif
#include "../htropclient.h"

#include <iostream>
#include <list>

char htrop::ScopDependency::ID = 0;

static RegisterPass < htrop::ScopDependency > X("scopdependency", "adds ScopDependency pass to the function", false, false);

FunctionPass *htrop::createScopDependencyPass() {
    return new ScopDependency();
}

/**
 *  Calculate the position of this argument in parent function. 
 *    -1, if not in parent argument.
 */
int htrop::ScopDependency::calcPositionInParent(Value * value_to_check) {
    // Default -1, if not in parent.
    int retPos = -1;

    int positionRunner = 0;

    for (Function::arg_iterator arg_in_parent = funcParent->arg_begin(), ae = funcParent->arg_end(); arg_in_parent != ae; ++arg_in_parent) {
        Value *value_in_parent = &*arg_in_parent;

        if (value_to_check == value_in_parent) {
            retPos = positionRunner;
            break;
        }

        positionRunner++;
    }

    return retPos;
}

// Updates buffer type of argument in root, depending on the children
void htrop::ScopDependency::updateRootBufferWithChildren(ScopCallDS * rootScopCall, ScopCallFnArg * root_arg, int root_pos) {

    ScopExp *scopAST;

    // Iterate through children. 
    for (int i = root_pos + 1; i < scopCallList->size(); i++) {
        ScopCallDS *childScopCall = scopCallList->at(i);

        // Iterate through children args and look for match. 
        for (unsigned i = 0; i < childScopCall->scopCallFunctonArgs.size(); ++i) {
            ScopCallFnArg *child_arg = childScopCall->scopCallFunctonArgs[i];

            // Match here. Update root type.
            if (root_arg->value == child_arg->value) {

                // Update type.
                switch (root_arg->typeOptimized) {
                case UNKNOWN:
                    // Helper. Nothing to do.
                    break;
                case IN:

                    switch (child_arg->typeOptimized) {
                    case UNKNOWN:
                        // Helper. Nothing to do.
                        break;
                    case IN:
                        // Diagonal. Nothing to do.
                        break;
                    case OUT:
                        // Update top.
                        root_arg->typeOptimized = IN_OUT;
                        break;
                    case IN_OUT:
                        // Update top.
                        root_arg->typeOptimized = IN_OUT;
                        break;
                    case TMP:
                        // Helper. Nothing to do.
                        break;
                    }

                    break;
                case OUT:

                    switch (child_arg->typeOptimized) {
                    case UNKNOWN:
                        // Nothing to do.
                        break;
                    case IN:
                        // Update bottom. 
                        // child_arg->typeOptimized = OUT;
                        break;
                    case OUT:
                        // Diagonal. Nothing to do.
                        break;
                    case IN_OUT:
                        // Update bottom. 
                        // child_arg->typeOptimized = OUT;
                        break;
                    case TMP:
                        // Helper. Nothing to do.
                        break;
                    }

                    break;
                case IN_OUT:

                    switch (child_arg->typeOptimized) {
                    case UNKNOWN:
                        // Nothing to do.
                        break;
                    case IN:
                        // Update bottom.
                        // child_arg->typeOptimized = OUT;
                        break;
                    case OUT:
                        // Update bottom.
                        // child_arg->typeOptimized = OUT;
                        break;
                    case IN_OUT:
                        // Diagonal. Nothing to do.
                        break;
                    case TMP:
                        // Helper. Nothing to do.
                        break;
                    }

                    break;
                case TMP:
                    // Helper. Nothing to do.
                    break;
                }

                if (root_arg->dimension_maxStr.size() != 0 && child_arg->dimension_maxStr.size() != 0) {
                    // Update dimension_maxStr.
                    // Get scopLoopInfo for child/root:
                    //   - ScopCallDS --> ScopDS --> scopLoopinfo
                    std::map < std::string, ScopDS * >::iterator scopListItroot;
                    scopListItroot = scopList->find(rootScopCall->scopID);
                    ScopDS *rootScop = scopListItroot->second;

                    assert(scopListItroot != scopList->end());

                    std::map < std::string, ScopDS * >::iterator scopListItchild;
                    scopListItchild = scopList->find(childScopCall->scopID);
                    ScopDS *childScop = scopListItchild->second;

                    assert(scopListItchild != scopList->end());

                    // ### Analyze dimension_maxStr. It looks like
                    //   root:   1541 + 1536(-4 + rows) + 3(-4 + cols)
                    //   child:  2 + 1536(-1 + rows) + 3(-1 + cols)
                    // Here we will remove i0, and i1 and replace them with 0.
                    std::string dimStr_noVars_root = root_arg->dimension_maxStr.at(0);

                    // Remove variables root.
                    for (auto stmDim: rootScop->scopLoopInfo) {

                        int r_start = dimStr_noVars_root.find(stmDim->nameStr);

                        while (r_start > -1) {
                            std::string rpl_str = "(" + std::to_string(0) + ")";
                            dimStr_noVars_root.replace(r_start, stmDim->nameStr.length(), rpl_str);
                            r_start = dimStr_noVars_root.find(stmDim->nameStr);
                        }
                    }

                    // Crease AST for expression and evaluate it.
                    scopAST = createASTfromScopString(dimStr_noVars_root);
                    long val_root = scopAST->eval();

                    scopAST->cln();

                    std::string dimStr_noVars_child = child_arg->dimension_maxStr.at(0);

                    // Remove variables child.
                    for (auto stmDim: childScop->scopLoopInfo) {

                        int r_start = dimStr_noVars_child.find(stmDim->nameStr);

                        while (r_start > -1) {
                            std::string rpl_str = "(" + std::to_string(0) + ")";
                            dimStr_noVars_child.replace(r_start, stmDim->nameStr.length(), rpl_str);
                            r_start = dimStr_noVars_child.find(stmDim->nameStr);
                        }
                    }

                    // Crease AST for expression and evaluate it.
                    scopAST = createASTfromScopString(dimStr_noVars_child);
                    long val_child = scopAST->eval();

                    scopAST->cln();

#ifdef HTROP_SCOP_DEBUG
                    errs().indent(12) << " LOOOKING AT ROOT \"" << root_arg->dimension_maxStr.at(0) << "\" --> without VARS: \"" << dimStr_noVars_root << "\" = " << val_root << "\n";
                    errs().indent(12) << " LOOOKING AT CHILD \"" << child_arg->dimension_maxStr.at(0) << "\" --> without VARS: \"" << dimStr_noVars_child << "\" = " << val_child << "\n";
#endif

                    if (val_child > val_root) {
#ifdef HTROP_SCOP_DEBUG
                        errs().indent(12) << "   ---> CHILD IS BIGGER, UPDATE ROOT!!\n";
#endif

                        root_arg->dimension_maxStr = child_arg->dimension_maxStr;
                    }
                }

                return;
            }

        }
    }
}

bool htrop::ScopDependency::runOnFunction(Function & F) {

#ifdef HTROP_SCOP_DEBUG
    errs() << "   ScopDependency analysis on Call Wrapper Function \"" << F.getName() << "\" \n";
#endif
    funcParent = &F;

    // Basic idea:
    //   1) iterate top-down through parent function: Find call instructions to Scop functions
    //   2) Analyse the calls and link them to the static scop information. This creates scopCallList
    //   3) Create sub-scopCallList, if the chain of calls gets interrupted. 
    //        Force data migration back to the host to not spoil the contents.
    //   4) iterate bottom-up through the scopCallList and update the previous data with the new information.

#ifdef HTROP_SCOP_DEBUG
    errs() << "     Step 1: Looking for callInst to Scop functions. \n";
#endif

    // Flags to help keep the state while parsing. Preparation to enable 
    // interrupted chains.
    bool flag_new_chain = false;
    int runner_chain_id = 0;
    
    // Iterate over instructions of the function.
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        // Look for calls.
        if (isa < llvm::CallInst > (&*I)) {
            llvm::CallInst * callInst = dyn_cast < llvm::CallInst > (&*I);
            Function *calledF = callInst->getCalledFunction();

            // Look for calls to functions.
            if (calledF) {
#ifdef HTROP_SCOP_DEBUG
                errs() << "       - A: Found call instruction. Does it point to a Scop? ... \n";
#endif

                // Does it point to a Scop?
                std::map < std::string, ScopDS * >::iterator scopListIt;
                scopListIt = scopList->find(calledF->getName());

                if (scopListIt != scopList->end()) {
                    // Yes.
                    flag_new_chain = true;
                  
                    // Store in sib-list. 
                    ScopDS *scop = scopListIt->second;

#ifdef HTROP_SCOP_DEBUG
                    errs() << "         ... Yes. Current chain-ID = \"" << runner_chain_id << "\". It points to \"" << scop->scopFunction->getName() << "\". We analyse it ... \n";
#endif

                    ScopCallDS *scopCall = new ScopCallDS();

                    scopCall->scopID = scop->scopFunction->getName();
                    scopCall->callInst = callInst;

                    std::vector < ScopCallFnArg * >scopCallFunctonArgs;

#ifdef HTROP_SCOP_DEBUG
                    errs() << "         - B: Look at each argument of the \"" << callInst->getNumOperands() << "\" args of call ... \n";
                    errs() << "           - C: calculate position in parent ...  \n";
#endif

                    // This approach uses use-def chains: 
                    //   http://llvm.org/docs/ProgrammersManual.html#iterating-over-def-use-use-def-chains
                    // What Values are used by the callInst.
                    //   Order is: each argument as Value first, and the Function declaration that is called last.
                    for (Use & U: callInst->operands()) {
                        // Skip the Function declaration.
                        if (!isa < llvm::Function > (U)) {
                            ScopCallFnArg *newArg = new ScopCallFnArg();

                            newArg->value = U.get();
                            // newArg->value->dump();

                            newArg->positionInParent = calcPositionInParent(newArg->value);

                            scopCallFunctonArgs.push_back(newArg);
                        }
                    }
#ifdef HTROP_SCOP_DEBUG
                    errs() << "           - D: Get buffer type information from Scop Info ...  \n";
#endif
                    // Get buffer information from scop. 
                    for (unsigned i = 0; i < scop->scopFunctonArgs.size(); ++i) {
                        ScopFnArg *scopArg = scop->scopFunctonArgs[i];
                        ScopCallFnArg *scopCallArg = scopCallFunctonArgs[i];

                        // Set Scop type as default. We optimize it later.
                        scopCallArg->typeOptimized = scopArg->type;
                        // Set Scop size string as default. We optimize it later.
                        scopCallArg->dimension_maxStr = scopArg->dimension_maxStr;
                    }

#ifdef HTROP_SCOP_DEBUG
                    errs() << "           - Current results ...  \n";

                    for (unsigned i = 0; i < scopCallFunctonArgs.size(); ++i) {
                        ScopCallFnArg *ha = scopCallFunctonArgs[i];

                        ha->dump(13);
                    }
#endif

                    scopCall->scopCallFunctonArgs = scopCallFunctonArgs;

                    scopCallList->push_back(scopCall);
                }
                else {
                  // No. 
                  
                  // If chain was open, close the last chain to force data migration back to the host.
                  if(flag_new_chain == true) {
                    flag_new_chain = false;
                    
                    // Generate next chain ID.
                    runner_chain_id += 1;
                  }
                  
#ifdef HTROP_SCOP_DEBUG
                  errs() << "         ... No. It points to \"" << calledF->getName() << "\". SKIPPED. Current chain-ID = \"" << runner_chain_id << "\" \n";
#endif
                }
            }

        }
    }
    
    // Heuristic to check for interrupts.
    if(runner_chain_id > 1) {
      flag_chain_interrupt = true;
    }

#ifdef HTROP_SCOP_DEBUG
    errs() << "       --- RESULTS BEFORE UPDATE, flag_chain_interrupt = \"" << flag_chain_interrupt << "\" ---  \n";

    for (auto it = scopCallList->begin(); it != scopCallList->end(); ++it) {
        ScopCallDS *scopCall = (*it);

        errs() << "           - function\"" << scopCall->scopID << "\" \n";
        // Print for debug.
        for (unsigned i = 0; i < scopCall->scopCallFunctonArgs.size(); ++i) {
            ScopCallFnArg *ha = scopCall->scopCallFunctonArgs[i];

            ha->dump(13);
        }

        errs() << "\n";
    }
#endif

    // If chain is not interrupted, apply optimizations.
    if(!flag_chain_interrupt) {
      // Iterate over list bottom-up. Root is current element. Children are the elements below.
      //   Look at each buffer in root. Find this buffer in children (top-down)
      //   Perform update
      for (int root_pos = scopCallList->size() - 1; root_pos >= 0; root_pos--) {
        ScopCallDS *rootScopCall = scopCallList->at(root_pos);
        
        for (unsigned i = 0; i < rootScopCall->scopCallFunctonArgs.size(); ++i) {
          ScopCallFnArg *ha = rootScopCall->scopCallFunctonArgs.at(i);
          
          updateRootBufferWithChildren(rootScopCall, ha, root_pos);
        }
      }
    }

#ifdef HTROP_SCOP_DEBUG
    errs() << "       --- RESULTS AFTER UPDATE ---  \n";

    for (auto it = scopCallList->begin(); it != scopCallList->end(); ++it) {
        ScopCallDS *scopCall = (*it);

        errs() << "           - function\"" << scopCall->scopID << "\" \n";
        // Print for debug.
        for (unsigned i = 0; i < scopCall->scopCallFunctonArgs.size(); ++i) {
            ScopCallFnArg *ha = scopCall->scopCallFunctonArgs[i];

            ha->dump(13);
        }

        errs() << "\n";
    }
#endif

    return false;
}

void htrop::ScopDependency::getAnalysisUsage(AnalysisUsage & AU) const {
}
