/*
 * External Data Representation
 * 
 * Copyright (c) 2025 Andreas Grabher
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <slirp.h>
#include <stdlib.h>

#include "xdr.h"

#define MAXDATA (1024 * 1024)

struct xdr_t* xdr_init(void) {
    struct xdr_t* xdr = (struct xdr_t*)malloc(sizeof(struct xdr_t));
    xdr->data = (uint8_t*)malloc(MAXDATA);
    xdr->head = xdr->data;
    xdr->size = 0;
    xdr->capacity = MAXDATA;
    return xdr;
}

void xdr_uninit(struct xdr_t* xdr) {
    free(xdr->head);
    free(xdr);
}

static void xdr_read_align(struct xdr_t* xdr) {
    while (xdr->size & 3) {
        xdr->data++;
        xdr->size--;
    }
}

static void xdr_write_align(struct xdr_t* xdr) {
    while (xdr->size & 3) {
        *(uint8_t*)xdr->data = 0;
        xdr->data++;
        xdr->size++;
    }
}

uint32_t xdr_read_long(struct xdr_t* xdr) {
    uint32_t val = 0;
    if (xdr->size < 4) {
        printf("[XDR] Error: Read long underrun\n");
    } else {
        val = ntohl(*(uint32_t*)xdr->data);
        xdr->data += 4;
        xdr->size -= 4;
    }
    return val;
}

void xdr_write_long(struct xdr_t* xdr, uint32_t val) {
    if (xdr->capacity - xdr->size < 4) {
        printf("[XDR] Error: Write long overflow\n");
    } else {
        *(uint32_t*)xdr->data = htonl(val);
        xdr->data += 4;
        xdr->size += 4;
    }
}

int xdr_read_string(struct xdr_t* xdr, char* str, uint32_t maxlen) {
    uint32_t len;
    if (xdr->size < 4) {
        printf("[XDR] Error: Read string underrun 1\n");
        return -1;
    }
    len = xdr_read_long(xdr);
    if (xdr->size < len) {
        printf("[XDR] Error: Read string underrun 2\n");
        return -1;
    }
    if (len < maxlen) {
        memcpy(str, xdr->data, len);
        str[len] = '\0';
    } else if (maxlen > 0) {
        printf("[XDR] Error: Read string truncated\n");
        memcpy(str, xdr->data, maxlen - 1);
        str[maxlen - 1] = '\0';
    } else {
        printf("[XDR] Error: Read string without buffer\n");
    }
    xdr->data += len;
    xdr->size -= len;
    xdr_read_align(xdr);
    return len;
}

void xdr_write_string(struct xdr_t* xdr, const char* str, uint32_t maxlen) {
    uint32_t len;
    if (xdr->capacity - xdr->size < 4) {
        printf("[XDR] Error: Write string overflow 1\n");
        return;
    }
    len = (uint32_t)strlen(str);
    xdr_write_long(xdr, len);
    if (xdr->capacity - xdr->size < len) {
        printf("[XDR] Error: Write string overflow 2\n");
        return;
    }
    if (len > maxlen) {
        printf("[XDR] Error: Write string truncated\n");
        memcpy(xdr->data, str, maxlen);
    } else {
        memcpy(xdr->data, str, len);
    }
    xdr->data += len;
    xdr->size += len;
    xdr_write_align(xdr);
}

int xdr_read_data(struct xdr_t* xdr, void* data, uint32_t len) {
    if (xdr->size < len) {
        printf("[XDR] Error: Read data underrun\n");
        return -1;
    } else if (len > 0) {
        memcpy(data, xdr->data, len);
        xdr->data += len;
        xdr->size -= len;
        xdr_read_align(xdr);
    }
    return 0;
}

void xdr_write_data(struct xdr_t* xdr, void* data, uint32_t len) {
    if (xdr->capacity - xdr->size < len) {
        printf("[XDR] Error: Write data overflow\n");
    } else if (len > 0) {
        memcpy(xdr->data, data, len);
        xdr->data += len;
        xdr->size += len;
        xdr_write_align(xdr);
    }
}

int xdr_read_skip(struct xdr_t* xdr, uint32_t len) {
    if (xdr->size < len) {
        printf("[XDR] Error: Read skip underrun\n");
        return -1;
    } else if (len > 0) {
        xdr->data += len;
        xdr->size -= len;
        xdr_read_align(xdr);
    }
    return 0;
}

void xdr_write_skip(struct xdr_t* xdr, uint32_t len) {
    if (xdr->capacity - xdr->size < len) {
        printf("[XDR] Error: Write skip overflow\n");
    } else if (len > 0) {
        xdr->data += len;
        xdr->size += len;
        xdr_write_align(xdr);
    }
}

void xdr_write_zero(struct xdr_t* xdr, uint32_t len) {
    if (xdr->capacity - xdr->size < len) {
        printf("[XDR] Error: Write zero overflow\n");
    } else if (len > 0) {
        memset(xdr->data, 0, len);
        xdr->data += len;
        xdr->size += len;
        xdr_write_align(xdr);
    }
}

int xdr_write_check(struct xdr_t* xdr, uint32_t len) {
    if (xdr->capacity - xdr->size < len) {
        printf("[XDR] Error: Write check overflow\n");
        return -1;
    }
    return 0;
}

uint8_t* xdr_get_pointer(struct xdr_t* xdr) {
    return (uint8_t*)xdr->data;
}

void xdr_write_long_at(uint8_t* data, uint32_t val) {
    *(uint32_t*)data = htonl(val);
}

uint32_t xdr_read_long_at(uint8_t* data) {
    return ntohl(*(uint32_t*)data);
}
