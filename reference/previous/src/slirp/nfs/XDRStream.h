#ifndef _XDRSTREAM_H_
#define _XDRSTREAM_H_

#include <string>
#include <stdio.h>
#include <stdint.h>
#include <ostream>

class XDROpaque {
public:
    size_t   m_size;
    uint8_t* m_data;
    bool     m_deleteData;
    
    XDROpaque();
    XDROpaque(size_t size);
    XDROpaque(const void* data, size_t size);
    virtual ~XDROpaque();
    
    void set(const XDROpaque& src);
    void resize(size_t size);
    virtual void set(const void* dataWillBeCopied, size_t size);
};

class XDRString : public XDROpaque {
    char* m_str;
public:
    XDRString(void);
    XDRString(const std::string& str);
    virtual ~XDRString();
    
    const char*  c_str(void);
    operator std::string() {return std::string(c_str());}
    virtual void set(const void* dataWillBeCopied, size_t size);
    void         set(const char* str);
    
    friend std::ostream& operator<<(std::ostream& os, const XDRString& s);
};

class XDRStream {
protected:
    bool     m_deleteBuffer;
    uint8_t* m_buffer;
    size_t   m_capacity;
    size_t   m_size;
    size_t   m_index;
    
    XDRStream(bool deleteBuffer, uint8_t* buffer, size_t capacity, size_t size);
    size_t   alignIndex(void);
public:
    size_t   size(void);
    uint8_t* data(void);
    void     resize(size_t nSize);
    size_t   getCapacity(void);
    size_t   getPosition(void);
    void     reset(void);
};

class XDROutput : public XDRStream {
public:
    XDROutput();
    ~XDROutput();
    void     write(void *pData, size_t nSize);
    void     write(uint32_t nValue);
    void     seek(int nOffset, int nFrom);
    void     write(const XDROpaque& opaque);
    void     write(const XDRString& string);
    void     write(const std::string& string);
    void     write(size_t maxLen, const char* format, ...);
};

class XDRInput : public XDRStream {
public:
    XDRInput();
    XDRInput(XDROpaque& opaque);
    XDRInput(uint8_t* data, size_t size);
    ~XDRInput();
    size_t   read(void *pData, size_t nSize);
    size_t   read(uint32_t* pnValue);
    size_t   read(XDROpaque& opaque);
    size_t   read(XDRString& string);
    size_t   skip(ssize_t nSize);
    bool     hasData(void);
};

#endif
