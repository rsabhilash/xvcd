#!/bin/sh

# Use this script for building on a Raspberry Pi (even the Zero)
# running Raspbian. Tested with Raspbian 2017-11-29, Linux Kernel
# 4.9.59+.
#
# Need libusb and libftdi installed through apt-get before running
# this shell script. An example of how to do this follows.
#
# sudo apt-get update
# sudo apt-get install libusb-1.0-0
# sudo apt-get install libftdi1-dev
#
# NOTE: Once have the executables built and running, may need to use
# the "-f" option to lower the frequency that Vivado picks.

echo "make USE_GETIFADDRS=wlan0 USE_LIBFTDI1=true"
make USE_GETIFADDRS=wlan0 USE_LIBFTDI1=true
