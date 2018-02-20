#!/bin/sh

# Use this script for building on a Linux box, specifically
# SLC6. Tested with Linux Kernel 2.6.32 x86_64.
#
# Need libusb and libftdi installed. If like my test system, yum did
# not have a new enough libusb for access to the FTDI to work
# properly. Need at least libusb-1.0. So I had to install libusb
# myself:
#
# Can download from: http://libusb.info/ or
#   git clone https://github.com/libusb/libusb
#
# I needed libudev-devel, so before building libusb, do:
#
# sudo yum install libudev-devel
#
# cd into libusb top level folder (ewhatever its full name is) and then:
#
# ./configure && make
#
# If that seems to build correctly, install with:
#
# sudo make install
#
# Next, get libftdi. Use:
#
# mkdir libftdi
# cd libftdi
# git clone git://developer.intra2net.com/libftdi
#
# Can also download from: https://www.intra2net.com/en/developer/libftdi/download.php
#
# cd into the libftdi folder (may have version numbers in the folder name).
#
# open README.build and follow the instructions. Namely, install the
# build tools, but they should have been installed to even build
# libusb. Can ignore installation of sudo libconfuse-dev swig
# python-dev & libboost-all-dev. They are optional for this build and
# not needed here.
#
# Then, as README.build says:
#
# mkdir build
# cd build
# cmake  -DCMAKE_INSTALL_PREFIX="/usr" ../
# make
# sudo make install
#
#
# NOTE: Once have the executables built and running, may need to use
# the "-f" option to lower the frequency that Vivado picks.

echo "make USE_GETIFADDRS=eth0 USE_LIBFTDI1=true"
make USE_GETIFADDRS=eth0 USE_LIBFTDI1=true
