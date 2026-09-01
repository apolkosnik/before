/* External Data Representation */

#ifndef _XDR_H_
#define _XDR_H_

struct xdr_t {
    uint8_t* data;
    uint8_t* head;
    uint32_t size;
    uint32_t capacity;
};

uint32_t xdr_read_long(struct xdr_t* xdr);
int      xdr_read_string(struct xdr_t* xdr, char* str, uint32_t maxlen);
int      xdr_read_data(struct xdr_t* xdr, void* data, uint32_t len);
int      xdr_read_skip(struct xdr_t* xdr, uint32_t len);

void xdr_write_long(struct xdr_t* xdr, uint32_t val);
void xdr_write_string(struct xdr_t* xdr, const char* str, uint32_t maxlen);
void xdr_write_data(struct xdr_t* xdr, void* data, uint32_t len);
void xdr_write_skip(struct xdr_t* xdr, uint32_t len);

void xdr_write_zero(struct xdr_t* xdr, uint32_t len);
int  xdr_write_check(struct xdr_t* xdr, uint32_t len);

uint8_t* xdr_get_pointer(struct xdr_t* xdr);
void     xdr_write_long_at(uint8_t* data, uint32_t val);
uint32_t xdr_read_long_at(uint8_t* data);

struct xdr_t* xdr_init(void);
void xdr_uninit(struct xdr_t* xdr);

#endif /* _XDR_H_ */
