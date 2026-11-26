#include "../src/engine.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    sfs_disk_t disk;
    assert(sfs_disk_init(&disk, 256) == SFS_OK);
    assert(sfs_format(&disk) == SFS_OK);

    sfs_superblock_t sb;
    assert(sfs_mount(&disk, &sb) == SFS_OK);
    assert(sb.magic == SFS_MAGIC);
    assert(sb.total_blocks == 256);

    uint32_t b1 = 0;
    assert(sfs_allocate_block(&disk, &sb, &b1) == SFS_OK);
    assert(b1 >= 8);

    sfs_free_block(&disk, &sb, b1);
    assert(sb.free_blocks > 0);

    const char *test_path = "/hello.txt";
    const char *test_data = "StellarFS Engine Unit Test Data Content String!";
    assert(sfs_write_file(&disk, &sb, test_path, test_data, strlen(test_data)) == SFS_OK);

    char read_buf[256];
    size_t read_len = 0;
    assert(sfs_read_file(&disk, &sb, test_path, read_buf, sizeof(read_buf), &read_len) == SFS_OK);
    assert(read_len == strlen(test_data));
    assert(memcmp(read_buf, test_data, read_len) == 0);

    assert(sfs_delete_file(&disk, &sb, test_path) == SFS_OK);

    sfs_disk_free(&disk);
    printf("ALL ENGINE UNIT TESTS PASSED SUCCESSFULLY.\n");
    return 0;
}
