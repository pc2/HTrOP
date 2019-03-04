//    Copyright (c) 2019 University of Paderborn 
//                         (Marvin Damschen <marvin.damschen@gullz.de>,
//                          Gavin Vaz <gavin.vaz@uni-paderborn.de>,
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

#include "accscore.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/InstIterator.h"

// #include "polly/ScopDetection.h"

#include <iostream>

char htrop::AccScore::ID = 0;
static RegisterPass < htrop::AccScore > X("scoring", "scores functions to decide which functions to offload to accelerator from candidates", false, false);

llvm::cl::opt < int >IOPsWeight("iop-weight", llvm::cl::desc("Weight to multiply to integer operation count when scoring functions, defaults to 1"), llvm::cl::init(1));
llvm::cl::opt < int >FLOPsWeight("flop-weight", llvm::cl::desc("Weight to multiply to floating point operationg count when scoring functions, defaults to 1"), llvm::cl::init(1));

FunctionPass *htrop::createScoringPass() {
    return new AccScore();
}

bool htrop::AccScore::runOnFunction(Function & F) {

    // Number of 
    totalInstrs = 0;
    totalLoops = 0;
    totalScops = 0;
    totalInstrFLOPs = 0;
    totalInstrIOPs = 0;

    scoreFunctionRec(F);

    // ##############
    // Compute score.

    // Add score of instr.
    score += instrScore;
    if (score < instrScore) {   // check for overflow
        std::cout << "WARNING: " << "score overflow, set to maximum value \n";
        score = std::numeric_limits < decltype(score) >::max();
    }

    // Add score of loops.
    score += loopsScore;
    if (score < loopsScore) {   // check for overflow
        std::cout << "WARNING: " << "score overflow, set to maximum value \n";
        score = std::numeric_limits < decltype(score) >::max();
    }

    // Add score of scops.
    score += scopsScore;
    if (score < scopsScore) {   // check for overflow
        std::cout << "WARNING: " << "scopsScore overflow, set to maximum value \n";
        score = std::numeric_limits < decltype(score) >::max();
    }

    // ###################################
    // Compute affinity and code gen time.

    // - CPU.
    // Default score is CPU score.
    affinityCPU = score;

    // - MPCPU.
    // Increase score, if loops or scops available.
    if (totalLoops > 0) {
        affinityMPCPU = affinityCPU * 2.12;
    }

    if (totalScops > 0) {
        affinityMPCPU = affinityMPCPU * 2.34;
    }

    // - GPU.
    // Loop or Scop available.
    if (affinityCPU < affinityMPCPU) {
        // If more FLOPs than IOPs, increase.
        if (totalInstrFLOPs > totalInstrIOPs) {
            affinityGPU = affinityMPCPU * 1.24;
        }
        else {
            affinityGPU = affinityMPCPU * 1.07;
        }
    }
    else {
        // No loops and scops, reduce GPU score.
        affinityGPU = affinityCPU / 2;
    }

    // - GPUOPENACC.
    // Loop or Scop available.
    if (affinityCPU < affinityMPCPU) {
        // If more FLOPs than IOPs, increase.
        if (totalInstrFLOPs > totalInstrIOPs) {
            affinityGPUOPENACC = affinityMPCPU * 1.24;
        }
        else {
            affinityGPUOPENACC = affinityMPCPU * 1.07;
        }
    }
    else {
        // No loops and scops, reduce GPU score.
        affinityGPUOPENACC = affinityCPU / 2;
    }

    // - DFE.
    if (totalInstrFLOPs > totalInstrIOPs) {
        affinityDFE = affinityGPU / 0.86;
    }
    else {
        affinityDFE = affinityGPU / 1.46;
    }

    if (totalInstrFLOPs > totalInstrIOPs) {
        affinityFPGA = affinityGPU / 0.86;
    }
    else {
        affinityFPGA = affinityGPU / 1.46;
    }

#ifdef HTROP_DEBUG
    std::cout << "### Affinity : " << " \n";
    std::cout << "CPU: " << affinityCPU << " \n";
    std::cout << "MPCPU: " << affinityMPCPU << " \n";
    std::cout << "GPU: " << affinityGPU << " \n";
    std::cout << "GPUOPENACC: " << affinityGPUOPENACC << " \n";
    std::cout << "DFE: " << affinityDFE << " \n";
    std::cout << "FPGA: " << affinityFPGA << " \n";
#endif

    // ## Code Gen Time.
    // Code is available.
    codegenCPU = 0;
    codegenMPCPU = (TIME_C_BACKEND + TIME_POLLY_BACKEND + TIME_OPENMP_BACKEND) + totalInstrs;
    codegenGPU = (TIME_C_BACKEND + TIME_OPENCL_BACKEND) + totalInstrs;
    codegenGPUOPENACC = (TIME_C_BACKEND + TIME_OPENACC_BACKEND) + totalInstrs;
    codegenDFE = (TIME_MAXELER_BACKEND) + totalInstrs;
    codegenFPGA = (TIME_C_BACKEND + TIME_OPENCL_BACKEND + TIME_FPGA_COMPILE) + totalInstrs;

#ifdef HTROP_DEBUG
    std::cout << "### Code Gen : " << " \n";
    std::cout << "CPU: " << codegenCPU << " \n";
    std::cout << "MPCPU: " << codegenMPCPU << " \n";
    std::cout << "GPU: " << codegenGPU << " \n";
    std::cout << "GPUOPENACC: " << codegenGPUOPENACC << " \n";
    std::cout << "DFE: " << codegenDFE << " \n";
    std::cout << "FPGA: " << codegenFPGA << " \n";
#endif

    return false;
}

void htrop::AccScore::getAnalysisUsage(AnalysisUsage & AU) const {
    AU.setPreservesAll();
    AU.addRequired < llvm::LoopInfoWrapperPass > ();
    AU.addRequired < llvm::ScalarEvolutionWrapperPass > ();
//     AU.addRequired<polly::ScopDetection>();
}

unsigned long htrop::AccScore::getScore() {
    return score;
}

unsigned int htrop::AccScore::getAffinityCPU() {
    return affinityCPU;
}

unsigned int htrop::AccScore::getAffinityMPCPU() {
    return affinityMPCPU;
}

unsigned int htrop::AccScore::getAffinityFPGA() {
    return affinityFPGA;
}

unsigned int htrop::AccScore::getAffinityGPU() {
    return affinityGPU;
}

unsigned int htrop::AccScore::getAffinityGPUOPENACC() {
    return affinityGPUOPENACC;
}

unsigned int htrop::AccScore::getAffinityDFE() {
    return affinityDFE;
}

unsigned int htrop::AccScore::getCodegenCPU() {
    return codegenCPU;
}

unsigned int htrop::AccScore::getCodegenMPCPU() {
    return codegenMPCPU;
}

unsigned int htrop::AccScore::getCodegenFPGA() {
    return codegenFPGA;
}

unsigned int htrop::AccScore::getCodegenGPU() {
    return codegenGPU;
}

unsigned int htrop::AccScore::getCodegenGPUOPENACC() {
    return codegenGPUOPENACC;
}

unsigned int htrop::AccScore::getCodegenDFE() {
    return codegenDFE;
}

void htrop::AccScore::scoreFunctionRec(Function & F) {
#ifdef HTROP_SCORE_DEBUG
    errs() << "Scoring function: " << F.getName() << "\n";
#endif

    // #########################################
    // Step 1. Look at instructions, score them.

    // Loop over blocks and look at instructions.
    for (Function::iterator bb = F.begin(), e = F.end(); bb != e; ++bb) {
        // Score BasicBlock.
        visit((llvm::BasicBlock *) bb);

        // Total instructions.
        totalInstrs += bb->size();
        // Instructions of specific type.
        totalInstrFLOPs += lastBB_FLOPs;
        totalInstrIOPs += lastBB_IOPs;

        // Print out the name of the basic block if it has one, and then the
        // number of instructions that it contains
#ifdef HTROP_SCORE_DEBUG
        errs() << "Basic Block \"" << bb->getName() << "\" found, has " << bb->size() << " instructions, (FLOP: " << lastBB_FLOPs << ", IOP" << lastBB_IOPs << ", CALL: " << lastBB_Calls << ").\n";
#endif

        // Score called functions.
        if (lastBB_Calls > 0) {
            // Iterate over instructions of the BB.
            for (BasicBlock::iterator instr = bb->begin(), e = bb->end(); instr != e; ++instr)
                // Look for call instructions.)
                if (llvm::CallInst * callInst = dyn_cast < llvm::CallInst > (instr)) {
                    Function *callF = callInst->getCalledFunction();

                    // Score this function.
                    scoreFunctionRec(*callF);
                }
        }
    }

    // Multiply Instruction type with weight.
    instrScore = (IOPsWeight * totalInstrIOPs + FLOPsWeight * totalInstrFLOPs);

#ifdef HTROP_SCORE_DEBUG
    std::cout << "DEBUG: Step 1. " << totalInstrs << " instructions with score (FLOP: " << totalInstrFLOPs << " * " << FLOPsWeight << ", IOP" << totalInstrIOPs << " * " << IOPsWeight << ") " <<
        instrScore << std::endl;
#endif

    // ########################
    // Step 2. Look for loops.

    LoopInfo & LoopInf = (getAnalysis < llvm::LoopInfoWrapperPass > ()).getLoopInfo();
    for (LoopInfo::iterator Loop = LoopInf.begin(); Loop != LoopInf.end(); Loop++) {
        // Count number of loops.
        totalLoops++;

        unsigned long LoopInnermostTotalTripCount = getInnermostTotalTripCount(**Loop);

        unsigned totalLoopInstr = 0;
        unsigned totalLoopFLOPs = 0;
        unsigned totalLoopIOPs = 0;

        for (const auto & Block:(*Loop)->getBlocks()) {
            visit(Block);
            totalLoopInstr += Block->size();

            totalLoopFLOPs += lastBB_FLOPs;
            totalLoopIOPs += lastBB_IOPs;
        }

#ifdef HTROP_SCORE_DEBUG
        std::cout << "DEBUG: " << " LoopInnermostTotalTripCount = " << LoopInnermostTotalTripCount << ", totalLoopIOPs = " << totalLoopIOPs << ", totalLoopFLOPs = " << totalLoopFLOPs << std::endl;
#endif

        unsigned long loopScore = LoopInnermostTotalTripCount * (IOPsWeight * totalLoopIOPs + FLOPsWeight * totalLoopFLOPs);

        loopsScore += loopScore;

#ifdef HTROP_SCORE_DEBUG
        errs() << "Loop found, has (FLOP: " << lastBB_FLOPs << ", IOP" << lastBB_IOPs << ") with score " << loopScore << ".\n";
#endif
    }

#ifdef HTROP_SCORE_DEBUG
    std::cout << "DEBUG: Step 2. Loops score is " << loopsScore << std::endl;
#endif

    // #######################
    // Step 3. Look for SCoPs.

    //   polly::ScopDetection &SCoPDetect = getAnalysis<polly::ScopDetection>();
    //   if (SCoPDetect.begin() == SCoPDetect.end()) {
    //     std::cout << "INFO: " << "no SCoP detected in " << F.getName().str() << std::endl;
    //   } else {
    //     LoopInfo &LoopInf = getAnalysis<LoopInfo>();
    //     for (auto Region = SCoPDetect.begin(); Region != SCoPDetect.end(); Region++) {
    //       unsigned long regionScore = 0;
    //       for (LoopInfo::iterator Loop = LoopInf.begin(); Loop != LoopInf.end(); Loop++) {
    //         unsigned long LoopInnermostTotalTripCount = getInnermostTotalTripCount(**Loop);
    //         
    //         unsigned totalLoopFLOPs = 0;
    //         unsigned totalLoopIOPs = 0;
    //         for (const auto& Block : (*Loop)->getBlocks()) {
    //           visit(Block);
    //           totalLoopFLOPs += lastBB_FLOPs;
    //           totalLoopIOPs += lastBB_IOPs;
    //         }
    //         std::cout << "DEBUG: " << " LoopInnermostTotalTripCount = " << LoopInnermostTotalTripCount << ", totalLoopIOPs = " << totalLoopIOPs << ", totalLoopFLOPs = " << totalLoopFLOPs << std::endl;
    //         unsigned long loopScore = LoopInnermostTotalTripCount * (IOPsWeight*totalLoopIOPs + FLOPsWeight*totalLoopFLOPs);
    //         regionScore += loopScore;
    //       }
    //       scopsScore += regionScore;
    //       if (scopsScore < regionScore) { // check for overflow
    //         std::cout << "WARNING: " << "scopsScore overflow, set to maximum value \n";
    //         scopsScore = std::numeric_limits<decltype(scopsScore)>::max();
    //         break;
    //       }
    //     }        
    //   }
    //   
    //   std::cout << "DEBUG: Step 3. SCoP score is " << scopsScore << std::endl;

}

unsigned long htrop::AccScore::getInnermostTotalTripCount(const Loop & L) {
//   LoopInfo &LoopInf = (getAnalysis<llvm::LoopInfoWrapperPass>()).getLoopInfo();

    ScalarEvolution & ScalarEv = getAnalysis < ScalarEvolutionWrapperPass > ().getSE();

    unsigned long LTripCount = 1;       // neutral value for multiplication below

    if (ScalarEv.hasLoopInvariantBackedgeTakenCount(&L)) {
        const SCEVConstant *ExitCount = dyn_cast < SCEVConstant > (ScalarEv.getBackedgeTakenCount(&L));

        if (ExitCount != nullptr)
            LTripCount = ExitCount->getValue()->getZExtValue();
    }
    else {
#ifdef HTROP_SCORE_DEBUG
        std::cout << "INFO: " << "Scalar Evolution was unable to detect a loop invariant in loop with ID " << L.getLoopID() << std::endl;
#endif
        return 0;
    }

    auto & SubLoops = L.getSubLoops();
    if (SubLoops.size() == 0) {
        return LTripCount;
    }
    else {
        unsigned long subTripCountTotal = 0;

        for (const auto & SubLoop:SubLoops) {
            auto currSubTripCount = getInnermostTotalTripCount(*SubLoop);

            subTripCountTotal += currSubTripCount;

            if (subTripCountTotal < currSubTripCount)
                return std::numeric_limits < decltype(subTripCountTotal) >::max();
        }
        subTripCountTotal *= LTripCount;
        if (subTripCountTotal < LTripCount)
            return std::numeric_limits < decltype(subTripCountTotal) >::max();
        else
            return subTripCountTotal;
    }
}
