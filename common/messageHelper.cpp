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

#include<stdint.h>
#include<arpa/inet.h>
#include<stdlib.h>
#include<string>
#include"messageHelper.h"

Message::Message() {
    type = 0;
    size = 0;
    payload = NULL;
    const_payload = NULL;
}

//Initialize the message with a message buffer 
Message::Message(void *message) {
    type = 0;
    size = 0;
    payload = (char *)message;
    const_payload = static_cast < const char *>(message);
}

//Initialize the message with the message type, buffer and size
Message::Message(int32_t type, void *message, int32_t size) {
    this->type = type;
    this->size = size;
    payload = (char *)message;
    const_payload = static_cast < const char *>(message);
}

//Initialize message with a message type, const message and size
//NOTE:  Use this if you need to send const message
//       manually set the message buffer to enable recieve
Message::Message(int32_t type, const void *message, int32_t size) {
    this->type = type;
    this->size = size;
    payload = NULL;
    const_payload = (const char *)message;
}

void Message::setType(int32_t type) {
    this->type = type;
}

void Message::setSize(uint32_t size) {
    this->size = size;
}

//Use this to set the send/recieve message buffer
void Message::setMessageBuffer(void *message) {
    payload = (char *)message;
}

//Use this to send constant messages
//NOTE: the sed/recieve buffer is not modified
void Message::setSendMessageBuffer(const void *message) {
    const_payload = (const char *)message;
}

int32_t Message::getType() {
    return type;
}

uint32_t Message::getSize() {
    return size;
}

char *Message::getMessageBuffer() {
    return payload;
}

const char *Message::getSendMessageBuffer() {
    return const_payload;
}

int Message::send(int _fd, int32_t type, const void *_buf, size_t _n, int _flags) {
    this->type = type;
    const_payload = (const char *)_buf;
    size = _n;
    return send(_fd, _flags);

}

int Message::send(int _fd, int _flags) {
    if (const_payload == NULL && size > 0)
        return -2;

    //send a header
    uint32_t header_size = 2 * sizeof(int32_t);

    char *header = (char *)malloc(header_size);
    int32_t *header_int32_t = (int32_t *) header;

    *header_int32_t = htonl(type);
    *++header_int32_t = htonl(size);

    //send the header
    int n =::send(_fd, static_cast < const char *>(header), header_size, _flags);
    int total = 0;              // how many bytes we've sent
    int bytesleft = size;       // how many we have left to send

    if (size > 0)
        while (total < size) {
            n =::send(_fd, const_payload + total, bytesleft, _flags);
            if (n == -1) {
                break;
            }
            total += n;
            bytesleft -= n;
        }

    delete header;

    return n == -1 ? -1 : 0;    // return -1 on failure, 0 on success
}

int Message::recv(int _fd, void *_buf, int _flags) {
    payload = (char *)_buf;
    return recv(_fd, _flags);
}

int Message::recv(int _fd, int _flags) {
    if (payload == NULL)
        return -2;

    //Get the Header  
    uint32_t header_size = 2 * sizeof(int32_t);
    char *header = (char *)malloc(header_size);
    int recv_size =::recv(_fd, header, header_size, _flags);

    if (recv_size < 1) {
        type = -1;
        return size = recv_size;
    }

    int32_t *header_uint32_t = (int32_t *) header;

    type = ntohl(*header_uint32_t);
    size = ntohl(*(uint32_t *) (++header_uint32_t));
    delete header;

    if (size == 0)
        return 0;

    recv_size = 0;
    //Get the Body
    while (recv_size < size) {
        int recv_size_tmp =::recv(_fd, (static_cast < char *>(payload) + recv_size), size - recv_size, _flags);

        if (recv_size_tmp < 1) {
            type = -1;
            break;
        }
        recv_size += recv_size_tmp;
    }
    return recv_size;
}
