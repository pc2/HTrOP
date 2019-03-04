cmake_minimum_required(VERSION 2.8)

project(HTROP)

message(STATUS "module path: ${CMAKE_MODULE_PATH} + ${CMAKE_ROOT}")

if(NOT LLVM_SRC_DIR)
    message(FATAL_ERROR "LLVM_SRC_DIR undefined, please define it to point to LLVM source")
endif()

if(NOT LLVM_BIN_DIR)
    message(FATAL_ERROR "LLVM_BIN_DIR undefined, please define it to point to the LLVM build corresponding to the sources in LLVM_SRC_DIR")
endif()

# GOOGLE TEST
if(NOT GTEST_SRC_DIR)
    message(FATAL_ERROR "GTEST_SRC_DIR undefined, please define it to point to googletest source")
endif()

if(NOT GTEST_BIN_DIR)
    message(FATAL_ERROR "GTEST_BIN_DIR undefined, please define it to point to the googletest build corresponding to the sources in GTEST_SRC_DIR")
endif()

set(CMAKE_BUILD_TYPE Debug)

add_definitions(-D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_FORMAT_MACROS -DGOOGLE_PROTOBUF_NO_RTTI)

#debug flags
# add_definitions(-DHTROP_DEBUG)
# add_definitions(-DHTROP_SCOP_DEBUG)
#add_definitions(-DHTROP_SCORE_DEBUG)

#energy measurement
#add_definitions(-DEVAL)

#performance measurement / overheads
add_definitions(-DMEASURE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -std=c++11")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/bin)

add_definitions(-DCMAKE_DEP_PATH="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

link_directories(${LLVM_BIN_DIR}/lib )
include_directories(${LLVM_SRC_DIR}/include ${LLVM_BIN_DIR}/include ${LLVM_SRC_DIR}/projects/axtor/include)

set(LLVM_POLLY_SRC_DIR "${LLVM_SRC_DIR}/tools/polly")
set(LLVM_POLLY_BIN_DIR  "${LLVM_BIN_DIR}/tools/polly")

include_directories(
  BEFORE
  ${LLVM_POLLY_SRC_DIR}/include
  ${LLVM_POLLY_SRC_DIR}/lib/JSON/include
  ${LLVM_POLLY_BIN_DIR}/lib/External/isl/include
  ${LLVM_POLLY_SRC_DIR}/lib/External/isl/include
  ${LLVM_POLLY_BIN_DIR}/include
  )

find_package(Threads REQUIRED)

if(PROTOBUF_BIN_DIR)
  link_directories(${PROTOBUF_BIN_DIR}/ )
  if(NOT PROTOBUF_SRC_DIR)
    message(FATAL_ERROR "PROTOBUF_SRC_DIR undefined, please define it to point to the PROTOBUF source directory")
  endif()
  include_directories(${PROTOBUF_SRC_DIR}/src )
else()
  find_package(Protobuf REQUIRED)
endif()


add_subdirectory(common)
add_subdirectory(client)
add_subdirectory(server)

#OCL Dependencies
if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "${regex}")
  if(EXISTS "${OCL_SDK_ROOT}")
	FIND_PATH( OPENCL_INCLUDE_DIRS NAMES CL/cl.h OpenCL/cl.h NO_DEFAULT_PATH PATHS ${OCL_SDK_ROOT}/include )
	  FIND_LIBRARY( OPENCL_LIBRARIES NAMES OpenCL NO_DEFAULT_PATH PATHS ${OCL_SDK_ROOT}/lib )	
  elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	  FIND_LIBRARY(OPENCL_LIBRARIES OpenCL DOC "OpenCL lib for OSX")
	  FIND_PATH(OPENCL_INCLUDE_DIRS OpenCL/cl.h DOC "Include for OpenCL on OSX")
  elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	  if(EXISTS "$ENV{AMDAPPSDKROOT}")
	    FIND_PATH( OPENCL_INCLUDE_DIRS NAMES CL/cl.h OpenCL/cl.h PATHS $ENV{AMDAPPSDKROOT}/include )
	    FIND_LIBRARY( OPENCL_LIBRARIES NAMES OpenCL PATHS $ENV{AMDAPPSDKROOT}/lib/x86_64 )
	  else()
	    FIND_PATH(OPENCL_INCLUDE_DIRS CL/cl.h DOC "Include for OpenCL on Linux")    
	    FIND_LIBRARY(OPENCL_LIBRARIES OpenCL ENV LD_LIBRARY_PATH)
	  endif()
  endif()
endif()

if(EXISTS "${OPENCL_INCLUDE_DIRS}")
  message(STATUS "OpenCL Include Directory => ${OPENCL_INCLUDE_DIRS}")
  set(OpenCL_INCLUDE_DIR ${OPENCL_INCLUDE_DIRS})
elseif(EXISTS "${OpenCL_INCLUDE_DIR}")
  find_package(OpenCL)
  include_directories(${OpenCL_INCLUDE_DIR})
  message(STATUS "OpenCL Include Directory => ${OpenCL_INCLUDE_DIR}")
endif()

#compile the dependencies
set (device_dir "${PROJECT_SOURCE_DIR}/common/")

set (compiler_cmd "${LLVM_BIN_DIR}/bin/clang++")
set (compiler_arg_0 "-S")
set (compiler_arg_1 "-std=c++11")
set (compiler_arg_2 "-I/${OpenCL_INCLUDE_DIR}")
set (compiler_arg_3 "-I/${device_dir}")
set (compiler_arg_4 "-emit-llvm")
set (compiler_arg_5 "-O0")
set (compiler_arg_6 "-o")

#NOTE: Comment out/in depending on the available devices
set (compiler_arg_7 "-DENABLE_MCPU") # for only m-cpu
# set (compiler_arg_7 "-DENABLE_MCPU -DENABLE_GPU -DENABLE_MIC") #for m-cpu, gpu and mic

#debug flag
# set (compiler_arg_8 "-DHTROP_DEBUG")

#enable measurements
set (compiler_arg_9 "-DMEASURE")

#set the threshold here
set (compiler_arg_10 "-DTHRESHOLD=15")

set (compiler_input "${PROJECT_SOURCE_DIR}/dependencies/registerOpenCL.cpp")
set (compiler_output "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/registerOpenCL.bc")
execute_process(COMMAND ${compiler_cmd} ${compiler_arg_0} ${compiler_arg_7} ${compiler_arg_8} ${compiler_arg_9} ${compiler_arg_10} ${compiler_arg_1} ${compiler_arg_2} ${compiler_arg_4} ${compiler_arg_5} ${compiler_input} ${compiler_arg_6} ${compiler_output}
				WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

#oclFunctions
set (compiler_input "${PROJECT_SOURCE_DIR}/dependencies/oclFunctions.cpp")
set (compiler_output "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/oclFunctions.bc")
execute_process(COMMAND ${compiler_cmd} ${compiler_arg_0} ${compiler_arg_7} ${compiler_arg_8} ${compiler_arg_9} ${compiler_arg_10} ${compiler_arg_1} ${compiler_arg_2} ${compiler_arg_4} ${compiler_input} ${compiler_arg_6} ${compiler_output}
				WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})