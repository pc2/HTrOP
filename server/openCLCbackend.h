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

#ifndef OPENCLCBACKEND_H
#define OPENCLCBACKEND_H

#include "../common/hds.pb.h"
#include "llvm/IR/Module.h"

#include <string>

class OpenCLCBackend {

 public:
    //              OpenCLCBackend(llvm::Module *&oclMod);
    OpenCLCBackend(llvm::Module * &oclModArg, HTROP_PB::Message_RCRS * codeGenMsgFromClient, HTROP_PB::Message_RSRC * codeGenMsgFromServer, std::string oclFileOnDisk);

    void generateOpenCLCode(unsigned int unrollFactorOuter, unsigned int unrollFactorInner);
    void generateOpenCLCode();
    //     virtual ~OpenCLCBackend();

 private:
     llvm::Function * func_get_global_id;
     std::string openCLCFile;
     std::string logFile;
     std::string generateAxtorCodeForKernel(llvm::Function * &kernel);

     llvm::Module * originalOclMod;
     std::unique_ptr < llvm::Module > oclModPtr;
     llvm::Module * oclMod;

     HTROP_PB::Message_RCRS * codeGenMsgFromClient;
     HTROP_PB::Message_RSRC * codeGenMsgFromServer;
     std::string oclFileOnDisk;

    void addOCLFunctions(llvm::Module * &oclModArg);

};

#endif                          // OPENCLCBACKEND_H
