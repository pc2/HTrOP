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

#ifndef LLVMHELPER_H
#define LLVMHELPER_H

#include <string>
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "hds.pb.h"
#include "sharedStructCompileRuntime.h"

std::string exportFunctionIntoBitcode(llvm::Module * ProgramMod, std::vector < llvm::Function * >functionstoBeExported, std::vector < llvm::GlobalVariable * >globalsToExport);
llvm::Function * addFunctionDefinition(llvm::Function * functionToAccelerate, std::string acceleratedFunctionName, llvm::Module * &programMod);
llvm::Function * createWrapper(llvm::Function * functionToAccelerate, llvm::Function * acceleratedFunction, std::string wrapperName, llvm::Module * &programMod);
llvm::Function * createClWrapper(llvm::Function * functionToAccelerate, llvm::Type * openClDeviceType, std::string wrapperName, llvm::Module * &programMod);

//Creates a global bool and initializes it to true
llvm::Value * createGlobalBool(llvm::Module * &programMod);
llvm::GlobalVariable * createGlobalFunctionPointer(llvm::Module * &programMod, llvm::Function * functionToPointTo);
void canonicalize_llvm_ir(llvm::Module * &ProgramMod);
void removeDependencies(llvm::Function * function);     //Inline all calls to internaly defined functions
int fileToBuffer(std::string filePath, char *&fileBuffer);
bool isIntegerType(llvm::Value * argValue);

llvm::Function * get_func_get_local_id(llvm::Module * mod);

llvm::Function * createFunctionFrom(llvm::Function * originalFunction, std::string newFunctionName, llvm::Module * &programMod, bool addDevice);
llvm::AllocaInst * createLlvmString(std::string strValue, llvm::BasicBlock ** start_block, llvm::BasicBlock * label_lpad, llvm::Function * function, llvm::Module * &programMod);
llvm::Argument * getArg(llvm::Function * function, int index);

llvm::Value * castTo64(llvm::Value * argValue, llvm::BasicBlock * block);

#endif                          //LLVMHELPER_H
