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

#ifndef MESSAGE_HELPER_INCLUDED
#define MESSAGE_HELPER_INCLUDED

#include<netdb.h>

/*
Message Structure
=================
|         Header        |                        Payload                        |
|MessageType|MessageSize|<------------------------data------------------------->|
|  int32_t  | uint32_t  |                   length = MessageSize                |
*/

class Message {
 private:
    int32_t type;
    char *payload;
    const char *const_payload;
    uint32_t size;

 public:
     Message();
     Message(void *message);
     Message(int32_t type, void *message, int32_t size);
     Message(int32_t type, const void *message, int32_t size);
    ~Message() {
    }
    //Setter methods
    void setType(int32_t type);
    void setSize(uint32_t size);
    void setMessageBuffer(void *message);
    void setSendMessageBuffer(const void *message);

    //Getter methods
    int32_t getType();
    uint32_t getSize();
    char *getMessageBuffer();
    const char *getSendMessageBuffer();

    //Methods
    int send(int _fd, int _flags);
    int send(int _fd, int32_t type, const void *_buf, size_t _n, int _flags);
    int recv(int _fd, int _flags);
    int recv(int _fd, void *_buf, int _flags);
};

#endif                          //MESSAGE_HELPER_INCLUDED
