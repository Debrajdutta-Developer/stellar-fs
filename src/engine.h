#ifndef STELLAR_FS_ENGINE_H
#define STELLAR_FS_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SFS_MAGIC 0x53544C5253465331ULL /* 'STLRFS1' */
#define SFS_BLOCK_SIZE 4096
#define SFS_SHA256_SIZE 32
#define SFS_MAX_PATH 256
#define SFS_MAX_FILE_BLOCKS 64

typedef enum {
    SFS_OK = 0,
    SFS_ERR_INVALID_ARGS = -1,
    SFS_ERR_OUT_OF_SPACE = -2,
    SFS_ERR_NOT_FOUND = -3,
    SFS_ERR_IO = -4,
    SFS_ERR_CORRUPT = -5,
    SFS_ERR_EXISTS = -6,
    SFS_ERR_FULL = -7
} sfs_status_t;

typedef struct {
    uint64_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t inode_root_block;
    uint32_t journal_start_block;
    uint32_t journal_block_count;
    uint32_t bitmap_block;
} sfs_superblock_t;

typedef struct {
    char path[SFS_MAX_PATH];
    uint64_t size;
    uint32_t block_count;
    uint32_t blocks[SFS_MAX_FILE_BLOCKS];
    uint8_t sha256[SFS_SHA256_SIZE];
    bool is_compressed;
} sfs_inode_t;

typedef struct {
    void *data;
    size_t size;
    uint32_t capacity;
} sfs_disk_t;

sfs_status_t sfs_disk_init(sfs_disk_t *disk, uint32_t total_blocks);
void sfs_disk_free(sfs_disk_t *disk);

sfs_status_t sfs_format(sfs_disk_t *disk);
sfs_status_t sfs_mount(sfs_disk_t *disk, sfs_superblock_t *sb);

sfs_status_t sfs_allocate_block(sfs_disk_t *disk, sfs_superblock_t *sb, uint32_t *block_out);
void sfs_free_block(sfs_disk_t *disk, sfs_superblock_t *sb, uint32_t block_id);

sfs_status_t sfs_write_file(sfs_disk_t *disk, sfs_superblock_t *sb, const char *path, const void *data, size_t size);
sfs_status_t sfs_read_file(sfs_disk_t *disk, sfs_superblock_t *sb, const char *path, void *buffer, size_t buffer_size, size_t *read_out);
sfs_status_t sfs_delete_file(sfs_disk_t *disk, sfs_superblock_t *sb, const char *path);

sfs_status_t sfs_btree_insert(sfs_disk_t *disk, sfs_superblock_t *sb, const char *path, uint32_t inode_block);
sfs_status_t sfs_btree_search(sfs_disk_t *disk, const sfs_superblock_t *sb, const char *path, uint32_t *inode_block_out);

sfs_status_t sfs_journal_commit(sfs_disk_t *disk, sfs_superblock_t *sb, uint32_t tx_id, const void *payload, size_t payload_len);
sfs_status_t sfs_journal_recover(sfs_disk_t *disk, sfs_superblock_t *sb);

size_t sfs_compress(const void *src, size_t src_len, void *dst, size_t dst_capacity);
size_t sfs_decompress(const void *src, size_t src_len, void *dst, size_t dst_capacity);
void sfs_sha256(const void *data, size_t len, uint8_t digest[SFS_SHA256_SIZE]);

#endif
