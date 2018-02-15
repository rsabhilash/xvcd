#include <stdio.h>
#include <ftdi.h>
#include <string.h>

#include "io_ftdi.h"

// NOTE: Merged in changes from https://github.com/ObKo/xvcd to support libftdi1

// Start with 4 MHz and let Vivado send settck: commands to change as
// it desires.
#define FTDI_TCK_DEFAULT_FREQ (4000000)

///////////////////////////////////////////////

// Size of the FTDI Write and Read buffers since they are statically
// defined. [TODO: make these dynamically defined].
//
// *2 on Write BUffer size because the write buffer does not know if
// exceeds the FTDI Write FIFO size until the command is built. So
// need more space so can hold the partial command that went over the
// FTDI Write buffer size while the previous commands are being sent
// to the FTDI.
//
// +30 is just to give a little extar space just in case.
//
#define FTDI_WRITE_BUFFER_SIZE ((2048*2)+30)
#define FTDI_READ_BUFFER_SIZE  (2048+30)

/* create a right shifted bit mask for number of bits */
#define BITMASK(bits)  (0xff >> (8-(bits)))

///////////////////////////////////////////////

#define PORT_TCK            0x01
#define PORT_TDI            0x02
#define PORT_TDO            0x04
#define PORT_TMS            0x08
#define PORT_MISC           0x90
#define IO_OUTPUT (PORT_MISC|PORT_TCK|PORT_TDI|PORT_TMS)

#define IO_DEFAULT_OUT     (0xe0)               /* Found to work best for some FTDI implementations */

// MPSSE Command Bytes (taken from pyftdi)
#define WRITE_BYTES_PVE_MSB  (0x10)
#define WRITE_BYTES_NVE_MSB  (0x11)
#define WRITE_BITS_PVE_MSB   (0x12)
#define WRITE_BITS_NVE_MSB   (0x13)
#define WRITE_BYTES_PVE_LSB  (0x18)
#define WRITE_BYTES_NVE_LSB  (0x19)
#define WRITE_BITS_PVE_LSB   (0x1a)
#define WRITE_BITS_NVE_LSB   (0x1b)
#define READ_BYTES_PVE_MSB   (0x20)
#define READ_BYTES_NVE_MSB   (0x24)
#define READ_BITS_PVE_MSB    (0x22)
#define READ_BITS_NVE_MSB    (0x26)
#define READ_BYTES_PVE_LSB   (0x28)
#define READ_BYTES_NVE_LSB   (0x2c)
#define READ_BITS_PVE_LSB    (0x2a)
#define READ_BITS_NVE_LSB    (0x2e)
#define RW_BYTES_PVE_NVE_MSB (0x31)
#define RW_BYTES_NVE_PVE_MSB (0x34)
#define RW_BITS_PVE_PVE_MSB  (0x32)
#define RW_BITS_PVE_NVE_MSB  (0x33)
#define RW_BITS_NVE_PVE_MSB  (0x36)
#define RW_BITS_NVE_NVE_MSB  (0x37)
#define RW_BYTES_PVE_NVE_LSB (0x39)
#define RW_BYTES_NVE_PVE_LSB (0x3c)
#define RW_BITS_PVE_PVE_LSB  (0x3a)
#define RW_BITS_PVE_NVE_LSB  (0x3b)
#define RW_BITS_NVE_PVE_LSB  (0x3e)
#define RW_BITS_NVE_NVE_LSB  (0x3f)
#define WRITE_BITS_TMS_PVE   (0x4a)
#define WRITE_BITS_TMS_NVE   (0x4b)
#define RW_BITS_TMS_PVE_PVE  (0x6a)
#define RW_BITS_TMS_PVE_NVE  (0x6b)
#define RW_BITS_TMS_NVE_PVE  (0x6e)
#define RW_BITS_TMS_NVE_NVE  (0x6f)

struct fifo_size
{
    unsigned int tx;
    unsigned int rx;
};

    
static struct ftdi_context ftdi;
static int vlevel = 0;


void io_close(void);

// Look at type in ftdi_context and report if this FTDI device is the
// more advanced H series or not.
//
// Although safe to call without initializing or opening the FTDI
// device, the result will be inaccurate. So best to only use after
// calling a ftdi_usb_openXXX function.
//
// return: logical true if ftdi->type indicates this is a H series, otherwise logical false
int io_is_H_series (struct ftdi_context *ftdi)
{

    switch(ftdi->type) {
	case TYPE_2232H:
	case TYPE_4232H:
	case TYPE_232H:
	    return !0;
	default:
	    return 0;
    }

}

// Based on the chip type, return the TX and RX FIFO byte sizes. This
// information was taken from the Python code for
// pyftdi (https://github.com/eblot/pyftdi).
//
// Although safe to call without initializing or opening the FTDI
// device, the result will be inaccurate. So best to only use after
// calling a ftdi_usb_openXXX function.
//
// return: a structure with fifo sizes for TX (write to FTDI) and RX (read from FTDI)
struct fifo_size io_get_fifo_sizes (struct ftdi_context *ftdi)
{

    // Original comment from pyftdi:
    //
    // # Note that the FTDI datasheets contradict themselves, so
    // # the following values may not be the right ones...
    // # Note that 'TX' and 'RX' are inverted with the datasheet terminology:
    // # Values here are seen from the host perspective, whereas datasheet
    // # values are defined from the device perspective

    struct fifo_size fifo;
    
    switch(ftdi->type) {
    case TYPE_2232C: fifo.tx = 384; fifo.rx = 128; break;   // BCD Device: 0x0500  # TX: 384, RX: 128
    case TYPE_R:     fifo.tx = 128; fifo.rx = 256; break;   // BCD Device: 0x0600  # TX: 128, RX: 256
    case TYPE_2232H: fifo.tx = 4096; fifo.rx = 4096; break; // BCD Device: 0x0700  # TX: 4KiB, RX: 4KiB
    case TYPE_4232H: fifo.tx = 2048; fifo.rx = 2048; break; // BCD Device: 0x0800  # TX: 2KiB, RX: 2KiB
    case TYPE_232H:  fifo.tx = 1024; fifo.rx = 1024; break; // BCD Device: 0x0900  # TX: 1KiB, RX: 1KiB
	// Newer Type which only shows up in newer version of
	// libftdi. Leave out to minimize compilation problems on
	// other systems. However, if using this type and the newer
	// libftdi, feel free to uncomment it.
	//
	//case TYPE_230X: return {512, 512}; // BCD Device: 0x1000    # TX: 512, RX: 512
    default: fifo.tx = 128; fifo.rx = 128; break;  //  # default sizes
    }

    return fifo;
}


int io_read_data (unsigned char *buffer, unsigned int len)
{
    // Read from the FTDI device and place len bytes into buffer.
    // It may take multiple reads over USB to receive all expected bytes.
    int i = 0;
    int timeout = 20;
    int res;
        
    while ((i < len) && timeout > 0)
    {
        res = ftdi_read_data(&ftdi, buffer + i, len - i);

        if (res < 0)
        {
            fprintf(stderr, "ftdi_read_data %d (%s)\n", res, ftdi_get_error_string(&ftdi));
            return res;
        }
                
        i += res;
	timeout--;
    }

    if (vlevel > 3 && timeout <= 0) {
	fprintf(stderr, "ftdi_read_data Timeout! (may be okay)\n");
    }
    
    return i;
}


// Send MPSSE Commands, receive data and decode into TDO bits
//
// cmdp - pointer to the command byte array to send
// cmdBytes - number of bytes to send
// rxBytes - number of bytes expected to receive
// TDOpp - pointer to pointer to byte where TDO bits are to be returned
//          allows the TDO pointer to be updated after returning
//
// return: actual number of bytes received or a negative number if error
//
int io_transfer_mpsse(unsigned char *cmdp, int cmdBytes, int rxBytes, unsigned char **TDOpp)
{
    static unsigned char rxbuf[FTDI_READ_BUFFER_SIZE];
    unsigned char *rxp;
    unsigned int len;
    unsigned int bits;
    unsigned int bi;		/* bit index */
    int res;
    
    // Make sure do not blow the rxbuf buffer - should be sized to
    // avoid this but notify in case that is not the case.
    if (rxBytes > sizeof(rxbuf)) {
	fprintf(stderr, "io_transfer_mpsse(): Number of read bytes (%d) exceeds read buffer size (%d)! Aborting transfer!\n", rxBytes, sizeof(rxbuf));
	return -1;
    }

    // Send MPSSE commands
    res = ftdi_write_data(&ftdi, cmdp, cmdBytes);
    if (res != cmdBytes) {
	fprintf(stderr, "io_transfer_mpsse(): ftdi_write_data() returned %d (%s)\n", res, ftdi_get_error_string(&ftdi));
	return -1;
    }

    // Read back the expected receive bytes
    res = io_read_data(rxbuf, rxBytes);
    if (res != rxBytes) {
	fprintf(stderr, "io_transfer_mpsse(): ftdi_read_data() only read %d bytes.", res);
	if (res == 0) {
	    fprintf(stderr, " Timeout Likely!!");
	}
	fprintf(stderr, "\n");
	return -1;
    }

    len = 0;
    bits = 0;
    bi = 0;
    rxp = rxbuf;
    while ((rxp - rxbuf) < rxBytes) {
	// Process TDO bits. Since some of the commands are partial bit
	// transfers, must do some bit shifting and splicing to form the
	// TDO bit stream. Use the command buffer to know how to handle
	// the received data.
	//
    
	// First double check that all commands are lsb so we can just
	// assume to process as lsb. If find a MSB command, tell the
	// user/programmer to fix the code. Bit 3 is a '1' for lsb and a
	// '0' for msb.
	if ((*cmdp & 0x08) == 0) {
	    fprintf(stderr, "io_transfer_mpsse(): MSB command used. Will process incorrectly as if LSB! Fix your code!\n");
	}
    
	// Next check if bit transfer or byte transfer. Bit 1 of the
	// command opcode is a '1' if a bit command and a '0' if a byte
	// command.
	if (*cmdp++ & 0x02) {
	    if (vlevel > 4) fprintf(stderr, "io_transfer_mpsse(): Found a bit command!\n");
	    // handle the bit command
	    // - next command byte is the (bit length - 1)
	    // - followed by data byte shifted down from bit 7
	    len = (unsigned int) ((*cmdp++) + 1);
	    cmdp++;		/* advance to next command */
	    bits |= ((unsigned int)(*rxp++) >> (8-len)) << bi;
	    if ((bi + len) >= 8) {
		// If collected 8 bits, copy the bit-aligned byte to
		// TDO and shift down the bits holder and the bit index
		**TDOpp = bits & 0x00ff;
		(*TDOpp)++;
		bits >>= 8;
		bi = (bi + len) - 8;
	    } else {
		// Have not filled up an entire byte, so simply increase bit index
		bi += len;
	    }
	} else {
	    if (vlevel > 4) fprintf(stderr, "io_transfer_mpsse(): Found a byte command!\n");
	    if (bi != 0) {
		// bi should always be 0 when reach a byte command. If error - issue an error, but continue, so can debug.
		fprintf(stderr, "io_transfer_mpsse(): bitindex error - has value of %d but should be 0 here!\n", bi);
	    }
	    // handle the byte command
	    // - next two command bytes are the (byte length - 1) with LSB first
	    // - followed by the data bytes
	    len = (unsigned int) (*cmdp++);
	    len |= (((unsigned int) (*cmdp++)) << 8);
	    len += 1;
	    cmdp += len;		/* advance to next command */
	    memcpy(*TDOpp,rxp,len);
	    (*TDOpp) += len;
	    rxp += len;
	}
    }

    if (bi != 0) {
	// Before leave, make sure all partial bits in bits have been sent to TDO. 
	**TDOpp = bits & 0x00ff;
	(*TDOpp)++;
    }
    
    // Check that did not process more bytes than should have so can catch the bug.
    if ((rxp - rxbuf) > rxBytes) {
	fprintf(stderr, "io_transfer_mpsse(): Processed TOO MANY bytes!\n");
    }

    // return number of bytes received and processed
    return (rxp - rxbuf);
}

// Build the FTDI MPSSE command for sending the TMS and TDI bits using MPSSE bit commands.
//
// TMSp - pointer to byte where TMS bits are to be pulled, lsb first
// TDIp - pointer to byte where TDI bits are to be pulled, lsb first
// bits - number of bits left in TMS and TDI buffers, but only up to 8 will be sent
// cmdp - pointer to cmd buffer to write command bytes
// cmdszp - the number of bytes added to cmdp will be returned here
// rxszp - the number of bytes to expect in the read bytes from these added commands will be returned here
//
// return: number of bits from TMSp and TDIp processed
//
int io_build_cmd_bits(const unsigned char *TMSp, const unsigned char *TDIp, int bits, unsigned char *cmdp, int *cmdszp, int *rxszp)
{
    // Seperate bit streams where TMS is '1' and where it is
    // '0'. Where TMS is '1', TDI must be the same value for the
    // MPSSE command due to how badly constructed the MPSSE
    // commands are (why is it this way?)
    unsigned char TMS = *TMSp;
    unsigned char TDI = *TDIp;

    /* i will be used to search for number of bits in bit stream for next MPSSE command */
    int i;
    int bitsUsed = 0;

    // Initialize return values
    *cmdszp = 0;
    *rxszp = 0;

    // Make sure bits will not be more than 8
    bits = (bits > 8) ? 8 : bits;
    	
    if (vlevel > 3) printf("11: bits: %d  TMS: 0x%02x  TDI: 0x%02x\n", bits, TMS, TDI); 		    
	
    while (bits > 0) {
	i = 1;		/* reset i to 1 at the start of each loop */
	if (TMS & 0x01) {
	    // lsb of TMS is '1', so send a TMS command.
	    //
	    // Can send up to 7 clocks of TMS zbut TDI must be
	    // static during these clocks, although its static
	    // state can be set. So search through TDI bits
	    // looking for where its value changes, up to 7 bits
	    // of TMS total.
	    //
	    // TMS command can only send 7 bits, so stop search if i == 7
	    while (((bits-i) > 0) && (i < 7) &&
		   ((TDI & BITMASK(i+1)) == 0 || (TDI & BITMASK(i+1)) == BITMASK(i+1))) {
		if (vlevel > 3) printf("15: i: %d   BITMASK(i+1): 0x%02x  TDI & BITMASK(i+1): 0x%02x\n", i, BITMASK(i+1), TDI & BITMASK(i+1)); 		    
		i++;			    
	    }
	    // TMS is all 1's, so send TMS command with a single TDI that remains in this stat during TMS's
	    *cmdp++ = RW_BITS_TMS_PVE_NVE; /* write TDI/TMS on falling TCK, read TDO on rising TCK */
	    *cmdp++ = i - 1;	/* 1 bits -> 0, 7 bits -> 6 */
	    *cmdp++ = ((TDI & 0x01) << 7) | (TMS & BITMASK(i));
	    *cmdszp += 3;
	    *rxszp += 1;		/* expect 1 byte to be read in response to this command */
	    if (vlevel > 3) printf("12: cmd: 0x%02x%02x%02x cmdsz: %d rxsz: %d\n", *(cmdp-3), *(cmdp-2), *(cmdp-1), *cmdszp, *rxszp); 		    
	} else {
	    // lsb of TMS is '0', so send a TDI command for bit stream where TMS is '0'
	    //
	    // although bits should not be > 8, stop search if i == 8
	    while (((bits-i) > 0) && (i < 8) && !(TMS & BITMASK(i+1))) {
		i++;			    
	    }
	    // Section of TMS is all 0's, so send TDI bits
	    *cmdp++ = RW_BITS_PVE_NVE_LSB; /* write TDI/TMS on falling TCK, read TDO on rising TCK */
	    *cmdp++ = i - 1;	/* 1 bits -> 0, 8 bits -> 7 */
	    *cmdp++ = TDI & BITMASK(i);
	    *cmdszp += 3;
	    *rxszp += 1;		/* expect 1 byte to be read in response to this command */
	    if (vlevel > 3) printf("13: cmd: 0x%02x%02x%02x cmdsz: %d rxsz: %d\n", *(cmdp-3), *(cmdp-2), *(cmdp-1), *cmdszp, *rxszp); 		    
	}

	// advance bitsUsed, bits, TMS & TDI
	bitsUsed += i;
	bits -= i;
	TMS >>= i;
	TDI >>= i;		
	if (vlevel > 3) printf("14: bits: %d  TMS: 0x%02x  TDI: 0x%02x\n", bits, TMS, TDI); 		    
    }

    return bitsUsed;
}

// Build the FTDI MPSSE command for sending the TDI bits using MPSSE byte commands.
//
// TMSp - pointer to byte where TMS bits are to be checked, lsb first
// TDIp - pointer to byte where TDI bits are to be pulled, lsb first
// bits - number of bits left in TMS and TDI buffers
// cmdp - pointer to cmd buffer to write command bytes
// cmdszp - the number of bytes added to cmdp will be returned here
// rxszp - the number of bytes to expect in the read bytes from these added commands will be returned here
//
// return: number of bits from TMSp and TDIp processed
//
int io_build_cmd_bytes(const unsigned char *TMSp, const unsigned char *TDIp, int bits, unsigned char *cmdp, int *cmdszp, int *rxszp)
{
    struct fifo_size fifo_sz = io_get_fifo_sizes(&ftdi);
    int bytes = (bits >> 3);	/* number of *full* bytes left in TMS & TDI */
    int i;			/* will be used to search for number of bits in bit stream for next MPSSE command */

    // Initialize return values
    *cmdszp = 0;
    *rxszp = 0;
    

    if (vlevel > 3) printf("111: bits: %d  TMS[0]: 0x%02x  TDI[0]: 0x%02x\n", bits, *TMSp, *TDIp); 		    
	

    // Search through TMS to find out how many bytes we can send
    // before TMS is no longer all 0's. i will be the number of bytes
    // of TDI that can be sent before TMS is non-0.
    //
    // Make sure do not build a command that cannot completely fit in
    // the Write FIFO (TX).
    i = 0; 
    while ((i < bytes) && (i < (fifo_sz.tx-3)) && !(*TMSp++)) {
	i++;			    
    }

    if (i > 0) {
	// If found a section to send, build the command
	bytes = i - 1;		           /* reuse bytes variable to be a holding register for filling in the length */
	*cmdp++ = RW_BYTES_PVE_NVE_LSB;    /* write TDI on falling TCK, read TDO on rising TCK */
	*cmdp++ = (bytes & 0x00ff);	   /* 1 bytes -> 0, 65536 bytes -> 0xffff LOW BYTE */
	*cmdp++ = ((bytes >> 8) & 0x00ff); /* 1 bytes -> 0, 65536 bytes -> 0xffff HIGH BYTE */
	memcpy(cmdp, TDIp, i);             /* copy i bytes into the command vector */
	cmdp += i;
	*cmdszp = 3+i;
	*rxszp = i;		         /* expect i byte to be read in response to this command */
	if (vlevel > 3) printf("113: cmd: 0x%02x%02x%02x%02x... cmdsz: %d rxsz: %d\n", *(cmdp-3-i), *(cmdp-2-i), *(cmdp-1-i), *(cmdp-i), *cmdszp, *rxszp); 		    
    }

    // return number of bits processed from TMSp and TDIp
    return (i << 3);
}


// frequency - desired JTAG TCK frequency in Hz
//
// return: if error, return an error code < 0
//         if success, return the actual frequency in Hz
//
int io_set_freq(unsigned int frequency)
{
    const unsigned int BUS_CLOCK_BASE = 6000000;
    const unsigned int BUS_CLOCK_HIGH = 30000000;
    const unsigned int max_freq =  (io_is_H_series(&ftdi) ? BUS_CLOCK_HIGH : BUS_CLOCK_BASE);
    unsigned int divisor, actual_freq;
    unsigned char divcode;
    unsigned char buf[6];
    int cnt, res;

    if (frequency > max_freq) {
	fprintf(stderr, "Unsupported frequency: %u Hz. Capping to: %u Hz\n", frequency, max_freq);
	frequency = max_freq;
    }
    
    if (frequency <= BUS_CLOCK_BASE) {
	divcode = EN_DIV_5;
	divisor = ((BUS_CLOCK_BASE+frequency-1)/frequency)-1;
	actual_freq = (BUS_CLOCK_BASE+divisor-1)/(divisor+1);
    } else if (frequency <= BUS_CLOCK_HIGH) {
	// BUS_CLOCK_HIGH is a H SERIES feature
	divcode = DIS_DIV_5;
	divisor = ((BUS_CLOCK_HIGH+frequency-1)/frequency)-1;
	actual_freq = (BUS_CLOCK_HIGH+divisor-1)/(divisor+1);
    } else {
	fprintf(stderr, "Unsupported frequency: %u Hz\n", frequency);
	return -254;
    }

    cnt = 0;

    if (io_is_H_series(&ftdi)) {
	buf[cnt++] = divcode;
    }
    buf[cnt++] = TCK_DIVISOR;
    buf[cnt++] = divisor & 0x00ff;
    buf[cnt++] = (divisor >> 8) & 0x00ff;
    res = ftdi_write_data(&ftdi, buf, cnt);
    if (res != cnt)
    {
	fprintf(stderr, "ftdi_write_data() for 0x%x: %d (%s)\n", buf[0], res, ftdi_get_error_string(&ftdi));
        return -254;
    }

    // Check for any error return by FTDI for any of the above
    // commands. The expected response is that io_read_data() will
    // timeout and return res = 0. However, if something is returned,
    // check to see if it is an error. (NOTE: not totally sure if
    // ftdi_read_data() will return status in case of error. It does
    // strip out the modem status bytes).
    if (vlevel > 1) {
	printf("Reading back response\n");
    }
    res = io_read_data (buf, 2);
    if (vlevel > 1 && res >= 2) {
	printf("FTDI Response: 0x%02x 0x%02x\n",buf[0], buf[1]);
    }
    if (res >= 2 && buf[0] == 0xfa) {
        fprintf(stderr, "Invalid FTDI MPSSE command at %d\n",buf[1]);
	return -253;
    }
    
    res = ftdi_usb_purge_rx_buffer(&ftdi);
    if (res < 0)
    {
        fprintf(stderr, "ftdi_usb_purge_rx_buffers %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        return res;
    }

    return (int) actual_freq;
}

// period - desired JTAG TCK period in ns
//
// return: if error, return an error code < 0
//         if success, return the actual period in ns
//
int io_set_period(unsigned int period)
{
    int actPeriod;

    // convert period in ns to frequency in Hz and set the TCK frequency
    actPeriod = io_set_freq( 1000000000 / period );

    if (actPeriod > 0) {
	// If no error in setting frequency, convert it back to a period in ns.
	actPeriod = 1000000000 / actPeriod;
    }
    
    return  actPeriod;
}

// Pass in the selected FTDI device to be opened
//
// vendor: vendor ID of desired device, or -1 to use the default
//
// product: product ID of desired device, or -1 to use the default
//
// serial: string of FTDI serial number to match in case of multiple
//         FTDI devices plugged into host with the same Vendor/Product
//         IDs or NULL to select first device found.
//
// index: number starting at 0 to select FTDI device. Can be used
//        instead of serial, but serial is a more definite match since
//        index depends on how host numbers the devices. If both
//        serial and index is given, index is ignored. Use a value of
//        0 to select first FTDI device that is found.
//
// interface: starts at 0 and selects one of multiple "ports" in the
//            selected device. For example, the FT4232H and FT2232H
//            have multiple ports. If not used, simply pass in 0 and
//            the first one, typically labeled "A", will be selected.
//
// frequency: (in Hz) set TCK frequency - settck: command from the
//            client is ignored. Pass in 0 to try to obey settck:
//            commands.
//
// verbosity: 0 means no output, increase from 0 for more and more debugging output
//
int io_init(int vendor, int product, const char* serial, unsigned int index, unsigned int interface, unsigned long frequency, int verbosity)
{
    unsigned char buf[16];
    int res, len;
    struct fifo_size fifo_sz;
    
    if (product < 0)
        product = 0x6010;
    if (vendor < 0)
        vendor = 0x0403;

    // Save verbosity level
    vlevel = verbosity;
    
    res = ftdi_init(&ftdi);
        
    if (res < 0)
    {
        fprintf(stderr, "ftdi_init: %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        return 1;
    }
    
    {
	enum ftdi_interface selected_interface;
	// Select interface - must be done before ftdi_usb_open
	switch (interface) {
	case 0: selected_interface = INTERFACE_A; break;
	case 1: selected_interface = INTERFACE_B; break;
	case 2: selected_interface = INTERFACE_C; break;
	case 3: selected_interface = INTERFACE_D; break;
	default: selected_interface = INTERFACE_ANY; break;	
	}

	if (interface != 0 && interface != 1) {
	    printf("WARNING: This device may not have a MPSSE on interface %d!\n         Pick another interface if get errors.\n\n", interface);
	}
    
	res = ftdi_set_interface(&ftdi, selected_interface);
	if (res < 0) {
	    fprintf(stderr, "ftdi_set_interface(%d): %d (%s)\n", interface, res, ftdi_get_error_string(&ftdi));
	    ftdi_deinit(&ftdi);
	    return 1;
	}
    }

    if (serial != NULL) index = 0; /* ignore index if serial is given */
    
    res = ftdi_usb_open_desc_index(&ftdi, vendor, product, NULL, serial, index);
        
    if (res < 0)
    {
        fprintf(stderr, "ftdi_usb_open(0x%04x, 0x%04x): %d (%s)\n", vendor, product, res, ftdi_get_error_string(&ftdi));
        ftdi_deinit(&ftdi);
        return 1;
    }

    if (vlevel > 0) {
	if (serial == NULL) {
	    printf("Opened FTDI 0x%04x:0x%04x, interface: %d, type=", vendor, product, interface);
	} else {
	    printf("Opened FTDI 0x%04x:0x%04x, serial=%s, interface: %d, type=", vendor, product, serial, interface);
	}
	
	switch(ftdi.type) {
	case TYPE_AM: printf("AM"); break;
	case TYPE_BM: printf("BM"); break;
	case TYPE_2232C: printf("2232C"); break;
	case TYPE_R: printf("R"); break;
	case TYPE_2232H: printf("2232H"); break;
	case TYPE_4232H: printf("4232H"); break;
	case TYPE_232H: printf("232H"); break;
	    // Newer Type which only shows up in newer version of
	    // libftdi. Leave out to minimize compilation problems on
	    // other systems.
	    //
	    //case TYPE_230X: printf("230X"); break;
	default: printf("!UNKNOWN!"); break;
	}
	printf("\n\n");
    }

    // Get the expected FIFO Siz of this FTDI device
    fifo_sz = io_get_fifo_sizes(&ftdi);
    
    //NOTE: Not sure if this is needed, but it seems like a good idea
    res = ftdi_usb_reset(&ftdi);
    if (res < 0) {
        fprintf(stderr, "Unable to reset FTDI device: %d (%s).\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }

    // THIS IS VERY IMPORTANT for fast JTAG accesses. If do not change
    // this, can go from 22 second program times to 75 second program
    // times. However, it might impact how well the host system works
    // so use with care. If find the host fails to handle other USB
    // device, increase this number.
    res = ftdi_set_latency_timer(&ftdi, 4);
    if (res < 0) {
	fprintf(stderr, "Unable to set latency timer: %d (%s).\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
    
    // Set the write chunk size to the full TX FIFO size. Not
    // completely sure, but believe this is the most efficient way of
    // handling MPSSE commands.
    res = ftdi_write_data_set_chunksize(&ftdi, fifo_sz.tx);
    if (res) {
	fprintf(stderr, "Unable to set write chunk size: %d (%s).\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
    
    // Set the read chunk size to the full RX FIFO size. Not
    // completely sure, but believe this is the most efficient way of
    // handling MPSSE commands.
    res = ftdi_read_data_set_chunksize(&ftdi, fifo_sz.rx);
    if (res) {
	fprintf(stderr, "Unable to set read chunk size: %d (%s).\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }

#if 0    
    // RESET FTDI - seems to cause more harm (I/Os reset) than good, so skip.
    res = ftdi_set_bitmode(&ftdi, IO_OUTPUT, BITMODE_RESET);

    if (res < 0) 
    {
        fprintf(stderr, "ftdi_set_bitmode: %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
#endif
    
    // Set the USB read and write timeouts (not sure if this really
    // helps but does not seem to hurt)
    ftdi.usb_read_timeout = 120000;
    ftdi.usb_write_timeout = 120000;

    res = ftdi_usb_purge_buffers(&ftdi);        
    if (res < 0)
    {
        fprintf(stderr, "ftdi_usb_purge_buffers %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
        
    // Disable event and error characters
    //
    // NOTE: Not sure if this is really needed but does not seem to hurt.
    res = ftdi_set_event_char(&ftdi, 0, 0);
    if (res < 0)
    {
        fprintf(stderr, "ftdi_set_event_char %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }        
    res = ftdi_set_error_char(&ftdi, 0, 0);
    if (res < 0)
    {
        fprintf(stderr, "ftdi_set_error_char %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
    
    // Enable MPSSE mode
    res = ftdi_set_bitmode(&ftdi, IO_OUTPUT, BITMODE_MPSSE);

    if (res < 0) 
    {
        fprintf(stderr, "ftdi_set_bitmode: %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }


    res = ftdi_usb_purge_buffers(&ftdi);        
    if (res < 0)
    {
        fprintf(stderr, "ftdi_usb_purge_buffers %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }

    if (frequency == 0) {
	frequency = FTDI_TCK_DEFAULT_FREQ;
    }
    
    res = io_set_freq(frequency);
    if (res < 0)
    {
        fprintf(stderr, "io_set_frequency %d\n", res);
        io_close();
        return 1;
    }

    // Update state of outputs to the default
    if (vlevel > 2) printf("Setting initial outputs\n");
    buf[0] = SET_BITS_LOW;
    buf[1] = IO_DEFAULT_OUT;
    buf[2] = IO_OUTPUT;
    len = 3;
    res = ftdi_write_data(&ftdi, buf, len);
    if (res != len)
    {
	fprintf(stderr, "ftdi_write_data() for 0x%x: %d (%s)\n", buf[0], res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
    
    // Disable loopback mode
    buf[0] = LOOPBACK_END;
    len = 1;
    res = ftdi_write_data(&ftdi, buf, len);
    if (res != len)
    {
	fprintf(stderr, "ftdi_write_data() for 0x%x: %d (%s)\n", buf[0], res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }

    // Check for any error return by FTDI for any of the above
    // commands. The expected response is that io_read_data() will
    // timeout and return res = 0. However, if something is returned,
    // check to see if it is an error. (NOTE: not totally sure if
    // ftdi_read_data() will return status in case of error. It does
    // strip out the modem status bytes).
    if (vlevel > 1) {
	printf("Reading back response\n");
    }
    res = io_read_data (buf, 2);
    if (vlevel > 1 && res >= 2) {
	printf("FTDI Response: 0x%02x 0x%02x\n",buf[0], buf[1]);
    }
    if (res >= 2 && buf[0] == 0xfa) {
        fprintf(stderr, "Invalid FTDI MPSSE command at %d\n",buf[1]);
	return -253;
    }

    if (vlevel > 0) printf("FTDI ready with MPSSE mode\n\n");

    return 0;
}


int io_scan(const unsigned char *TMS, const unsigned char *TDI, unsigned char *TDO, int bits)
{
    static unsigned char cmdbuf[FTDI_WRITE_BUFFER_SIZE];
    struct fifo_size fifo_sz = io_get_fifo_sizes(&ftdi);
    unsigned char *cmdp = cmdbuf;
    unsigned char *TDOstart = TDO;
    int numTDOBytes = (bits+7) / 8;     // Number of TDO bytes to be received

    int res;
    int cmdBytes = 0, rxBytes = 0, bitsUsed=0;
    
    if (vlevel > 3) printf("io_scan() of %d bits\n", bits);

    cmdp = cmdbuf;
    cmdBytes = 0;
    rxBytes = 0;
    while (bits > 0) {
	int cmdsz, rxsz;

	if (bits < 16 || *TMS || *(TMS+1)) {
	    // If less than two bytes to send, or current TMS byte has
	    // '1's or the next TMS byte has 1's, handle using bit
	    // commands.
	    //
	    // The reason for checking for less than 16 bits or for
	    // the next TMS byte is because it is slightly more
	    // efficient to send a single byte using a bit command
	    // than a byte command since the byte commands use two
	    // bytes for the byte length. So if io_build_cmd_bits()
	    // will be called on the next byte, call it for this byte
	    // as well. Note that the number of remaining bits is
	    // checked first so that TMS buffer does not go past the
	    // end.
	    //
	    // NOTE: io_build_cmd_bits() will not use more than 8 bits
	    // so next byte can be checked whether it should be sent
	    // with bit commands or byte commands.
	    //
	    if (vlevel > 4) printf("1: bits = %d\n", bits);
	    bitsUsed = io_build_cmd_bits(TMS, TDI, bits, cmdp, &cmdsz, &rxsz);
	    if (vlevel > 4) printf("2: bitsUsed = %d, cmdsz = %d, rxsz = %d\n", bitsUsed, cmdsz, rxsz);
	} else {
	    // Otherwise, handle with byte commands
	    if (vlevel > 4) printf("5: bits = %d\n", bits);
	    bitsUsed = io_build_cmd_bytes(TMS, TDI, bits, cmdp, &cmdsz, &rxsz);
	    if (vlevel > 4) printf("6: bitsUsed = %d, cmdsz = %d, rxsz = %d\n", bitsUsed, cmdsz, rxsz);
	}
	 /* update TMS & TDI pointers to skip bytes already consumed */
	TMS += ((bitsUsed+7) >> 3);
	TDI += ((bitsUsed+7) >> 3);

	// check that did not overflow cmdbuf array
	if ((cmdBytes + cmdsz) > FTDI_WRITE_BUFFER_SIZE) {
	    fprintf(stderr, "io_scan(): cmdbuf OVERFLOW! cmdbuf is now %d bytes. Fix code to prevent this from happening!\n", (cmdBytes + cmdsz));
	}        	
	
	if ((cmdBytes + cmdsz) > fifo_sz.tx) {
	    // cannot fit in the new commands without exceeding
	    // FTDI FIFO Write Size, so send on cmdbuf and then
	    // restart at beginning of buffer with the commands that
	    // we just received.
	    res = io_transfer_mpsse(cmdbuf, cmdBytes, rxBytes, &TDO);
	    if (res != rxBytes) {
		fprintf(stderr, "Error transferring data with FTDI\n");
		return -1;
	    }

	    // Move commands from last io_cmd_build_xxx() call to
	    // front of cmdbuf to continue sending/receiving data and
	    // reset counters.
	    //
	    // Areas under some really odd cirsumstance could overlap
	    // so use memmove(). This should not be happening often,
	    // so can be a little non-optimized.
	    cmdp = memmove(cmdbuf, cmdp, cmdBytes);
	    cmdBytes = 0;
	    rxBytes = 0;
	}
	cmdp += cmdsz;
	cmdBytes += cmdsz;
	rxBytes += rxsz;
	bits -= bitsUsed;
	if (vlevel > 4) printf("3: cmdBytes = %d, rxBytes = %d, bits = %d\n", cmdBytes, rxBytes, bits);
    }

    if (cmdBytes > 0) {
	// handle the remaining command bytes
	res = io_transfer_mpsse(cmdbuf, cmdBytes, rxBytes, &TDO);
	if (res != rxBytes) {
	    fprintf(stderr, "Error transferring data with FTDI\n");
	    return -1;
	}
    }

    // check that did not over blow TDO array
    if ((TDO - TDOstart) > numTDOBytes) {
	fprintf(stderr, "io_scan(): wrote too many bytes into TDO! Exp.: %d  Act. %d\n", numTDOBytes, (TDO - TDOstart));
    }
        
    return 0;
}

void io_close(void)
{
    ftdi_set_bitmode(&ftdi, 0, BITMODE_RESET);
    ftdi_usb_close(&ftdi);
    ftdi_deinit(&ftdi);
}
