/******************************************************************************
 * Copyright (C) 2017-2018 Marco Giammarini
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

#include "sdcard.h"

#define SDCARD_WAIT_RETRY    10
#define SDCARD_MAX_RETRY     10

#define SDCARD_TIMEOUT_WRITE 500 // [ms]
#define SDCARD_TIMEOUT_READ  200 // [ms]
#define SDCARD_TIMEOUT_ERASE 30000 // [ms]

typedef enum _SDCard_Command
{
    /* Basic command set */
    SDCARD_COMMAND_0  = 0x40,                  /**< Reset cards to idle state */
    SDCARD_COMMAND_1  = 0x41,  /**< Read the OCR (MMC mode, DNU for SD cards) */
    SDCARD_COMMAND_8  = 0x48,          /**< Send SD card interface conditions */
    SDCARD_COMMAND_9  = 0x49,                         /**< Card sends the CSD */
    SDCARD_COMMAND_10 = 0x4A,                             /**< Card sends CID */
    SDCARD_COMMAND_12 = 0x4C,        /**< Stop a multiple block r/w operation */
    SDCARD_COMMAND_13 = 0x4D,   /**< Get the addressed card's status register */

    /* Block read commands: */
    SDCARD_COMMAND_16 = 0x50,                       /**< Set the block length */
    SDCARD_COMMAND_17 = 0x51,                          /**< Read single block */
    SDCARD_COMMAND_18 = 0x52,            /**< Read multiple block until CMD12 */

    /* Block write commands: */
    SDCARD_COMMAND_24 = 0x58,  /**< Write block of the size selected w/ CMD16 */
    SDCARD_COMMAND_25 = 0x59,         /**< Multiple block write until a CMD12 */
    SDCARD_COMMAND_27 = 0x5B,   /**< Program the programmable bits of the CSD */

    /* Write protection commands: */
    SDCARD_COMMAND_28 = 0x5C,       /**< Set protection bit of the addr group */
    SDCARD_COMMAND_29 = 0x5D,     /**< Clear protection bit of the addr group */
    SDCARD_COMMAND_30 = 0x5E,  /**< Ask for the status of the protection bits */

    /* Erase commands: */
    SDCARD_COMMAND_32 = 0x60,   /**< Set addr of the first block to be erased */
    SDCARD_COMMAND_33 = 0x61,    /**< Set addr of the last block to be erased */
    SDCARD_COMMAND_38 = 0x66,                   /**< Erase the selected block */

    /* Lock Card commands: */
    /* Commands from 42 to 54, not defined here! */

    /* Application-specific commands: */
    SDCARD_COMMAND_55 = 0x77,  /**< Flag next command is application-specific */
    SDCARD_COMMAND_56 = 0x78,   /**< General I/O for application-specific cmd */
    SDCARD_COMMAND_58 = 0x7A,               /**< Read the OCR (SPI mode only) */
    SDCARD_COMMAND_59 = 0x7B,                            /**< Turn CRC ON/OFF */

    SDCARD_COMMAND_A23 = 0x57,        /**< Set the number of erase block */
    SDCARD_COMMAND_A41 = 0x69,         /**< Get the card's OCR (SD mode) */

} SDCard_Command;

typedef enum _SDCard_Response
{
    SDCARD_RESPONSE_OK    = 0x00,
    SDCARD_RESPONSE_IDLE  = 0x01,
    SDCARD_RESPONSE_FAULT = 0x05
} SDCard_Response;

/**
 * The function wait a maximum time configured by the user and check if the
 * SDCard is ready.
 *
 * @param[in] timeout Max timeout to wait for board is ready [ms]
 * @return SDCARD_ERRORS_OK if the card is ready, SDCARD_ERRORS_TIMEOUT
 *         otherwise.
 */
static SDCard_Errors SDCard_waitReady (SDCard_Device* dev, uint16_t timeout)
{
    uint8_t response;
    uint32_t timer = dev->currentTime() + timeout;

    do
    {
        Spi_readByte(dev->device,&response);
    } while ((response != 0xFF) && (response != 0x00) && (dev->currentTime() < timer));

    return ((response == 0xFF) || (response == 0x00)) ? SDCARD_ERRORS_OK : SDCARD_ERRORS_TIMEOUT;
}

/**
 * The function close the SPI communication with SDCard.
 *
 * @param[in] dev An handle of the device
 */
static void SDCard_deselect (SDCard_Device* dev)
{
    uint8_t response;
    Gpio_set(dev->csPin);
    // Dummy cicle!
    Spi_readByte(dev->device,&response);
}

/**
 * The function enable the SPI communication and check if the SDCard is
 * available or not.
 *
 * @param[in] dev An handle of the device
 * @return TRUE if the device selection works, FALSE otherwise.
 */
static bool SDCard_select (SDCard_Device* dev)
{
    uint8_t response;
    Gpio_clear(dev->csPin);

    // WARNING: There are a lot of problem with this code for Kingstone card!!
#if 0
    // Dummy cicle!
    Spi_readByte(dev->device,&response);

    // Wait for card ready
    if (SDCard_waitReady(dev,500) == SDCARD_ERRORS_OK)
    {
        return TRUE;
    }
    else
    {
        SDCard_deselect(dev);
        return FALSE;
    }
#endif
}

/**
 * @param[in] dev An handle of the device
 * @param[in] dev The command to be sent
 */
static SDCard_Errors SDCard_sendCommand (SDCard_Device* dev,
                                         SDCard_Command cmd,
                                         uint32_t arguments,
                                         uint8_t* response)
{
    uint8_t retry = 0, currentResponse = 0;

    if (dev->isSDHC == FALSE)
    {
        if ((cmd == SDCARD_COMMAND_17) ||
            (cmd == SDCARD_COMMAND_18) ||
            (cmd == SDCARD_COMMAND_24) ||
            (cmd == SDCARD_COMMAND_25) ||
            (cmd == SDCARD_COMMAND_32) ||
            (cmd == SDCARD_COMMAND_33))
        {
            arguments <<= 9;
        }
    }

    // Select sd card
    SDCard_select(dev);

    // Send command
    Spi_writeByte(dev->device,cmd);

    // Send arguments
    Spi_writeByte(dev->device,(uint8_t) (arguments>>24));
    Spi_writeByte(dev->device,(uint8_t) (arguments>>16));
    Spi_writeByte(dev->device,(uint8_t) (arguments>>8));
    Spi_writeByte(dev->device,(uint8_t) arguments);

    // Send CRC: for CMD8 must be 0X87, indeed for CMD0 must be 0x95.
    if (cmd == SDCARD_COMMAND_8)
        Spi_writeByte(dev->device,0x87);
    else if (cmd == SDCARD_COMMAND_0)
        Spi_writeByte(dev->device,0x95);
    else
        Spi_writeByte(dev->device,0x01);

    // Discard following 1 byte - ONLY IN THIS CASE!
    if (cmd == SDCARD_COMMAND_12)
        Spi_readByte(dev->device,&currentResponse);

    // Receive response
    retry = SDCARD_WAIT_RETRY;
    do
    {
        Spi_readByte(dev->device,&currentResponse);
        retry--;
    } while ((currentResponse == 0xFF) && (retry > 0));

    if (retry == 0)
    {
        *response = 0xFF;
        SDCard_deselect(dev);
        return SDCARD_ERRORS_COMMAND_TIMEOUT;
    }

    *response = currentResponse;

    if ((cmd != SDCARD_COMMAND_58) &&
        (cmd != SDCARD_COMMAND_17) &&
        (cmd != SDCARD_COMMAND_18) &&
        (cmd != SDCARD_COMMAND_24) &&
        (cmd != SDCARD_COMMAND_25))
        SDCard_deselect(dev);
    return SDCARD_ERRORS_OK;
}

SDCard_Errors SDCard_init (SDCard_Device* dev)
{
    uint8_t i;
    uint8_t response, retry = 0;
    SDCard_Command command = 0;
    uint8_t ocr[4];
    uint32_t time;

    dev->isInit = FALSE;

    Gpio_config(dev->csPin,GPIO_PINS_OUTPUT);
    Gpio_set(dev->csPin);
    Gpio_config(dev->cpPin,GPIO_PINS_INPUT);

    if (Gpio_get(dev->cpPin) != dev->cpType)
    {
#ifdef WARCOMEB_SDCARD_DEBUG
    	Cli_sendMessage("SDCARD","Card not present",CLI_MESSAGETYPE_ERROR);
#endif
		return SDCARD_ERRORS_CARD_NOT_PRESENT;
    }

    // Send 120 dummy clocks
    for (i = 0; i < 15; ++i)
        Spi_writeByte(dev->device,0xFF);

    // Reset the card
    retry = 0;
    do
    {
		SDCard_sendCommand(dev,SDCARD_COMMAND_0,0,&response);
		retry++;
		if (retry > SDCARD_MAX_RETRY)
		{
			break;
		}
    	dev->delayTime(10);
    } while (response != SDCARD_RESPONSE_IDLE);

    // Try to understand the sdcard version and init
    SDCard_sendCommand(dev,SDCARD_COMMAND_8,0x000001AA,&response);

    if (response != SDCARD_RESPONSE_IDLE)
    {
        dev->cardVersion  = 1;

        SDCard_sendCommand(dev,SDCARD_COMMAND_A41,0x40000000,&response);

        // Select the correct command
        if (response <= 1)
        {
            // SDCARD v1
            dev->cardType = 1;
            command = SDCARD_COMMAND_A41;
        }
        else
        {
            // MMC v3
            dev->cardType = 3;
            command = SDCARD_COMMAND_1;
        }

        // Polling for command
        time = dev->currentTime() + 1000;
        do
        {
            SDCard_sendCommand(dev,command,0,&response);
        } while ((response != SDCARD_RESPONSE_OK) && (dev->currentTime() < time));

        if ((time < dev->currentTime()) || (response != SDCARD_RESPONSE_OK))
        {
            SDCard_deselect(dev);
            return SDCARD_ERRORS_INIT_FAILED;
        }

        SDCard_sendCommand(dev,SDCARD_COMMAND_16,0X00000200,&response);
        if (response != SDCARD_RESPONSE_OK)
        {
            SDCard_deselect(dev);
            return SDCARD_ERRORS_INIT_FAILED;
        }
    }
    else
    {
        dev->cardVersion = 2;

        // Polling card with CMD55 and ACMD41 until reply 0x00
        time = dev->currentTime() + 1000;
        do
        {
            SDCard_sendCommand(dev,SDCARD_COMMAND_55,0,&response);
            SDCard_sendCommand(dev,SDCARD_COMMAND_A41,0x40000000,&response);
            dev->delayTime(100);
        } while ((response != SDCARD_RESPONSE_OK) && (dev->currentTime() < time));

        if ((time < dev->currentTime()) || (response != SDCARD_RESPONSE_OK))
        {
            SDCard_deselect(dev);
#ifdef WARCOMEB_SDCARD_DEBUG
            Cli_sendMessage("SDCARD","CMD55/ACMD41 wrong reply or timeout",CLI_MESSAGETYPE_ERROR);
            Cli_sendMessage("SDCARD","initialization fail",CLI_MESSAGETYPE_ERROR);
#endif
            return SDCARD_ERRORS_INIT_FAILED;
        }

        // Check CCS bit into OCR of CMD58
        SDCard_sendCommand(dev,SDCARD_COMMAND_58,0,&response);
        if (response == SDCARD_RESPONSE_OK)
        {
            for (i = 0; i < 4; ++i) Spi_readByte(dev->device,&ocr[i]);
            if (ocr[0] & 0x40)
            {
                dev->isSDHC = TRUE;
            }
            else
            {
                // Close CMD58
                SDCard_deselect(dev);
                SDCard_sendCommand(dev,SDCARD_COMMAND_16,0X00000200,&response);
                if (response != SDCARD_RESPONSE_OK)
                {

                    return SDCARD_ERRORS_INIT_FAILED;
                }
            }
        }
        else
        {
            // Close CMD58
            SDCard_deselect(dev);
#ifdef WARCOMEB_SDCARD_DEBUG
            Cli_sendMessage("SDCARD","CMD58 wrong reply",CLI_MESSAGETYPE_ERROR);
            Cli_sendMessage("SDCARD","initialization fail",CLI_MESSAGETYPE_ERROR);
#endif
            return SDCARD_ERRORS_INIT_FAILED;
        }
    }

    dev->isInit = TRUE;
    // Must be just closed
    SDCard_deselect(dev);

#ifdef WARCOMEB_SDCARD_DEBUG
    Cli_sendMessage("SDCARD","card initialized!",CLI_MESSAGETYPE_INFO);
#endif

    return SDCARD_ERRORS_OK;
}

SDCard_Errors SDCard_writeBlock (SDCard_Device* dev,
                                 uint32_t blockAddress,
                                 const uint8_t* data)
{
    uint8_t response;
    uint8_t retry = 0;
    uint16_t i;

    // Send starting block with writing block command
    do
	{
        SDCard_sendCommand(dev,SDCARD_COMMAND_24,blockAddress,&response);
		retry++;
		if (retry > SDCARD_MAX_RETRY)
		{
		    // Close CMD24
	        SDCard_deselect(dev);
#ifdef WARCOMEB_SDCARD_DEBUG
            Cli_sendMessage("SDCARD","CMD24 write block fail",CLI_MESSAGETYPE_ERROR);
#endif
			return SDCARD_ERRORS_WRITE_BLOCK_FAILED;
		}
		dev->delayTime(10);
	} while (response != SDCARD_RESPONSE_OK);

    // Send TOKEN
    Spi_writeByte(dev->device,0xFE);

    // Send DATA
    for (i = 0; i < 512; ++i)
    {
        Spi_writeByte(dev->device,data[i]);
    }

    // Send dummy CRC
    Spi_writeByte(dev->device,0xFF);
    Spi_writeByte(dev->device,0xFF);

    // Read card reply
    // Every data block written to the card will be acknoledged by a
    // data response token. It is one byte long and has the following format:
    // X X X 0 STATUS 1, where status bits is defined as
    // 010 - Data accepted
    // 101 - Data rejected due to a CRC error
    // 110 - Data rejected due to a write error
    Spi_readByte(dev->device,&response);
    if ((response & 0x0F) != SDCARD_RESPONSE_FAULT)
    {
        // Close CMD24
        SDCard_deselect(dev);
#ifdef WARCOMEB_SDCARD_DEBUG
        Cli_sendMessage("SDCARD","write block response fault",CLI_MESSAGETYPE_ERROR);
#endif
		return SDCARD_ERRORS_WRITE_BLOCK_FAILED;
    }

    SDCard_waitReady(dev,SDCARD_TIMEOUT_WRITE);

    // Close CMD24
    SDCard_deselect(dev);
    return SDCARD_ERRORS_OK;
}

SDCard_Errors SDCard_writeBlocks (SDCard_Device* dev,
                                  uint32_t blockAddress,
                                  const uint8_t* data,
                                  uint8_t count)
{
    uint8_t response, retry = 0;
    uint16_t i;

    if (dev->isSDHC)
    {
        SDCard_sendCommand(dev,SDCARD_COMMAND_55,0,&response);
        SDCard_sendCommand(dev,SDCARD_COMMAND_A23,count,&response);
    }

    // Send starting block with writing blocks command
    do
	{
        SDCard_sendCommand(dev,SDCARD_COMMAND_25,blockAddress,&response);
		retry++;
		if (retry > SDCARD_MAX_RETRY)
		{
		    // Close CMD25
	        SDCard_deselect(dev);
#ifdef WARCOMEB_SDCARD_DEBUG
            Cli_sendMessage("SDCARD","CMD25 write blocks fail",CLI_MESSAGETYPE_ERROR);
#endif
	        return SDCARD_ERRORS_WRITE_BLOCKS_FAILED;
		}
		dev->delayTime(10);
	} while (response != SDCARD_RESPONSE_OK);

    do
    {
        // Send TOKEN
        Spi_writeByte(dev->device,0xFC);

        // Send DATA
        for (i = 0; i < 512; ++i)
        {
            Spi_writeByte(dev->device,data[i]);
        }

        // Send dummy CRC
        Spi_writeByte(dev->device,0xFF);
        Spi_writeByte(dev->device,0xFF);

        // Read card reply
        // Every data block written to the card will be acknoledged by a
        // data response token. It is one byte long and has the following format:
        // X X X 0 STATUS 1, where status bits is defined as
        // 010 - Data accepted
        // 101 - Data rejected due to a CRC error
        // 110 - Data rejected due to a write error
        Spi_readByte(dev->device,&response);
        if (response != SDCARD_RESPONSE_FAULT)
        {
            // Close CMD25
            SDCard_deselect(dev);
#ifdef WARCOMEB_SDCARD_DEBUG
            Cli_sendMessage("SDCARD","write blocks response fault",CLI_MESSAGETYPE_ERROR);
#endif
            return SDCARD_ERRORS_WRITE_BLOCKS_FAILED;
        }

        SDCard_waitReady(dev,SDCARD_TIMEOUT_WRITE);

        // Move forward the data pointer
        data += 512;

    } while (--count);

    // Send TOKEN for STOP TRANS
    Spi_writeByte(dev->device,0xFD);

    SDCard_waitReady(dev,SDCARD_TIMEOUT_WRITE);

    // Close CMD25
    SDCard_deselect(dev);
    return SDCARD_ERRORS_OK;
}

SDCard_Errors SDCard_readBlock (SDCard_Device* dev,
                                uint32_t blockAddress,
                                uint8_t* data)
{
    uint8_t response, retry = 0;
    uint16_t i;
    uint32_t time;

    // Send starting block with reading command
    do
	{
        SDCard_sendCommand(dev,SDCARD_COMMAND_17,blockAddress,&response);
		retry++;
		if (retry > SDCARD_MAX_RETRY)
		{
		    // Close CMD17
	        SDCard_deselect(dev);
#ifdef WARCOMEB_SDCARD_DEBUG
            Cli_sendMessage("SDCARD","CMD17 read block fail",CLI_MESSAGETYPE_ERROR);
#endif
	        return SDCARD_ERRORS_READ_BLOCK_FAILED;
		}
		dev->delayTime(10);
	} while (response != SDCARD_RESPONSE_OK);

    // Wait for datastart token
    // Current time + Timeout
    time = dev->currentTime() + SDCARD_TIMEOUT_READ;
    do
    {
        Spi_readByte(dev->device,&response);
    } while ((response != 0xFE) && (dev->currentTime() < time));
    if (response != 0xFE)
    {
        // Close CMD17
        SDCard_deselect(dev);
#ifdef WARCOMEB_SDCARD_DEBUG
        Cli_sendMessage("SDCARD","read block failed",CLI_MESSAGETYPE_ERROR);
#endif
        return SDCARD_ERRORS_READ_BLOCK_FAILED;
    }

    // Read DATA
    for (i = 0; i < 512; ++i)
    {
        Spi_readByte(dev->device,&data[i]);
    }

    // Read CRC, doesn't used
    Spi_readByte(dev->device,&response);
    Spi_readByte(dev->device,&response);

    // Close CMD17
    SDCard_deselect(dev);
    return SDCARD_ERRORS_OK;
}

SDCard_Errors SDCard_readBlocks (SDCard_Device* dev,
                                 uint32_t blockAddress,
                                 uint8_t* data,
                                 uint8_t count)
{
    uint8_t response, retry = 0;
    uint16_t i;
    uint32_t time;

    // Send starting block with reading multiple block command
    do
	{
        SDCard_sendCommand(dev,SDCARD_COMMAND_18,blockAddress,&response);
		retry++;
		if (retry > SDCARD_MAX_RETRY)
		{
		    // Close CMD18
	        SDCard_deselect(dev);
#ifdef WARCOMEB_SDCARD_DEBUG
            Cli_sendMessage("SDCARD","CMD18 read blocks fail",CLI_MESSAGETYPE_ERROR);
#endif
	        return SDCARD_ERRORS_READ_BLOCKS_FAILED;
		}
		dev->delayTime(10);
	} while (response != SDCARD_RESPONSE_OK);

    // Wait for datastart token
    // Current time + Timeout
    time = dev->currentTime() + SDCARD_TIMEOUT_READ;
    do
    {
        Spi_readByte(dev->device,&response);
    } while ((response != 0xFE) && (dev->currentTime() < time));
    if (response != 0xFE)
    {
        // Close CMD18
        SDCard_deselect(dev);
#ifdef WARCOMEB_SDCARD_DEBUG
        Cli_sendMessage("SDCARD","read blocks failed",CLI_MESSAGETYPE_ERROR);
#endif
        return SDCARD_ERRORS_READ_BLOCK_FAILED;
    }

    do
    {
        // Read DATA
        for (i = 0; i < 512; ++i)
        {
            Spi_readByte(dev->device,&data[i]);
        }

        // Read CRC, doesn't used
        Spi_readByte(dev->device,&response);
        Spi_readByte(dev->device,&response);

        // Move forward the data pointer
        data += 512;

    } while (--count);

    // Close CMD18
    SDCard_deselect(dev);

    // Send STOP command
    SDCard_sendCommand(dev,SDCARD_COMMAND_12,0,&response);

    if (count > 0)
    {
#ifdef WARCOMEB_SDCARD_DEBUG
        Cli_sendMessage("SDCARD","read blocks fail (count > 0)",CLI_MESSAGETYPE_ERROR);
#endif
        return SDCARD_ERRORS_READ_BLOCKS_FAILED;
    }
    else
    {
        return SDCARD_ERRORS_OK;
    }
}

SDCard_Errors SDCard_eraseBlocks (SDCard_Device* dev,
                                  uint32_t blockAddress,
                                  uint32_t count)
{
    uint8_t response;

    // Send starting block
    SDCard_sendCommand(dev,SDCARD_COMMAND_32,blockAddress,&response);
    if (response != SDCARD_RESPONSE_OK)
    {
//        SDCard_deselect(dev);
        return SDCARD_ERRORS_ERASE_BLOCKS_FAILED;
    }

    // Send ending block
    SDCard_sendCommand(dev,SDCARD_COMMAND_33,(blockAddress+count-1),&response);
    if (response != SDCARD_RESPONSE_OK)
    {
//        SDCard_deselect(dev);
        return SDCARD_ERRORS_ERASE_BLOCKS_FAILED;
    }

    // Send starting block
    SDCard_sendCommand(dev,SDCARD_COMMAND_38,0,&response);
    if (response != SDCARD_RESPONSE_OK)
    {
//        SDCard_deselect(dev);
        return SDCARD_ERRORS_ERASE_BLOCKS_FAILED;
    }

    // Wait...
    SDCard_waitReady(dev,SDCARD_TIMEOUT_ERASE);

    SDCard_deselect(dev);
    return SDCARD_ERRORS_OK;
}

SDCard_Errors SDCard_getSectorCount (SDCard_Device* dev,
                                     uint32_t* size)
{
    uint8_t response;
    uint8_t i;
    uint8_t csd[16];

    uint32_t tempSize = 0, time = 0;


    SDCard_sendCommand(dev,SDCARD_COMMAND_9,0,&response);
    if (response != SDCARD_RESPONSE_OK)
    {
        // Close CMD9
        SDCard_deselect(dev);
        *size = 0;
        return SDCARD_ERRORS_COMMAND_FAILED;
    }

    // Wait for datastart token
    // Current time + Timeout
    time = dev->currentTime() + SDCARD_TIMEOUT_READ;
    do
    {
        Spi_readByte(dev->device,&response);
    } while ((response != 0xFE) && (dev->currentTime() < time));
    if (response != 0xFE)
    {
        *size = 0;
        // Close CMD9
        SDCard_deselect(dev);
        return SDCARD_ERRORS_READ_BLOCK_FAILED;
    }

    // Read DATA
    for (i = 0; i < 16; ++i) Spi_readByte(dev->device,&csd[i]);
    // Read CRC, doesn't used
    Spi_readByte(dev->device,&response);
    Spi_readByte(dev->device,&response);
    // Close CMD9
    SDCard_deselect(dev);

    // !! The first byte read is the last one!
    if ((csd[0] >> 6) == 1) // SDCARD v.2
    {
        // See page 87 of "Physical Layer Simplified Specification Version 2.00"
        tempSize = csd[9] + ((uint32_t)csd[8] << 8) + ((uint32_t)(csd[7] & 63) << 16) + 1;
        *size = tempSize << 10;
    }
    else // SDCARD v.1 or MMC v.3
    {
        // See page 81 of "Physical Layer Simplified Specification Version 2.00"
        i = (csd[5] & 0x0F) + ((csd[10] & 0x80) >> 7) + ((csd[9] & 0x03) << 1) + 2;
        tempSize = (csd[8] >> 6) + ((uint32_t)csd[7] << 2) + ((uint32_t)(csd[6] & 3) << 10) + 1;
        *size = tempSize << (i - 9);
    }

    return SDCARD_ERRORS_OK;
}

bool SDCard_isBusy(SDCard_Device* dev)
{
    bool result = SDCard_select(dev);
    SDCard_deselect(dev);

    return !result;
}

bool SDCard_isPresent(SDCard_Device* dev)
{
    return (Gpio_get(dev->cpPin) != dev->cpType) ? FALSE : TRUE;
}
