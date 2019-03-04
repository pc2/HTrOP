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

#ifndef DATATRANSFERTYPE_INCLUDED
#define DATATRANSFERTYPE_INCLUDED

typedef enum {
    UNKNOWN,                    // Default.
    IN,                         // Needs to be transfered to accelerator.
    OUT,                        // Needs to be transfered from accelerator.
    IN_OUT,                     // Needs to be transfered to and from accelerator.
    TMP                         // Does not need to be transfered.
} DataTransferType;

typedef enum {
    LEG,
    MCPU,
    GPU,
    MIC
} DeviceType;

#endif                          //DATATRANSFERTYPE_INCLUDED
