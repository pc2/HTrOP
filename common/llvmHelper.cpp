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

#include "llvmHelper.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"

#if LLVM_VERSION == 3 && LLVM_MINOR_VERSION < 5
#include "llvm/Analysis/Verifier.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Linker.h"
#else
#include "llvm/IR/Verifier.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/Linker/Linker.h"
#include "llvm/IR/InstIterator.h"
#endif

#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <iostream>
#include <fstream>
#include "llvm/Support/SourceMgr.h"
#include "llvm/IRReader/IRReader.h"
#include <cstdlib>
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Transforms/IPO.h"
#include <memory>

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

//Add the function definition for the accelerated function
llvm::Function * addFunctionDefinition(llvm::Function * functionToAccelerate, std::string acceleratedFunctionName, llvm::Module * &programMod) {

    return llvm::Function::Create(llvm::cast < llvm::FunctionType > (functionToAccelerate->getType()->getElementType()), llvm::GlobalValue::ExternalLinkage, acceleratedFunctionName, programMod);
}

llvm::Argument * getArg(llvm::Function * function, int index) {
    int varIndex = 0;

    for (auto argIter = function->arg_begin(); argIter != function->arg_end(); argIter++) {
        if (varIndex == index) {
            return (llvm::Argument *) argIter;;
        }
        varIndex++;
    }
    return NULL;
}

bool isIntegerType(llvm::Value * argValue) {
    if (argValue->getType() == IntegerType::get(llvm::getGlobalContext(), 32) || argValue->getType() == IntegerType::get(llvm::getGlobalContext(), 64)
        || argValue->getType() == IntegerType::get(llvm::getGlobalContext(), 8))
        return true;
    return false;
}

llvm::Value * castTo64(llvm::Value * argValue, llvm::BasicBlock * block) {
    Value *replace_param = argValue;

    if (argValue->getType() == IntegerType::get(llvm::getGlobalContext(), 32)) {
        replace_param = new ZExtInst(argValue, IntegerType::get(block->getContext(), 64), "conv", block);
    }
    return replace_param;
}

//Create a wrapper for the accelerated function
llvm::Function * createClWrapper(llvm::Function * functionToAccelerate, llvm::Type * openClDeviceType, std::string wrapperName, llvm::Module * &programMod) {

    auto functionType = llvm::cast < llvm::FunctionType > (functionToAccelerate->getType()->getElementType());

    std::vector < llvm::Type * >theArgTypes;
    for (int i = 0; i < functionType->getNumParams(); i++) {
        auto argType = functionType->getParamType(i);

        theArgTypes.push_back(argType);
    }

    theArgTypes.push_back(openClDeviceType);

    llvm::FunctionType * functType = llvm::FunctionType::get(functionType->getReturnType(), theArgTypes, false);
    llvm::Function * wrapperFunction = llvm::Function::Create(functType, functionToAccelerate->getLinkage(), wrapperName, programMod);
    llvm::Function * personality = programMod->getFunction("__gxx_personality_v0");
    wrapperFunction->setPersonalityFn(personality);

    llvm::BasicBlock * ret = llvm::BasicBlock::Create(llvm::getGlobalContext(), "return", wrapperFunction);
    llvm::IRBuilder <> builder(ret);
    builder.CreateRetVoid();

    return wrapperFunction;
}

llvm::AllocaInst * createLlvmString(std::string strValue, llvm::BasicBlock ** start_block, llvm::BasicBlock * label_lpad, llvm::Function * function, llvm::Module * &programMod) {

    ConstantInt *const_int32_0 = ConstantInt::get(programMod->getContext(), APInt(32, StringRef("0"), 10));

    //create the corresponding global string in LLVM IR
    Function *func__ZNSaIcEC1Ev = programMod->getFunction("_ZNSaIcEC1Ev");

    assert(func__ZNSaIcEC1Ev != nullptr);

    ArrayType *ArrayTy_str = ArrayType::get(IntegerType::get(programMod->getContext(), 8), strValue.size() + 1);        // array / string size
    GlobalVariable *gvar_array__str = new GlobalVariable( /*Module= */ *programMod,
                                                         /*Type= */ ArrayTy_str,
                                                         /*isConstant= */ true,
                                                         /*Linkage= */ GlobalValue::InternalLinkage,
                                                         /*Initializer= */ 0,
                                                         // has initializer, specified below
                                                         /*Name= */ ".strValue");

    Constant *formulaStr = ConstantDataArray::getString(programMod->getContext(), strValue, true);

    gvar_array__str->setInitializer(formulaStr);
    BasicBlock *label_invoke_cont1 = BasicBlock::Create(programMod->getContext(), "invoke.cont", function, label_lpad);

//     StructType *StructTy_class_std__allocator = programMod->getTypeByName("class.std::ios_base::Init");
    StructType *StructTy_class_std__allocator = programMod->getTypeByName("class.std::allocator");
    AllocaInst *ptr_ref_tmp2 = new AllocaInst(StructTy_class_std__allocator, "ref.tmp", *start_block);
    CallInst *call_ZNSaIcEC1Ev = CallInst::Create(func__ZNSaIcEC1Ev, ptr_ref_tmp2, "", *start_block);

    std::vector < Constant * >const_ptr_kernels_indices;
    const_ptr_kernels_indices.push_back(const_int32_0);
    const_ptr_kernels_indices.push_back(const_int32_0);
    Constant *const_ptr_247 = ConstantExpr::getGetElementPtr(cast < PointerType > (gvar_array__str->getType()->getScalarType())->getContainedType(0u), gvar_array__str, const_ptr_kernels_indices);

    StructType *StructTy_class_std__basic_string = programMod->getTypeByName("class.std::basic_string");
    AllocaInst *ptr_ref_formula = new AllocaInst(StructTy_class_std__basic_string, "ref.strValue", *start_block);

    std::vector < Value * >ZNSsC1EPKcRKSaIcE_params;
    ZNSsC1EPKcRKSaIcE_params.push_back(ptr_ref_formula);
    ZNSsC1EPKcRKSaIcE_params.push_back(const_ptr_247);
    ZNSsC1EPKcRKSaIcE_params.push_back(ptr_ref_tmp2);

    Function *func__ZNSsC1EPKcRKSaIcE = programMod->getFunction("_ZNSsC1EPKcRKSaIcE");

    assert(func__ZNSsC1EPKcRKSaIcE != nullptr);
    InvokeInst *invoke_ZNSsC1EPKcRKSaIcE = InvokeInst::Create(func__ZNSsC1EPKcRKSaIcE, label_invoke_cont1, label_lpad, ZNSsC1EPKcRKSaIcE_params, "", *start_block);

    *start_block = label_invoke_cont1;
    return ptr_ref_formula;

}

//Create a wrapper for the accelerated function
llvm::Function * createFunctionFrom(llvm::Function * originalFunction, std::string newFunctionName, llvm::Module * &programMod, bool addDevice) {

    auto functionType = llvm::cast < llvm::FunctionType > (originalFunction->getType()->getElementType());

    std::vector < llvm::Type * >theArgTypes;
    for (int i = 0; i < functionType->getNumParams(); i++) {
        auto argType = functionType->getParamType(i);

        theArgTypes.push_back(argType);
    }
    if (addDevice) {
        theArgTypes.push_back(IntegerType::get(programMod->getContext(), 32));
    }
    std::string functionName = newFunctionName + originalFunction->getName().str();

    llvm::FunctionType * functType = llvm::FunctionType::get(functionType->getReturnType(), theArgTypes, false);
    llvm::Function * transferFunction = llvm::Function::Create(functType, originalFunction->getLinkage(), functionName, programMod);

    return transferFunction;
}

//Create a wrapper for the accelerated function
llvm::Function * createWrapper(llvm::Function * functionToAccelerate, llvm::Function * acceleratedFunction, std::string wrapperName, llvm::Module * &programMod) {

    auto functionType = llvm::cast < llvm::FunctionType > (functionToAccelerate->getType()->getElementType());

    std::vector < llvm::Type * >theArgTypes;
    for (int i = 0; i < functionType->getNumParams(); i++) {
        auto argType = functionType->getParamType(i);

        theArgTypes.push_back(argType);
    }

    llvm::FunctionType * functType = llvm::FunctionType::get(functionType->getReturnType(), theArgTypes, false);
    llvm::Function * wrapperFunction = llvm::Function::Create(functType, functionToAccelerate->getLinkage(), wrapperName, programMod);

    //Add the basic block structure
    llvm::BasicBlock * entry = llvm::BasicBlock::Create(llvm::getGlobalContext(), "entry", wrapperFunction);
    llvm::BasicBlock * sendData = llvm::BasicBlock::Create(llvm::getGlobalContext(), "send_data", wrapperFunction);
    llvm::BasicBlock * acceleratorCall = llvm::BasicBlock::Create(llvm::getGlobalContext(), "acc_call", wrapperFunction);
    llvm::BasicBlock * recieiveData = llvm::BasicBlock::Create(llvm::getGlobalContext(), "recieive_data", wrapperFunction);
    llvm::BasicBlock * ret = llvm::BasicBlock::Create(llvm::getGlobalContext(), "return", wrapperFunction);

    llvm::IRBuilder <> builder(entry);
    builder.CreateBr(sendData);

    builder.SetInsertPoint(sendData);
    builder.CreateBr(acceleratorCall);

    builder.SetInsertPoint(acceleratorCall);

    //set up the arguments
    std::vector < llvm::Value * >localArgs;
    for (llvm::Function::arg_iterator I = wrapperFunction->arg_begin(), E = wrapperFunction->arg_end(); I != E; ++I) {
        localArgs.push_back((llvm::Argument *) I);
    }

    llvm::Value * retValue;
    if (acceleratedFunction->getReturnType()->isVoidTy())
        builder.CreateCall(acceleratedFunction, localArgs);
    else
        retValue = builder.CreateCall(acceleratedFunction, localArgs, "accelerated_call");
    builder.CreateBr(recieiveData);

    builder.SetInsertPoint(recieiveData);
    builder.CreateBr(ret);

    builder.SetInsertPoint(ret);
    if (acceleratedFunction->getReturnType()->isVoidTy())
        builder.CreateRetVoid();
    else
        builder.CreateRet(retValue);

    return wrapperFunction;
}

llvm::Value * createGlobalBool(llvm::Module * &programMod) {

    // Global Variable Declarations
    llvm::GlobalVariable * globalVariable = new llvm::GlobalVariable( /*Module= */ *programMod,
                                                                     /*Type= */ llvm::IntegerType::get(programMod->getContext(), 8),
                                                                     /*isConstant= */ false,
                                                                     /*Linkage= */ llvm::GlobalValue::ExternalLinkage,
                                                                     /*Initializer= */ 0,
                                                                     /*Name= */ "registerBool");
    globalVariable->setAlignment(1);

    // Constant Definitions
    llvm::ConstantInt * const_int = llvm::ConstantInt::get(programMod->getContext(), llvm::APInt(8, llvm::StringRef("1"), 10));

    // Global Variable Definitions
    globalVariable->setInitializer(const_int);

    return globalVariable;
}

llvm::GlobalVariable * createGlobalFunctionPointer(llvm::Module * &programMod, llvm::Function * functionToPointTo) {

    std::vector < llvm::Type * >theArgTypes;
    for (auto argIter = functionToPointTo->arg_begin(); argIter != functionToPointTo->arg_end(); argIter++) {
        theArgTypes.push_back(argIter->getType());
    }

    FunctionType *FuncTy_2 = FunctionType::get(
                                                  /*Result= */ functionToPointTo->getReturnType(),
                                                  /*Params= */ theArgTypes,
                                                  /*isVarArg= */ false);

    PointerType *PointerTy_1 = PointerType::get(FuncTy_2, 0);

    GlobalVariable *globalVariable = new GlobalVariable( /*Module= */ *programMod,
                                                        /*Type= */ PointerTy_1,
                                                        /*isConstant= */ false,
                                                        /*Linkage= */ GlobalValue::ExternalLinkage,
                                                        /*Initializer= */ 0,
                                                        /*Name= */ "funPointer");

    globalVariable->setAlignment(8);

    ConstantPointerNull *const_ptr_52 = ConstantPointerNull::get(PointerTy_1);

    globalVariable->setInitializer(const_ptr_52);

    return globalVariable;
}

std::string exportFunctionIntoBitcode(llvm::Module * ProgramMod, std::vector < llvm::Function * >functionstoBeExported, std::vector < llvm::GlobalVariable * >globalsToExport) {        // See LLVM CloneModule.cpp

    //Clone the module
    std::unique_ptr < Module > EM = llvm::CloneModule(ProgramMod);
    llvm::Module * ExportModule = EM.get();

    // Use SetVector to avoid duplicates.
    std::vector < llvm::GlobalValue * >GVs;

    for (int i = 0; i < functionstoBeExported.size(); i++)
        GVs.push_back(ExportModule->getFunction(functionstoBeExported[i]->getName().str()));

    for (int i = 0; i < globalsToExport.size(); i++)
        GVs.push_back(ExportModule->getNamedGlobal(globalsToExport[i]->getName().str()));

    // In addition to deleting all other functions, we also want to spiff it
    // up a little bit.  Do this now.
    llvm::legacy::PassManager Passes;

    Passes.add(llvm::createGVExtractionPass(GVs, false));
    Passes.add(llvm::createGlobalDCEPass());    // Delete unreachable globals
    Passes.add(llvm::createStripDeadDebugInfoPass());   // Remove dead debug info
    Passes.add(llvm::createStripDeadPrototypesPass());  // Remove dead func decls
    Passes.run(*ExportModule);

#if LLVM_VERSION == 3 && LLVM_MINOR_VERSION < 5
    llvm::verifyModule(*ExportModule, llvm::PrintMessageAction);
#else
    llvm::verifyModule(*ExportModule, &llvm::errs());
#endif

    //Write ExportModule to IR file
    std::string moduleBitcode;
    llvm::raw_string_ostream moduleStream(moduleBitcode);
    moduleStream << *ExportModule;      // human readable IR
    moduleStream.flush();
    return moduleBitcode;
}

void canonicalize_llvm_ir(llvm::Module * &ProgramMod) {

    // Build up all of the passes that we want to run on the module.
    llvm::legacy::PassManager pm;

    //Phase I: Normalization Passes 
    //WARNING: createPromoteMemoryToRegisterPass sometimes causes issues with the OrchDetect pass
    pm.add(llvm::createPromoteMemoryToRegisterPass());  //Limit use of memory, increasing the effectiveness of subsequent passes
    pm.add(llvm::createLoopSimplifyPass());     //Canonicalize loop shape, lower burden of writing passes
    pm.add(llvm::createLCSSAPass());    //Keep effects of subsequent loop optimizations local, limiting overhead of maintaining SSA form
    pm.add(llvm::createIndVarSimplifyPass());   //Normalize induction variables, highlighting the canonical induction variable

    //Phase II: Code Optimization Passes  
    pm.add(llvm::createLICMPass());     //Loop Invariant Code Motion
    pm.add(llvm::createConstantPropagationPass());      //Merge Duplicate Global Constants   
    pm.add(llvm::createInstructionCombiningPass());     //Combine redundant instructions

    //Dead code Elmination
    pm.add(llvm::createDeadCodeEliminationPass());
    pm.add(llvm::createDeadStoreEliminationPass());

    //Verify the module
    pm.add(llvm::createVerifierPass());

    //Execute all the passes
    pm.run(*ProgramMod);
}

void removeDependencies(llvm::Function * function) {
    bool check = true;

    while (check) {
        check = false;
        //Iterate through all instuctions of the funcion and search for call instructions
        for (llvm::inst_iterator I = llvm::inst_begin(function), E = llvm::inst_end(function); I != E; ++I) {
            if (llvm::CallInst * callInst = llvm::dyn_cast < llvm::CallInst > (&*I)) {
                //We can inline the function if it defined in the same module
                if (!callInst->getCalledFunction()->isDeclaration()) {
                    check = true;
                    llvm::InlineFunctionInfo IFI;
                    llvm::InlineFunction(callInst, IFI);
                    break;
                }
            }
        }
    }
}

int fileToBuffer(std::string filePath, char *&fileBuffer) {
    std::streampos size;
    std::ifstream file(filePath.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        size = file.tellg();
        fileBuffer = (char *)calloc(size, sizeof(char));
        file.seekg(0, std::ios::beg);
        file.read(fileBuffer, size);
        file.close();
        return size;
    }
    return -1;

}

llvm::Function * get_func_get_local_id(llvm::Module * mod) {

    //Setup the get_local_id function
    Function *func_get_local_id = mod->getFunction("get_local_id");

    if (!func_get_local_id) {
        std::vector < Type * >FuncTy_8_args;
        FuncTy_8_args.push_back(IntegerType::get(mod->getContext(), 32));
        FunctionType *FuncTy_8 = FunctionType::get(
                                                      /*Result= */ IntegerType::get(mod->getContext(), 32),
                                                      /*Params= */ FuncTy_8_args,
                                                      /*isVarArg= */ false);

        func_get_local_id = Function::Create(
                                                /*Type= */ FuncTy_8,
                                                /*Linkage= */ GlobalValue::ExternalLinkage,
                                                /*Name= */ "get_local_id", mod);
        func_get_local_id->setCallingConv(CallingConv::C);

        AttributeSet func_get_local_id_PAL;

        {
            SmallVector < AttributeSet, 4 > Attrs;
            AttributeSet PAS;

            {
                AttrBuilder B;

                B.addAttribute(Attribute::NoUnwind);
                B.addAttribute(Attribute::ReadNone);
                PAS = AttributeSet::get(mod->getContext(), ~0U, B);
            }

            Attrs.push_back(PAS);
            func_get_local_id_PAL = AttributeSet::get(mod->getContext(), Attrs);

        }
        func_get_local_id->setAttributes(func_get_local_id_PAL);
    }
    return func_get_local_id;
}
