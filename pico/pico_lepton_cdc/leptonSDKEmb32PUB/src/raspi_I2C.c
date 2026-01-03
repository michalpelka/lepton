#include <LEPTON_Types.h>
#include <LEPTON_ErrorCodes.h>
#include <stdio.h>

#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include <stdlib.h>
#include <string.h>

/* Add near top of `leptonSDKEmb32PUB/src/raspi_I2C.c` */
static bool pico_i2c_initialized = false;

/* Initialize Raspberry Pi Pico (RP2040) I2C */
LEP_RESULT DEV_I2C_MasterInit(LEP_UINT16 portID, LEP_UINT16 *BaudRate)
{
    (void)portID;
    uint32_t baud = 1000000; /* default 100kHz */
    if (BaudRate && *BaudRate) baud = *BaudRate;

    if (pico_i2c_initialized) return LEP_OK;

#if defined(PICO_DEFAULT_I2C_SDA_PIN) && defined(PICO_DEFAULT_I2C_SCL_PIN)
    i2c_init(i2c_default, baud);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
    pico_i2c_initialized = true;
    return LEP_OK;
#else
    return LEP_ERROR_I2C_FAIL;
#endif

}

/* Close (no-op for Pico, clear flag) */
LEP_RESULT DEV_I2C_MasterClose()
{
    pico_i2c_initialized = false;
    return LEP_OK;
}

/* Read wordsToRead 16-bit words starting at regAddress using combined write(reg) + read(data) */
LEP_RESULT DEV_I2C_MasterReadData(LEP_UINT16  portID,
                                  LEP_UINT8   deviceAddress,
                                  LEP_UINT16  regAddress,
                                  LEP_UINT16 *readDataPtr,
                                  LEP_UINT16  wordsToRead,
                                  LEP_UINT16 *numWordsRead,
                                  LEP_UINT16 *status)
{
    (void)portID;
    if (status) *status = 0;
    if (!readDataPtr || !numWordsRead) return LEP_ERROR;
    *numWordsRead = 0;
    if (!pico_i2c_initialized) return LEP_ERROR_I2C_FAIL;

    size_t bytes_to_read = (size_t)wordsToRead * 2;
    uint8_t reg_buf[2] = { (uint8_t)(regAddress >> 8), (uint8_t)(regAddress & 0xFF) };

    /* write register address without stop, then read the data with stop */
    const absolute_time_t deadline1 = make_timeout_time_ms (100);
    int written = i2c_write_blocking_until(i2c_default, deviceAddress, reg_buf, 2, true, deadline1);
    if (written != 2) return LEP_ERROR_I2C_FAIL;

    uint8_t *rx = (uint8_t*)malloc(bytes_to_read);
    if (!rx) return LEP_ERROR;

    const absolute_time_t deadline2 = make_timeout_time_ms (1000);
    int read_bytes = i2c_read_blocking_until(i2c_default, deviceAddress, rx, bytes_to_read, false, deadline2);
    if (read_bytes != (int)bytes_to_read) {
        free(rx);
        printf("Read failed: %d bytes read, expected %d\n", read_bytes, (int)bytes_to_read);
        return LEP_ERROR_I2C_FAIL;
    }

    for (LEP_UINT16 i = 0; i < wordsToRead; ++i) {
        readDataPtr[i] = ((LEP_UINT16)rx[2*i] << 8) | (LEP_UINT16)rx[2*i + 1];
        (*numWordsRead)++;
    }

    free(rx);
    return LEP_OK;
}

/* Write wordsToWrite 16-bit words starting at regAddress: single write (reg + data) */
LEP_RESULT DEV_I2C_MasterWriteData(LEP_UINT16  portID,
                                   LEP_UINT8   deviceAddress,
                                   LEP_UINT16  regAddress,
                                   LEP_UINT16 *writeDataPtr,
                                   LEP_UINT16  wordsToWrite,
                                   LEP_UINT16 *numWordsWritten,
                                   LEP_UINT16 *status)
{
    (void)portID;
    if (status) *status = 0;
    if (!writeDataPtr || !numWordsWritten) return LEP_ERROR;
    *numWordsWritten = 0;
    if (!pico_i2c_initialized) return LEP_ERROR_I2C_FAIL;

    size_t payload_len = 2 + (size_t)wordsToWrite * 2;

    /* Use a small stack buffer for small writes (avoids malloc in ISR) */
    uint8_t tx[32]; /* adjust size if you need larger stack buffer */

    /* build payload: reg (big-endian) + data (big-endian) */
    tx[0] = (uint8_t)(regAddress >> 8);
    tx[1] = (uint8_t)(regAddress & 0xFF);
    for (LEP_UINT16 i = 0; i < wordsToWrite; ++i) {
        LEP_UINT16 w = writeDataPtr[i];
        tx[2 + 2*i]     = (uint8_t)((w >> 8) & 0xFF);
        tx[2 + 2*i + 1] = (uint8_t)(w & 0xFF);
    }

    /* Note: this is a blocking call — do NOT call from ISR. */
    const absolute_time_t deadline = make_timeout_time_ms(1000);
    int written = i2c_write_blocking_until(i2c_default, deviceAddress, tx, payload_len, false, deadline);


    if (written != (int)payload_len) {
        printf("Write failed: %d bytes written, expected %d");
        return LEP_ERROR_I2C_FAIL;
    }

    *numWordsWritten = wordsToWrite;
    return LEP_OK;
}

LEP_RESULT DEV_I2C_MasterReadRegister( LEP_UINT16 portID,
                                              LEP_UINT8  deviceAddress,
                                              LEP_UINT16 regAddress,
                                              LEP_UINT16 *regValue,     // Number of 16-bit words actually written
                                              LEP_UINT16 *status
                                            )
{
    (void)portID;
    if (status) *status = 0;
    if (!regValue) return LEP_ERROR;
    if (!pico_i2c_initialized) return LEP_ERROR_I2C_FAIL;

    uint8_t reg_buf[2] = { (uint8_t)(regAddress >> 8), (uint8_t)(regAddress & 0xFF) };

    const absolute_time_t deadline1 = make_timeout_time_ms(100);
    int written = i2c_write_blocking_until(i2c_default, deviceAddress, reg_buf, 2, true, deadline1);
    if (written != 2) return LEP_ERROR_I2C_FAIL;

    uint8_t rx[2];
    const absolute_time_t deadline2 = make_timeout_time_ms(100);
    int read_bytes = i2c_read_blocking_until(i2c_default, deviceAddress, rx, 2, false, deadline2);
    if (read_bytes != 2) return LEP_ERROR_I2C_FAIL;

    *regValue = (LEP_UINT16)((rx[0] << 8) | rx[1]);
    return LEP_OK;
}
