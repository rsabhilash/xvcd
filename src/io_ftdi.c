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

//@@@#define FTDI_BAUDRATE (750000)  /* Have not measured this, yet */
#define FTDI_BAUDRATE (50000)  /* Measured to be a 2MHz TCK frequency */

struct ftdi_context ftdi;

void io_close(void);

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
    int res;
    unsigned char buf[1];

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
        
    res = ftdi_usb_open_desc_index(&ftdi, vendor, product, NULL, serial, index);
        
    if (res < 0)
    {
        fprintf(stderr, "ftdi_usb_open(0x%04x, 0x%04x): %d (%s)\n", vendor, product, res, ftdi_get_error_string(&ftdi));
        ftdi_deinit(&ftdi);
        return 1;
    }
        
    ftdi_set_bitmode(&ftdi, 0xFF, BITMODE_CBUS);
    res = ftdi_set_bitmode(&ftdi, IO_OUTPUT, BITMODE_SYNCBB);

    if (res < 0) 
    {
        fprintf(stderr, "ftdi_set_bitmode: %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
        
    // Update state of outputs to the default
    buf[0] = IO_DEFAULT_OUT;
    res = ftdi_write_data(&ftdi, buf, 1);
    if (res < 0) 
    {
        fprintf(stderr, "write failed for 0x%x, error %d (%s)\n",buf[0], res, ftdi_get_error_string(&ftdi));
    }
        
    res = ftdi_usb_purge_buffers(&ftdi);
        
    if (res < 0)
    {
        fprintf(stderr, "ftdi_usb_purge_buffers %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
        
    res = ftdi_set_baudrate(&ftdi, FTDI_BAUDRATE);
        
    if (res < 0)
    {
        fprintf(stderr, "ftdi_set_baudrate %d (%s)\n", res, ftdi_get_error_string(&ftdi));
        io_close();
        return 1;
    }
        
    return 0;
}

// period - desired JTAG TCK period in ns
//
// return: if error, return an error code < 0
//         if success, return the actual period in ns
//
int io_set_period(unsigned int period)
{
    int actPeriod;

    actPeriod = 1000000000 / (FTDI_BAUDRATE * 40);
    
    return  actPeriod;
}


int io_scan(const unsigned char *TMS, const unsigned char *TDI, unsigned char *TDO, int bits)
{
    unsigned char buffer[2*16384];
    int i, res; 
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

    return 0;
}

void io_close(void)
{
    ftdi_usb_close(&ftdi);
    ftdi_deinit(&ftdi);
}
