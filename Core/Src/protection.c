/* ============================================================================
 * protection.c  ——  保护模块（实现文件）
 * ----------------------------------------------------------------------------
 * 只有 4 条判断，从上到下依次检查，命中就返回 0（断电流）。
 * 没有锁存、没有降额、没有堵转检测 —— 正常运行时它什么都不做。
 * ==========================================================================*/

#include "protection.h"
#include "can_motor.h"      /* motor1 */
#include "pid.h"            /* PID_SPEED_OUT_MAX */
#include "remote.h"         /* Remote_IsOnline() */
#include "cmsis_os2.h"      /* osKernelGetTickCount() */
#include <string.h>         /* memset() */


/* 全局变量【定义】*/
Protect_t protect;


/* ============================================================================
 * 初始化
 * ==========================================================================*/
void Protection_Init(void)
{
    memset(&protect, 0, sizeof(Protect_t));
}


/* ============================================================================
 * ⭐ 核心：算本次允许的电流上限
 * ==========================================================================*/
float Protection_Update(void)
{
    float limit = PID_SPEED_OUT_MAX;        /* 默认不限制 */

    protect.temp = (float)motor1.raw_temp;

    /* ---- ① 遥控器掉线（收不到 SBUS 帧 / 接收机报失控保护）---- */
    if (Remote_IsOnline() == 0)
    {
        protect.fault = FAULT_RC_OFFLINE;
        return 0.0f;
    }

    /* ---- ② 电调掉线（100 ms 收不到 CAN 反馈）---- */
    if (M3508_IsOnline() == 0)
    {
        protect.fault = FAULT_ESC_OFFLINE;
        return 0.0f;
    }

    /* ---- ③ 电调报了错误码（手册第 8 个字节）---- */
    if (motor1.err_code != 0U)
    {
        protect.fault = FAULT_ESC_ERROR;
        return 0.0f;
    }

    /* ---- ④ 电机过温 ----
     * ⚠️ 故意【不做】降额（降额会削弱位置环的保持能力）*/
    if (protect.temp >= PROTECT_TEMP_STOP_C)
    {
        protect.fault = FAULT_OVER_TEMP;
        return 0.0f;
    }

    /* ---- 一切正常 ---- */
    protect.fault = FAULT_NONE;
    return limit;
}


/* ============================================================================
 * 自测：靠改写全局变量构造各种场景
 * ==========================================================================*/

/* 把环境设成"一切正常" */
static void SetEnvNormal(void)
{
    Protection_Init();

    remote.rx_count     = 5U;
    remote.failsafe     = 0U;
    remote.last_rx_tick = osKernelGetTickCount();

    motor1.inited       = 1U;
    motor1.last_rx_tick = osKernelGetTickCount();   /* 刚刚收到过 */
    motor1.err_code     = 0U;
    motor1.raw_temp     = 30U;
}

/* 恢复成"刚上电"的样子（自测完必须调）*/
static void RestoreEnv(void)
{
    Protection_Init();

    remote.rx_count     = 0U;
    remote.failsafe     = 0U;
    remote.last_rx_tick = 0U;

    motor1.inited       = 0U;
    motor1.last_rx_tick = 0U;
    motor1.err_code     = 0U;
    motor1.raw_temp     = 0U;
}

int Protection_SelfTest(void)
{
    float lim;

    /* ① 一切正常 → 返回满量程（= 不限流）*/
    SetEnvNormal();
    lim = Protection_Update();
    if (lim < (PID_SPEED_OUT_MAX - 1.0f)) { return 1; }
    if (protect.fault != FAULT_NONE)      { return 2; }

    /* ② 遥控掉线 → 断电流 */
    SetEnvNormal();
    remote.rx_count = 0U;
    lim = Protection_Update();
    if (protect.fault != FAULT_RC_OFFLINE) { return 3; }
    if (lim != 0.0f)                       { return 4; }

    /* ③ 遥控恢复 → 自动恢复（没有锁存）*/
    SetEnvNormal();
    remote.rx_count = 0U;
    (void)Protection_Update();
    remote.rx_count     = 5U;
    remote.last_rx_tick = osKernelGetTickCount();
    lim = Protection_Update();
    if (lim < (PID_SPEED_OUT_MAX - 1.0f)) { return 5; }

    /* ④ 电调掉线之一：收到过帧，但 200ms 没再来（超时）*/
    SetEnvNormal();
    motor1.last_rx_tick = osKernelGetTickCount() - 200U;   /* 假装 200ms 前收的 */
    lim = Protection_Update();
    if (protect.fault != FAULT_ESC_OFFLINE) { return 6; }
    if (lim != 0.0f)                        { return 7; }

    /* ④b 电调掉线之二：一帧都没收到过 */
    SetEnvNormal();
    motor1.inited = 0U;
    lim = Protection_Update();
    if (protect.fault != FAULT_ESC_OFFLINE) { return 14; }

    /* ⑤ 电调报错误码 → 断电流 */
    SetEnvNormal();
    motor1.err_code = 4U;                  /* 4 = 位置传感器数据丢失 */
    lim = Protection_Update();
    if (protect.fault != FAULT_ESC_ERROR) { return 8; }
    if (lim != 0.0f)                      { return 9; }

    /* ⑥ 过温 → 断电流 */
    SetEnvNormal();
    motor1.raw_temp = 120U;
    lim = Protection_Update();
    if (protect.fault != FAULT_OVER_TEMP) { return 10; }
    if (lim != 0.0f)                      { return 11; }

    /* ⑦ 温度刚好在阈值下面（114℃）→ 不该触发 */
    SetEnvNormal();
    motor1.raw_temp = 114U;
    lim = Protection_Update();
    if (protect.fault != FAULT_NONE)      { return 12; }
    if (lim < (PID_SPEED_OUT_MAX - 1.0f)) { return 13; }

    RestoreEnv();
    return 0;
}
