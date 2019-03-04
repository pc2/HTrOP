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

#include "../common/openCLDevice.h"
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <boost/concept_check.hpp>
#include <assert.h>
#include<thread>

#if MEASURE
#include <chrono>
#endif


extern "C" void importOpenClLib(OpenCLDevice & device) {

#if MEASURE
    std::chrono::steady_clock::time_point time_start = std::chrono::steady_clock::now();
#endif

    device.importKernel(0);

#if MEASURE
    std::cout << "\nMEASURE-TIME: Compile Kernel : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - time_start).count());
#endif
}

extern "C" void importOpenClLibBin(OpenCLDevice & device) {
#if MEASURE
    std::chrono::steady_clock::time_point time_start = std::chrono::steady_clock::now();
#endif

    device.importKernel(1);

#if MEASURE
    std::cout << "\nMEASURE-TIME: Compile Kernel : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - time_start).count());
#endif
}

extern "C" void addOpenClKernel(OpenCLDevice & device, const char *kernelName) {
#if MEASURE
    std::chrono::steady_clock::time_point time_start = std::chrono::steady_clock::now();
#endif

    cl_int ret = device.addKernel(kernelName);

    if (ret != CL_SUCCESS) {
        std::cout << "\n==============================\nOCL ERROR: Unable to add kernel \"" << kernelName << "\"\n==============================\n";
        std::cout.flush();
    }

#if MEASURE
    std::cout << "\nMEASURE-TIME: Add Kernel : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - time_start).count());
#endif
}

struct RuntimeDecision {
    DeviceType currentAcelerator;
    DeviceType previousAcelerator;
};

RuntimeDecision *runtimeDecision_Handle;

OpenCLDevice *cpuDevice;
OpenCLDevice *gpuDevice;
OpenCLDevice *micDevice;

std::vector < OpenCLDevice * >deviceList;

float chanceLEG;
float chanceCPU;
float chanceGPU;
float chanceMIC;
long sum;
long switchPosition;
long callCount;
bool switchOnFirst;

std::vector < std::string > kernelNames;
std::vector < std::thread > compilerThreads;

extern "C" std::string resolveDataTransferTypeName(DataTransferType transferType) {

    //      #ifdef HTROP_DEBUG
    //      std::cout << "\nHTROP INFO : resolveAcceleratorName";
    //      std::cout.flush();
    //      #endif

    switch (transferType) {
    case IN:
        return "IN";
    case OUT:
        return "OUT";
    case IN_OUT:
        return "IN_OUT";
    case TMP:
        return "TMP";
    }
}

extern "C" std::string resolveAcceleratorName(DeviceType deviceType) {

//      #ifdef HTROP_DEBUG
//      std::cout << "\nHTROP INFO : resolveAcceleratorName";
//      std::cout.flush();
//      #endif

    switch (deviceType) {
    case LEG:
        return "LEG";
    case MCPU:
        return "MCPU";
    case GPU:
        return "GPU";
    case MIC:
        return "MIC";
    }
}

//Helper in C
extern "C" OpenCLDevice * resolveDevice(DeviceType deviceType) {

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : resolveDevice -> " << resolveAcceleratorName(deviceType);
    std::cout.flush();
#endif

    switch (deviceType) {
    case LEG:
        return NULL;
    case MCPU:
        return cpuDevice;
    case GPU:
        return gpuDevice;
    case MIC:
        return micDevice;
    }
}

extern "C" void compileKernel(OpenCLDevice * device) {
#if MEASURE
    std::chrono::steady_clock::time_point init_time_start = std::chrono::steady_clock::now();
#endif

    device->setInCompilationPhase(true);
    device->importKernel(0);

#if MEASURE
    std::cout << "\nMEASURE-TIME: ImportKernelFile" << resolveAcceleratorName(device->getDeviceType()) << " : " <<
        (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - init_time_start).count());
    std::cout.flush();
#endif

    //Add kernels
    for (auto kernekName:kernelNames) {

#if MEASURE
        init_time_start = std::chrono::steady_clock::now();
#endif

        device->addKernel(kernekName);

#if MEASURE
        std::cout << "\nMEASURE-TIME: AddKernel : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - init_time_start).count());
        std::cout.flush();
#endif
    }

    device->setInCompilationPhase(false);

}

extern "C" void compileForDevice(DeviceType deviceType) {

    OpenCLDevice *device = resolveDevice(deviceType);

    //check if other devices are compiled, if not start compilation on a seperate thread
    for (OpenCLDevice * oclDevice:deviceList) {
        if (oclDevice != device) {
            if (!oclDevice->inCompilationPhase() && !oclDevice->isCompiled()) {

#ifdef HTROP_DEBUG
                std::cout << "\nHTROP INFO : importClLib on New Thread : " << resolveAcceleratorName(deviceType);
                std::cout.flush();
#endif

                compilerThreads.push_back(std::thread(&compileKernel, oclDevice));
            }
        }
    }

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : importClLib : " << resolveAcceleratorName(deviceType);
    std::cout.flush();
#endif

    //wait until compilation 
    if (device->inCompilationPhase()) {
        while (device->inCompilationPhase()) {
        }
    }

    if (!device->isCompiled()) {
        //Compile the kernels
        compileKernel(device);
    }
}

extern "C" void staticDecision(DeviceType deviceType) {

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : staticDecision";
    std::cout.flush();
#endif

    runtimeDecision_Handle->previousAcelerator = runtimeDecision_Handle->currentAcelerator;
    runtimeDecision_Handle->currentAcelerator = deviceType;
    switchPosition = 0;
    
    if(deviceType!=LEG)
        compileForDevice(deviceType);

    std::cout << "\nHTROP INFO : Static Resource switch: " << resolveAcceleratorName(runtimeDecision_Handle->previousAcelerator) << " - " << resolveAcceleratorName(runtimeDecision_Handle->
                                                                                                                                                                    currentAcelerator);
    std::cout.flush();
}

extern "C" void initOpenCLDevices(const char *kernelPath) {

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : initOpenCLDevices";
    std::cout.flush();
#endif

#if MEASURE
    std::chrono::steady_clock::time_point init_time_start = std::chrono::steady_clock::now();
#endif

#ifdef ENABLE_MCPU

    cpuDevice = new OpenCLDevice(CL_DEVICE_TYPE_CPU, kernelPath, MCPU);
    deviceList.push_back(cpuDevice);

#if MEASURE
    std::cout << "\nMEASURE-TIME: InitMCPU : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - init_time_start).count());
    std::cout.flush();
    init_time_start = std::chrono::steady_clock::now();
#endif
#endif

#ifdef ENABLE_GPU

    gpuDevice = new OpenCLDevice(CL_DEVICE_TYPE_GPU, kernelPath, GPU);
    deviceList.push_back(gpuDevice);
#if MEASURE
    std::cout << "\nMEASURE-TIME: InitGPU : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - init_time_start).count());
    std::cout.flush();
    init_time_start = std::chrono::steady_clock::now();
#endif
#endif

#ifdef ENABLE_MIC

    micDevice = new OpenCLDevice(CL_DEVICE_TYPE_ACCELERATOR, kernelPath, MIC);
    deviceList.push_back(micDevice);
#if MEASURE
    std::cout << "\nMEASURE-TIME: InitMIC : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - init_time_start).count());
    std::cout.flush();
#endif

#endif

    runtimeDecision_Handle = new RuntimeDecision();
    //init
    runtimeDecision_Handle->currentAcelerator = runtimeDecision_Handle->previousAcelerator = LEG;

    callCount = 0;
    switchPosition = -1;    
}

// Helper in C
extern "C" void addClKernel(const char *kernelName) {

    kernelNames.push_back(kernelName);

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : addClKernel";
    std::cout.flush();
#endif

}

// Helper in C
extern "C" void decideWhichAccelerator() {

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : decideWhichAccelerator";
    std::cout.flush();
#endif

    runtimeDecision_Handle->previousAcelerator = runtimeDecision_Handle->currentAcelerator;
    // Take the biggest.
    if (chanceLEG > chanceCPU && chanceLEG > chanceGPU && chanceLEG > chanceMIC) {
        runtimeDecision_Handle->currentAcelerator = LEG;
    }
    else if (chanceCPU > chanceGPU && chanceCPU > chanceMIC) {
        runtimeDecision_Handle->currentAcelerator = MCPU;
    }
    else if (chanceGPU > chanceMIC) {
        runtimeDecision_Handle->currentAcelerator = GPU;
    }
    else {
        runtimeDecision_Handle->currentAcelerator = MIC;
    }

    if (runtimeDecision_Handle->previousAcelerator != runtimeDecision_Handle->currentAcelerator) {
        switchPosition = callCount;

        compileForDevice(runtimeDecision_Handle->currentAcelerator);

#ifdef HTROP_DEBUG
        std::cout << "\n Switchd acclerator at call no: " << switchPosition;
        std::cout.flush();
#endif

    }

    callCount++;

    std::cout << "\nHTROP INFO : Resource switch: " << resolveAcceleratorName(runtimeDecision_Handle->previousAcelerator) << " - " << resolveAcceleratorName(runtimeDecision_Handle->currentAcelerator);
    std::cout.flush();
}

// Helper in C
extern "C" void bonusLastAccelerator() {
#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : bonusLastAccelerator";
    std::cout.flush();
#endif

    // Evaluate last accelerator.
    //   - general bonus, when last accelerator is reused.
    //   - additional bonus, if last accelerator holds buffers that are required by this function.
    switch (runtimeDecision_Handle->currentAcelerator) {
    case LEG:
        // Give general bonus for last accelerator.
        chanceLEG += 0.5;
        break;
    case MCPU:
        // Give general bonus for last accelerator.
        chanceCPU += 0.5;
        break;
    case GPU:
        // Give general bonus for last accelerator.
        chanceGPU += 0.5;
        break;
    case MIC:
        // Give general bonus for last accelerator.
        chanceMIC += 0.5;
        break;
    }
}

// Helper in C
extern "C" void bonusIfAvailable() {

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : bonusIfAvailable";
    std::cout.flush();
#endif

#ifdef ENABLE_MCPU
    if (!cpuDevice->isAvailable()) {
        chanceCPU -= 3.0;
    }
#endif
#ifdef ENABLE_GPU
    if (!gpuDevice->isAvailable()) {
        chanceGPU -= 3.0;
    }
#endif
#ifdef ENABLE_MIC
    if (!micDevice->isAvailable()) {
        chanceMIC -= 3.0;
    }
#endif
}

// Helper in C
extern "C" void bonusIfOnDevice(void *cpuDataPointer) {

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : bonusIfOnDevice";
    std::cout.flush();
#endif

#ifdef ENABLE_MCPU
    if (cpuDevice->getBuffer(cpuDataPointer) != NULL) {
        chanceCPU += 0.3;
        return;
    }
#endif
#ifdef ENABLE_GPU
    if (gpuDevice->getBuffer(cpuDataPointer) != NULL) {
        chanceGPU += 0.3;
        return;
    }
#endif
#ifdef ENABLE_MIC
    if (micDevice->getBuffer(cpuDataPointer) != NULL) {
        chanceMIC += 0.3;
        return;
    }
#endif
}

// Helper in C
extern "C" void bonusBigData() {

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : bonusBigData";
    std::cout.flush();
#endif

    if (sum > THRESHOLD) {
        chanceLEG -= 0.2;
        chanceCPU += 0.1;
        chanceGPU += 0.2;
        chanceMIC += 0.15;
    }
    else {
        chanceLEG += 0.2;
        chanceCPU -= 0.1;
        chanceGPU -= 0.2;
        chanceMIC -= 0.15;
    }
}

// Helper in C
extern "C" void calculateBonusAndDecide() {

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : calculateBonusAndDecide";
    std::cout.flush();
#endif

    // Bonus for big data sizes. 
    bonusBigData();

    // Bonus for last accelerator.
    bonusLastAccelerator();

    // Penalty, if accelerator is not available.
    bonusIfAvailable();

    // Decide which acclerator to take and update.
    decideWhichAccelerator();
}

// Helper in C
//Overloaded setKernelArg functions
extern "C" cl_int setKernelArg_cl_mem(DeviceType deviceType, std::string kernelName, int position, cl_mem * clBuffer) {
#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : setKernelArg_cl_mem";
    std::cout.flush();
#endif

    OpenCLDevice *device = resolveDevice(deviceType);

    if (device != NULL) {
        cl_kernel kernel = device->getKernel(kernelName);

        assert(kernel != NULL);
        return clSetKernelArg(kernel, position, sizeof(cl_mem), clBuffer);
    }

    return -1;
}

extern "C" cl_int setKernelArg_char(DeviceType deviceType, std::string kernelName, int position, char clBuffer) {
#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : setKernelArg_char";
    std::cout.flush();
#endif
    OpenCLDevice *device = resolveDevice(deviceType);

    if (device != NULL) {
        cl_kernel kernel = device->getKernel(kernelName);

        return clSetKernelArg(kernel, position, sizeof(char), &clBuffer);
    }
    return -1;
}

extern "C" cl_int setKernelArg_int(DeviceType deviceType, std::string kernelName, int position, int clBuffer) {
#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : setKernelArg_int";
    std::cout.flush();
#endif
    OpenCLDevice *device = resolveDevice(deviceType);

    if (device != NULL) {
        cl_kernel kernel = device->getKernel(kernelName);

        return clSetKernelArg(kernel, position, sizeof(int), &clBuffer);
    }
    return -1;
}

extern "C" cl_int setKernelArg_long(DeviceType deviceType, std::string kernelName, int position, long clBuffer) {
#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : setKernelArg_long";
    std::cout.flush();
#endif
    OpenCLDevice *device = resolveDevice(deviceType);

    if (device != NULL) {
        cl_kernel kernel = device->getKernel(kernelName);

        return clSetKernelArg(kernel, position, sizeof(long), &clBuffer);
    }
    return -1;
}

extern "C" cl_int setKernelArg_float(DeviceType deviceType, std::string kernelName, int position, float clBuffer) {
#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : setKernelArg_float";
    std::cout.flush();
#endif
    OpenCLDevice *device = resolveDevice(deviceType);

    if (device != NULL) {
        cl_kernel kernel = device->getKernel(kernelName);

        return clSetKernelArg(kernel, position, sizeof(float), &clBuffer);
    }
    return -1;
}

extern "C" cl_int setKernelArg_double(DeviceType deviceType, std::string kernelName, int position, double clBuffer) {
#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : setKernelArg_double";
    std::cout.flush();
#endif
    OpenCLDevice *device = resolveDevice(deviceType);

    if (device != NULL) {
        cl_kernel kernel = device->getKernel(kernelName);

        return clSetKernelArg(kernel, position, sizeof(double), &clBuffer);
    }
    return -1;
}

//Helper in C
extern "C" cl_mem transferToDevice(DeviceType deviceType, void *cpuDataPointer, DataTransferType transferType, int dataTypeSize, long size) {

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : transferToDevice";
    std::cout.flush();
#endif

    cl_int ret;
    cl_mem clBuffer = NULL;
    OpenCLDevice *device = resolveDevice(deviceType);

    if (device != NULL) {
        clBuffer = device->getBuffer(cpuDataPointer);
        if (clBuffer == NULL) {
            clBuffer = device->createAndPopulateBuffer(cpuDataPointer, transferType, dataTypeSize, size);

#ifdef HTROP_DEBUG
            std::cout << "\nHTROP INFO : host (" << cpuDataPointer << ") clBuffer (" << clBuffer << ") :" << resolveDataTransferTypeName(transferType);
            std::cout.flush();
#endif

        }
#ifdef HTROP_DEBUG
        else {
            std::cout << "\nHTROP INFO : clBuffer " << cpuDataPointer << " found";
            std::cout.flush();
        }
#endif
    }
    return clBuffer;
}

//Helper in C
extern "C" void executeOCLKernel(DeviceType deviceType, std::string kernelName, int dimension, const size_t global_work_size[], const size_t local_work_size[]) {
#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : executeOCLKernel";
    std::cout.flush();
#endif

    OpenCLDevice *device = resolveDevice(deviceType);

    if (device != NULL) {
        clEnqueueNDRangeKernel(device->getCommandQueue(), device->getKernel(kernelName), dimension, NULL, global_work_size, local_work_size, 0, NULL, NULL);
        cl_int ret = clFinish(device->getCommandQueue());
    }
}

//Helper in C
extern "C" void oclFinish(DeviceType deviceType) {
#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : oclFinish";
    std::cout.flush();
#endif

    cl_int ret;
    OpenCLDevice *device = resolveDevice(deviceType);

    if (device != NULL) {
        ret = clFinish(device->getCommandQueue());
    }
}

//Helper in C
extern "C" void cleanUpBuffers_Device(DeviceType deviceType) {
#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : cleanUpBuffers_Device";
    std::cout.flush();
#endif
    OpenCLDevice *device = resolveDevice(deviceType);

    if (device != NULL) {
        device->cleanUpBuffers();
    }
}

//Helper in C
extern "C" void cleanupBuffers() {
#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : cleanupBuffers";
    std::cout.flush();
#endif

    //to cleanup or not to cleanup
    if (runtimeDecision_Handle->previousAcelerator != runtimeDecision_Handle->currentAcelerator) {
        cleanUpBuffers_Device(runtimeDecision_Handle->previousAcelerator);
    }
}

//Helper in C
extern "C" bool isLegacy() {
#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : isLegacy";
    std::cout.flush();
#endif

    return runtimeDecision_Handle->currentAcelerator == LEG;
}

//helper in C
extern "C" void initChance() {

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : initChance";
    std::cout.flush();
#endif

    chanceLEG = 0.0;
    chanceCPU = 0.05;
    chanceGPU = 0.15;
    chanceMIC = 0.1;
    sum = 0;
    switchOnFirst = false;
}

//helper in C
extern "C" void addToSum(long size) {

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : addToSum";
    std::cout.flush();
#endif

    sum += size;
}

//helper in C
extern "C" DeviceType getCurrentAccelerator() {

#ifdef HTROP_DEBUG
    std::cout << "\nHTROP INFO : getCurrentAccelerator";
    std::cout.flush();
#endif

    return runtimeDecision_Handle->currentAcelerator;
}
