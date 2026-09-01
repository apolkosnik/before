//
//  compat.c
//  Previous
//
//  Created by Simon Schubiger on 20.02.19.
//

#include "compat.h"

#ifndef _WIN32

char* strcpy_s(char * dst, size_t maxLen, const char * src) {
    return strncpy(dst, src, maxLen);
}

char* strncpy_s(char * dst, size_t maxLen, const char * src, size_t len) {
    return strncpy(dst, src, std::min(maxLen, len));
}

char* strcat_s(char* s1, const char* s2) {
    return strcat(s1, s2);
}

int strcat_s(char* s1, size_t maxLen, const char* s2) {
    strncat(s1, s2, maxLen);
    return 0;
}

#endif
