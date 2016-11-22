#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "libbtrfstrans.h"

#define MAX_PATH_LEN 500

void read_file(const char* path);
void write_file(const char* path);

int main(int argc, char *argv[]) {

    const char *top_dir = "/mnt/btrfs/orig"; // without '/' at last
    char snap_name[MAX_PATH_LEN];
    char temp_name[MAX_PATH_LEN];
    int ret = 0;

    ret = make_subvolume(top_dir);
    if (ret < 0) {
        printf("ERROR %s: Error in making %s subvolume.\n", __func__, top_dir);
        return -EINVAL;
    }

    ret = snprintf(snap_name, MAX_PATH_LEN, "%s_snap", top_dir);
    if (ret >= MAX_PATH_LEN) {
        printf("ERROR %s: Path longer than buffer, allocate larger buffer.\n",
            __func__);
        return -ENOMEM;
    }
    else if (ret < 0) {
        printf("ERROR %s: Error in writing string.\n", __func__);
        return -ENOMEM;
    }

    ret = create_snapshot(top_dir, snap_name, 0 /* writable */, 1);
    if (ret < 0) {
        printf("ERROR %s: Error in snapshotting %s subvolume.\n", __func__, top_dir);
        return -EINVAL;
    }

    printf("Snapshot created, do some work here\n");

    write_file("/mnt/btrfs/orig_snap/test_file");

    ret = snprintf(temp_name, MAX_PATH_LEN, "%s_temp", top_dir);
    if (ret >= MAX_PATH_LEN) {
        printf("ERROR %s: Path longer than buffer, allocate larger buffer.\n",
            __func__);
        return -ENOMEM;
    }
    else if (ret < 0) {
        printf("ERROR %s: Error in writing string.\n", __func__);
        return -ENOMEM;
    }

    // rename orig to temp
    ret = rename(top_dir, temp_name);
    if (ret < 0) {
        printf("ERROR %s: Error in renaming %s subvolume.\n", __func__, top_dir);
        return -EINVAL;
    }

    // rename snapshot to orig
    ret = rename(snap_name, top_dir);
    if (ret < 0) {
        printf("ERROR %s: Error in renaming %s subvolume.\n", __func__,
            snap_name);
        return -EINVAL;
    }

    // clean up the temporary copy
    ret = delete_subvolume(temp_name);
    if (ret < 0) {
        printf("ERROR %s: Error in cleaning up %s subvolume.\n", __func__,
            temp_name);
        return -1;
    }

    return ret;
}


void write_file(const char *path) {
    FILE *fp;
    const char *text = "this is random text.";
    fp = fopen(path, "w+");
    if (!fp) {
        printf("path could not be opened\n");
        return;
    }
    fputs(text, fp);
    fclose(fp);
}


void read_file(const char* path){
    FILE* fp;
    fp = fopen(path, "rt");
    char line[81];
    if (fp == NULL) {
        fprintf(stderr, "ERROR: couldn't open file named '%s' for reading it\n", path);
    }

    printf("Content of file named '%s' is:\n-----\n", path);
    while (fgets(line, 80, fp) != NULL) {
        printf("%s\n", line);
    }
    printf("-----\n");

    fclose(fp);
}

