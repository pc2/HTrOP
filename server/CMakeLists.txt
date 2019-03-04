set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")

add_executable(htrop_server main.cpp htropserver.cpp openCLCbackend.cpp)
target_link_libraries(htrop_server htrop_server_util htrop_common htrop_proto)

#architecture dependent
set( regex "x86*" )
if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "${regex}")
  target_link_libraries(htrop_server
    LLVMX86Disassembler
    LLVMX86AsmParser
    LLVMX86CodeGen
    LLVMX86Desc
    LLVMX86Info
    LLVMX86AsmPrinter
    LLVMX86Utils
  )
else()
 target_link_libraries(htrop_server
    LLVMARMDisassembler
    LLVMARMAsmParser
    LLVMARMCodeGen
    LLVMARMDesc
    LLVMARMInfo
  )
endif()

target_link_libraries(htrop_server
	LLVMIRReader 
	LLVMBitReader 
	LLVMAsmParser 
	LLVMSelectionDAG 
	LLVMAsmPrinter 
	LLVMMCParser 
	LLVMMCJIT 
	LLVMRuntimeDyld
	LLVMInterpreter
	LLVMExecutionEngine 
	LLVMCodeGen
	LLVMipo
	LLVMVectorize
	LLVMObjCARCOpts 
	LLVMScalarOpts 
	LLVMInstCombine
	LLVMInstrumentation
	LLVMTransformUtils 
	LLVMAnalysis 
	LLVMTarget 
	LLVMMC 
	LLVMObject 
	LLVMCore 
	LLVMSupport
	LLVMLinker
	Axtor
)

#start 3.5+
find_package( LLVM )
if( NOT LLVM_FOUND )
  message( FATAL_ERROR "LLVM package can't be found. Set CMAKE_PREFIX_PATH variable to LLVM's installation prefix." )
else()
  message( STATUS "LLVM lib: ${LLVM_LIBRARY_DIRS}" )
endif()

add_subdirectory(utils)

include_directories( ${LLVM_INCLUDE_DIRS} )
link_directories( ${LLVM_LIBRARY_DIRS} )
add_definitions( ${LLVM_DEFINITIONS} )

llvm_map_components_to_libraries(
  REQ_LLVM_LIBRARIES
  nativecodegen
)
#end 3.5+

include_directories(/usr/lib64/libffi-3.0.5/include)
include_directories(utils)

target_link_libraries(htrop_server ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(htrop_server "-lprotobuf")

message(STATUS "CMAKE_SYSTEM_PROCESSOR is ${CMAKE_SYSTEM_PROCESSOR}")

if(UNIX AND NOT APPLE)
    target_link_libraries(htrop_server rt)
endif()