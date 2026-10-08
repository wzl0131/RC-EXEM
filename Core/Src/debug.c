/* ============================================================================
 * debug.c  ——  调试数据模块（实现文件）
 * ----------------------------------------------------------------------------
 * 只做一件事：把各种量搬进 5 个全局变量，给 J-Scope 看。
 * 不参与任何控制计算，整个删掉也不影响控制逻辑。
 * ==========================================================================*/

#include "debug.h"
#include "can_motor.h"      /* motor1 */
#include "pid.h"            /* pid_speed.output */
#include "control.h"        /* Control_GetTargetDeg / GetTargetRpm */


/* ============================================================================
 * 全局变量【定义】—— 就是考核要求的那 5 条曲线
 * ==========================================================================*/
volatile float dbg_target_deg = 0.0f;
volatile float dbg_actual_deg = 0.0f;
volatile float dbg_target_rpm = 0.0f;
volatile float dbg_actual_rpm = 0.0f;
volatile float dbg_pid_out    = 0.0f;


/* ============================================================================
 * 搬运
 * ==========================================================================*/
void Debug_Update(void)
{
    dbg_target_deg = Control_GetTargetDeg();    /* 位置环目标 */
    dbg_actual_deg = motor1.out_angle_deg;      /* 位置环反馈 */
    dbg_target_rpm = Control_GetTargetRpm();    /* 速度环目标（位置模式下 = 位置环输出）*/
    dbg_actual_rpm = motor1.out_rpm;            /* 速度环反馈 */
    dbg_pid_out    = pid_speed.output;          /* 速度环输出 = 发给电调的电流 */
}


/* ============================================================================
 * 自测：造已知值，验证搬运有没有出错
 * ⚠️ 会临时改动 motor1，测完立刻复原
 * ==========================================================================*/
int Debug_SelfTest(void)
{
    /* ---- 造已知值 ---- */
    motor1.out_angle_deg = 12.5f;
    motor1.out_rpm       = -34.0f;
    pid_speed.output     = 777.0f;

    Debug_Update();

    /* ---- 对答案（浮点给容差）---- */
    if (dbg_actual_deg < 12.4f  || dbg_actual_deg > 12.6f)   { return 1; }
    if (dbg_actual_rpm > -33.9f || dbg_actual_rpm < -34.1f)  { return 2; }
    if (dbg_pid_out    < 776.0f || dbg_pid_out    > 778.0f)  { return 3; }

    /* ---- 复原，别影响真实运行 ---- */
    motor1.out_angle_deg = 0.0f;
    motor1.out_rpm       = 0.0f;
    pid_speed.output     = 0.0f;

    Debug_Update();

    return 0;
}
