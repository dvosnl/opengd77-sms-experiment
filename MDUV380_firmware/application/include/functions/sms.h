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

#ifndef _OPENGD77_SMS_H_
#define _OPENGD77_SMS_H_

#include <stdint.h>
#include <stdbool.h>

#define SMS_MAX_TEXT_LENGTH           64U
#define SMS_MAX_UTF16_PAYLOAD_BYTES  (SMS_MAX_TEXT_LENGTH * 2U)
#define SMS_BLOCK_DATA_BYTES          12U
#define SMS_MAX_DATA_BLOCKS          ((SMS_MAX_UTF16_PAYLOAD_BYTES + SMS_BLOCK_DATA_BYTES - 1U) / SMS_BLOCK_DATA_BYTES)

typedef enum
{
	SMS_PACK_OK = 0,
	SMS_PACK_ERROR_EMPTY,
	SMS_PACK_ERROR_TOO_LONG,
	SMS_PACK_ERROR_INVALID_DEST,
	SMS_PACK_ERROR_INVALID_SRC,
	SMS_PACK_ERROR_UNSUPPORTED_CHAR
} smsPackResult_t;

typedef struct
{
	uint32_t destinationId;
	uint32_t sourceId;
	uint16_t payloadLength;
	uint8_t padOctetCount;
	uint8_t blockCount;
	uint8_t csbk[SMS_BLOCK_DATA_BYTES];
	uint8_t dataHeader[SMS_BLOCK_DATA_BYTES];
	uint8_t payload[SMS_MAX_UTF16_PAYLOAD_BYTES];
	uint8_t blocks[SMS_MAX_DATA_BLOCKS][SMS_BLOCK_DATA_BYTES];
} smsPreparedMessage_t;

smsPackResult_t smsPackMessage(uint32_t destinationId, uint32_t sourceId, const char *text, smsPreparedMessage_t *message);
smsPackResult_t smsQueueMessage(uint32_t destinationId, uint32_t sourceId, const char *text);
bool smsHasQueuedMessage(void);
const smsPreparedMessage_t *smsGetQueuedMessage(void);
void smsClearQueuedMessage(void);

#endif