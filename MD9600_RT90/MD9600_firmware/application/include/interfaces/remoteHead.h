/*
 * Copyright (C) 2021-2025 Roger Clark, VK3KYY / G4KYF
 *                         Colin Durbridge, G4EML
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

#ifndef _OPENGD77_REMOTEHEAD_H_
#define _OPENGD77_REMOTEHEAD_H_

#define REMOTE_DMA_BUFFER_SIZE     6U

extern bool remoteHeadActive;

extern uint16_t remoteHeadKey;
extern uint8_t remoteHeadMicRow;
extern uint8_t remoteHeadMicCol;
extern uint8_t remoteHeadPTT;
extern int8_t remoteHeadRotary;

void remoteHeadInit(void);


void remoteHead_UART_RxHalfCpltCallback(void);
void remoteHead_UART_RxCpltCallback(void);
void remoteHeadRenderRows(int16_t startRow, int16_t endRow);
void remoteHeadGpsPower(bool on);
void remoteHeadDim(uint8_t bright);
void remoteHeadTransferCommand(uint8_t data1);
void remoteHeadRestartIP(void);
void remoteHeadPowerDown(void);
void remoteHeadGpsDataInputStartStop(bool on);

#endif
