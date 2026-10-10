/* ============================================================================
 * safety.c  ——  安全中心（实现文件）
 * ----------------------------------------------------------------------------
 * 三层安全体系全部集中在这一个文件里，别的模块只负责"上报状态"。
 *
 *   第一层：通信 / 硬件故障（可恢复）
 *       遥控掉线 / 电调掉线 / 电调报错 / 过温
 *       → 只断电流，系统继续跑，故障消失自动恢复
 *
 *   第二层：程序跑飞（不可恢复 → 看门狗复位）
 *       HardFault 等 → 记崩溃地址 → 亮红灯 → 不喂狗 → 复位
 *
 *   第三层：任务卡死（不可恢复 → 看门狗复位）
 *       任务心跳超时 → 记是哪个任务 → 不喂狗 → 复位
 *
 * ⭐ 喂狗规则（整个设计的关键）：
 *       所有任务心跳都新鲜  → 喂狗（【哪怕有通信故障也喂】）
 *       有任务卡死          → 不喂狗 → 500ms 后看门狗复位
 *
 *   为什么通信故障不影响喂狗？
 *       那是"外部故障"，程序本身是健康的，断电流就能恢复。
 *       如果这时复位，车会莫名其妙重启，反而更危险。
 *   为什么任务卡死要复位？
 *       程序已经不能正常干活了，只能靠复位救回来。
 * ==========================================================================*/

#include "safety.h"
#include "main.h"           /* HAL / GPIO / LED 引脚 */
#include "iwdg.h"           /* hiwdg */
#include "can_motor.h"      /* motor1 / M3508_IsOnline() */
#include "remote.h"         /* Remote_IsOnline() */
#include "pid.h"            /* PID_SPEED_OUT_MAX */
#include "cmsis_os2.h"      /* osKernelGetTickCount() */


/* ============================================================================
 * 一、备份寄存器（复位后内容还在，用来记录"上次为什么挂的"）
 * ----------------------------------------------------------------------------
 * STM32F4 有 20 个 32 位备份寄存器 RTC->BKP0R ~ BKP19R
 *
 * ⚠️ 访问前必须：开 PWR 时钟 + 允许写备份域（在 Safety_Init 里做）
 * ⚠️ 备份域靠 VDD/VBAT 供电：【断电】会丢，但【复位】不会丢 ✓
 *    看门狗复位时 VDD 一直有电 → 正好适合记复位原因
 * ==========================================================================*/

#define BKP_FAULT       (RTC->BKP0R)    /* 故障码 */
#define BKP_FAULT_PC    (RTC->BKP1R)    /* HardFault 时的 PC（崩溃地址）*/
#define BKP_RESET_CNT   (RTC->BKP2R)    /* 复位次数 */
#define BKP_MAGIC       (RTC->BKP3R)    /* 魔术字：判断备份域有没有被清过 */

#define BKP_MAGIC_VALUE 0x5AFE1234U


/* ============================================================================
 * 二、内部状态
 * ==========================================================================*/

static volatile uint32_t s_fault          = FAULT_NONE;  /* 当前故障码 */
static volatile uint32_t s_last_fault     = FAULT_NONE;  /* 上次复位前的故障码 */
static volatile uint32_t s_fault_pc       = 0U;          /* 上次崩溃的地址 */
static volatile uint8_t  s_fault_recorded = 0U;          /* 卡死时只记一次 */

static volatile uint32_t s_heartbeat[SAFETY_TASK_COUNT]; /* 心跳计数（调试用）*/
static volatile uint32_t s_hb_stamp [SAFETY_TASK_COUNT]; /* 最后一次心跳的时刻 */

static uint8_t  s_reset_reason = RESET_REASON_UNKNOWN;
static uint16_t s_reset_count  = 0U;


/* ============================================================================
 * 三、从异常栈帧里取出崩溃地址
 * ----------------------------------------------------------------------------
 * Cortex-M4 进异常时，硬件会自动把 8 个寄存器压栈：
 *     R0, R1, R2, R3, R12, LR, PC, xPSR
 * 其中 PC（第 7 个字，偏移 24 字节）就是【崩在哪条指令】
 * ==========================================================================*/
static uint32_t Fault_GetPC(void)
{
    uint32_t pc;

    __asm volatile (
        "TST    LR, #4          \n"   /* 判断进异常前用的是 MSP 还是 PSP */
        "ITE    EQ              \n"
        "MRSEQ  R0, MSP         \n"
        "MRSNE  R0, PSP         \n"
        "LDR    %0, [R0, #24]   \n"   /* 栈帧偏移 24 = PC */
        : "=r" (pc) : : "r0"
    );

    return pc;
}


/* ============================================================================
 * 四、初始化
 * ==========================================================================*/
void Safety_Init(void)
{
    uint32_t magic;
    uint8_t  i;

    /* ---- ① 允许访问备份域 ---- */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    /* ---- ② 读复位原因（⚠️ 必须在清标志之前读）---- */
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != 0U)
    {
        s_reset_reason = RESET_REASON_IWDG;         /* ⭐ 看门狗复位 */
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != 0U)
    {
        s_reset_reason = RESET_REASON_SOFTWARE;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != 0U)
    {
        s_reset_reason = RESET_REASON_POWER_ON;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != 0U)
    {
        s_reset_reason = RESET_REASON_PIN;
    }
    else
    {
        s_reset_reason = RESET_REASON_UNKNOWN;
    }
    __HAL_RCC_CLEAR_RESET_FLAGS();

    /* ---- ③ 读上次的故障记录（靠魔术字判断有效）---- */
    magic = BKP_MAGIC;
    if (magic == BKP_MAGIC_VALUE)
    {
        /* 备份域没被清过 → 上次的记录有效 */
        s_last_fault  = BKP_FAULT;
        s_fault_pc    = BKP_FAULT_PC;
        s_reset_count = (uint16_t)(BKP_RESET_CNT) + 1U;
    }
    else
    {
        /* 首次上电（或断电过）→ 建立魔术字 */
        BKP_MAGIC     = BKP_MAGIC_VALUE;
        s_last_fault  = FAULT_NONE;
        s_fault_pc    = 0U;
        s_reset_count = 1U;
    }
    BKP_RESET_CNT = s_reset_count;

    /* ---- ④ 清掉记录，准备记这次的 ---- */
    BKP_FAULT    = FAULT_NONE;
    BKP_FAULT_PC = 0U;

    /* ---- ⑤ 复位心跳 ---- */
    for (i = 0U; i < SAFETY_TASK_COUNT; i++)
    {
        s_heartbeat[i] = 0U;
        s_hb_stamp[i]  = osKernelGetTickCount();
    }

    s_fault          = FAULT_NONE;
    s_fault_recorded = 0U;
}


/* ============================================================================
 * 五、任务心跳
 * ==========================================================================*/
void Safety_Heartbeat(SafetyTaskId_t id)
{
    if ((uint8_t)id < SAFETY_TASK_COUNT)
    {
        s_heartbeat[id]++;
        s_hb_stamp[id] = osKernelGetTickCount();
    }
}


/* ============================================================================
 * 六、⭐ 核心：每 1ms 调一次 —— 收集故障 + 判心跳 + 喂狗
 * ==========================================================================*/
float Safety_Update(void)
{
    const uint32_t now = osKernelGetTickCount();
    uint32_t fault     = FAULT_NONE;
    uint8_t  i;
    uint8_t  task_alive = 1U;

    /* ---- ① 第一层：通信 / 硬件故障 ---- */
    if (Remote_IsOnline() == 0)
    {
        fault |= FAULT_RC_OFFLINE;
    }
    if (M3508_IsOnline() == 0)
    {
        fault |= FAULT_ESC_OFFLINE;
    }
    if (motor1.err_code != 0U)
    {
        fault |= FAULT_ESC_ERROR;
    }
    if ((float)motor1.raw_temp >= SAFETY_TEMP_STOP_C)
    {
        fault |= FAULT_OVER_TEMP;
    }

    /* ---- ② 第三层：任务心跳 ---- */
    for (i = 0U; i < SAFETY_TASK_COUNT; i++)
    {
        if ((now - s_hb_stamp[i]) > SAFETY_HB_TIMEOUT_MS)
        {
            task_alive = 0U;                    /* 有任务卡死了 */

            if (i == SAFETY_TASK_CONTROL) { fault |= FAULT_TASK_CONTROL; }
            if (i == SAFETY_TASK_REMOTE)  { fault |= FAULT_TASK_REMOTE;  }
            if (i == SAFETY_TASK_DEBUG)   { fault |= FAULT_TASK_DEBUG;   }
        }
    }

    s_fault = fault;

    /* ---- ③ ⭐ 喂狗 or 让狗咬 ----
     *   所有任务都活着 → 喂狗（哪怕有通信故障也喂）
     *   有任务卡死     → 不喂狗 → 500ms 后看门狗复位 */
    if (task_alive != 0U)
    {
        HAL_IWDG_Refresh(&hiwdg);
        s_fault_recorded = 0U;
    }
    else if (s_fault_recorded == 0U)
    {
        /* ⚠️ 只记一次 —— 备份寄存器写起来慢，不能每 1ms 写 */
        BKP_FAULT = fault;
        s_fault_recorded = 1U;
    }
    else
    {
        /* 已经记过了，什么都不做，等狗咬 */
    }

    /* ---- ④ 有通信故障 → 断电流 ---- */
    if ((fault & FAULT_COMM_MASK) != 0U)
    {
        return 0.0f;
    }

    /* ---- ⑤ 一切正常 → 不限制 ---- */
    return PID_SPEED_OUT_MAX;
}


/* ============================================================================
 * 七、⭐ 给 stm32f4xx_it.c 里的 Fault_Handler 调用
 * ==========================================================================*/
void Safety_FaultHandler(void)
{
    /* ---- ① 记录：故障码 + 崩溃地址 ---- */
    BKP_FAULT    = s_fault | FAULT_HARDFAULT;
    BKP_FAULT_PC = Fault_GetPC();

    /* ---- ② 亮红灯（告诉人"我崩了"）---- */
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);

    /* ---- ③ ⭐ 故意【不喂狗】→ 500ms 后看门狗复位 → 系统自动恢复 ----
     *    这里只死循环，不做别的：因为已经不知道系统状态了，
     *    继续执行只会更糟。 */
    for (;;)
    {
    }
}


/* ============================================================================
 * 八、读取接口（给调试 / J-Scope 用）
 * ==========================================================================*/
uint32_t Safety_GetFault(void)       { return s_fault;      }
uint32_t Safety_GetLastFault(void)   { return s_last_fault; }
uint32_t Safety_GetFaultPC(void)     { return s_fault_pc;   }
uint8_t  Safety_GetResetReason(void) { return s_reset_reason; }
uint16_t Safety_GetResetCount(void)  { return s_reset_count;  }


/* ============================================================================
 * 九、自测
 * ==========================================================================*/

/* 把环境设成"一切正常" */
static void SetEnvNormal(void)
{
    remote.rx_count     = 5U;
    remote.failsafe     = 0U;
    remote.last_rx_tick = osKernelGetTickCount();

    motor1.inited       = 1U;
    motor1.last_rx_tick = osKernelGetTickCount();
    motor1.err_code     = 0U;
    motor1.raw_temp     = 30U;
}

/* 恢复成"刚上电"的样子（自测完必须调）*/
static void RestoreEnv(void)
{
    remote.rx_count     = 0U;
    remote.failsafe     = 0U;
    remote.last_rx_tick = 0U;

    motor1.inited       = 0U;
    motor1.last_rx_tick = 0U;
    motor1.err_code     = 0U;
    motor1.raw_temp     = 0U;
}

int Safety_SelfTest(void)
{
    float lim;
    uint8_t i;

    /* 确保心跳都是新鲜的（不然下面会误判任务卡死）*/
    for (i = 0U; i < SAFETY_TASK_COUNT; i++)
    {
        s_hb_stamp[i] = osKernelGetTickCount();
    }

    /* ① 一切正常 → 返回满量程（= 不限流）*/
    SetEnvNormal();
    lim = Safety_Update();
    if (lim < (PID_SPEED_OUT_MAX - 1.0f))  { return 1; }
    if (s_fault != FAULT_NONE)             { return 2; }

    /* ② 遥控掉线 → 断电流 */
    SetEnvNormal();
    remote.rx_count = 0U;
    lim = Safety_Update();
    if ((s_fault & FAULT_RC_OFFLINE) == 0U) { return 3; }
    if (lim != 0.0f)                        { return 4; }

    /* ③ 电调掉线 → 断电流 */
    SetEnvNormal();
    motor1.inited = 0U;
    lim = Safety_Update();
    if ((s_fault & FAULT_ESC_OFFLINE) == 0U) { return 5; }
    if (lim != 0.0f)                         { return 6; }

    /* ④ 电调报错误码 → 断电流 */
    SetEnvNormal();
    motor1.err_code = 4U;
    lim = Safety_Update();
    if ((s_fault & FAULT_ESC_ERROR) == 0U)  { return 7; }
    if (lim != 0.0f)                        { return 8; }

    /* ⑤ 过温 → 断电流 */
    SetEnvNormal();
    motor1.raw_temp = 120U;
    lim = Safety_Update();
    if ((s_fault & FAULT_OVER_TEMP) == 0U)  { return 9; }
    if (lim != 0.0f)                        { return 10; }

    /* ⑥ 温度刚好在阈值下（114℃）→ 不该触发 */
    SetEnvNormal();
    motor1.raw_temp = 114U;
    lim = Safety_Update();
    if (s_fault != FAULT_NONE)              { return 11; }
    if (lim < (PID_SPEED_OUT_MAX - 1.0f))   { return 12; }

    /* ⑦ ⭐ 多个故障同时报 → 位掩码能叠加（枚举做不到这点）*/
    SetEnvNormal();
    remote.rx_count = 0U;
    motor1.err_code = 4U;
    (void)Safety_Update();
    if ((s_fault & FAULT_RC_OFFLINE) == 0U) { return 13; }
    if ((s_fault & FAULT_ESC_ERROR)  == 0U) { return 14; }

    /* ⑧ 故障消失 → 自动恢复（没有锁存）*/
    SetEnvNormal();
    lim = Safety_Update();
    if (s_fault != FAULT_NONE)              { return 15; }
    if (lim < (PID_SPEED_OUT_MAX - 1.0f))   { return 16; }

    /* ⑨ ⭐ 任务心跳超时 → 报对应的故障位 */
    SetEnvNormal();
    s_hb_stamp[SAFETY_TASK_CONTROL] = osKernelGetTickCount();
    s_hb_stamp[SAFETY_TASK_REMOTE]  = osKernelGetTickCount();
    s_hb_stamp[SAFETY_TASK_DEBUG]   = osKernelGetTickCount()
                                      - (SAFETY_HB_TIMEOUT_MS + 50U);
    (void)Safety_Update();
    if ((s_fault & FAULT_TASK_DEBUG) == 0U) { return 17; }
    /* ⚠️ 通信故障和任务卡死是两回事：卡死不该影响"断电流"的判断 */
    if ((s_fault & FAULT_COMM_MASK) != 0U)  { return 18; }

    /* 复原 */
    for (i = 0U; i < SAFETY_TASK_COUNT; i++)
    {
        s_hb_stamp[i] = osKernelGetTickCount();
    }
    RestoreEnv();
    s_fault = FAULT_NONE;

    return 0;
}
