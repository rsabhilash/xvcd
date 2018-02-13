#include <stdio.h>
#include <ftdi.h>
#include <string.h>

#include "io_ftdi.h"

// NOTE: Merged in changes from https://github.com/ObKo/xvcd to support libftdi1

#define BUILD_FOR_H_SERIES (-1)
#define USE_ASYNC
#undef  USE_LIBFTDI1

///////////////////////////////////////////////

// Each read also returns 2 status bytes which are not returned to
// us. Not sure if this really counts against CHUNK SIZE but
// subtracting 2 for FTDI_READ_CHUNK_SIZE just in case.
#define FTDI_WRITE_CHUNK_SIZE  (256)
#define FTDI_READ_CHUNK_SIZE  (256-2)

#ifndef USE_ASYNC
#define FTDI_MAX_WRITESIZE 256
#endif

/* create a right shifted bit mask for number of bits */
#define BITMASK(bits)  (0xff >> (8-bits))

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

static struct ftdi_context ftdi;
static int vlevel = 0;


void io_close(void);

int io_read_data (unsigned char *buffer, unsigned int len)
{
    // Read from the FTDI device and place len bytes into buffer.
    // It may take multiple reads over USB to receive all expected bytes.
    int i = 0;
    int timeout = 20;
    int res;
        
    while ((i < len) && timeout > 0)
    {
	//@@@printf("Calling ftdi_read_data()\n");
        res = ftdi_read_data(&ftdi, buffer + i, len - i);
	//@@@printf("Return from ftdi_read_data(): %d\n", res);

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
// TDOp - pointer to byte where TDO bits are to be returned
// rxBytes - number of bytes expected to receive
//
// return: actual number of bytes received or a negative number if error
//
int io_transfer_mpsse(unsigned char *cmdp, int cmdBytes, unsigned char *TDOp, int rxBytes)
{
    static unsigned char rxbuf[FTDI_READ_CHUNK_SIZE+30];
    unsigned char *rxp;
    unsigned int len;
    unsigned int bits;
    unsigned int bi;		/* bit index */
    int res;
    
    // Make sure do not blow the READ CHUNK SIZE - should be sized to
    // avoid this but notify in case that is not the case.
    if (rxBytes > FTDI_READ_CHUNK_SIZE) {
	fprintf(stderr, "io_transfer_mpsse(): Number of read bytes (%d) exceeds read chunk size (%d)! Aborting transfer!\n", rxBytes, FTDI_READ_CHUNK_SIZE);
	return -1;
    }

    // Send MPSSE commands
    res = ftdi_write_data(&ftdi, cmdp, cmdBytes);
    if (res != cmdBytes) {
	fprintf(stderr, "io_transfer_mpsse(): ftdi_write_data %d (%s)\n", res, ftdi_get_error_string(&ftdi));
	return -1;
    }

    // Read back the expected receive bytes
    res = io_read_data(&ftdi, rxbuf, rxBytes);
    if (res != rxBytes) {
	fprintf(stderr, "io_transfer_mpsse(): ftdi_read_data %d (%s)\n", res, ftdi_get_error_string(&ftdi));
	return -1;
    }

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
	    // handle the bit command
	    // - next command byte is the (bit length - 1)
	    // - followed by data byte shifted down from bit 7
	    len = (unsigned int) ((*cmdp++) + 1);
	    cmdp++;		/* advance to next command */
	    bits |= ((unsigned int)(*rxp++) >> (8-len)) << bi;
	    if ((bi + len) >= 8) {
		// If collected 8 bits, copy the bit-aligned byte to
		// TDO and shift down the bits holder and the bit index
		*TDOp++ = bits & 0x00ff;
		bits >>= 8;
		bi = (bi + len) - 8;
	    } else {
		// Have not filled up an entire byte, so simply increase bit index
		bi += len;
	    }
	} else {
	    fprintf(stderr, "io_transfer_mpsse(): Found a byte command!\n");
	    if (bi != 0) {
		// bi should always be 0 when reach a byte command. If error - issue an error, but continue, so can debug.
		fprintf(stderr, "io_transfer_mpsse(): bitindex error - has value of %d but should be 0 here!\n", bi);
	    }
	    // handle the byte command
	    // - next two command bytes are the (byte length - 1) with LSB first
	    // - followed by the data bytes
	    len = (unsigned int) (*cmdp++);
	    len |= ((unsigned int) (*cmdp++)) << 8;
	    len += 1;
	    cmdp += len;		/* advance to next command */
	    TDOp = memcpy(TDOp,rxp,len);
	    rxp += len;
	}
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
// TMSp - pointer to byte where bits are to be pulled, lsb first
// TDIp - pointer to byte where bits are to be pulled, lsb first
// bits - number of bits to send (1 .. 8)
// cmdp - pointer to cmd buffer to write command bytes
// cmdszp - the number of bytes added to cmdp will be returned here
// rxszp - the number of bytes to expect in the read bytes from these added commands will be returned here
//
void io_build_cmd_bits(const unsigned char *TMSp, const unsigned char *TDIp, int bits, unsigned char *cmdp, int *cmdszp, int *rxszp)
{
    if (bits > 8 || bits < 1) {
	fprintf(stderr, "io_build_cmd_bits(): requested invalid bits number: %d\n", bits);
	*cmdszp = 0;
	*rxszp = 0;
    } else {
    
	// Seperate bit streams where TMS is '1' and where it is
	// '0'. Where TMS is '1', TDI must be the same value for the
	// MPSSE command due to how badly constructed the MPSSE
	// commands are (why is it this way?)
	unsigned char TMS = *TMSp;
	unsigned char TDI = *TDIp;
	int i;			/* will be used to search for number of bits in bit stream for next MPSSE command */
	    
	while (bits > 0) {
	    i = 1;		/* reset i to 1 at the start of each loop */
	    if (TMS & 0x01) {
		// lsb of TMS is '1', so send a TMS command.
		//
		// search through TMS bits looking for how long the section of '1's is.
		// TDI must be the same value during this section or else stop looking.
		// TMS command can only send 7 bits, so stop search if i == 7
		while (((bits-i) > 0) && (i < 7) && ((TMS & BITMASK(i+1)) == BITMASK(i+1)) &&
		       ((TDI & BITMASK(i+1)) == 0 || (TDI & BITMASK(i+1)) == BITMASK(i+1))) {
		    i++;			    
		}
		// TMS is all 1's, so send TMS command with a single TDI that remains in this stat during TMS's
		*cmdp++ = RW_BITS_TMS_PVE_NVE; /* write TDI/TMS on falling TCK, read TDO on rising TCK */
		*cmdp++ = i - 1;	/* 1 bits -> 0, 7 bits -> 6 */
		*cmdp++ = ((TDI & 0x01) << 7) | (TMS & BITMASK(i));
		*cmdszp += 3;
		*rxszp += 1;		/* expect 1 byte to be read in response to this command */
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
	    }

	    // advance cmd, res, bits, TMS & TDI
	    bits -= i;
	    TMS >>= i;
	    TDI >>= i;		
	}

    }

}


int io_set_freq(unsigned int frequency)
{
    const unsigned int BUS_CLOCK_BASE = 6000000;
    const unsigned int BUS_CLOCK_HIGH = 30000000;
    const unsigned int max_freq =  (BUILD_FOR_H_SERIES ? BUS_CLOCK_HIGH : BUS_CLOCK_BASE);
    unsigned int divisor, actual_freq;
    unsigned char divcode;
    unsigned char buf[6];
    int cnt, res;
    
    if (frequency > max_freq) {
	fprintf(stderr, "Unsupported frequency: %u Hz\n", frequency);
	return -255;
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
#ifdef BUILD_FOR_H_SERIES
    buf[cnt++] = divcode;
#endif
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

int io_init(int product, int vendor, int verbosity=0)
{
    int res, len;
    unsigned char buf[16];

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
        
    res = ftdi_usb_open(&ftdi, vendor, product);
        
    if (res < 0)
    {
        fprintf(stderr, "ftdi_usb_open(0x%04x, 0x%04x): %d (%s)\n", vendor, product, res, ftdi_get_error_string(&ftdi));
        ftdi_deinit(&ftdi);
        return 1;
    }


    // @@@ Not sure if this is needed
    res = ftdi_usb_reset(&ftdi);
    if (res < 0) {
        fprintf(stderr, "Unable to reset FTDI device: %d (%s).\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }

    // @@@ Not sure if this is needed
    res = ftdi_set_latency_timer(&ftdi, 2);
    if (res < 0) {
	fprintf(stderr, "Unable to set latency timer: %d (%s).\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }

    // @@@ Not sure if this is needed
    res = ftdi_write_data_set_chunksize(&ftdi, FTDI_WRITE_CHUNK_SIZE 256); // Saw 65535 used
    if (res) {
	fprintf(stderr, "Unable to set write chunk size: %d (%s).\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
    
    // @@@ Not sure if this is needed
    res = ftdi_read_data_set_chunksize(&ftdi, FTDI_READ_CHUNK_SIZE  256); // Saw 65535 used
    if (res) {
	fprintf(stderr, "Unable to set read chunk size: %d (%s).\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }

#if 0    
    // RESET FTDI
    res = ftdi_set_bitmode(&ftdi, IO_OUTPUT, BITMODE_RESET);

    if (res < 0) 
    {
        fprintf(stderr, "ftdi_set_bitmode: %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
#endif
    
    // Set the USB read and write timeouts
    ftdi.usb_read_timeout = 120000;
    ftdi.usb_write_timeout = 120000;

#if 0    
    res = ftdi_usb_purge_buffers(&ftdi);        
    if (res < 0)
    {
        fprintf(stderr, "ftdi_usb_purge_buffers %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
        
    // Disable event and error characters
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
#endif
    
    // Enable MPSSE mode
    res = ftdi_set_bitmode(&ftdi, IO_OUTPUT, BITMODE_MPSSE);

    if (res < 0) 
    {
        fprintf(stderr, "ftdi_set_bitmode: %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }


    /* Give the chip a few mS to initialize */
    //@@@usleep(25000);
    
    res = ftdi_usb_purge_buffers(&ftdi);        
    if (res < 0)
    {
        fprintf(stderr, "ftdi_usb_purge_buffers %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }

    res = io_set_freq(2000000);
    if (res < 0)
    {
        fprintf(stderr, "io_set_frequency %d\n", res);
        io_close();
        return 1;
    }

#if 0    
    //@@@res = ftdi_set_baudrate(&ftdi, 750000); /* Automatically Multiplied by 4 */
    res = ftdi_set_baudrate(&ftdi, 50000); /* Measured to be a 2MHz TCK frequency */
        
    if (res < 0)
    {
        fprintf(stderr, "ftdi_set_baudrate %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
#endif
    
    // Update state of outputs to the default
    //@@@printf("Setting initial outputs\n");
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
    
    printf("FTDI ready with MPSSE mode\n");

    return 0;
}


int io_scan(const unsigned char *TMS, const unsigned char *TDI, unsigned char *TDO, int bits)
{
    static unsigned char cmdbuf[2*FTDI_WRITE_CHUNK_SIZE];
    unsigned char *TDOstart = TDO;
    int numTDOBytes = (bits+7) / 8;     // Number of TDO bytes to be received

    //@@@int i, res; 
    int res, len;
    int cmdBytes = 0, rxBytes = 0;
    
#ifndef USE_ASYNC
#error no async
    int r, t;
#else 
#ifdef USE_LIBFTDI1
    void *vres;
#else
    /* declarations for USE_ASYNC go here */
#endif
#endif

    printf("io_scan() of %d bits\n", bits);

    // Clear out TDO bits to make it easier to assemble (likely NOT necessary)
    //@@@memset(TDO, 0, (bits + 7) / 8);
    
#if 0    
    // Send a test of 6 bits with TMS high and 1 last bit with TMS low
    // while TDI remains low the entire time.
    buffer[0] = RW_BITS_TMS_PVE_NVE;
    buffer[1] = 7 - 1;		/* 7 bits */
    buffer[2] = 0x03f;
    len = 3;
    res = ftdi_write_data(&ftdi, buffer, len);
    if (res != len)
    {
	fprintf(stderr, "ftdi_write_data() for 0x%x: %d (%s)\n", buffer[0], res, ftdi_get_error_string(&ftdi));
        return -1;
    }

    printf("Reading back response\n");
    res = io_read_data (buffer, 1);
    printf("FTDI Response: 0x%02x\n",buffer[0]);
    if (res < 0) {
        fprintf(stderr, "read failed, error %d (%s)\n", res, ftdi_get_error_string(&ftdi));
    } else if (buffer[0] == 0xfa) {
        fprintf(stderr, "Invalid FTDI MPSSE command at %d\n",buffer[1]);
    }
#endif

    cmdp = cmdbuf;
    while (bits > 0) {
	int cmdsz, rxsz;
	// @@@ At first, send to FTDI using bit mode for every byte
	nextBits = (bits > 8) ? 8 : bits;
	io_build_cmd_bits(TMS, TDI, nextBits, cmdp, &cmdsz, &rxsz);
	TMS++;
	TDI++;
	
	if ((cmdBytes + cmdsz) > FTDI_WRITE_CHUNK_SIZE) {
	    // cannot fit in the new commands without exceeding
	    // FTDI_WRITE_CHUNK_SIZE, so send on cmdbuf and then
	    // restart at beginning of buffer with the commands that
	    // we just received.
	    res = io_transfer_mpsse(cmdbuf, cmdBytes, rxBytes, &TDO);
	    if (res != rxBytes) {
		fprintf(stderr, "Error transferring data with FTDI: %d (%s)\n", res, ftdi_get_error_string(&ftdi));
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
	bits -= nextBits;
    }

    if (cmdBytes > 0) {
	// handle the remaining command bytes
	res = io_transfer_mpsse(cmdbuf, cmdBytes, rxBytes, &TDO);
	if (res != rxBytes) {
	    fprintf(stderr, "Error transferring data with FTDI: %d (%s)\n", res, ftdi_get_error_string(&ftdi));
	    return -1;
	}
    }

    // check that did not over blow TDO array
    if ((TDO - TDOstart) > numTDOBytes) {
	fprintf(stderr, "io_scan(): wrote too many bytes into TDO! Exp.: %d  Act. %d\n", numTDOBytes, (TDO - TDOstart));
    }
    
#if 0    
    if (bits > sizeof(buffer)/2)
    {
        fprintf(stderr, "FATAL: out of buffer space for %d bits\n", bits);
        return -1;
    }
        
    for (i = 0; i < bits; ++i)
    {
        unsigned char v = IO_DEFAULT_OUT;
        if (TMS[i/8] & (1<<(i&7)))
            v |= PORT_TMS;
        if (TDI[i/8] & (1<<(i&7)))
            v |= PORT_TDI;
        buffer[i * 2 + 0] = v;
        buffer[i * 2 + 1] = v | PORT_TCK;
    }

#ifndef USE_ASYNC
    r = 0;
        
    while (r < bits * 2)
    {
        t = bits * 2 - r;
        if (t > FTDI_MAX_WRITESIZE)
            t = FTDI_MAX_WRITESIZE;
                
        printf("writing %d bytes\n", t);
        res = ftdi_write_data(&ftdi, buffer + r, t);

        if (res != t)
        {
            fprintf(stderr, "ftdi_write_data %d (%s)\n", res, ftdi_get_error_string(&ftdi));
            return -1;
        }
                
        i = 0;
                
        while (i < t)
        {
            res = ftdi_read_data(&ftdi, buffer + r + i, t - i);

            if (res < 0)
            {
                fprintf(stderr, "ftdi_read_data %d (%s)\n", res, ftdi_get_error_string(&ftdi));
                return -1;
            }
                        
            i += res;
        }
                
        r += t;
    }
#else
#ifdef USE_LIBFTDI1
    vres = ftdi_write_data_submit(&ftdi, buffer, bits * 2);
    if (!vres)
    {
        fprintf(stderr, "ftdi_write_data_submit (%s)\n", ftdi_get_error_string(&ftdi));
        return -1;
    }
#else
    res = ftdi_write_data_async(&ftdi, buffer, bits * 2);
    if (res < 0)
    {
        fprintf(stderr, "ftdi_write_data_async %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        return -1;
    }
#endif

    i = 0;
        
    while (i < bits * 2)
    {
        res = ftdi_read_data(&ftdi, buffer + i, bits * 2 - i);

        if (res < 0)
        {
            fprintf(stderr, "ftdi_read_data %d (%s)\n", res, ftdi_get_error_string(&ftdi));
            return -1;
        }
                
        i += res;
    }
#endif

    memset(TDO, 0, (bits + 7) / 8);
        
    for (i = 0; i < bits; ++i)
    {
        if (buffer[i * 2 + 1] & PORT_TDO)
        {
            TDO[i/8] |= 1 << (i&7);
        }
    }

#endif
    
    return 0;
}

void io_close(void)
{
    ftdi_set_bitmode(&ftdi, 0, BITMODE_RESET);
    ftdi_usb_close(&ftdi);
    ftdi_deinit(&ftdi);
}
