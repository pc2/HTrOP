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

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/LinkAllPasses.h"

#include <signal.h>
#include <iostream>

using namespace std;

llvm::cl::opt < bool > EnableCaching("enable-cache", llvm::cl::desc("Enable server caching"), llvm::cl::init(false));
llvm::cl::opt < int >HTROPHostPort("htrop-port", llvm::cl::desc("HTROP Server port, defaults to '55066'"), llvm::cl::init(55066));

void handleSignal(int) {
    exit(0);
}

int main(int argc, char *argv[]) {
    llvm::PassRegistry & Registry = *llvm::PassRegistry::getPassRegistry();
    initializeCore(Registry);
    initializeScalarOpts(Registry);
    initializeObjCARCOpts(Registry);
    initializeVectorization(Registry);
    initializeIPO(Registry);
    initializeAnalysis(Registry);
    initializeTransformUtils(Registry);
    initializeInstCombine(Registry);
    initializeInstrumentation(Registry);
    initializeTarget(Registry);

    llvm::cl::ParseCommandLineOptions(argc, argv);

    struct sigaction signal_action;

    memset(&signal_action, 0, sizeof(signal_action));
    signal_action.sa_handler = &handleSignal;
    sigfillset(&signal_action.sa_mask);
    sigaction(SIGINT, &signal_action, NULL);

    std::cout << "\nSERVER INFO: Cache Enabled = " << EnableCaching;
    std::cout << "\nSERVER INFO: Listening on port : " << HTROPHostPort;

    HTROPServer *server = new HTROPServer(EnableCaching, HTROPHostPort);

    server->start();

    return 0;
}
