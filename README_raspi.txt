 Use these instructions for building on a Raspberry Pi (even the Zero)
 running Raspbian. Tested with Raspbian 2017-11-29, Linux Kernel
 4.9.59+.

 Need libusb and libftdi installed through apt-get before running
 this shell script. An example of how to do this follows.

 sudo apt-get update
 sudo apt-get install libusb-1.0-0
 sudo apt-get install libftdi1-dev

 IMPORTANT: If the USB accesses seem slow (should be about 30 seconds
 to program a ZYNC 020). May need to build libusb and libftdi from
 source. If so, see README_slc6.txt for details.

 Before can use the FTDI device, may need to create a udev entry. See
 the udev instructions at:
 http://eblot.github.io/pyftdi/installation.html. Then unplug and
 plug the FTDI device in to have udev use this new rule.

 To build xvcd for a Raspberry Pi running Rasbian, simply do:

 make raspi

 The executables are in the src/ folder. xvcdbb uses bit-bang mode,
 which is likely slightly slower but can be used on FTDI GPIO. xvcdmp
 uses a MPSSE within the FTDI device.

 NOTE: Once have the executables built and running, may need to use
 the "-f" option to lower the frequency that Vivado picks.
