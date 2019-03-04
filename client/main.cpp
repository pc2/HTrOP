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

#include "polly/ScopDetection.h"
#include "polly/LinkAllPasses.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/CommandLine.h"

#include "pass/accscore.h"
#include "htropclient.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <list>
#include <unistd.h>
#include <string>
#include <cstring>
#include <unordered_map>
#include <sys/stat.h>
#include <signal.h>
#include <algorithm>

#if LLVM_VERSION == 3 && LLVM_MINOR_VERSION < 5
#include "llvm/Support/ValueHandle.h"
#include "llvm/Analysis/Verifier.h"
#else
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/Verifier.h"
#endif

#include "gtest/gtest.h"

#if MEASURE
#include <chrono>
#endif

#ifdef EVAL
#include "/usr/ampehre/include/ms_measurement.h"
#include "/usr/ampehre/include/ms_cpu_intel_xeon_sandy.h"
#include "/usr/ampehre/include/ms_fpga_maxeler_max3a.h"
#include "/usr/ampehre/include/ms_gpu_nvidia_tesla_kepler.h"
#include "/usr/ampehre/include/ms_mic_intel_knc.h"
#endif

static std::unordered_map < std::string, unsigned long >scores;
static std::fstream timeMeasureStream;

llvm::cl::opt < int >OffloadScoreThreshold("offload-threshold", llvm::cl::desc("Functions getting a score greater than this are offloaded, defaults to zero"), llvm::cl::init(0));
llvm::cl::opt < int >ProgramRuns("runs",
                                 llvm::cl::desc("How often the acceleration on program and program itself should run, defaults to one. Reruns the whole process on program exit if greater one"),
                                 llvm::cl::init(1));

llvm::cl::opt < std::string > HTROPHostname("htrop-host", llvm::cl::desc("HTROP Server hostname or IP, defaults to 'localhost'"), llvm::cl::init("localhost"));
llvm::cl::opt < int >HTROPHostPort("htrop-port", llvm::cl::desc("HTROP Server port, defaults to '55066'"), llvm::cl::init(55066));

llvm::cl::opt < std::string > OrchestratorHostname("orch-host", llvm::cl::desc("Orchestrator hostname or IP, defaults to 'localhost'"), llvm::cl::init("localhost"));
llvm::cl::opt < std::string > TimeMeasureFile("time-file", llvm::cl::desc("Output file for time measuring, defaults to 'time_measures.txt'"), llvm::cl::init("time_measures.txt"));

llvm::cl::opt < std::string > IRFilename(llvm::cl::Positional, llvm::cl::Required, llvm::cl::desc("<IR file>"));
llvm::cl::list < std::string > InputArgv(llvm::cl::ConsumeAfter, llvm::cl::desc("<program arguments>..."));

llvm::cl::list < int >CGLDepth("codegen-max-nesting", llvm::cl::desc("Max Loop Nesting to consider for codegen. This is a stub for auto-tuning to overwrite the automatic detection."));
llvm::cl::list < int >SCOPLDepth("scop-max-nesting", llvm::cl::desc("Max Loop Nesting to consider for SCOP detection. This is a stub for auto-tuning to overwrite the automatic detection."));

llvm::cl::opt < bool > EnableHostOclPtr("use-host-ptr-ocl-mapping", llvm::cl::desc("Use host OpenCL Mapping"), llvm::cl::init(false));

llvm::cl::opt < std::string > HTROPTarget("target", llvm::cl::desc("HTROP Server target, defaults to 'NONE : LEG,MCPU,GPU,MIC"), llvm::cl::init("NONE"));

llvm::cl::opt < int >BlockSizeDim0("block-size", llvm::cl::desc("Block size of OpenCL kernel and local_workgroup_size[block-size,block-size-1], defaults to 0"), llvm::cl::init(0));
llvm::cl::opt < int >BlockSizeDim1("block-size-1", llvm::cl::desc("Block size of OpenCL kernel and local_workgroup_size[block-size,block-size-1], defaults to 0"), llvm::cl::init(0));

llvm::cl::opt < std::string > OutputFile("o", llvm::cl::desc("Compile only and save to file"), llvm::cl::init("none"));

static void printUsage(std::string programName);
static void runExeEngine(llvm::Module * Mod, llvm::ExecutionEngine * EE);
static void declareCallAcc(llvm::Module * ProgramMod);
static std::string exportFunctionsIntoBitcode(llvm::LLVMContext * Context, llvm::Module * ProgramMod, const std::list < llvm::Function * >&functionList);

static void handleSignal(int) {
    timeMeasureStream << std::endl;
    timeMeasureStream.close();
    exit(0);
}



int main(int argc, char *argv[]) {

    ::testing::InitGoogleTest(&argc, argv);

#ifdef EVAL
    MS_VERSION version = {.major = MS_MAJOR_VERSION,.minor = MS_MINOR_VERSION,.revision = MS_REVISION_VERSION };
    MS_SYSTEM *ms = ms_init(&version, CPU_GOVERNOR_ONDEMAND, 2000000, 2500000, GPU_FREQUENCY_CUR, IPMI_SET_TIMEOUT, SKIP_PERIODIC, VARIANT_FULL);       //VARIANT_LIGHT

    // Allocate measurement list
    MS_LIST *m1 = ms_alloc_measurement(ms);

    // Set timer for m1. Measurements perform every (10ms/30ms)*10 = 100ms/300ms.
    ms_set_timer(m1, CPU, 0, 30000000, 1);
    ms_set_timer(m1, GPU, 0, 30000000, 1);
    ms_set_timer(m1, FPGA, 0, 100000000, 1);
    ms_set_timer(m1, SYSTEM, 0, 100000000, 1);
    ms_set_timer(m1, MIC, 0, 40000000, 1);
    ms_init_measurement(ms, m1, CPU | GPU | MIC);
    ms_start_measurement(ms);

    std::chrono::steady_clock::time_point total_Time_start = std::chrono::steady_clock::now();

#endif

#if MEASURE
    std::chrono::steady_clock::time_point total_Time_start = std::chrono::steady_clock::now();
#endif

    llvm::cl::ParseCommandLineOptions(argc, argv);
    std::transform(HTROPTarget.begin(), HTROPTarget.end(), HTROPTarget.begin(),::toupper);

    // If this flag is set, the user wants to overwrite the automatic detected values by hand-tuned values.
    //   Check if the values are within meaningful range for OpenCL.
    for (auto valCGLDepth : CGLDepth) {
        if (valCGLDepth <= 0 || valCGLDepth > 3) {
            std::cout << "\n Choose a valid value for  codegen-max-nesting (1-3)";
            exit(0);
        }
    }

    struct stat buf;
    bool fileExists = stat(TimeMeasureFile.c_str(), &buf) != -1;

    timeMeasureStream.open(TimeMeasureFile, std::ios::out | std::ios::app);
    if (!fileExists)
        timeMeasureStream << "Ana\tSrvIni\tOpt\tBndIni\tAlt\t(FuncName\tFuncScore\tExeAcc\tCallAcc)*\n";

    for (int i = 0; i < ProgramRuns; i++) {
        if (ProgramRuns > 1)
            std::cout << "INFO: " << "---------- " << "Beginning run " << i + 1 << " of " << ProgramRuns << " ----------" << std::endl;

        //Create the client
        std::cout << "\n Listening on port : " << HTROPHostPort;
        HTROPClient *htropclient = new HTROPClient(OrchestratorHostname, HTROPHostname, HTROPHostPort, IRFilename, CGLDepth, SCOPLDepth, BlockSizeDim0, BlockSizeDim1, HTROPTarget, &InputArgv);

#if MEASURE
        std::cout << "\nMEASURE-TIME: Units microseconds ";
        std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();;
#endif
        htropclient->initializeLLVMModule();

#if MEASURE
        std::cout << "\nMEASURE-TIME: initializeLLVMModule : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
        std::cout.flush();
        startTime = std::chrono::steady_clock::now();
#endif

#ifdef HTROP_DEBUG
        std::cout << "INFO: " << "Preparing LLVM IR for Polly/ScopDetection: " << std::endl;
        std::cout.flush();
#endif
        // Run polly passes to prepare the IR.
        htropclient->prepareIRForPolly();
#ifdef HTROP_DEBUG
        std::cout << "\nINFO: ... preparation completed.\n";
        std::cout.flush();
#endif

#if MEASURE
        std::cout << "\nMEASURE-TIME: prepare for POLLY : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
        startTime = std::chrono::steady_clock::now();
#endif

#ifdef HTROP_DEBUG
        std::cout << "\nINFO: " << "Looking for SCoPs: " << std::endl;
        std::cout.flush();
#endif
        // Run passes to detect and analyse scops.
        htropclient->analyseScop();

        //Analysis should be correct till here.

#ifdef HTROP_DEBUG
        std::cout << "\nINFO: ... SCOP detection completed.\n";
        std::cout.flush();
#endif

#ifdef HTROP_DEBUG
        std::cout << "\nINFO: " << "Looking for SCoP Calls (wrapper): " << std::endl;
        std::cout.flush();
#endif

        htropclient->analyseScopDependency();

#ifdef HTROP_DEBUG
        std::cout << "\nINFO: ... SCOP calls completed.\n";
        std::cout.flush();
#endif


#if MEASURE
        std::cout << "\nMEASURE-TIME: SCOP Analysis : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
        std::cout.flush();
        startTime = std::chrono::steady_clock::now();
#endif

        //Always do this after optimizations
        htropclient->extendLLVMModule();
        htropclient->initializeExecutionEngine();

#if MEASURE
        startTime = std::chrono::steady_clock::now();
#endif

#ifdef HTROP_DEBUG
        std::cout << "\nINFO: Establish Connections: \n";
        std::cout.flush();
#endif
        htropclient->connectToHTROPServer();
#ifdef HTROP_DEBUG
        std::cout << "\nINFO: ... connection established.\n";
        std::cout.flush();
#endif

#ifdef HTROP_DEBUG
        std::cout << "\nINFO: handle Request: \n";
        std::cout.flush();
#endif
        htropclient->handleRequests();
#ifdef HTROP_DEBUG
        std::cout << "\nINFO: ... request handled.\n";
        std::cout.flush();
#endif

        //Wait for the codegen to complete before we start the application
        while (!htropclient->finished()) {
        }

#if MEASURE
        std::cout << "\nMEASURE-TIME: Code  Gen : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
        std::chrono::steady_clock::time_point totalApp_Time_start = std::chrono::steady_clock::now();
        std::cout.flush();
#endif

#ifdef HTROP_DEBUG
        std::cout << "\nINFO: Start Appl sequential: \n";
        std::cout.flush();
#endif

        if (OutputFile == "none") {
            htropclient->startAppExecutionSequential();
        }
        else {
            htropclient->exportToFile(OutputFile);
        }

#ifdef HTROP_DEBUG
        std::cout << "\nINFO: ... Start Appl completed.\n";
        std::cout.flush();
#endif

#if MEASURE
        std::cout << "\nMEASURE-TIME: App Execution : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - totalApp_Time_start).count());
        std::cout.flush();
#endif

        timeMeasureStream << std::endl;
    }
    timeMeasureStream.close();

#if MEASURE
    std::chrono::steady_clock::time_point total_Time_stop = std::chrono::steady_clock::now();
#endif

#ifdef EVAL

    std::chrono::steady_clock::time_point total_Time_stop = std::chrono::steady_clock::now();

    // Stop all measurement procedures.
    ms_stop_measurement(ms);

    // Join measurement threads and remove thread objects.
    ms_join_measurement(ms);
    ms_fini_measurement(ms);

    // Print the measured data to stdout.    
    double cpu_energy = cpu_energy_total_pkg(m1, 0) + cpu_energy_total_pkg(m1, 1) + cpu_energy_total_dram(m1, 0) + cpu_energy_total_dram(m1, 1);
    double gpu_energy = gpu_energy_total(m1);
    double mic_energy = mic_energy_total_power_usage(m1);

    std::cout.precision(2);

    std::cout << std::fixed << "\nMEASURE-POWER: (WattSecond|microseconds)";
    std::cout << std::fixed << "\nMEASURE-POWER: CPU : " << cpu_energy / 1000.0;
    std::cout << std::fixed << "\nMEASURE-POWER: GPU : " << gpu_energy / 1000.0;
    std::cout << std::fixed << "\nMEASURE-POWER: PHI : " << mic_energy / 1000.0;
    std::cout << std::fixed << "\nMEASURE-POWER: Total execution : " << (std::chrono::duration_cast < std::chrono::microseconds > (total_Time_stop - total_Time_start).count()) << "\n";
    std::cout.flush();

    // Cleanup the environment before exiting the program
    ms_free_measurement(m1);
    ms_fini(ms);

#else
#if MEASURE
    std::cout << "\nMEASURE-TIME: Total execution : " << (std::chrono::duration_cast < std::chrono::microseconds > (total_Time_stop - total_Time_start).count());
    std::cout.flush();
#endif
#endif

    return 0;
}
