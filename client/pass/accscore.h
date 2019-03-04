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

#ifndef ACCSCORE_H
#define ACCSCORE_H

#include "llvm/Pass.h"
#include "llvm/PassAnalysisSupport.h"
#if LLVM_VERSION == 3 && LLVM_MINOR_VERSION < 5
#include "llvm/InstVisitor.h"
#else
#include "llvm/IR/InstVisitor.h"
#endif

#include "llvm/Analysis/LoopInfo.h"

#include "../../common/hds.pb.h"

using namespace llvm;

namespace htrop {
    class AccScore:public FunctionPass, public InstVisitor < AccScore > {
        friend class InstVisitor < AccScore >;
 public:
        static char ID;
         AccScore():FunctionPass(ID) {
        } virtual bool runOnFunction(Function & F);
        virtual void getAnalysisUsage(AnalysisUsage & AU) const;

        unsigned long getScore();

        unsigned int getAffinityCPU();
        unsigned int getAffinityMPCPU();
        unsigned int getAffinityGPU();
        unsigned int getAffinityGPUOPENACC();
        unsigned int getAffinityDFE();
        unsigned int getAffinityFPGA();

        unsigned int getCodegenCPU();
        unsigned int getCodegenMPCPU();
        unsigned int getCodegenGPU();
        unsigned int getCodegenGPUOPENACC();
        unsigned int getCodegenDFE();
        unsigned int getCodegenFPGA();
 private:
        unsigned long score = 0;

        unsigned totalInstrs = 0;
        unsigned totalLoops = 0;
        unsigned totalScops = 0;
        unsigned totalInstrFLOPs = 0;
        unsigned totalInstrIOPs = 0;

        unsigned long instrScore = 0;
        unsigned long loopsScore = 0;
        unsigned long scopsScore = 0;

        unsigned lastBB_FLOPs = 0;
        unsigned lastBB_IOPs = 0;
        unsigned lastBB_Calls = 0;

        // ## Affinity.
        unsigned int affinityCPU = 0;
        unsigned int affinityMPCPU = 0;
        unsigned int affinityGPU = 0;
        unsigned int affinityGPUOPENACC = 0;
        unsigned int affinityDFE = 0;
        unsigned int affinityFPGA = 0;

        const unsigned int TIME_CLIENT_TO_SERVER = 511;
        const unsigned int TIME_C_BACKEND = 53;
        const unsigned int TIME_POLLY_BACKEND = 74;
        const unsigned int TIME_OPENMP_BACKEND = 33;
        const unsigned int TIME_OPENCL_BACKEND = 56;
        const unsigned int TIME_OPENACC_BACKEND = 73;
        const unsigned int TIME_MAXELER_BACKEND = 563;
        const unsigned int TIME_FPGA_COMPILE = 18000;

        // Code is available.
        unsigned int codegenCPU = 0;
        unsigned int codegenMPCPU = 0;
        unsigned int codegenGPU = 0;
        unsigned int codegenGPUOPENACC = 0;
        unsigned int codegenDFE = 0;
        unsigned int codegenFPGA = 0;

#define HANDLE_INST(N, OPCODE, CLASS) \
        void visit##OPCODE(CLASS &) { if (#OPCODE == "FAdd" || #OPCODE == "FSub" || #OPCODE == "FMul" || #OPCODE == "FDiv" || #OPCODE == "FRem") lastBB_FLOPs++; \
                                      if (#OPCODE == "Add" || #OPCODE == "Sub" || #OPCODE == "Mul" || #OPCODE == "UDiv" || #OPCODE == "SDiv" || #OPCODE == "URem" || #OPCODE == "SRem") lastBB_IOPs++; \
                                      if (#OPCODE == "Shl" || #OPCODE == "LShr" || #OPCODE == "AShr" || #OPCODE == "And" || #OPCODE == "Or" || #OPCODE == "Xor") lastBB_IOPs++; \
                                      if (#OPCODE == "Call") lastBB_Calls++; }
#include "llvm/IR/Instruction.def"

        void visitBasicBlock(BasicBlock & BB) {
            lastBB_FLOPs = 0;
            lastBB_IOPs = 0;
        } void scoreFunctionRec(Function & F);
        unsigned long getInnermostTotalTripCount(const Loop & L);
    };
    llvm::FunctionPass * createScoringPass();
}

#endif                          // ACCSCORE_H
