 Use these instructions for building on a Linux PC, specifically
 SLC6. Tested with Linux Kernel 2.6.32 x86_64.

 Need libusb and libftdi installed. If like my test system, yum did
 not have a recent enough libusb for access to the FTDI to work
 properly. Need at least libusb-1.0. So I had to install libusb
 myself:

 Can download from: http://libusb.info/ or
   git clone https://github.com/libusb/libusb

 I needed libudev-devel to build libusb, so before building libusb, do:

 sudo yum install libudev-devel

 cd into libusb top level folder (whatever its full name is) and then:

 ./configure && make

 If that seems to build correctly, install with:

 sudo make install

 Next, get libftdi. Use:

 mkdir libftdi
 cd libftdi
 git clone git://developer.intra2net.com/libftdi

 Can also download from: https://www.intra2net.com/en/developer/libftdi/download.php

 cd into the libftdi folder (may have version numbers in the folder name).

 Open README.build and follow the instructions. Namely, install the
 build tools, but they should have been installed to even build
 libusb. Can ignore installation of sudo libconfuse-dev swig
 python-dev & libboost-all-dev. They are optional for libftdi and
 not needed here.

 Then, as README.build says:

 mkdir build
 cd build
 cmake  -DCMAKE_INSTALL_PREFIX="/usr" ../
 make
 sudo make install

 Before can use the FTDI device, may need to create a udev entry. See
 the udev instructions at:
 http://eblot.github.io/pyftdi/installation.html. For this slc6
 system, instead of setting the group to "plugdev", I set it to
 "dialup", which already existed as a group on this system. Also, I
 set the filename to 99-libftdi.rules so it was lower priority than
 existing udev rules for Xilinx and Digilent devices with the same
 vendor ID of 0x0403. Then unplug and plug the FTDI device in to have
 udev use this new rule.

 NOTE: Once have the executables built and running, may need to use
 the "-f" option to lower the frequency that Vivado picks.
