#!/bin/sh
gcc -static -Wall -o libbtrfstrans libbtrfstrans.c read-file.c -L/home/ubuntu/524/btrfs_expr/test/btrfs-progs -lbtrfs -lpthread
