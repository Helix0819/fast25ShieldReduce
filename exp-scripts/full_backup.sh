#!/bin/bash

echo "start dd"
dd if=/dev/mapper/loop0p1 of=./multi14/vm$1-$2
#./chunking tmp.img vm$1-$2
#./send_backup.sh $1 $2 &
#scp vm$1-$2 ldfs@192.168.118.15:~/data/2year/2year8/
#rm -rf vm$1-$2
    #/bin /initrd.img /lib64 /mnt /root /selinux /tmp /vmlinuz \
    #/boot /etc /lib /lost+found /opt /run /srv /usr \
    #/data /home /media /sbin /var \
