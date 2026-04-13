/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "io/buttons.h"
#include "user_interface/uiGlobals.h"
#include "functions/settings.h"
#include "hardware/HR-C6000.h"
#include "functions/trx.h"
#include "interfaces/gps.h"
#include "functions/aprs.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static void faultLogCaptureAndReset(faultLogReason_t reason, uint32_t *stackFrame, uint32_t excReturn);
static void hardFaultHandlerC(uint32_t *stackFrame, uint32_t excReturn);
static void memManageHandlerC(uint32_t *stackFrame, uint32_t excReturn);
static void busFaultHandlerC(uint32_t *stackFrame, uint32_t excReturn);
static void usageFaultHandlerC(uint32_t *stackFrame, uint32_t excReturn);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define FAULT_LOG_MAGIC                    0x464C5447U /* FLTG */
#define FAULT_LOG_VERSION                  0x00010000U
#define FAULT_LOG_STACK_WORDS              8U
#define FAULT_LOG_RAM_START                0x20000000U
#define FAULT_LOG_RAM_END                  (FAULT_LOG_RAM_START + (128U * 1024U))

enum
{
  FAULT_LOG_BKP_MAGIC = 0,
  FAULT_LOG_BKP_VERSION,
  FAULT_LOG_BKP_REASON,
  FAULT_LOG_BKP_EXC_RETURN,
  FAULT_LOG_BKP_MSP,
  FAULT_LOG_BKP_PSP,
  FAULT_LOG_BKP_R0,
  FAULT_LOG_BKP_R1,
  FAULT_LOG_BKP_R2,
  FAULT_LOG_BKP_R3,
  FAULT_LOG_BKP_R12,
  FAULT_LOG_BKP_LR,
  FAULT_LOG_BKP_PC,
  FAULT_LOG_BKP_PSR,
  FAULT_LOG_BKP_CFSR,
  FAULT_LOG_BKP_HFSR,
  FAULT_LOG_BKP_MMFAR,
  FAULT_LOG_BKP_BFAR,
  FAULT_LOG_BKP_DFSR,
  FAULT_LOG_BKP_CHECKSUM,
  FAULT_LOG_BKP_COUNT
};

static volatile uint32_t *faultLogGetBackupRegs(void)
{
  return &RTC->BKP0R;
}

static void faultLogEnableBackupWrite(void)
{
  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
  (void)RCC->APB1ENR;
  PWR->CR |= PWR_CR_DBP;
  RCC->BDCR |= RCC_BDCR_RTCEN;
}

static bool faultLogStackFrameIsValid(const uint32_t *stackFrame)
{
  uintptr_t addr = (uintptr_t)stackFrame;
  return ((addr >= FAULT_LOG_RAM_START) && (addr <= (FAULT_LOG_RAM_END - (FAULT_LOG_STACK_WORDS * sizeof(uint32_t)))));
}

static uint32_t faultLogComputeChecksum(volatile const uint32_t *regs)
{
  uint32_t checksum = 0x9E3779B9U;

  for (uint32_t i = FAULT_LOG_BKP_VERSION; i < FAULT_LOG_BKP_CHECKSUM; i++)
  {
    checksum ^= (regs[i] + (i * 0x45D9F3BU));
  }

  return checksum;
}

static void faultLogClearRaw(void)
{
  faultLogEnableBackupWrite();

  volatile uint32_t *regs = faultLogGetBackupRegs();

  for (uint32_t i = 0; i < FAULT_LOG_BKP_COUNT; i++)
  {
    regs[i] = 0U;
  }
}

static void faultLogWriteRaw(faultLogReason_t reason, uint32_t *stackFrame, uint32_t excReturn)
{
  faultLogEnableBackupWrite();

  volatile uint32_t *regs = faultLogGetBackupRegs();
  const bool validStack = faultLogStackFrameIsValid(stackFrame);

  regs[FAULT_LOG_BKP_MAGIC] = FAULT_LOG_MAGIC;
  regs[FAULT_LOG_BKP_VERSION] = FAULT_LOG_VERSION;
  regs[FAULT_LOG_BKP_REASON] = (uint32_t)reason;
  regs[FAULT_LOG_BKP_EXC_RETURN] = excReturn;
  regs[FAULT_LOG_BKP_MSP] = __get_MSP();
  regs[FAULT_LOG_BKP_PSP] = __get_PSP();

  regs[FAULT_LOG_BKP_R0] = validStack ? stackFrame[0] : 0U;
  regs[FAULT_LOG_BKP_R1] = validStack ? stackFrame[1] : 0U;
  regs[FAULT_LOG_BKP_R2] = validStack ? stackFrame[2] : 0U;
  regs[FAULT_LOG_BKP_R3] = validStack ? stackFrame[3] : 0U;
  regs[FAULT_LOG_BKP_R12] = validStack ? stackFrame[4] : 0U;
  regs[FAULT_LOG_BKP_LR] = validStack ? stackFrame[5] : 0U;
  regs[FAULT_LOG_BKP_PC] = validStack ? stackFrame[6] : 0U;
  regs[FAULT_LOG_BKP_PSR] = validStack ? stackFrame[7] : 0U;

  regs[FAULT_LOG_BKP_CFSR] = SCB->CFSR;
  regs[FAULT_LOG_BKP_HFSR] = SCB->HFSR;
  regs[FAULT_LOG_BKP_MMFAR] = SCB->MMFAR;
  regs[FAULT_LOG_BKP_BFAR] = SCB->BFAR;
  regs[FAULT_LOG_BKP_DFSR] = SCB->DFSR;
  regs[FAULT_LOG_BKP_CHECKSUM] = faultLogComputeChecksum(regs);
}

bool faultLogReadAndClear(faultLogInfo_t *outInfo)
{
  if (outInfo == NULL)
  {
    return false;
  }

  faultLogEnableBackupWrite();

  volatile uint32_t *regs = faultLogGetBackupRegs();

  if ((regs[FAULT_LOG_BKP_MAGIC] != FAULT_LOG_MAGIC) || (regs[FAULT_LOG_BKP_VERSION] != FAULT_LOG_VERSION))
  {
    return false;
  }

  if (regs[FAULT_LOG_BKP_CHECKSUM] != faultLogComputeChecksum(regs))
  {
    faultLogClearRaw();
    return false;
  }

  uint32_t reasonRaw = regs[FAULT_LOG_BKP_REASON];
  if ((reasonRaw < (uint32_t)FAULT_LOG_REASON_HARDFAULT) || (reasonRaw > (uint32_t)FAULT_LOG_REASON_USAGEFAULT))
  {
    reasonRaw = (uint32_t)FAULT_LOG_REASON_NONE;
  }

  outInfo->reason = (faultLogReason_t)reasonRaw;
  outInfo->excReturn = regs[FAULT_LOG_BKP_EXC_RETURN];
  outInfo->msp = regs[FAULT_LOG_BKP_MSP];
  outInfo->psp = regs[FAULT_LOG_BKP_PSP];
  outInfo->stackedLR = regs[FAULT_LOG_BKP_LR];
  outInfo->stackedPC = regs[FAULT_LOG_BKP_PC];
  outInfo->stackedPSR = regs[FAULT_LOG_BKP_PSR];
  outInfo->cfsr = regs[FAULT_LOG_BKP_CFSR];
  outInfo->hfsr = regs[FAULT_LOG_BKP_HFSR];
  outInfo->mmfar = regs[FAULT_LOG_BKP_MMFAR];
  outInfo->bfar = regs[FAULT_LOG_BKP_BFAR];

  faultLogClearRaw();

  return true;
}

static void faultLogCaptureAndReset(faultLogReason_t reason, uint32_t *stackFrame, uint32_t excReturn)
{
  __disable_irq();
  faultLogWriteRaw(reason, stackFrame, excReturn);
  __DSB();
  __ISB();
  NVIC_SystemReset();

  while (1)
  {
  }
}

__attribute__((used)) static void hardFaultHandlerC(uint32_t *stackFrame, uint32_t excReturn)
{
  faultLogCaptureAndReset(FAULT_LOG_REASON_HARDFAULT, stackFrame, excReturn);
}

__attribute__((used)) static void memManageHandlerC(uint32_t *stackFrame, uint32_t excReturn)
{
  faultLogCaptureAndReset(FAULT_LOG_REASON_MEMMANAGE, stackFrame, excReturn);
}

__attribute__((used)) static void busFaultHandlerC(uint32_t *stackFrame, uint32_t excReturn)
{
  faultLogCaptureAndReset(FAULT_LOG_REASON_BUSFAULT, stackFrame, excReturn);
}

__attribute__((used)) static void usageFaultHandlerC(uint32_t *stackFrame, uint32_t excReturn)
{
  faultLogCaptureAndReset(FAULT_LOG_REASON_USAGEFAULT, stackFrame, excReturn);
}

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
extern DMA_HandleTypeDef hdma_adc1;
extern DAC_HandleTypeDef hdac;
extern DMA_HandleTypeDef hdma_i2s3_ext_tx;
extern DMA_HandleTypeDef hdma_spi3_rx;
extern DMA_HandleTypeDef hdma_tim1_ch1;
extern TIM_HandleTypeDef htim6;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim14;

/* USER CODE BEGIN EV */
extern volatile bool mainIsRunning;

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */

  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
__attribute__((naked)) void HardFault_Handler(void)
{
	__asm volatile
	(
		"tst lr, #4\n"
		"ite eq\n"
		"mrseq r0, msp\n"
		"mrsne r0, psp\n"
		"mov r1, lr\n"
		"b hardFaultHandlerC\n"
	);
}

/**
  * @brief This function handles Memory management fault.
  */
__attribute__((naked)) void MemManage_Handler(void)
{
	__asm volatile
	(
		"tst lr, #4\n"
		"ite eq\n"
		"mrseq r0, msp\n"
		"mrsne r0, psp\n"
		"mov r1, lr\n"
		"b memManageHandlerC\n"
	);
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
__attribute__((naked)) void BusFault_Handler(void)
{
	__asm volatile
	(
		"tst lr, #4\n"
		"ite eq\n"
		"mrseq r0, msp\n"
		"mrsne r0, psp\n"
		"mov r1, lr\n"
		"b busFaultHandlerC\n"
	);
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
__attribute__((naked)) void UsageFault_Handler(void)
{
	__asm volatile
	(
		"tst lr, #4\n"
		"ite eq\n"
		"mrseq r0, msp\n"
		"mrsne r0, psp\n"
		"mov r1, lr\n"
		"b usageFaultHandlerC\n"
	);
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles EXTI line0 interrupt.
  */
void EXTI0_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI0_IRQn 0 */

  /* USER CODE END EXTI0_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(TIME_SLOT_INTER_Pin);
  /* USER CODE BEGIN EXTI0_IRQn 1 */

  /* USER CODE END EXTI0_IRQn 1 */
}

/**
  * @brief This function handles EXTI line1 interrupt.
  */
void EXTI1_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI1_IRQn 0 */

  /* USER CODE END EXTI1_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(SYS_INTER_Pin);
  /* USER CODE BEGIN EXTI1_IRQn 1 */

  /* USER CODE END EXTI1_IRQn 1 */
}

/**
  * @brief This function handles EXTI line2 interrupt.
  */
void EXTI2_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI2_IRQn 0 */

  /* USER CODE END EXTI2_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(RF_TX_INTER_Pin);
  /* USER CODE BEGIN EXTI2_IRQn 1 */

  /* USER CODE END EXTI2_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream0 global interrupt.
  */
void DMA1_Stream0_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream0_IRQn 0 */

  /* USER CODE END DMA1_Stream0_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_spi3_rx);
  /* USER CODE BEGIN DMA1_Stream0_IRQn 1 */

  /* USER CODE END DMA1_Stream0_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream5 global interrupt.
  */
void DMA1_Stream5_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream5_IRQn 0 */

  /* USER CODE END DMA1_Stream5_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_i2s3_ext_tx);
  /* USER CODE BEGIN DMA1_Stream5_IRQn 1 */

  /* USER CODE END DMA1_Stream5_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles EXTI line[15:10] interrupts.
  */
void EXTI15_10_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI15_10_IRQn 0 */

  /* USER CODE END EXTI15_10_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(ROTARY_SW_B_Pin);
  /* USER CODE BEGIN EXTI15_10_IRQn 1 */

  /* USER CODE END EXTI15_10_IRQn 1 */
}

/**
  * @brief This function handles TIM8 trigger and commutation interrupts and TIM14 global interrupt.
  */
void TIM8_TRG_COM_TIM14_IRQHandler(void)
{
  /* USER CODE BEGIN TIM8_TRG_COM_TIM14_IRQn 0 */

  /* USER CODE END TIM8_TRG_COM_TIM14_IRQn 0 */
  HAL_TIM_IRQHandler(&htim14);
  /* USER CODE BEGIN TIM8_TRG_COM_TIM14_IRQn 1 */

  /* USER CODE END TIM8_TRG_COM_TIM14_IRQn 1 */
}

/**
  * @brief This function handles TIM6 global interrupt, DAC1 and DAC2 underrun error interrupts.
  */
void TIM6_DAC_IRQHandler(void)
{
  /* USER CODE BEGIN TIM6_DAC_IRQn 0 */
	aprsBitStreamSender();
  /* USER CODE END TIM6_DAC_IRQn 0 */
  HAL_DAC_IRQHandler(&hdac);
  HAL_TIM_IRQHandler(&htim6);
  /* USER CODE BEGIN TIM6_DAC_IRQn 1 */

  /* USER CODE END TIM6_DAC_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream1 global interrupt.
  */
void DMA2_Stream1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream1_IRQn 0 */

  /* USER CODE END DMA2_Stream1_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_tim1_ch1);
  /* USER CODE BEGIN DMA2_Stream1_IRQn 1 */

  /* USER CODE END DMA2_Stream1_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream2 global interrupt.
  */
void DMA2_Stream2_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream2_IRQn 0 */

  /* USER CODE END DMA2_Stream2_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart1_rx);
  /* USER CODE BEGIN DMA2_Stream2_IRQn 1 */

  /* USER CODE END DMA2_Stream2_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream4 global interrupt.
  */
void DMA2_Stream4_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream4_IRQn 0 */

  /* USER CODE END DMA2_Stream4_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc1);
  /* USER CODE BEGIN DMA2_Stream4_IRQn 1 */

  /* USER CODE END DMA2_Stream4_IRQn 1 */
}

/**
  * @brief This function handles USB On The Go FS global interrupt.
  */
void OTG_FS_IRQHandler(void)
{
  /* USER CODE BEGIN OTG_FS_IRQn 0 */

  /* USER CODE END OTG_FS_IRQn 0 */
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
  /* USER CODE BEGIN OTG_FS_IRQn 1 */

  /* USER CODE END OTG_FS_IRQn 1 */
}

/* USER CODE BEGIN 1 */

// HRC6000 and rotary button interrupts related
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if (trxGetMode() == RADIO_MODE_DIGITAL)
	{
		if (GPIO_Pin == GPIO_PIN_0)
		{
			hrc6000SetInIRQHandler(true);
			hrc6000TimeslotInterruptHandler();
			hrc6000SetInIRQHandler(false);
		}
		else if (GPIO_Pin == GPIO_PIN_1)
		{
			hrc6000SetInIRQHandler(true);
			hrc6000SysInterruptHandler();
			hrc6000SetInIRQHandler(false);
		}
		else if (GPIO_Pin == GPIO_PIN_2)
		{
			hrc6000SetInIRQHandler(true);
			hrc6000TxInterruptHandler();
			hrc6000SetInIRQHandler(false);
		}
	}

	if ((GPIO_Pin == GPIO_PIN_14) || (GPIO_Pin == GPIO_PIN_11))
	{
		rotaryEncoderISR(); // Handles rotary encoder pin_A or pin_B IRQ
	}
}

void HAL_IncTick(void)
{
	uwTick += uwTickFreq;

	PIT2SecondsCounter++;
	if (PIT2SecondsCounter == 1000)
	{
		PIT2SecondsCounter = 0;
		uiDataGlobal.dateTimeSecs++;
	}

	if (timer_maintask > 0)
	{
		timer_maintask--;
	}

//	if (timer_beeptask > 0)           G4EML Now using vtaskdelay() instead of PIT Timer
//	{
//		timer_beeptask--;
//	}

//	if (timer_hrc6000task > 0)        G4EML Now using vtaskdelay() instead of PIT Timer
//	{
//		timer_hrc6000task--;
//	}

	if (timer_keypad > 0)
	{
		timer_keypad--;
	}
	if (timer_keypad_timeout > 0)
	{
		timer_keypad_timeout--;
	}
	if (timer_mbuttons[0] > 0)
	{
		timer_mbuttons[0]--;
	}

	if (timer_mbuttons[1] > 0)
	{
		timer_mbuttons[1]--;
	}

	if (timer_mbuttons[2] > 0)
	{
		timer_mbuttons[2]--;
	}
}

/* USER CODE END 1 */
