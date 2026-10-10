/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "can_motor.h"
#include "pid.h"
#include "remote.h"
#include "control.h"
#include "safety.h"
#include "debug.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ④ 速度环的测试目标已废弃 —— 现在目标由 ⑦ 档位/摇杆映射给出（见 control.c）*/
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for remoteTask */
osThreadId_t remoteTaskHandle;
const osThreadAttr_t remoteTask_attributes = {
  .name = "remoteTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for controlTask */
osThreadId_t controlTaskHandle;
const osThreadAttr_t controlTask_attributes = {
  .name = "controlTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for debugTask */
osThreadId_t debugTaskHandle;
const osThreadAttr_t debugTask_attributes = {
  .name = "debugTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void startRemoteTask(void *argument);
void StartControlTask(void *argument);
void StartDebugTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of remoteTask */
  remoteTaskHandle = osThreadNew(startRemoteTask, NULL, &remoteTask_attributes);

  /* creation of controlTask */
  controlTaskHandle = osThreadNew(StartControlTask, NULL, &controlTask_attributes);

  /* creation of debugTask */
  debugTaskHandle = osThreadNew(StartDebugTask, NULL, &debugTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_startRemoteTask */
/**
* @brief Function implementing the remoteTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_startRemoteTask */
void startRemoteTask(void *argument)
{
  /* USER CODE BEGIN startRemoteTask */
  uint32_t tick;

  /* ==========================================================================
   * ⭐ remoteTask —— 考核要求里的"接收遥控器指令"任务
   *    每 10 ms 跑一次：
   *      读 remote.ch[] → 判档位（SWA5）→ 算摇杆目标 → 存到全局
   *    （SBUS 的字节接收在 USART1 中断里完成，这里只做"解读"）
   *
   *    ⭐ 每次都上报心跳 → 这个任务卡死的话，安全中心会发现，
   *       然后停止喂狗让看门狗复位（见 safety.c）
   * ========================================================================== */
  tick = osKernelGetTickCount();
  for(;;)
  {
    tick += 10U;                 /* 10 ms → 100 Hz，够跟遥控器（约 14ms 一帧）*/
    osDelayUntil(tick);

    Safety_Heartbeat(SAFETY_TASK_REMOTE);   /* ⭐ "我还活着" */

    /* ⭐ 遥控任务也参与安全检查 —— 这是【故意】的！
     *   如果只有 controlTask 做检查，那 controlTask 一旦卡死，
     *   连"是谁卡了"都记录不下来。
     *   两个任务都调 Safety_Check()，只要还有一个活着，
     *   故障现场就能被记到备份寄存器里 ✓
     *   （喂狗 Safety_FeedDog() 只在 controlTask 里调，所以不影响复位逻辑）*/
    (void)Safety_Check();

    Control_RemoteUpdate();      /* 读遥控 + 判档位 + 算目标 */
  }
  /* USER CODE END startRemoteTask */
}

/* USER CODE BEGIN Header_StartControlTask */
/**
* @brief Function implementing the controlTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartControlTask */
void StartControlTask(void *argument)
{
  /* USER CODE BEGIN StartControlTask */
  uint32_t tick;

  /* ---- ① 上电自测 + Control_Init()：都在 main.c 的 USER CODE 2 里做
   *      （自测必须在启动 CAN/串口接收之前，否则真实数据会搅乱假数据）*/

  /* ---- ② 1 kHz 控制循环 ----
   * osDelayUntil 保证"每 1 ms 一次"，不会像 osDelay 那样累积漂移
   * ⚠️ 必须和 control.h 里的 CONTROL_DT_S(0.001f) 一致 */
  tick = osKernelGetTickCount();
  for(;;)
  {
    tick += 1U;
    osDelayUntil(tick);

    /* ⭐ 先上报心跳，再干活 —— 顺序不能反！
     *   因为 Control_Update() 里面会调 Safety_Update()，
     *   Safety_Update() 要检查"这个任务的心跳新不新鲜"。
     *   如果反了，它检查到的就是【上一周期】的心跳。 */
    Safety_Heartbeat(SAFETY_TASK_CONTROL);

    /* 所有决策逻辑都在 Control_Update() 里：
     *   读档位 → 安全中心 → 算目标 → 位置环 → 速度环 → 发 CAN
     *
     * ⭐ 安全中心（Safety_Update）在里面，它负责喂狗。
     *    所以：这个任务卡住 → 不喂狗 → 500ms 后看门狗复位 */
    Control_Update();

    /* ⭐ 把这一周期的数据记进 debug 结构体，给 J-Scope 看
     * 放在这里而不是 Control_Update 里面，是因为保护触发时
     * Control_Update 会提前 return，那样就看不到故障时的数据了 */
    Debug_Update();
  }
  /* USER CODE END StartControlTask */
}

/* USER CODE BEGIN Header_StartDebugTask */
/**
* @brief Function implementing the debugTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDebugTask */
void StartDebugTask(void *argument)
{
  /* USER CODE BEGIN StartDebugTask */
  uint32_t cnt = 0U;

  /* ==========================================================================
   * ⭐ debugTask —— 绿灯心跳 + 任务存活上报
   *
   *   ⚠️ 为什么要每 10ms 跑一次，而不是直接用 osDelay(500)？
   *      看门狗超时是 500ms，而这个任务原来也是 500ms 才跑一次
   *      → 安全中心检查"它的心跳超时（200ms）"时会一直判定它卡死
   *      → 结果就是无限复位
   *
   *   ⭐ 解法：每 10ms 跑一次并上报心跳，
   *          每 50 次（= 500ms）才翻转一次绿灯
   *          → 心跳够勤，绿灯还是 500ms 闪一次 ✓
   * ========================================================================== */
  for(;;)
  {
    Safety_Heartbeat(SAFETY_TASK_DEBUG);    /* ⭐ "我还活着" */

    cnt++;
    if (cnt >= 50U)                          /* 50 × 10ms = 500ms */
    {
      cnt = 0U;
      HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
    }

    osDelay(10U);                            /* ⭐ 10ms 一次（原来是 500）*/
  }
  /* USER CODE END StartDebugTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

