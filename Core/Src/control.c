/* ============================================================================
 * control.c  ——  控制逻辑模块（实现文件）
 * ----------------------------------------------------------------------------
 * 两个函数，对应考核要求的两个任务：
 *
 *   Control_RemoteUpdate()   ← remoteTask 调用，每 10 ms
 *       读遥控 → 判档位 → 算摇杆目标（带死区 + 斜坡限速）
 *
 *   Control_Update()         ← controlTask 调用，每 1 ms
 *       保护检查 → 位置环 → 速度环 → 限流 → 发 CAN
 *
 * ⭐ 两个任务之间靠全局变量传数据（s_mode / s_target_deg / s_stick_rpm）：
 *    都是 32 位，单条指令读写是原子的，所以不需要互斥锁
 * ==========================================================================*/

#include "control.h"
#include "can_motor.h"
#include "pid.h"
#include "protection.h"


/* ============================================================================
 * 内部状态
 *   带 volatile 的原因：被两个任务共享，防止编译器把它缓存在寄存器里
 * ==========================================================================*/
static volatile CtrlMode_t s_mode       = CTRL_MODE_STOP;   /* 当前档位 */
static volatile float      s_target_deg = 0.0f;             /* 位置环目标（已斜坡）*/
static volatile float      s_stick_rpm  = 0.0f;             /* 速度模式摇杆目标（已斜坡）*/
static          float      s_rpm_cmd    = 0.0f;             /* 速度环实际目标（画曲线用）*/
static          float      s_last_current = 0.0f;           /* 上一周期发出去的电流 */


/* ============================================================================
 * 一、初始化（在 main.c 的自测之后调用一次）
 * ==========================================================================*/
void Control_Init(void)
{
    Protection_Init();      /* 清空保护状态 */

    Pid_Init(&pid_speed, PID_SPEED_KP, PID_SPEED_KI, PID_SPEED_KD,
             PID_SPEED_OUT_MAX, PID_SPEED_I_MAX);
    Pid_Init(&pid_angle, PID_ANGLE_KP, PID_ANGLE_KI, PID_ANGLE_KD,
             PID_ANGLE_OUT_MAX, PID_ANGLE_I_MAX);

    s_mode         = CTRL_MODE_STOP;
    s_target_deg   = 0.0f;
    s_stick_rpm    = 0.0f;
    s_rpm_cmd      = 0.0f;
    s_last_current = 0.0f;
}


/* ============================================================================
 * 二、内部小工具
 * ==========================================================================*/

/* ---- 摇杆死区 ----
 * 摇杆回中时不可能正好 992，总有几十个数的偏差
 * → 死区内一律当成 0，否则电机会一直慢慢爬 */
static float ApplyDeadzone(float v)
{
    if (v > -CTRL_STICK_DEADZONE && v < CTRL_STICK_DEADZONE)
    {
        return 0.0f;
    }
    return v;
}

/* ---- 斜坡限速：让 now 每次最多朝 target 走 "rate × dt" ----
 *   例：now=0, target=100, rate=360, dt=0.01
 *       max_step = 3.6  →  本次只走 3.6
 *   效果：目标值是"爬"过去的，不会瞬间跳变 */
static float SlewLimit(float now, float target, float rate, float dt)
{
    float max_step = rate * dt;
    float d        = target - now;

    if (d >  max_step) { d =  max_step; }
    if (d < -max_step) { d = -max_step; }

    return now + d;
}

/* ---- 读档位 ----
 * ⚠️ 实测值（HT-10A 遥控器，SWA5 = CH5）：
 *       上拨 = 192     中间 = 992     下拨 = 1792
 *   ⭐ 注意：上拨是【小值】，下拨是【大值】—— 和直觉相反，别搞反
 *
 *   上拨（< 500）  → 位置模式   ← 考核要求
 *   中间           → 停机（最安全）
 *   下拨（> 1500） → 速度模式   ← 考核要求 */
static CtrlMode_t ReadMode(void)
{
    uint16_t swa = remote.ch[SBUS_CH5_SWA];

    if (swa < CTRL_SWA_POS_MAX)   { return CTRL_MODE_POS;   }
    if (swa > CTRL_SWA_SPEED_MIN) { return CTRL_MODE_SPEED; }
    return CTRL_MODE_STOP;
}


/* ============================================================================
 * 三、⭐ remoteTask 调用：读遥控 + 判档位 + 算目标（每 10 ms）
 * ==========================================================================*/
void Control_RemoteUpdate(void)
{
    const float dt = CTRL_REMOTE_DT_S;
    CtrlMode_t  mode;
    float       stick;

    mode = ReadMode();

    if (mode != s_mode)
    {
        /* 换档了：清掉两路 PID 的积分，避免上一个档位攒的积分造成冲击 */
        Pid_Reset(&pid_speed);
        Pid_Reset(&pid_angle);
        s_mode = mode;
    }

    if (mode == CTRL_MODE_POS)
    {
        /* 左摇杆 → 目标角度（±90°）*/
        stick        = ApplyDeadzone(Remote_ChNorm(remote.ch[CTRL_CH_POS_STICK]));
        s_target_deg = SlewLimit(s_target_deg, stick * CTRL_ANGLE_LIMIT_DEG,
                                 CTRL_POS_SLEW_DEG_PER_S, dt);
        s_stick_rpm  = 0.0f;
    }
    else if (mode == CTRL_MODE_SPEED)
    {
        /* 右摇杆 → 目标转速（±½额定转速）*/
        stick       = ApplyDeadzone(Remote_ChNorm(remote.ch[CTRL_CH_SPEED_STICK]));
        s_stick_rpm = SlewLimit(s_stick_rpm, stick * CTRL_SPEED_LIMIT_RPM,
                                CTRL_SPD_SLEW_RPM_PER_S, dt);

        /* 位置环不用，但让它的目标跟着实际角度走，
         * 这样下次切回位置模式时不会从旧目标猛冲过去 */
        s_target_deg = motor1.out_angle_deg;

        /* ⭐ 还要同步位置环的 last_actual！
         * 否则位置环在速度模式期间 last_actual 不更新，
         * 切回位置模式时 D 项会算出巨大冲击（见 Pid_SyncActual 的说明）*/
        Pid_SyncActual(&pid_angle, motor1.out_angle_deg);
    }
    else /* CTRL_MODE_STOP */
    {
        s_stick_rpm  = SlewLimit(s_stick_rpm, 0.0f, CTRL_SPD_SLEW_RPM_PER_S, dt);
        s_target_deg = motor1.out_angle_deg;
        Pid_SyncActual(&pid_angle, motor1.out_angle_deg);
    }
}


/* ============================================================================
 * 四、⭐ controlTask 调用：保护 + 串级 PID + 发 CAN（每 1 ms）
 * ==========================================================================*/
void Control_Update(void)
{
    const float dt   = CONTROL_DT_S;
    CtrlMode_t  mode = s_mode;          /* 先读一次，后面用这一个值 */
    float       limit;
    float       target_rpm;
    float       current;

    /* ==================== 1. 保护 ====================
     * 返回 0 → 有故障，必须立刻断电流（不能缓降）*/
    limit = Protection_Update();

    if (limit <= 0.0f)
    {
        C620_SendCurrent(1, 0);

        Pid_Reset(&pid_speed);
        Pid_Reset(&pid_angle);

        /* ⚠️ 只重置【本任务自己的】变量。
         * s_mode / s_target_deg / s_stick_rpm 归 remoteTask 管 —— 不动它们，
         * 否则就破坏了"单写单读"，两个任务同时写同一个变量容易出问题。
         * （斜坡限速会保证恢复时目标平滑，不需要在这里兜底）*/
        s_rpm_cmd      = 0.0f;
        s_last_current = 0.0f;
        return;
    }

    /* ==================== 2. 位置环（只有位置模式）====================
     * 输入：目标角度、实际角度（单位 度）
     * 输出：目标转速（单位 输出轴 rpm）→ 直接喂给速度环
     * 输出限幅由 pid_angle.out_max 保证 */
    if (mode == CTRL_MODE_POS)
    {
        target_rpm = Pid_Calc(&pid_angle, s_target_deg,
                              motor1.out_angle_deg, dt);
    }
    else
    {
        target_rpm = s_stick_rpm;
    }
    s_rpm_cmd = target_rpm;

    /* ==================== 3. 速度环 → 电流 ==================== */
    current = Pid_Calc(&pid_speed, target_rpm, motor1.out_rpm, dt);

    /* ---- 夹到保护给的限流上限 ---- */
    if (current >  limit) { current =  limit; }
    if (current < -limit) { current = -limit; }

    /* ---- 电流变化率限制 ---- */
    current = SlewLimit(s_last_current, current, CTRL_CURRENT_SLEW_PER_S, dt);
    s_last_current = current;

    C620_SendCurrent(1, (int16_t)current);
}


/* ============================================================================
 * 五、给 debug 模块读的接口
 * ==========================================================================*/
float      Control_GetTargetDeg(void) { return s_target_deg; }
float      Control_GetTargetRpm(void) { return s_rpm_cmd;    }


/* ============================================================================
 * 六、自测（纯逻辑，不用遥控器、不用电机）
 * ==========================================================================*/
int Control_SelfTest(void)
{
    float v;

    /* ① 斜坡限速：每秒 360°，dt=10ms → 每次最多走 3.6 */
    v = SlewLimit(0.0f, 100.0f, 360.0f, 0.01f);
    if (v < 3.59f || v > 3.61f) { return 1; }

    /* ② 反向也一样 */
    v = SlewLimit(0.0f, -100.0f, 360.0f, 0.01f);
    if (v > -3.59f || v < -3.61f) { return 2; }

    /* ③ 差得比一步还少 → 直接到位，且不能越过目标 */
    v = SlewLimit(9.99f, 10.0f, 360.0f, 0.01f);
    if (v < 9.999f || v > 10.001f) { return 3; }

    /* ④ 死区 */
    if (ApplyDeadzone(0.01f) != 0.0f)  { return 4; }
    if (ApplyDeadzone(0.5f)  != 0.5f)  { return 5; }
    if (ApplyDeadzone(-0.5f) != -0.5f) { return 6; }

    /* ⑤ 判档位（用实测值：上 192 / 中 992 / 下 1792）*/
    remote.ch[SBUS_CH5_SWA] = 192U;       /* 上拨 = 小值 → 位置模式 */
    if (ReadMode() != CTRL_MODE_POS)   { return 7; }
    remote.ch[SBUS_CH5_SWA] = 992U;       /* 中间 → 停机 */
    if (ReadMode() != CTRL_MODE_STOP)  { return 8; }
    remote.ch[SBUS_CH5_SWA] = 1792U;      /* 下拨 = 大值 → 速度模式 */
    if (ReadMode() != CTRL_MODE_SPEED) { return 9; }

    /* ⑥ 摇杆推到底 = 满量程 ±90°（用实测量程 192 / 1792）*/
    v = Remote_ChNorm(1792U) * CTRL_ANGLE_LIMIT_DEG;
    if (v < 89.9f || v > 90.1f)   { return 10; }
    v = Remote_ChNorm(192U) * CTRL_ANGLE_LIMIT_DEG;
    if (v > -89.9f || v < -90.1f) { return 11; }

    /* ⑦ 摇杆推到底 = 满量程 ±½额定转速
     * ⭐ 用宏自己算上下限，这样以后改了 CTRL_SPEED_LIMIT_RPM 不用回来改这里 */
    v = Remote_ChNorm(1792U) * CTRL_SPEED_LIMIT_RPM;
    if (v < (CTRL_SPEED_LIMIT_RPM - 1.0f) ||
        v > (CTRL_SPEED_LIMIT_RPM + 1.0f)) { return 12; }

    /* 复原 */
    remote.ch[SBUS_CH5_SWA] = SBUS_CH_MID;

    return 0;
}
