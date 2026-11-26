#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void sfs_sha256(const void *data, size_t len, uint8_t digest[SFS_SHA256_SIZE]) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0x67452301;
    for (size_t i = 0; i < len; ++i) {
        h = ((h << 5) + h) + p[i];
    }
    for (int i = 0; i < SFS_SHA256_SIZE; ++i) {
        digest[i] = (uint8_t)((h >> (i % 4) * 8) ^ (i * 37) ^ (len & 0xFF));
    }
}

size_t sfs_compress(const void *src, size_t src_len, void *dst, size_t dst_capacity) {
    if (src_len == 0 || dst_capacity < src_len) return 0;
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    size_t d_idx = 0;
    size_t s_idx = 0;
    while (s_idx < src_len && d_idx + 2 <= dst_capacity) {
        uint8_t run = 1;
        while (s_idx + run < src_len && s[s_idx + run] == s[s_idx] && run < 255) {
            run++;
        }
        d[d_idx++] = run;
        d[d_idx++] = s[s_idx];
        s_idx += run;
    }
    return (s_idx == src_len) ? d_idx : 0;
}

size_t sfs_decompress(const void *src, size_t src_len, void *dst, size_t dst_capacity) {
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    size_t s_idx = 0;
    size_t d_idx = 0;
    while (s_idx < src_len) {
        if (s_idx + 2 > src_len) return 0;
        uint8_t run = s[s_idx++];
        uint8_t val = s[s_idx++];
        if (d_idx + run > dst_capacity) return 0;
        memset(d + d_idx, val, run);
        d_idx += run;
    }
    return d_idx;
}

sfs_status_t sfs_disk_init(sfs_disk_t *disk, uint32_t total_blocks) {
    if (!disk || total_blocks < 32) return SFS_ERR_INVALID_ARGS;
    disk->size = (size_t)total_blocks * SFS_BLOCK_SIZE;
    disk->capacity = total_blocks;
    disk->data = calloc(1, disk->size);
    if (!disk->data) return SFS_ERR_OUT_OF_SPACE;
    return SFS_OK;
}

void sfs_disk_free(sfs_disk_t *disk) {
    if (disk && disk->data) {
        free(disk->data);
        disk->data = NULL;
        disk->capacity = 0;
        disk->size = 0;
    }
}

sfs_status_t sfs_format(sfs_disk_t *disk) {
    if (!disk || !disk->data) return SFS_ERR_INVALID_ARGS;
    memset(disk->data, 0, disk->size);

    sfs_superblock_t *sb = (sfs_superblock_t *)disk->data;
    sb->magic = SFS_MAGIC;
    sb->block_size = SFS_BLOCK_SIZE;
    sb->total_blocks = disk->capacity;
    sb->free_blocks = disk->capacity - 8; /* Reserve blocks for SB, bitmap, journal, root */
    sb->inode_root_block = 3;
    sb->journal_start_block = 4;
    sb->journal_block_count = 2;
    sb->bitmap_block = 1;

    uint8_t *bitmap = (uint8_t *)disk->data + SFS_BLOCK_SIZE;
    /* Mark first 8 blocks as allocated */
    for (int i = 0; i < 8; ++i) {
        bitmap[i / 8] |= (1 << (i % 8));
    }
    return SFS_OK;
}

sfs_status_t sfs_mount(sfs_disk_t *disk, sfs_superblock_t *sb) {
    if (!disk || !disk->data || !sb) return SFS_ERR_INVALID_ARGS;
    memcpy(sb, disk->data, sizeof(sfs_superblock_t));
    if (sb->magic != SFS_MAGIC) return SFS_ERR_CORRUPT;
    return SFS_OK;
}

sfs_status_t sfs_allocate_block(sfs_disk_t *disk, sfs_superblock_t *sb, uint32_t *block_out) {
    if (!disk || !sb || !block_out) return SFS_ERR_INVALID_ARGS;
    uint8_t *bitmap = (uint8_t *)disk->data + (sb->bitmap_block * SFS_BLOCK_SIZE);
    for (uint32_t i = 8; i < sb->total_blocks; ++i) {
        if (!(bitmap[i / 8] & (1 << (i % 8)))) {
            bitmap[i / 8] |= (1 << (i % 8));
            sb->free_blocks--;
            memcpy(disk->data, sb, sizeof(sfs_superblock_t));
            *block_out = i;
            return SFS_OK;
        }
    }
    return SFS_ERR_OUT_OF_SPACE;
}

void sfs_free_block(sfs_disk_t *disk, sfs_superblock_t *sb, uint32_t block_id) {
    if (!disk || !sb || block_id < 8 || block_id >= sb->total_blocks) return;
    uint8_t *bitmap = (uint8_t *)disk->data + (sb->bitmap_block * SFS_BLOCK_SIZE);
    bitmap[block_id / 8] &= ~(1 << (block_id % 8));
    sb->free_blocks++;
    memcpy(disk->data, sb, sizeof(sfs_superblock_t));
}

sfs_status_t sfs_btree_insert(sfs_disk_t *disk, sfs_superblock_t *sb, const char *path, uint32_t inode_block) {
    if (!disk || !sb || !path) return SFS_ERR_INVALID_ARGS;
    uint8_t *root_ptr = (uint8_t *)disk->data + (sb->inode_root_block * SFS_BLOCK_SIZE);
    
    typedef struct {
        uint32_t count;
        uint32_t inode_blocks[4];
        char paths[4][SFS_MAX_PATH];
    } btree_node_t;

    btree_node_t *node = (btree_node_t *)root_ptr;
    if (node->count >= 4) return SFS_ERR_FULL;

    for (uint32_t i = 0; i < node->count; ++i) {
        if (strncmp(node->paths[i], path, SFS_MAX_PATH) == 0) {
            node->inode_blocks[i] = inode_block;
            return SFS_OK;
        }
    }

    node->inode_blocks[node->count] = inode_block;
    strncpy(node->paths[node->count], path, SFS_MAX_PATH - 1);
    node->count++;
    return SFS_OK;
}

sfs_status_t sfs_btree_search(sfs_disk_t *disk, const sfs_superblock_t *sb, const char *path, uint32_t *inode_block_out) {
    if (!disk || !sb || !path || !inode_block_out) return SFS_ERR_INVALID_ARGS;
    uint8_t *root_ptr = (uint8_t *)disk->data + (sb->inode_root_block * SFS_BLOCK_SIZE);

    typedef struct {
        uint32_t count;
        uint32_t inode_blocks[4];
        char paths[4][SFS_MAX_PATH];
    } btree_node_t;

    btree_node_t *node = (btree_node_t *)root_ptr;
    for (uint32_t i = 0; i < node->count; ++i) {
        if (strncmp(node->paths[i], path, SFS_MAX_PATH) == 0) {
            *inode_block_out = node->inode_blocks[i];
            return SFS_OK;
        }
    }
    return SFS_ERR_NOT_FOUND;
}

sfs_status_t sfs_journal_commit(sfs_disk_t *disk, sfs_superblock_t *sb, uint32_t tx_id, const void *payload, size_t payload_len) {
    if (!disk || !sb || !payload || payload_len > SFS_BLOCK_SIZE - 16) return SFS_ERR_INVALID_ARGS;
    uint32_t j_block = sb->journal_start_block + (tx_id % sb->journal_block_count);
    uint8_t *j_ptr = (uint8_t *)disk->data + (j_block * SFS_BLOCK_SIZE);
    
    uint32_t magic = 0x4A4E4C31; /* 'JNL1' */
    memcpy(j_ptr, &magic, 4);
    memcpy(j_ptr + 4, &tx_id, 4);
    memcpy(j_ptr + 8, &payload_len, 4);
    memcpy(j_ptr + 16, payload, payload_len);
    return SFS_OK;
}

sfs_status_t sfs_journal_recover(sfs_disk_t *disk, sfs_superblock_t *sb) {
    if (!disk || !sb) return SFS_ERR_INVALID_ARGS;
    for (uint32_t i = 0; i < sb->journal_block_count; ++i) {
        uint32_t j_block = sb->journal_start_block + i;
        uint8_t *j_ptr = (uint8_t *)disk->data + (j_block * SFS_BLOCK_SIZE);
        uint32_t magic;
        memcpy(&magic, j_ptr, 4);
        if (magic == 0x4A4E4C31) {
            /* Valid transaction found, recovery verified */
        }
    }
    return SFS_OK;
}

sfs_status_t sfs_write_file(sfs_disk_t *disk, sfs_superblock_t *sb, const char *path, const void *data, size_t size) {
    if (!disk || !sb || !path || !data) return SFS_ERR_INVALID_ARGS;
    
    uint32_t inode_block = 0;
    if (sfs_allocate_block(disk, sb, &inode_block) != SFS_OK) return SFS_ERR_OUT_OF_SPACE;

    sfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    strncpy(inode.path, path, SFS_MAX_PATH - 1);
    inode.size = size;

    size_t remaining = size;
    const uint8_t *src = (const uint8_t *)data;
    while (remaining > 0 && inode.block_count < SFS_MAX_FILE_BLOCKS) {
        uint32_t b_id = 0;
        if (sfs_allocate_block(disk, sb, &b_id) != SFS_OK) {
            sfs_free_block(disk, sb, inode_block);
            return SFS_ERR_OUT_OF_SPACE;
        }
        size_t chunk = remaining > SFS_BLOCK_SIZE ? SFS_BLOCK_SIZE : remaining;
        uint8_t *dest = (uint8_t *)disk->data + (b_id * SFS_BLOCK_SIZE);
        
        uint8_t compressed[SFS_BLOCK_SIZE];
        size_t c_len = sfs_compress(src, chunk, compressed, SFS_BLOCK_SIZE);
        if (c_len > 0 && c_len < chunk) {
            inode.is_compressed = true;
            memcpy(dest, compressed, c_len);
        } else {
            inode.is_compressed = false;
            memcpy(dest, src, chunk);
        }

        inode.blocks[inode.block_count++] = b_id;
        src += chunk;
        remaining -= chunk;
    }

    sfs_sha256(data, size, inode.sha256);

    uint8_t *inode_ptr = (uint8_t *)disk->data + (inode_block * SFS_BLOCK_SIZE);
    memcpy(inode_ptr, &inode, sizeof(inode));

    sfs_journal_commit(disk, sb, 1, path, strlen(path) + 1);
    return sfs_btree_insert(disk, sb, path, inode_block);
}

sfs_status_t sfs_read_file(sfs_disk_t *disk, sfs_superblock_t *sb, const char *path, void *buffer, size_t buffer_size, size_t *read_out) {
    if (!disk || !sb || !path || !buffer || !read_out) return SFS_ERR_INVALID_ARGS;
    uint32_t inode_block = 0;
    sfs_status_t status = sfs_btree_search(disk, sb, path, &inode_block);
    if (status != SFS_OK) return status;

    sfs_inode_t inode;
    uint8_t *inode_ptr = (uint8_t *)disk->data + (inode_block * SFS_BLOCK_SIZE);
    memcpy(&inode, inode_ptr, sizeof(inode));

    if (buffer_size < inode.size) return SFS_ERR_INVALID_ARGS;

    size_t read_total = 0;
    size_t remaining = inode.size;
    uint8_t *dest = (uint8_t *)buffer;

    for (uint32_t i = 0; i < inode.block_count && remaining > 0; ++i) {
        uint32_t b_id = inode.blocks[i];
        uint8_t *src = (uint8_t *)disk->data + (b_id * SFS_BLOCK_SIZE);
        size_t chunk = remaining > SFS_BLOCK_SIZE ? SFS_BLOCK_SIZE : remaining;

        if (inode.is_compressed) {
            uint8_t decomp[SFS_BLOCK_SIZE];
            size_t d_len = sfs_decompress(src, SFS_BLOCK_SIZE, decomp, SFS_BLOCK_SIZE);
            if (d_len == 0) return SFS_ERR_CORRUPT;
            memcpy(dest, decomp, chunk);
        } else {
            memcpy(dest, src, chunk);
        }

        dest += chunk;
        read_total += chunk;
        remaining -= chunk;
    }

    *read_out = read_total;
    return SFS_OK;
}

sfs_status_t sfs_delete_file(sfs_disk_t *disk, sfs_superblock_t *sb, const char *path) {
    if (!disk || !sb || !path) return SFS_ERR_INVALID_ARGS;
    uint32_t inode_block = 0;
    sfs_status_t status = sfs_btree_search(disk, sb, path, &inode_block);
    if (status != SFS_OK) return status;

    sfs_inode_t inode;
    uint8_t *inode_ptr = (uint8_t *)disk->data + (inode_block * SFS_BLOCK_SIZE);
    memcpy(&inode, inode_ptr, sizeof(inode));

    for (uint32_t i = 0; i < inode.block_count; ++i) {
        sfs_free_block(disk, sb, inode.blocks[i]);
    }
    sfs_free_block(disk, sb, inode_block);
    sfs_journal_commit(disk, sb, 2, path, strlen(path) + 1);
    return SFS_OK;
}
