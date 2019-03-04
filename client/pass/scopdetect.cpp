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

#include "scopdetect.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Support/CommandLine.h"
#if LLVM_VERSION == 3 && LLVM_MINOR_VERSION < 5
#include "llvm/Support/InstIterator.h"
#else
#include "llvm/IR/InstIterator.h"
#endif

#include "../htropclient.h"
#include "../../common/stringHelper.h"
#include "../../consts.h"

#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/LoopInfo.h"

#include "polly/ScopDetection.h"
#include "polly/Support/GICHelper.h"
#include "polly/DependenceInfo.h"
#include "polly/ScopInfo.h"
#include "polly/Support/GICHelper.h"

#include "isl/set.h"

#include "../../common/math_parser.h"

#include "depdetect.h"

#include <iostream>
#include <algorithm>
#include <list>

char htrop::ScopDetect::ID = 0;
int htrop::ScopDetect::debug_hand_tune_pos = 0;

static RegisterPass < htrop::ScopDetect > X("scopdetect", "adds scop detection pass to the function", false, false);

llvm::Pass * htrop::createScopDetectPass(std::vector<int> maxScopDepth) {
    return new ScopDetect(maxScopDepth);
}


// ### Analyze "Domain" to get sizes of hotspot arguments.
//        i.e. [n] -> { Stmt_for_body3[i0, i1] : i0 >= 0 and i0 <= -1 + n and i1 >= 0 and i1 <= -1 + n }
// The idea is:
//   1) get dimensions from Stmt_for_body3[i0, i1]:
//        i0, i1, etc. are the dim_0, dim_1, etc.
//   2) parse the min/max values from the conditions i0 >= 0 and i0 <= -1 + n and i1 >= 0 and i1 <= -1 + n
//        i0 >= 0, i1 >= 0 are the min conditions, 
//        i0 <= -1 + n, i1 <= -1 + n are the max conditions.
//   3) adjust those values, when parsing the statements; [n] -> { Stmt_for_body3[i0, i1] -> MemRef_A[2 + i0, 2 + i1] };
//        here we update the max of MemRef_A from -1 + n to -1 + n + 2
std::vector < htrop::InternArrayDimensionInfo > htrop::ScopDetect::resolveDomainString(std::string domainStr) {
    // Preprocessing.
    //   Trim to the substring between { }, i.e. { Stmt_for_body3[i0, i1] : i0 >= 0 and i0 <= -1 + n and i1 >= 0 and i1 <= -1 + n }
    domainStr = substring(domainStr, "{", "}");

    // Split domain by " : " to get prefix and postfix:
    //   - the prefix Stmt_for_body3[i0, i1] gives the array dimensions.
    //   - the postfix i0 >= 0 and i0 <= -1 + n and i1 >= 0 and i1 <= -1 + n gives the bounds of the array dimensions.
    std::vector < std::string > domainStrSplit = explodeStr(domainStr, " : ");

    if (domainStrSplit.size() != 2) {
        std::vector < htrop::InternArrayDimensionInfo > internArrayDimensionInfo;
        htrop::InternArrayDimensionInfo newDim;
        newDim.nameStr = "NULL";
        internArrayDimensionInfo.push_back(newDim);
        return internArrayDimensionInfo;
    }

    trim(domainStrSplit[0]);
    trim(domainStrSplit[1]);

#ifdef HTROP_SCOP_DEBUG
    errs().indent(6) << "" << " - PREFIX \"" << domainStrSplit[0] << "\" .\n";
    errs().indent(6) << "" << " - POSTFIX \"" << domainStrSplit[1] << "\" .\n";
#endif

    // Get the dimensions from the prefix.
    // Parse the value between [], i.e. Stmt_for_body3[i0, i1] gives i0, i1
    std::string arrayDimensionsStr = substring(domainStrSplit[0], "[", "]");
    // Split the numbers to get each dimension.
    std::vector < std::string > arrayDimensions = explodeStr(arrayDimensionsStr, ",");
    for (std::vector < std::string >::iterator it = arrayDimensions.begin(); it != arrayDimensions.end(); ++it) {
        trim(*it);
    }

    // Get min and max from the postfix by splitting at " and ".
    //   i0 >= 0 and i0 <= -1 + n and i1 >= 0 and i1 <= -1 + n
    std::vector < std::string > minMaxExpressions = explodeStr(domainStrSplit[1], " and ");
    for (std::vector < std::string >::iterator it = minMaxExpressions.begin(); it != minMaxExpressions.end(); ++it) {
        trim(*it);
    }

    std::vector < htrop::InternArrayDimensionInfo > internArrayDimensionInfo;

    // Iterate over each dimension.
    //   i.e.  i0, i1
    for (unsigned i = 0; i < arrayDimensions.size(); ++i) {
        // Remove prefix/postfix spaces.
        //  now: i0
        trim(arrayDimensions[i]);

        htrop::InternArrayDimensionInfo newDim;
        newDim.nameStr = arrayDimensions[i];

        internArrayDimensionInfo.push_back(newDim);
    }

    //   - found "i0 >= 0" .
    //   - found "i0 <= -1 + n" .
    //   - found "i1 >= 0" .
    //   - found "i1 <= -1 + n" .
    for (unsigned i = 0; i < minMaxExpressions.size(); ++i) {
        // Remove prefix/postfix spaces.
        trim(minMaxExpressions[i]);
        std::string mm = minMaxExpressions[i];

        // Are we looking at min or max expression.
        bool isMin = false;

        if (mm.find(">=") != std::string::npos) {
            // Min. 
            isMin = true;
        }

        // Tokenize expression by "<=" or ">=" depending if its a min or max: i0 <= -1 + n
        //   0: i0
        //   1: -1 + n
        std::vector < std::string > minMaxTokens = explodeStr(mm, isMin ? " >= " : " <= ");

        // Get internArrayDimensionInfo from name.
        for (auto ad = internArrayDimensionInfo.begin(); ad != internArrayDimensionInfo.end(); ad++) {
            if (minMaxTokens[0] == (*ad).nameStr) {
                if (isMin) {
                    (*ad).minValueStr = minMaxTokens[1];
#ifdef HTROP_SCOP_DEBUG
                    errs().indent(6) << "" << " - " << (*ad).nameStr << ".min \"" << (*ad).minValueStr << "\" .\n";
#endif
                }
                else {
                    (*ad).maxValueStr = minMaxTokens[1];
#ifdef HTROP_SCOP_DEBUG
                    errs().indent(6) << "" << " - " << (*ad).nameStr << ".max \"" << (*ad).maxValueStr << "\" .\n";
#endif
                }
            }
        }
    }

    return internArrayDimensionInfo;
}

void htrop::ScopDetect::setDataTransferType(polly::MemoryAccess * MA, ScopFnArg * kernel_argument) {
    switch (MA->getType()) {
    case polly::MemoryAccess::AccessType::READ:
        switch (kernel_argument->type) {
        case UNKNOWN:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at READ - update from UNKNOWN to IN \n";
#endif
            kernel_argument->type = IN;
            break;
        case IN:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at READ - nothing to do \n";
#endif
            break;
        case OUT:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at READ - update from OUT to IN_OUT \n";
#endif
            kernel_argument->type = IN_OUT;
            break;
        case IN_OUT:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at READ - nothing to do \n";
#endif
            break;
        case TMP:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at READ - update from TMP to IN \n";
#endif
            kernel_argument->type = IN;
            break;
        }
        break;
    case polly::MemoryAccess::AccessType::MUST_WRITE:
        switch (kernel_argument->type) {
        case UNKNOWN:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at MUST_WRITE - update from UNKNOWN to OUT \n";
#endif
            kernel_argument->type = OUT;
            break;
        case IN:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at MUST_WRITE - update from IN to IN_OUT \n";
#endif
            kernel_argument->type = IN_OUT;
            break;
        case OUT:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at MUST_WRITE - nothing to do \n";
#endif
            break;
        case IN_OUT:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at MUST_WRITE - nothing to do \n";
#endif
            break;
        case TMP:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at MUST_WRITE - update from TMP to OUT \n";
#endif
            kernel_argument->type = OUT;
            break;
        }
        break;
    case polly::MemoryAccess::AccessType::MAY_WRITE:
        // NOTE: check "MAY_WRITE"
        switch (kernel_argument->type) {
        case UNKNOWN:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at MAY_WRITE - update from UNKNOWN to OUT \n";
#endif
            kernel_argument->type = OUT;
            break;
        case IN:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at MAY_WRITE - update from IN to IN_OUT \n";
#endif
            kernel_argument->type = IN_OUT;
            break;
        case OUT:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at MAY_WRITE - nothing to do \n";
#endif
            break;
        case IN_OUT:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at MAY_WRITE - nothing to do \n";
#endif
            break;
        case TMP:
#ifdef HTROP_SCOP_DEBUG
            dbgs().indent(12) << "Looking at MAY_WRITE - update from TMP to OUT \n";
#endif
            kernel_argument->type = OUT;
            break;
        }
        break;
    }
}

bool htrop::ScopDetect::runOnScop(polly::Scop & S) {
    // Get kernel function.
    scopFuncton = S.getRegion().getEntry()->getParent();

//   kernel_function->dump();
#ifdef HTROP_SCOP_DEBUG
    // S.dump();
#endif

// #ifdef HTROP_DEBUG
    std::cout << "INFO: " << " - SCoP detected in function \"" << scopFuncton->getName().str() << "\" " << "\n";
// #endif

    // ### Get argument dependencies.
    // ###   Skip the SCoP, if it has no valid dependencies.
#ifdef HTROP_SCOP_DEBUG
    std::cout << "INFO: " << "   - get dependencies " << std::endl;
#endif

    const polly::Dependences & D = getAnalysis < polly::DependenceInfo > ().getDependences();

    if (!D.hasValidDependences()) {
        std::cout << "INFO: " << "   - - NO valid dependencies! " << std::endl;

        return false;
    } 
    else {
#ifdef HTROP_SCOP_DEBUG
        std::cout << "INFO: " << "   - - valid dependencies FOUND " << std::endl;
#endif

#ifdef HTROP_SCOP_DEBUG
        D.dump();
#endif
    }
    
    // Dependency Check here.
    // Its parent is the base function.
    Module *theM = scopFuncton->getParent();
    
    llvm::legacy::FunctionPassManager DependencyAnalysisPM(theM);
    htrop::DepDetect* DepDetectPass = static_cast<htrop::DepDetect*>(htrop::createDepDetectPass());
    DependencyAnalysisPM.add(DepDetectPass);
    DependencyAnalysisPM.run(*scopFuncton);
    
    
    // ### Gather information about the loop.
    // Use automatic detection of independent loops to parallelize.
    if(maxScopDepth.size() == 0) {
      max_codegen_loop_depth = DepDetectPass->getMaxCodegenLoopDepth();
    } 
    // Use user provided flag to hand tune.
    else if(maxScopDepth.size() == 1) {
      max_codegen_loop_depth = maxScopDepth[0];
    } else {
      // Reuse last to prevent overflow.
      if((maxScopDepth.size()-1) == debug_hand_tune_pos) {
        max_codegen_loop_depth = maxScopDepth[debug_hand_tune_pos];
      } else {
        max_codegen_loop_depth = maxScopDepth[debug_hand_tune_pos++];
      }
    }
    
    #ifdef HTROP_SCOP_DEBUG
    errs().indent(2) << "   - S.getMaxLoopDepth: " << S.getMaxLoopDepth() << ", DepDetectPass->getMaxCodegenLoopDepth(): " << DepDetectPass->getMaxCodegenLoopDepth() << ", hand-tuned: " << max_codegen_loop_depth << "\n";
    #endif    
        
    scopFound = true;
    // Math. expression parser.
    ScopExp *scopAST;

    bool foundParent = false;
    // Iterate over all functions of module.
    for (llvm::Module::iterator fi = theM->getFunctionList().begin(), fe = theM->getFunctionList().end(); fi != fe; ++fi) {
        llvm::Function * FF = dyn_cast < llvm::Function > (fi);

        // Skip function that only checks results on CPU for correctness.
        if (FF->getName().str().find(CHECK_RESULTS_CPU) != std::string::npos) {
            // std::cout << "INFO: " << "Skipped SCoP analysis for: \"" << FF->getName().str() << "\"\n";
        }
        else {
            for (BasicBlock & B:*FF) {
                for (Instruction & I:B) {
                    if (CallInst * tmp_tmp_callInst = dyn_cast < CallInst > (&I)) {
                        if (tmp_tmp_callInst->getCalledFunction() == scopFuncton) {
                            // Check if we have already a parent. Hopefully its the same.
                            if (foundParent) {
                                assert(scopFunctonParent == FF && "ERROR: We found a second parent that is different. ");
                            }
#ifdef HTROP_SCOP_DEBUG
                            std::cout << "INFO: " << " - PARENT CALLER of SCOP function is \"" << FF->getName().str() << "\" " << "\n";
#endif
                            scopFunctonParent = FF;
                            foundParent = true;
                        }
                    }
                }
            }
        }
    }

    // assert(foundParent);
    if (!foundParent) {
        std::cout << "INFO: " << " - this scop is never called in the application!! " << "\n";
    }

    // Iterate over all wrapper function args.
    for (auto & A:scopFuncton->getArgumentList()) {
        // Create ScopFnArg for it.
        ScopFnArg *ha = new ScopFnArg();
        ha->name = A.getName();
        scopFunctonArgs.push_back(ha);
    }

#ifdef HTROP_SCOP_DEBUG
    errs().indent(2) << "" << "   - Result from Copying Function Arguments: " << "\n";
    for (unsigned i = 0; i < scopFunctonArgs.size(); ++i) {
        ScopFnArg *ha = scopFunctonArgs[i];
        ha->dump();
    }
#endif

    // Create intern data structure for the loop.
    for (int i = 0; i < max_codegen_loop_depth; i++) {
        ScopLoopInfo *ifi = new ScopLoopInfo();

        scopLoopInfo.push_back(ifi);
    }

    // ### Fill information for each loop:
    // NOTE: currently information is gathered by string matching. Better solution would evaluate the isl_set.
    //       This is not save for arbitrary nested loops.
    //       Current assumption; loop has serializable structure:
    //         for(i ... n)
    //           for(j ... m)
    // (A) name as string
    // (B) maximum value as integer

    // (A)
    // Context. It gives us information about the upper bound for the loop.
    // Example 1)
    //   for(int i=0; i < n; i++)  
    //   - here we are looking for n
    //
    // Example 2)
    //   for(int i=0; i < n; i++) 
    //     for(int j=0; j < n; j++) 
    // 
    // - for both example we are looking at (see below), because both nesting are bounded by n.
    // 
    // Output: [n] -> {  : n <= 2147483647 and n >= -2147483648 }
    // 
    // Example 3) 
    //   for(int i=0; i < n; i++) 
    //     for(int j=0; j < m; j++) 
    // 
    // - here we have to different bounds for each nesting.
    //   NOTE: this example will be resolved incorrectly, because we have no information about "n" 
    //           from SCOP detection.
    //
    // Output: [n, m] -> {  : n <= 2147483647 and n >= -2147483648 and m <= 2147483647 and m >= -2147483648 }
    std::string contextStr = S.getContextStr();

#ifdef HTROP_SCOP_DEBUG
    errs().indent(4) << "" << " - context: " << contextStr << "\n";
#endif

    // Parse the value between [], i.e. "n" or "n, m". 
    //   Will be used as iterator for the loops.
    int t_pos_start = contextStr.find("[") + 1;
    int t_pos_end = contextStr.find("]") - t_pos_start;

    // NOTE: here we could also parse the type of the bound, i.e. 2147483647 for integer etc.
    std::string subS = contextStr.substr(t_pos_start, t_pos_end);

#ifdef HTROP_SCOP_DEBUG
    errs().indent(6) << "" << " - found \"" << subS << "\" in context.\n";
#endif

    // Get the name of the loop bound, i.e. "n" and 
    //   map argument position of loopBound in kernel_function
    std::vector < std::string > loopBounds = explodeStr(subS, ",");
    for (unsigned i = 0; i < loopBounds.size(); ++i) {
        trim(loopBounds[i]);
    }

    //Limit loopBounds to max(max_codegen_loop_depth)

    // Map to loop structure.
    if (loopBounds.size() >= max_codegen_loop_depth) {
        // Number of arguments match. Map each to loop level.
        // - Context string: [n, m], loop depth = 2
        // - Loop: for(i ... n)
        //           for(j ... m)
      for (unsigned i = 0; i < loopBounds.size() && i < max_codegen_loop_depth; ++i) {
#ifdef HTROP_SCOP_DEBUG
            errs().indent(6) << " - mapping \"" << loopBounds[i] << "\" to loop level \"" << i << "\" \n";
#endif
            scopLoopInfo.at(i)->nameStr = loopBounds[i];
        }
    }
    else if (loopBounds.size() < max_codegen_loop_depth) {
        // Number of arguments does not. Reuse arguments.
        // - Context string: [n], loop depth = 2
        // - Loop: for(i ... n)
        //           for(j ... n)

        // Map the matching structure.
        for (unsigned i = 0; i < loopBounds.size(); ++i) {
#ifdef HTROP_SCOP_DEBUG
            errs().indent(6 + (i * 2)) << " - mapping \"" << loopBounds[i] << "\" to loop level \"" << i << "\" \n";
#endif
            scopLoopInfo.at(i)->nameStr = loopBounds[i];
        }

        // Fill the rest. Currently with last item in context string.
        for (unsigned i = loopBounds.size(); i <= max_codegen_loop_depth - loopBounds.size(); ++i) {
#ifdef HTROP_SCOP_DEBUG
            errs().indent(6 + (i * 2)) << " - fill \"" << loopBounds[loopBounds.size() - 1] << "\" to loop level \"" << i << "\" \n";
#endif
            scopLoopInfo.at(i)->nameStr = loopBounds[loopBounds.size() - 1];
        }
    }

#ifdef HTROP_SCOP_DEBUG
    errs().indent(2) << "" << "   - ScopLoopInfo: " << "\n";
    for (unsigned i = 0; i < max_codegen_loop_depth; ++i) {
        errs().indent(4 + (i * 2)) << "" << "   \"" << scopLoopInfo.at(i)->nameStr << "\" \n";
    }
#endif

    // (B)
    // Assumed Context. Gives the maximum value of the loops.
    //   Example: [n] -> {  : n <= 4000 }
    std::string assumedContextStr = S.getAssumedContextStr();

#ifdef HTROP_SCOP_DEBUG
    errs().indent(2) << "" << "   - assumedContext: " << assumedContextStr << "\n";
#endif

    // We look for:
    //   n <= 4000 in [n] -> {  : n <= 4000 }
    // Therefore we iterate over all possible loop bounds and check if a value exists.
    for (unsigned i = 0; i < max_codegen_loop_depth; ++i) {

#ifdef HTROP_SCOP_DEBUG
        errs().indent(4 + (i * 2)) << "" << "   looking for \"" << scopLoopInfo.at(i)->nameStr << " <= NUMBER\" \n";
#endif

        std::string t_needle = scopLoopInfo.at(i)->nameStr + " <= ";
        t_pos_start = assumedContextStr.find(t_needle);

        if (t_pos_start > 0) {
            // Found. Now parse the number.
            t_pos_start += t_needle.length();
            t_pos_end = assumedContextStr.find(" ", t_pos_start) - t_pos_start;
            subS = assumedContextStr.substr(t_pos_start, t_pos_end);

#ifdef HTROP_SCOP_DEBUG
            errs().indent(6) << "" << " - found \"" << subS << "\" in context.\n";
#endif

            // Insert into ScopLoopInfo.
            scopLoopInfo.at(i)->maxValue = std::stoi(subS);
        }
    }
#ifdef HTROP_SCOP_DEBUG
    errs().indent(2) << "" << "   - ScopLoopInfo: " << "\n";
    for (unsigned i = 0; i < max_codegen_loop_depth; ++i) {
        errs().indent(4 + (i * 2)) << "" << "   \"" << scopLoopInfo.at(i)->nameStr << "\" => \"" << scopLoopInfo.at(i)->maxValue << "\"\n";
    }
#endif

    // ###
    // Get the kernel arguments
#ifdef HTROP_SCOP_DEBUG
    errs().indent(2) << "INFO: " << "   - get kernel arguments" << "\n";
#endif

    DenseMap < Value *, polly::MemoryAccess * >PtrToAcc;
    DenseSet < Value * >HasWriteAccess;

    // Loop over scop statements.
    for (polly::ScopStmt & Stmt:S) {

        // ### Analyze "Domain" to get sizes of hotspot arguments.
        //        i.e. [n] -> { Stmt_for_body3[i0, i1] : i0 >= 0 and i0 <= -1 + n and i1 >= 0 and i1 <= -1 + n }
#ifdef HTROP_SCOP_DEBUG
        errs().indent(2) << "" << "   - Looking at Domain: " << Stmt.getDomainStr() << "\n";
#endif

        std::vector < htrop::InternArrayDimensionInfo > internArrayDimensionInfo = resolveDomainString(Stmt.getDomainStr());

        if (internArrayDimensionInfo.at(0).nameStr == "NULL")
            continue;

        // Get memory accesses.
        for (polly::MemoryAccess * MA:Stmt) {
            // Debug.
#ifdef HTROP_SCOP_DEBUG
            errs().indent(2) << "---Looking at Memory Access \n";
            MA->dump();
#endif

            // ### Get access instruction: load or store:
            // A) LOAD
            // -   %3 = load double, double* %arrayidx8, align 8
            // B) STORE
            // -   store double %div, double* %arrayidx61, align 8
            llvm::Instruction * instr_access = MA->getAccessInstruction();

            // ### Backtrack to get the operand:
            //   %arrayidx8 = getelementptr inbounds [4000 x double], [4000 x double]* %A, i64 %2, i64 %1
            // Pattern for 2D arrays:
            //   A[i][j] --> %A[%2][%1]
            llvm::Value * value_operand = polly::getPointerOperand(*instr_access);

            // ### Get array info from SCOP.
            isl_id *arrayId = MA->getArrayId();
            void *user = isl_id_get_user(arrayId);
            const polly::ScopArrayInfo * SAI = static_cast < polly::ScopArrayInfo * >(user);

            isl_id_free(arrayId);

            // NOTE: there are two types of access patterns to 2d-arrays:
            //   1) real 2d to 2d access
            //       ReadAccess := [Reduction Type: NONE] [Scalar: 0]
            //                     [n] -> { Stmt_for_body3[i0, i1] -> MemRef_A[i0, i1] };
            //  
            //   2) FLAT 2d to 1d access
            //       ReadAccess := [Reduction Type: NONE] [Scalar: 0]
            //                     [rows, cols] -> { Stmt_for_body3[i0, i1] -> MemRef_inputImage_data[3840i0 + 3i1] };
            //   [n] -> { Stmt_for_body3[i0, i1] -> MemRef_A[1 + i0, i1] }
            std::string accessStr = MA->getOriginalAccessRelationStr();

            // Preprocessing.
            //   Trim to the substring between { }, i.e. { Stmt_for_body3[i0, i1] : i0 >= 0 and i0 <= -1 + n and i1 >= 0 and i1 <= -1 + n }
            accessStr = substring(accessStr, "{", "}");

            // Split domain by " -> " to get prefix and postfix:
            std::vector < std::string > accessStrSplit = explodeStr(accessStr, " -> ");
            assert(accessStrSplit.size() == 2);
            //   - Prefix Stmt_for_body3[i0, i1]
            trim(accessStrSplit[0]);
            //   - Postfix MemRef_A[2 + i0, 1 + i1]
            trim(accessStrSplit[1]);

#ifdef HTROP_SCOP_DEBUG
            errs().indent(6) << "" << " - PREFIX \"" << accessStrSplit[0] << "\" .\n";
            errs().indent(6) << "" << " - POSTFIX \"" << accessStrSplit[1] << "\" .\n";
#endif

            // Get the dimensions from the prefix.
            // Parse the value between [], i.e. Stmt_for_body3[i0, i1] gives i0, i1
            std::string stmtDimensionsStr = substring(accessStrSplit[0], "[", "]");
            std::string arrayDimensionsStr = substring(accessStrSplit[1], "[", "]");

            // Split the numbers to get each dimension.
            //   0: i0
            //   1: i1
            std::vector < std::string > stmtDimensions = explodeStr(stmtDimensionsStr, ",");
            for (std::vector < std::string >::iterator it = stmtDimensions.begin(); it != stmtDimensions.end(); ++it) {
                trim(*it);
            }

            // Parse the value between [] in the postfix, i.e. MemRef_A[2 + i0, 1 + i1] gives
            //   0: 2 + i0 
            //   1: 1 + i1
            std::vector < std::string > arrayDimensions = explodeStr(arrayDimensionsStr, ",");
            for (std::vector < std::string >::iterator it = arrayDimensions.begin(); it != arrayDimensions.end(); ++it) {
                trim(*it);
            }

            // ### Look if this is a new argument or already in the list.
            // Argument handle.
            ScopFnArg *kernel_argument = nullptr;

            for (ScopFnArg * ha:scopFunctonArgs) {
                if (ha->name == SAI->getBasePtr()->getName()) {
                    // UPDATE
#ifdef HTROP_SCOP_DEBUG
                    errs().indent(4) << "### I ALREADY KNOW THIS ARGUMENT \"" << (SAI->getBasePtr()->getName()).str() << "\". UPDATE " << "\n";
#endif

                    kernel_argument = ha;

                    kernel_argument->name = SAI->getBasePtr()->getName();
                    kernel_argument->isPointer = !MA->isScalarKind();

                    // Set coorect dimension for this array.
                    kernel_argument->resize(SAI->getNumberOfDimensions());

#ifdef HTROP_SCOP_DEBUG
                    kernel_argument->dump();
#endif
                }
            }

            // If this is not a know kernel argument, its a phi helper.
            if (kernel_argument == nullptr) {
#ifdef HTROP_SCOP_DEBUG
                errs().indent(4) << "### PHI HELPER STATEMENT \"" << (SAI->getBasePtr()->getName()).str() << "\". SKIP IT" << "\n";
#endif
                continue;
            }

            if (MA->isScalarKind()) {
                // NOTE: process scalars here
#ifdef HTROP_SCOP_DEBUG
                errs().indent(4) << " FOUND SCALAR MEMORY ACCESS\n";
#endif
                continue;
            }

            // ### Set DataTransferType
            setDataTransferType(MA, kernel_argument);

            // ### Analyze array dimensions to get sizes of hotspot arguments. I.e. MemRef_A[2 + i0, 1 + i1] gives
            //   0: 2 + i0 
            //   1: 1 + i1
            for (unsigned i = 0; i < arrayDimensions.size(); ++i) {
                // Here we will remove i0, and i1 and replace them with 0.
                //   The idea is to get the MIN and MAX padding values.
                std::string arrayDimensionStr_noVars = arrayDimensions[i];

                // Remove variables.
                for (auto stmDim:stmtDimensions) {
                    int r_start = arrayDimensionStr_noVars.find(stmDim);

                    while (r_start > -1) {
                        std::string rpl_str = "(" + std::to_string(0) + ")";
                        arrayDimensionStr_noVars.replace(r_start, stmDim.length(), rpl_str);
                        r_start = arrayDimensionStr_noVars.find(stmDim);
                    }
                }

                // Crease AST for expression and evaluate it.
                scopAST = createASTfromScopString(arrayDimensionStr_noVars);
                long tree_val = scopAST->eval();

                scopAST->cln();

#ifdef HTROP_SCOP_DEBUG
                errs().indent(12) << " LOOOKING AT \"" << arrayDimensions[i] << "\" --> without VARS: \"" << arrayDimensionStr_noVars << "\" = " << tree_val << "\n";
#endif

                // See if this index is smaller for this dimension.
                if (tree_val <= kernel_argument->dimension_offset_min.at(i)) {
#ifdef HTROP_SCOP_DEBUG
                    errs().indent(12) << "FOUND SMALLER INDEX" << "   \"" << tree_val << "\" for DIM = " << i << ", currently \"" << kernel_argument->dimension_offset_min.at(i) << "\"\n";
#endif

                    kernel_argument->dimension_offset_min.at(i) = tree_val;
                    kernel_argument->dimension_minStr.at(i) = arrayDimensions[i];

                    std::string arrayDimensionStr_expanded = arrayDimensions[i];

                    // Get internArrayDimensionInfo from name.
                    for (auto ad = internArrayDimensionInfo.begin(); ad != internArrayDimensionInfo.end(); ad++) {
                        int r_start = arrayDimensionStr_expanded.find((*ad).nameStr);

                        while (r_start > -1) {
                            std::string minValueReplace = "(" + (*ad).minValueStr + ")";

                            arrayDimensionStr_expanded.replace(r_start, (*ad).nameStr.length(), minValueReplace);
                            r_start = arrayDimensionStr_expanded.find((*ad).nameStr);
                        }
                    }

                    scopAST = createASTfromScopString(arrayDimensionStr_expanded);
                    long minEval = scopAST->eval();

                    scopAST->cln();

                    kernel_argument->dimension_min.at(i) = minEval;
                }

                // See if this index is bigger for this dimension.
                if (tree_val >= kernel_argument->dimension_offset_max.at(i)) {
#ifdef HTROP_SCOP_DEBUG
                    errs().indent(12) << "FOUND BIGGER INDEX" << "   \"" << tree_val << "\" for DIM = " << i << ", currently \"" << kernel_argument->dimension_offset_max.at(i) << "\"\n";
#endif

                    kernel_argument->dimension_offset_max.at(i) = tree_val;

                    // Update max string, FLAT array access here.
                    // Set/update dimension.
                    std::string arrayDimensionStr_expanded = arrayDimensions[i];

                    // Get internArrayDimensionInfo from name.
                    for (auto ad = internArrayDimensionInfo.begin(); ad != internArrayDimensionInfo.end(); ad++) {
                        int r_start = arrayDimensionStr_expanded.find((*ad).nameStr);

                        while (r_start > -1) {
                            std::string maxValueReplace = "(" + (*ad).maxValueStr + ")";

                            arrayDimensionStr_expanded.replace(r_start, (*ad).nameStr.length(), maxValueReplace);
                            r_start = arrayDimensionStr_expanded.find((*ad).nameStr);
                        }
                    }

                    kernel_argument->dimension_maxStr.at(i) = arrayDimensionStr_expanded;
                }

#ifdef HTROP_SCOP_DEBUG
                kernel_argument->dump();
#endif
            }

            kernel_argument = nullptr;
        }
    }

#ifdef HTROP_SCOP_DEBUG
    errs().indent(2) << "" << "   - Result from Statement: " << "\n";
    for (unsigned i = 0; i < scopFunctonArgs.size(); ++i) {
        ScopFnArg *ha = scopFunctonArgs[i];

        ha->dump();
    }
    errs().indent(2) << "" << "   - Apply +1 fix: " << "\n";
#endif
    for (unsigned i = 0; i < scopFunctonArgs.size(); ++i) {
        ScopFnArg *ha = scopFunctonArgs[i];

        if (ha->isPointer) {
            // If the min is "0", we add another element.
            if (ha->dimension_min.at(0) == 0) {
                ha->dimension_maxStr.at(0) += " + 1";
            }
        }
    }

#ifdef HTROP_SCOP_DEBUG
    errs().indent(2) << "" << "   - Result from Statement: " << "\n";
    for (unsigned i = 0; i < scopFunctonArgs.size(); ++i) {
        ScopFnArg *ha = scopFunctonArgs[i];
        ha->dump();
    }
#endif


    // ### Map to argument list of function.
    for (unsigned i = 0; i < scopFunctonArgs.size(); ++i) {
        ScopFnArg *ha = scopFunctonArgs[i];

        for (llvm::Function::arg_iterator arg_I = scopFuncton->arg_begin(); arg_I != scopFuncton->arg_end(); arg_I++) {
            Value *theA = dyn_cast < Value > (arg_I);

            if (theA->getName() == ha->name) {
                ha->value = theA;
                break;
            }
        }
    }

#ifdef HTROP_SCOP_DEBUG
    errs().indent(2) << "" << "   - Result from Function mapping: " << "\n";
    for (unsigned i = 0; i < scopFunctonArgs.size(); ++i) {
        ScopFnArg *ha = scopFunctonArgs[i];

        ha->dump();
    }
#endif

    return false;
}

void htrop::ScopDetect::getAnalysisUsage(AnalysisUsage & AU) const {
    polly::ScopPass::getAnalysisUsage(AU);

    AU.addRequired < llvm::LoopInfoWrapperPass > ();
    AU.addRequired < polly::ScopInfo > ();

    AU.addRequired < polly::ScopDetection > ();
    AU.addRequired < polly::DependenceInfo > ();
}

std::vector < ScopFnArg * >htrop::ScopDetect::getScopFuncitonArgs() {
    return scopFunctonArgs;
}

Function *htrop::ScopDetect::getScopFunction() {
    return scopFuncton;
}

Function *htrop::ScopDetect::getScopFunctionParent() {
    return scopFunctonParent;
}

std::vector < ScopLoopInfo * >htrop::ScopDetect::getScopLoopInfo() {
    return scopLoopInfo;
}

int htrop::ScopDetect::getMaxCodegenLoopDepth(){
  return max_codegen_loop_depth;
}

void htrop::ScopDetect::clear() {
    scopFunctonArgs.clear();
    scopLoopInfo.clear();
    scopFound = false;
}

bool htrop::ScopDetect::containsScop() {
    return scopFound;
}
