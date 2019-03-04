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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <cctype>
#include <iterator>

// Include OpenCLDevice helper.
#include "../common/openCLDevice.h"
#include "../common/math_parser.h"

#if MEASURE
#include <chrono>
std::chrono::steady_clock::time_point g_time_start;
#endif

void GPU_Linker(float *dummyData, unsigned int numberOfData, OpenCLDevice & openCLInfo) {
    cl_int ret;
    cl_mem mem_buffer = clCreateBuffer(openCLInfo.getContext(), CL_MEM_READ_WRITE, sizeof(cl_float) * numberOfData, NULL, &ret);

    clFinish(openCLInfo.getCommandQueue());
    clEnqueueWriteBuffer(openCLInfo.getCommandQueue(), mem_buffer, CL_FALSE, 0, numberOfData * sizeof(cl_float), dummyData, 0, NULL, NULL);
    clSetKernelArg(openCLInfo.getKernel("someDummy"), 0, sizeof(cl_mem), &mem_buffer);
    size_t global_work_size[3] = { numberOfData, 1, 1 };        //set number of item per dimension
    clEnqueueNDRangeKernel(openCLInfo.getCommandQueue(), openCLInfo.getKernel("someDummy"), 1, NULL, global_work_size, NULL, 0, NULL, NULL);
    clEnqueueReadBuffer(openCLInfo.getCommandQueue(), mem_buffer, CL_FALSE, 0, numberOfData * sizeof(cl_float), dummyData, 0, NULL, NULL);
    clReleaseMemObject(mem_buffer);
}

extern "C" void replaceWithValue(std::string * in_str, std::string var_str, long var_val) {

#ifdef HTROP_DEBUG
    printf("\n replace: %s : %s : %ld", in_str->c_str(), var_str.c_str(), var_val);
#endif
    int r_start = in_str->find(var_str);

    while (r_start > -1) {
        std::string rpl_str = "(" + std::to_string(var_val) + ")";
        in_str->replace(r_start, var_str.length(), rpl_str);
        r_start = in_str->find(var_str);
    }
}

extern "C" long evaluateExpression(std::string * in_str) {
    // 2. Call math engine.

    // Create AST.
    ScopExp *scopAST = createASTfromScopString(*in_str);

#ifdef HTROP_DEBUG
    printf("\n evaluating: %s \n ", in_str->c_str());
#endif

    long tree_val = scopAST->eval();

#ifdef HTROP_DEBUG
    std::cout << "done ... scopAST.value = \"" << tree_val << "\" \n";
#endif

    scopAST->cln();
    delete scopAST;

    return tree_val;
}

#if MEASURE
extern "C" void startTimeStamp() {
    g_time_start = std::chrono::steady_clock::now();
}

extern "C" void createCompileOCLTimeStamp() {
    std::cout << "\nMEASURE-TIME: CompileOCLFile : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - g_time_start).count());
    std::cout.flush();
    g_time_start = std::chrono::steady_clock::now();
}

extern "C" void createAddOCLKernelTimeStamp() {
    std::cout << "\nMEASURE-TIME: AddOCLKernel : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - g_time_start).count());
    std::cout.flush();
    g_time_start = std::chrono::steady_clock::now();
}

extern "C" void createOCLBuffersTimeStamp() {
    std::cout << "\nMEASURE-TIME: CreateOCLBuffers : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - g_time_start).count());
    std::cout.flush();
    g_time_start = std::chrono::steady_clock::now();
}

extern "C" void enqueWriteBuffersTimeStamp() {
    std::cout << "\nMEASURE-TIME: EnqueWriteBuffers : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - g_time_start).count());
    std::cout.flush();
    g_time_start = std::chrono::steady_clock::now();
}

extern "C" void kernelExecutionTimeStamp() {
    std::cout << "\nMEASURE-TIME: KernelExecution : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - g_time_start).count());
    std::cout.flush();
    g_time_start = std::chrono::steady_clock::now();
}

extern "C" void enqueReadBuffersTimeStamp() {
    std::cout << "\nMEASURE-TIME: EnqueReadBuffers : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - g_time_start).count());
    std::cout.flush();
    g_time_start = std::chrono::steady_clock::now();
}

extern "C" void releaseBuffersTimeStamp() {
    std::cout << "\nMEASURE-TIME: ReleaseBuffers : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - g_time_start).count()) << "\n";
    std::cout.flush();
}

extern "C" void runtimeDecisionTimeStamp() {
    std::cout << "\nMEASURE-TIME: RuntimeDecision : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - g_time_start).count());
    std::cout.flush();
    g_time_start = std::chrono::steady_clock::now();
}
#endif
