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

#include <stdlib.h>
#include <iostream>
#include "openCLDevice.h"

OpenCLDevice::OpenCLDevice() {
}

OpenCLDevice::OpenCLDevice(cl_device_type dType, std::string kernelFile, DeviceType oclDeviceType) {
    clType = dType;
    kernelFileName = kernelFile;
    deviceType = oclDeviceType;
}

DeviceType OpenCLDevice::getDeviceType() {
    return deviceType;
}

bool OpenCLDevice::isCompiled() {
    return isCompiledValue;
}

bool OpenCLDevice::inCompilationPhase() {
    return inCompilationPhaseValue;
}

void OpenCLDevice::setInCompilationPhase(bool value) {
    inCompilationPhaseValue = value;
}

cl_command_queue OpenCLDevice::getCommandQueue() {
    return commandQueue;
}

cl_context OpenCLDevice::getContext() {
    return context;
}

cl_program OpenCLDevice::getProgram() {
    return program;
}

void OpenCLDevice::importKernel(int libType) {

    //Read the file from the client
    FILE *fp;

    fp = fopen(kernelFileName.c_str(), "r");

    if (fp == NULL) {
        std::cout << "Error: Error reading the kernel file " << kernelFileName << std::endl;
        exit(1);
    }

    size_t fileSize;
    char *fileContent;

    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    fileContent = (char *)malloc(fileSize);
    if (fileContent == NULL) {
        std::cout << "Error: There was an error allocating " << fileSize << " bytes of memory" << std::endl;
        exit(1);
    }
    else if (fread(fileContent, sizeof(char), fileSize, fp) != fileSize) {
        std::cout << "Error: Error reading the kernel file " << kernelFileName << std::endl;
        free(fileContent);
        exit(1);
    }
    fclose(fp);

    cl_int ret;
    cl_uint numberOfDevices;
    cl_uint numberOfPlatforms;

    cl_platform_id oclPlatformId = NULL;
    cl_device_id oclDeviceId = NULL;

    /* Get Platform/Device Information */
    ret = clGetPlatformIDs(1, &oclPlatformId, &numberOfPlatforms);

    cl_uint platformCount;
    cl_platform_id *platforms;

    // get platform count
    clGetPlatformIDs(5, NULL, &platformCount);

    // get all platforms
    platforms = (cl_platform_id *) malloc(sizeof(cl_platform_id) * platformCount);
    clGetPlatformIDs(platformCount, platforms, NULL);

    //Get the OLC device handle     
    for (int i = 0; i < platformCount; i++) {
        ret = clGetDeviceIDs(platforms[i], clType, 1, &oclDeviceId, &numberOfDevices);
        if (ret == CL_SUCCESS) {
            oclPlatformId = platforms[i];
            break;
        }
    }

    if (ret != CL_SUCCESS) {
        std::cout << "ERROR: Unable to " << ret << std::endl;
        exit(0);
    }

    //Create the device context
    context = clCreateContext(NULL, 1, &oclDeviceId, NULL, NULL, &ret);
    if (ret != CL_SUCCESS) {
        std::cout << "Error: Unable to ccreate the OCL context" << ret << std::endl;
    }

    //Create the Command Queue
    commandQueue = clCreateCommandQueue(context, oclDeviceId, 0, &ret);
    if (ret != CL_SUCCESS) {
        std::cout << "Error: Unable to create the OCL command queue" << ret << std::endl;
    }

    //Create the program
    program = clCreateProgramWithSource(context, 1, (const char **)&fileContent, (const size_t *)&fileSize, &ret);

    if (ret != CL_SUCCESS) {
        std::cout << "Error: Unable to create the OCL Program with Source" << ret << std::endl;
    }

    const char *flags = "";

    ret = clBuildProgram(program, 1, &oclDeviceId, flags, NULL, NULL);

    if (ret != CL_SUCCESS) {
        std::cout << "Error: OpenCL" << ret << std::endl;
    }

    if (ret == CL_BUILD_PROGRAM_FAILURE) {
        std::cout << "Error: Unable to Build the kernel" << std::endl;
    }
    isCompiledValue = true;
}

OpenCLDevice::~OpenCLDevice() {
    cl_int ret;

    for (auto item:bufferList) {
        clReleaseMemObject(item.second->oclBuffer);
    }

    clFinish(commandQueue);
    bufferList.clear();

    ret = clFlush(commandQueue);
    ret = clFinish(commandQueue);

    for (auto item:kernelList) {
        ret = clReleaseKernel(item.second);
    }

    kernelList.clear();

    ret = clReleaseProgram(program);
    ret = clReleaseCommandQueue(commandQueue);
    ret = clReleaseContext(context);
}

cl_int OpenCLDevice::addKernel(std::string kernelName) {
    cl_int ret;

    cl_kernel ocl_kernel = clCreateKernel(program, kernelName.c_str(), &ret);

    if (ret != CL_SUCCESS) {

        std::cout << "ERROR: Unable to add kernel " << kernelName << std::endl;

        return ret;
    }

    kernelList.emplace(kernelName.c_str(), ocl_kernel);
    return ret;
}

cl_int OpenCLDevice::removeKernel(std::string kernelName) {
    cl_int ret;

    auto item = kernelList.find(kernelName);

    if (item != kernelList.end()) {
        ret = clReleaseKernel(item->second);
        kernelList.erase(item);
    }

    return ret;
}

cl_mem OpenCLDevice::getBuffer(void *hostDataPointer) {
    cl_int ret;
    auto item = bufferList.find(hostDataPointer);

    if (item != bufferList.end()) {
        return item->second->oclBuffer;
    }
    return NULL;
}

cl_mem OpenCLDevice::createAndPopulateBuffer(void *hostDataPointer, DataTransferType transferType, long size, int dataTypeSize) {
    cl_int ret;
    OclBuffer *buffer = new OclBuffer();

    buffer->transferType = transferType;
    buffer->dataTypeSize = size;
    buffer->size = dataTypeSize;

#ifdef HTROP_DEBUG
    std::cout << "\nBuffer Size = " << buffer->size;
    std::cout << "\nData Type Size = " << buffer->dataTypeSize;
    std::cout.flush();
#endif

    switch (transferType) {
    case IN:
        buffer->oclBuffer = clCreateBuffer(context, CL_MEM_READ_ONLY, buffer->dataTypeSize * buffer->size, NULL, &ret);
        break;
    case OUT:
        buffer->oclBuffer = clCreateBuffer(context, CL_MEM_READ_WRITE, buffer->dataTypeSize * buffer->size, NULL, &ret);
        break;
    case IN_OUT:
        buffer->oclBuffer = clCreateBuffer(context, CL_MEM_READ_WRITE, buffer->dataTypeSize * buffer->size, NULL, &ret);
        break;
    default:
        buffer->oclBuffer = clCreateBuffer(context, CL_MEM_READ_WRITE, buffer->dataTypeSize * buffer->size, NULL, &ret);
    }

    bufferList.emplace(hostDataPointer, buffer);

    if (transferType != OUT)
        ret = clEnqueueWriteBuffer(commandQueue, buffer->oclBuffer, CL_FALSE, 0, buffer->size * buffer->dataTypeSize, hostDataPointer, 0, NULL, NULL);

    return buffer->oclBuffer;
}

void OpenCLDevice::cleanUpBuffers() {
    cl_int ret;

#ifdef HTROP_DEBUG
    for (auto item:bufferList) {
        std::cout << "\nMapping device (" << item.second->oclBuffer << ")" << " to host (" << item.first << ")";
        std::cout.flush();
    }
#endif

    for (auto item:bufferList) {

        std::cout.flush();
        if (item.second->transferType != IN) {
#ifdef HTROP_DEBUG
            std::cout << "\nTransfer data from device (" << item.second->oclBuffer << ")" << " to host (" << item.first << ")";
            std::cout.flush();
#endif
            ret = clEnqueueReadBuffer(commandQueue, item.second->oclBuffer, CL_FALSE, 0, item.second->size * item.second->dataTypeSize, item.first, 0, NULL, NULL);

        }
#ifdef HTROP_DEBUG
        std::cout << "\nclReleaseMemObject (" << item.second->oclBuffer << ")";
        std::cout.flush();
#endif

        clReleaseMemObject(item.second->oclBuffer);
    }

    clFinish(commandQueue);
    bufferList.clear();
}

cl_kernel OpenCLDevice::getKernel(std::string kernelName) {
    cl_int ret;

    auto item = kernelList.find(kernelName);

    if (item != kernelList.end()) {
        return item->second;
    }
    return NULL;
}

void OpenCLDevice::executeKernel(std::string kernelName, KernelExectionBlockType executionBlock, KernelCompletionBlockType completionBlock) {

    if (executionBlock) {
        executionBlock();
    }

    if (completionBlock) {
        completionBlock();
    }
}

//NOTE: This is device specific. Replace with calls to your target machine.
bool OpenCLDevice::isAvailable(){
    return true;
}


