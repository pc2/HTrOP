add_subdirectory(pass)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
add_executable(htrop_client htropclient.cpp main.cpp)

find_library(GTEST_LIBRARY gtest HINTS ${GTEST_BIN_DIR})

if( NOT GTEST_LIBRARY )
  message( FATAL_ERROR "GTEST_LIBRARY not found" )
else()
  message( STATUS "GTEST_LIBRARY lib: ${GTEST_LIBRARY}" )
endif()

include_directories(${GTEST_SRC_DIR}/include)
target_link_libraries(htrop_client ${GTEST_LIBRARY})

target_link_libraries(htrop_client htrop_client_passes htrop_common htrop_proto)

#architecture dependent
set( regex "x86*" )
if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "${regex}")
  target_link_libraries(htrop_client
    LLVMX86Disassembler
    LLVMX86AsmParser
    LLVMX86CodeGen
    LLVMX86Desc
    LLVMX86Info
    LLVMX86AsmPrinter
    LLVMX86Utils
  )
else()
 target_link_libraries(htrop_client
    LLVMARMDisassembler
    LLVMARMAsmParser
    LLVMARMCodeGen
    LLVMARMDesc
    LLVMARMInfo
  )
endif()

target_link_libraries(htrop_client
        LLVMPolly
        LLVMIRReader
        LLVMBitReader
        LLVMBitWriter
        LLVMAsmParser
        LLVMSelectionDAG
        LLVMAsmPrinter
        LLVMMCParser
        LLVMMCJIT
        LLVMRuntimeDyld
        LLVMExecutionEngine
        LLVMCodeGen
        LLVMipo
        LLVMVectorize
        LLVMObjCARCOpts
        LLVMScalarOpts
        LLVMInstCombine
        LLVMInstrumentation
        LLVMTransformUtils
        #LLVMipa
        LLVMAnalysis
        LLVMTarget
        LLVMMC
        LLVMObject
        LLVMCore
        LLVMSupport
        LLVMLinker        
)

#start 3.5+
find_package( LLVM )
if( NOT LLVM_FOUND )
  message( FATAL_ERROR "LLVM package can't be found. Set CMAKE_PREFIX_PATH variable to LLVM's installation prefix." )
else()
  message( STATUS "LLVM lib: ${LLVM_LIBRARY_DIRS}" )
endif()

include_directories( ${LLVM_INCLUDE_DIRS} )
link_directories( ${LLVM_LIBRARY_DIRS} )
add_definitions( ${LLVM_DEFINITIONS} )

llvm_map_components_to_libraries(
  REQ_LLVM_LIBRARIES
  nativecodegen
)
#end 3.5+

target_link_libraries(htrop_client ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})

#OCL Dependencies
if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "${regex}")
  if(EXISTS "${OCL_SDK_ROOT}")
	FIND_PATH( OPENCL_INCLUDE_DIRS NAMES CL/cl.h OpenCL/cl.h NO_DEFAULT_PATH PATHS ${OCL_SDK_ROOT}/include )
	  #FIND_PATH( _OPENCL_CPP_INCLUDE_DIRS NAMES CL/cl.hpp OpenCL/cl.hpp PATHS ${OCL_SDK_ROOT}/include )
	  FIND_LIBRARY( OPENCL_LIBRARIES NAMES OpenCL NO_DEFAULT_PATH PATHS ${OCL_SDK_ROOT}/lib )	
  elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	  FIND_LIBRARY(OPENCL_LIBRARIES OpenCL DOC "OpenCL lib for OSX")
	  FIND_PATH(OPENCL_INCLUDE_DIRS OpenCL/cl.h DOC "Include for OpenCL on OSX")
	  #FIND_PATH(_OPENCL_CPP_INCLUDE_DIRS OpenCL/cl.hpp DOC "Include for OpenCL CPP bindings on OSX")
  elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	  if(EXISTS "$ENV{AMDAPPSDKROOT}")
	    FIND_PATH( OPENCL_INCLUDE_DIRS NAMES CL/cl.h OpenCL/cl.h PATHS $ENV{AMDAPPSDKROOT}/include )
	    #FIND_PATH( _OPENCL_CPP_INCLUDE_DIRS NAMES CL/cl.hpp OpenCL/cl.hpp PATHS $ENV{AMDAPPSDKROOT}/include )
	    FIND_LIBRARY( OPENCL_LIBRARIES NAMES OpenCL PATHS $ENV{AMDAPPSDKROOT}/lib/x86_64 )
	  else()
	    FIND_PATH(OPENCL_INCLUDE_DIRS CL/cl.h DOC "Include for OpenCL on Linux")
	    #FIND_PATH(_OPENCL_CPP_INCLUDE_DIRS CL/cl.hpp DOC "Include for OpenCL CPP bindings on Linux")
	    FIND_LIBRARY(OPENCL_LIBRARIES OpenCL ENV LD_LIBRARY_PATH)
	  endif()
  endif()
endif()

if(EXISTS "${OPENCL_INCLUDE_DIRS}")
  include_directories(${OPENCL_INCLUDE_DIRS})
  message(STATUS "OpenCL Include Directory => ${OPENCL_INCLUDE_DIRS}")
elseif(EXISTS "${OpenCL_INCLUDE_DIR}")
  find_package(OpenCL)
  include_directories(${OpenCL_INCLUDE_DIR})
  message(STATUS "OpenCL Include Directory => ${OpenCL_INCLUDE_DIR}")
endif()

#OpenCLDevice class (Use this to include OpenCL functions while linking
include_directories(OpenCLDevice)
add_library(OpenCLDevice        STATIC ${PROJECT_SOURCE_DIR}/common/openCLDevice.cpp)
SET (OpenCLDeviceLIB -Wl,--whole-archive OpenCLDevice -Wl,--no-whole-archive)


#ampehre
# set (lib1_location "/usr/ampehre/lib/libms_common.so")
# set (lib2_location "/usr/ampehre/lib/libms_common_apapi.so")
# target_link_libraries(htrop_client ${lib1_location} ${lib2_location})

if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "${regex}")
  target_link_libraries( htrop_client  ${OpenCLDeviceLIB} ${OPENCL_LIBRARIES} "-lprotobuf")
else()
  target_link_libraries( htrop_client ${MonitorLIB} CSocket BinarySemaphore ${OpenCLDeviceLIB} "-lprotobuf -L/root/libs -lOpenCL -lmali" )
endif()


if(UNIX AND NOT APPLE)
    target_link_libraries(htrop_client rt)
endif()

if(CURSES_FOUND AND NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "k1om")
    target_link_libraries(htrop_client ${CURSES_LIBRARIES})
endif()

