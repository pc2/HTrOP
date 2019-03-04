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

#include "htropclient.h"

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <unistd.h>
#include <cstring>
#include <climits>
#include <cinttypes>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <iostream>
#include <string>
#include <list>

#include "polly/ScopDetection.h"
#include "polly/LinkAllPasses.h"

#include "llvm/IR/DerivedTypes.h"

#include "../consts.h"

#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/TargetSelect.h"
#if LLVM_VERSION == 3 && LLVM_MINOR_VERSION < 5
#include "llvm/Analysis/Verifier.h"
#include "llvm/Linker.h"
#include "llvm/Support/InstIterator.h"
#else
#include "llvm/IR/Verifier.h"
#include "llvm/Linker/Linker.h"
#include "llvm/IR/InstIterator.h"
#endif
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/IRBuilder.h"

#include "llvm/Transforms/Scalar.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "../common/messageHelper.h"
#include "../common/messageTypes.h"
#include "../common/llvmHelper.h"

#include <cxxabi.h>
#include <fstream>
#include <dlfcn.h>
#include <assert.h>

#include <signal.h>
#include <boost/concept_check.hpp>

#include "polly/Canonicalization.h"
#include "polly/ScopDetection.h"

#define DATA_WIDTH 64

#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Pass.h"
#include <CL/cl_platform.h>

HTROPClient::HTROPClient(std::string orchServerName, std::string htropServerName, int portNumber, std::string IRFilename, std::vector < int > maxCogeGenLoopDepth, std::vector < int > maxScopLoopDepth, int blockSizeDim0,
                         int blockSizeDim1, std::string target, std::vector < std::string > *InputArgv) {

    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    orch_sockfd = htrop_server_sockfd = -1;
    htrop_server_portno = portNumber;
    htrop_server_name = htropServerName;
    HTROPClient::IRFilename = IRFilename;
    HTROPClient::maxCogeGenLoopDepth = maxCogeGenLoopDepth;
    HTROPClient::maxScopLoopDepth = maxScopLoopDepth;
    HTROPClient::target = target;
    inputArgv = InputArgv;

    if (blockSizeDim0 == 0)
        blockSizeDim1 = 0;
    else if (blockSizeDim1 == 0)
        blockSizeDim1 = blockSizeDim0;

    HTROPClient::blockSizeDim0 = blockSizeDim0;
    HTROPClient::blockSizeDim1 = blockSizeDim1;

#if defined(FORCE_CLANG_BACKEND)
    std::cout << "\n WARNING: Client compiled with forced CLANG option";
    std::cout.flush();
#endif

}

HTROPClient::~HTROPClient() {
    //Close the sockets
    shutdown(orch_sockfd, 2);
    shutdown(htrop_server_sockfd, 2);

    //Delete all global objects allocated by libprotobuf.
    google::protobuf::ShutdownProtobufLibrary();
}

//BEGIN ESTABLISH CONNECTIONS

//Connect to the HTROP Server
int HTROPClient::connectToHTROPServer() {

    //Connect to the HTROP Server / Code Gen
    htrop_server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (htrop_server_sockfd < 0) {
        std::cerr << "ERROR opening socket (HTROP Server)";
        return -1;
    }
    htrop_server = gethostbyname(htrop_server_name.c_str());
    if (htrop_server == NULL) {
        std::cerr << "ERROR no such host (HTROP Server)";
        return -2;
    }

    bzero((char *)&htrop_server_addr, sizeof(htrop_server_addr));
    htrop_server_addr.sin_family = AF_INET;
    bcopy((char *)htrop_server->h_addr, (char *)&htrop_server_addr.sin_addr.s_addr, htrop_server->h_length);
    htrop_server_addr.sin_port = htons(htrop_server_portno);

    //Connect to remote server
    if (::connect(htrop_server_sockfd, (struct sockaddr *)&htrop_server_addr, sizeof(htrop_server_addr)) < 0) {
        std::cerr << "ERROR connection to HTROP Server failed";
        return -3;
    }
    return 0;
}

//END ESTABLISH CONNECTIONS

//BEGIN

//Handle HTROP communication
void HTROPClient::handleRequests() {
    std::thread clientHTROPServerHandler(&HTROPClient::handleHTROPServerCommunicationWrapper, this);
    clientHTROPServerHandler.detach();
    //Send the request to the server
    sendCodeGenReq();
}

void *HTROPClient::handleHTROPServerCommunicationWrapper(void *object) {
    HTROPClient *clientObject = reinterpret_cast < HTROPClient * >(object);

    clientObject->handleHTROPServerCommunication();
}

//Handle requests from the HTROP Server
void HTROPClient::handleHTROPServerCommunication() {

    //check if there is a connection to the Orchestrator and the HTROP Server
    if (htrop_server_sockfd < 0) {
        std::cerr << "ERROR in socket (HTROP Server)";
        return;
    }

    char recvMessageBufferHTROPServer[1024];
    Message *handleReqMessage = new Message(recvMessageBufferHTROPServer);
    int active = 1;             //used to break out of the loop

    //Wait for incomming API requests from the orchestrator
    while (active) {
#ifdef HTROP_DEBUG
        std::cout << "\n Waiting for request";
        std::cout.flush();
#endif

        handleReqMessage->recv(htrop_server_sockfd, 0);

#ifdef HTROP_DEBUG
        std::cout << "\n Processing request";
        std::cout.flush();
#endif

        switch (handleReqMessage->getType()) {
        case RSP_CODE_GEN_COMPLETE:
            active = handleCompiledBinary(handleReqMessage->getMessageBuffer(), handleReqMessage->getSize());
            break;

        case REQ_LLVM_IR:
            active = handleLLVMIRReq(handleReqMessage->getMessageBuffer(), handleReqMessage->getSize());
            break;

        case -1:               // Disconnection / error

            if (handleReqMessage->getSize() == 0) {
                std::cout << "\nHTROP Server Disconnected ...";
            }
            else if (handleReqMessage->getSize() == -1) {
                std::cout << "recv failed ...";
            }
            active = 0;
            break;

        default:
            std::cout << "\ndefault case ...";
            break;
        }
    }
    delete handleReqMessage;

    return;
}

//END

//BEGIN LLVM

//Initialize the LLVM execution environment
void HTROPClient::initializeLLVMModule() {
    // prepare LLVM ExecutionEngine
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    context = &llvm::getGlobalContext();

    // parse program IR from file
    programModPtr = llvm::parseIRFile(IRFilename, Err, *context);
    if (!programModPtr) {
        Err.print("HTROPclient : ", llvm::errs());
        exit(1);
    }
    programMod = programModPtr.get();
    if (!programMod) {
        Err.print("HTROPclient: ", llvm::errs());
        exit(1);
    }
}

void HTROPClient::extendLLVMModule() {
    std::string dependentPath = "";
#ifdef CMAKE_DEP_PATH
    dependentPath = CMAKE_DEP_PATH;
    dependentPath += "/";
#endif

    //Add additonal OplenCl support functions   
    std::unique_ptr < Module > tmpM = llvm::parseIRFile(std::string(dependentPath + "registerOpenCL.bc").c_str(), Err, *context);
    if (!tmpM) {
        Err.print("HTROPclient : ", llvm::errs());
        exit(1);
    }
    //merge the modules
    llvm::Linker::linkModules(*programMod, std::move(tmpM), llvm::Linker::Flags::None);
    std::unique_ptr < Module > tmpOCL = llvm::parseIRFile(std::string(dependentPath + "oclFunctions.bc").c_str(), Err, *context);
    if (!tmpOCL) {
        Err.print("HTROPclient : ", llvm::errs());
        exit(1);
    }

    llvm::Linker::linkModules(*programMod, std::move(tmpOCL), llvm::Linker::Flags::None);

#ifdef HTROP_DEBUG
    std::cout << "\nINFO : Merged additional modules";
    std::cout.flush();
#endif

}

//Initialize the execution engine
void HTROPClient::initializeExecutionEngine() {
    // create ExecutionEngine
    std::string errStr;
    exeEngine = EngineBuilder(std::move(programModPtr))
        .setErrorStr(&errStr)
        .create();

    if (!exeEngine) {
        errs() << "HTROP Client:: Failed to construct ExecutionEngine: " << errStr << "\n";
        exit(1);
    }
}

void HTROPClient::exportToFile(std::string fileName) {

    std::error_code EC;
    auto out = new raw_fd_ostream(fileName, EC, sys::fs::F_None);

    WriteBitcodeToFile(programMod, *out);

    programMod->dump();

}

void HTROPClient::prepareIRForPolly() {
    /*
     *  -polly-canonicalize -polly-run-inliner \
     *      -polly-detect -polly-scops \
     *       -polly-dce -polly-opt-isl \
     *      -polly-codegen -polly-parallel \
     *      seidel-2d-no-dependency/out/seidel-2d.O0.ll \
     *      | opt -O3 > seidel-2d-no-dependency/out/seidel-2d.openmp.dance.ll
     */

    // Build up all of the passes that we want to run on the module.
    llvm::legacy::PassManager pm;
    pm.add(polly::createPollyCanonicalizePass());
    pm.run(*programMod);
}



int HTROPClient::runScopDetection(Function *function){
    llvm::legacy::FunctionPassManager ScopDetectPM(programMod);
    htrop::ScopDetect * ScopDetectPass = static_cast < htrop::ScopDetect * >(htrop::createScopDetectPass(maxScopLoopDepth));
    ScopDetectPM.add(ScopDetectPass);
    
    // NOTE:
    // if we iterate over several functions then we would overwrite the results.
    // The idea should be to loop over the given functions (analysisCandidates) 
    // and try to match whatever we would like to detect.
    
    ScopDetectPass->clear();
    ScopDetectPM.run(*function);
    
    //Assumption only one scop per function
    if (ScopDetectPass->containsScop()) {
        ScopDS *scopDS = new ScopDS();
        
        scopDS->scopFunction = ScopDetectPass->getScopFunction();
        scopDS->scopFunctionParent = ScopDetectPass->getScopFunctionParent();
        scopDS->scopFunctonArgs = ScopDetectPass->getScopFuncitonArgs();
        scopDS->scopLoopInfo = ScopDetectPass->getScopLoopInfo();
        
        // Get accelerator affinity. 
        // Run the scoring pass.
        llvm::legacy::FunctionPassManager ScopScorePM(programMod);
        htrop::AccScore * ScopScorePass = static_cast < htrop::AccScore * >(htrop::createScoringPass());
        ScopScorePM.add(ScopScorePass);
        ScopScorePM.run(*function);
        
        // Set accelerator affinity.
        ResourceInfo LEG_Resource, MCPU_Resource, GPU_Resource, MIC_Resource;
        
        LEG_Resource.affinity = MCPU_Resource.affinity = GPU_Resource.affinity = MIC_Resource.affinity = 0;
        LEG_Resource.codeGenTime = MCPU_Resource.codeGenTime = GPU_Resource.codeGenTime = MIC_Resource.codeGenTime = 0;
        LEG_Resource.function = MCPU_Resource.function = GPU_Resource.function = MIC_Resource.function = NULL;
        LEG_Resource.registrationFunction = MCPU_Resource.registrationFunction = GPU_Resource.registrationFunction = MIC_Resource.registrationFunction = NULL;
        
        scopDS->resources.insert(std::pair < DeviceType, ResourceInfo > (LEG, LEG_Resource));
        scopDS->resources.insert(std::pair < DeviceType, ResourceInfo > (MCPU, MCPU_Resource));
        scopDS->resources.insert(std::pair < DeviceType, ResourceInfo > (GPU, GPU_Resource));
        scopDS->resources.insert(std::pair < DeviceType, ResourceInfo > (MIC, MIC_Resource));
        
        scopList.insert(std::pair < std::string, ScopDS * >(ScopDetectPass->getScopFunction()->getName(), scopDS));
        scopFunctionParent = ScopDetectPass->getScopFunctionParent(); 
        
        scopDS->maxParalleizationDepth = ScopDetectPass->getMaxCodegenLoopDepth();
        
        #ifdef HTROP_DEBUG
        std::cout << "\nHTROP INFO: Max codegen depth : " << scopDS->maxParalleizationDepth << "\n";
        std::cout.flush();
        #endif
        
        return 0;
    }
    return 1;
}



// Run pass to detect and analysescops in functions.
//   This is the static part. 
int HTROPClient::analyseScop() {

    // Functions to analyse. 
    std::list < llvm::Function * >analysisCandidates;
    for (llvm::Module::iterator I = programMod->begin(); I != programMod->end(); I++) {
        if (I->getName().str().find(CHECK_RESULTS_CPU) != std::string::npos) {
            std::cout << "INFO: " << "Skipped SCoP analysis for: \"" << (*I).getName().str() << "\"\n";
        }
        else {
            analysisCandidates.push_back(&(*I));
        }
    }

    // Run the detection/analysis pass.
    llvm::legacy::FunctionPassManager ScopDetectPM(programMod);

    htrop::ScopDetect * ScopDetectPass = static_cast < htrop::ScopDetect * >(htrop::createScopDetectPass(maxScopLoopDepth));
    ScopDetectPM.add(ScopDetectPass);

    //Go through all the functions and detect scops
    for (const auto & function:analysisCandidates) {
        // NOTE:
        // if we iterate over several functions then we would overwrite the results.
        // The idea should be to loop over the given functions (analysisCandidates) 
        // and try to match whatever we would like to detect.

        int status = runScopDetection(function);
    }
    
    //Search for multiple calls to the same scop
    std::vector<std::string> seenScop;
    struct RepeatedCalls{
        Function *calledF;
        CallInst *callInst;
    };
    std::vector<RepeatedCalls*> repeatedCalls;
    
    // Iterate over instructions of the scop parent
    for (inst_iterator I = inst_begin(scopFunctionParent), E = inst_end(scopFunctionParent); I != E; ++I) {
        // Look for calls.
        if (isa < llvm::CallInst > (&*I)) {
            llvm::CallInst * callInst = dyn_cast < llvm::CallInst > (&*I);
            Function *calledF = callInst->getCalledFunction();
            // Look for calls to functions.
            if (calledF) {
                // Does it point to a Scop?
                std::map < std::string, ScopDS * >::iterator scopListIt;
                scopListIt = scopList.find(calledF->getName());
                
                if (scopListIt != scopList.end()) {
                    //Check if the scop has been called more than once
                    if(std::find(seenScop.begin(), seenScop.end(), calledF->getName()) != seenScop.end()){
                        auto repeatedCall =  new RepeatedCalls();
                        repeatedCall->calledF = calledF;
                        repeatedCall->callInst = callInst;
                        repeatedCalls.push_back(repeatedCall);
                    }
                    else{
                        seenScop.push_back(calledF->getName());
                    }
                }
            }
        }
    }
    
    //clone the scop functions and replace the corresponding calls
    int seq=1;
    for(auto repeatedCall: repeatedCalls) {
        
        //Clone the scop function
        llvm::ValueToValueMapTy VMap;
        llvm::Function *newFn = llvm::CloneFunction(repeatedCall->calledF, VMap,
                                                    /*ModuleLevelChanges=*/false);
        newFn->setName(repeatedCall->calledF->getName().str() + std::to_string(seq++));
        repeatedCall->calledF->getParent()->getFunctionList().push_back(newFn);
        
        //replace the call to the call to the new function
        std::vector < Value * >newFn_params;
        for (Use & U: repeatedCall->callInst->operands()) {
            // Skip the Function declaration.
            if (!isa < llvm::Function > (U)) {
                newFn_params.push_back(U.get());
            }
        }
        auto newCallInst = CallInst::Create(newFn, newFn_params, "", repeatedCall->callInst);
        repeatedCall->callInst->eraseFromParent();
       
        //run scop analysis for the new function
        int status = runScopDetection(newFn);
    }
    
    #ifdef HTROP_SCOP_DEBUG
    std::cout << "\nINFO : Updated the application.";
    std::cout.flush();
    #endif


#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO: No. of Scops detected : " << scopList.size();
    std::cout.flush();
#endif

    // NOTE: here we have not SCOPs, we need a flag to prevent rest of out tool flow.
    assert(scopList.size() > 0 && "Error: We have no SCOPs.");
    
}

// Run pass to detect order and dependencies between calls to scops.
//   This is the dynamic part. 
int HTROPClient::analyseScopDependency() {
    // We assume all Scops belong to the same parent. Let's check this, before
    // things get out of control. 

    bool tmpFirst = true;
    Function *lastParent;

    for (std::pair < std::string, ScopDS * >scop:scopList) {
        std::string scopFunctionName = scop.first;
        ScopDS *scopDS = scop.second;
        Function *fParent = scopDS->scopFunctionParent;

        if (tmpFirst) {
            lastParent = fParent;
            tmpFirst = false;
        }

        assert(lastParent == fParent && "Error: We have different parent functions, from where SCOP functions are called.");
    }

    std::cout << "Info: Parent function that calls the SCOP functions is \"" << lastParent->getName().str() << "\"\n";

    // Functions to analyse.
    std::list < llvm::Function * >analysisCandidates;

    analysisCandidates.push_back(lastParent);

    // Run the detection/analysis pass.
    llvm::legacy::FunctionPassManager ScopDependencyPM(programMod);

    ScopDependencyPM.add(llvm::createDependenceAnalysisPass());

    htrop::ScopDependency * ScopDependencyPass = static_cast < htrop::ScopDependency * >(htrop::createScopDependencyPass());

    ScopDependencyPass->setScopList(&scopList);
    ScopDependencyPass->setScopCallList(&scopCallList);

    ScopDependencyPM.add(ScopDependencyPass);
    for (const auto & function:analysisCandidates) {
        // NOTE:
        // if we iterate over several functions then we would overwrite the results.
        // The idea should be to loop over the given functions (analysisCandidates) 
        // and try to match whatever we would like to detect.
        ScopDependencyPM.run(*function);
    }
    
    flag_chain_interrupt = ScopDependencyPass->getFlagChainInterrupt();
}

int HTROPClient::startAppExecutionSequential() {

    // generate code for main before starting parallel thread to avoid segfaults
    exeEngine->getPointerToFunction(programMod->getFunction("main"));
    HTROPClient::runExeEngineWrapper(this);
}

void *HTROPClient::runExeEngineWrapper(void *object) {
    HTROPClient *clientObject = reinterpret_cast < HTROPClient * >(object);

    clientObject->runExeEngine();
}

void HTROPClient::runExeEngine() {
#ifdef HTROP_DEBUG
    std::cout << "\nINFO: " << "Starting module main\n";
    std::cout.flush();
#endif

    // insert module name in front of argv
    inputArgv->insert(inputArgv->begin(), IRFilename);
    // run program main
    Function *mainF = programMod->getFunction("main");

    if (!mainF) {
        Err.print("Function : main >> ", errs());
        return;
    }

    exeEngine->runFunctionAsMain(mainF, *inputArgv, nullptr);
}

//END LLVM

//BEGIN handle requests

// Code Generation
// With this routine we create a fake GPU code gen request.
int HTROPClient::sendCodeGenReq() {

#if MEASURE
    std::chrono::steady_clock::time_point time_start = std::chrono::steady_clock::now();
#endif

#ifdef HTROP_DEBUG
    std::cout << "\nGenerating code gen request for the GPU ...";
    std::cout.flush();
#endif

    HTROP_PB::Message_RCRS codeGenMsgToServer;

    codeGenMsgToServer.set_scopfunctionparentname(scopFunctionParent->getName().str());

    // Debug flag to enable hand tuning, if automated detection is not sufficient.
    //   Call stub for automated code tuner.
    int debug_hand_tune_pos = 0;

    for (auto scop:scopList) {
        //NOTE do we check against a static threshold  here?

        HTROP_PB::Message_RCRS::ScopInfo * scopInfo = codeGenMsgToServer.add_scoplist();
        scopInfo->set_scopfunctionname(scop.second->scopFunction->getName().str());
        // Use automatic detection of independent loops to parallelize.
        if(maxCogeGenLoopDepth.size() == 0) {
            scopInfo->set_max_codegen_loop_depth(scop.second->maxParalleizationDepth);
        } 
        // Use user provided flag to hand tune.
        else if(maxCogeGenLoopDepth.size() == 1) {
            scopInfo->set_max_codegen_loop_depth(maxCogeGenLoopDepth[0]);
        } else {
            // Reuse last to prevent overflow.
            if((maxCogeGenLoopDepth.size()-1) == debug_hand_tune_pos) {
                scopInfo->set_max_codegen_loop_depth(maxCogeGenLoopDepth[debug_hand_tune_pos]);
            } else {
                scopInfo->set_max_codegen_loop_depth(maxCogeGenLoopDepth[debug_hand_tune_pos++]);
            }
        }
    }

    std::string msgBuffer = codeGenMsgToServer.SerializeAsString();

#ifdef HTROP_DEBUG
    std::cout << "\n -- forward request to HTROP server...";
    std::cout.flush();
#endif

    Message *codeGenHTROPServerMessage = new Message();

    codeGenHTROPServerMessage->send(htrop_server_sockfd, REQ_CODE_GEN, msgBuffer.c_str(), msgBuffer.size(), 0);
    delete codeGenHTROPServerMessage;

#if MEASURE
    std::cout << "\nMEASURE-TIME: Code gen req msg : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - time_start).count());
#endif

    return 1;
}

//Recieve the compiled binary from the HTROP Server
int HTROPClient::handleCompiledBinary(const char *recvMessageBuffer, int messageSize) {

#if MEASURE
    std::chrono::steady_clock::time_point time_start = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
#endif

    //PHASE I: Get the binary from the server

#ifdef HTROP_DEBUG
    std::cout << "\nBinary ready. Recieved handle compiled binary request ...";
    std::cout.flush();
#endif

    //Get the message
    HTROP_PB::Message_RSRC codeGenMsgFromServer;
    if (!codeGenMsgFromServer.ParseFromArray(recvMessageBuffer, messageSize)) {
        std::cerr << ": Failed to parse message" << std::endl;
        return 0;
    }

    //we need to allocate memory for the library
    char *function_binary_buffer = (char *)calloc(codeGenMsgFromServer.binarysize(), sizeof(char));

    Message *binaryMessage = new Message(function_binary_buffer);

    binaryMessage->recv(htrop_server_sockfd, 0);
    assert(binaryMessage->getType() == BINARY_STREAM);
    delete binaryMessage;

#ifdef HTROP_DEBUG
    std::cout << "\n -- recieved binary...";
    std::cout.flush();
#endif

#if MEASURE
    std::cout << "\nMEASURE-TIME: -> Accelerated Code Xfer : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
    startTime = std::chrono::steady_clock::now();
#endif

    //PHASE II: Process the Binary
    handleOclBinary(programMod, function_binary_buffer, &codeGenMsgFromServer);
    free(function_binary_buffer);

#if MEASURE
    std::cout << "\nMEASURE-TIME: -> Function wrapper creation : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
    startTime = std::chrono::steady_clock::now();
#endif

#if MEASURE
    std::cout << "\nMEASURE-TIME: Integrate Acceleated Code : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - time_start).count()) << "\n";
    std::cout.flush();
#endif

    processed = true;

    return 1;
}

bool HTROPClient::finished() {
    if (processed == true)
        return true;
    return false;
}

llvm::Value * HTROPClient::resolveScopValue(ScopFnArg * scopArg, std::vector < ScopFnArg * >*scopFunctonArgs, llvm::BasicBlock ** start_block, llvm::BasicBlock * label_lpad, llvm::Function * function,
                                            llvm::Module * &programMod) {

    Function *func_replaceWithValue = programMod->getFunction("replaceWithValue");
    Function *func_evaluateExpression = programMod->getFunction("evaluateExpression");

    assert(func_replaceWithValue != nullptr && func_evaluateExpression != nullptr);

    //Get the scop string to evaluate
    AllocaInst *scop_formulaStr = createLlvmString(scopArg->dimension_maxStr[0], start_block, label_lpad, function, programMod);

    //At this point we have the string

    //go through all the scalar values and call the resolve function
    int arg_pos = 0;

    for (ScopFnArg * scopScalarArg:*scopFunctonArgs) {

        llvm::Argument * argValue = getArg(function, arg_pos);
        if (!scopScalarArg->isPointer && isIntegerType(argValue)) {

            std::string argName = scopScalarArg->value->getName().str();
            AllocaInst *argStr = createLlvmString(argName, start_block, label_lpad, function, programMod);

            //call func_replaceWithValue 
            std::vector < Value * >func_replaceWithValue_params;
            func_replaceWithValue_params.push_back(scop_formulaStr);
            func_replaceWithValue_params.push_back(argStr);
            func_replaceWithValue_params.push_back(castTo64(argValue, *start_block));

            BasicBlock *label_invoke_cont2 = BasicBlock::Create(*context, "invoke.cont", function, label_lpad);

            InvokeInst *invoke_func_replaceWithValue = InvokeInst::Create(func_replaceWithValue, label_invoke_cont2, label_lpad, func_replaceWithValue_params, "", *start_block);

            *start_block = label_invoke_cont2;

        }
        arg_pos++;
    }

    //Finally call the evaluateExpression function
    std::vector < Value * >func_evaluateExpression_params;
    func_evaluateExpression_params.push_back(scop_formulaStr);

    BasicBlock *label_invoke_cont3 = BasicBlock::Create(*context, "invoke.cont", function, label_lpad);
    InvokeInst *invoke_evaluateExpression = InvokeInst::Create(func_evaluateExpression, label_invoke_cont3, label_lpad, func_evaluateExpression_params, "dimSize", *start_block);

    *start_block = label_invoke_cont3;
    return invoke_evaluateExpression;
}

ScopCallDS *HTROPClient::getFirstScopWithId(std::string scopId) {
    for (auto scop:scopCallList) {
        if (scop->scopID == scopId) {
            return scop;
        }
    }
    return NULL;
}

ConstantInt *HTROPClient::resolveBufferType(DataTransferType dtType) {
    switch (dtType) {
    case UNKNOWN:
        return ConstantInt::get(*context, APInt(32, StringRef("0"), 10));
    case IN:
        return ConstantInt::get(*context, APInt(32, StringRef("1"), 10));
    case OUT:
        return ConstantInt::get(*context, APInt(32, StringRef("2"), 10));
    case IN_OUT:
        return ConstantInt::get(*context, APInt(32, StringRef("3"), 10));
    case TMP:
        return ConstantInt::get(*context, APInt(32, StringRef("4"), 10));
    }
}

ConstantInt *HTROPClient::resolveDeviceType(std::string deviceType) {
    if (deviceType == "LEG")
        return ConstantInt::get(*context, APInt(32, StringRef("0"), 10));
    if (deviceType == "MCPU")
        return ConstantInt::get(*context, APInt(32, StringRef("1"), 10));
    if (deviceType == "GPU")
        return ConstantInt::get(*context, APInt(32, StringRef("2"), 10));
    if (deviceType == "MIC")
        return ConstantInt::get(*context, APInt(32, StringRef("3"), 10));

    return ConstantInt::get(*context, APInt(32, StringRef("0"), 10));   //Defaults to legacy
}

ConstantInt *HTROPClient::resolveSizeOf(Value * arg) {
    Type *argType = arg->getType();

    if (arg->getType()->getNumContainedTypes() > 0) {
        argType = arg->getType()->getContainedType(0);
    }

    if (argType->isFloatTy()) {
        return ConstantInt::get(*context, APInt(32, StringRef("4"), 10));
    }
    else if (argType->isIntegerTy()) {
        int intSize = argType->getIntegerBitWidth();

        switch (intSize) {
        case 1:
        case 8:
            return ConstantInt::get(*context, APInt(32, StringRef("1"), 10));
        case 32:
            return ConstantInt::get(*context, APInt(32, StringRef("4"), 10));
        case 64:
        default:
            return ConstantInt::get(*context, APInt(32, StringRef("8"), 10));
        }
    }
    return ConstantInt::get(*context, APInt(32, StringRef("8"), 10));   //double
}

const HTROP_PB::Message_RSRC::ScopFunctionOCLInfo * HTROPClient::getServerInfo(HTROP_PB::Message_RSRC * codeGenMsgFromServer, std::string scopName) {
    for (int i = 0; i < codeGenMsgFromServer->scopfunctions_size(); i++) {
        if (scopName == codeGenMsgFromServer->scopfunctions(i).scopfunctionname()) {
            return &codeGenMsgFromServer->scopfunctions(i);
        }
    }
    return NULL;
}

Function *HTROPClient::resolveKernelArgFunction(ScopFnArg * scopArg) {
    if (scopArg->isPointer) {
        return programMod->getFunction("setKernelArg_cl_mem");
    }
    else {
        Type *argType = scopArg->value->getType();

        if (argType->isIntegerTy()) {
            int intSize = argType->getIntegerBitWidth();

            switch (intSize) {
            case 1:
                return programMod->getFunction("setKernelArg_char");
            case 32:
                return programMod->getFunction("setKernelArg_int");
            case 64:
            default:
                return programMod->getFunction("setKernelArg_long");
            }
        }
        else if (argType->isFloatTy()) {
            return programMod->getFunction("setKernelArg_float");
        }
        else {
            return programMod->getFunction("setKernelArg_double");
        }
    }
}

void HTROPClient::createLocalAndGlobalWorkGroups(const HTROP_PB::Message_RSRC::ScopFunctionOCLInfo * scopServerInfo, BasicBlock * insertCallIntoBlock, int blockSizeDim0, int blockSizeDim1,
                                                 Value ** ptr_arraydecay, Value ** ptr_arraydecay_local) {

    ConstantInt *const_int64_0 = ConstantInt::get(*context, APInt(DATA_WIDTH, StringRef("0"), 10));
    ConstantInt *const_int64_1 = ConstantInt::get(*context, APInt(DATA_WIDTH, StringRef("1"), 10));
    ConstantInt *const_int64_block_size_0 = ConstantInt::get(*context, APInt(DATA_WIDTH, blockSizeDim0, 10));
    ConstantInt *const_int64_block_size_1 = ConstantInt::get(*context, APInt(DATA_WIDTH, blockSizeDim1, 10));
    PointerType *PointerTy_64_0 = PointerType::get(IntegerType::get(*context, DATA_WIDTH), 0);

    assert(scopServerInfo->workgroup_arg_index_size() > 0);
    ArrayType *workgroupSize = ArrayType::get(IntegerType::get(*context, DATA_WIDTH), scopServerInfo->workgroup_arg_index_size());
    AllocaInst *ptr_global_work_size = new AllocaInst(workgroupSize, "global_work_size", insertCallIntoBlock);

    //Local Group size
    AllocaInst *ptr_local_work_size;

    if (blockSizeDim0)
        ptr_local_work_size = new AllocaInst(workgroupSize, "local_work_size", insertCallIntoBlock);

    Instruction *arrayPtr;
    Instruction *arrayPtrLocal;

    for (int parmsProcessed = 0; parmsProcessed < scopServerInfo->workgroup_arg_index_size(); parmsProcessed++) {
        if (parmsProcessed == 0) {
            std::vector < Value * >ptr_arrayinit_begin_indices;
            ptr_arrayinit_begin_indices.push_back(const_int64_0);
            ptr_arrayinit_begin_indices.push_back(const_int64_0);

            Instruction *ptr_arrayinit_begin =
                GetElementPtrInst::Create(cast < PointerType > (ptr_global_work_size->getType()->getScalarType())->getElementType(), ptr_global_work_size, ptr_arrayinit_begin_indices,
                                          "arrayinit.begin", insertCallIntoBlock);
            arrayPtr = ptr_arrayinit_begin;

            //local
            Instruction *ptr_arrayinit_begin_local;

            if (blockSizeDim0) {
                ptr_arrayinit_begin_local =
                    GetElementPtrInst::Create(cast < PointerType > (ptr_local_work_size->getType()->getScalarType())->getElementType(), ptr_local_work_size, ptr_arrayinit_begin_indices,
                                              "arrayinit.begin.local", insertCallIntoBlock);
                arrayPtrLocal = ptr_arrayinit_begin_local;
                StoreInst *storetmp = new StoreInst(const_int64_block_size_0, arrayPtrLocal, false, insertCallIntoBlock);
            }
        }
        else {
            arrayPtr = GetElementPtrInst::Create(cast < PointerType > (arrayPtr->getType()->getScalarType())->getElementType(), arrayPtr, const_int64_1, "arrayinit.element", insertCallIntoBlock);
            if (blockSizeDim0) {
                arrayPtrLocal =
                    GetElementPtrInst::Create(cast < PointerType > (arrayPtrLocal->getType()->getScalarType())->getElementType(), arrayPtrLocal, const_int64_1, "arrayinit.element.local",
                                              insertCallIntoBlock);
                StoreInst *storetmp = new StoreInst(const_int64_block_size_1, arrayPtrLocal, false, insertCallIntoBlock);
            }
        }

        Value *sizeParam;

        if (parmsProcessed < scopServerInfo->workgroup_arg_index_size()) {
            Value *int64_conv = getArg(insertCallIntoBlock->getParent(), scopServerInfo->workgroup_arg_index(parmsProcessed));

            sizeParam = castTo64(int64_conv, insertCallIntoBlock);
        }
        else {
            sizeParam = const_int64_1;
        }
        StoreInst *store_ptr_arrayinit_begin = new StoreInst(sizeParam, arrayPtr, false, insertCallIntoBlock);
    }

    std::vector < Value * >ptr_arraydecay_indices;
    ptr_arraydecay_indices.push_back(const_int64_0);
    ptr_arraydecay_indices.push_back(const_int64_0);
    *ptr_arraydecay =
        GetElementPtrInst::Create(cast < PointerType > (ptr_global_work_size->getType()->getScalarType())->getElementType(), ptr_global_work_size, ptr_arraydecay_indices, "arraydecay",
                                  insertCallIntoBlock);

    if (blockSizeDim0)
        *ptr_arraydecay_local =
            GetElementPtrInst::Create(cast < PointerType > (ptr_local_work_size->getType()->getScalarType())->getElementType(), ptr_local_work_size, ptr_arraydecay_indices, "arraydecaylocal",
                                      insertCallIntoBlock);
    else
        *ptr_arraydecay_local = ConstantPointerNull::get(PointerTy_64_0);

}

void HTROPClient::setupCatchForInvoke(BasicBlock * label_lpad, BasicBlock * retBlock) {

    //Set personality
    llvm::Function * personality = programMod->getFunction("__gxx_personality_v0");
    label_lpad->getParent()->setPersonalityFn(personality);

    llvm::IRBuilder <> builder(label_lpad);
    //Setup landing pad

    typedef llvm::ArrayRef < llvm::Type * >TypeArray;

    llvm::Type * caughtResultFieldTypes[] = {
        builder.getInt8PtrTy(), builder.getInt32Ty()
    };

    // Create our landingpad result type
    static llvm::StructType * ourCaughtResultType = llvm::StructType::get(getGlobalContext(), TypeArray(caughtResultFieldTypes));

    llvm::LandingPadInst * caughtResult = builder.CreateLandingPad(ourCaughtResultType, 50, "landingPad");
    caughtResult->setCleanup(true);
    builder.CreateBr(retBlock);

}

StructType *getDataTransferType(Module * mod) {

    StructType *StructTy_class_std__vector = mod->getTypeByName("class.std::vector");

    if (!StructTy_class_std__vector) {
        StructTy_class_std__vector = StructType::create(mod->getContext(), "class.std::vector");
    }
    std::vector < Type * >StructTy_class_std__vector_fields;
    StructType *StructTy_struct_std___Vector_base = mod->getTypeByName("struct.std::_Vector_base");

    if (!StructTy_struct_std___Vector_base) {
        StructTy_struct_std___Vector_base = StructType::create(mod->getContext(), "struct.std::_Vector_base");
    }
    std::vector < Type * >StructTy_struct_std___Vector_base_fields;
    StructType *StructTy_struct_std___Vector_base_DataTransferType__std__allocator_DataTransferType______Vector_impl =
        mod->getTypeByName("struct.std::_Vector_base<DataTransferType, std::allocator<DataTransferType> >::_Vector_impl");
    if (!StructTy_struct_std___Vector_base_DataTransferType__std__allocator_DataTransferType______Vector_impl) {
        StructTy_struct_std___Vector_base_DataTransferType__std__allocator_DataTransferType______Vector_impl =
            StructType::create(mod->getContext(), "struct.std::_Vector_base<DataTransferType, std::allocator<DataTransferType> >::_Vector_impl");
    }
    std::vector < Type * >StructTy_struct_std___Vector_base_DataTransferType__std__allocator_DataTransferType______Vector_impl_fields;
    PointerType *PointerTy_4 = PointerType::get(IntegerType::get(mod->getContext(), 32), 0);

    StructTy_struct_std___Vector_base_DataTransferType__std__allocator_DataTransferType______Vector_impl_fields.push_back(PointerTy_4);
    StructTy_struct_std___Vector_base_DataTransferType__std__allocator_DataTransferType______Vector_impl_fields.push_back(PointerTy_4);
    StructTy_struct_std___Vector_base_DataTransferType__std__allocator_DataTransferType______Vector_impl_fields.push_back(PointerTy_4);
    if (StructTy_struct_std___Vector_base_DataTransferType__std__allocator_DataTransferType______Vector_impl->isOpaque()) {
        StructTy_struct_std___Vector_base_DataTransferType__std__allocator_DataTransferType______Vector_impl->
            setBody(StructTy_struct_std___Vector_base_DataTransferType__std__allocator_DataTransferType______Vector_impl_fields, /*isPacked= */ false);
    }

    StructTy_struct_std___Vector_base_fields.push_back(StructTy_struct_std___Vector_base_DataTransferType__std__allocator_DataTransferType______Vector_impl);
    if (StructTy_struct_std___Vector_base->isOpaque()) {
        StructTy_struct_std___Vector_base->setBody(StructTy_struct_std___Vector_base_fields, /*isPacked= */ false);
    }

    StructTy_class_std__vector_fields.push_back(StructTy_struct_std___Vector_base);
    if (StructTy_class_std__vector->isOpaque()) {
        StructTy_class_std__vector->setBody(StructTy_class_std__vector_fields, /*isPacked= */ false);
    }

    return StructTy_class_std__vector;
}

void HTROPClient::handleOclBinary(Module * &programMod, char *function_binary_buffer, HTROP_PB::Message_RSRC * codeGenMsgFromServer) {

    //PAHSE II A: Save the kernel
    std::ofstream exportedClFile;
    std::string oclKernelFilePath = scopFunctionParent->getName().str() + "_client.cl";
    exportedClFile.open(oclKernelFilePath);
    exportedClFile.write(function_binary_buffer, codeGenMsgFromServer->binarysize());
    exportedClFile.close();

#ifdef HTROP_DEBUG
    std::cout << "\n -- wrote kernel to file... " << oclKernelFilePath;
    std::cout.flush();
#endif

    Function *func_addToSum = programMod->getFunction("addToSum");
    Function *func_initChance = programMod->getFunction("initChance");
    Function *func_bonusIfOnDevice = programMod->getFunction("bonusIfOnDevice");
    Function *func_calculateBonusAndDecide = programMod->getFunction("calculateBonusAndDecide");

    //      Function* func_ifGlobalSwitch = programMod->getFunction("ifGlobalSwitch");
    Function *func_transferToDevice = programMod->getFunction("transferToDevice");
    Function *func_oclFinish = programMod->getFunction("oclFinish");
    Function *func_executeOCLKernel = programMod->getFunction("executeOCLKernel");
    Function *func_isLegacy = programMod->getFunction("isLegacy");
    Function *func_getCurrentAccelerator = programMod->getFunction("getCurrentAccelerator");
    Function *func_cleanupBuffers = programMod->getFunction("cleanupBuffers");
    Function *func_cleanUpBuffers_Device = programMod->getFunction("cleanUpBuffers_Device");
    Function *func_staticDecision = programMod->getFunction("staticDecision");

    StructType *struct_cl_mem = programMod->getTypeByName("struct._cl_mem");
    PointerType *voidPtrType = PointerType::get(IntegerType::get(*context, 8), 0);

    assert(func_addToSum != nullptr);
    assert(func_initChance != nullptr);
    assert(func_bonusIfOnDevice != nullptr);
    assert(func_calculateBonusAndDecide != nullptr);
    //      assert(func_ifGlobalSwitch!=nullptr);
    assert(func_transferToDevice != nullptr);
    assert(func_oclFinish != nullptr);
    assert(func_executeOCLKernel != nullptr);
    assert(func_isLegacy != nullptr);
    assert(func_getCurrentAccelerator != nullptr);
    assert(func_cleanupBuffers != nullptr);
    assert(func_cleanUpBuffers_Device != nullptr);
    assert(func_staticDecision != nullptr);

    assert(struct_cl_mem != nullptr);
    PointerType *PointerTy_struct_cl_mem = PointerType::get(struct_cl_mem, 0);

#if MEASURE
    Function *func_startTimeStamp = programMod->getFunction("startTimeStamp");
    Function *func_createOCLBuffersTimeStamp = programMod->getFunction("createOCLBuffersTimeStamp");
    Function *func_enqueWriteBuffersTimeStamp = programMod->getFunction("enqueWriteBuffersTimeStamp");
    Function *func_kernelExecutionTimeStamp = programMod->getFunction("kernelExecutionTimeStamp");
    Function *func_enqueReadBuffersTimeStamp = programMod->getFunction("enqueReadBuffersTimeStamp");
    Function *func_releaseBuffersTimeStamp = programMod->getFunction("releaseBuffersTimeStamp");
    Function *func_runtimeDecisionTimeStamp = programMod->getFunction("runtimeDecisionTimeStamp");

    assert(func_startTimeStamp != nullptr);
    assert(func_createOCLBuffersTimeStamp != nullptr);
    assert(func_enqueWriteBuffersTimeStamp != nullptr);
    assert(func_kernelExecutionTimeStamp != nullptr);
    assert(func_enqueReadBuffersTimeStamp != nullptr);
    assert(func_releaseBuffersTimeStamp != nullptr);
    assert(func_runtimeDecisionTimeStamp != nullptr);
#endif

    //Step I : Create the initialization function
    addOCLInitializationFunction(programMod, codeGenMsgFromServer, oclKernelFilePath);
    int scop_id = 0;

    for (auto scop:scopList) {

        Function *runtimeDecisionFn;
        BasicBlock *start_block;
        BasicBlock *label_lpad;
        BasicBlock *retBlock;

        std::vector < ScopFnArg * >*scopFunctonArgs = &scop.second->scopFunctonArgs;

        //BEGIN Create runtimeDecision_func_XYZ
        if (target == "NONE") {
            runtimeDecisionFn = createFunctionFrom(scop.second->scopFunction, "runtimeDecisionFn", programMod, false);
            start_block = BasicBlock::Create(getGlobalContext(), "start_block", runtimeDecisionFn);
            label_lpad = BasicBlock::Create(getGlobalContext(), "lpad", runtimeDecisionFn);
            CallInst::Create(func_initChance, "", start_block);

            //Get the size of each argument to transfer
            int scopFnParamId = 0;

        for (ScopFnArg * scopArg:*scopFunctonArgs) {
                scopFnParamId++;
                if (!scopArg->isPointer)
                    continue;

                Value *scopValue = resolveScopValue(scopArg, scopFunctonArgs, &start_block, label_lpad, runtimeDecisionFn, programMod);

                //call addToSum
                std::vector < Value * >func_addToSum_params;
                func_addToSum_params.push_back(scopValue);
                CallInst::Create(func_addToSum, func_addToSum_params, "", start_block);

                std::vector < Value * >func_bonusIfOnDevice_params;

                CastInst *castArg = new BitCastInst(getArg(runtimeDecisionFn, scopFnParamId - 1), voidPtrType, "", start_block);

                func_bonusIfOnDevice_params.push_back(castArg);

                CallInst::Create(func_bonusIfOnDevice, func_bonusIfOnDevice_params, "", start_block);
            }

            // Give additional bonus, if required buffers are already on device.
            CallInst::Create(func_calculateBonusAndDecide, "", start_block);

            //add return type and branch
            retBlock = BasicBlock::Create(getGlobalContext(), "ret", runtimeDecisionFn);
            llvm::IRBuilder <> builder(start_block);
            builder.CreateBr(retBlock);
            setupCatchForInvoke(label_lpad, retBlock);
            builder.SetInsertPoint(retBlock);
            builder.CreateRetVoid();
        }
        //runtimeDecisionFn->dump();  //TESTING
        //END Create runtimeDecision_func_XYZ

        //BEGIN create function for transferAndInvoke_func_XYZ

        ScopCallDS *scopInstance = getFirstScopWithId(scop.second->scopFunction->getName().str());

        assert(scopInstance != NULL);

        //TESTING 
        //              int scopFnArgId=0;
        //              for(ScopCallFnArg* scopInstanceArgs : scopInstance->scopCallFunctonArgs){
        //                      if(scop.second->scopFunctonArgs.at(scopFnArgId)->isPointer){
        //                              scopInstanceArgs->typeOptimized = IN;
        //                      }
        //                      scopFnArgId++;
        //              }

        Function *transferAndInvokeFn = createFunctionFrom(scop.second->scopFunction, "transferAndInvokeFn", programMod, true);

        start_block = BasicBlock::Create(getGlobalContext(), "start_block", transferAndInvokeFn);
        label_lpad = BasicBlock::Create(getGlobalContext(), "lpad", transferAndInvokeFn);

        BasicBlock *intBlock = start_block;
        BasicBlock *dataTransferBB = BasicBlock::Create(getGlobalContext(), "dataTransfer", transferAndInvokeFn);
        BasicBlock *setKernelArgsBlock = BasicBlock::Create(getGlobalContext(), "setKernelArgsBlock", transferAndInvokeFn);

        auto deviceType = getArg(transferAndInvokeFn, transferAndInvokeFn->arg_size() - 1);

        //call finalize
        std::vector < Value * >func_oclFinish_params;
        func_oclFinish_params.push_back(deviceType);
        CallInst::Create(func_oclFinish, func_oclFinish_params, "", setKernelArgsBlock);

#if MEASURE
        CallInst::Create(func_enqueWriteBuffersTimeStamp, "", setKernelArgsBlock);
#endif

        //Get the scopInfo from the server message
        auto scopServerInfo = getServerInfo(codeGenMsgFromServer, scop.second->scopFunction->getName().str());

        //create the kernelname string
        AllocaInst *scop_KernelNameStr = createLlvmString(scopServerInfo->scopoclkernelname(), &start_block, label_lpad, transferAndInvokeFn, programMod);

        //Get the size of each argument to transfer
        int scopFnParamId = 0;

        for (ScopFnArg * scopArg:*scopFunctonArgs) {
            Value *dataPtr = getArg(transferAndInvokeFn, scopFnParamId);

            if (scopArg->isPointer) {
                AllocaInst *ptr_clBuffer = new AllocaInst(PointerTy_struct_cl_mem, "clBuffer", start_block);
                Value *scopValue = resolveScopValue(scopArg, scopFunctonArgs, &start_block, label_lpad, transferAndInvokeFn, programMod);

                std::vector < Value * >func_transferToDevice_params;
                func_transferToDevice_params.push_back(deviceType);
                CastInst *castArg = new BitCastInst(getArg(transferAndInvokeFn, scopFnParamId), voidPtrType, "", start_block);

                func_transferToDevice_params.push_back(castArg);
                func_transferToDevice_params.push_back(resolveBufferType(scopInstance->scopCallFunctonArgs.at(scopFnParamId)->typeOptimized));  //resolve from scopCallList
                func_transferToDevice_params.push_back(resolveSizeOf(scopArg->value));  //resolve from scop
                func_transferToDevice_params.push_back(scopValue);
                auto transferCall = CallInst::Create(func_transferToDevice, func_transferToDevice_params, "", dataTransferBB);
                StoreInst *storeTransfer = new StoreInst(transferCall, ptr_clBuffer, false, dataTransferBB);

                dataPtr = ptr_clBuffer;
            }

            Function *func_setKernelArg = resolveKernelArgFunction(scopArg);

            std::vector < Value * >func_setKernelArg_params;
            func_setKernelArg_params.push_back(deviceType);
            func_setKernelArg_params.push_back(scop_KernelNameStr);
            ConstantInt *const_int_kernel_pos = ConstantInt::get(*context, APInt(32, StringRef(std::to_string(scopFnParamId)), 10));

            func_setKernelArg_params.push_back(const_int_kernel_pos);
            func_setKernelArg_params.push_back(dataPtr);

            CallInst::Create(func_setKernelArg, func_setKernelArg_params, "", setKernelArgsBlock);

            scopFnParamId++;
        }

        //Create the array
        BasicBlock *insertCallIntoBlock = setKernelArgsBlock;
        Value *ptr_arraydecay_global;
        Value *ptr_arraydecay_local;

        createLocalAndGlobalWorkGroups(scopServerInfo, insertCallIntoBlock, blockSizeDim0, blockSizeDim1, &ptr_arraydecay_global, &ptr_arraydecay_local);

#if MEASURE
        CallInst::Create(func_startTimeStamp, "", insertCallIntoBlock);
#endif

        std::vector < Value * >func_executeOCLKernel_params;
        func_executeOCLKernel_params.push_back(deviceType);
        func_executeOCLKernel_params.push_back(scop_KernelNameStr);
        func_executeOCLKernel_params.push_back(ConstantInt::get(*context, APInt(32, StringRef(std::to_string(scopServerInfo->workgroup_arg_index_size())), 10)));
        func_executeOCLKernel_params.push_back(ptr_arraydecay_global);
        func_executeOCLKernel_params.push_back(ptr_arraydecay_local);
        CallInst::Create(func_executeOCLKernel, func_executeOCLKernel_params, "", insertCallIntoBlock);

#if MEASURE
        CallInst::Create(func_kernelExecutionTimeStamp, "", insertCallIntoBlock);
#endif

        //Add the branch and return instructions
        retBlock = BasicBlock::Create(getGlobalContext(), "ret", transferAndInvokeFn);
        llvm::IRBuilder <> builder(start_block);
        builder.CreateBr(dataTransferBB);
        builder.SetInsertPoint(dataTransferBB);
        builder.CreateBr(setKernelArgsBlock);
        builder.SetInsertPoint(setKernelArgsBlock);
        builder.CreateBr(retBlock);
        setupCatchForInvoke(label_lpad, retBlock);
        builder.SetInsertPoint(retBlock);
        builder.CreateRetVoid();

        //transferAndInvokeFn->dump(); //TESTING

        //END create function for transferAndInvoke_func_XYZ

        //BEGIN Create the wrapper

        Function *wrapperFn = createFunctionFrom(scop.second->scopFunction, "wrapperfn", programMod, false);

        start_block = BasicBlock::Create(getGlobalContext(), "start_block", wrapperFn);
        auto legacyBlock = BasicBlock::Create(getGlobalContext(), "legacy_block", wrapperFn);
        auto acceleratedBlock = BasicBlock::Create(getGlobalContext(), "accelerated_block", wrapperFn);

        retBlock = BasicBlock::Create(getGlobalContext(), "ret_block", wrapperFn);

        std::vector < Value * >Fn_params;
        for (auto argIter = wrapperFn->arg_begin(); argIter != wrapperFn->arg_end(); argIter++) {
            Fn_params.push_back((llvm::Argument *) argIter);
        }

#if MEASURE
        CallInst::Create(func_startTimeStamp, "", start_block);
#endif

        if (target == "NONE") {
            CallInst::Create(runtimeDecisionFn, Fn_params, "", start_block);
        }
        else {
            std::vector < Value * >func_staticDecision_params;
            func_staticDecision_params.push_back(resolveDeviceType(target));
            CallInst::Create(func_staticDecision, func_staticDecision_params, "", start_block);
        }

#if MEASURE
        CallInst::Create(func_runtimeDecisionTimeStamp, "", start_block);
#endif

        CallInst::Create(func_cleanupBuffers, "", start_block);

#if MEASURE
        CallInst::Create(func_enqueReadBuffersTimeStamp, "", start_block);
#endif

        CallInst *call_isLegacy = CallInst::Create(func_isLegacy, "call", start_block);

        BranchInst::Create(legacyBlock, acceleratedBlock, call_isLegacy, start_block);

#if MEASURE
        CallInst::Create(func_startTimeStamp, "", legacyBlock);
#endif

        CallInst::Create(scop.second->scopFunction, Fn_params, "", legacyBlock);

#if MEASURE
        CallInst::Create(func_kernelExecutionTimeStamp, "", legacyBlock);
#endif

        CallInst *call_getCurrentAccelerator = CallInst::Create(func_getCurrentAccelerator, "call", acceleratedBlock);

        Fn_params.push_back(call_getCurrentAccelerator);
        CallInst::Create(transferAndInvokeFn, Fn_params, "", acceleratedBlock);

        builder.SetInsertPoint(legacyBlock);
        builder.CreateBr(retBlock);
        builder.SetInsertPoint(acceleratedBlock);
        builder.CreateBr(retBlock);

        builder.SetInsertPoint(retBlock);
        builder.CreateRetVoid();

        //END Create the wrapper

        //BEGIN Replace all uses of the old function call
        
        
        CallInst *call_getCurrentAcceleratorFoChain;
        bool call_getCurrentAcceleratorSet = false;
        std::vector < Value * >cleanupBuffers_params;

        for (ScopCallDS * scopCall:scopCallList) {
            //Check and replace
            if (scopCall->scopID == scop.second->scopFunction->getName().str()) {
                auto scopCallInst = scopCall->callInst;
                
                std::vector < Value * >wrapperFn_params;

                for (auto argIter:scopCall->scopCallFunctonArgs) {
                    wrapperFn_params.push_back(argIter->value);
                }
                CallInst::Create(wrapperFn, wrapperFn_params, "", scopCallInst);
                
                if(flag_chain_interrupt){
                    
                    if(!call_getCurrentAcceleratorSet){
                        CallInst *call_getCurrentAcceleratorFoChain = CallInst::Create(func_getCurrentAccelerator, "call", scopCallInst);
                        cleanupBuffers_params.push_back(call_getCurrentAcceleratorFoChain);
                        call_getCurrentAcceleratorSet = true;
                    }
                     CallInst::Create(func_cleanUpBuffers_Device, cleanupBuffers_params, "", scopCallInst);
                }
                scopCallInst->eraseFromParent();
            }
        }

        //END Replace all uses of the old function call
        scop_id++;
    }

    //BEGIN Call to final Device cleanup
    
    if(!flag_chain_interrupt){

        //Add cleanup in the scop Parent before the first free
        Instruction *lastWrapperCall = NULL;

        for (inst_iterator instInScop = inst_begin(scopFunctionParent), e = inst_end(scopFunctionParent); instInScop != e; ++instInScop) {
            if (isa < CallInst > (&*instInScop)) {
                CallInst *callScopInst = dyn_cast < CallInst > (&*instInScop);

                if (callScopInst->getCalledFunction()->getName().startswith("wrapper")) {
                    lastWrapperCall = callScopInst;
                }
            }
        }

        assert(lastWrapperCall != NULL);

        Instruction *insertBefore = lastWrapperCall->getNextNode();

        std::vector < Value * >cleanupBuffers_params;
        CallInst *call_getCurrentAccelerator = CallInst::Create(func_getCurrentAccelerator, "call", insertBefore);

        cleanupBuffers_params.push_back(call_getCurrentAccelerator);

    #if MEASURE
        CallInst::Create(func_startTimeStamp, "", insertBefore);
    #endif

        CallInst::Create(func_cleanUpBuffers_Device, cleanupBuffers_params, "", insertBefore);

    #if MEASURE
        CallInst::Create(func_enqueReadBuffersTimeStamp, "", insertBefore);
    #endif
    }

    //END Call to final Device cleanup

}

void HTROPClient::addOCLInitializationFunction(Module * &programMod, HTROP_PB::Message_RSRC * codeGenMsgFromServer, std::string oclKernelFilePath) {

    llvm::Function * fnInitOpenCLDevices = programMod->getFunction("initOpenCLDevices");
    assert(fnInitOpenCLDevices != nullptr);

    //create the funciton
    llvm::FunctionType * functType = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), false);

    llvm::Function * initFunction = llvm::Function::Create(functType, fnInitOpenCLDevices->getLinkage(), "intOCL", programMod);
    assert(initFunction != nullptr);

    //Add a dummy basic block
    llvm::BasicBlock * ret = llvm::BasicBlock::Create(llvm::getGlobalContext(), "return", initFunction);
    llvm::IRBuilder <> builder(ret);
    builder.CreateRetVoid();

    llvm::BasicBlock * retBlock = (llvm::BasicBlock *) initFunction->begin();
    llvm::BasicBlock * initBlock = llvm::BasicBlock::Create(llvm::getGlobalContext(), "init", initFunction, retBlock);

    //Step I : Initialize all the OpenCLDevices

    //entry block
    builder.SetInsertPoint(initBlock);

    std::vector < Value * >paramsInitOpenCLDevices;
    paramsInitOpenCLDevices.push_back(builder.CreateGlobalStringPtr(oclKernelFilePath));
    builder.CreateCall(fnInitOpenCLDevices, paramsInitOpenCLDevices);

    //Step II : Add all the kernels to the deveices
    llvm::Function * fnAddClKernel = programMod->getFunction("addClKernel");
    assert(fnAddClKernel != nullptr);

    for (int i = 0; i < codeGenMsgFromServer->scopfunctions_size(); i++) {
        std::vector < Value * >paramsAddClKernel;
        paramsAddClKernel.push_back(builder.CreateGlobalStringPtr(codeGenMsgFromServer->scopfunctions(i).scopoclkernelname()));
        builder.CreateCall(fnAddClKernel, paramsAddClKernel);
    }

    llvm::BranchInst::Create(retBlock, initBlock);

    //Add call to the main function
    llvm::Function * main = programMod->getFunction("main");
    assert(main != nullptr);

    llvm::Instruction * insrtPt = nullptr;
    llvm::BasicBlock * BB = &*(main->begin());
    insrtPt = BB->getTerminator();

    assert(insrtPt != nullptr);

    builder.SetInsertPoint(insrtPt);
    builder.CreateCall(initFunction);
}

//Handle the LLVM IR request for a function
int HTROPClient::handleLLVMIRReq(const char *recvMessageBuffer, int messageSize) {

#if MEASURE
    std::chrono::steady_clock::time_point time_start = std::chrono::steady_clock::now();
#endif

    //Get the message
    HTROP_PB::LLVM_IR_Req llvmReqFromServer;
    if (!llvmReqFromServer.ParseFromArray(recvMessageBuffer, messageSize)) {
        std::cerr << ": Failed to parse message" << std::endl;
        return 0;
    }

#ifdef HTROP_DEBUG
    std::cout << "\nRecieved request for LLVM IR";
    std::cout.flush();
#endif

    std::vector < llvm::Function * >functionsToExport;
    for (auto scop:scopList) {
        functionsToExport.push_back(scop.second->scopFunction);
    }

    std::vector < llvm::GlobalVariable * >globalsToExport;
    std::string llvmIR = exportFunctionIntoBitcode(programMod, functionsToExport, globalsToExport);

    //Send LLVM IR
    Message *llvmIRResponseMessage = new Message();

    if (llvmIRResponseMessage->send(htrop_server_sockfd, RSP_LLVM_IR, llvmIR.c_str(), llvmIR.size(), 0) < 0) {
        std::cout << "\nError: Send failed";
        return 0;
    }
    delete llvmIRResponseMessage;

#ifdef HTROP_DEBUG
    std::cout << "\n -- Sent LLVM IR...";
    std::cout.flush();
#endif

#if MEASURE
    std::cout << "\nMEASURE-TIME: LLVM IR Xfer : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - time_start).count());
#endif

    return 1;
}
