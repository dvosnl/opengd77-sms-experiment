/*
 * Copyright (C) 2021-2025 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
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

#include "main.h"
#include "usb_device.h"
#include "interfaces/batteryAndPowerManagement.h"
#include "user_interface/uiGlobals.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "hardware/HR-C6000.h"
#include "usb/usb_com.h"
#include "functions/ticks.h"
#include "functions/codeplug.h"
#include "hardware/radioHardwareInterface.h"
#include "interfaces/gps.h"
#include "interfaces/batteryRAM.h"
#include "interfaces/settingsStorage.h"
#if defined(PLATFORM_MD9600)
#include "interfaces/remoteHead.h"
#endif
#include "interfaces/gps.h"

//#define DEBUG_HARDWARE_SCREEN 1

static uint32_t lowBatteryCount = 0;
#define LOW_BATTERY_INTERVAL                       ((1000 * 60) * 5) // 5 minute;
#define LOW_BATTERY_WARNING_VOLTAGE_DIFFERENTIAL   6	// Offset between the minimum voltage and when the battery warning audio starts. 6 = 0.6V
#define LOW_BATTERY_VOLTAGE_RECOVERY_TIME          30000 // 30 seconds
#define SUSPEND_LOW_BATTERY_RATE                   1000 // 1 second

#define BATTERY_VOLTAGE_TICK_RELOAD                100
#define BATTERY_VOLTAGE_CALLBACK_TICK_RELOAD       20


static int batteryVoltageCallbackTick = 0;
static int batteryVoltageTick = BATTERY_VOLTAGE_TICK_RELOAD;

volatile float averageBatteryVoltage = 0;
static volatile float previousAverageBatteryVoltage;
volatile int batteryVoltage = 0;



#if !defined(PLATFORM_GD77S)
ticksTimer_t apoTimer;
#endif

void showLowBattery(void)
{
	showErrorMessage(currentLanguage->low_battery);
}

bool batteryIsLowWarning(void)
{
	return (lowBatteryCount > LOW_BATTERY_VOLTAGE_RECOVERY_TIME);
}

bool batteryIsLowVoltageWarning(void)
{
	return (batteryVoltage < (CUTOFF_VOLTAGE_LOWER_HYST + LOW_BATTERY_WARNING_VOLTAGE_DIFFERENTIAL));
}

bool batteryIsLowCriticalVoltage(bool isSuspended)
{
	return (batteryVoltage < CUTOFF_VOLTAGE_LOWER_HYST);
}

bool batteryLastReadingIsCritical(bool isSuspended)
{
	return (adcGetBatteryVoltage() < CUTOFF_VOLTAGE_UPPER_HYST);
}

void batteryChecking(void)
{
	static ticksTimer_t lowBatteryBeepTimer = { 0, 0 };
	static ticksTimer_t lowBatteryHeaderRedrawTimer = { 0, 0 };
	static uint32_t lowBatteryCriticalCount = 0;
	bool lowBatteryWarning = batteryIsLowVoltageWarning();
	bool batIsLow = false;

	// Low battery threshold is reached after 30 seconds, in total, of lowBatteryWarning.
	// Once reached, another 30 seconds is added to the counter to avoid retriggering on voltage fluctuations.
	lowBatteryCount += (lowBatteryWarning
			? ((lowBatteryCount <= (LOW_BATTERY_VOLTAGE_RECOVERY_TIME * 2)) ? ((lowBatteryCount == LOW_BATTERY_VOLTAGE_RECOVERY_TIME) ? LOW_BATTERY_VOLTAGE_RECOVERY_TIME : 1) : 0)
			: (lowBatteryCount ? -1 : 0));

	// Do we need to redraw the header row now ?
	if ((batIsLow = batteryIsLowWarning()) && ticksTimerHasExpired(&lowBatteryHeaderRedrawTimer))
	{
		headerRowIsDirty = true;
		ticksTimerStart(&lowBatteryHeaderRedrawTimer, 500);
	}

	if ((settingsUsbMode != USB_MODE_HOTSPOT) &&
#if defined(PLATFORM_GD77S)
			(trxTransmissionEnabled == false) &&
#else
			(menuSystemGetCurrentMenuNumber() != UI_TX_SCREEN) &&
#endif
			batIsLow &&
			ticksTimerHasExpired(&lowBatteryBeepTimer))
	{

		if (melody_play == NULL)
		{
			if (nonVolatileSettings.audioPromptMode < AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
			{
				soundSetMelody(MELODY_LOW_BATTERY);
			}
			else
			{
				voicePromptsInit();
				voicePromptsAppendLanguageString(currentLanguage->low_battery);
				voicePromptsPlay();
			}

			// Let the beep sound, or the VP, to finish to play before resuming the scan (otherwise is could be silent).
			if (uiDataGlobal.Scan.active)
			{
				uiDataGlobal.Scan.active = false;
				watchdogRun(false);

				while ((melody_play != NULL) || voicePromptsIsPlaying())
				{
					voicePromptsTick();
					soundTickMelody();

					osDelay(1);
				}

				watchdogRun(true);
				uiDataGlobal.Scan.active = true;
			}

			ticksTimerStart(&lowBatteryBeepTimer, LOW_BATTERY_INTERVAL);
		}
	}

	// Check if the battery has reached critical voltage (power off)
	bool lowBatteryCritical = batteryIsLowCriticalVoltage(false);

	// Critical battery threshold is reached after 30 seconds, in total, of lowBatteryCritical.
	lowBatteryCriticalCount += (lowBatteryCritical ? 1 : (lowBatteryCriticalCount ? -1 : 0));

	// Low battery or poweroff (non RD-5R)
	bool powerSwitchIsOff = false;


	if ((powerSwitchIsOff || lowBatteryCritical) && (menuSystemGetCurrentMenuNumber() != UI_POWER_OFF))
	{
		// is considered as flat (stable value), now stop the firmware: make it silent
		if ((lowBatteryCritical && (lowBatteryCriticalCount > LOW_BATTERY_VOLTAGE_RECOVERY_TIME)) || powerSwitchIsOff)
		{
			radioAudioAmp(false);
			soundSetMelody(NULL);
		}

		// Avoids delayed power off (on non RD-5R) if the power switch is turned off while in low battery condition
		if (lowBatteryCritical && (powerSwitchIsOff == false))
		{
			// Now, the battery is considered as flat (stable value), powering off.
			if (lowBatteryCriticalCount > LOW_BATTERY_VOLTAGE_RECOVERY_TIME)
			{
				showLowBattery();
				powerOffFinalStage(false, false);
			}
		}
#if ! defined(PLATFORM_RD5R)
		else
		{
			menuSystemPushNewMenu(UI_POWER_OFF);
		}
#endif // ! PLATFORM_RD5R
	}
}

void batteryUpdate(void)
{
	batteryVoltageTick++;
	if (batteryVoltageTick >= BATTERY_VOLTAGE_TICK_RELOAD)
	{
		if (previousAverageBatteryVoltage != averageBatteryVoltage)
		{
			previousAverageBatteryVoltage = averageBatteryVoltage;
			headerRowIsDirty = true;
		}

		batteryVoltageCallbackTick++;
		if (batteryVoltageCallbackTick >= BATTERY_VOLTAGE_CALLBACK_TICK_RELOAD)
		{
			menuRadioInfosPushBackVoltage(averageBatteryVoltage);
			batteryVoltageCallbackTick = 0;
		}

		batteryVoltageTick = 0;
	}
}

// Note. This function is currently not used, as the radio is now rebooted to enter its powered off / standby state
void powerOff(void)
{
#if 0
	MX_USB_DEVICE_DeInit(); // Deinit USB
	displayEnableBacklight(false, 0);
	displaySetDisplayPowerMode(false);
	gpsPower(false);  // Turn off the power to the Microphone and GPS
	radioPowerOff();
#endif
}

void powerDown(bool doNotSavePowerOffState)
{
	if (!doNotSavePowerOffState)
	{
#if defined(PLATFORM_MD9600)
		uint16_t radioPowerState = RADIO_POWER_STATE_STANDBY_FLAG;

		batteryRAM_Write(0, (uint8_t *)&radioPowerState, 2U);
#endif
	}

	settingsSaveSettings(true, true);
	codeplugSaveLastUsedChannelInZone();

#if defined(HAS_GPS)
#if defined(LOG_GPS_DATA)
	gpsLoggingStop();
#endif
	gpsOff();

#if defined(PLATFORM_MD9600)
	gpsPower(false);
#endif

#endif

	// Give it a bit of time to finish to write the flash (avoiding corruptions).
	uint32_t m = ticksGetMillis();
	while (true)
	{
		if ((ticksGetMillis() - m) > 300U)
		{
			break;
		}

		osDelay(1);
	}

#if defined(PLATFORM_MD9600)
	if(remoteHeadActive)
	{
		remoteHeadPowerDown();
	}
#endif

	NVIC_SystemReset();

	while(true);
}

void die(bool usbMonitoring, bool maintainRTC, bool forceSuspend, bool safeBoot)
{
#if !defined(PLATFORM_RD5R) && !defined(PLATFORM_GD77S)
	uint32_t lowBatteryCriticalCount = 0;
	ticksTimer_t nextPITCounterRunTimer = { ticksGetMillis(), SUSPEND_LOW_BATTERY_RATE };
#if defined(DEBUG_HARDWARE_SCREEN)
	uint16_t y = 0;

#else
	UNUSED_PARAMETER(safeBoot);
#endif

	if (!maintainRTC)
	{
/*		vk3yy commented out as not supported by the MD9600
		GPIO_PinWrite(GPIO_Keep_Power_On, Pin_Keep_Power_On, 0);// This is normally already done before this function is called.
*/
		// But do it again, just in case, as its important that the radio will turn off when the power control is turned to off
	}
#endif

	audioAmpDisable(AUDIO_AMP_CHANNEL_RF);
	audioAmpDisable(AUDIO_AMP_CHANNEL_BEEP);
	audioAmpDisable(AUDIO_AMP_CHANNEL_PROMPT);

	LedWrite(LED_GREEN, 0);
	LedWrite(LED_RED, 0);

	trxResetSquelchesState(RADIO_DEVICE_PRIMARY); // Could be put in sleep state and awaken with a signal, so this will re-enable the audio AMP

	trxPowerUpDownRxAndC6000(false, true);

	if (usbMonitoring)
	{
#if defined(DEBUG_HARDWARE_SCREEN)
		ticksTimer_t checkBatteryTimer = { .start = ticksGetMillis(), .timeout = 1000U };

#warning SAFEBOOT HARDWARE DEBUG SCREEN ENABLED
		if (safeBoot)
		{
			char cpuTypeBuf[SCREEN_LINE_BUFFER_SIZE] = {0};
			uint32_t vpHeader[2];

			displayClearBuf();

			sprintf(cpuTypeBuf, "Flash Type :0x%X", flashChipPartNumber);
			displayPrintAt(0, y, cpuTypeBuf , FONT_SIZE_1);
			y += 8;

			sprintf(cpuTypeBuf, "NVRAM :0x%X %u", nonVolatileSettings.magicNumber, settingsIsOptionBitSet(BIT_SETTINGS_UPDATED));
			displayPrintAt(0, y, cpuTypeBuf , FONT_SIZE_1);
			y += 8;

			sprintf(cpuTypeBuf, "Codec :%s", (codecIsAvailable() ? "OK" : "KO"));
			displayPrintAt(0, y, cpuTypeBuf , FONT_SIZE_1);
			y += 8;

			SPI_Flash_read(VOICE_PROMPTS_FLASH_HEADER_ADDRESS, (uint8_t *)&vpHeader, sizeof(vpHeader));
			sprintf(cpuTypeBuf, "VP :0x%X 0x%X", vpHeader[0], vpHeader[1]);
			displayPrintAt(0, y, cpuTypeBuf , FONT_SIZE_1);
			y += 8;

			displayRender();
		}
#endif

		while(true)
		{
			tick_com_request();
			vTaskDelay((0U / portTICK_PERIOD_MS));

#if defined(DEBUG_HARDWARE_SCREEN)
			if (safeBoot)
			{
				if (ticksTimerHasExpired(&checkBatteryTimer)) // reuse this timer
				{
					char buf[SCREEN_LINE_BUFFER_SIZE];
					uint16_t ypos = y;
					uint16_t fpButtons = buttonsFrontPanelRead();
					uint8_t micButtons = micButtonsRead();
					uint8_t rotDir = ' ';

					if (rotaryData.Direction != 0)
					{
						rotDir = ((rotaryData.Direction == 1) ? '+' : '-');

						nonVolatileSettings.displayBacklightPercentage[DAY] += ((rotaryData.Direction > 0) ?
								(nonVolatileSettings.displayBacklightPercentage[DAY] >= 10 ? 10 : 1)
								:
								(nonVolatileSettings.displayBacklightPercentage[DAY] >= 20 ? -10 : -1));
						nonVolatileSettings.displayBacklightPercentage[DAY] = CLAMP(nonVolatileSettings.displayBacklightPercentage[DAY], 0, 100);
						gpioSetDisplayBacklightIntensityPercentage(nonVolatileSettings.displayBacklightPercentage[DAY]);
						rotaryData.Direction = 0;
					}

					displayFillRect(0, ypos, DISPLAY_SIZE_X, DISPLAY_SIZE_Y - y, true);

					sprintf(buf, "Rot :[%c], Bcl :%d%%", rotDir, nonVolatileSettings.displayBacklightPercentage[DAY]);
					displayPrintAt(0, ypos, buf , FONT_SIZE_1);
					ypos += 8;

					sprintf(buf, "V :%d, ADC :%d", (int)previousAverageBatteryVoltage, adcGetBatteryVoltage());
					displayPrintAt(0, ypos, buf , FONT_SIZE_1);
					ypos += 8;

					sprintf(buf, "Btn :0x%02X [%c] - [%u %u]", micButtons, ((micButtons != 0x00) ? micButtons : ' '),
							(remoteHeadActive ? remoteHeadPTT : 0), (HAL_GPIO_ReadPin(PTT_GPIO_Port, PTT_Pin) == 0));
					displayPrintAt(0, ypos, buf , FONT_SIZE_1);
					ypos += 8;

					sprintf(buf, "fRmt :%u, fKey :0x%04X", remoteHeadActive, fpButtons);
					displayPrintAt(0, ypos, buf , FONT_SIZE_1);
					//ypos += 8;

					displayRenderRows(y / 8, ((ypos / 8) + (8 / 8)));
					ticksTimerStart(&checkBatteryTimer, 200U);
				}

				batteryUpdate();
			}
#endif
		}
	}
	else
	{
#if defined(HAS_GPS)
#if defined(LOG_GPS_DATA)
		gpsLoggingStop();
#endif
		gpsOff();

#if defined(PLATFORM_MD9600)
		gpsPower(false);
#endif

#endif



/* vk3yy commented out as not supported by the MD9600
		clockManagerSetRunMode(kAPP_PowerModeHsrun,CLOCK_MANAGER_SPEED_RUN);
*/
		bool batteryIsCritical = batteryLastReadingIsCritical(maintainRTC);

/* VK3KYY Need to port to MD9600
		watchdogDeinit();
*/
		if (batteryIsCritical == false)
		{
			displaySetDisplayPowerMode(false);
		}

		while(true)
		{
			if (batteryIsCritical)
			{
#if !defined(PLATFORM_RD5R)

/* vk3yy commented out as not supported by the MD9600
 *
 			GPIO_PinWrite(GPIO_Keep_Power_On, Pin_Keep_Power_On, 0);
 			*/
#endif

/* vk3yy commented out as not supported by the MD9600
				clockManagerSetRunMode(kAPP_PowerModeVlpr,0);
*/
				while(true);
			}

#if !defined(PLATFORM_RD5R) && !defined(PLATFORM_GD77S)
			if (uiDataGlobal.SatelliteAndAlarmData.alarmType == ALARM_TYPE_NONE)
			{

				/* vk3yy commented out as not supported by the MD9600
				if (GPIO_Power_Switch, Pin_Power_Switch) == 0)
				{


					// User wants to go in bootloader mode
					if (buttonsRead() == (BUTTON_SK1 | BUTTON_SK2))
					{
						watchdogRebootNow();
					}

					wakeFromSleep();
					return;
				}
				*/
			}

			if (ticksTimerHasExpired(&nextPITCounterRunTimer))
			{
				// Check if the battery has reached critical voltage (power off)
				bool lowBatteryCritical = batteryIsLowCriticalVoltage(maintainRTC);

				// Critical battery threshold is reached after 30 seconds, in total, of lowBatteryCritical.
				lowBatteryCriticalCount += (lowBatteryCritical ? 1 : (lowBatteryCriticalCount ? -1 : 0));

				if (lowBatteryCritical && (lowBatteryCriticalCount > (LOW_BATTERY_VOLTAGE_RECOVERY_TIME / 1000) /* 30s in total */))
				{
/* vk3yy commented out as not supported by the MD9600
					GPIO_PinWrite(GPIO_Keep_Power_On, Pin_Keep_Power_On, 0);
					*/
					while(true);
				}

				ticksTimerStart(&nextPITCounterRunTimer, SUSPEND_LOW_BATTERY_RATE);
			}

			if (uiDataGlobal.SatelliteAndAlarmData.alarmType != ALARM_TYPE_NONE)
			{

/* vk3yy commented out as not supported by the MD9600
				bool powerSwitchIsOff =	(GPIO_PinRead(GPIO_Power_Switch, Pin_Power_Switch) != 0);
				if (powerSwitchIsOff)
				{
					uiDataGlobal.SatelliteAndAlarmData.alarmType = ALARM_TYPE_NONE;
				}
*/
			}


			if (uiDataGlobal.SatelliteAndAlarmData.alarmType != ALARM_TYPE_NONE)
			{

/* vk3yy commented out as not supported by the MD9600
				bool isOrange = (GPIO_PinRead(GPIO_Orange, Pin_Orange) == 0);
				if (isOrange)
								{
					wakeFromSleep();
					return;
				}
*/
			}

			if (uiDataGlobal.SatelliteAndAlarmData.alarmType == ALARM_TYPE_SATELLITE
					|| uiDataGlobal.SatelliteAndAlarmData.alarmType == ALARM_TYPE_CLOCK)
			{
				if (uiDataGlobal.dateTimeSecs >= uiDataGlobal.SatelliteAndAlarmData.alarmTime)
				{
					wakeFromSleep();
					return;
				}
			}
#endif
		}

	}
}

void wakeFromSleep(void)
{
#if !defined(PLATFORM_RD5R)
	if (menuSystemGetPreviousMenuNumber() == MENU_SATELLITE)
	{
/* vk3yy commented out as not supported by the MD9600
//		clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_SPEED_HS_RUN);
		clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_SPEED_RUN);
*/
	}
	else
	{
/* vk3yy commented out as not supported by the MD9600
		clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_SPEED_RUN);
*/
	}

/*
	GPIO_PinWrite(GPIO_Keep_Power_On, Pin_Keep_Power_On, 1);// This is normally already done before this function is called.
	// But do it again, just in case, as its important that the radio will turn off when the power control is turned to off
*/

	trxPowerUpDownRxAndC6000(true, true);

	// Reset counters before enabling watchdog
	hrc6000Task.AliveCount = TASK_FLAGGED_ALIVE;
	beepTask.AliveCount = TASK_FLAGGED_ALIVE;

/* VK3KYY Need to port to MD9600
	watchdogInit();
*/

	displaySetDisplayPowerMode(true);

	if (trxGetMode() == RADIO_MODE_DIGITAL)
	{
		HRC6000ResetTimeSlotDetection();
		HRC6000InitDigitalDmrRx();
	}

#if defined(HAS_GPS)
	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_OFF)
	{
		gpsOn();
#if defined(LOG_GPS_DATA)
		gpsLoggingStart();
#endif
	}
#endif

	if ((nonVolatileSettings.autolockTimer > 0) && ticksTimerIsEnabled(&autolockTimer))
	{
		ticksTimerStart(&autolockTimer, (nonVolatileSettings.autolockTimer * 30000U));
	}

	voicePromptsInit(); // Flush the VPs
#endif
}

void powerOffFinalStage(bool maintainRTC, bool forceSuspend)
{
	uint32_t m;

	// If TXing, get back to RX (this function can be called on low battery event).
	if (trxTransmissionEnabled)
	{
		trxTransmissionEnabled = false;
		trxActivateRx(true);
		trxIsTransmitting = false;

		LedWrite(LED_RED, 0);//LEDs_PinWrite(GPIO_LEDred, Pin_LEDred, 0);
	}

	// Restore DMR filter settings if the radio is turned off whilst in monitor mode
	if (monitorModeData.isEnabled)
	{
		nonVolatileSettings.dmrCcTsFilter = monitorModeData.savedDMRCcTsFilter;
		nonVolatileSettings.dmrDestinationFilter = monitorModeData.savedDMRDestinationFilter;
	}

	// If user was in a private call when they turned the radio off we need to restore the last Tg prior to stating the Private call.
	// to the nonVolatile Setting overrideTG, otherwise when the radio is turned on again it be in PC mode to that station.
	if ((trxTalkGroupOrPcId >> 24) == PC_CALL_FLAG)
	{
		settingsSet(nonVolatileSettings.overrideTG, uiDataGlobal.tgBeforePcMode);
	}

	menuHotspotRestoreSettings();

	codeplugSaveLastUsedChannelInZone();

#if defined(LOG_GPS_DATA)
	gpsLoggingStop();
#endif

	m = ticksGetMillis();
	settingsSaveSettings(true, true);

	// Give it a bit of time before pulling the plug as DM-1801 EEPROM looks slower
	// than GD-77 to write, then quickly power cycling triggers settings reset.
	while (true)
	{
		if ((ticksGetMillis() - m) > 50)
		{
			break;
		}

		osDelay(1);
	}

	displayEnableBacklight(false, 0);

#if !defined(PLATFORM_RD5R)
	// This turns the power off to the CPU.
	if (!maintainRTC)
	{
/* vk3yy commented out as not supported by the MD9600
 * GPIO_PinWrite(GPIO_Keep_Power_On, Pin_Keep_Power_On, 0);
 *
 */
	}
#endif

	die(false, maintainRTC, forceSuspend, false);
}

#if !defined(PLATFORM_GD77S)
void apoTick(bool eventFromOperator)
{
	if (nonVolatileSettings.apo > 0)
	{
		int currentMenu = menuSystemGetCurrentMenuNumber();

		// Reset APO timer:
		//   - on events
		//   - when scanning
		//   - when user has set a Satellite alarm
		//   - when transmissing, while in hotspot mode or while using the CPS
		//   - on RF activity
		if (eventFromOperator ||
				uiDataGlobal.Scan.active ||
				((currentMenu == UI_TX_SCREEN) || (currentMenu == UI_HOTSPOT_MODE) || (currentMenu == UI_CPS)) ||
				(uiDataGlobal.SatelliteAndAlarmData.alarmType != ALARM_TYPE_NONE))
		{
			if (uiNotificationIsVisible() && (uiNotificationGetId() == NOTIFICATION_ID_USER_APO))
			{
				uiNotificationHide(true);
			}

			ticksTimerStart(&apoTimer, ((nonVolatileSettings.apo * 30) * 60000U));
		}

		// No event in the last 'apo' time => Suspend
		if (ticksTimerHasExpired(&apoTimer))
		{
			powerDown(false);
		}
		else
		{
			 // 1 minute or less is remaining, it's time to show the APO notification
			if ((ticksTimerRemaining(&apoTimer) <= 60000U) &&
					((uiNotificationIsVisible() && (uiNotificationGetId() == NOTIFICATION_ID_USER_APO)) == false))
			{
				if (nonVolatileSettings.audioPromptMode < AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
				{
					soundSetMelody(MELODY_APO_TRIGGERED);
				}
				else
				{
					voicePromptsInit();
					voicePromptsAppendLanguageString(currentLanguage->auto_power_off);
					voicePromptsPlay();
				}

				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER_APO, 60000U, currentLanguage->auto_power_off, true);
			}
		}
	}
}
#endif
