if [ $2x = x ] ;then
echo firm ready
else
scp test@192.168.0.200:~/firm/$2/eagle.app.$1.flash.bin .
fi
cat eagle.app.$1.flash.bin | ./ebin2c eagle_fw > eagle_fw.h
rm Makefile
ln -s Makefile.android Makefile
make clean && make
mv eagle.ko eagle_pad_$1
rm Makefile
ln -s Makefile.dongle Makefile
make clean && make
mv eagle.ko eagle_dongle_$1
if [ $2x = x ] ;then
echo no copy
else
scp eagle_pad_$1 test@192.168.0.200:~/firm/$2/eagle_pad_$1.ko
scp eagle_dongle_$1 test@192.168.0.200:~/firm/$2/eagle_dongle_$1.ko
fi
