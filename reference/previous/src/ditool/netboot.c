/*
 *  netboot.c
 *  Previous
 *
 *  Created by Andreas Grabher on 25.07.2025.
 *
 *  Inspired by ditool.cpp by Simon Schubiger.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "netboot.h"
#include "ctl.h"
#include "rpc/vfs.h"


#define MAX_LINE_SIZE 128
#define GET_EXTRA(n,s) (2+(n)*((s)+1))

static void* read_file_to_buffer(struct vfs_t* ft, const char* path, size_t* size, size_t* maxsize, size_t extra, int silent) {
    struct path_t file_path;
    int err;
    struct stat fstat;
    void* buf = NULL;
    
    vfscpy(file_path.vfs, path, sizeof(file_path.vfs));
    vfs_to_host_path(ft, &file_path);
    
    err = vfs_stat(&file_path, &fstat);
    if (err) {
        if (!silent) printf("       ! cannot access '%s' (%s)\n", file_path.host, strerror(err));
    } else {
        size_t filesize = (size_t)fstat.st_size;
        if (filesize > (1024 * 1024)) {
            printf("       ! strange size of '%s' (%llu Byte)\n", file_path.host, (long long unsigned)filesize);
        } else {
            *size = filesize;
            *maxsize = filesize + extra;
            buf = calloc(1, *maxsize);
            if (filesize > 0) {
                uint32_t readsize = (uint32_t)filesize;
                err = vfs_read(&file_path, 0, buf, &readsize);
                if (err || (size_t)readsize != filesize || (extra && strlen(buf) != filesize)) {
                    const char* errstr = err ? strerror(err) : ((size_t)readsize != filesize ? "Short read" : "Invalid data");
                    printf("       ! cannot read '%s' (%s)\n", file_path.host, errstr);
                    free(buf);
                    buf = NULL;
                }
            }
        }
    }
    return buf;
}

static void write_buffer_to_file(struct vfs_t* ft, const char* path, void* buf, size_t size) {
    struct path_t file_path;
    int err;
    
    vfscpy(file_path.vfs, path, sizeof(file_path.vfs));
    vfs_to_host_path(ft, &file_path);
    
    err = vfs_create(&file_path, buf, (uint32_t)size);
    if (err) {
        printf("       ! cannot write '%s' (%s)\n", file_path.host, strerror(err));
    }
    free(buf);
}

static size_t add_line(char* data, size_t maxsize, const char* line) {
    size_t len    = strlen(line);
    size_t size   = strlen(data);
    size_t before = size;
    if (size > 0 && size + 1 < maxsize && data[size - 1] != '\n') {
        vfscpy(data + size, "\n", maxsize);
        size += 1;
    }
    if (size + len + 1 < maxsize) {
        printf("       - adding line '%s'\n", line);
        memcpy(data + size, line, len);
        size += len;
        vfscpy(data + size, "\n", maxsize);
        size += 1;
    } else {
        printf("       ! adding line '%s' failed\n", line);
    }
    return size - before;
}

static size_t remove_line(char* data, const char* line) {
    char* start;
    char* stop;
    size_t size = 0;
    while ((start = strstr(data, line))) {
        if (start > data && *(start - 1) != '\n') {
            data = strchr(start, '\n');
            if (data++) continue;
            break;
        }
        stop = strchr(start, '\n');
        if (stop++ == NULL) {
            printf("       - removing line '%s'\n", start);
            size += strlen(start);
            start[0] = '\0';
            break;
        }
        printf("       - removing line '%.*s'\n", (int)(stop - start - 1), start);
        memmove(start, stop, strlen(stop) + 1);
        size += stop - start;
        data = start;
    }
    return size;
}

static char* ip_addr_str(char* buf, size_t maxsize, uint32_t addr) {
    snprintf(buf, maxsize, "%d.%d.%d.%d", (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF);
    return buf;
}


static void netboot_link_file(struct vfs_t* ft, const char* to, const char* from) {
    int err = 0;
    struct path_t path_to;
    struct path_t path_from;
    
    vfscpy(path_from.vfs, from, sizeof(path_from.vfs));
    vfscpy(path_to.vfs, to, sizeof(path_to.vfs));
    vfs_to_host_path(ft, &path_from);
    vfs_to_host_path(ft, &path_to);
    
    if (vfs_access(&path_to, F_OK)) {
        printf("     - linking '%s' -> '%s'\n", to, from);
        if ((err = vfs_access(&path_from, F_OK))) {
            printf("       ! cannot access '%s' (%s)\n", path_from.host, strerror(err));
        } else if ((err = vfs_link(&path_from, &path_to, 0))) {
            printf("       ! cannot create '%s' (%s)\n", path_to.host, strerror(err));
        }
    }
}

static void netboot_make_resolv(struct vfs_t* ft, const char* file) {
    void* data;
    char ip_addr[32];
    char line[MAX_LINE_SIZE];
    size_t size    = 0;
    size_t maxsize = GET_EXTRA(2, sizeof(line));
    
    printf("     - writing '%s'\n", file);
    
    data = calloc(1, maxsize);
    if (data) {
        snprintf(line, sizeof(line), "domain %s", NAME_DOMAIN[0] == '.' ? &NAME_DOMAIN[1] : &NAME_DOMAIN[0]);
        size += add_line(data, maxsize, line);
        snprintf(line, sizeof(line), "nameserver %s", ip_addr_str(ip_addr, sizeof(ip_addr), CTL_NET | CTL_DNS));
        size += add_line(data, maxsize, line);
        write_buffer_to_file(ft, file, data, size);
    }
}

static void netboot_patch_hosts(struct vfs_t* ft, const char* file) {
    void* data;
    char ip_addr[32];
    char line[MAX_LINE_SIZE];
    size_t size    = 0;
    size_t maxsize = 0;
    size_t extra   = GET_EXTRA(2, sizeof(line));
    
    printf("     - patching '%s'\n", file);
    
    data = read_file_to_buffer(ft, file, &size, &maxsize, extra, 0);
    if (data) {
        size -= remove_line(data, ip_addr_str(ip_addr, sizeof(ip_addr), CTL_NET | CTL_HOST));
        size -= remove_line(data, ip_addr_str(ip_addr, sizeof(ip_addr), CTL_NET | CTL_NFSD));
        snprintf(line, sizeof(line), "%s\t%s", ip_addr_str(ip_addr, sizeof(ip_addr), CTL_NET | CTL_HOST), NAME_HOST);
        size += add_line(data, maxsize, line);
        snprintf(line, sizeof(line), "%s\t%s", ip_addr_str(ip_addr, sizeof(ip_addr), CTL_NET | CTL_NFSD), NAME_NFSD);
        size += add_line(data, maxsize, line);
        write_buffer_to_file(ft, file, data, size);
    }
}

static void netboot_patch_hostconfig(struct vfs_t* ft, const char* file, const char* template) {
    void* data;
    size_t size    = 0;
    size_t maxsize = 0;
    size_t extra   = GET_EXTRA(2, MAX_LINE_SIZE);
    
    printf("     - patching '%s'\n", file);
    
    data = read_file_to_buffer(ft, template, &size, &maxsize, extra, 1);
    if (data) {
        printf("       - using template '%s'\n", template);
    } else {
        data = read_file_to_buffer(ft, file, &size, &maxsize, extra, 0);
    }
    if (data) {
        size -= remove_line(data, "ROUTER");
        size -= remove_line(data, "IPNETMASK");
        size += add_line(data, maxsize, "ROUTER=-ROUTED-");
        size += add_line(data, maxsize, "IPNETMASK=-AUTOMATIC-");
        write_buffer_to_file(ft, file, data, size);
    }
}

static void netboot_patch_fstab(struct vfs_t* ft, const char* file, const char* template) {
    void* data;
    char line[MAX_LINE_SIZE];
    size_t size    = 0;
    size_t maxsize = 0;
    size_t extra   = GET_EXTRA(2, sizeof(line));
    
    printf("     - patching '%s'\n", file);
    
    data = read_file_to_buffer(ft, template, &size, &maxsize, extra, 1);
    if (data) {
        printf("       - using template '%s'\n", template);
        size -= remove_line(data, "SERVER");
        size -= remove_line(data, "/dev");
    } else {
        maxsize = extra;
        data = calloc(1, maxsize);
    }
    if (data) {
        snprintf(line, sizeof(line), "%s:/ / nfs rw,noauto 0 0", NAME_NFSD);
        size += add_line(data, maxsize, line);
        snprintf(line, sizeof(line), "%s:/private /private nfs rw,noauto 0 0", NAME_NFSD);
        size += add_line(data, maxsize, line);
        write_buffer_to_file(ft, file, data, size);
    }
}


void prepare_netboot(const char* path) {
    struct vfs_t* ft = vfs_init(path, "/");
    
    netboot_link_file(ft, "/private/tftpboot/mach", "/sdmach");
    netboot_link_file(ft, "/private/tftpboot/boot", "/usr/standalone/boot");
    netboot_make_resolv(ft, "/private/etc/resolv.conf");
    netboot_patch_hosts(ft, "/private/etc/hosts");
    netboot_patch_hostconfig(ft, "/private/etc/hostconfig", "/usr/template/client/etc/hostconfig");
    netboot_patch_fstab(ft, "/private/etc/fstab", "/usr/template/client/etc/fstab.client");
    
    vfs_uninit(ft);
}
