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

//Assumption (only nested loop)

// --Level 0
//   |-Level 1
//     |-Level 2 (optional)


#include "depdetect.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Support/CommandLine.h"
#if LLVM_VERSION == 3 && LLVM_MINOR_VERSION < 5
#include "llvm/Support/InstIterator.h"
#else
#include "llvm/IR/InstIterator.h"
#endif
#include "../htropclient.h"

#include <iostream>
#include <list>

char htrop::DepDetect::ID = 0;

static RegisterPass<htrop::DepDetect> X("depdetect", "adds depdetect pass to the function", false, false);

FunctionPass *htrop::createDepDetectPass() {
    return new DepDetect();
}

bool htrop::DepDetect::runOnFunction(Function &F) {
    
    struct depInfo {
        Instruction *index;
        int constIndexOffset = 0;
        Value *array;
        llvm::Loop *loop;
    };
    
    std::vector<depInfo> loadDepInfo;
    std::vector<depInfo> storeDepInfo;
    std::vector<depInfo> *depInfoPtr;
    
    LoopInfo &LI = (getAnalysis<llvm::LoopInfoWrapperPass>()).getLoopInfo();
    
    for (auto &I : instructions(F)) {
        Instruction *Inst = &I;
        
        if (!Inst->mayReadFromMemory() && !Inst->mayWriteToMemory())
            continue;
        
        if (auto CS = CallSite(Inst)) 
            continue;

        GetElementPtrInst *gepInst = NULL;
        //walkback till you find the array_name and index variables (array_name[index])
        if (LoadInst *loadInst = dyn_cast<LoadInst>(Inst)){
            gepInst = dyn_cast<GetElementPtrInst>(loadInst->getOperand(0));
            depInfoPtr = &loadDepInfo;
        }
        else if (StoreInst* storeInst = dyn_cast<StoreInst>(Inst)){
            gepInst = dyn_cast<GetElementPtrInst>(storeInst->getOperand(1));
            depInfoPtr = &storeDepInfo;
        }
        else{
            continue;
        }
        
        if(gepInst==NULL)
            continue;
        
        //Check if the array is passed to the function as a param
        //Get the array pointer
        bool matchesArgs = false;
        for (auto& A : F.getArgumentList()) { 
            // Create HotspotArgument for it.
            if( &A == gepInst->getOperand(0)){
                matchesArgs=true;
                break;
            }
        }
        
        if(!matchesArgs)
            continue;
        
        depInfo arrayDependeny;
        
        //save the loop
        arrayDependeny.loop = LI.getLoopFor(Inst->getParent());
        
        //save the array_name
        arrayDependeny.array = gepInst->getOperand(0);
        
        // find the index
        if(Instruction *indexInst =  dyn_cast<Instruction>(gepInst->getOperand(1))){
            arrayDependeny.index = indexInst;
        }
        else{
            std::cout << "\n!!!!! depdect.cpp => DepDetectPass : runOnFunction : Implement";
            std::cout.flush();
            F.dump();
            exit(0);
        }
        depInfoPtr->push_back(arrayDependeny);    
    }
    
    //Simple check
    //Check if all reads and writes are in the same loop
    bool hasLoopDependencies = false;
    
    llvm::Loop *minDepthLoop = NULL;
    
    
    //find the min store loop depth
    for(std::vector<depInfo>::iterator it = storeDepInfo.begin(); it != storeDepInfo.end(); ++it) {
        //Check if the loop is the same
        if(minDepthLoop){
            if(minDepthLoop->getLoopDepth()>it->loop->getLoopDepth())
                minDepthLoop = it->loop;
        }
        else{
            minDepthLoop = it->loop;
        }
    }
    
    //Check of all load instructions comply
    for(std::vector<depInfo>::iterator it = loadDepInfo.begin(); it != loadDepInfo.end(); ++it) {   
        //check if the loopDepth is greater than or equal to loop depth
        if(it->loop->getLoopDepth() < minDepthLoop->getLoopDepth()){
            hasLoopDependencies=true;
            break;
        }
    }
    
    //Check if input and output are independent
    for(std::vector<depInfo>::iterator st = storeDepInfo.begin(); st != storeDepInfo.end(); ++st) {
        for(std::vector<depInfo>::iterator it = loadDepInfo.begin(); it != loadDepInfo.end(); ++it) {   
            //check if the loopDepth is greater than or equal to loop depth
            if(st->array == it->array){
                
                //check if the iterator is the same
                //NOTE can be improved
                if(st->index != it->index){
                    hasLoopDependencies=true;
                    break;
                }
            }
        }
        if(hasLoopDependencies)
            break;
    }
    max_codegen_loop_depth = minDepthLoop->getLoopDepth();   
    return false;
}


int htrop::DepDetect::getMaxCodegenLoopDepth(){
    return max_codegen_loop_depth;
}


void htrop::DepDetect::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
    AU.addRequired<LoopInfoWrapperPass>();
}
