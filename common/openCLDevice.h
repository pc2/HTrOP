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

#ifndef _OPENCLDEVICE_H
#define _OPENCLDEVICE_H

#include <map>
#include <algorithm>
#include <string>

#include "CL/cl.h"
#include "../common/dataTransferType.h"

typedef std::function < void (void) > KernelCompletionBlockType;
typedef std::function < void (void) > KernelExectionBlockType;

typedef struct {
    cl_kernel kernel;
    KernelExectionBlockType executionBlock;
    KernelCompletionBlockType completionBlock;
} KernelInfo;

class OpenCLDevice {
 public:
    struct OclBuffer {
        cl_mem oclBuffer;
        DataTransferType transferType;
        long size;
        int dataTypeSize;
    };

    OpenCLDevice();
    OpenCLDevice(cl_device_type dType, std::string kernelFile, DeviceType oclDeviceType);
    ~OpenCLDevice();
    void importKernel(int libType);

    cl_int addKernel(std::string kernelName);
    void executeKernel(std::string kernelName, KernelExectionBlockType executionBlock, KernelCompletionBlockType completionBlock);
    cl_int removeKernel(std::string kernelName);

    cl_kernel getKernel(std::string kernelName);
    cl_mem getBuffer(void *hostDataPointer);
    cl_mem createAndPopulateBuffer(void *hostDataPointer, DataTransferType transferType, long size, int dataTypeSize);
    void cleanUpBuffers();
    bool isAvailable();
    bool isCompiled();
    bool inCompilationPhase();
    void setInCompilationPhase(bool value);
    DeviceType getDeviceType();
    cl_command_queue getCommandQueue();
    cl_context getContext();
    cl_program getProgram();

 private:
    cl_command_queue commandQueue = NULL;
    cl_context context = NULL;
    cl_program program = NULL;
    std::string platformName;
    std::string deviceName;
    bool isCompiledValue = false;
    bool inCompilationPhaseValue = false;
    std::string kernelFileName;
    DeviceType deviceType;
    std::map < std::string, cl_kernel > kernelList;
    std::map < void *, OclBuffer * >bufferList;
    cl_device_type clType;

};

#endif
