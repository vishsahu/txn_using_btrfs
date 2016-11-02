#!/bin/bash

base_dir=/home/btrfs/Desktop
global_dir=mounted
img_name=img
operation=$1
disksize=50M
img_path=$base_dir/$img_name
global_path=$base_dir/$global_dir

sem_lock_name=/dev/shm/sem.libbtrfstranssemaphorelock
sem_ro_name=/dev/shm/sem.libbtrfstranssemaphoreread
sem_rename_name=/dev/shm/sem.libbtrfstranssemaphorerename

function delete_semaphores {
    sudo rm -f $sem_lock_name
        sudo rm -f $sem_ro_name
        sudo rm -f $sem_rename_name
}

if [[ -z $operation ]]
then
echo "ERROR: you need to call this script with: $0 operation"
echo "valid operations are"
echo " 'create' : create the btrfs volume"
echo " 'remove' : remove the btrfs volume"
echo " 'unlock' : unlock semaphore (to be used after a crash)"
fi

if [[ $operation = "create" ]]
then
if [[ ! -e $global_path ]];
then
mkdir $global_path
dd if=/dev/zero of=$img_path bs=$disksize count=1 2> /dev/null
mkfs.btrfs -m single $img_path > /dev/null
sudo mount -o loop $img_path $global_path
sudo chown -R btrfs:btrfs $global_path
else
echo "ERROR: $global_path already exists. Aborting."
exit 1
fi
fi


if [[ $operation = "remove" ]]
then
if [[ -e $global_path ]]
then
sudo umount $global_path 2> /dev/null
rmdir $global_path
fi

if [[ -e $img_path ]]
then
rm $img_path
fi

delete_semaphores
fi


if [[ $operation = "unlock" ]]
then
delete_semaphores
fi
