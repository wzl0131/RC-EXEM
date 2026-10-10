/* ============================================================================
 * safety.h  ——  安全中心（头文件）
 * ----------------------------------------------------------------------------
 * ⭐ 这个模块把【所有】安全相关的逻辑集中到一处：
 *
 *   第一层：通信 / 硬件故障（可恢复）
 *       遥控掉线 / 电调掉线 / 电调报错 / 过温
 *       → 只断电流，程序继续跑，故障消失自动恢复
 *
 *   第二层：程序跑飞（不可恢复 → 看门狗复位）
 *       HardFault / BusFault / UsageFault / MemManage
 *       → 记录崩溃地址 → 亮红灯 → 不喂狗 → 看门狗复位
 *
 *   第三层：任务卡死（不可恢复 → 看门狗复位）
 *       每个任务定期上报心跳 → 有任务超时没上报
 *       → 记录是哪个任务 → 不喂狗 → 看门狗复位
 *
 *   ⭐ 第四件事：把故障记到【备份寄存器】，复位后还能读出来
 *              → 这就是"出现问题能集中看到哪里出问题了"
 * ==========================================================================*/

#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>


/* ============================================================================
 * 一、故障码（⭐ 用【位掩码】，可以同时报多个故障）
 * ----------------------------------------------------------------------------
 * 为什么用位掩码而不是枚举？
 *   枚举一次只能报一个故障。比如"遥控和电调同时掉线"，
 *   用枚举只能看到最后一个；用位掩码能同时看到两个 ✓
 * ==========================================================================*/

#define FAULT_NONE              0x00000000U

/* ---- 第一层：通信 / 硬件（只断电流，不复位）---- */
#define FAULT_RC_OFFLINE        (1U << 0)   /* 遥控器掉线 */
#define FAULT_ESC_OFFLINE       (1U << 1)   /* 电调掉线（收不到 CAN 反馈）*/
#define FAULT_ESC_ERROR         (1U << 2)   /* 电调报了错误码 */
#define FAULT_OVER_TEMP         (1U << 3)   /* 电机过温 */

/* ---- 第二层：程序跑飞（记录 + 等看门狗复位）---- */
#define FAULT_HARDFAULT         (1U << 4)   /* 进了 HardFault / 其他 Fault */

/* ---- 第三层：任务卡死（停止喂狗 → 复位）---- */
#define FAULT_TASK_CONTROL      (1U << 5)   /* 控制任务卡死 */
#define FAULT_TASK_REMOTE       (1U << 6)   /* 遥控任务卡死 */
#define FAULT_TASK_DEBUG        (1U << 7)   /* 心跳任务卡死 */

/* 三个任务卡死位的掩码（判断"有没有任务卡死"用）*/
#define FAULT_TASK_MASK         (FAULT_TASK_CONTROL | FAULT_TASK_REMOTE | FAULT_TASK_DEBUG)

/* 第一层故障的掩码（判断"要不要断电流"用）*/
#define FAULT_COMM_MASK         (FAULT_RC_OFFLINE | FAULT_ESC_OFFLINE | \
                                 FAULT_ESC_ERROR  | FAULT_OVER_TEMP)


/* ============================================================================
 * 二、复位原因（Safety_GetResetReason 的返回值）
 * ==========================================================================*/
#define RESET_REASON_UNKNOWN    0U
#define RESET_REASON_IWDG       1U      /* 看门狗复位（说明程序卡死过）*/
#define RESET_REASON_SOFTWARE   2U      /* 软件复位 */
#define RESET_REASON_POWER_ON   3U      /* 上电复位（正常开机）*/
#define RESET_REASON_PIN        4U      /* 复位引脚 / 按键 */


/* ============================================================================
 * 三、阈值
 * ==========================================================================*/

/* 电机过温停机（手册：电调自己到 125℃ 会报错误码 8，我们提前一点）*/
#define SAFETY_TEMP_STOP_C          115.0f

/* ⭐ 任务心跳超时（毫秒）
 *   每个任务都 ≤10ms 上报一次，200ms 相当于给了 20 倍的余量
 *   ⚠️ 必须 < 看门狗超时（500ms），否则心跳还没判定超时，狗先咬了 */
#define SAFETY_HB_TIMEOUT_MS        200U

/* 任务个数 */
#define SAFETY_TASK_COUNT           3U


/* ============================================================================
 * 四、任务 ID
 * ==========================================================================*/
typedef enum {
    SAFETY_TASK_CONTROL = 0,    /* controlTask（1ms）*/
    SAFETY_TASK_REMOTE,         /* remoteTask （10ms）*/
    SAFETY_TASK_DEBUG           /* debugTask  （10ms，每 50 次翻一次绿灯）*/
} SafetyTaskId_t;


/* ============================================================================
 * 五、对外接口
 * ==========================================================================*/

/* 初始化：读复位原因 + 读上次的故障记录 + 复位心跳
 * ⭐ 在 main() 的 USER CODE 2 里调用（自测之后、启动接收之前）*/
void     Safety_Init(void);

/* ⭐ 各任务定期调用，上报"我还活着"
 *   调用周期：controlTask 1ms / remoteTask 10ms / debugTask 10ms */
void     Safety_Heartbeat(SafetyTaskId_t id);

/* ⭐ 每个控制周期调用一次（1kHz），返回本次允许的电流幅值上限
 *   返回 0     = 有通信故障，必须【立刻】断电流
 *   返回 16384 = 正常，不限制
 *   内部还会根据心跳决定【喂狗还是让狗咬】*/
float    Safety_Update(void);

/* 读当前故障码（调试时加到 Watch 窗口看）*/
uint32_t Safety_GetFault(void);

/* ⭐ 读【上次复位前】的故障码（复位后还能看到，用来定位问题）*/
uint32_t Safety_GetLastFault(void);

/* ⭐ 读【上次崩溃的地址】（HardFault 时存下来的 PC）
 *   拿这个地址去 MDK-ARM\TEST\TEST.map 里查，就知道崩在哪个函数 */
uint32_t Safety_GetFaultPC(void);

/* 读这次是什么复位（见上面的 RESET_REASON_xxx）*/
uint8_t  Safety_GetResetReason(void);

/* 读复位次数（上电后清零）*/
uint16_t Safety_GetResetCount(void);

/* ⭐ 给 stm32f4xx_it.c 里的 Fault_Handler 调用
 *   作用：记录故障码和崩溃地址 → 亮红灯 → 不喂狗 → 等看门狗复位 */
void     Safety_FaultHandler(void);


/* ============================================================================
 * 六、自测
 * ==========================================================================*/
int      Safety_SelfTest(void);


#endif /* SAFETY_H */
