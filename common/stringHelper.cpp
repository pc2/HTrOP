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

#include "stringHelper.h"

#include <list>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <utility>
#include <cassert>
#include <algorithm>

// substring of a string, starting at s and ending at e.
std::string substring(std::string const &str, std::string s, std::string e) {
    int start_pos = str.find(s);
    assert(start_pos > -1);
    start_pos += s.length();
    int end_pos = str.find(e, start_pos) - start_pos;
    std::string res = str.substr(start_pos, end_pos);
    return res;
}

// trim from start (in place)
void ltrim(std::string & s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun < int, int >(std::isspace))));
}

// trim from end (in place)
void rtrim(std::string & s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun < int, int >(std::isspace))).base(), s.end());
}

// trim from both ends (in place)
void trim(std::string & s) {
    ltrim(s);
    rtrim(s);
}

// trim from start (copying)
std::string ltrimmed(std::string s) {
    ltrim(s);
    return s;
}

// trim from end (copying)
std::string rtrimmed(std::string s) {
    rtrim(s);
    return s;
}

// trim from both ends (copying)
std::string trimmed(std::string s) {
    trim(s);
    return s;
}

// explode similar to php explode.
std::vector < std::string > explode(std::string const &s, char del) {
    std::vector < std::string > res;
    std::istringstream iss(s);

    for (std::string tok; std::getline(iss, tok, del);) {
        res.push_back(std::move(tok));
    }

    return res;
}

// explode similar to php explode (string version).
std::vector < std::string > explodeStr(const std::string &s, const std::string &del) {
    std::vector < std::string > ret;

    int s_length = s.length();
    int del_length = del.length();

    if (del_length == 0)
        return ret;

    int i = 0;
    int j = 0;

    while (i < s_length) {
        int runner = 0;

        while (i + runner < s_length && runner < del_length && s[i + runner] == del[runner]) {
            runner++;
        }
        if (runner == del_length) {
            ret.push_back(s.substr(j, i - j));
            i += del_length;
            j = i;
        } 
        else {
            i++;
        }
    }

    std::string tmps = s.substr(j, i - j);
    trim(tmps);

    ret.push_back(tmps);
    return ret;
}
