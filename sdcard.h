/******************************************************************************
 * Copyright (C) 2017 Marco Giammarini
 *
 * Authors:
 *  Marco Giammarini <m.giammarini@warcomeb.it>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 ******************************************************************************/

/******************************************************************************
 * @mainpage SDCard/SPI library based on @a libohiboard
 *
 * @section changelog ChangeLog
 *
 * @li v1.0 of 2017/07/07 - First release
 *
 * @section library External Library
 *
 * The library use the following external library
 * @li libohiboard https://github.com/ohilab/libohiboard a C framework for
 * NXP Kinetis microcontroller
 * @li timer https://github.com/warcomeb/timer small programmable library for
 * generate an internal tick
 *
 * @section thanksto Thanks to...
 *
 * The library is based on the great description of MMC/SD protocol written
 * by Elm Chan in this page http://elm-chan.org/docs/mmc/mmc_e.html
 *
 ******************************************************************************/

#ifndef __WARCOMEB_SDCARD_H
#define __WARCOMEB_SDCARD_H

#define WARCOMEB_SDCARD_LIBRARY_VERSION     "1.0"
#define WARCOMEB_SDCARD_LIBRARY_VERSION_M   1
#define WARCOMEB_SDCARD_LIBRARY_VERSION_m   0
#define WARCOMEB_SDCARD_LIBRARY_TIME        1499427261

#include "libohiboard.h"

#ifndef __NO_BOARD_H
#include "board.h"
#endif

#ifdef WARCOMEB_SDCARD_DEBUG
#include "../cli/cli.h"
#endif

typedef enum _SDCard_Errors
{
    SDCARD_ERRORS_OK,
    SDCARD_ERRORS_CARD_NOT_PRESENT,
    SDCARD_ERRORS_CARD_NOT_DETECTED,

    SDCARD_ERRORS_COMMAND_TIMEOUT,
    SDCARD_ERRORS_COMMAND_FAILED,
    SDCARD_ERRORS_TIMEOUT,                        /**< Generic timeout errors */

    SDCARD_ERRORS_INIT_FAILED,

    SDCARD_ERRORS_WRITE_BLOCK_FAILED,
    SDCARD_ERRORS_READ_BLOCK_FAILED,

    SDCARD_ERRORS_WRITE_BLOCKS_FAILED,
    SDCARD_ERRORS_READ_BLOCKS_FAILED,

    SDCARD_ERRORS_ERASE_BLOCKS_FAILED
} SDCard_Errors;

typedef enum _SDCard_PresentType
{
    SDCARD_PRESENTTYPE_LOW  = 0,
    SDCARD_PRESENTTYPE_HIGH = 1,
} SDCard_PresentType;

typedef struct _SDCard_Device
{
    Spi_DeviceHandle   device;
    Gpio_Pins          csPin;

    Gpio_Pins          cpPin;                           /**< Card Present pin */
    SDCard_PresentType cpType;

    bool               isSDHC;
    uint8_t            cardVersion;
    uint8_t            cardType;

    void (*delayTime)(uint32_t delay);       /**< Function for blocking delay */
    uint32_t (*currentTime)(void);             /**< Function for current time */

    bool               isInit;
} SDCard_Device;

/**
 * @brief
 *
 * @param[in] dev
 * @return
 */
SDCard_Errors SDCard_init (SDCard_Device* dev);

/**
 * This function control if there is some pending write operations.
 *
 * @param[in] dev
 * @return TRUE if the SDC is busy, FALSE otherwise.
 */
bool SDCard_isBusy(SDCard_Device* dev);

/**
 * This function control if the SDC is present in the socket.
 *
 * @param[in] dev
 * @return TRUE if the SDC is present in the socket, FALSE otherwise.
 */
bool SDCard_isPresent(SDCard_Device* dev);

/**
 * @brief
 *
 * @param[in] dev
 * @param[in] blockAddress
 * @param[in] data
 * @return
 */
SDCard_Errors SDCard_writeBlock (SDCard_Device* dev,
                                 uint32_t blockAddress,
                                 const uint8_t* data);

/**
 * @brief
 *
 * @param[in] dev
 * @param[in] blockAddress
 * @param[in] data
 * @param[in] count Number of sectors to write (1 to 128)
 * @return
 */
SDCard_Errors SDCard_writeBlocks (SDCard_Device* dev,
                                  uint32_t blockAddress,
                                  const uint8_t* data,
                                  uint8_t count);

/**
 * @brief
 *
 * @param[in] dev
 * @param[in] blockAddress
 * @param[out] data
 * @return
 */
SDCard_Errors SDCard_readBlock (SDCard_Device* dev,
                                uint32_t blockAddress,
                                uint8_t* data);

/**
 * @brief
 *
 * @param[in] dev
 * @param[in] blockAddress
 * @param[out] data
 * @param[in] count Number of sectors to read (1 to 128)
 * @return
 */
SDCard_Errors SDCard_readBlocks (SDCard_Device* dev,
                                 uint32_t blockAddress,
                                 uint8_t* data,
                                 uint8_t count);

/**
 * @brief
 *
 * @param[in] dev
 * @param[in] blockAddress
 * @param[in] count
 * @return
 */
SDCard_Errors SDCard_eraseBlocks (SDCard_Device* dev,
                                  uint32_t blockAddress,
                                  uint32_t count);

/**
 * @brief
 *
 * @param[in] dev
 * @param[out] size The number of sectors of the card
 * @return
 */
SDCard_Errors SDCard_getSectorCount (SDCard_Device* dev,
                                     uint32_t* size);


#endif /* __WARCOMEB_SDCARD_H */
