# xvcd

This is a daemon that listens to "xilinx_xvc" (xilinx virtual cable)
traffic and operates JTAG over an FTDI in either bitbang mode or in
MPSSE mode.

The bitbang mode is the original code with slight modifications with
changes to the arguments of the interface functions. After building,
the executeable that uses bitbang mode is called "xvcdbb". From the
original README: "This version is hardcoded to use an FTDI cable with
an FT2232C chip. It does not use MPSSE but rather bitbang mode. It
uses ftdi_write_async which might not be available on your
platform. You can use the (much slower) non-async version instead."

For MPSSE mode, which can be faster than bitbang mode, the output
executable is called "xvcdmp". As a comparison, for a Xilinx Zync
XCXC7Z020 and using Vivado 2017.4, programming the Zynq FPGA logic
directly can take about 35 seconds through bitbang_mode and about
27 seconds with MPSSE mode. Have tested this with both a FT2232H and a
FT4232H. Both MPSSE ports of either of these devices can be used by
selecting it through the -i option.

If having trouble connecting to the JTAG target, try forcing the
frequency lower with the -f option. However, this is only used by the
MPSSE mode version.

If use the -i option to select a different interface on a multiple
interface device like the FT2232H and FT4232H, keep in mind that for
MPSSE mode, only interface 0 & 1 have MPSSE engines and you will get
an error if try to select interface 2 or 3. However, for bitbang mode,
can use all 4 interfaces.

One key to speeding up both the bitbang and MPSSE modes is setting the
USB latency timer setting to a lower value in io_init(). However, this
may have a detrimental impact on the host system. If the host is a
Raspberry Pi, which this was tested on, it seems fine. If the host is
a PC with lots of USB devices, you may need to increase the latency
timer value. This can be done with the LATENCY_TIMER make variable
(ie. make LATENCY_TIMER=16)

Also, the base server code was merged with
https://github.com/Xilinx/XilinxVirtualCable/blob/master/XAPP1251/src/xvcServer.c
so that Xilinx Virtual Cable is fully supported through both ISE and
Vivado.

## Configuration

To try to determine the IP address where the executable is running in
order to tell the user, the ethernet interface must be selected by
defining USE_GETIFADDRS to the interface name. This can be done
through a make variable (ie. make USE_GETIFADDRS=wlan0). If do not
define it, then the code is skipped.


## Installation

Need both libusb and libftdi1 libraries. See individual README_xxx.txt
files with extensive comments on how to install/build libusb and
libftdi for this build.

*Have fun!*
