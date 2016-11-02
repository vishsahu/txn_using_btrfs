#include <stdio.h>
#include <unistd.h>
#include "../libbtrfstrans/libbtrfstrans.h"

void read_file(const char* path);

int main(int argc, char *argv[]) {
    init_libbtrfstrans("/home/btrfs/Desktop/mounted");

    start_ro_transaction();

    if (argc != 2) {
        read_file("file");
    } else {
        read_file(argv[1]);
    }

    puts("Press enter to stop the read-only transaction.");
    getchar();

    stop_ro_transaction();

    return 0;
}

void read_file(const char* path){
    FILE* fp;
    fp = btrfstrans_fopen(path, "rt");
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

