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

#ifndef MATH_PARSER_H
#define MATH_PARSER_H

#include "math_parser.h"

/**
 * @brief Gets mathematical expressions _without_ variables and parses it into pre-fix AST tree.
 * 
 * @param s the expression to parse, i.e. "2 + 3840(-1 + (720)) + 3(-1 + (1280)) + 1"
 * @return ScopExp* the AST
 */
ScopExp *createASTfromScopString(std::string &s) {
    s.erase(remove_if(s.begin(), s.end(),::isspace), s.end());
    int lvl = 0;
    
    for (int i = s.size() - 1; i >= 0; --i) {
        char c = s[i];
        
        if (c == ')') {
            ++lvl;
            continue;
        }
        if (c == '(') {
            --lvl;
            continue;
        }
        if (lvl > 0)
            continue;
        
        if ((c == '+' || c == '-') && i != 0) {
            std::string leftS(s.substr(0, i));
            std::string rightS(s.substr(i + 1));
            return new NodeExp(c, createASTfromScopString(leftS), createASTfromScopString(rightS));
        }
    }
    
    bool braket_mul = false;
    for (int i = s.size() - 1; i >= 0; --i) {
        char c = s[i];
        
        if (c == ')') {
            ++lvl;
            continue;
        }
        if (c == '(') {
            --lvl;
            if (lvl == 0) {
                if ((i - 1) >= 0) {
                    char c_lookahead = s[i - 1];
                    if (c_lookahead != '+' && c_lookahead != '-' && c_lookahead != '*' && c_lookahead != '/') {
                        braket_mul = true;
                    }
                }
            }
            continue;
        }
        if (lvl > 0)
            continue;
        
        if (c == '*' || c == '/' || braket_mul) {
            int start_pos = i;
            int end_pos = i;
            
            if (braket_mul) {
                start_pos = i + 1;
                end_pos = i;
                c = '*';
                braket_mul = false;
            }
            
            std::string leftS(s.substr(0, start_pos));
            std::string rightS(s.substr(end_pos + 1));
            
            return new NodeExp(c, createASTfromScopString(leftS), createASTfromScopString(rightS));
        }
    }
    
    if (s[0] == '(') {
        for (int i = 0; i < s.size(); ++i) {
            if (s[i] == '(') {
                ++lvl;
                continue;
            }
            if (s[i] == ')') {
                --lvl;
                if (lvl == 0) {
                    std::string expS(s.substr(1, i - 1));
                    return createASTfromScopString(expS);
                }
                continue;
            }
        }
    }
    else {
        return new TermExp(s);
    }
    return NULL;
}

#endif                          //MATH_PARSER_H

