#!/bin/sh
gcc -static -Wall -o libbtrfstrans libbtrfstrans.c read-file.c \
    -L/home/ubuntu/524/txn_btrfs/btrfs-progs -lbtrfs -lpthread
