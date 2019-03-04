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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <cctype>
#include <iterator>

// Parsing helper.
class ScopExp {
public:
    virtual void dbg() { }
    virtual long eval() { }
    virtual void cln() { }
};

// Terminate expression.
class TermExp:public ScopExp {
    std::string s;
    long v = 0;
public:
    TermExp(std::string str_v):s(str_v) {
        try {
            v = std::stol(str_v);
        } catch(const std::invalid_argument & ia) {
            std::cerr << "Error ScopExp:TermExp\n";
        }
    }
    
    void dbg() {
        std::cout << ' ' << s << ' ';
    }
    
    long eval() {
        return v;
    }
    
    void cln() {
    }
};

// ASTree Node.
class NodeExp:public ScopExp {
    ScopExp *leftExp;
    ScopExp *rightExp;
    char op;
public:
    // Const.
    NodeExp(char op, ScopExp * leftT, ScopExp * rightT):op(op), leftExp(leftT), rightExp(rightT) {
    } 
    // Deconst.
    ~NodeExp() {
    }
    
    void dbg() {
        std::cout << '[' << op << ' ';
        leftExp->dbg();
        rightExp->dbg();
        std::cout << ']';
    }
    
    long eval() {
        if(op == '+') {
            return leftExp->eval() + rightExp->eval();
        } else if(op == '-') {
            return leftExp->eval() - rightExp->eval();
        } else if(op == '*') {
            return leftExp->eval() * rightExp->eval();
        } else if(op == '/') {
            return leftExp->eval() / rightExp->eval();
        } else {
            // Error.
            std::cerr << "Invalid Operator ScopExp:Node >> \"" << op << "\"\n";
            return 0;
        }
    }
    
    void cln() {
        leftExp->cln();
        rightExp->cln();
        delete leftExp;
        delete rightExp;
    }
};

/**
 * @brief Gets mathematical expressions _without_ variables and parses it into pre-fix AST tree.
 * 
 * @param str the expression to parse, i.e. "2 + 3840(-1 + (720)) + 3(-1 + (1280)) + 1"
 * @return ScopExp* the AST
 */
ScopExp *createASTfromScopString(std::string & str);

