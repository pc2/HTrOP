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

#ifndef HTROPSERVER_H
#define HTROPSERVER_H

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
#include "../common/hds.pb.h"

#include<netdb.h>
#include<vector>
#include<thread>

#include "../common/sharedStructCompileRuntime.h"

#define MAX_NO_OF_QUED_CONNECTIONS 15

class HTROPServer {

    int sockfd, portno;
    struct sockaddr_in serv_addr;
     std::vector < std::thread > threads;
    int handleIncommingConnections();
    int createSocket();
    int handleCodeGenReq(int sockfd, char *recvMessageBuffer, int messageSize);

    static void *connection_handler(void *object, int client_sockfd);

    //Cache

    bool isCacheEnabled = true;
    //Check if the Code for the function and resource is available
     HTROP_PB::Message_RSRC * isCodeCached(std::string key);
    //Functin Name is the key
     std::map < std::string, HTROP_PB::Message_RSRC * >cacheList;
    void addToCache(std::string key, HTROP_PB::Message_RSRC * codeGenMsgFromServer);

    char recvMessageBuffer[2000];

    //Generate OCL code
    void codeGen_OCL(llvm::Module * &Mod, HTROP_PB::Message_RCRS * codeGenMsgFromClient, HTROP_PB::Message_RSRC * codeGenMsgFromServer);

 public:
    int start();
     HTROPServer(bool isCacheEnabled, int portNumber);
    ~HTROPServer();
};

#endif                          // HTROPSERVER_H
