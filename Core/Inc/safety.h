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

/* ⭐ 检查故障 + 任务心跳，返回故障码（位掩码）
 *   ⚠️ controlTask(1ms) 和 remoteTask(10ms) 【都调】它！
 *      这样即使 controlTask 卡死，remoteTask 也能把"谁卡了"记下来 ✓ */
uint32_t Safety_Check(void);

/* ⭐ 根据最近一次 Safety_Check() 的结果决定喂狗还是让狗咬
 *   ⚠️【只】在 controlTask 里调 —— controlTask 卡死就没人喂狗 → 复位 ✓ */
void     Safety_FeedDog(void);

/* ⭐ 给 control.c 用的总入口（1kHz），返回本次允许的电流幅值上限
 *   返回 0     = 有通信故障，必须【立刻】断电流
 *   返回 16384 = 正常，不限制
 *   内部依次调 Safety_Check() + Safety_FeedDog() */
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

/* ⭐ 读某个任务"距离上次心跳过了多少毫秒"
 *   正常应该 < SAFETY_HB_TIMEOUT_MS（200），超过就说明它卡了
 *   给 debug 模块打包到 J-Scope 变量里用 */
uint32_t Safety_GetHeartbeatAge(SafetyTaskId_t id);

/* ⭐ 给 stm32f4xx_it.c 里的 Fault_Handler 调用
 *   参数 fault_sp = 异常栈帧的起始地址（用下面的宏取）
 *   作用：记录故障码和崩溃地址 → 亮红灯 → 不喂狗 → 等看门狗复位 */
void     Safety_FaultHandler(uint32_t fault_sp);


/* ============================================================================
 * ⭐⭐ 取异常栈帧地址的宏（必须在 Fault_Handler 的【第一行】用！）
 * ----------------------------------------------------------------------------
 * 原理：
 *   Cortex-M4 进异常时，硬件自动把 8 个寄存器压栈：
 *       R0, R1, R2, R3, R12, LR, PC, xPSR
 *   同时把 LR 设成 EXC_RETURN（0xFFFFFFF9 或 0xFFFFFFFD）：
 *       bit2 = 0  → 异常前用的是 MSP（主栈）
 *       bit2 = 1  → 异常前用的是 PSP（任务栈，FreeRTOS 任务跑在 PSP 上）
 *   所以要先看 LR 的 bit2 决定查哪个栈，才能拿到正确的栈帧地址。
 *
 * ⚠️⚠️ 为什么必须写成【宏】而不是函数？
 *   因为 BL 指令会把 LR 覆盖成"返回地址"！
 *   一旦调用了任何函数，LR 里就不再是 EXC_RETURN 了，
 *   再去 TST LR,#4 就是拿"某个返回地址的 bit2"在瞎猜，
 *   有一半概率查错栈 → 读出来的崩溃地址是垃圾。
 *
 *   写成宏 → 编译时直接展开到 handler 函数体里 → 不产生 BL → LR 是好的 ✓
 *
 * 用法（在 stm32f4xx_it.c 的每个 Fault_Handler 里）：
 *
 *     void HardFault_Handler(void)
 *     {
 *       SAFETY_CAPTURE_FAULT_SP();      // ⭐ 必须是函数体第一条语句
 *       Safety_FaultHandler(fault_sp);  // 之后怎么调用都行
 *       while (1) { }
 *     }
 *
 * 展开后会声明一个局部变量 fault_sp（uint32_t），存放异常栈帧地址。
 * ==========================================================================*/

#define SAFETY_CAPTURE_FAULT_SP()                                       \
    uint32_t fault_sp_;                                                 \
    __asm volatile (                                                    \
        "TST    LR, #4          \n"   /* 测 EXC_RETURN 的 bit2        */ \
        "ITE    EQ              \n"   /* If-Then-Else，条件 = 相等     */ \
        "MRSEQ  %0, MSP         \n"   /* bit2==0 → 栈帧在 MSP 上      */ \
        "MRSNE  %0, PSP         \n"   /* bit2==1 → 栈帧在 PSP 上      */ \
        : "=r" (fault_sp_) :: "cc")


/* ⭐ 从异常栈帧里取崩溃地址（PC）
 *   fault_sp 是 SAFETY_CAPTURE_FAULT_SP() 取到的栈帧地址
 *   栈帧里的第 7 个字（偏移 24）就是 PC —— 崩在哪条指令
 *
 *   拿这个地址去 MDK-ARM\TEST\TEST.map 里查，就知道崩在哪个函数 */
uint32_t Safety_GetPCFromFrame(uint32_t fault_sp);


/* ============================================================================
 * 六、自测
 * ==========================================================================*/
int      Safety_SelfTest(void);


#endif /* SAFETY_H */
