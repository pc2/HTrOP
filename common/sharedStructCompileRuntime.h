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

#ifndef SHAREDSTRUCTCOMPILETIMERUNTIME_INCLUDED
#define SHAREDSTRUCTCOMPILETIMERUNTIME_INCLUDED

#include <iostream>
#include <limits.h>

#include "hds.pb.h"
#include "dataTransferType.h"
// Argument type of hotspot function. Depending of the type, the argument needs to
// be transfered to/from the accelerator.

struct DataSize {
    llvm::Value * value;        //Load (global), const, or function argument (see position)
    int positionInArgList;
};

/**
 * @brief This struct is an intern helper to abstract the structure of the loop.
 * 
 * for(i = 0; i < N; i++)
 * 
 * We are interested in N.
 *  - the name as string
 *  - the maximum value (technically the trip count).
 * 
 */
struct ScopLoopInfo {
    // Name of the upper bound.
    std::string nameStr = "";
    // Maximum value for the trip count. This is the upper bound
    //   from static SCoP analysis.
    unsigned long long maxValue = 0;
    // Upper bound of the trip count. This is the string, still 
    //   containing variables: i.e. "-2 + n"
     std::string maxValueStr = "";
};

// Argument of hotspot function.
typedef struct {

    llvm::Value * value;
    DataTransferType type = UNKNOWN;
     std::vector < DataSize > dataSizeDependencies;
    bool isPointer = false;
    // The name as string.
     std::string name = "";
    // Dimension of array.
    //   Array[] has dimension = 1. Array[][] has dimension = 2.
    unsigned int dimension = 0;
    // Min value for each dimension.
    //   i.e. Array[][] has dimension = 2.
    //        dimension_min[0] is the minimum value of Array[THIS][]
    //        dimension_min[1] is the minimum value of Array[][THIS]
     std::vector < long long >dimension_min;
     std::vector < long long >dimension_max;
     std::vector < std::string > dimension_minStr;
     std::vector < std::string > dimension_maxStr;
     std::vector < long >dimension_offset_min;
     std::vector < long >dimension_offset_max;

#ifdef HTROP_SCOP_DEBUG
    void dump() {
        std::string type_s = "\"" + name + "\"";

        // Print general dimension.
        for (int i = 0; i < dimension; i++) {
            type_s += "[]";
        } type_s += " ";

        std::string str_min = "";
        // Try to print MIN/MAX, if available.
        for (auto const &value:dimension_min) {
            str_min += "[" + std::to_string(value) + "]";
        }
        for (auto const &value:dimension_minStr) {
            str_min += "[STR: " + value + "]";
        }
        for (auto const &value:dimension_offset_min) {
            str_min += "[offset " + std::to_string(value) + "]";
        }

        std::string str_max = "";
        // Try to print MIN/MAX, if available.
        for (auto const &value:dimension_max) {
            str_max += "[" + std::to_string(value) + "]";
        }
        for (auto const &value:dimension_maxStr) {
            str_max += "[STR: " + value + "]";
        }

        for (auto const &value:dimension_offset_max) {
            str_max += "[offset " + std::to_string(value) + "]";
        }

        type_s += " MIN (" + str_min + ")" + " MAX (" + str_max + ") ";

        switch (type) {
        case UNKNOWN:
            type_s += "UNKNOWN";
            break;
        case IN:
            type_s += "IN";
            break;
        case OUT:
            type_s += "OUT";
            break;
        case IN_OUT:
            type_s += "IN_OUT";
            break;
        case TMP:
            type_s += "TMP";
            break;
        }

        if (isPointer) {
            type_s += " (pointer) ";
        }
        else {
            type_s += " (non pointer) ";
        }

        type_s += " value -> ";
        if (value) {
            value->dump();
        }
        else {
            type_s += " null";
        }

        std::cout << "      " << type_s << std::endl;
    }
#endif

    void resize(unsigned int new_dimension) {
        if (dimension != new_dimension) {
            dimension = new_dimension;
            dimension_min.resize(dimension);
            dimension_max.resize(dimension);
            dimension_minStr.resize(dimension);
            dimension_maxStr.resize(dimension);
            dimension_offset_min.resize(dimension);
            dimension_offset_max.resize(dimension);

            // Set min. default to the biggest value.
            for (int i = 0; i < new_dimension; i++) {
                dimension_offset_min.at(i) = LONG_MAX;
            }

            // Set max. default to the smallest value.
            for (int i = 0; i < new_dimension; i++) {
                dimension_offset_max.at(i) = LONG_MIN;
            }
        }
    }

} ScopFnArg;

typedef struct {
    // Flag to tell if parameter is also in parent argument list. 
    //   Default is -1, which means temp var.
    int positionInParent = -1;
     llvm::Value * value;
    DataTransferType typeOptimized;
     std::vector < std::string > dimension_maxStr;
     std::vector < DataSize > dataSizeDependencies;

#ifdef HTROP_SCOP_DEBUG
    void dump(int indent) {
        std::string indent_s = "";
        for (int i = 0; i < indent; i++) {
            indent_s += " ";
        } std::cout << indent_s;

        std::cout.flush();
        value->dump();
        std::cout.flush();

        std::string type_s = indent_s + " (positionInParent = \"" + std::to_string(positionInParent) + "\", ";

        type_s += indent_s + "typeOptimized = [";

        switch (typeOptimized) {
        case UNKNOWN:
            type_s += "UNKNOWN";
            break;
        case IN:
            type_s += "IN";
            break;
        case OUT:
            type_s += "OUT";
            break;
        case IN_OUT:
            type_s += "IN_OUT";
            break;
        case TMP:
            type_s += "TMP";
            break;
        }

        type_s += "]. ";

        std::string str_max = "";
        // Try to print MIN/MAX, if available.
        for (auto const &value:dimension_maxStr) {
            str_max += "[STR: " + value + "]";
        }

        type_s += " dimension_maxStr (" + str_max + ") ";

        std::cout << type_s << std::endl;

    }

    void dump() {
        dump(0);
    }
#endif

} ScopCallFnArg;

typedef struct {
    uint32_t affinity;
    uint64_t codeGenTime;
     llvm::Function * function;
     llvm::Function * registrationFunction;
     llvm::Function * initFunction;
     llvm::Value * globalRegMutex;
     llvm::GlobalVariable * globalFunctionPtr;
} ResourceInfo;

typedef struct {
    llvm::Function * scopFunction;      // Function containing the scop
    llvm::Function * scopFunctionParent;        // Function calling the function containing the scop.
    std::vector < ScopFnArg * >scopFunctonArgs; // Parameter analysis
    std::vector < ScopLoopInfo * >scopLoopInfo; // SCop loop analysis
    std::map < DeviceType, ResourceInfo > resources;    // Resources and affinity
    int maxParalleizationDepth = 0;
} ScopDS;

typedef struct {
    // ID of Scop that this call points to (referred to ScopDSMap).
    //   The name of the function containing the scop is unique. Hence, we use it.
    std::string scopID;
    // The CallInst that calls the function containing the scop.
    llvm::CallInst * callInst;
    // Function argument analysis.
    std::vector < ScopCallFnArg * >scopCallFunctonArgs;
    
#ifdef HTROP_SCOP_DEBUG
    void dump() {
        std::string type_s = "ScopCall to scop \"" + scopID + "\". \n";

        std::cout << "      " << type_s << std::endl;
    }
#endif
} ScopCallDS;

#endif                          // SHAREDSTRUCTCOMPILETIMERUNTIME_INCLUDED
