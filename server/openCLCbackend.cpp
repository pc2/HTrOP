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

#include "openCLCbackend.h"

#include <string>
#include <iostream>
#include <stdio.h>
#include <fstream>

#include "axtor_ocl/OCLModuleInfo.h"
#include "axtor_ocl/OCLBackend.h"
#include "axtor/Axtor.h"

#include "llvm/IR/LegacyPassManager.h"

#include "axtor/metainfo/ModuleInfo.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Analysis/ScalarEvolution.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"

#include "llvm/Transforms/Scalar.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"

#if LLVM_VERSION == 3 && LLVM_MINOR_VERSION < 5
#include "llvm/Support/InstIterator.h"
#else
#include "llvm/IR/InstIterator.h"
#endif

#include "cl_replace.h"
#include <assert.h>
#include <boost/concept_check.hpp>

#include "../../common/stringHelper.h"

#if MEASURE
#include <chrono>
#endif

using namespace llvm;

//Adds the OpenCL function definition to the module
void OpenCLCBackend::addOCLFunctions(llvm::Module * &oclModArg) {

    //Setup all the get_global_id function
    std::vector < Type * >FuncTy_8_args;
    FuncTy_8_args.push_back(IntegerType::get(oclModArg->getContext(), 32));
    FunctionType *FuncTy_8 = FunctionType::get(
                                                  /*Result= */ IntegerType::get(oclModArg->getContext(), 32),
                                                  /*Params= */ FuncTy_8_args,
                                                  /*isVarArg= */ false);

    func_get_global_id = oclModArg->getFunction("get_global_id");
    if (!func_get_global_id) {
        func_get_global_id = Function::Create(
                                                 /*Type= */ FuncTy_8,
                                                 /*Linkage= */ GlobalValue::ExternalLinkage,
                                                 /*Name= */ "get_global_id", oclModArg);
        func_get_global_id->setCallingConv(CallingConv::C);
    }
    AttributeSet func_get_global_id_PAL;

    {
        SmallVector < AttributeSet, 4 > Attrs;
        AttributeSet PAS;

        {
            AttrBuilder B;

            B.addAttribute(Attribute::NoUnwind);
            B.addAttribute(Attribute::ReadNone);
            PAS = AttributeSet::get(oclModArg->getContext(), ~0U, B);
        }

        Attrs.push_back(PAS);
        func_get_global_id_PAL = AttributeSet::get(oclModArg->getContext(), Attrs);
    }
    func_get_global_id->setAttributes(func_get_global_id_PAL);
}

OpenCLCBackend::OpenCLCBackend(llvm::Module * &oclModArg, HTROP_PB::Message_RCRS * codeGenMsgFromClient, HTROP_PB::Message_RSRC * codeGenMsgFromServer, std::string oclFileOnDisk) {
    originalOclMod = oclModArg;
    OpenCLCBackend::codeGenMsgFromClient = codeGenMsgFromClient;
    OpenCLCBackend::codeGenMsgFromServer = codeGenMsgFromServer;
    OpenCLCBackend::oclFileOnDisk = oclFileOnDisk;
    generateOpenCLCode();
}

void OpenCLCBackend::generateOpenCLCode() {

    std::ofstream outStream;
    outStream.open(oclFileOnDisk.c_str(), std::ios::out);
    logFile = oclFileOnDisk + ".log";
    llvm::raw_ostream * logOut;
    std::error_code EC;
    logOut = new llvm::raw_fd_ostream(logFile.c_str(), EC, llvm::sys::fs::F_None);
    axtor::Log::init(*logOut);

#ifdef HTROP_DEBUG
    std::cout << "\n OCL File : " << oclFileOnDisk;
    std::cout.flush();
#endif

    //Process each kernel independently
    for (int scopFunctionIter = 0; scopFunctionIter < codeGenMsgFromClient->scoplist_size(); scopFunctionIter++) {

#ifdef HTROP_DEBUG
        std::cout << "\n SCOP Function: " << codeGenMsgFromClient->scoplist(scopFunctionIter).scopfunctionname();
        std::cout << " - max codegen  " << codeGenMsgFromClient->scoplist(scopFunctionIter).max_codegen_loop_depth();
        std::cout.flush();
#endif

        //We do this for every kernel because Axtor changes the Source
        //clone the original module
        oclModPtr = llvm::CloneModule(originalOclMod);
        if (!oclModPtr) {
            std::cout << "\nError: Cannot clone OpenCL module";
            exit(1);
        }
        oclMod = oclModPtr.get();
        if (!oclMod) {
            std::cout << "\nError: Cannot clone OpenCL module";
            exit(1);
        }
        addOCLFunctions(oclMod);

        HTROP_PB::Message_RSRC::ScopFunctionOCLInfo * scopFunctionInfo = codeGenMsgFromServer->add_scopfunctions();
        auto clientKernelInfo = codeGenMsgFromClient->scoplist(scopFunctionIter);

        //Save scopFunctionInfo for simplicity
        std::string kernelName = clientKernelInfo.scopfunctionname();
        scopFunctionInfo->set_scopfunctionname(kernelName);
        scopFunctionInfo->set_scopoclkernelname(kernelName);

        unsigned int max_codegen_loop_depth = clientKernelInfo.max_codegen_loop_depth();

        //get the kernel
        llvm::Function * kernelFunction = oclMod->getFunction(kernelName);

        //Get the loop analysis
        llvm::DominatorTree * DT = new llvm::DominatorTree();
        DT->recalculate(*kernelFunction);

        //generate the LoopInfoBase for the current function
        llvm::LoopInfoBase < llvm::BasicBlock, llvm::Loop > *KLoop = new llvm::LoopInfoBase < llvm::BasicBlock, llvm::Loop > ();
        KLoop->releaseMemory();
        KLoop->analyze(*DT);

        LoopInfo::iterator l = KLoop->begin();
        assert(l != KLoop->end());

        llvm::Loop * outerLoop = *l;
        // The loops are stored in this list.
        std::vector < Loop * >loopList;
        loopList.push_back(outerLoop);

        // Create the loop list
        auto subLoops = outerLoop->getSubLoops();

        while (subLoops.size() > 0 && loopList.size() < max_codegen_loop_depth) {
            loopList.push_back(subLoops[0]);
            subLoops = subLoops[0]->getSubLoops();
        }

        //Get the last block / exit block
        llvm::BasicBlock * last_block = NULL;
        for (Function::iterator i = outerLoop->getHeader()->getParent()->begin(), e = outerLoop->getHeader()->getParent()->end(); i != e; ++i) {
            last_block = (&*i);
        }

        assert(last_block != NULL);

        //Go over each of the two loops.
        for (unsigned int loopIte = 0; loopIte < loopList.size() && loopIte < max_codegen_loop_depth; loopIte++) {

            llvm::Loop * loop = loopList[loopIte];
            llvm::PHINode * phi = dyn_cast < llvm::PHINode > (loop->getHeader()->begin());

            int numIncommigVals = phi->getNumIncomingValues();

            assert(numIncommigVals == 2);

            //find the coresponding compare instructions
            llvm::Instruction * inst = dyn_cast < llvm::Instruction > (phi->getIncomingBlock(numIncommigVals - 1)->begin());

            while (!isa < ICmpInst > (inst))
                inst = inst->getNextNode();

            llvm::ICmpInst * cmpInst = dyn_cast < ICmpInst > (inst);

            while (!isa < BranchInst > (inst))
                inst = inst->getNextNode();

            llvm::BranchInst * branchInst = dyn_cast < BranchInst > (inst);

            int phi_offset = 0;
            ConstantInt *constInt = NULL;
            Value *phi_offsetValue = phi->getIncomingValue(0);

            if (isa < llvm::ConstantInt > (phi_offsetValue)) {
                constInt = dyn_cast < llvm::ConstantInt > (phi_offsetValue);
                phi_offset = constInt->getSExtValue();
            }

            //find the position of the iterator limit
            bool foundArg = false;

            for (unsigned int oper_iter = 0; oper_iter < cmpInst->getNumOperands(); oper_iter++) {
                unsigned int pos = 0;

                for (llvm::Function::arg_iterator arg_I = kernelFunction->arg_begin(); arg_I != kernelFunction->arg_end(); arg_I++) {

                    Value *instCmp = cmpInst->getOperand(oper_iter);

                    if (llvm::SExtInst * sextInst = dyn_cast < SExtInst > (instCmp)) {
                        instCmp = sextInst->getOperand(0);
                    }

                    if (instCmp == dyn_cast < Value > (arg_I)) {
                        scopFunctionInfo->add_workgroup_arg_index(pos);
                        scopFunctionInfo->add_workgroup_arg_index_offset(phi_offset);
                        foundArg = true;
                        break;
                    }
                    pos++;
                }
            }

            //If not found in the argument list then we have a problem
            assert(foundArg);

            //remove unnecesary instructions from the header
            llvm::BasicBlock * bb = phi->getParent();

            //insert the call to get_global_id
            ConstantInt *const_int32_14 = ConstantInt::get(oclMod->getContext(), APInt(32, StringRef(std::to_string(loopIte)), 10));

            llvm::CallInst * callInst = llvm::CallInst::Create(func_get_global_id, const_int32_14, "call_ggi", phi);

            //check the data-type size
            llvm::Instruction * expandednCallInst = callInst;
            if (phi->getType() == llvm::Type::getInt64Ty(oclMod->getContext())) {
                //Upgrade to 64 bit
                expandednCallInst = new llvm::SExtInst(callInst, llvm::Type::getInt64Ty(oclMod->getContext()), "scale", phi);
            }

            //insert compare and jump
            if (phi_offset > 0) {
                branchInst->getParent()->getNextNode();
                llvm::BasicBlock * tmpBlock = llvm::BasicBlock::Create(oclMod->getContext(), "branch_cmp", branchInst->getParent()->getParent(), branchInst->getParent()->getNextNode());
                assert(constInt != NULL);
                llvm::ICmpInst * icmpInst = new llvm::ICmpInst(*tmpBlock, llvm::ICmpInst::ICMP_SLT, callInst, constInt, "icmpInst");
                llvm::BranchInst::Create(last_block, branchInst->getSuccessor(1), icmpInst, tmpBlock);
                branchInst->setSuccessor(1, tmpBlock);
            }

            //go to the latch and replace all the contents with a branch to the next block
            bb = loop->getLoopLatch();

            //Find the corresponding increment instruction that uses the PHINode
            BasicBlock::iterator tmp_I = bb->end();
            llvm::Instruction * instIter = dyn_cast < Instruction > (--tmp_I);

            while (!isa < AddOperator > (instIter)) {
                llvm::Instruction * inst = instIter;
                instIter = instIter->getPrevNode();
                inst->dropAllReferences();
                inst->eraseFromParent();
            }

            //Check if the instruction is the same as the Phi value
            for (unsigned int phiValIter = 0; phiValIter < phi->getNumIncomingValues(); phiValIter++) {
                if (phi->getIncomingValue(phiValIter) == instIter) {
                    phi->removeIncomingValue(phiValIter);
                    instIter->dropAllReferences();
                    instIter->eraseFromParent();
                    break;
                }
            }
            llvm::BranchInst::Create(bb->getNextNode(), bb);

            //replace all instance of the phi with the call (NOTE: unsafe replace)
            while (!phi->use_empty()) {
                auto & U = *phi->use_begin();
                //U.set(callInst);
                U.set(expandednCallInst);
            }
            phi->dropAllReferences();
            phi->eraseFromParent();
        }

        std::string kernelCode = generateAxtorCodeForKernel(kernelFunction);

        //CleanUp; remove all other functions
        kernelCode = cleanKernel(kernelCode, kernelName);

        //Add restrict keyword
        kernelCode = addRestrict(kernelCode);

        outStream << "\n\n" << kernelCode;
    }

    //Save the code to file 
    outStream.close();

    delete logOut;
}

//Generate AXTOR code
std::string OpenCLCBackend::generateAxtorCodeForKernel(llvm::Function * &kernel) {

    std::vector < llvm::Function * >kernelList;
    kernelList.push_back(kernel);
    std::ostringstream stream;
    axtor::OCLModuleInfo modInfo = axtor::OCLModuleInfo(kernel->getParent(), kernelList, stream);
    axtor::OCLBackend backend;
    axtor::translateModule(backend, modInfo);
    std::string oclCode = stream.str();
    stream.str("");
    stream.clear();
    return oclCode;
}
