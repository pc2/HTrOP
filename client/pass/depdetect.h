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

#ifndef DEPDETECT_H
#define DEPDETECT_H

#include "llvm/Pass.h"
#include "llvm/PassAnalysisSupport.h"
#if LLVM_VERSION == 3 && LLVM_MINOR_VERSION < 5
#include "llvm/InstVisitor.h"
#else
#include "llvm/IR/InstVisitor.h"
#endif
#include "llvm/Analysis/LoopInfo.h"

#include <list>
#include <iostream>

using namespace llvm;

namespace htrop {
    
    class DepDetect : public FunctionPass, public InstVisitor<DepDetect> {
    public:
        static char ID;
        DepDetect() : FunctionPass(ID) {}
        
        virtual bool runOnFunction(Function &F);
        virtual void getAnalysisUsage(AnalysisUsage &AU) const;
        
        int getMaxCodegenLoopDepth();
        
    private:
        int max_codegen_loop_depth=0;
    };
    
    llvm::FunctionPass *createDepDetectPass();
}

#endif // DEPDETECT_H
