#include <stdio.h>
#include <unistd.h>
#include "../libbtrfstrans/libbtrfstrans.h"

void write_file(const char* path, const char* content);

int main(int argc, char* argv[]) {
    init_libbtrfstrans("/home/btrfs/Desktop/mounted");

    start_transaction();

    if (argc < 2 || argc > 3) {
        write_file("file", "some text");
    } else if (argc == 2) {
        write_file(argv[1], "some text");
    } else if (argc == 3) {
        write_file(argv[1], argv[2]);
    }

    puts("File was created. Press enter to commit the transaction.");
    getchar();

    commit_transaction();

    return 0;
}

void write_file(const char* path, const char* content){
    FILE* fp;
    fp = btrfstrans_fopen(path, "w");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: couldn't open file %s\n", path);
    }
    fputs(content, fp);
    fclose(fp);
}

