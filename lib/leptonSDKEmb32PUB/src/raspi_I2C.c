/*******************************************************************************
**
**    File NAME: jova_I2C.c
**
**      AUTHOR:  Hart Thomson
**
**      CREATED: 9/25/2012
**  
**      DESCRIPTION: Lepton Device-Specific Driver for the JOVA
**                   Master I2C
**
**      HISTORY:  9/25/2012 HT - Initial Draft 
**
** Copyright 2010, 2011, 2012, 2013 FLIR Systems - Commercial Vision Systems
**
**  All rights reserved.
**
**  Redistribution and use in source and binary forms, with or without
**  modification, are permitted provided that the following conditions are met:
**
**  Redistributions of source code must retain the above copyright notice, this
**  list of conditions and the following disclaimer.
**
**  Redistributions in binary form must reproduce the above copyright notice,
**  this list of conditions and the following disclaimer in the documentation
**  and/or other materials provided with the distribution.
**
**  Neither the name of the Indigo Systems Corporation nor the names of its
**  contributors may be used to endorse or promote products derived from this
**  software without specific prior written permission.
**
**  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
**  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
**  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
**  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
**  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
**  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
**  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
**  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
**  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
**  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
**  THE POSSIBILITY OF SUCH DAMAGE.
**
*******************************************************************************/
/******************************************************************************/
/** INCLUDE FILES                                                            **/
/******************************************************************************/

#include "LEPTON_Types.h"
#include "LEPTON_ErrorCodes.h"
#include "LEPTON_Macros.h"
#include "raspi_I2C.h"
#include "LEPTON_I2C_Reg.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <errno.h>

//#define _AVR_STK600_ATXMEGA128A1_BOARD

//#ifdef _AVR_STK600_ATXMEGA128A1_BOARD
//    #include "twiDriver.h"
//#endif
/******************************************************************************/
/** LOCAL DEFINES                                                            **/
/******************************************************************************/
//Raspi handle;
const LEP_INT32 ADDRESS_SIZE_BYTES = 2;
const LEP_INT32 VALUE_SIZE_BYTES = 2;
float clk_rate;
const LEP_INT32 comm_timeout_ms = 500;
const u_int8_t DeviceAddress = 0x2a;
/* module-global file descriptor for /dev/i2c-X */
static int leptonDevice = -1;

/**
 * Initializes the I2C master interface.
 *
 * @param portID        User-defined port ID (ignored unless you want mapping)
 * @param deviceAddress Lepton device address (0x2A)
 *
 * @return LEP_RESULT   LEP_OK on success, error otherwise.
 */
LEP_RESULT DEV_I2C_MasterInit(LEP_UINT16 portID,
                                         LEP_UINT16 *BaudRate)
{
    (void)BaudRate;   // unused, but kept for API compatibility

    /* Already open? Close it first */
    if (leptonDevice >= 0) {
        close(leptonDevice);
        leptonDevice = -1;
    }

    /* Open Linux I2C device */
    const char *i2c_path = "/dev/i2c-1";   // Adjust if needed

    leptonDevice = open(i2c_path, O_RDWR);
    if (leptonDevice < 0) {
        return LEP_ERROR_I2C_FAIL;
    }

    /* Check that device responds */
    if (ioctl(leptonDevice, I2C_SLAVE, DeviceAddress) < 0) {
        close(leptonDevice);
        leptonDevice = -1;
        return LEP_ERROR_I2C_FAIL;
    }

    return LEP_OK;
}

/**
 * Closes the I2C driver connection.
 *
 * @return LEP_RESULT  0 if all goes well, errno otherwise.
 */
LEP_RESULT DEV_I2C_MasterClose()
{
    if (leptonDevice >= 0) {
        close(leptonDevice);
        leptonDevice = -1;
    }
    return LEP_OK;
}

/**
 * Resets the I2C driver back to the READY state.
 *
 * @return LEP_RESULT  0 if all goes well, errno otherwise.
 */
LEP_RESULT DEV_I2C_MasterReset(void)
{
    /* Basic reset: close FD if open.  Actual re-open should be done by caller. */
    if (leptonDevice >= 0) {
        close(leptonDevice);
        leptonDevice = -1;
    }
    return LEP_OK;
}

LEP_RESULT DEV_I2C_MasterReadData(LEP_UINT16  portID,               // User-defined port ID
                                  LEP_UINT8   deviceAddress,        // Lepton Camera I2C Device Address
                                  LEP_UINT16  regAddress,           // Lepton Register Address
                                  LEP_UINT16 *readDataPtr,          // Read DATA buffer pointer
                                  LEP_UINT16  wordsToRead,          // Number of 16-bit words to Read
                                  LEP_UINT16 *numWordsRead,         // Number of 16-bit words actually Read
                                  LEP_UINT16 *status                // Transaction Status
                                 )
{

    for (LEP_UINT16 i = 0; i < wordsToRead; ++i)
        {
            u_int8_t read_buf[2] = {0};
            if (read(leptonDevice, read_buf, sizeof(read_buf)) != sizeof(read_buf))
            {
                return LEP_ERROR;
            }

        readDataPtr[i] = read_buf[0] << 8 | read_buf[1];
        (*numWordsRead)++;
    }
    return LEP_OK;
}

LEP_RESULT DEV_I2C_MasterWriteData(LEP_UINT16  portID,              // User-defined port ID
                                   LEP_UINT8   deviceAddress,       // Lepton Camera I2C Device Address
                                   LEP_UINT16  regAddress,          // Lepton Register Address
                                   LEP_UINT16 *writeDataPtr,        // Write DATA buffer pointer
                                   LEP_UINT16  wordsToWrite,        // Number of 16-bit words to Write
                                   LEP_UINT16 *numWordsWritten,     // Number of 16-bit words actually written
                                   LEP_UINT16 *status)              // Transaction Status
{
    for (LEP_UINT16 i = 0; i < wordsToWrite; ++i) {
        u_int8_t write_buf[4] = {
            regAddress >> 8 & 0xff,
            regAddress & 0xff,
            writeDataPtr[i] >> 8 & 0xff,
            writeDataPtr[i] & 0xff
        };

        if (write(leptonDevice, write_buf, sizeof(write_buf)) != sizeof(write_buf)) {
            return LEP_ERROR;
        }
        regAddress+=2;
    }
    return LEP_OK;
}

LEP_RESULT DEV_I2C_MasterReadRegister( LEP_UINT16 portID,
                                       LEP_UINT8  deviceAddress,
                                       LEP_UINT16 regAddress,
                                       LEP_UINT16 *regValue,     // Number of 16-bit words actually written
                                       LEP_UINT16 *status
                                     )
{
    LEP_RESULT result = LEP_OK;
    LEP_UINT16 wordsActuallyRead = 0;

    result = DEV_I2C_MasterReadData(portID, deviceAddress, regAddress, regValue, 1 /*1 word*/, &wordsActuallyRead, status);

    return result;
}

LEP_RESULT DEV_I2C_MasterWriteRegister( LEP_UINT16 portID,
                                        LEP_UINT8  deviceAddress,
                                        LEP_UINT16 regAddress,
                                        LEP_UINT16 regValue,     // Number of 16-bit words actually written
                                        LEP_UINT16 *status
                                      )
{
   LEP_RESULT result = LEP_OK;
   LEP_UINT16 wordsActuallyWritten = 0;

   result = DEV_I2C_MasterWriteData(portID, deviceAddress, regAddress, &regValue, 1, &wordsActuallyWritten, status);

   return result;
}

LEP_RESULT DEV_I2C_MasterStatus(void )
{
    LEP_RESULT result = LEP_OK;
    /* Could optionally return whether leptonDevice >= 0, etc. */
    if (leptonDevice < 0) {
        result = LEP_ERROR_I2C_FAIL;
    }
    return(result);
}
