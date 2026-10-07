/* ============================================================================
 * control.c  ——  控制逻辑模块（实现文件）
 * ----------------------------------------------------------------------------
 * 每 1 ms 被 controlTask 调用一次（Control_Update），流程：
 *
 *   ┌─ 1. 安全闸门 ── 遥控掉线 / 电调掉线 / 电调报错  → 断电流 + 复位 PID，退出
 *   │
 *   ├─ 2. 判档位 ──── 读 CH5（SWA）→ 位置 / 速度 / 停机
 *   │
 *   ├─ 3. 算目标 ──── 摇杆 → 目标角度 或 目标转速（带死区 + 斜坡限速）
 *   │
 *   ├─ 4. 外环 ────── 位置环：(目标角度, 实际角度) → 目标转速   【只有位置模式】
 *   │
 *   └─ 5. 内环 ────── 速度环：(目标转速, 实际转速) → 电流 → 发 CAN
 *
 * ⭐ 两个"防冲击"的设计（本模块的重点）：
 *   ① 斜坡限速 SlewLimit()：目标值不会瞬间跳变
 *   ② 切档 / 断连时 Pid_Reset()：清掉积分残留
 * ==========================================================================*/

#include "control.h"
#include "can_motor.h"
#include "pid.h"
#include "protection.h"
#include "cmsis_os2.h"


/* ============================================================================
 * 内部状态
 *   这些必须放在函数外面（static 文件级），才能"跨次调用记住"
 * ==========================================================================*/
static CtrlMode_t s_mode       = CTRL_MODE_STOP;   /* 当前档位 */
static float      s_target_deg = 0.0f;             /* 位置环的目标角度（已斜坡限速）*/
static float      s_target_rpm = 0.0f;             /* 速度环的目标转速（已斜坡限速）*/
static float      s_last_current = 0.0f;           /* 上一周期实际发出去的电流 */


/* ============================================================================
 * 一、初始化
 * ==========================================================================*/
void Control_Init(void)
{
    Protection_Init();      /* 清空保护状态、解除故障锁存 */

    /* 两路 PID：参数在 pid.h 里，上机时整定 */
    Pid_Init(&pid_speed, PID_SPEED_KP, PID_SPEED_KI, PID_SPEED_KD,
             PID_SPEED_OUT_MAX, PID_SPEED_I_MAX);
    Pid_Init(&pid_angle, PID_ANGLE_KP, PID_ANGLE_KI, PID_ANGLE_KD,
             PID_ANGLE_OUT_MAX, PID_ANGLE_I_MAX);

    s_mode       = CTRL_MODE_STOP;
    s_target_deg = 0.0f;
    s_target_rpm = 0.0f;
    s_last_current = 0.0f;
}


/* ============================================================================
 * 二、内部小工具
 * ==========================================================================*/

/* ---- 摇杆死区 ----
 * 摇杆回中时不可能正好 992，总有几十个数的偏差。
 * 死区就是"这附近的一律当成 0"，否则电机会一直慢慢爬 */
static float ApplyDeadzone(float v)
{
    if (v > -CTRL_STICK_DEADZONE && v < CTRL_STICK_DEADZONE)
    {
        return 0.0f;
    }
    return v;
}

/* ---- ⭐ 斜坡限速：让 now 每次最多朝 target 走 "rate × dt"，不会一步跳到位 ----
 *   例：now=0, target=100, rate=180, dt=0.001
 *       max_step = 0.18
 *       d = 100（想一步走 100）→ 被夹到 0.18 → 返回 0.18
 *   效果：目标值以 180/秒 的速度"爬"过去，而不是瞬间跳过去 */
static float SlewLimit(float now, float target, float rate, float dt)
{
    float max_step = rate * dt;
    float d        = target - now;

    if (d >  max_step) { d =  max_step; }
    if (d < -max_step) { d = -max_step; }

    return now + d;
}

/* ---- 读档位 ----
 * SBUS 三位开关的三个值大约是：172（下）/ 992（中）/ 1811（上）
 *   上   → 位置模式（考核要求：位置控制）
 *   下   → 速度模式
 *   中间 → 停机（最安全）*/
static CtrlMode_t ReadMode(void)
{
    uint16_t swa = remote.ch[SBUS_CH5_SWA];

    if (swa > CTRL_SWA_POS_MIN)   { return CTRL_MODE_POS;   }
    if (swa < CTRL_SWA_SPEED_MAX) { return CTRL_MODE_SPEED; }
    return CTRL_MODE_STOP;
}


/* ============================================================================
 * 三、⭐ 控制循环主体（每 1 ms 调用一次）
 * ==========================================================================*/
void Control_Update(void)
{
    const float dt = CONTROL_DT_S;
    CtrlMode_t  mode;
    float       limit;
    float       stick;
    float       raw_target;
    float       current;

    /* ==================== 1. 读档位 ==================== */
    mode = ReadMode();

    if (mode != s_mode)
    {
        /* 换档了：清掉两路 PID 的积分，避免上一个档位攒的积分造成冲击 */
        Pid_Reset(&pid_speed);
        Pid_Reset(&pid_angle);
        s_mode = mode;
    }

    /* ==================== 2. 保护：本次最多允许给多大电流 ====================
     * 全部判断（遥控掉线 / 电调掉线 / 电调报错 / 过温 / 角度越限 / 堵转）
     * 都在 protection.c 里，这里只根据返回的限流值做两件事：
     *   limit == 0  →  立刻断电流、复位 PID、退出
     *   limit >  0  →  记下来，最后夹在 PID 输出上 */
    limit = Protection_Update(mode);

    if (limit <= 0.0f)
    {
        /* ⚠️ 必须【立刻】断电流，不能缓慢降 —— 保护要的就是快 */
        C620_SendCurrent(1, 0);

        Pid_Reset(&pid_speed);           /* 清积分，避免恢复时暴冲 */
        Pid_Reset(&pid_angle);

        /* 让"斜坡目标"跟上实际值 → 恢复时不会突然冲 */
        s_target_deg   = motor1.out_angle_deg;
        s_target_rpm   = 0.0f;
        s_last_current = 0.0f;
        return;
    }

    /* ==================== 3. 按档位算目标 ==================== */
    if (mode == CTRL_MODE_POS)
    {
        /* 左摇杆 → 目标角度（±90°），带死区 + 斜坡限速 */
        stick      = ApplyDeadzone(Remote_ChNorm(remote.ch[CTRL_CH_POS_STICK]));
        raw_target = stick * CTRL_ANGLE_LIMIT_DEG;
        s_target_deg = SlewLimit(s_target_deg, raw_target,
                                 CTRL_POS_SLEW_DEG_PER_S, dt);
        /* 注意：这一档 s_target_rpm 会在下面第 4 步被外环算出来 */
    }
    else if (mode == CTRL_MODE_SPEED)
    {
        /* 右摇杆 → 目标转速（±½额定转速），带死区 + 斜坡限速 */
        stick        = ApplyDeadzone(Remote_ChNorm(remote.ch[CTRL_CH_SPEED_STICK]));
        raw_target   = stick * CTRL_SPEED_LIMIT_RPM;
        s_target_rpm = SlewLimit(s_target_rpm, raw_target,
                                 CTRL_SPD_SLEW_RPM_PER_S, dt);

        /* 位置环不用，但让它的目标跟着实际角度走，
         * 这样下次切回位置模式时不会"从旧目标猛地冲过去" */
        s_target_deg = motor1.out_angle_deg;
        Pid_Reset(&pid_angle);
    }
    else /* CTRL_MODE_STOP */
    {
        s_target_rpm = SlewLimit(s_target_rpm, 0.0f,
                                 CTRL_SPD_SLEW_RPM_PER_S, dt);
        s_target_deg = motor1.out_angle_deg;
    }

    /* ==================== 4. 外环：位置环（只有位置模式）====================
     * 输入：目标角度、实际角度（都来自 motor1.out_angle_deg，单位 度）
     * 输出：目标转速（单位 输出轴 rpm）→ 直接喂给内环
     * 输出限幅由 pid_angle.out_max 保证（= PID_ANGLE_OUT_MAX，允许的最大 rpm）*/
    if (mode == CTRL_MODE_POS)
    {
        s_target_rpm = Pid_Calc(&pid_angle, s_target_deg,
                                motor1.out_angle_deg, dt);
    }

    /* ==================== 5. 内环：速度环 → 电流 ==================== */
    current = Pid_Calc(&pid_speed, s_target_rpm, motor1.out_rpm, dt);

    /* ---- 夹到保护给的限流上限 ----
     * 平时 limit = 16384（不限），过温降额时会变小（例如 10922）*/
    if (current >  limit) { current =  limit; }
    if (current < -limit) { current = -limit; }

    /* ---- 电流变化率限制：不让电流一步跳到底 ----
     * 保护电调和机械（尤其是换档、PID 参数不当时）*/
    current = SlewLimit(s_last_current, current, CTRL_CURRENT_SLEW_PER_S, dt);
    s_last_current = current;

    /* Pid_Calc 已经限幅在 ±16384，再夹一次后正好装进 int16_t */
    C620_SendCurrent(1, (int16_t)current);
}


/* ============================================================================
 * 四、给调试/画曲线用的读取接口
 * ==========================================================================*/
CtrlMode_t Control_GetMode(void)      { return s_mode;       }
float      Control_GetTargetDeg(void) { return s_target_deg; }
float      Control_GetTargetRpm(void) { return s_target_rpm; }


/* ============================================================================
 * 五、自测函数（不用遥控器、不用电机，纯逻辑验证）
 * ==========================================================================*/
int Control_SelfTest(void)
{
    float v;

    /* ============ ① 斜坡限速：每秒 180°，dt=1ms → 每次最多走 0.18° ============ */
    v = SlewLimit(0.0f, 100.0f, 180.0f, 0.001f);
    if (v < 0.179f || v > 0.181f) { return 1; }

    /* ============ ② 反向也一样 ============ */
    v = SlewLimit(0.0f, -100.0f, 180.0f, 0.001f);
    if (v > -0.179f || v < -0.181f) { return 2; }

    /* ============ ③ 差得比一步还少 → 直接到位，且【不能越过】目标 ============ */
    v = SlewLimit(9.99f, 10.0f, 180.0f, 0.001f);
    if (v < 9.999f || v > 10.001f) { return 3; }

    /* ============ ④ 死区 ============ */
    if (ApplyDeadzone(0.01f)  != 0.0f)  { return 4; }   /* 死区内 → 0 */
    if (ApplyDeadzone(0.5f)   != 0.5f)  { return 5; }   /* 死区外 → 原值 */
    if (ApplyDeadzone(-0.5f)  != -0.5f) { return 6; }

    /* ============ ⑤ 判档位：SWA = 1811 / 992 / 172 ============ */
    remote.ch[SBUS_CH5_SWA] = 1811U;
    if (ReadMode() != CTRL_MODE_POS)   { return 7; }
    remote.ch[SBUS_CH5_SWA] = 992U;
    if (ReadMode() != CTRL_MODE_STOP)  { return 8; }
    remote.ch[SBUS_CH5_SWA] = 172U;
    if (ReadMode() != CTRL_MODE_SPEED) { return 9; }

    /* ============ ⑥ 摇杆推到底 = 满量程 ±90° ============ */
    v = Remote_ChNorm(1811U) * CTRL_ANGLE_LIMIT_DEG;
    if (v < 89.9f || v > 90.1f)   { return 10; }
    v = Remote_ChNorm(172U) * CTRL_ANGLE_LIMIT_DEG;
    if (v > -89.9f || v < -90.1f) { return 11; }

    /* ============ ⑦ 摇杆推到底 = 满量程 ±½额定转速 ============ */
    v = Remote_ChNorm(1811U) * CTRL_SPEED_LIMIT_RPM;
    if (v < 234.0f || v > 236.0f) { return 12; }

    /* 复原，别影响真实运行 */
    remote.ch[SBUS_CH5_SWA] = SBUS_CH_MID;

    return 0;       /* 全部通过 */
}
