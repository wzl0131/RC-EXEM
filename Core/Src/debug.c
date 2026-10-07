/* ============================================================================
 * debug.c  ——  调试数据模块（实现文件）
 * ----------------------------------------------------------------------------
 * 这个模块只做一件事：把各个模块里的量【搬】到全局变量里。
 * 不做任何计算，也不控制任何东西。
 * ==========================================================================*/

#include "debug.h"
#include "can_motor.h"      /* motor1 */
#include "pid.h"            /* pid_speed */
#include "remote.h"         /* remote */
#include "control.h"        /* Control_GetMode / GetTargetDeg / GetTargetRpm */
#include "protection.h"     /* protect */
#include "cmsis_os2.h"      /* osKernelGetTickCount() */


/* ============================================================================
 * 全局变量【定义】
 * ==========================================================================*/

/* ---- 第 1 组：考核要画的 5 条曲线（J-Scope 直接读这五个）---- */
volatile float dbg_target_deg = 0.0f;
volatile float dbg_actual_deg = 0.0f;
volatile float dbg_target_rpm = 0.0f;
volatile float dbg_actual_rpm = 0.0f;
volatile float dbg_pid_out    = 0.0f;

/* ---- 第 2 组：辅助诊断量 ---- */
volatile Debug_t debug;


/* ============================================================================
 * 把当前数据搬进上面的变量
 * ==========================================================================*/
void Debug_Update(void)
{
    /* ---------- 第 1 组：考核要画的 5 条曲线 ---------- */
    dbg_target_deg = Control_GetTargetDeg();        /* 位置环目标 */
    dbg_actual_deg = motor1.out_angle_deg;          /* 位置环反馈 */
    dbg_target_rpm = Control_GetTargetRpm();        /* 速度环目标 */
    dbg_actual_rpm = motor1.out_rpm;                /* 速度环反馈 */
    dbg_pid_out    = pid_speed.output;              /* 速度环输出 = 发给电调的电流 */

    /* ---------- 第 2 组：辅助诊断量 ---------- */
    debug.mode        = (uint8_t)Control_GetMode();
    debug.fault       = (uint8_t)protect.fault;
    debug.over_temp   = protect.over_temp;
    debug.online_can  = motor1.online;
    debug.online_rc   = (uint8_t)Remote_IsOnline();

    debug.raw_angle   = motor1.raw_angle;
    debug.raw_rpm     = motor1.raw_rpm;
    debug.raw_current = motor1.raw_current;
    debug.raw_temp    = motor1.raw_temp;
    debug.err_code    = motor1.err_code;

    /* SWA 通道的原始值：上机时用它确认三个档位到底是多少
     * ⚠️ 这里不能写成 (uint8_t)！SBUS 值最大 1811，一个字节装不下 */
    debug.swa         = remote.ch[SBUS_CH5_SWA];

    debug.tick        = osKernelGetTickCount();
}


/* ============================================================================
 * 自测：造几个"已知值"，看搬运有没有出错
 * ----------------------------------------------------------------------------
 * ⚠️ 会临时改动 motor1，测完立刻复原
 * ==========================================================================*/
int Debug_SelfTest(void)
{
    /* ---- 造已知值 ---- */
    motor1.out_angle_deg = 12.5f;
    motor1.out_rpm       = -34.0f;
    motor1.raw_angle     = 4096U;
    motor1.raw_rpm       = 1000;
    motor1.raw_current   = 5000;
    motor1.raw_temp      = 45U;
    motor1.err_code      = 0U;
    motor1.online        = 1U;

    remote.ch[SBUS_CH5_SWA] = 1811U;    /* 假装 SWA 在上档 */

    Debug_Update();

    /* ---- 对答案（浮点给容差）---- */
    if (dbg_actual_deg < 12.4f || dbg_actual_deg > 12.6f)   { return 1; }
    if (dbg_actual_rpm > -33.9f || dbg_actual_rpm < -34.1f) { return 2; }
    if (debug.raw_angle   != 4096U)                         { return 3; }
    if (debug.raw_rpm     != 1000)                          { return 4; }
    if (debug.raw_current != 5000)                          { return 5; }
    if (debug.raw_temp    != 45U)                           { return 6; }
    if (debug.swa         != 1811U)                         { return 7; }
    if (debug.online_can  != 1U)                            { return 8; }

    /* ---- 复原，别影响真实运行 ---- */
    motor1.out_angle_deg    = 0.0f;
    motor1.out_rpm          = 0.0f;
    motor1.raw_angle        = 0U;
    motor1.raw_rpm          = 0;
    motor1.raw_current      = 0;
    motor1.raw_temp         = 0U;
    motor1.err_code         = 0U;
    motor1.online           = 0U;
    remote.ch[SBUS_CH5_SWA] = SBUS_CH_MID;

    Debug_Update();

    return 0;       /* 全部通过 */
}
