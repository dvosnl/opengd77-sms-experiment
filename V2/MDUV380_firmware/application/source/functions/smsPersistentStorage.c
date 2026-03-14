/*
 * Copyright (C) 2024
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
 *    incorporation in a commercial product or incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * SMS Persistent Storage — flash-direct implementation (no large RAM arrays).
 *
 * Flash layout (1 sector = 4096 bytes at SMS_FLASH_ADDRESS = 0xF0000):
 *   Offset  0   : uint32_t magic = SMS_FLASH_MAGIC
 *   Offset  4   : uint8_t  inboxCount
 *   Offset  5   : uint8_t  sentCount
 *   Offset  6   : uint8_t  pad[2]
 *   Offset  8   : smsPersistentMsg_t inbox[16]   (16 x 72 = 1152 bytes)
 *   Offset  1160: smsPersistentMsg_t sent[16]    (16 x 72 = 1152 bytes)
 *   Total used  : 2312 bytes < 4096
 *
 * All read/write/delete operations use the existing SPI_Flash_sectorbuffer
 * (4096 bytes, declared in SPI_Flash.c) to avoid adding large static RAM.
 * Only the two count values are kept in RAM.
 */

#include <string.h>
#include "functions/smsPersistentStorage.h"
#include "hardware/SPI_Flash.h"

#define SMS_FLASH_ADDRESS        0xF0000U
#define SMS_FLASH_MAGIC          0x534D5301U
#define SMS_STORE_INBOX_OFFSET   8U
#define SMS_STORE_SENT_OFFSET    (SMS_STORE_INBOX_OFFSET + (SMS_STORE_MAX_MESSAGES * (uint32_t)sizeof(smsPersistentMsg_t)))

// Only the counts live in RAM — all message data is read on demand from flash
static uint8_t persistInboxCount = 0U;
static uint8_t persistSentCount  = 0U;

static void smsPersistWriteBack(void)
{
	// SPI_Flash_sectorbuffer already contains the full updated sector image
	SPI_Flash_eraseSector(SMS_FLASH_ADDRESS);
	for (uint32_t page = 0U; page < (4096U / 256U); page++)
	{
		SPI_Flash_writePage(SMS_FLASH_ADDRESS + (page * 256U), &SPI_Flash_sectorbuffer[page * 256U]);
	}
}

static void smsPersistReadSector(void)
{
	SPI_Flash_read(SMS_FLASH_ADDRESS, SPI_Flash_sectorbuffer, 4096U);
}

void smsPersistInit(void)
{
	uint8_t header[8];
	uint32_t magic;

	persistInboxCount = 0U;
	persistSentCount  = 0U;

	SPI_Flash_read(SMS_FLASH_ADDRESS, header, sizeof(header));
	memcpy(&magic, &header[0], sizeof(magic));

	if (magic != SMS_FLASH_MAGIC)
	{
		return;  // No valid data yet
	}

	persistInboxCount = (header[4] <= SMS_STORE_MAX_MESSAGES) ? header[4] : 0U;
	persistSentCount  = (header[5] <= SMS_STORE_MAX_MESSAGES) ? header[5] : 0U;
}

bool smsPersistAddInbox(uint32_t sourceId, const char *text)
{
	if (text == NULL)
	{
		return false;
	}

	smsPersistReadSector();

	// Ensure magic is present
	uint32_t magic = SMS_FLASH_MAGIC;
	memcpy(&SPI_Flash_sectorbuffer[0], &magic, sizeof(magic));

	uint8_t *inboxBase = &SPI_Flash_sectorbuffer[SMS_STORE_INBOX_OFFSET];

	if (persistInboxCount >= SMS_STORE_MAX_MESSAGES)
	{
		// Drop the oldest entry (index 0) by shifting left
		memmove(inboxBase,
				inboxBase + sizeof(smsPersistentMsg_t),
				(SMS_STORE_MAX_MESSAGES - 1U) * sizeof(smsPersistentMsg_t));
		persistInboxCount = SMS_STORE_MAX_MESSAGES - 1U;
	}

	smsPersistentMsg_t *slot = (smsPersistentMsg_t *)(inboxBase + persistInboxCount * sizeof(smsPersistentMsg_t));
	memset(slot, 0, sizeof(smsPersistentMsg_t));
	slot->peerId = sourceId;
	strncpy(slot->text, text, sizeof(slot->text) - 1U);
	persistInboxCount++;

	SPI_Flash_sectorbuffer[4] = persistInboxCount;
	SPI_Flash_sectorbuffer[5] = persistSentCount;

	smsPersistWriteBack();
	return true;
}

bool smsPersistAddSent(uint32_t destId, const char *text)
{
	if (text == NULL)
	{
		return false;
	}

	smsPersistReadSector();

	uint32_t magic = SMS_FLASH_MAGIC;
	memcpy(&SPI_Flash_sectorbuffer[0], &magic, sizeof(magic));

	uint8_t *sentBase = &SPI_Flash_sectorbuffer[SMS_STORE_SENT_OFFSET];

	if (persistSentCount >= SMS_STORE_MAX_MESSAGES)
	{
		memmove(sentBase,
				sentBase + sizeof(smsPersistentMsg_t),
				(SMS_STORE_MAX_MESSAGES - 1U) * sizeof(smsPersistentMsg_t));
		persistSentCount = SMS_STORE_MAX_MESSAGES - 1U;
	}

	smsPersistentMsg_t *slot = (smsPersistentMsg_t *)(sentBase + persistSentCount * sizeof(smsPersistentMsg_t));
	memset(slot, 0, sizeof(smsPersistentMsg_t));
	slot->peerId = destId;
	strncpy(slot->text, text, sizeof(slot->text) - 1U);
	persistSentCount++;

	SPI_Flash_sectorbuffer[4] = persistInboxCount;
	SPI_Flash_sectorbuffer[5] = persistSentCount;

	smsPersistWriteBack();
	return true;
}

uint8_t smsPersistGetInboxCount(void)
{
	return persistInboxCount;
}

uint8_t smsPersistGetSentCount(void)
{
	return persistSentCount;
}

bool smsPersistGetInboxMsg(uint8_t index, smsPersistentMsg_t *msg)
{
	if ((msg == NULL) || (index >= persistInboxCount))
	{
		return false;
	}

	uint32_t flashAddr = SMS_FLASH_ADDRESS + SMS_STORE_INBOX_OFFSET
						 + ((uint32_t)index * sizeof(smsPersistentMsg_t));
	SPI_Flash_read(flashAddr, (uint8_t *)msg, sizeof(smsPersistentMsg_t));
	return true;
}

bool smsPersistGetSentMsg(uint8_t index, smsPersistentMsg_t *msg)
{
	if ((msg == NULL) || (index >= persistSentCount))
	{
		return false;
	}

	uint32_t flashAddr = SMS_FLASH_ADDRESS + SMS_STORE_SENT_OFFSET
						 + ((uint32_t)index * sizeof(smsPersistentMsg_t));
	SPI_Flash_read(flashAddr, (uint8_t *)msg, sizeof(smsPersistentMsg_t));
	return true;
}

bool smsPersistDeleteInboxMsg(uint8_t index)
{
	if (index >= persistInboxCount)
	{
		return false;
	}

	smsPersistReadSector();

	uint8_t *inboxBase = &SPI_Flash_sectorbuffer[SMS_STORE_INBOX_OFFSET];

	if (index < (persistInboxCount - 1U))
	{
		memmove(inboxBase + (index * sizeof(smsPersistentMsg_t)),
				inboxBase + ((index + 1U) * sizeof(smsPersistentMsg_t)),
				(persistInboxCount - 1U - index) * sizeof(smsPersistentMsg_t));
	}

	// Zero out the now-unused last slot
	memset(inboxBase + ((persistInboxCount - 1U) * sizeof(smsPersistentMsg_t)), 0, sizeof(smsPersistentMsg_t));

	persistInboxCount--;
	SPI_Flash_sectorbuffer[4] = persistInboxCount;

	smsPersistWriteBack();
	return true;
}

bool smsPersistDeleteSentMsg(uint8_t index)
{
	if (index >= persistSentCount)
	{
		return false;
	}

	smsPersistReadSector();

	uint8_t *sentBase = &SPI_Flash_sectorbuffer[SMS_STORE_SENT_OFFSET];

	if (index < (persistSentCount - 1U))
	{
		memmove(sentBase + (index * sizeof(smsPersistentMsg_t)),
				sentBase + ((index + 1U) * sizeof(smsPersistentMsg_t)),
				(persistSentCount - 1U - index) * sizeof(smsPersistentMsg_t));
	}

	memset(sentBase + ((persistSentCount - 1U) * sizeof(smsPersistentMsg_t)), 0, sizeof(smsPersistentMsg_t));

	persistSentCount--;
	SPI_Flash_sectorbuffer[5] = persistSentCount;

	smsPersistWriteBack();
	return true;
}
