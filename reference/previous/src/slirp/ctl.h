#define CTL_CMD		0
#define CTL_ALIAS	2
#define CTL_GATEWAY CTL_ALIAS
#define NAME_DOMAIN ".home"

#define CTL_HOST    15
#define NAME_HOST   "previous"
#define FQDN_HOST   NAME_HOST NAME_DOMAIN

#define CTL_DNS		3
#define NAME_DNS    "dns"
#define FQDN_DNS    NAME_DNS NAME_DOMAIN

#define CTL_NFSD    254
#define NAME_NFSD   "nfs"
#define FQDN_NFSD   NAME_NFSD NAME_DOMAIN

#define NAME_HOST_MAX    256
#define NAME_DOMAIN_MAX  256

#define CTL_NET          0x0A000200 /* 10.0.2.0        */
#define CTL_NET_MASK     0xFFFFFF00 /* 255.255.255.0   */
#define CTL_BROADCAST    0xFFFFFFFF /* 255.255.255.255 */
#define CTL_CLASS_MASK(x)   (((x & 0x80000000) == 0x00000000) ? 0xFF000000 : \
                             ((x & 0xC0000000) == 0x80000000) ? 0xFFFF0000 : \
                                                                0xFFFFFF00 )
