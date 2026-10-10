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
#include "safety.h"         /* ⭐ 安全中心的状态 */


/* ============================================================================
 * 全局变量【定义】—— 就是考核要求的那 5 条曲线
 * ==========================================================================*/
volatile float dbg_target_deg = 0.0f;
volatile float dbg_actual_deg = 0.0f;
volatile float dbg_target_rpm = 0.0f;
volatile float dbg_actual_rpm = 0.0f;
volatile float dbg_pid_out    = 0.0f;

/* ⭐ 安全中心的状态（拆成 8 个独立变量，J-Scope 里一眼看得懂）*/
volatile uint32_t dbg_safety_fault        = 0U;
volatile uint32_t dbg_safety_last_fault   = 0U;
volatile uint32_t dbg_safety_fault_pc     = 0U;
volatile uint32_t dbg_safety_reset_reason = 0U;
volatile uint32_t dbg_safety_reset_count  = 0U;
volatile uint32_t dbg_safety_hb_ctrl      = 0U;
volatile uint32_t dbg_safety_hb_remote    = 0U;
volatile uint32_t dbg_safety_hb_debug     = 0U;


/* ============================================================================
 * 心跳"年龄"超过 255 ms 就按 255 算
 * ----------------------------------------------------------------------------
 * 为什么要限幅？
 *   J-Scope 的 Y 轴是按数据范围自动缩放的。
 *   如果某个任务卡死了，年龄会一直涨到几十万毫秒，
 *   那样正常值（0~10）在图上就看不见了。
 *   限到 255 之后，"正常(0~10)"和"卡死(255)"在一张图上都看得清 ✓
 * ==========================================================================*/
static uint32_t ClampAge(uint32_t age)
{
    return (age > 255U) ? 255U : age;
}


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

    /* ⭐ 安全中心的状态（拆成独立的变量，J-Scope 里一眼看得懂）
     *
     *   正常运行时应该长这样：
     *     dbg_safety_fault         = 0
     *     dbg_safety_last_fault    = 0
     *     dbg_safety_fault_pc      = 0
     *     dbg_safety_reset_reason  = 3      （上电复位）
     *     dbg_safety_reset_count   = 1
     *     dbg_safety_hb_*          = 0~10   （三个任务都很勤快）
     */
    dbg_safety_fault          = Safety_GetFault();
    dbg_safety_last_fault     = Safety_GetLastFault();
    dbg_safety_fault_pc       = Safety_GetFaultPC();
    dbg_safety_reset_reason   = (uint32_t)Safety_GetResetReason();
    dbg_safety_reset_count    = (uint32_t)Safety_GetResetCount();

    /* 心跳"年龄"：距离上次上报过了多少毫秒。
     * 用 ClampAge 限到 255 —— 因为 J-Scope 的 Y 轴分辨率有限，
     * 画出来看得清"是 5 还是 200"就够了，不需要精确到毫秒 */
    dbg_safety_hb_ctrl        = ClampAge(Safety_GetHeartbeatAge(SAFETY_TASK_CONTROL));
    dbg_safety_hb_remote      = ClampAge(Safety_GetHeartbeatAge(SAFETY_TASK_REMOTE));
    dbg_safety_hb_debug       = ClampAge(Safety_GetHeartbeatAge(SAFETY_TASK_DEBUG));
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
