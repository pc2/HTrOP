HTrOP
=====

This repository contains HTrOP, a prototype implementation to automatically generate and execute OpenCL code from sequential CPU code. Computational hotspots can be automatically identified and transparently offloaded to different resources (tested with CPU, GPGPU and Xeon Phi). 


## Requirements

##### LLVM Environment

1. llvm-3.8.0 ([http://releases.llvm.org/3.8.0/llvm-3.8.0.src.tar.xz]())
2. clang ([http://releases.llvm.org/3.8.0/cfe-3.8.0.src.tar.xz]())
3. polly ([http://releases.llvm.org/3.8.0/polly-3.8.0.src.tar.xz]())
4. axtor ([https://github.com/cdl-saarland/axtor/tree/llvm_38]())

##### Test Suite and Communication Library

5. protobuf-3.0.0-beta-3.1 ([https://github.com/protocolbuffers/protobuf.git]())
6. googletest ([https://github.com/google/googletest]())

## Steps for buildng the requirements

1. Build llvm from source with CMake (See: [Building LLVM with CMake](https://llvm.org/docs/CMake.html), [Polly - Getting Started](https://polly.llvm.org/get_started.html))
2. Download and extract llvm, clang and polly
3. Place clang and polly into the right directories in the llvm source directory
4. Download axtor and place it in the project folder in the llvm source
5. Rebuild llvm
6. Build protobuf and googletest according to the documentation

## Steps for building HTrOP with CMake

1. Download HTrOP, create a seperate HTrOP build directory and switch to it.
2. Configure build with CMake


    cmake -DLLVM_SRC_DIR=<path to LLVM source directory> -DLLVM_BIN_DIR=<path to LLVM build directory> -DLLVM_DIR=<path to LLVM build directory>/cmake/modules/CMakeFiles -DPROTOBUF_SRC_DIR=<path to protobuf source directory> -DPROTOBUF_BIN_DIR=<path to protobuf build directory> -DGTEST_SRC_DIR=<path to googletest source directory> -DGTEST_BIN_DIR=<path to googletest build directory>/googlemock/gtest <path to HTroP source diirectory>
3. Build with make

## Running

1. Go to \<HTrOP source directory>/test/
2. Update the Common.mk with the correct paths
3. Go to motion and run: make llvmir
4. Go to \<HTrOP build directory>/out/bin
5. Start the htrop_server in the background
6. Start the htrop_client with the generated file (step 3) as then argument

For more options run the htrop_server and htrop_client with the __-help__ option


## Troubleshooting

Issue : Error in hds.pb.*  
Solution: Go to \<HTrOP source directory>/common/hds.proto and follow the instructions to generate the hds files

## Publications

* H. Riebler, G. Vaz, T. Kenter, C. Plessl. __Automated Code Acceleration Targeting Heterogeneous OpenCL Devices.__ In *Proc. ACM SIGPLAN Symposium on Principles and Practice of Parallel Programming (PPoPP), ACM*, 2018.
* H. Riebler, G. Vaz, T. Kenter, C. Plessl. __Transparent Acceleration for Heterogeneous Platforms with Compilation to OpenCL.__ In *ACM Transactions on Architecture and Code Optimization (TACO) Volume 16 Issue April, 2019.

Please cite
       
    @article{riebler19transparent,
     author = {Riebler, Heinrich and Vaz, Gavin and Kenter, Tobias and Plessl, Christian},
     title = {Transparent Acceleration for Heterogeneous Platforms With Compilation to OpenCL},
     journal = {ACM Trans. Archit. Code Optim.},
     issue_date = {April 2019},
     volume = {16},
     number = {2},
     month = apr,
     year = {2019},
     issn = {1544-3566},
     pages = {14:1--14:26},
     articleno = {14},
     numpages = {26},
     url = {http://doi.acm.org/10.1145/3319423},
     doi = {10.1145/3319423},
     acmid = {3319423},
     publisher = {ACM},
     address = {New York, NY, USA},
     keywords = {OpenCL, Transparent acceleration, multi-accelerator, runtime system},
    } 

