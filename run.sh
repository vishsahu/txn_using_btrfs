#!/bin/sh
gcc -static -Wall -o libbtrfstrans libbtrfstrans.c rw-file.c \
    -L/home/ubuntu/524/txn_btrfs/btrfs-progs -lbtrfs -lpthread
