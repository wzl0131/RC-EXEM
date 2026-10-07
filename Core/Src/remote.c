/* ============================================================================
 * remote.c  ——  SBUS 遥控接收模块（实现文件）
 * ----------------------------------------------------------------------------
 * 数据流：
 *   USART1 收到 1 个字节 → 中断 → HAL_UART_RxCpltCallback()
 *        → Remote_FeedByte()  ← 自己攒帧、自己找帧头
 *        → 攒满 25 字节 → Remote_ParseFrame() → 拆出 16 个通道
 *
 * ⭐ 为什么用"一字节一字节收"而不是"一次收 25 字节"？
 *   一次收 25 字节的话，只要中间掉一个字节，后面所有帧都会错位；
 *   而"自己找帧头 0x0F"的写法能自动重新同步，而且【可以离线自测】——
 *   自测函数只要逐个字节喂假数据，完全不需要遥控器。
 * ==========================================================================*/

#include "remote.h"
#include "usart.h"        /* huart1 */
#include "main.h"         /* HAL 库 */
#include "cmsis_os2.h"    /* osKernelGetTickCount() */
#include <string.h>       /* memset() */


/* ============================================================================
 * 全局变量【定义】
 * ==========================================================================*/
Remote_t remote;


/* ============================================================================
 * 内部状态（接收状态机）
 *   用【文件级 static】而不是函数内 static，这样 Remote_Init() 能把它复位
 * ==========================================================================*/
static uint8_t rx_byte;                     /* HAL 每次收到的 1 个字节 */
static uint8_t rx_buf[SBUS_FRAME_LEN];      /* 攒帧用的缓冲区 */
static uint8_t rx_idx = 0U;                 /* 攒到第几个字节了 */


/* ============================================================================
 * 一、初始化
 * ==========================================================================*/
void Remote_Init(void)
{
    memset(&remote, 0, sizeof(Remote_t));   /* 通道值、标志、统计 全部清零 */
    rx_idx = 0U;                            /* 解析状态机复位 */
    memset(rx_buf, 0, sizeof(rx_buf));
}


/* ============================================================================
 * 二、启动硬件接收
 *   在 main.c 的 MX_USART1_UART_Init() 之后调用一次
 * ==========================================================================*/
void Remote_StartReceive(void)
{
    /* 让 HAL 每次只收 1 个字节，收满就进 HAL_UART_RxCpltCallback */
    (void)HAL_UART_Receive_IT(&huart1, &rx_byte, 1U);
}


/* ============================================================================
 * 三、位操作工具：从 buf 的第 start_bit 位开始，取 n 位出来
 * ----------------------------------------------------------------------------
 * SBUS 把 16 个 11 位通道【一个接一个】挤在 22 个字节里，中间没有对齐
 *   ch[0] 占 bit 0~10
 *   ch[1] 占 bit 11~21
 *   ch[2] 占 bit 22~32     ← 跨越了字节边界！
 *   ...
 *   ch[15] 占 bit 165~175
 *
 * 位序是"小端"：先到的字节是低位，先到的位是低位
 * ==========================================================================*/
static uint16_t GetBits(const uint8_t *buf, uint8_t start_bit, uint8_t n)
{
    uint16_t v = 0U;
    uint8_t  i;

    for (i = 0U; i < n; i++)
    {
        uint8_t bit  = (uint8_t)(start_bit + i);
        uint8_t byte = (uint8_t)(bit / 8U);      /* 这个位在第几个字节 */
        uint8_t off  = (uint8_t)(bit % 8U);      /* 在字节里的第几位 */

        if ((buf[byte] & (uint8_t)(1U << off)) != 0U)
        {
            v |= (uint16_t)(1U << i);            /* 把第 i 位置 1 */
        }
    }
    return v;
}


/* ============================================================================
 * 四、解析一整帧（只在收满 25 字节时被调用）
 * ==========================================================================*/
static void Remote_ParseFrame(const uint8_t *buf)
{
    uint8_t i;
    uint8_t flags;

    /* ---- 帧尾校验：不是 0x00 就整帧丢掉 ---- */
    if (buf[SBUS_FRAME_LEN - 1U] != SBUS_TAIL)
    {
        return;
    }

    /* ---- 拆出 16 个通道 ----
     * 数据从 buf[1] 开始，第 i 个通道占 bit (i*11) ~ (i*11+10) */
    for (i = 0U; i < SBUS_CH_NUM; i++)
    {
        remote.ch[i] = GetBits(&buf[1], (uint8_t)(i * 11U), 11U);
    }

    /* ---- 第 24 个字节（buf[23]）是标志位 ----
     *   bit0 = 通道 17
     *   bit1 = 通道 18
     *   bit2 = Frame Lost（这一帧丢了）
     *   bit3 = Failsafe（遥控器失控保护，说明信号断了）*/
    flags = buf[23];
    remote.frame_lost = ((flags & 0x04U) != 0U) ? 1U : 0U;
    remote.failsafe   = ((flags & 0x08U) != 0U) ? 1U : 0U;

    /* ---- 记录"活着"的证据 ---- */
    remote.last_rx_tick = osKernelGetTickCount();
    remote.rx_count++;
}


/* ============================================================================
 * 五、喂进一个字节（在 USART1 中断里调用）
 * ----------------------------------------------------------------------------
 * 状态机逻辑：
 *   rx_idx = 0 时：这个字节必须是帧头 0x0F，否则丢掉、继续等
 *   rx_idx > 0 时：无脑存下来，攒满 25 个就解析，然后复位等下一个帧头
 * ==========================================================================*/
void Remote_FeedByte(uint8_t b)
{
    /* --- 等帧头 --- */
    if (rx_idx == 0U)
    {
        if (b != SBUS_HEADER)
        {
            return;                     /* 不是帧头，继续等 */
        }
        rx_buf[0] = b;
        rx_idx    = 1U;
        return;
    }

    /* --- 攒数据 --- */
    rx_buf[rx_idx] = b;
    rx_idx++;

    /* --- 攒满一帧 --- */
    if (rx_idx >= SBUS_FRAME_LEN)
    {
        rx_idx = 0U;                    /* 先复位，免得解析里出错影响状态 */
        Remote_ParseFrame(rx_buf);
    }
}


/* ============================================================================
 * 六、HAL 回调（"接线"）
 * ----------------------------------------------------------------------------
 * ⚠️ 这两个函数名都是 HAL 库规定的弱函数，拼错一个字母就静默失效！
 * ⚠️ 都在【中断】里执行 → 不能调用 osDelay() / printf() / malloc()
 * ==========================================================================*/

/* 收到 1 个字节 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1)
    {
        return;
    }

    Remote_FeedByte(rx_byte);           /* 交给状态机 */

    /* ⭐ 必须重新"挂上"接收！
     * HAL_UART_Receive_IT 是【一次性】的，收满指定长度就停了
     * 不重新调用的话，只会收到第一个字节，后面全是死的 */
    (void)HAL_UART_Receive_IT(&huart1, &rx_byte, 1U);
}

/* 通信出错（帧错误 / 溢出 / 噪声）
 * ⭐ SBUS 是反相信号，如果反相器/接线有问题很容易触发错误。
 *    HAL 出错后会【自动关闭接收】，所以必须在这里重新挂上，
 *    否则一次错误就导致"永久收不到数据"，而且没有任何提示！ */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        (void)HAL_UART_Receive_IT(&huart1, &rx_byte, 1U);
    }
}


/* ============================================================================
 * 七、在线判断（现算，不存状态）
 * ----------------------------------------------------------------------------
 * 三个条件同时满足才算在线：
 *   ① 至少收到过一帧          （否则 remote 全 0，通道值都是假的）
 *   ② 接收机没有报失控保护
 *   ③ 距离最后一帧不超过 50 ms
 * ==========================================================================*/
int Remote_IsOnline(void)
{
    uint32_t now;

    if (remote.rx_count == 0U)
    {
        return 0;
    }
    if (remote.failsafe != 0U)
    {
        return 0;
    }

    now = osKernelGetTickCount();
    if ((now - remote.last_rx_tick) > REMOTE_TIMEOUT_MS)
    {
        return 0;
    }
    return 1;
}


/* ============================================================================
 * 八、通道值归一化：172~1811 → -1.0 ~ +1.0
 * ==========================================================================*/
float Remote_ChNorm(uint16_t raw)
{
    float v;

    if (raw > SBUS_CH_MID)
    {
        /* 中位以上：0 → +1（分母是 1811-992 = 819）*/
        v = (float)(raw - SBUS_CH_MID) / (float)(SBUS_CH_MAX - SBUS_CH_MID);
    }
    else
    {
        /* 中位以下：0 → -1（分母是 992-172 = 820）*/
        v = -(float)(SBUS_CH_MID - raw) / (float)(SBUS_CH_MID - SBUS_CH_MIN);
    }

    /* 限幅：如果收到的是垃圾数据（比如 2047），归一化后会超出 ±1 */
    if (v >  1.0f) { v =  1.0f; }
    if (v < -1.0f) { v = -1.0f; }

    return v;
}


/* ============================================================================
 * 九、自测函数
 * ----------------------------------------------------------------------------
 * 思路：我可以【自己把 16 个通道打包成一帧 SBUS】，再喂给解析函数
 *       → 打包和解析是一对逆运算，能对上就说明两边都对（round-trip 测试）
 * ==========================================================================*/

/* 把 v 的 n 位写进 buf 的第 start_bit 位开始处（GetBits 的逆运算）*/
static void SetBits(uint8_t *buf, uint8_t start_bit, uint8_t n, uint16_t v)
{
    uint8_t i;

    for (i = 0U; i < n; i++)
    {
        uint8_t bit  = (uint8_t)(start_bit + i);
        uint8_t byte = (uint8_t)(bit / 8U);
        uint8_t off  = (uint8_t)(bit % 8U);

        if ((v & (uint16_t)(1U << i)) != 0U)
        {
            buf[byte] |=  (uint8_t)(1U << off);
        }
        else
        {
            buf[byte] &= (uint8_t)(~(1U << off));
        }
    }
}

/* 造一帧假的 SBUS 数据 */
static void BuildFakeFrame(uint8_t *buf, const uint16_t *ch, uint8_t flags)
{
    uint8_t i;

    memset(buf, 0, SBUS_FRAME_LEN);
    buf[0]                        = SBUS_HEADER;   /* 帧头 */
    buf[23]                       = flags;         /* 标志位 */
    buf[SBUS_FRAME_LEN - 1U]      = SBUS_TAIL;     /* 帧尾 */

    for (i = 0U; i < SBUS_CH_NUM; i++)
    {
        SetBits(&buf[1], (uint8_t)(i * 11U), 11U, ch[i]);
    }
}

int Remote_SelfTest(void)
{
    uint8_t  frame[SBUS_FRAME_LEN];
    uint8_t  i;
    uint32_t count_before;
    int      ok;

    /* 故意用一些"刁钻"的值：
     *   992 = 中位、1811 = 最大、172 = 最小、0 和 2047 = 11 位的两个极端
     *   还混了几个跨越字节边界的值，专门验证 GetBits 的位运算 */
    const uint16_t test_ch[SBUS_CH_NUM] = {
        992U, 1811U, 172U, 1000U, 1500U, 2047U, 0U, 1U,
        2U, 3U, 1023U, 1024U, 2000U, 500U, 300U, 1700U
    };

    /* ============ ① 逐字节喂进去，看 16 个通道能不能原样读回来 ============ */
    Remote_Init();                              /* 复位状态机 */
    BuildFakeFrame(frame, test_ch, 0x00U);      /* 造帧 */
    for (i = 0U; i < SBUS_FRAME_LEN; i++)
    {
        Remote_FeedByte(frame[i]);              /* 一个字节一个字节喂 */
    }
    if (remote.rx_count != 1U) { return 1; }    /* 应该刚好收到 1 帧 */

    for (i = 0U; i < SBUS_CH_NUM; i++)
    {
        if (remote.ch[i] != test_ch[i]) { return (int)(2 + i); }   /* ch 不对 → 返回 2~17 */
    }

    /* ============ ② 归一化：中位 = 0 ============ */
    {
        float v = Remote_ChNorm(SBUS_CH_MID);
        if (v < -0.01f || v > 0.01f) { return 20; }
    }

    /* ============ ③ 归一化：最大 = +1 ============ */
    {
        float v = Remote_ChNorm(SBUS_CH_MAX);
        if (v < 0.99f || v > 1.01f) { return 21; }
    }

    /* ============ ④ 归一化：最小 = -1 ============ */
    {
        float v = Remote_ChNorm(SBUS_CH_MIN);
        if (v < -1.01f || v > -0.99f) { return 22; }
    }

    /* ============ ⑤ 归一化：垃圾数据（2047）应该被夹到 +1 ============ */
    {
        float v = Remote_ChNorm(2047U);
        if (v > 1.01f) { return 23; }
    }

    /* ============ ⑥ 失控保护标志：buf[23] 的 bit3 = 1 ============ */
    Remote_Init();
    BuildFakeFrame(frame, test_ch, 0x08U);      /* 0x08 = bit3 = Failsafe */
    for (i = 0U; i < SBUS_FRAME_LEN; i++) { Remote_FeedByte(frame[i]); }
    if (remote.failsafe != 1U) { return 24; }
    if (Remote_IsOnline() != 0) { return 25; }  /* 失控 → 必须判为不在线 */

    /* ============ ⑦ 帧尾错误 → 整帧应该被丢掉 ============ */
    Remote_Init();
    BuildFakeFrame(frame, test_ch, 0x00U);
    frame[SBUS_FRAME_LEN - 1U] = 0xFFU;         /* 故意把帧尾改错 */
    count_before = remote.rx_count;
    for (i = 0U; i < SBUS_FRAME_LEN; i++) { Remote_FeedByte(frame[i]); }
    if (remote.rx_count != count_before) { return 26; }   /* 不该计入 */

    /* ============ ⑧ 帧头错位后能自动重新同步 ============ */
    Remote_Init();
    Remote_FeedByte(0x11U);                     /* 先喂 3 个垃圾字节 */
    Remote_FeedByte(0x22U);
    Remote_FeedByte(0x33U);
    BuildFakeFrame(frame, test_ch, 0x00U);
    for (i = 0U; i < SBUS_FRAME_LEN; i++) { Remote_FeedByte(frame[i]); }
    if (remote.rx_count != 1U) { return 27; }   /* 应该能重新找到帧头并解析成功 */

    Remote_Init();                              /* 测完清干净 */

    ok = 0;
    (void)ok;
    return 0;       /* 全部通过 */
}
