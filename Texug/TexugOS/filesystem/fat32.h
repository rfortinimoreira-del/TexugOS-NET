#ifndef TEXUGOS_FAT32_H
#define TEXUGOS_FAT32_H

#include <stdint.h>

#define FAT32_ATTRIBUTE_DIRECTORY 0x10u

struct fat32_entry {
    char name[256];
    uint8_t attributes;
    uint32_t size;
    uint32_t first_cluster;
};

int fat32_mount(void);
uint32_t fat32_root_cluster(void);
int fat32_list(uint32_t directory, struct fat32_entry *entries,
               uint32_t capacity, uint32_t *count);
int fat32_find(uint32_t directory, const char *name,
               struct fat32_entry *entry);
int fat32_read_file(uint32_t directory, const char *name, void *buffer,
                    uint32_t capacity, uint32_t *size);
int fat32_write_file(uint32_t directory, const char *name, const void *buffer,
                     uint32_t size);
int fat32_create_directory(uint32_t parent, const char *name,
                           uint32_t *directory_cluster);
int fat32_delete(uint32_t directory, const char *name);

#endif
