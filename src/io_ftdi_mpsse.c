#include <stdio.h>
#include <ftdi.h>
#include <string.h>

#include "io_ftdi.h"

// NOTE: Merged in changes from https://github.com/ObKo/xvcd to support libftdi1

#define PORT_TCK            0x01
#define PORT_TDI            0x02
#define PORT_TDO            0x04
#define PORT_TMS            0x08
#define PORT_MISC           0x90
#define IO_OUTPUT (PORT_MISC|PORT_TCK|PORT_TDI|PORT_TMS)

#define IO_DEFAULT_OUT     (0xe0)               /* Found to work best for some FTDI implementations */

#define USE_ASYNC
#undef  USE_LIBFTDI1

#ifndef USE_ASYNC
#define FTDI_MAX_WRITESIZE 256
#endif

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

struct ftdi_context ftdi;

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

    if (timeout <= 0) {
	fprintf(stderr, "ftdi_read_data Timeout!\n");
    }
    
    return i;
}

#define BUILD_FOR_H_SERIES (-1)
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

#if 0    
    printf("Reading back response\n");
    res = io_read_data (buf, 2);
    printf("FTDI Response: 0x%02x 0x%02x\n",buf[0], buf[1]);
    if (res < 0) {
        fprintf(stderr, "read failed, error %d (%s)\n", res, ftdi_get_error_string(&ftdi));
	return res;
    } else if (buf[0] == 0xfa) {
        fprintf(stderr, "Invalid FTDI MPSSE command at %d\n",buf[1]);
	return -253;
    }
#endif
    
    res = ftdi_usb_purge_rx_buffer(&ftdi);
    if (res < 0)
    {
        fprintf(stderr, "ftdi_usb_purge_rx_buffers %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        return res;
    }

    return (int) actual_freq;
}

int io_init(int product, int vendor)
{
    int res, len;
    unsigned char buf[16];

    if (product < 0)
        product = 0x6010;
    if (vendor < 0)
        vendor = 0x0403;
        
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
    res = ftdi_write_data_set_chunksize(&ftdi, 256); // Saw 65535 used
    if (res) {
	fprintf(stderr, "Unable to set write chunk size: %d (%s).\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
    
    // @@@ Not sure if this is needed
    res = ftdi_read_data_set_chunksize(&ftdi, 256); // Saw 65535 used
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

#if 0    
    printf("Reading back response\n");
    res = io_read_data (buf, 2);
    printf("FTDI Response: 0x%02x 0x%02x\n",buf[0], buf[1]);
    if (res < 0) {
        fprintf(stderr, "read failed, error %d (%s)\n", res, ftdi_get_error_string(&ftdi));
    } else if (buf[0] == 0xfa) {
        fprintf(stderr, "Invalid FTDI MPSSE command at %d\n",buf[1]);
    }
#endif
    
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

#if 0    
    printf("Reading back response\n");
    res = io_read_data (buf, 2);
    printf("FTDI Response: 0x%02x 0x%02x\n",buf[0], buf[1]);
    if (res < 0) {
        fprintf(stderr, "read failed, error %d (%s)\n", res, ftdi_get_error_string(&ftdi));
    } else if (buf[0] == 0xfa) {
        fprintf(stderr, "Invalid FTDI MPSSE command at %d\n",buf[1]);
    }
#endif    

    printf("FTDI ready with MPSSE mode\n");

    return 0;
}

int io_scan(const unsigned char *TMS, const unsigned char *TDI, unsigned char *TDO, int bits)
{
    unsigned char buffer[2*16384];
    //@@@int i, res; 
    int res, len; 
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
