/* ============================================================================
 * protection.c  ——  保护逻辑模块（实现文件）
 * ----------------------------------------------------------------------------
 * 每个控制周期被调用一次，返回"本次最多允许给多大电流"。
 *
 * 判断优先级（从最轻到最重，用 else if 链，一次只报最靠前的那个）：
 *    ① 遥控掉线      （轻：信号回来自动恢复）
 *    ② 电调掉线      （轻）
 *    ③ 电调报错误码  （重：锁存）
 *    ④ 电机过温      （重：锁存）
 *    ⑤ 角度越软限位  （重：锁存，只在位置模式查）
 *    ⑥ 堵转          （重：锁存，只在位置模式查）
 *
 * 另外还有两项"不是故障、但要限流"的处理：
 *    · 过温降额：100℃ 开始线性降额，到 115℃ 直接停机
 *    · （电流变化率限制放在 control.c 里，因为它是对"输出"的处理）
 * ==========================================================================*/

#include "protection.h"
#include "can_motor.h"
#include "pid.h"
#include "remote.h"
#include "cmsis_os2.h"
#include <string.h>       /* memset() */


/* ============================================================================
 * 全局变量【定义】
 * ==========================================================================*/
Protect_t protect;

/* 重故障锁存标志：1 = 还锁着，不许自动恢复 */
static uint8_t s_latched = 0U;


/* ============================================================================
 * 一、初始化
 * ==========================================================================*/
void Protection_Init(void)
{
    memset(&protect, 0, sizeof(Protect_t));
    s_latched = 0U;
}


/* ---- 取绝对值（不想为这一个函数引入 math.h）---- */
static float AbsF(float v)
{
    return (v < 0.0f) ? -v : v;
}


/* ============================================================================
 * 二、⭐ 核心：算本次的电流上限
 * ==========================================================================*/
float Protection_Update(CtrlMode_t mode)
{
    float       limit   = PID_SPEED_OUT_MAX;   /* 默认不限流 */
    float       temp    = (float)motor1.raw_temp;
    FaultCode_t f       = FAULT_NONE;
    uint8_t     serious = 0U;                  /* 1 = 这类故障要锁存 */

    protect.temp = temp;

    /* ==================== 1. 堵转条件计数（每周期都更新）====================
     * 判断思路：拼命给电流，转速却几乎为 0 → 机构卡住了
     * 用 pid_speed.output（上一周期发出去的电流）当"拼命程度"的指标 */
    {
        float cmd = AbsF(pid_speed.output);
        float rpm = AbsF(motor1.out_rpm);

        if ((mode == CTRL_MODE_POS) &&
            (cmd > PROTECT_STALL_CURRENT) &&
            (rpm < PROTECT_STALL_RPM))
        {
            protect.stall_ticks++;             /* 1 kHz → 1 个 tick = 1 ms */
        }
        else
        {
            protect.stall_ticks = 0U;          /* 条件一断就重新计数 */
        }
    }

    /* ==================== 2. 依次判断（else if：只报最靠前的）==================== */
    if (Remote_IsOnline() == 0)
    {
        f = FAULT_RC_OFFLINE;   serious = 0U;
    }
    else if (motor1.online == 0)
    {
        f = FAULT_ESC_OFFLINE;  serious = 0U;
    }
    else if (motor1.err_code != 0U)
    {
        f = FAULT_ESC_ERROR;    serious = 1U;
    }
    else if (temp >= PROTECT_TEMP_STOP_C)
    {
        f = FAULT_OVER_TEMP;    serious = 1U;
    }
    else if ((mode == CTRL_MODE_POS) &&
             (AbsF(motor1.out_angle_deg) >
              (CTRL_ANGLE_LIMIT_DEG + PROTECT_ANGLE_MARGIN_DEG)))
    {
        f = FAULT_ANGLE_LIMIT;  serious = 1U;
    }
    else if (protect.stall_ticks >= PROTECT_STALL_MS)
    {
        f = FAULT_STALL;        serious = 1U;
    }
    else
    {
        f = FAULT_NONE;
    }

    /* ==================== 3. 过温【降额】（不是故障，只是限流）====================
     * 线性插值：100℃ 时还是满电流，115℃ 时降到 0
     *   k = (115 - temp) / (115 - 100)
     *   temp=105 → k = 10/15 = 0.667 → 上限变成满量的 66.7% */
    protect.over_temp = 0U;
    if ((f == FAULT_NONE) && (temp >= PROTECT_TEMP_DERATE_C))
    {
        float k = (PROTECT_TEMP_STOP_C - temp) /
                  (PROTECT_TEMP_STOP_C - PROTECT_TEMP_DERATE_C);

        if (k < 0.0f) { k = 0.0f; }
        if (k > 1.0f) { k = 1.0f; }

        limit = PID_SPEED_OUT_MAX * k;
        protect.over_temp = 1U;
    }

    /* ==================== 4. 故障处理 + 锁存 ==================== */
    if (f != FAULT_NONE)
    {
        protect.fault      = f;
        protect.last_fault = f;          /* 这个不会被清，留给点灯诊断 */
        protect.fault_ticks++;
        if (serious != 0U) { s_latched = 1U; }

        limit = 0.0f;                    /* ⚠️ 立刻归零，绝不缓降 */
    }
    else if (s_latched != 0U)
    {
        /* 重故障还锁着：条件虽然消失了，也不许自动恢复
         * 只有把 SWA 拨回中间（停机档）才清除 —— 逼你确认一次现场 */
        limit = 0.0f;
        if (mode == CTRL_MODE_STOP)
        {
            s_latched     = 0U;
            protect.fault = FAULT_NONE;
        }
    }
    else
    {
        protect.fault = FAULT_NONE;
    }

    protect.limit = limit;
    return limit;
}


/* ============================================================================
 * 三、自测函数
 * ----------------------------------------------------------------------------
 * 保护逻辑的输入是几个全局变量（remote / motor1 / pid_speed），
 * 所以自测可以直接【改写这些全局变量】来构造各种场景，不需要任何硬件。
 * ==========================================================================*/

/* 把环境设成"一切正常" */
static void SetEnvNormal(void)
{
    Protection_Init();

    remote.rx_count      = 5U;                             /* 收到过帧 */
    remote.failsafe      = 0U;                             /* 没失控 */
    remote.last_rx_tick  = osKernelGetTickCount();         /* 刚刚收到 */

    motor1.online        = 1U;                             /* 电调在线 */
    motor1.err_code      = 0U;                             /* 无错误 */
    motor1.raw_temp      = 30U;                            /* 凉快 */
    motor1.raw_rpm       = 0;
    motor1.out_rpm       = 0.0f;
    motor1.out_angle_deg = 0.0f;

    pid_speed.output     = 0.0f;                           /* 上一周期没输出 */
}

/* 把环境恢复成"刚上电"的样子（自测完必须调）*/
static void RestoreEnv(void)
{
    Protection_Init();

    remote.rx_count      = 0U;
    remote.failsafe      = 0U;
    remote.last_rx_tick  = 0U;

    motor1.online        = 0U;
    motor1.err_code      = 0U;
    motor1.raw_temp      = 0U;
    motor1.raw_rpm       = 0;
    motor1.out_rpm       = 0.0f;
    motor1.out_angle_deg = 0.0f;

    pid_speed.output     = 0.0f;
}

int Protection_SelfTest(void)
{
    float    lim;
    uint16_t i;

    /* ============ ① 一切正常 → 不限流、无故障 ============ */
    SetEnvNormal();
    lim = Protection_Update(CTRL_MODE_SPEED);
    if (lim < (PID_SPEED_OUT_MAX - 1.0f)) { return 1; }
    if (protect.fault != FAULT_NONE)      { return 2; }

    /* ============ ② 遥控掉线 → 断电流 ============ */
    SetEnvNormal();
    remote.rx_count = 0U;
    lim = Protection_Update(CTRL_MODE_SPEED);
    if (protect.fault != FAULT_RC_OFFLINE) { return 3; }
    if (lim != 0.0f)                       { return 4; }

    /* ============ ③ 轻故障【不】锁存：信号回来就自动恢复 ============ */
    SetEnvNormal();
    remote.rx_count = 0U;
    (void)Protection_Update(CTRL_MODE_SPEED);          /* 触发一次 */
    remote.rx_count     = 5U;
    remote.last_rx_tick = osKernelGetTickCount();      /* 信号回来了 */
    lim = Protection_Update(CTRL_MODE_SPEED);
    if (lim < (PID_SPEED_OUT_MAX - 1.0f))  { return 5; }   /* 应该满血复活 */

    /* ============ ④ 电调掉线 ============ */
    SetEnvNormal();
    motor1.online = 0U;
    lim = Protection_Update(CTRL_MODE_SPEED);
    if (protect.fault != FAULT_ESC_OFFLINE) { return 6; }
    if (lim != 0.0f)                        { return 7; }

    /* ============ ⑤ 电调报错误码 ============ */
    SetEnvNormal();
    motor1.err_code = 4U;                              /* 4 = 位置传感器丢失 */
    lim = Protection_Update(CTRL_MODE_SPEED);
    if (protect.fault != FAULT_ESC_ERROR) { return 8; }
    if (lim != 0.0f)                      { return 9; }

    /* ============ ⑥ 重故障【锁存】：条件消失了也不恢复 ============ */
    SetEnvNormal();
    motor1.err_code = 4U;
    (void)Protection_Update(CTRL_MODE_SPEED);          /* 触发并锁存 */
    motor1.err_code = 0U;                              /* 错误码没了 */
    lim = Protection_Update(CTRL_MODE_SPEED);
    if (lim != 0.0f) { return 10; }                    /* 仍然锁着 ✓ */

    /* ============ ⑦ 拨回中间（STOP）才解锁 ============ */
    (void)Protection_Update(CTRL_MODE_STOP);           /* 这一周期还在锁 */
    lim = Protection_Update(CTRL_MODE_SPEED);          /* 下一周期恢复 */
    if (lim < (PID_SPEED_OUT_MAX - 1.0f)) { return 11; }

    /* ============ ⑧ 过温停机 ============ */
    SetEnvNormal();
    motor1.raw_temp = 120U;
    lim = Protection_Update(CTRL_MODE_SPEED);
    if (protect.fault != FAULT_OVER_TEMP) { return 12; }
    if (lim != 0.0f)                      { return 13; }

    /* ============ ⑨ 过温降额：105℃ → 上限应为满量的 2/3 ≈ 10922 ============ */
    SetEnvNormal();
    motor1.raw_temp = 105U;
    lim = Protection_Update(CTRL_MODE_SPEED);
    if (protect.fault     != FAULT_NONE) { return 14; }   /* 还不是故障 */
    if (protect.over_temp != 1U)         { return 15; }   /* 但已在降额 */
    if (lim < 10800.0f || lim > 11050.0f) { return 16; }

    /* ============ ⑩ 角度越限（位置模式）============ */
    SetEnvNormal();
    motor1.out_angle_deg = 120.0f;                     /* 超 90+8=98 */
    lim = Protection_Update(CTRL_MODE_POS);
    if (protect.fault != FAULT_ANGLE_LIMIT) { return 17; }

    /* ============ ⑪ 同样的角度，速度模式下【不该】报越限 ============ */
    SetEnvNormal();
    motor1.out_angle_deg = 120.0f;
    lim = Protection_Update(CTRL_MODE_SPEED);
    if (protect.fault != FAULT_NONE) { return 18; }

    /* ============ ⑫ 堵转：位置模式 + 电流大 + 转速低，持续 500 周期 ============ */
    SetEnvNormal();
    pid_speed.output = 12000.0f;                       /* 拼命给电流 */
    motor1.out_rpm   = 3.0f;                           /* 却转不动 */
    for (i = 0U; i < PROTECT_STALL_MS; i++)
    {
        lim = Protection_Update(CTRL_MODE_POS);
    }
    if (protect.fault != FAULT_STALL) { return 19; }

    /* ============ ⑬ 转速恢复正常 → 不该再判堵转 ============ */
    SetEnvNormal();
    pid_speed.output = 12000.0f;
    motor1.out_rpm   = 200.0f;                         /* 转起来了 */
    for (i = 0U; i < 600U; i++)
    {
        lim = Protection_Update(CTRL_MODE_POS);
    }
    if (protect.fault != FAULT_NONE) { return 20; }

    /* ---- 复原环境，别影响真实运行 ---- */
    RestoreEnv();

    (void)lim;
    return 0;       /* 全部通过 */
}
