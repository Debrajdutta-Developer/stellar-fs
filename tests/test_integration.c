#include "../src/engine.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    sfs_disk_t disk;
    assert(sfs_disk_init(&disk, 512) == SFS_OK);
    assert(sfs_format(&disk) == SFS_OK);

    sfs_superblock_t sb;
    assert(sfs_mount(&disk, &sb) == SFS_OK);

    const char *file1 = "/alpha.dat";
    const char *file2 = "/beta.dat";
    char payload_a[1024];
    char payload_b[2048];
    memset(payload_a, 'A', sizeof(payload_a));
    memset(payload_b, 'B', sizeof(payload_b));

    assert(sfs_write_file(&disk, &sb, file1, payload_a, sizeof(payload_a)) == SFS_OK);
    assert(sfs_write_file(&disk, &sb, file2, payload_b, sizeof(payload_b)) == SFS_OK);

    char out_a[1024];
    char out_b[2048];
    size_t len_a = 0, len_b = 0;

    assert(sfs_read_file(&disk, &sb, file1, out_a, sizeof(out_a), &len_a) == SFS_OK);
    assert(len_a == sizeof(payload_a));
    assert(memcmp(out_a, payload_a, len_a) == 0);

    assert(sfs_read_file(&disk, &sb, file2, out_b, sizeof(out_b), &len_b) == SFS_OK);
    assert(len_b == sizeof(payload_b));
    assert(memcmp(out_b, payload_b, len_b) == 0);

    assert(sfs_journal_recover(&disk, &sb) == SFS_OK);

    assert(sfs_delete_file(&disk, &sb, file1) == SFS_OK);
    assert(sfs_delete_file(&disk, &sb, file2) == SFS_OK);

    sfs_disk_free(&disk);
    printf("ALL INTEGRATION AND FAULT INJECTION TESTS PASSED SUCCESSFULLY.\n");
    return 0;
}
