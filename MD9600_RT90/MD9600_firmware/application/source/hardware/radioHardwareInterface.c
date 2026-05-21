/*
 * Copyright (C) 2021-2025 Roger Clark, VK3KYY / G4KYF
 *
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

#include "functions/settings.h"
#include "functions/trx.h"
#include "functions/rxPowerSaving.h"
#include "hardware/HR-C6000.h"
#if defined(USING_EXTERNAL_DEBUGGER)
#include "SeggerRTT/RTT/SEGGER_RTT.h"
#endif
#include "main.h"

//list of CTCSS tones supported by the HRC6000. These are used to find the index for the tone. The order is critical.
const uint16_t HRC_CTCSSTones[] =
{
		6700,  7190,  7440,  7700,  7970,  8250,  8540,
		8850,  9150,  9480,  9740,  10000, 10350, 10720,
		11090, 11480, 11880, 12300, 12730, 13180, 13650,
		14130, 14620, 15140, 15670, 16220, 16790, 17380,
		17990, 18620, 19280, 20350, 21070, 21810, 22570,
		23360, 24180, 25030, 6930,  6250,  15980, 16550,
		17130, 17730, 18350, 18990, 19660, 19950, 20650,
		22910, 25410
};

static bool audioPathFromFM = true;
static bool ampIsOn = false;

RadioDevice_t currentRadioDeviceId = RADIO_DEVICE_PRIMARY;
TRXDevice_t radioDevices[RADIO_DEVICE_MAX] = {
		{
				.currentRxFrequency = FREQUENCY_UNSET,
				.currentTxFrequency = FREQUENCY_UNSET,
				.lastSetTxFrequency = FREQUENCY_UNSET,
				.trxCurrentBand = { RADIO_BAND_VHF, RADIO_BAND_VHF },
				.trxDMRModeTx = DMR_MODE_DMO,
				.trxDMRModeRx = DMR_MODE_DMO,
				.currentMode = RADIO_MODE_NONE,
				.txPowerLevel = POWER_UNSET,
				.lastSetTxPowerLevel = POWER_UNSET,
				.trxRxSignal = 0,
				.trxRxNoise = 255,
				.currentBandWidthIs25kHz = BANDWIDTH_12P5KHZ,
				.analogSignalReceived = false,
				.analogTriggeredAudio = false,
				.digitalSignalReceived = false
		}
};

TRXDevice_t *currentRadioDevice = &radioDevices[RADIO_DEVICE_PRIMARY];


void radioPowerOn(void)
{
	//Turn off Tx Voltages to prevent transmission.
	HAL_GPIO_WritePin(T5_V_SW_GPIO_Port, T5_V_SW_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(T5_U_SW_GPIO_Port, T5_U_SW_Pin, GPIO_PIN_RESET);

	//Turn on all receiver voltages. (thats what the TYT firmware does, both receivers are on together)
	RadioDevice_t previousDeviceId = currentRadioDeviceId;
	for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
	{
		radioSetTRxDevice(device);
		radioSetRxLNAForDevice(device);

#if 0 // Not used on MD-UV380 and friends.
		uint8_t rxAndMuteSetting = 0x26;
#if defined(PLATFORM_MD2017)
		if (currentRadioDeviceId != RADIO_DEVICE_PRIMARY)
		{
			rxAndMuteSetting = 0x2E;
		}
#endif

		if (currentRadioDevice->currentBandWidthIs25kHz)
		{
			// 25 kHz settings
			radioWriteReg2byte(0x30, 0x70, rxAndMuteSetting); // RX on
		}
		else
		{
			// 12.5 kHz settings
			radioWriteReg2byte(0x30, 0x60, rxAndMuteSetting); // RX on
		}
#endif
	}

	radioSetTRxDevice(previousDeviceId);

	//turn on the main power control
	HAL_GPIO_WritePin(Power_Control_GPIO_Port, Power_Control_Pin, GPIO_PIN_SET);

// The V4 VHF synthesiser needs to be initialised as soon as the power is applied. Otherwise it fails to lock.
#if defined(MD9600_VERSION_4)
	radioSetFrequency(14500000, false);				//Initially set 145MHz receive frequency. (This will be overwritten when channel is loaded)
#endif
}

void radioSetRxLNAForDevice(RadioDevice_t deviceId)
{
	if (deviceId == RADIO_DEVICE_PRIMARY)
	{
		HAL_GPIO_WritePin(C5_V_SW_GPIO_Port, C5_V_SW_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(C5_U_SW_GPIO_Port, C5_U_SW_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(R5_V_SW_GPIO_Port, R5_V_SW_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(R5_U_SW_GPIO_Port, R5_U_SW_Pin, GPIO_PIN_SET);
	}
}

void radioPowerOff(void)
{
	//turn off all of the transmitter and receiver voltages
	HAL_GPIO_WritePin(T5_V_SW_GPIO_Port, T5_V_SW_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(T5_U_SW_GPIO_Port, T5_U_SW_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(C5_V_SW_GPIO_Port, C5_V_SW_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(C5_U_SW_GPIO_Port, C5_U_SW_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(R5_V_SW_GPIO_Port, R5_V_SW_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(R5_U_SW_GPIO_Port, R5_U_SW_Pin, GPIO_PIN_RESET);

	//turn off the main power control
	HAL_GPIO_WritePin(Power_Control_GPIO_Port, Power_Control_Pin, GPIO_PIN_RESET);
	trxInvalidateCurrentFrequency();
}

void radioInit(void)
{
	radioSetTRxDevice(RADIO_DEVICE_PRIMARY);
}

void radioPostinit(void)
{

}

RadioDevice_t radioSetTRxDevice(RadioDevice_t deviceId)
{
	RadioDevice_t previousDeviceId = currentRadioDeviceId;

	currentRadioDeviceId = deviceId;

	currentRadioDevice = &radioDevices[currentRadioDeviceId];

	return previousDeviceId;
}

RadioDevice_t radioGetTRxDeviceId(void)
{
	return currentRadioDeviceId;
}

void radioSetBandwidth(bool Is25K)
{

}

void radioSetCalibration(void)
{

}

void radioSetMode(int mode)
{
	switch (mode)
	{
		case RADIO_MODE_ANALOG:
			HAL_GPIO_WritePin(FM_SW_GPIO_Port, FM_SW_Pin, GPIO_PIN_SET); 			//Turn on the FM audio stages
			HRC6000SetFMRx();
			break;
		case RADIO_MODE_DIGITAL:
			HAL_GPIO_WritePin(FM_SW_GPIO_Port, FM_SW_Pin, GPIO_PIN_RESET); 			//Turn off the FM Audio Stages
			HAL_GPIO_WritePin(FM_MUTE_GPIO_Port, FM_MUTE_Pin, GPIO_PIN_RESET);		//Mute The FM Audio
			HRC6000SetDMR();
			break;
		case RADIO_MODE_NONE:
			HAL_GPIO_WritePin(FM_SW_GPIO_Port, FM_SW_Pin, GPIO_PIN_RESET); 			//Turn off the FM Audio Stages
			break;
	}
}

#if defined(MD9600_VERSION_1) || defined (MD9600_VERSION_2) || defined (MD9600_VERSION_4)
//this function initialises the AK2365A IF Chip in V1-V4 Radios for the specified band and bandwidth and needs to be called whenever the receiver is turned on.
void radioSetIF(int band, bool wide)
{
	HAL_GPIO_WritePin(IF_RST_GPIO_Port, IF_RST_Pin, GPIO_PIN_RESET);     //Pulse the reset pin Low
//	HAL_Delay(1);
	for( volatile int i=0;i<100;i++);									//short delay to meet datasheet requirement of 1us
	HAL_GPIO_WritePin(IF_RST_GPIO_Port, IF_RST_Pin, GPIO_PIN_SET);
//	HAL_Delay(8);													   //allow time for the reset (this time is what the TYT Firmware uses)
	IFTransfer(band, 0x08AA);                                           //Send the chip soft reset command to Reg 4
//	HAL_Delay(2);
	if (wide)
	{
		IFTransfer(band, 0x02F1);										  // Reg 1 values for 25Khz filtering
	}
	else
	{
		IFTransfer(band, 0x02E9);										  // Reg 1 values for 12.5Khz filtering
	}
	IFTransfer(band, 0x041D);										  // Reg 2 values for AGC
	IFTransfer(band, 0x0600);										  // Reg 3 values for IF Gain=6dB (Default setting)
	IFTransfer(band, 0x1601);										  // Reg B values for Auto AGC
	IFTransfer(band, 0x1880);										  // Reg C values for Auto AGC

	//as we have just set the IF for a band we should also select that bands audio for receive
	if (band == RADIO_BAND_VHF)				//VHF
	{
		HAL_GPIO_WritePin(U_V_AF_SW_GPIO_Port, U_V_AF_SW_Pin, GPIO_PIN_SET);				//select the VHF IF Audio
	}
	else					//UHF
	{
		HAL_GPIO_WritePin(U_V_AF_SW_GPIO_Port, U_V_AF_SW_Pin, GPIO_PIN_RESET);			//select the UHF IF Audio
	}
}

void IFTransfer(bool band, uint16_t data1)
{
	//set the correct CE low
	if (band == RADIO_BAND_VHF)
	{
		HAL_GPIO_WritePin(IF_CS_V_GPIO_Port, IF_CS_V_Pin, GPIO_PIN_RESET);
	}
	else
	{
		HAL_GPIO_WritePin(IF_CS_U_GPIO_Port, IF_CS_U_Pin, GPIO_PIN_RESET);
	}

	//Send the data as 16 bits of SPI
	for (register int i = 0; i < 16; i++)
	{
		HAL_GPIO_WritePin(IF_DATA_GPIO_Port, IF_DATA_Pin, ((data1 & 0x8000) != 0U));
		HAL_GPIO_WritePin(IF_CLK_GPIO_Port, IF_CLK_Pin, GPIO_PIN_RESET);
		data1 = data1 << 1;
		HAL_GPIO_WritePin(IF_CLK_GPIO_Port, IF_CLK_Pin, GPIO_PIN_SET);
	}
	//set the CS High Again
	if (band == RADIO_BAND_VHF)
	{
		HAL_GPIO_WritePin(IF_CS_V_GPIO_Port, IF_CS_V_Pin, GPIO_PIN_SET);
	}
	else
	{
		HAL_GPIO_WritePin(IF_CS_U_GPIO_Port, IF_CS_U_Pin, GPIO_PIN_SET);
	}

}
#endif

#if defined(MD9600_VERSION_1) || defined (MD9600_VERSION_2)
//This is the SKY Synthesiser version For V1-V3 Radios
void radioSetFrequency(uint32_t freq, bool Tx)
{
	bool bandIsVHF;
	uint16_t refdiv;
	uint32_t ref;
	uint16_t N;
	uint32_t rem;
	uint32_t frac;

	//don't really need these register variables, could send them directly to the SPI but they make debugging easier for now.

	uint16_t reg0;
	uint16_t reg1;
	uint16_t reg2;
	uint16_t reg5;
	uint16_t reg6;
	uint16_t reg7;
	uint16_t reg8;
	uint16_t reg9;

	if (freq < 30000000)
	{
		bandIsVHF = true;
	}
	else
	{
		bandIsVHF = false;
	}

	if (bandIsVHF)            //VHF band has divide by 2 on output so needs the frequency to be doubled.
	{
		if (!Tx)				//if this is for the Receiver then add the IF Frequency of 49.95 MHz to get the LO frequency
		{
			freq = freq + 4995000;
		}
		refdiv = 6;
		freq = freq * 2;
	}
	else                //UHF Band
	{
		if (!Tx)				//if this is for the Receiver then subtract the IF Frequency of 49.95 MHz to get the LO frequency
		{
			freq = freq - 4995000;
		}
		refdiv = 4;
	}

	ref = 1680000 / refdiv;        //16.8 MHz reference Oscillator
	N = freq / ref;
	rem = freq % ref;
	frac = ((uint64_t)rem * 262144) / ref;

	if (frac > 131071)     //if the fractional part is more than 0.5 then increase the Integer. The fractional part is then taken as a negative offset. (thats the way the SKY chip works!)
	{
		N = N + 1;
	}

	//generate the register values add the non-changing values for regs 6-9

	reg0 = N - 32;
	reg1 = frac >> 8;
	reg2 = frac & 0xFF;
	reg5 = refdiv - 1;
	reg6 = 0x001F;
	reg7 = 0x03D0;
	reg8 = 0x0000;
	reg9 = 0x0000;

	//Now need to power up the correct VCO.

	if (bandIsVHF)          //VHF
	{
		//connect the HRC6000 Mod 2 output to this VCO to set the correct frequency
		HAL_GPIO_WritePin(U_V_MOD_SW_GPIO_Port, U_V_MOD_SW_Pin, GPIO_PIN_SET);
		//Select the correct VCO for Rx or Tx by setting VCOVCC_V_SW
		if (Tx)
		{
			HAL_GPIO_WritePin(VCO_VCC_V_SW_GPIO_Port, VCO_VCC_V_SW_Pin, GPIO_PIN_RESET);
		}
		else
		{
			HAL_GPIO_WritePin(VCO_VCC_V_SW_GPIO_Port, VCO_VCC_V_SW_Pin, GPIO_PIN_SET);
		}
	}
	else                       //UHF
	{
		//connect the HRC6000 Mod 2 output to this VCO to set the correct frequency
		HAL_GPIO_WritePin(U_V_MOD_SW_GPIO_Port, U_V_MOD_SW_Pin, GPIO_PIN_RESET);
		//Select the correct VCO for Rx or Tx by setting VCOVCC_U_SW
		if (Tx)
		{
			HAL_GPIO_WritePin(VCO_VCC_U_SW_GPIO_Port, VCO_VCC_U_SW_Pin, GPIO_PIN_RESET);
		}
		else
		{
			HAL_GPIO_WritePin(VCO_VCC_U_SW_GPIO_Port, VCO_VCC_U_SW_Pin, GPIO_PIN_SET);
		}
	}


	//now send the register values to the Synthesiser chip. This is the order they are sent by the TYT firmware.

	SynthTransfer(bandIsVHF, 8, reg8);
	SynthTransfer(bandIsVHF, 9, reg9);
	SynthTransfer(bandIsVHF, 5, reg5);
	SynthTransfer(bandIsVHF, 0, reg0);
	SynthTransfer(bandIsVHF, 2, reg2);
	SynthTransfer(bandIsVHF, 1, reg1);
	SynthTransfer(bandIsVHF, 6, reg6);
	SynthTransfer(bandIsVHF, 7, reg7);

	//As we have just changed the frequency we should also output the relevant tuning voltages.

	if (Tx) 						//We have just set the Tx frequency so we are about to transmit. Set the Tx Power Control DAC (Shared with VHF Receive Tuning)
	{
		dacOut(2, txDACDrivePower);
	}
	else						//if we have just set the Rx Frequency then set the relevant Front End Tuning Voltage.
	{
		if (bandIsVHF)
		{
			dacOut(2, VHFRxTuningVolts);
		}
		else
		{
			dacOut(1, UHFRxTuningVolts);
		}
	}
}

//send the 16 bit value to the VHF or UHF Sky Synthesiser
void SynthTransfer(bool VHF, uint8_t add, uint16_t data1)
{
	data1 = data1 | (add << 12);

	//set the correct CE low
	if (VHF)
	{
		HAL_GPIO_WritePin(PLL_CS_V_GPIO_Port, PLL_CS_V_Pin, GPIO_PIN_RESET);
	}
	else
	{
		HAL_GPIO_WritePin(PLL_CS_U_GPIO_Port, PLL_CS_U_Pin, GPIO_PIN_RESET);
	}

	//Send the data as 16 bits of SPI
	for (register int i = 0; i < 16; i++)
	{
		HAL_GPIO_WritePin(PLL_CLK_GPIO_Port, PLL_CLK_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(PLL_DATA_GPIO_Port, PLL_DATA_Pin, ((data1 & 0x8000) != 0));
		data1 = data1 << 1;
		HAL_GPIO_WritePin(PLL_CLK_GPIO_Port, PLL_CLK_Pin, GPIO_PIN_SET);
	}
	//set the CS High Again
	if (VHF)
	{
		HAL_GPIO_WritePin(PLL_CS_V_GPIO_Port, PLL_CS_V_Pin, GPIO_PIN_SET);
	}
	else
	{
		HAL_GPIO_WritePin(PLL_CS_U_GPIO_Port, PLL_CS_U_Pin, GPIO_PIN_SET);
	}

}
#endif

#if defined(MD9600_VERSION_4)
//This is the AK1590 Synthesiser version For V4 Radios
void radioSetFrequency(uint32_t freq, bool Tx)
{
	bool bandIsVHF;
	uint16_t refdiv;
	uint32_t ref;
	uint16_t N;
	uint32_t rem;
	uint32_t frac;

	//don't really need these register variables, could send them directly to the SPI but they make debugging easier for now.

	uint32_t reg1;
	uint32_t reg2;
	uint32_t reg3;
	uint32_t reg4;
	uint32_t reg5;
	uint32_t reg6;


	if (freq < 30000000)
	{
		bandIsVHF = true;
	}
	else
	{
		bandIsVHF = false;
	}

	if (bandIsVHF)
	{
		if (!Tx)				//if this is for the Receiver then add the IF Frequency of 49.95 MHz to get the LO frequency
		{
			freq = freq + 4995000;
		}
		refdiv = 12;
	}
	else                //UHF Band
	{
		if (!Tx)				//if this is for the Receiver then subtract the IF Frequency of 49.95 MHz to get the LO frequency
		{
		  freq = freq - 4995000;
		  refdiv=8;
		}
		else
		{
		  refdiv = 4;
		}
	}

	ref = 1680000 / refdiv;        //16.8 MHz reference Oscillator
	N = freq / ref;
	rem = freq % ref;
	frac = ((uint64_t)rem * 262144) / ref;

	if (frac > 131071)     //if the fractional part is more than 0.5 then increase the Integer. The fractional part is then taken as a negative offset. (thats the way the chip works!)
	{
		N = N + 1;
	}

	//generate the register values add the non-changing values

	reg1 = frac;
	reg3 = 0x06000 | refdiv;
	reg5 = 0x00;
	reg6 = 0x00;
	if (bandIsVHF)
	{
	  reg2 = 0x20000 | N;
	  reg4 = 0x1FF40;
	}
	else
	{
	  reg2 = 0x38000 | N;
	  reg4 = 0x19F40;
	}

	//Now need to power up the correct VCO.

	if (bandIsVHF)          //VHF
	{
		//connect the HRC6000 Mod 2 output to this VCO to set the correct frequency
		HAL_GPIO_WritePin(U_V_MOD_SW_GPIO_Port, U_V_MOD_SW_Pin, GPIO_PIN_SET);
		//Select the correct VCO for Rx or Tx by setting VCOVCC_V_SW
		if (Tx)
		{
			HAL_GPIO_WritePin(VCO_VCC_V_SW_GPIO_Port, VCO_VCC_V_SW_Pin, GPIO_PIN_RESET);
		}
		else
		{
			HAL_GPIO_WritePin(VCO_VCC_V_SW_GPIO_Port, VCO_VCC_V_SW_Pin, GPIO_PIN_SET);
		}
	}
	else                       //UHF
	{
		//connect the HRC6000 Mod 2 output to this VCO to set the correct frequency
		HAL_GPIO_WritePin(U_V_MOD_SW_GPIO_Port, U_V_MOD_SW_Pin, GPIO_PIN_RESET);
		//Select the correct VCO for Rx or Tx by setting VCOVCC_U_SW
		if (Tx)
		{
			HAL_GPIO_WritePin(VCO_VCC_U_SW_GPIO_Port, VCO_VCC_U_SW_Pin, GPIO_PIN_RESET);
		}
		else
		{
			HAL_GPIO_WritePin(VCO_VCC_U_SW_GPIO_Port, VCO_VCC_U_SW_Pin, GPIO_PIN_SET);
		}
	}


	//now send the register values to the Synthesiser chip. This is the order they are sent by the TYT firmware.

	SynthTransfer(bandIsVHF, 5, reg5);
	SynthTransfer(bandIsVHF, 6, reg6);
	SynthTransfer(bandIsVHF, 3, reg3);
	SynthTransfer(bandIsVHF, 4, reg4);
	SynthTransfer(bandIsVHF, 1, reg1);
	SynthTransfer(bandIsVHF, 2, reg2);


	//As we have just changed the frequency we should also output the relevant tuning voltages.

	if (Tx) 						//We have just set the Tx frequency so we are about to transmit. Set the Tx Power Control DAC (Shared with VHF Receive Tuning)
	{
		dacOut(2, txDACDrivePower);
	}
	else						//if we have just set the Rx Frequency then set the relevant Front End Tuning Voltage.
	{
		if (bandIsVHF)
		{
			dacOut(2, VHFRxTuningVolts);
		}
		else
		{
			dacOut(1, UHFRxTuningVolts);
		}
	}

}

//send the 16 bit value to the VHF or UHF AK1590 Synthesiser
void SynthTransfer(bool VHF, uint8_t add, uint32_t data1)
{
	data1 = (data1 << 4) | (add & 0x0F);

	//set the correct CE low
	if (VHF)
	{
		HAL_GPIO_WritePin(PLL_CS_V_GPIO_Port, PLL_CS_V_Pin, GPIO_PIN_RESET);
	}
	else
	{
		HAL_GPIO_WritePin(PLL_CS_U_GPIO_Port, PLL_CS_U_Pin, GPIO_PIN_RESET);
	}

	//Send the data as 24 bits of SPI
	for (register int i = 0; i < 24; i++)
	{
		HAL_GPIO_WritePin(PLL_CLK_GPIO_Port, PLL_CLK_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(PLL_DATA_GPIO_Port, PLL_DATA_Pin, ((data1 & 0x800000) != 0));
		data1 = data1 << 1;
		HAL_GPIO_WritePin(PLL_CLK_GPIO_Port, PLL_CLK_Pin, GPIO_PIN_SET);
	}
	//set the CS High Again
	if (VHF)
	{
		HAL_GPIO_WritePin(PLL_CS_V_GPIO_Port, PLL_CS_V_Pin, GPIO_PIN_SET);
	}
	else
	{
		HAL_GPIO_WritePin(PLL_CS_U_GPIO_Port, PLL_CS_U_Pin, GPIO_PIN_SET);
	}

}
#endif

#if defined(MD9600_VERSION_5)
//V5 Radios do not have a configurable IF Chip this function is for compatibility.
void radioSetIF(int band, bool wide)
{
	// PE13 and PE14 appear to control the connecting of the IF chips to the C6000
	// PE13 connects the VHF IF to the C6000, PE14 connects the UHF IF to the C6000
	if (band == RADIO_BAND_VHF)				//VHF
	{
		if (wide)			//25KHz bandwidth
		{
			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_11, GPIO_PIN_SET);// Set VHF IF to wide
		}
		else				//12.5KHz Bandwidth
		{
			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_11, GPIO_PIN_RESET);// Set VHF IF to narrow
		}

		HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_SET);
		HAL_GPIO_WritePin(GPIOE, GPIO_PIN_14, GPIO_PIN_RESET);

		HAL_GPIO_WritePin(U_V_AF_SW_GPIO_Port, U_V_AF_SW_Pin, GPIO_PIN_SET);				//select the VHF IF Audio
	}
	else					//UHF
	{

		if (wide)			//25KHz bandwidth
		{
			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_12, GPIO_PIN_SET);// Set UHF IF to wide
		}
		else				//12.5KHz Bandwidth
		{
			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_12, GPIO_PIN_RESET);// Set UHF IF to narrow
		}

		HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(GPIOE, GPIO_PIN_14, GPIO_PIN_SET);

		HAL_GPIO_WritePin(U_V_AF_SW_GPIO_Port, U_V_AF_SW_Pin, GPIO_PIN_RESET);			//select the UHF IF Audio
	}
}

//This is the TI Synthesiser version For V5 Radios
void radioSetFrequency(uint32_t freq, bool Tx)
{
	bool bandIsVHF;
	uint16_t refdiv;
	uint32_t ref;
	uint16_t N;
	uint32_t rem;
	uint32_t frac;

	//don't really need these register variables, could send them directly to the SPI but they make debugging easier for now.

	uint16_t reg0;
	uint16_t reg1;
	uint16_t reg2;
	uint16_t reg3;
	uint16_t reg4;
	uint16_t reg5;
	uint16_t reg6;
	uint16_t reg7;
	uint16_t reg8;
	uint16_t reg29;
	uint16_t reg2A;

	if (freq < 30000000)
	{
		bandIsVHF = true;
	}
	else
	{
		bandIsVHF = false;
	}

	if (bandIsVHF)
	{
		if (!Tx)				//if this is for the Receiver then add the IF Frequency of 49.95 MHz to get the LO frequency
		{
			freq = freq + 4995000;
		}
		refdiv = 8;
	}
	else                //UHF Band
	{
		if (!Tx)				//if this is for the Receiver then subtract the IF Frequency of 49.95 MHz to get the LO frequency
		{
			freq = freq - 4995000;
		}
		refdiv = 4;
	}

	ref = 1680000 / refdiv;        //16.8 MHz reference Oscillator
	N = freq / ref;
	rem = freq % ref;
	frac = ((uint64_t)rem * 16777216) / ref;     //convert remainder to 24 bit fractional value


	//generate the register values. Add the non-changing values for the other registers

	reg0 = 0x003E;
	reg1 = (frac >> 16) & 0xFF;
	reg2 = frac & 0xFFFF;
	reg3 = 0x0000;
	reg4 = (N & 0xFF) + 0x3000;
	reg5 = refdiv + 0x0100;
	reg6 = 0x8581;
	reg7 = 0x1024;
	reg8 = 0x0430;
	reg29 = 0x0230;
	reg2A = 0x0108;

	//Now need to power up the correct VCO.

	if (bandIsVHF)          //VHF
	{
		//connect the HRC6000 Mod 2 output to this VCO to set the correct frequency
		HAL_GPIO_WritePin(U_V_MOD_SW_GPIO_Port, U_V_MOD_SW_Pin, GPIO_PIN_SET);
		//Select the correct VCO for Rx or Tx by setting VCOVCC_V_SW
		if (Tx)
		{
			HAL_GPIO_WritePin(VCO_VCC_V_SW_GPIO_Port, VCO_VCC_V_SW_Pin, GPIO_PIN_RESET);
		}
		else
		{
			HAL_GPIO_WritePin(VCO_VCC_V_SW_GPIO_Port, VCO_VCC_V_SW_Pin, GPIO_PIN_SET);
		}
	}
	else                       //UHF
	{
		//connect the HRC6000 Mod 2 output to this VCO to set the correct frequency
		HAL_GPIO_WritePin(U_V_MOD_SW_GPIO_Port, U_V_MOD_SW_Pin, GPIO_PIN_RESET);
		//Select the correct VCO for Rx or Tx by setting VCO_VCC_U_SW
		if (Tx)
		{
			HAL_GPIO_WritePin(VCO_VCC_U_SW_GPIO_Port, VCO_VCC_U_SW_Pin, GPIO_PIN_RESET);
		}
		else
		{
			HAL_GPIO_WritePin(VCO_VCC_U_SW_GPIO_Port, VCO_VCC_U_SW_Pin, GPIO_PIN_SET);
		}
	}


	//now send the register values to the Synthesiser chip. This is the order they are sent by the TYT firmware.

	SynthTransfer(bandIsVHF, 0x00, 0x2000);				//reset chip
	//may need a delay here to allow time to reset.
	SynthTransfer(bandIsVHF, 0x00, reg0);
	SynthTransfer(bandIsVHF, 0x04, reg4);
	SynthTransfer(bandIsVHF, 0x01, reg1);
	SynthTransfer(bandIsVHF, 0x02, reg2);
	SynthTransfer(bandIsVHF, 0x03, reg3);
	SynthTransfer(bandIsVHF, 0x05, reg5);
	SynthTransfer(bandIsVHF, 0x06, reg6);
	SynthTransfer(bandIsVHF, 0x07, reg7);
	SynthTransfer(bandIsVHF, 0x08, reg8);
	SynthTransfer(bandIsVHF, 0x29, reg29);
	SynthTransfer(bandIsVHF, 0x2A, reg2A);
	SynthTransfer(bandIsVHF, 0x00, reg0 + 1);          //Enable Calibrate
	//may need a delay here to allow time to Cal.
	SynthTransfer(bandIsVHF, 0x00, reg0);

	//As we have just changed the frequency we should also output the relevant tuning voltages.

	if (Tx) 						//We have just set the Tx frequency so we are about to transmit. Set the Tx Power Control DAC (Shared with VHF Receive Tuning)
	{
		dacOut(2, txDACDrivePower);
	}
	else						//if we have just set the Rx Frequency then set the relevant Front End Tuning Voltage.
	{
		if (bandIsVHF)
		{
			dacOut(2, VHFRxTuningVolts);
		}
		else
		{
			dacOut(1, UHFRxTuningVolts);
		}
	}
}

//send the address followed by the 16 bit value to the VHF or UHF TI Synthesiser
void SynthTransfer(bool VHF, uint8_t add, uint16_t reg)
{
	uint32_t data1;

	data1 = ((uint32_t)add << 16) + reg;

	//Set the correct CE Low
	if (VHF)
	{
		HAL_GPIO_WritePin(PLL_CS_V_GPIO_Port, PLL_CS_V_Pin, GPIO_PIN_RESET);
	}
	else
	{
		HAL_GPIO_WritePin(PLL_CS_U_GPIO_Port, PLL_CS_U_Pin, GPIO_PIN_RESET);
	}

	//send the 24 bits of SPI data
	for (register int i = 0; i < 24; i++)
	{
		HAL_GPIO_WritePin(PLL_CLK_GPIO_Port, PLL_CLK_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(PLL_DATA_GPIO_Port, PLL_DATA_Pin, ((data1 & 0x800000) != 0));
		data1 = data1 << 1;
		HAL_GPIO_WritePin(PLL_CLK_GPIO_Port, PLL_CLK_Pin, GPIO_PIN_SET);
	}
	//set the CE High
	if (VHF)
	{
		HAL_GPIO_WritePin(PLL_CS_V_GPIO_Port, PLL_CS_V_Pin, GPIO_PIN_SET);
	}
	else
	{
		HAL_GPIO_WritePin(PLL_CS_U_GPIO_Port, PLL_CS_U_Pin, GPIO_PIN_SET);
	}
	HAL_GPIO_WritePin(PLL_CLK_GPIO_Port, PLL_CLK_Pin, GPIO_PIN_RESET);
}
#endif

void radioSetTx(uint8_t band)
{
	//Turn off receiver voltages. (thats what the TYT firmware does, both receivers are on or off together)
	HAL_GPIO_WritePin(R5_V_SW_GPIO_Port, R5_V_SW_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(R5_U_SW_GPIO_Port, R5_U_SW_Pin, GPIO_PIN_RESET);

	//Configure HRC-6000 for transmit
	if (trxGetMode() == RADIO_MODE_ANALOG)
	{
		HRC6000SetFMTx();
	}
	else
	{
		HRC6000SetDMR();
	}

	//Turn on Tx Voltage for the current band.

	if (band == RADIO_BAND_VHF)
	{
		HAL_GPIO_WritePin(T5_U_SW_GPIO_Port, T5_U_SW_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(T5_V_SW_GPIO_Port, T5_V_SW_Pin, GPIO_PIN_SET);
	}
	else
	{
		HAL_GPIO_WritePin(T5_V_SW_GPIO_Port, T5_V_SW_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(T5_U_SW_GPIO_Port, T5_U_SW_Pin, GPIO_PIN_SET);
	}

	//Turn on the power control circuit
	HAL_GPIO_WritePin(RF_APC_SW_GPIO_Port, RF_APC_SW_Pin, GPIO_PIN_SET);
	trxIsTransmitting = true;
}

//just enable or disable the RF output . doesn't change back to receive
void radioFastTx(bool tx)
{
	if (tx)
	{
		if (trxGetFrequency() > 30000000)
		{
			HAL_GPIO_WritePin(T5_U_SW_GPIO_Port, T5_U_SW_Pin, GPIO_PIN_SET);
		}
		else
		{
			HAL_GPIO_WritePin(T5_V_SW_GPIO_Port, T5_V_SW_Pin, GPIO_PIN_SET);
		}

		//Turn on the power control circuit
		HAL_GPIO_WritePin(RF_APC_SW_GPIO_Port, RF_APC_SW_Pin, GPIO_PIN_SET);
	}
	else
	{
		//Turn off the power control circuit
		HAL_GPIO_WritePin(RF_APC_SW_GPIO_Port, RF_APC_SW_Pin, GPIO_PIN_RESET);

		//Turn off Tx Voltages to prevent transmission.
		HAL_GPIO_WritePin(T5_V_SW_GPIO_Port, T5_V_SW_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(T5_U_SW_GPIO_Port, T5_U_SW_Pin, GPIO_PIN_RESET);
	}

}

void radioSetRx(uint8_t band)
{
	//Turn off the power control circuit
	HAL_GPIO_WritePin(RF_APC_SW_GPIO_Port, RF_APC_SW_Pin, GPIO_PIN_RESET);

	//Turn off Tx Voltages to prevent transmission.
	HAL_GPIO_WritePin(T5_V_SW_GPIO_Port, T5_V_SW_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(T5_U_SW_GPIO_Port, T5_U_SW_Pin, GPIO_PIN_RESET);

	//Turn on receiver voltages. (thats what the TYT firmware does, both receivers are on together)

	HAL_GPIO_WritePin(R5_V_SW_GPIO_Port, R5_V_SW_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(R5_U_SW_GPIO_Port, R5_U_SW_Pin, GPIO_PIN_SET);

	trxIsTransmitting = false;
	if (trxGetMode() == RADIO_MODE_ANALOG)
	{
		HRC6000SetFMRx();
	}
	else
	{
		HRC6000SetDMR();
	}
}


void radioReadVoxAndMicStrength(void)
{
	trxTxVox = adcGetVOX();
	trxTxMic = trxTxVox;					//MD9600 doesn't have separate Signals so use Vox for both.
}

void radioReadRSSIAndNoiseForBand(uint8_t band)
{
	if (band == RADIO_BAND_VHF)
	{
		currentRadioDevice->trxRxSignal = adcGetVHFRSSI();
		currentRadioDevice->trxRxNoise = adcGetVHFNoise();
	}
	else
	{
		currentRadioDevice->trxRxSignal = adcGetUHFRSSI();
		currentRadioDevice->trxRxNoise = adcGetUHFNoise();
	}
	trxDMRSynchronisedRSSIReadPending = false;
}

void radioRxCSSOff(void)
{
	//turn off the receive CTCSS detection;
	//don't really need to do anything here, the decode runs all of the time anyway
}

static int getCSSToneIndex(uint16_t tone)
{
	//the TYT firmware uses a PWM output to directly generate the CTCSS Tone.
	//however the HR-C6000 can also generate tones so we will use that instead
	//this uses a table so we need to convert the CTCSS tone to the Table Index.
	uint8_t HRCToneIndex = 1;

	for (int i = 0; i < ARRAY_SIZE(HRC_CTCSSTones); i++)
	{
		if (HRC_CTCSSTones[i] == tone)
		{
			HRCToneIndex = i + 1;
			break;
		}
	}

	return HRCToneIndex;
}

void radioRxCTCSOn(uint16_t tone)
{
	HRC6000SetRxCTCSS(getCSSToneIndex(tone));
}

void radioRxDCSOn(uint16_t code, bool inverted)
{
	HRC6000SetRxDCS(code, inverted);
}

void radioTxCSSOff(void)
{
	HRC6000SetTxCTCSS(0);
}

void radioTxCTCSOn(uint16_t tone)
{
	HRC6000SetTxCTCSS(getCSSToneIndex(tone));
}

void radioTxDCSOn(uint16_t code, bool inverted)
{
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
	AT1846SSetTxCSS(code, inverted);
#elif defined(PLATFORM_MD9600)
	HRC6000SetTxDCS(code, inverted);
#else
#error UNSUPPORTED PLATFORM
#endif
}

bool radioCheckCSS(uint16_t tone, CodeplugCSSTypes_t type)
{
	UNUSED_PARAMETER(tone);
	UNUSED_PARAMETER(type);
//test if CTCSS or DCS is being received and return true if it is
	return HRC6000CheckCSS();
}

void radioSetTone1(int tonefreq)
{
	HRC6000SendTone(tonefreq);
}


void radioSetMicGainFM(uint8_t gain)
{
	HRC6000SetMicGainFM(gain);
}


static void radioSetAudioMutePWM(uint32_t val)
{
	TIM_OC_InitTypeDef sConfigOC;
	sConfigOC.OCMode = TIM_OCMODE_PWM1;
	sConfigOC.Pulse = val;
	sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
	sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_1);
	HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
}

void radioAudioAmp(bool on)
{
	const uint32_t MIN_PWM_VALUE_TO_MUTE_AUDIO = 30;
	const uint32_t MAX_PWM_VALUE = 100;

	if (on)
	{
		radioSetAudioMutePWM(0);//		HAL_GPIO_WritePin(AF_SW_GPIO_Port, AF_SW_Pin, GPIO_PIN_RESET);				//Unmute the Audio from the volume control.

		if (audioPathFromFM)
		{
			HRC6000SetLineOut(false);													//Disable the Line Output
			HAL_GPIO_WritePin(FM_MUTE_GPIO_Port, FM_MUTE_Pin, GPIO_PIN_SET);		//Unmute the FM Audio to the volume control

		}
		else
		{
			HAL_GPIO_WritePin(FM_MUTE_GPIO_Port, FM_MUTE_Pin, GPIO_PIN_RESET);		//Mute the FM Audio to the volume control
			HRC6000SetLineOut(true);													//Enable the Line Output
		}
		ampIsOn = true;
	}
	else
	{
		radioSetAudioMutePWM((settingsIsOptionBitSet(BIT_SPEAKER_CLICK_SUPPRESS) && (audioAmpIsMuted() == false)) ? MIN_PWM_VALUE_TO_MUTE_AUDIO : MAX_PWM_VALUE); //HAL_GPIO_WritePin(AF_SW_GPIO_Port, AF_SW_Pin, GPIO_PIN_SET);					//Mute the audio from the volume control
		HAL_GPIO_WritePin(FM_MUTE_GPIO_Port, FM_MUTE_Pin, GPIO_PIN_RESET);			    //Mute the FM audio to the volume control (we can always do this regardless of the actual audio source)
		HRC6000SetLineOut(false);															//Disable the Line Output
		ampIsOn = false;
	}
}

void radioSetAudioPath(bool fromFM)
{
	audioPathFromFM = fromFM;

	if (ampIsOn)												//if the audio source is being changed while the amplifier is on then we need to update the audio amp control
	{
		radioAudioAmp(true);
	}
}
