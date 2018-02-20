#!/bin/sh

# Use this script for building on macOS. Tested with macOS Sierra 10.12.6
#
# Need libusb and libftdi installed through brew before running this
# shell script.
#
# To install brew, go to: https://brew.sh/
#
# Once brew is installed, should be able to just do:
#
# brew install libftdi
#
# May need to also install libusb, but first let brew try to figure
# that out. If it cannot, try: "brew install libusb"
#
# IMPORTANT! macOS has a built-in driver to FTDI or you may have
# installed a FTDI driver at some point. Either of these, if loaded,
# will grab the device before this executable can. To remedy this,
# unload the kernel extensions for the FTDI device. An example command
# follows:
#
# sudo kextunload -bundle-id com.apple.driver.AppleUSBFTDI -bundle-id com.FTDI.driver.FTDIUSBSerialDriver
#
# This unloads both drivers, but only if they are loaded,
# obviously. If you get an error that one or both of these are not
# loaded, you can ignore it.
#
# If you want to go back to using these drivers, then either reboot,
# or simply repeat the command but with kextload. Keep in mind that
# trying to load a driver that you do not have will result in the
# command complaining but will not cause any system
# issues. Regardless, you will want to adjust the arguments of the
# command to fit your case.
#
# sudo kextload -bundle-id com.apple.driver.AppleUSBFTDI -bundle-id com.FTDI.driver.FTDIUSBSerialDriver
#

echo "make USE_GETIFADDRS=en0 USE_LIBFTDI1=true"
make USE_GETIFADDRS=en0 USE_LIBFTDI1=true
