/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "safety.h"          /* ⭐ 所有 Fault 都交给安全中心处理 */
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

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern CAN_HandleTypeDef hcan1;
extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim1;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/* ============================================================================
 * ⭐ 二层安全：程序跑飞的处理
 * ----------------------------------------------------------------------------
 * 这一组 Fault 处理函数的做法完全一样：
 *
 *   ① 记录故障码 + 崩溃地址到【备份寄存器】（复位后还能读出来）
 *   ② 点亮红灯
 *   ③ 故意【不喂狗】→ 500ms 后看门狗复位 → 系统自动恢复
 *
 * ⭐ 为什么不用 NVIC_SystemReset() 立刻复位？
 *      立刻复位快是快，但你【什么线索都没有】。
 *      交给看门狗咬的话，复位前有 500ms 的时间把现场记下来，
 *      复位后读 Safety_GetLastFault() / Safety_GetFaultPC()
 *      就知道是崩在哪条指令上了（拿 PC 去 TEST.map 里查）。
 *
 * ⚠️ 为什么要把现场记到【备份寄存器】而不是普通 RAM？
 *      普通 RAM 在复位后会被启动代码清零（.bss 段），存不住。
 *      备份寄存器靠 VDD/VBAT 供电，复位不会丢 ✓
 *
 * 具体实现都在 safety.c 的 Safety_FaultHandler() 里，
 * 这样"安全逻辑"就全部集中在安全中心一处了。
 * ==========================================================================*/

/**
  * @brief This function handles Non maskable interrupt.
  * @note  NMI 通常是外部硬件异常（比如时钟失效）。
  *        看门狗用的是独立的 LSI 时钟，所以即使主时钟挂了也能复位 ✓
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */
  Safety_FaultHandler();       /* 记录 + 亮灯 + 不喂狗 → 等看门狗复位 */
  /* USER CODE END NonMaskableInt_IRQn 0 */
  while (1)
  {
  }
}

/**
  * @brief This function handles Hard fault interrupt.
  * @note  HardFault 是"兜底"的：MemManage/BusFault/UsageFault
  *        默认没使能时，它们的异常都会升级成 HardFault 跑进来。
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  Safety_FaultHandler();       /* ⭐ 记录崩溃 PC → 亮红灯 → 等狗咬 */
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */
  Safety_FaultHandler();
  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */
  Safety_FaultHandler();
  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */
  Safety_FaultHandler();
  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
  }
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
  * @brief This function handles CAN1 RX0 interrupts.
  */
void CAN1_RX0_IRQHandler(void)
{
  /* USER CODE BEGIN CAN1_RX0_IRQn 0 */

  /* USER CODE END CAN1_RX0_IRQn 0 */
  HAL_CAN_IRQHandler(&hcan1);
  /* USER CODE BEGIN CAN1_RX0_IRQn 1 */

  /* USER CODE END CAN1_RX0_IRQn 1 */
}

/**
  * @brief This function handles TIM1 update interrupt and TIM10 global interrupt.
  */
void TIM1_UP_TIM10_IRQHandler(void)
{
  /* USER CODE BEGIN TIM1_UP_TIM10_IRQn 0 */

  /* USER CODE END TIM1_UP_TIM10_IRQn 0 */
  HAL_TIM_IRQHandler(&htim1);
  /* USER CODE BEGIN TIM1_UP_TIM10_IRQn 1 */

  /* USER CODE END TIM1_UP_TIM10_IRQn 1 */
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

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
