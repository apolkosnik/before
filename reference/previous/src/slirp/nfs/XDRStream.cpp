#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#ifdef _WIN32
#include <Winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <iostream>

#include "XDRStream.h"

using namespace std;

#define DBG 0

#define MAXDATA (1024 * 1024)

XDROpaque::XDROpaque() : m_size(0), m_data(NULL), m_deleteData(false) {}

XDROpaque::XDROpaque(size_t size) : m_size(size), m_data(new uint8_t[size]), m_deleteData(true) {}

XDROpaque::XDROpaque(const void* data, size_t size) : m_size(0), m_data(NULL), m_deleteData(false) {
    set(data, size);
}

XDROpaque::~XDROpaque() {
    if(m_deleteData) delete[] m_data;
}

void XDROpaque::set(const XDROpaque& src) {
    set(src.m_data, src.m_size);
}

void XDROpaque::resize(size_t size) {
    set(m_data, size);
}

void XDROpaque::set(const void* data, size_t size) {
    if(data == m_data && size <= m_size) {
        m_size = size;
    } else {
        uint8_t* toDelete = m_deleteData ? m_data : NULL;
        m_deleteData = true;
        m_size       = size;
        m_data       = new uint8_t[size];
        if(data) memcpy(m_data, data, size);
        delete[] toDelete;
    }
}

XDRString::XDRString(const string& str) : XDROpaque(str.c_str(), str.size()), m_str(NULL) {}

XDRString::XDRString(void) : XDROpaque(), m_str(NULL) {}

XDRString::~XDRString() {
    if(m_str) delete[] m_str;
}

const char* XDRString::c_str(void) {
    if(!(m_str)) {
        m_str = new char[m_size+1];
        memcpy(m_str, m_data, m_size);
        m_str[m_size] = '\0';
    }
    return m_str;
}

void XDRString::set(const void* data, size_t size) {
    XDROpaque::set(data, size);
    if(m_str) delete[ ] m_str;
    m_str = NULL;
}

void XDRString::set(const char* str) {
    set((uint8_t*)str, strlen(str) + 1);
}

ostream& operator<<(ostream& os, const XDRString& s)
{
    for(size_t i = 0; i < s.m_size; i++)
        os << static_cast<char>(s.m_data[i]);
    return os;
}

XDRStream::XDRStream(bool deleteBuffer, uint8_t* buffer, size_t capacity, size_t size) :
m_deleteBuffer(deleteBuffer),
m_buffer (buffer),
m_capacity(capacity),
m_size (size),
m_index (0) {}

size_t XDRStream::alignIndex(void) {
    m_index = (m_index + 3) & 0xFFFFFFFC;
    if (m_index > m_size) m_size = m_index;
    return m_index;
}
uint8_t* XDRStream::data(void) {
    return m_buffer;
}

void XDRStream::resize(size_t nSize) {
    m_index = 0;  //seek to the beginning of the input buffer
    m_size = nSize;
}

size_t XDRStream::getCapacity(void) {return m_capacity;}

size_t XDRStream::getPosition(void) {return m_index;}

size_t XDRStream::size(void) {return m_size;}

void XDRStream::reset(void) {m_index = m_size = 0;}

XDRInput::XDRInput()                           : XDRStream(true,  new uint8_t[MAXDATA], MAXDATA,       0)             {}
XDRInput::XDRInput(XDROpaque& opaque)          : XDRStream(false, opaque.m_data,        opaque.m_size, opaque.m_size) {}
XDRInput::XDRInput(uint8_t* data, size_t size) : XDRStream(false, data,                 size,          size)          {}

XDRInput::~XDRInput() {
    if(m_deleteBuffer)  delete[] m_buffer;
}

size_t XDRInput::read(void *pData, size_t nSize) {
	if (nSize > m_size - m_index)  //over the number of bytes of data in the input buffer
		nSize = m_size - m_index;
	memcpy(pData, m_buffer + m_index, nSize);
	m_index += nSize;
	return nSize;
}

size_t XDRInput::read(uint32_t* val) {
    uint32_t hval;
    size_t count = read(&hval, sizeof(uint32_t));
    *val         = ntohl(hval);
#if DBG
    cout << "read(" << *val << ")" << endl;
#endif
    return count;
}

size_t XDRInput::read(XDRString& string) {
    size_t result = read(static_cast<XDROpaque&>(string));
#if DBG
    cout << "read(\"" << string.c_str() << "\")" << endl;
#endif
    return result;
}

size_t XDRInput::read(XDROpaque& opaque) {
    read((uint32_t*)&opaque.m_size);
    if(opaque.m_deleteData) delete [] opaque.m_data;
    opaque.m_data = &m_buffer[m_index];
    if(skip(opaque.m_size) < opaque.m_size)
        throw __LINE__;
    alignIndex();
    return 4 + opaque.m_size;
}

size_t XDRInput::skip(ssize_t nSize)
{
	if (nSize > (ssize_t)(m_size - m_index))  //over the number of bytes of data in the input buffer
		nSize = m_size - m_index;
	m_index += nSize;
	return nSize;
}

bool XDRInput::hasData() {
    return m_index < m_size;
}

XDROutput::XDROutput() : XDRStream(true,  new uint8_t[MAXDATA], MAXDATA, 0) {}

XDROutput::~XDROutput() {
    if(m_deleteBuffer) delete[] m_buffer;
}

void XDROutput::write(void *pData, size_t nSize) {
	if (m_index + nSize > m_capacity)  //over the size of output buffer
		nSize = m_capacity - m_index;
	memcpy(m_buffer + m_index, pData, nSize);
	m_index += nSize;
	if (m_index > m_size)
		m_size = m_index;
}

void XDROutput::write(const XDROpaque& opaque) {
    write(opaque.m_size);
    write(opaque.m_data, opaque.m_size);
    alignIndex();
}

void XDROutput::write(const XDRString& string) {
    write(static_cast<const XDROpaque&>(string));
#if DBG
    cout << "write(\"" << string << "\")" << endl;
#endif
}

void XDROutput::write(const std::string& string) {
    XDRString str(string);
    write(str);
}

void XDROutput::write(uint32_t val) {
    uint32_t nval= htonl(val);
	write(&nval, sizeof(uint32_t));
#if DBG
    cout << "write(" << val << ")" << endl;
#endif
}

void XDROutput::seek(int nOffset, int nFrom) {
	if      (nFrom == SEEK_SET) m_index = nOffset;
	else if (nFrom == SEEK_CUR) m_index += nOffset;
	else if (nFrom == SEEK_END) m_index = m_size + nOffset;
}

void XDROutput::write(size_t maxLen, const char* format, ...) {
    va_list vargs;
    
    va_start(vargs, format);
    size_t len = vsnprintf((char*)&m_buffer[m_index+4], maxLen, format, vargs);
    write(len);
    va_end(vargs);
    m_index += len;
    alignIndex();
}

