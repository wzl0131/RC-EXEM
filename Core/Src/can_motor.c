/* ============================================================================
 * can_motor.c  ——  C620 电调 / M3508 电机  CAN 通信模块（实现文件）
 * ----------------------------------------------------------------------------
 * 接口（有什么）在 can_motor.h 里，本文件写"怎么做"
 *
 * 数据流：
 *   电调发帧 → CAN1_RX0 中断 → HAL_CAN_RxFifo0MsgPendingCallback()
 *            → C620_ParseRx() → 填 motor1 → 给 PID 用
 * ==========================================================================*/

#include "can_motor.h"
#include "can.h"          /* hcan1、CAN_FilterTypeDef 等 */
#include "main.h"         /* HAL 库、Error_Handler() */
#include "cmsis_os2.h"    /* osKernelGetTickCount() */
#include <string.h>       /* memset() */


/* ============================================================================
 * 全局变量【定义】（对应的 extern 声明在 can_motor.h 里）
 *   注意：定义只能有这 1 处！头文件里写的是 extern 声明
 * ==========================================================================*/
Motor3508_t motor1;


/* ============================================================================
 * 一、初始化
 * ==========================================================================*/
void M3508_Init(Motor3508_t *m)
{
    /* 把整块内存的每一个字节都设成 0
     *   参数 1：从哪个地址开始
     *   参数 2：设成什么值
     *   参数 3：一共设多少个字节
     * → 结构体里 11 个字段一次全部清零，不用写 11 行 */
    memset(m, 0, sizeof(Motor3508_t));
}


/* ============================================================================
 * 二、多圈角度累计（处理 "8191 → 0" 的过零问题）
 * ----------------------------------------------------------------------------
 * 原理：
 *   raw_angle 只有 0~8191，转一圈就归零重来
 *   → 拿这次的 raw 和上一次的 last_raw_angle 相减，得到"增量"
 *   → 增量累加到 total_cnt 上，就能表示"转了很多圈"
 *
 * 过零修正：
 *   反馈是 1 kHz（1 ms 一帧），M3508 转子最快约 9000 rpm
 *   → 1 ms 内最多转 9000/60*0.001 = 0.15 圈 ≈ 1230 个计数
 *   → 远小于 2048（1/4 圈）
 *   → 所以"差值看起来很大"一定是绕过了零点，必须修正
 * ==========================================================================*/
void M3508_UpdateAngle(Motor3508_t *m, uint16_t raw)
{
    int32_t diff;

    /* ---- 第 1 步：算这次和上次的差值 ---- */
    diff = (int32_t)raw - (int32_t)m->last_raw_angle;

    /* ---- 第 2 步：过零修正 ----
     * 举例：last=8190，raw=5  →  diff = -8185（看起来像猛倒退）
     *       实际是正转绕过零点，真实增量 = +7
     *       修正：-8185 + 8192 = +7  ✓
     * 2048 = 8192 的 1/4，6144 = 8192 的 3/4 */
    if (raw < 2048U && m->last_raw_angle > 6144U)
    {
        diff += 8192;   /* 正转绕过零点 */
    }
    else if (raw > 6144U && m->last_raw_angle < 2048U)
    {
        diff -= 8192;   /* 反转绕过零点 */
    }

    /* ---- 第 3 步：累加 + 记账 ---- */
    m->total_cnt      += diff;      /* 累加到总账本 */
    m->last_raw_angle  = raw;       /* 记下这次的值，给下一次用 */
}


/* ============================================================================
 * 三、解析一帧反馈数据（8 个字节 → 结构体）
 * ----------------------------------------------------------------------------
 * 手册规定的字节顺序（大端 = 高字节在前）：
 *   [0][1] = 转子机械角度  0~8191
 *   [2][3] = 转子转速      int16，单位 rpm
 *   [4][5] = 实际转矩电流  int16，-16384~16384
 *   [6]    = 温度          ℃
 *   [7]    = 错误码        0 = 无异常
 * ==========================================================================*/
int C620_ParseRx(uint32_t std_id, const uint8_t *data)
{
    Motor3508_t *m = &motor1;

    /* ---- 只认（一号电调）电调 ID = 1 的反馈帧（ID = 0x201）---- */
    if (std_id != (C620_RX_ID_BASE + 1U))
    {
        return -1;      /* 不是我们要的 ID */
    }

    /* ---- 大端解析：高字节在前 ----
     *   例：data[0]=0x10, data[1]=0x00
     *       0x10 << 8 = 0x1000
     *       | 0x00   = 0x1000 = 4096  ✓ */
    m->raw_angle   = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    m->raw_rpm     = (int16_t) (((uint16_t)data[2] << 8) | (uint16_t)data[3]);
    m->raw_current = (int16_t) (((uint16_t)data[4] << 8) | (uint16_t)data[5]);
    m->raw_temp    = data[6];
    m->err_code    = data[7];

    /* ---- 多圈角度累计 ---- */
    if (m->inited == 0)
    {
        /* 上电后的第一帧：只建立基准，不做增量累计
         * → 把"上电时电机所在的位置"当作 0 度
         * → 后面的位置环就能用"相对上电位置的 ±90°" */
        m->last_raw_angle = m->raw_angle;
        m->total_cnt      = 0;
				m->inited         = 1; 
    }
    else
    {
        M3508_UpdateAngle(m, m->raw_angle);
    }

    /* ---- 换算成物理量（给 PID 用）---- */
    m->out_angle_deg = (float)m->total_cnt * CNT2DEG_OUT;       /* 计数 → 输出轴度 */
    m->out_rpm       = (float)m->raw_rpm   * ROTOR_RPM2OUT_RPM; /* 转子rpm → 输出轴rpm */

    /* ---- 在线检测：记下"现在还活着" ---- */
    m->last_rx_tick = osKernelGetTickCount();
    m->online       = 1;

    return 0;       /* 解析成功 */
}


/* ============================================================================
 * 四、发送控制帧
 * ----------------------------------------------------------------------------
 * 重要：一帧控制帧里装【4 个电调】的电流！
 *   ID = 0x200 → 装电调 1、2、3、4 的电流（每台 2 字节）
 *   电调1 用 data[0][1]、电调2 用 data[2][3]、
 *   电调3 用 data[4][5]、电调4 用 data[6][7]
 * → 所以不能"只发 1 号电机"而不发其他三个，否则其他三个会被发成 0
 * → 用 static 数组记住 4 台电调最近的值，每次一起发出去
 * ==========================================================================*/
void C620_SendCurrent(uint8_t motor_id, int16_t current)
{
    static int16_t cur_buf[4] = {0, 0, 0, 0};   /* 4 台电调最近的电流值 */
    CAN_TxHeaderTypeDef tx;
    uint8_t  data[8];
    uint32_t mailbox;
    uint8_t  i;

    /* 本函数只处理电调 ID 1~4（ID 5~8 用 0x1FF，需要另一套）*/
    if (motor_id < 1U || motor_id > 4U)
    {
        return;
    }

    /* ---- 限幅：手册规定范围 -16384 ~ 16384 ---- */
    if (current >  C620_CURRENT_MAX) { current =  C620_CURRENT_MAX; }
    if (current < -C620_CURRENT_MAX) { current = -C620_CURRENT_MAX; }

    /* ---- 更新这一台的值 ---- */
    cur_buf[motor_id - 1U] = current;

    /* ---- 把 4 台电调的值都拼进 8 个字节（大端）---- */
    for (i = 0U; i < 4U; i++)
    {
        data[i * 2U]      = (uint8_t)((uint16_t)cur_buf[i] >> 8);       /* 高字节 */
        data[i * 2U + 1U] = (uint8_t)((uint16_t)cur_buf[i] & 0x00FFU);  /* 低字节 */
    }

    /* ---- 填帧头 ---- */
    tx.StdId              = C620_TX_ID_1_4;   /* 0x200 */
    tx.IDE                = CAN_ID_STD;       /* 标准帧 */
    tx.RTR                = CAN_RTR_DATA;     /* 数据帧 */
    tx.DLC                = 8U;               /* 8 个字节 */
    tx.TransmitGlobalTime = DISABLE;

    /* ---- 发送（返回值不用管，失败就算了，下次还会发）---- */
    (void)HAL_CAN_AddTxMessage(&hcan1, &tx, data, &mailbox);
}


/* ============================================================================
 * 五、CAN 外设初始化
 * ----------------------------------------------------------------------------
 * CubeMX 生成的 MX_CAN1_Init() 只配好了"波特率参数"，还差三件事：
 *   ① 配滤波器 —— 不配的话，收到的帧进不了 FIFO，等于收不到
 *   ② HAL_CAN_Start() —— 不 Start，CAN 还停在初始化模式，不工作
 *   ③ 打开"接收中断通知" —— 不开的话，收到帧也不会进回调函数
 * ==========================================================================*/
void C620_CanInit(void)
{
    CAN_FilterTypeDef f;

    /* ---- 滤波器：只接收 ID = 0x200 ~ 0x20F 的帧 ----
     * 规则：(收到的ID & 掩码) == (设定的ID & 掩码)
     *   设定 ID = 0x200，掩码 = 0x7F0
     *   0x7F0 = 0111 1111 0000  → 高 7 位必须匹配，低 4 位随便
     *   → 接受 0x200 ~ 0x20F ✓（正好是 8 个电调的反馈 ID）
     *
     * 为什么左移 5 位？
     *   STM32 的 bxCAN 寄存器把标准帧 ID 存在 bit15~bit5
     *   → 代码里的 ID 必须 << 5 才能对齐到寄存器正确位置 */
    f.FilterBank           = 0U;
    f.FilterMode           = CAN_FILTERMODE_IDMASK;      /* ID + 掩码 模式 */
    f.FilterScale          = CAN_FILTERSCALE_32BIT;
    f.FilterIdHigh         = (C620_RX_ID_BASE << 5);     /* 0x200 << 5 = 0x4000 */
    f.FilterIdLow          = 0U;
    f.FilterMaskIdHigh     = (0x7F0U << 5);              /* 0x7F0 << 5 = 0xFE00 */
    f.FilterMaskIdLow      = 0U;
    f.FilterFIFOAssignment = CAN_RX_FIFO0;               /* 收到的帧放进 FIFO0 */
    f.FilterActivation     = ENABLE;
    f.SlaveStartFilterBank = 14U;                        /* F4 双 CAN 时用，单 CAN 写 14 */

    if (HAL_CAN_ConfigFilter(&hcan1, &f) != HAL_OK)
    {
        Error_Handler();
    }

    /* ---- 启动 CAN ---- */
    if (HAL_CAN_Start(&hcan1) != HAL_OK)
    {
        Error_Handler();
    }

    /* ---- 打开"RX FIFO0 收到新消息"的中断通知 ----
     * 打开之后，收到帧才会调用下面的 HAL_CAN_RxFifo0MsgPendingCallback() */
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    {
        Error_Handler();
    }
}


/* ============================================================================
 * 六、HAL 回调函数（"接线"）
 * ----------------------------------------------------------------------------
 * 这个函数名是【HAL 库规定的弱函数】，不是我们自己起的名字！
 * 拼写错一个字母 → 编译不会报错 → 但永远收不到数据 ⚠️
 *
 * 调用链：
 *   CAN1_RX0_IRQHandler()  →  HAL_CAN_IRQHandler()  →  本函数
 *
 * ⚠️ 本函数在【中断】里执行 → 绝对不能调用 osDelay()、printf()、malloc()
 * ==========================================================================*/
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef rx;
    uint8_t data[8];

    /* 只处理 CAN1 */
    if (hcan->Instance != CAN1)
    {
        return;
    }

    /* 把 8 个字节从邮箱里读出来 */
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx, data) != HAL_OK)
    {
        return;
    }

    /* 只要标准帧、8 字节的（电调反馈就长这样）*/
    if (rx.IDE != CAN_ID_STD || rx.DLC != 8U)
    {
        return;
    }

    /* 交给解析函数 */
    (void)C620_ParseRx(rx.StdId, data);
}


/* ============================================================================
 * 七、自测函数（不需要电机、不需要遥控器就能验证代码对不对）
 *   返回值：0 = 通过；非 0 = 第几项失败
 * ==========================================================================*/

/* ---- 自测 1：验证大端解析 ----
 * 自己编一帧"我知道答案"的假数据，喂给 C620_ParseRx，再检查结果 */
int C620_SelfTest(void)
{
    /* 假数据（每个字节对应什么，我心里清清楚楚）：
     *   [0][1] = 0x10 0x00 = 4096  角度
     *   [2][3] = 0x03 0xE8 = 1000  转速
     *   [4][5] = 0x13 0x88 = 5000  电流
     *   [6]    = 0x2D      = 45    温度
     *   [7]    = 0x00      = 0     错误码 */
    const uint8_t fake[8] = {0x10, 0x00, 0x03, 0xE8, 0x13, 0x88, 0x2D, 0x00};

    if (C620_ParseRx(C620_RX_ID_BASE + 1U, fake) != 0) { return 1; }  /* ID 0x201 应该成功 */

    /* 一项一项对答案 */
    if (motor1.raw_angle   != 4096) { return 2; }
    if (motor1.raw_rpm     != 1000) { return 3; }
    if (motor1.raw_current != 5000) { return 4; }
    if (motor1.raw_temp    != 45)   { return 5; }
    if (motor1.err_code    != 0)    { return 6; }

    /* 浮点数不能用 == 比较！要给一个"允许误差范围"
     *   1000 / 19.2032 = 52.07 rpm */
    if (motor1.out_rpm < 52.0f || motor1.out_rpm > 52.2f) { return 7; }

    /* 自测改了 motor1，测完把它清零，免得影响真实运行 */
    M3508_Init(&motor1);

    return 0;   /* 全部通过 */
}

/* ---- 自测 2：验证过零判断 + 多圈累计 ----
 * 覆盖 4 种情况：正常正转 / 正常反转 / 正向跨零 / 反向跨零 */
int AngleAccumSelfTest(void)
{
    Motor3508_t t;

    /* ① 正常正转 100 个计数 */
    M3508_Init(&t);
    t.last_raw_angle = 1000U;      /* 假装"上一次读到 1000" */
    M3508_UpdateAngle(&t, 1100U);
    if (t.total_cnt != 100) { return 1; }

    /* ② 正常反转 100 个计数 */
    M3508_Init(&t);
    t.last_raw_angle = 1000U;
    M3508_UpdateAngle(&t, 900U);
    if (t.total_cnt != -100) { return 2; }

    /* ③ 正向跨零：8190 → 5，应该 +7 */
    M3508_Init(&t);
    t.last_raw_angle = 8190U;
    M3508_UpdateAngle(&t, 5U);
    if (t.total_cnt != 7) { return 3; }

    /* ④ 反向跨零：3 → 8190，应该 -5 */
    M3508_Init(&t);
    t.last_raw_angle = 3U;
    M3508_UpdateAngle(&t, 8190U);
    if (t.total_cnt != -5) { return 4; }

    return 0;   /* 全部通过 */
}
