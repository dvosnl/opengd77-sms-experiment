/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <FreeRTOS.h>
#include <task.h>

#include "applicationMain.h"
#include "utils.h"
#include "io/buttons.h"
#include "io/LEDs.h"
#include "io/keyboard.h"
#include "io/rotary_switch.h"
#include "io/display.h"
#include "functions/vox.h"
#include "hardware/ST7567.h"
#include "hardware/HR-C6000.h"
#include "interfaces/i2c.h"
#include "interfaces/hr-c6000_spi.h"
#include "interfaces/i2s.h"
#include "interfaces/wdog.h"
#include "interfaces/adc.h"
#include "interfaces/dac.h"
#include "interfaces/pit.h"
#include "functions/sound.h"
#include "functions/trx.h"
#include "hardware/SPI_Flash.h"
#include "hardware/EEPROM.h"

#include "cmsis_os.h"

extern uint32_t NumInterruptPriorityBits;

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
extern ADC_HandleTypeDef hadc1;
extern I2S_HandleTypeDef hi2s3;
extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern DAC_HandleTypeDef hdac;
extern RTC_HandleTypeDef hrtc;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim6;
extern volatile bool aprsIsrFlag;
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
void SystemClock_Config(void);
void MX_USART3_UART_Init(void);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define DMR_SPI_CS_Pin GPIO_PIN_2
#define DMR_SPI_CS_GPIO_Port GPIOE
#define DMR_SPI_CLK_Pin GPIO_PIN_3
#define DMR_SPI_CLK_GPIO_Port GPIOE
#define DMR_SPI_MISO_Pin GPIO_PIN_4
#define DMR_SPI_MISO_GPIO_Port GPIOE
#define DMR_SPI_MOSI_Pin GPIO_PIN_5
#define DMR_SPI_MOSI_GPIO_Port GPIOE
#define C6000_PWD_Pin GPIO_PIN_6
#define C6000_PWD_GPIO_Port GPIOE
#define U_V_AF_SW_Pin GPIO_PIN_13
#define U_V_AF_SW_GPIO_Port GPIOC
#define TIME_SLOT_INTER_Pin GPIO_PIN_0
#define TIME_SLOT_INTER_GPIO_Port GPIOC
#define TIME_SLOT_INTER_EXTI_IRQn EXTI0_IRQn
#define SYS_INTER_Pin GPIO_PIN_1
#define SYS_INTER_GPIO_Port GPIOC
#define SYS_INTER_EXTI_IRQn EXTI1_IRQn
#define RF_TX_INTER_Pin GPIO_PIN_2
#define RF_TX_INTER_GPIO_Port GPIOC
#define RF_TX_INTER_EXTI_IRQn EXTI2_IRQn
#define RX_2T_5T_Pin GPIO_PIN_3
#define RX_2T_5T_GPIO_Port GPIOC
#define RSSI_VHF_Pin GPIO_PIN_0
#define RSSI_VHF_GPIO_Port GPIOA
#define Battery_voltage_Pin GPIO_PIN_1
#define Battery_voltage_GPIO_Port GPIOA
#define QT_DQT_IN_Pin GPIO_PIN_2
#define QT_DQT_IN_GPIO_Port GPIOA
#define VOX_Pin GPIO_PIN_3
#define VOX_GPIO_Port GPIOA
#define TV_U_Pin GPIO_PIN_4
#define TV_U_GPIO_Port GPIOA
#define APC_TV_V_Pin GPIO_PIN_5
#define APC_TV_V_GPIO_Port GPIOA
#define MIC_KEYPAD_P2_Pin GPIO_PIN_6
#define MIC_KEYPAD_P2_GPIO_Port GPIOA
#define MIC_KEYPAD_P1_Pin GPIO_PIN_7
#define MIC_KEYPAD_P1_GPIO_Port GPIOA
#define RF_APC_SW_Pin GPIO_PIN_4
#define RF_APC_SW_GPIO_Port GPIOC
#define NOISE_VHF_Pin GPIO_PIN_5
#define NOISE_VHF_GPIO_Port GPIOC
#define RSSI_UHF_Pin GPIO_PIN_0
#define RSSI_UHF_GPIO_Port GPIOB
#define NOISE_UHF_Pin GPIO_PIN_1
#define NOISE_UHF_GPIO_Port GPIOB
#define FM_SW_Pin GPIO_PIN_2
#define FM_SW_GPIO_Port GPIOB
#define FM_MUTE_Pin GPIO_PIN_7
#define FM_MUTE_GPIO_Port GPIOE
#define VCO_VCC_U_SW_Pin GPIO_PIN_8
#define VCO_VCC_U_SW_GPIO_Port GPIOE
#define VCO_VCC_V_SW_Pin GPIO_PIN_9
#define VCO_VCC_V_SW_GPIO_Port GPIOE
#define PTT_Pin GPIO_PIN_10
#define PTT_GPIO_Port GPIOE
#define IF_CS_V_Pin GPIO_PIN_11
#define IF_CS_V_GPIO_Port GPIOE
#define IF_CS_U_Pin GPIO_PIN_12
#define IF_CS_U_GPIO_Port GPIOE
#define IF_DATA_Pin GPIO_PIN_13
#define IF_DATA_GPIO_Port GPIOE
#define IF_CLK_Pin GPIO_PIN_14
#define IF_CLK_GPIO_Port GPIOE
#define IF_RST_Pin GPIO_PIN_15
#define IF_RST_GPIO_Port GPIOE
#define ROTARY_SW_A_Pin GPIO_PIN_10
#define ROTARY_SW_A_GPIO_Port GPIOB
#define ROTARY_SW_A_EXTI_IRQn EXTI15_10_IRQn
#define ROTARY_SW_B_Pin GPIO_PIN_11
#define ROTARY_SW_B_GPIO_Port GPIOB
#define SPI_Flash_CS_Pin GPIO_PIN_12
#define SPI_Flash_CS_GPIO_Port GPIOB
#define SPI2_SCK_Pin GPIO_PIN_13
#define SPI2_SCK_GPIO_Port GPIOB
#define SPI2_MISO_Pin GPIO_PIN_14
#define SPI2_MISO_GPIO_Port GPIOB
#define SPI2_MOSI_Pin GPIO_PIN_15
#define SPI2_MOSI_GPIO_Port GPIOB
#define PLL_CS_V_Pin GPIO_PIN_8
#define PLL_CS_V_GPIO_Port GPIOD
#define PLL_CS_U_Pin GPIO_PIN_9
#define PLL_CS_U_GPIO_Port GPIOD
#define PLL_DATA_Pin GPIO_PIN_10
#define PLL_DATA_GPIO_Port GPIOD
#define PLL_CLK_Pin GPIO_PIN_11
#define PLL_CLK_GPIO_Port GPIOD
#define LCD_Reset_Pin GPIO_PIN_12
#define LCD_Reset_GPIO_Port GPIOD
#define LCD_RS_Pin GPIO_PIN_13
#define LCD_RS_GPIO_Port GPIOD
#define LCD_CS_Pin GPIO_PIN_14
#define LCD_CS_GPIO_Port GPIOD
#define Power_Control_Pin GPIO_PIN_15
#define Power_Control_GPIO_Port GPIOD
#define Display_Light_Pin GPIO_PIN_6
#define Display_Light_GPIO_Port GPIOC
#define CTC_DCS_PWM_Pin GPIO_PIN_7
#define CTC_DCS_PWM_GPIO_Port GPIOC
#define BEEP_PWM_Pin GPIO_PIN_8
#define BEEP_PWM_GPIO_Port GPIOC
#define C5_V_SW_Pin GPIO_PIN_9
#define C5_V_SW_GPIO_Port GPIOC
#define C5_U_SW_Pin GPIO_PIN_8
#define C5_U_SW_GPIO_Port GPIOA
#define GPS_Tx_Mic_Power_Pin GPIO_PIN_9
#define GPS_Tx_Mic_Power_GPIO_Port GPIOA
#define GPS_Rx_Pin GPIO_PIN_10
#define GPS_Rx_GPIO_Port GPIOA
#define R5_U_SW_Pin GPIO_PIN_13
#define R5_U_SW_GPIO_Port GPIOA
#define R5_V_SW_Pin GPIO_PIN_14
#define R5_V_SW_GPIO_Port GPIOA
#define KEYPAD_ROW2_K2_Pin GPIO_PIN_0
#define KEYPAD_ROW2_K2_GPIO_Port GPIOD
#define KEYPAD_ROW3_K3_Pin GPIO_PIN_1
#define KEYPAD_ROW3_K3_GPIO_Port GPIOD
#define KEYPAD_COL2_K4_Pin GPIO_PIN_2
#define KEYPAD_COL2_K4_GPIO_Port GPIOD
#define KEYPAD_COL1_K5_Pin GPIO_PIN_3
#define KEYPAD_COL1_K5_GPIO_Port GPIOD
#define KEYPAD_COL0_K6_Pin GPIO_PIN_4
#define KEYPAD_COL0_K6_GPIO_Port GPIOD
#define T5_V_SW_Pin GPIO_PIN_5
#define T5_V_SW_GPIO_Port GPIOD
#define T5_U_SW_Pin GPIO_PIN_6
#define T5_U_SW_GPIO_Port GPIOD
#define V_SPI_CS_Pin GPIO_PIN_7
#define V_SPI_CS_GPIO_Port GPIOD
#define V_SPI_SCK_Pin GPIO_PIN_3
#define V_SPI_SCK_GPIO_Port GPIOB
#define V_SPI_SDO_Pin GPIO_PIN_4
#define V_SPI_SDO_GPIO_Port GPIOB
#define V_SPI_SDI_Pin GPIO_PIN_5
#define V_SPI_SDI_GPIO_Port GPIOB
#define AF_SW_Pin GPIO_PIN_6
#define AF_SW_GPIO_Port GPIOB
#define U_V_MOD_SW_Pin GPIO_PIN_7
#define U_V_MOD_SW_GPIO_Port GPIOB
#define KEYPAD_ROW0_K0_Pin GPIO_PIN_0
#define KEYPAD_ROW0_K0_GPIO_Port GPIOE
#define KEYPAD_ROW1_K1_Pin GPIO_PIN_1
#define KEYPAD_ROW1_K1_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
