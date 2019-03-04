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

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdlib.h>
#include <assert.h>

#include "../../common/stringHelper.h"
#include "cl_replace.h"
using namespace std;

void removeBaseFunction(std::string oclFile, std::string kernelName) {
    std::ifstream inFile {
    oclFile.c_str()};
    std::string inputData((std::istreambuf_iterator < char >(inFile)), std::istreambuf_iterator < char >());

    std::ofstream outStream;
    outStream.open(oclFile.c_str());

    auto lines = explodeStr(inputData, "\n");

    for (unsigned int lineIter = 0; lineIter < lines.size(); lineIter++) {
        std::string line = lines[lineIter];
        // Line has the kernel.
        if (line.find(kernelName) != std::string::npos) {
            break;
        }
        outStream << "" << line << "\n";
    }
    outStream << "\n";
    outStream.close();
}

void replaceOCLArrayPointers(std::string inFile, std::string outFile) {
    string line;

    ofstream outStream(outFile);

    if (!outStream.is_open()) {
        cout << "Unable to open write file";
    }

    ifstream myfile(inFile);

    if (myfile.is_open()) {

        // Flag if we found a kernel.
        bool is_kernel_found = false;

        // Depending on the function definition, we are directly in the kernel body with the next line,
        // or we need to consume more lines until we find the opening curly bracket {.
        bool in_kernel_function = false;

        // Have defined the parameter widths.
        bool array_width_defined = false;

        // Detect when function is over, therefore we count opening and closing curly brackets.
        int bracketCount = 0;

        // Store the replace parameters here.
        std::vector < std::pair < std::string, long >>replace_params;

        while (getline(myfile, line)) {
            std::string lineCopy = line;
            std::string lineCopy2 = line;
            trim(lineCopy2);

            // ### Adjust kernel function parameters.
            //  Idea: we have a kernel function like
            //        __kernel void gpu__Z8kernel_0iPA16000_dS0_(int n, __global double[16000]* A, __global double[16000]* B)
            //        1) we remove the [16000], pointer to pointer array will be a pointer array.
            //        2) we store the value 16000 as the width of the array.
            int t_func_needle = line.find("__kernel");

            // Line has the needle.
            if (t_func_needle > -1) {
                // We found a kernel.
                is_kernel_found = true;
                // Reset values, if we have more kenerls.
                in_kernel_function = false;
                array_width_defined = false;
                replace_params.clear();

                // Does the kernel body start directly with the next line?
                int t_curly = line.find("{");

                if (t_curly > -1) {
                    in_kernel_function = true;
                }

                // Match the parameter list, text between ( ), i.e.
                //   int n, __global double[16000]* A, __global double[16000]* B
                int t_params_start = line.find("(");
                int t_params_end = line.find(")");

                // Store each parameter in vector.
                std::string func_paramsStr = line.substr(t_params_start + 1, t_params_end - t_params_start - 1);
                std::vector < std::string > func_params = explodeStr(func_paramsStr, ", ");

                // Resamble new function parameters.
                std::string func_paramsStr_new = "";

                for (auto param:func_params) {
                    // int n, __global double[16000]* A, __global double[16000]* B
                    // int n, __global double* A, __global double* B

                    // Check if parameter contains [].
                    int t_param_start = param.find("[");
                    bool param_replaced = false;

                    if (t_param_start > -1) {
                        int t_param_end = param.find("]");

                        std::string paramWidthStr = param.substr(t_param_start + 1, t_param_end - t_param_start - 1);
                        long paramWidth = stol(paramWidthStr);

                        std::string paramName = param.substr(param.rfind(" ") + 1);

                        replace_params.push_back(std::make_pair(paramName, paramWidth));
                        param_replaced = true;

                        param = param.replace(t_param_start, t_param_end - t_param_start + 1, "");
                    }

                    //Add a restrict keyword for pointers
                    int t_param_last = param.find("* ");

                    if (t_param_last != std::string::npos) {
                        param.replace(t_param_last, 1, "* restrict ");
                    }
                    func_paramsStr_new += param + ", ";
                }

                // Remove the last ", ".
                if (func_params.size() > 1) {
                    func_paramsStr_new = func_paramsStr_new.substr(0, func_paramsStr_new.length() - 2);
                }

                // Resamble the new function parameters.
                line = line.replace(t_params_start + 1, t_params_end - t_params_start - 1, func_paramsStr_new);

                outStream << "" << "// ### REPLACED FUNCTION PARAMETERS: " << lineCopy << "\n";
            }

            // ### 2d array access replacement.
            //  Idea: we have a 2d array access pattern, like "A[x][y];"
            //        and replace it with a "flat" access to a 1d array.
            //        Here, "A[(x) * array_width + (y)]"

            // We look for 2d array access: "[x][y]"
            std::string s_array_needle = "][";
            int t_array_needle = line.find(s_array_needle);
            int start_pos = 0;
            bool replaced = false;

            // Line has the needle.
            while (t_array_needle > -1) {
                // find first "[" of "[x][y]"
                int t_pos_dim_0_start = line.find("[", start_pos);
                int t_pos_dim_1_end = line.find("]", t_array_needle + 2);

                // Parse the name A.
                std::string arrayName = line.substr(0, t_pos_dim_0_start);
                arrayName = arrayName.substr(arrayName.rfind(" ") + 1);

                std::string array_dim_0 = line.substr(t_pos_dim_0_start + 1, t_array_needle - t_pos_dim_0_start - 1);
                std::string array_dim_1 = line.substr(t_array_needle + 2, t_pos_dim_1_end - t_array_needle - 2);

                std::string array_rewrite = "(" + array_dim_0 + ") * the_width_" + arrayName + " + (" + array_dim_1 + ")";

                line = line.replace(t_pos_dim_0_start + 1, t_pos_dim_1_end - t_pos_dim_0_start - 1, array_rewrite);

                start_pos = t_pos_dim_1_end - t_pos_dim_0_start - 1;

                t_array_needle = line.find(s_array_needle, start_pos);
                replaced = true;
            }

            if (replaced) {
                outStream << "" << "// ### REPLACED 2D ACCESS: " << lineCopy << "\n";
            }

            // Add all opening brackets.
            int lastBracketCount = bracketCount;

            bracketCount += std::count(line.begin(), line.end(), '{');
            bracketCount -= std::count(line.begin(), line.end(), '}');

            bool switchKernelNonKernel = false;

            if (is_kernel_found && lastBracketCount != 0 && bracketCount == 0) {
                outStream << "" << line << "\n";
                is_kernel_found = false;
                switchKernelNonKernel = true;
            }

            if (is_kernel_found) {
                // Dont print empty lines.
                //         if(lineCopy2 != "") {
                outStream << "" << line << "\n";
                //         }
            }
            else {
                if (!switchKernelNonKernel) {
                    if (lineCopy2 != "") {
                        outStream << "// " << line << "\n";
                    }
                }
            }

            // ### Look for body of the kernel
            // Does the kernel body start in this line? We look for opening curly bracket.
            int t_curly = line.find("{");

            if (t_curly > -1) {
                in_kernel_function = true;
            }

            if (is_kernel_found && in_kernel_function && !array_width_defined) {
                outStream << "// ### ADDING GENERATED WIDTH. \n";

                for (auto rp: replace_params) {
                    outStream << "  long the_width_" << rp.first << " = " << rp.second << ";\n";
                }

                outStream << "\n";
                array_width_defined = true;
            }
        }
        myfile.close();
        outStream.close();
    }

    else
        cout << "Unable to open file";

    return;
}

/**
 *  Replace function call "llvm.sqrt.f64" to real sqrt() function.
 */
void axtorFixSqrt(std::string inFile, std::string outFile) {

    const std::string declaration_sqrt_wrong("llvm.sqrt.f64");
    const std::string declaration_sqrt_correct("sqrt");

    std::ifstream filein(inFile);       //File to read from
    std::ofstream fileout(outFile);     //Temporary file

    if (!filein || !fileout) {
        std::cout << "Error opening files!" << std::endl;
        return;
    }

    std::string readline;

    while (getline(filein, readline)) {
        std::string line = readline;

        // Idea: replace all wrong calls with correct calls.
        std::size_t found_pos = line.find(declaration_sqrt_wrong);

        if (found_pos != std::string::npos) {
            line = line.substr(0, found_pos) + declaration_sqrt_correct + line.substr(found_pos + declaration_sqrt_wrong.size());
        }

        fileout << line << std::endl;
    }

    filein.close();
    fileout.close();

    return;
}

void prepareSharedMemoryKernel(std::string inFile, std::string outFile) {
    string line;

    ofstream outStream(outFile);

    if (!outStream.is_open()) {
        cout << "Unable to open write file";
    }

    ifstream myfile(inFile);

    if (myfile.is_open()) {

        bool addSyncBeforeLoop = false;
        bool addSyncAfterLoop = false;
        int bracesCount = 0;

        bool printLine = true;

        while (getline(myfile, line)) {
            printLine = true;
            std::string lineCopy = line;

            // ### Adjust kernel function parameters.
            // three patterns

            //1. float array[32][32]; |-> found "][" but without "="  |-> add __local 
            //2. array[x][y] = something |-> "=" to the right of "][" |-> add synchronization before start of next loop
            //3. something = array[x][y] |-> "=" to the left of "]["  |-> add synchronization immediatly after loop

            int localVariable = lineCopy.find("][");

            if (localVariable > -1) {
                int equalLoc = lineCopy.find("=");

                if (equalLoc > -1) {
                    if (localVariable < equalLoc) {
                        addSyncBeforeLoop = true;
                    }
                    else {
                        addSyncAfterLoop = true;
                        bracesCount = 0;
                    }
                }
                else {
                    //Add local defn
                    outStream << "" << "// ### REPLACED : " << lineCopy << "\n";
                    outStream << "__local " << lineCopy << "\n";
                    printLine = false;
                }
            }
            else if (addSyncBeforeLoop) {
                int whileLoc = lineCopy.find("while");

                if (whileLoc > -1) {
                    outStream << "" << "// ### ADD CLK_LOCAL_MEM_FENCE\n";
                    outStream << "" << "barrier(CLK_LOCAL_MEM_FENCE);\n";
                    outStream << lineCopy << "\n";
                    addSyncBeforeLoop = false;
                    printLine = false;
                }
            }
            else if (addSyncAfterLoop) {

                int closeBraceLoc = lineCopy.find("}");

                if (closeBraceLoc > -1) {
                    if (bracesCount == 0) {

                        outStream << lineCopy << "\n";
                        outStream << "" << "// ### ADD CLK_LOCAL_MEM_FENCE\n";
                        outStream << "" << "barrier(CLK_LOCAL_MEM_FENCE);\n";
                        addSyncAfterLoop = false;
                        printLine = false;
                    }
                    else {
                        bracesCount--;
                    }
                }
                else {
                    int openBraceLoc = lineCopy.find("{");

                    if (openBraceLoc > -1) {
                        bracesCount++;
                    }
                }
            }

            if (printLine) {
                outStream << lineCopy << "\n";
            }

        }
        myfile.close();
        outStream.close();
    }

    else
        cout << "Unable to open file";
    return;
}

//Add the restrict keyword for all pointers 
std::string addRestrict(std::string inputData) {

    std::ostringstream outStream;
    auto lines = explodeStr(inputData, "\n");

    for (unsigned int lineIter = 0; lineIter < lines.size(); lineIter++) {
        std::string line = lines[lineIter];
        // Line has the kernel.
        if (line.find("__kernel") != std::string::npos) {
            std::vector < std::string > params = explodeStr(line, ",");

            //Resemble new function parameters.
            std::string new_line = "";

            for (auto param:params) {
                //Add a restrict keyword for pointers
                //We can safely do this because we perform alias analysis before
                auto t_param_last = param.find("* ");

                if (t_param_last != std::string::npos) {
                    param.replace(t_param_last, 2, "* restrict ");
                }
                new_line += param + ",";
            }
            line = new_line.substr(0, new_line.size() - 1);
        }
        outStream << "" << line << "\n";
    }
    return outStream.str();
}

std::string cleanKernel(std::string inputData, std::string kernelName) {
    std::ostringstream outStream;
    auto lines = explodeStr(inputData, "\n");

    bool foundKernel = false;
    int openBraces = 0;

    for (unsigned int lineIter = 0; lineIter < lines.size(); lineIter++) {
        std::string lineCopy = lines[lineIter];
        int t_func_needle = lineCopy.find("__kernel");

        // Line has the needle.
        if (t_func_needle > -1) {
            int t_k_needle = lineCopy.find(kernelName);

            if (t_func_needle > -1) {
                foundKernel = true;
                outStream << lineCopy << "\n";
                continue;
            }
        }

        if (foundKernel) {
            t_func_needle = lineCopy.find("{");
            if (t_func_needle > -1) {
                openBraces++;
            }
            else {
                t_func_needle = lineCopy.find("}");
                if (t_func_needle > -1) {
                    openBraces--;
                }
            }
            outStream << lineCopy << "\n";
            if (openBraces == 0)
                break;
        }
    }
    outStream << "\n";
    return outStream.str();
}

std::string remove_as_ubool(std::string inputData) {

    const std::string str_as_ubool("as_ubool");

    std::ostringstream outStream;
    auto lines = explodeStr(inputData, "\n");

    bool foundKernel = false;
    int openBraces = 0;

    for (unsigned int lineIter = 0; lineIter < lines.size(); lineIter++) {
        std::string line = lines[lineIter];

        // remove all calls.
        std::size_t found_pos = line.find(str_as_ubool);

        if (found_pos != std::string::npos) {
            line = line.substr(0, found_pos) + line.substr(found_pos + str_as_ubool.size());
        }
        outStream << line << "\n";
    }

    outStream << "\n";
    return outStream.str();
}
