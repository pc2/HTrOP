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

#ifndef HTROPCLIENT_H
#define HTROPCLIENT_H

#include <string>
#include <list>
#include <cstdarg>

#include "../consts.h"
#include "pass/scopdetect.h"
#include "pass/scopdependency.h"
#include "pass/accscore.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Support/CommandLine.h"
#include<netdb.h>
#include<mutex>
#include<thread>
#include<stdint.h>

#include "../common/hds.pb.h"
#include "../common/sharedStructCompileRuntime.h"

class HTROPClient {

    //LLVM infrastructure
    std::string IRFilename;
    std::vector < std::string > *inputArgv;

    llvm::SMDiagnostic Err;
    llvm::LLVMContext * context;

    llvm::Module * programMod;
    std::unique_ptr < Module > programModPtr;
    llvm::ExecutionEngine * exeEngine;

    static void *runExeEngineWrapper(void *object);
    void runExeEngine();
     std::thread * exeEngineThread;

    //Communication properties
    int orch_sockfd, htrop_server_sockfd;
     std::string orch_server_name, htrop_server_name;
    unsigned int htrop_server_portno = HTROP_SERVER_PORT;
    struct sockaddr_in htrop_server_addr;
    struct hostent *htrop_server;

    //Specialized handlers
    int handleLLVMIRReq(const char *recvMessageBuffer, int messageSize);        //htropServer
    int handleCompiledBinary(const char *recvMessageBuffer, int messageSize);   //htropServer
    int sendCodeGenReq();

    //Handle requests and responses
    void handleHTROPServerCommunication();
    static void *handleHTROPServerCommunicationWrapper(void *object);

    // Limit depth. Automatically detected by default. Can be explicitly overwritten by the user. 
    std::vector < int > maxScopLoopDepth;
    std::vector < int > maxCogeGenLoopDepth;

    std::string target = "NONE";

    int blockSizeDim0 = 0;
    int blockSizeDim1 = 0;

    typedef std::map < std::string, ScopDS * >ScopDSMap;
    ScopDSMap scopList;

     std::vector < ScopCallDS * >scopCallList;
     llvm::Function * scopFunctionParent;

    void addOCLInitializationFunction(Module * &programMod, HTROP_PB::Message_RSRC * codeGenMsgFromServer, std::string oclKernelFilePath);
    void handleOclBinary(Module * &programMod, char *function_binary_buffer, HTROP_PB::Message_RSRC * codeGenMsgFromServer);
     llvm::Value * resolveScopValue(ScopFnArg * scopArg, std::vector < ScopFnArg * >*scopFunctonArgs, llvm::BasicBlock ** start_block, llvm::BasicBlock * label_lpad, llvm::Function * function,
                                    llvm::Module * &programMod);
    int runScopDetection(Function *function);
    ScopCallDS *getFirstScopWithId(std::string scopId);
    ConstantInt *resolveBufferType(DataTransferType dtType);
    ConstantInt *resolveDeviceType(std::string deviceType);
    ConstantInt *resolveSizeOf(Value * arg);
    const HTROP_PB::Message_RSRC::ScopFunctionOCLInfo * getServerInfo(HTROP_PB::Message_RSRC * codeGenMsgFromServer, std::string scopName);
    Function *resolveKernelArgFunction(ScopFnArg * scopArg);
    void createLocalAndGlobalWorkGroups(const HTROP_PB::Message_RSRC::ScopFunctionOCLInfo * scopServerInfo, BasicBlock * insertCallIntoBlock, int blockSizeDim0, int blockSizeDim1,
                                        Value ** ptr_arraydecay, Value ** ptr_arraydecay_local);
    void setupCatchForInvoke(BasicBlock * label_lpad, BasicBlock * retBlock);
    bool processed = false;
    bool flag_chain_interrupt = false;

 public:
     HTROPClient(std::string orchServerName, std::string htropServerName, int portNumber, std::string IRFilename, std::vector < int > maxCogeGenLoopDepth, std::vector < int > maxScopLoopDepth, int blockSizeDim0, int blockSizeDim1,
                 std::string target, std::vector < std::string > *InputArgv);
    int connectToHTROPServer(); //Establishes hte connections to the RTSC_Server and orchestrator
    void handleRequests();      //Start waiting for requests from the orchestrator and RTSC_Server
    int analyseScopDependency();        // Detect Orchestrator components.
    int analyseScop();          // Detect Scops in functions.
    int startAppExecutionSequential();  //Start executing the application on same thread
    void initializeExecutionEngine();   //intitalze the execution engine
    void initializeLLVMModule();        //intitalze the LLVM module  
    void prepareIRForPolly();
    bool finished();
    void extendLLVMModule();
    void exportToFile(std::string fileName);

    ~HTROPClient();
};

#endif                          // HTROPCLIENT_H
