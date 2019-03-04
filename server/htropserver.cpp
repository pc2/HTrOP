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

#include "htropserver.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <limits.h>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>

#include "llvm/IR/Function.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APFloat.h"

#include "../common/messageHelper.h"
#include "../common/messageTypes.h"
#include "../common/hds.pb.h"
#include "../consts.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/IR/Module.h"

#if LLVM_VERSION == 3 && LLVM_MINOR_VERSION < 5
#include "llvm/Analysis/Verifier.h"
#else
#include "llvm/IR/Verifier.h"
#endif
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/MemoryBuffer.h"

#include "openCLCbackend.h"
#include "../common/llvmHelper.h"
#include "../common/sharedStructCompileRuntime.h"

#include "llvm/IR/LegacyPassManager.h"

HTROPServer::HTROPServer(bool isCacheEnabled, int portNumber) {
    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    portno = portNumber;
    sockfd = -1;
    HTROPServer::isCacheEnabled = isCacheEnabled;
}

HTROPServer::~HTROPServer() {
    //Close the sockets
    shutdown(sockfd, 2);
    //Delete all global objects allocated by libprotobuf.
    google::protobuf::ShutdownProtobufLibrary();
}

int
 HTROPServer::start() {
    if (portno == 0) {
        std::cerr << "Unspecified Port";
        return -1;
    }

    if (createSocket() < 0)
        return -1;
    handleIncommingConnections();
}

int HTROPServer::createSocket() {
    //Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Could not create socket";
        return -1;
    }

#ifdef HTROP_DEBUG
    std::cout << "\nSocket created";
    std::cout.flush();
#endif
    bzero((char *)&serv_addr, sizeof(serv_addr));

    //Prepare the sockaddr_in structure
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    //Bind
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Bind failed. Error";
        return -2;
    }
#ifdef HTROP_DEBUG
    std::cout << "\nBind complete";
    std::cout.flush();
#endif
    return 0;
}

//BEGIN HANDLE NEW CLIENT CONNECTIONS

//Wait for incomming connections
int HTROPServer::handleIncommingConnections() {

    int client_sock, new_sock;
    struct sockaddr_in cli_addr;

    //Listen
    listen(sockfd, MAX_NO_OF_QUED_CONNECTIONS);

    //Accept and incoming connection
    std::cout << "\n" << sockfd << ": Waiting for incoming connections ...";
    std::cout.flush();
    int c = sizeof(struct sockaddr_in);

    while (client_sock = accept(sockfd, (struct sockaddr *)&cli_addr, (socklen_t *) & c)) {

#ifdef HTROP_DEBUG
        std::cout << "\n" << client_sock << ": Connection accepted";
        std::cout << "\n" << client_sock << ": Handler assigned";
        std::cout.flush();
#endif

        threads.push_back(std::thread(&HTROPServer::connection_handler, this, client_sock));
    }

    //Now join the thread , so that we dont terminate before the thread
 for (auto & th:threads) {
        th.join();
    }

    if (client_sock < 0) {
        std::cerr << "Accept failed";
        std::cout.flush();
        return -2;
    }

    return 0;
}

//END HANDLE NEW CLIENT CONNECTIONS

//This will handle the connection for each client
void *HTROPServer::connection_handler(void *object, int client_sockfd) {

    HTROPServer *serverObject = reinterpret_cast < HTROPServer * >(object);

    //check if there is a connection to the HTROP Client
    if (client_sockfd < 0) {
        std::cerr << "ERROR in socket";
        return 0;
    }

    //Create a new message buffer for each client
    char *messageBuffer = (char *)calloc(256, sizeof(char));
    Message *handleReqMessage = new Message(messageBuffer);
    int active = 1;             //used to break out of the loop

    //Wait for incomming codegen requests from the HTROPClient
    while (active) {

        //blocking call
        handleReqMessage->recv(client_sockfd, 0);

        switch (handleReqMessage->getType()) {

        case REQ_CODE_GEN:
            active = serverObject->handleCodeGenReq(client_sockfd, handleReqMessage->getMessageBuffer(), handleReqMessage->getSize());
            break;

        case -1:               // Disconnection / error

            if (handleReqMessage->getSize() == 0) {
                std::cout << "\n" << client_sockfd << ": Client Disconnected ...";
                std::cout.flush();
            }
            else if (handleReqMessage->getSize() == -1) {
                std::cout << "\n" << client_sockfd << ": recv failed ...";
                std::cout.flush();
            }
            active = 0;
            break;

        default:
            std::cout << "\n" << client_sockfd << ": default case >" << handleReqMessage->getType();
            std::cout.flush();
            break;
        }
    }

    //close the socket
    free(messageBuffer);
    shutdown(client_sockfd, 2);
    return 0;
}

//Check if the code is already present
HTROP_PB::Message_RSRC * HTROPServer::isCodeCached(std::string key) {
    if (cacheList.count(key)) {
        return cacheList[key];
    }
    return NULL;
}

//returns the index of the newly created hotspot
void HTROPServer::addToCache(std::string key, HTROP_PB::Message_RSRC * codeGenMsgFromServer) {
    cacheList.insert(std::pair < std::string, HTROP_PB::Message_RSRC * >(key, codeGenMsgFromServer));
}

void HTROPServer::codeGen_OCL(llvm::Module * &Mod, HTROP_PB::Message_RCRS * codeGenMsgFromClient, HTROP_PB::Message_RSRC * codeGenMsgFromServer) {
    std::string oclKernelFilePath = codeGenMsgFromClient->scopfunctionparentname() + "_server.cl";
    codeGenMsgFromServer->set_oclkernelfilename(oclKernelFilePath);
    auto *openCLCBackend = new OpenCLCBackend(Mod, codeGenMsgFromClient, codeGenMsgFromServer, oclKernelFilePath);
}

// Code Generation
int HTROPServer::handleCodeGenReq(int sockfd, char *recvMessageBuffer, int messageSize) {

#ifdef HTROP_DEBUG
    std::cout << "\n" << sockfd << ": Recieved code gen request ...";
    std::cout.flush();
#endif

#if MEASURE
    std::chrono::steady_clock::time_point time_start = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
#endif

    HTROP_PB::Message_RSRC * codegenMessageFromServer = NULL;

    //Extract the function name and platform  
    HTROP_PB::Message_RCRS codeGenMsgFromClient;
    if (!codeGenMsgFromClient.ParseFromArray(recvMessageBuffer, messageSize)) {
        std::cerr << ": Failed to parse message" << std::endl;
        return 0;
    }

    std::string scopFunctionParentName = "ocl_" + codeGenMsgFromClient.scopfunctionparentname();

#if MEASURE
    std::cout << "\nMEASURE-TIME: Preprocessing : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
    std::chrono::steady_clock::time_point acc_startTime = std::chrono::steady_clock::now();
    startTime = std::chrono::steady_clock::now();
#endif

    //PHASE I: CHECK IF THE CODE IS CACHED
    codegenMessageFromServer = isCodeCached(scopFunctionParentName);

    if (!(isCacheEnabled && codegenMessageFromServer != NULL)) {
#ifdef HTROP_DEBUG
        std::cout << "\n" << sockfd << ": Request for LLVM IR ...";
        std::cout.flush();
#endif

        //PHASE II: Check if the LLVM code for the function is cached
        std::unique_ptr < llvm::Module > module_Ptr;
        llvm::Module * Mod = NULL;

        HTROP_PB::LLVM_IR_Req llvmReqFromServer;
        std::string msgRequest = llvmReqFromServer.SerializeAsString();

        //Request for the LLVM IR for the function
        Message *llvmReqMessage = new Message;

        if (llvmReqMessage->send(sockfd, REQ_LLVM_IR, msgRequest.c_str(), msgRequest.size(), 0) < 0) {
            std::cerr << sockfd << ": Error: Failed to request Client for the LLVM IR";
            return 0;
        }
        delete llvmReqMessage;

        //Get the LLVM IR for the function
#ifdef HTROP_DEBUG
        std::cout << "\n" << sockfd << ": -- waiting for LLVM IR...";
        std::cout.flush();
#endif

        Message *llvmModuleMsg = new Message();
        char *function_ir_buffer = (char *)calloc(IR_BUFFER_SIZE, sizeof(char));

        llvmModuleMsg->recv(sockfd, function_ir_buffer, 0);

        assert(llvmModuleMsg->getType() == RSP_LLVM_IR);

#ifdef HTROP_DEBUG
        std::cout << "\n" << sockfd << ": -- LLVM IR recieved.... ";
        std::cout.flush();
#endif

        llvm::SMDiagnostic Err;
        auto moduleMemBufferPtr = llvm::MemoryBuffer::getMemBuffer(llvm::StringRef(llvmModuleMsg->getMessageBuffer(), llvmModuleMsg->getSize()));
        auto moduleMemBuffer = moduleMemBufferPtr.get();

        module_Ptr = llvm::parseIR(moduleMemBuffer->getMemBufferRef(), Err, llvm::getGlobalContext());
        Mod = module_Ptr.get();
        if (!Mod) {
            Err.print("htropserver", llvm::errs());
            return 0;
        }

#ifdef HTROP_DEBUG
        std::cout << "\n" << sockfd << ": -- Verifying LLVM IR Module ....  ";
        std::cout.flush();
#endif
        llvm::verifyModule(*Mod);

        delete llvmModuleMsg;

        free(function_ir_buffer);

#if MEASURE
        std::cout << "\nMEASURE-TIME: -> Get LLVM IR from Client : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
        startTime = std::chrono::steady_clock::now();
#endif

        //PHASE III: Generate code for the specific platform

#ifdef HTROP_DEBUG
        std::cout << "\n" << sockfd << "Generating code for target  ...";
        std::cout.flush();
#endif

        codegenMessageFromServer = new HTROP_PB::Message_RSRC();
        codeGen_OCL(Mod, &codeGenMsgFromClient, codegenMessageFromServer);

        //Update the cache
        addToCache(scopFunctionParentName, codegenMessageFromServer);

#if MEASURE
        std::cout << "\nMEASURE-TIME: -> Code Generation : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
#endif

    }
#ifdef HTROP_DEBUG
    else {
        std::cout << "\n Cached Code found";
        std::cout.flush();
    }
#endif

#if MEASURE
    std::cout << "\nMEASURE-TIME: Generate Accelereated Code : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - acc_startTime).count());
    startTime = std::chrono::steady_clock::now();
#endif

    //PHASE IV: Transfer the generated code to the client

    char *binaryCodeBuffer;

    uint32_t binaryCodeSize = fileToBuffer(codegenMessageFromServer->oclkernelfilename(), binaryCodeBuffer);

    codegenMessageFromServer->set_binarysize(binaryCodeSize);

#ifdef HTROP_DEBUG
    std::cout << "\n" << sockfd << ": Sending code to Client :: " << codegenMessageFromServer->oclkernelfilename();
    std::cout.flush();
#endif

    std::string codeGenMsg = codegenMessageFromServer->SerializeAsString();

#ifdef HTROP_DEBUG
    std::cout << "\n" << sockfd << ": -- code ready message with function name and code size";
    std::cout.flush();
#endif

    Message *codeGenReadyMessage = new Message();

    if (codeGenReadyMessage->send(sockfd, RSP_CODE_GEN_COMPLETE, codeGenMsg.c_str(), codeGenMsg.size(), 0) < 0) {
        std::cerr << sockfd << ": Error: Failed to send code gen ready";
        return 0;
    }
    delete codeGenReadyMessage;

#ifdef HTROP_DEBUG
    std::cout << "\n" << sockfd << ": -- code binary, " << binaryCodeSize << " bytes";
    std::cout.flush();
#endif

#if MEASURE
    std::chrono::steady_clock::time_point binTime = std::chrono::steady_clock::now();
#endif

    Message *binaryMessage = new Message();

    if (binaryMessage->send(sockfd, BINARY_STREAM, binaryCodeBuffer, binaryCodeSize, 0) < 0) {
        std::cerr << sockfd << ": Error: Failed to send the binary code to the HTROP Client";
        return 0;
    }
    delete binaryMessage;

    free(binaryCodeBuffer);

#if MEASURE
    std::cout << "\nMEASURE-TIME: Send binary to Client : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - binTime).count());
    std::cout << "\nMEASURE-TIME: Send to Client : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
#endif

#ifdef HTROP_DEBUG
    std::cout << "\n" << sockfd << ": -- ... done";
    std::cout.flush();
#endif

#if MEASURE
    std::cout << "\nMEASURE-TIME: Total Code Gen Time : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - time_start).count());
    startTime = std::chrono::steady_clock::now();
#endif
    return 1;
}
