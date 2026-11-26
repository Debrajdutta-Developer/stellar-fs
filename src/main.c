#include "engine.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <create|write|read|delete> <image_file> [path] [data]\n", argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    const char *img = argv[2];

    sfs_disk_t disk;
    if (strcmp(cmd, "create") == 0) {
        if (sfs_disk_init(&disk, 2048) != SFS_OK) {
            fprintf(stderr, "Failed to initialize disk\n");
            return 1;
        }
        if (sfs_format(&disk) != SFS_OK) {
            fprintf(stderr, "Failed to format disk\n");
            sfs_disk_free(&disk);
            return 1;
        }
        FILE *f = fopen(img, "wb");
        if (!f) {
            fprintf(stderr, "Failed to open image file\n");
            sfs_disk_free(&disk);
            return 1;
        }
        fwrite(disk.data, 1, disk.size, f);
        fclose(f);
        sfs_disk_free(&disk);
        printf("Successfully created StellarFS image: %s\n", img);
        return 0;
    }

    FILE *f = fopen(img, "rb+");
    if (!f) {
        fprintf(stderr, "Failed to open existing image file\n");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sfs_disk_init(&disk, (uint32_t)(fsize / SFS_BLOCK_SIZE)) != SFS_OK) {
        fclose(f);
        return 1;
    }
    fread(disk.data, 1, fsize, f);

    sfs_superblock_t sb;
    if (sfs_mount(&disk, &sb) != SFS_OK) {
        fprintf(stderr, "Failed to mount filesystem\n");
        sfs_disk_free(&disk);
        fclose(f);
        return 1;
    }

    if (strcmp(cmd, "write") == 0 && argc >= 5) {
        const char *path = argv[3];
        const char *data = argv[4];
        if (sfs_write_file(&disk, &sb, path, data, strlen(data)) == SFS_OK) {
            fseek(f, 0, SEEK_SET);
            fwrite(disk.data, 1, disk.size, f);
            printf("Successfully wrote %s\n", path);
        } else {
            fprintf(stderr, "Write failed\n");
        }
    } else if (strcmp(cmd, "read") == 0 && argc >= 4) {
        const char *path = argv[3];
        char buf[4096];
        size_t read_bytes = 0;
        if (sfs_read_file(&disk, &sb, path, buf, sizeof(buf) - 1, &read_bytes) == SFS_OK) {
            buf[read_bytes] = '\0';
            printf("Read content: %s\n", buf);
        } else {
            fprintf(stderr, "Read failed\n");
        }
    } else if (strcmp(cmd, "delete") == 0 && argc >= 4) {
        const char *path = argv[3];
        if (sfs_delete_file(&disk, &sb, path) == SFS_OK) {
            fseek(f, 0, SEEK_SET);
            fwrite(disk.data, 1, disk.size, f);
            printf("Successfully deleted %s\n", path);
        } else {
            fprintf(stderr, "Delete failed\n");
        }
    }

    fclose(f);
    sfs_disk_free(&disk);
    return 0;
}
