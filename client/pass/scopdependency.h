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
//    THE SOFTWARE..

#ifndef SCOPDEPENDENCY_H
#define SCOPDEPENDENCY_H

#include "llvm/Pass.h"
#include "llvm/PassAnalysisSupport.h"
#if LLVM_VERSION == 3 && LLVM_MINOR_VERSION < 5
#include "llvm/InstVisitor.h"
#else
#include "llvm/IR/InstVisitor.h"
#endif
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/LoopInfo.h"

#include "../../common/sharedStructCompileRuntime.h"

#include <list>
#include <iostream>

using namespace llvm;

namespace htrop {

    class ScopDependency:public FunctionPass, public InstVisitor < ScopDependency > {
        friend class InstVisitor < ScopDependency >;
 public:
        static char ID;
         ScopDependency():FunctionPass(ID) {
        } virtual bool runOnFunction(Function & F);
        virtual void getAnalysisUsage(AnalysisUsage & AU) const;

        void setScopList(std::map < std::string, ScopDS * >*scopList) {
            this->scopList = scopList;
        } 
        
        void setScopCallList(std::vector < ScopCallDS * >*scopCallList) {
            this->scopCallList = scopCallList;
        }

        Function *getFunctionParent() {
            return funcParent;
        }
        
        bool getFlagChainInterrupt() {
          return flag_chain_interrupt;
        }

 private:

        Function * funcParent;
        std::map < std::string, ScopDS * >*scopList;
        std::vector < ScopCallDS * > *scopCallList;
        int calcPositionInParent(Value * v);
        void updateRootBufferWithChildren(ScopCallDS * rootScopCall, ScopCallFnArg * arg, int root_pos);
        // Hard flag to disable optimizations, if chain is interrupted by code 
        // executing on the host. Currently calls between scops are detected. Further
        // analysis is required to catch arbitrary possible modifications to the buffers,
        // while they are on the device.
        // If flag is raised, all data optimizations are prevented and the data is
        // always written-back. 
        bool flag_chain_interrupt = false;
    };

    llvm::FunctionPass * createScopDependencyPass();
}

#endif                          // SCOPDEPENDENCY_H
