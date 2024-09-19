#!/bin/bash

vmdir="/mnt/image/vm"
curdir="/mnt/image"
# hasher="/home/ncsgroup/jingwei/distribattack/util/fs-hasher/fs-hasher"
a=0.3
b=0.3
M=10000
vers=1
users=14

echo "Creating directories and symbolic links..."
sudo mkdir -p $vmdir/root
sudo ln -sf $vmdir/root synthetic_fs

for((i=1;i<=${users};i++)); do
    cd $vmdir 

    # clean
    echo "Cleaning up previous mounts and loop devices..."
    if sudo mount | grep $vmdir/root > /dev/null; then
        sudo umount root
    fi
    if sudo losetup -a | grep /dev/loop0 > /dev/null; then 
        sudo losetup -d /dev/loop0 	# delete loop device
        sudo kpartx -d /dev/loop0	# delete partition mapping
    fi
    sudo rm -f $curdir/vmroot.raw

    echo "Converting qcow2 image to raw..."
    sudo qemu-img convert -f qcow2 CentOS-7-x86_64-Azure-1704.qcow2 -O raw $curdir/vmroot.raw
    echo "Setting up loop device..."
    sudo losetup /dev/loop0 $curdir/vmroot.raw 	# setup vmroot
    sudo kpartx -a /dev/loop0	# add partition mapping
    sleep 1s
    echo "Mounting root filesystem..."
    sudo mount /dev/mapper/loop0p1 root	# mount

    cd $curdir
    for((j=1;j<=${vers};j++)); do
        echo "Running rrandomio.py for user $i, version $j..."
        sudo python2 rrandomio.py 'synthetic_fs/' $a $b $M
        echo "Running full_backup.sh for user $i, version $j..."
        sudo bash ./full_backup.sh $i $j
        # $hasher -c variable -C algo=rabin -h md5-48bit -z none -p ./vm$i-$j -o ./vm$i-$j.hash
        # rm -f vm$i-$j
    done
done

# clean
echo "Final cleanup..."
cd $vmdir 
if sudo mount | grep $vmdir/root > /dev/null; then
    sudo umount root
fi
if sudo losetup -a | grep /dev/loop0 > /dev/null; then 
    sudo losetup -d /dev/loop0 	# delete loop device
    sudo kpartx -d /dev/loop0	# delete partition mapping
fi
rm -f $curdir/vmroot.raw
rm -rf synthetic_fs
rm -rf $vmdir/root

echo "Script completed."