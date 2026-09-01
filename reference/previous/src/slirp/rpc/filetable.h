/* File Table */

#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include "vfs.h"

#define HASH_BITS  8
#define HASH_SIZE  (1<<HASH_BITS)
#define HASH_MASK  (HASH_SIZE-1)

struct ft_entry_t {
    uint64_t fhandle;
    char* path;
    struct ft_entry_t* next;
};

struct ft_t {
    struct ft_entry_t* table[HASH_SIZE];
    struct vfs_t* vfs;
};

uint64_t ft_get_fhandle(struct ft_t* ft, const struct path_t* path);
void ft_set_sattr(struct ft_t* ft, struct path_t* path, struct sattr_t* sattr);
void ft_get_sattr(struct ft_t* ft, struct path_t* path, struct sattr_t* sattr);
int ft_get_canonical_path(struct ft_t* ft, uint64_t fhandle, char* vfs_path);

int ft_stat(struct ft_t* ft, const struct path_t* path, struct stat* fstat);
void ft_move(struct ft_t* ft, uint64_t fhandle_from, struct path_t* path_to);
void ft_remove(struct ft_t* ft, uint64_t fhandle);

int ft_is_inited(struct ft_t* ft);
int ft_path_changed(struct ft_t* ft, const char* host_path);

struct ft_t* ft_init(const char* host_path, const char* base_path_alias);
struct ft_t* ft_uninit(struct ft_t* ft);

#endif /* _FILETABLE_H_ */
