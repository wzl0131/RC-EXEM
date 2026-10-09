# RC-EXEM · 电控考核工程

基于 **STM32F427IIH6（大疆 A 板）** 的电控考核工程。
用遥控器（SBUS）控制 **M3508 电机**，位置控制采用**角度环 + 速度环串级 PID**。

- 编译状态：**0 Error(s), 0 Warning(s)**
- 框架：STM32CubeMX + HAL + FreeRTOS
- 调试：Keil MDK 5（AC6 编译器）+ J-Scope

---

## 一、功能

用左侧 SWA5 三档拨杆切换：

| 拨杆位置 | 模式 | 操作 | 说明 |
|---|---|---|---|
| **最上** | 位置控制 | 左摇杆 | 推到底 = ±90°，中间线性对应。用角度环 + 速度环**串级** PID |
| **中间** | 停机 | — | 电流归零 |
| **最下** | 速度控制 | 右摇杆 | 推到底 = 额定转速的 1/2（215 rpm），回中停止。用**速度闭环** PID |

J-Scope 实时显示 5 条曲线：目标角度、实际角度、目标转速、实际转速、PID 输出。

---

## 二、硬件

| 部件 | 型号 / 说明 |
|---|---|
| 主控 | 大疆开发板 A 型，STM32F427IIH6（180 MHz） |
| 电机 | 大疆 M3508 直流无刷减速电机 |
| 电调 | 大疆 C620（CAN 通信，1 Mbps） |
| 遥控 | HOTRC 遥控器 + SBUS 接收机（接 A 板遥控口） |
| 调试器 | J-Link V9（J-Scope 必须用 J-Link，ST-Link 不行） |

### 接线

| 从 | 到 | A 板接口 | 线 |
|---|---|---|---|
| 电池 24V | 分电板 | — | XT60 |
| 分电板 | A 板供电 | **接口 21** `S8B-PH-SM4-TB`（8pin，CAN1 输入 + 24V 输入） | 或用背面备用 24V 焊盘 |
| 分电板 | C620 供电 | — | C620 电源线兼容 XT30 |
| A 板 CAN1 | C620 CAN 口 | **接口 2** `BM02B-GHS-TBT`（2pin GH1.25） | 自制转接线 |
| 接收机 | A 板遥控口 | **接口 9** DBUS（板子右上角） | 配套线 |
| C620 | M3508 | — | 7-Pin 数据线 + 三相动力线 |
| J-Link | A 板 SWD | **接口 14** `Molex-53261-0471` | 3.3V / GND / SWDIO / SWCLK |

**注意事项：**

- ⚠️ C620 必须**先接 24V** 才能工作，禁止只从 CAN 口供电
- A 板遥控口**内置反相器**（手册原文："DBUS 是 UART 信号的反相形式，会由一个反相器反相之后再进入 MCU"，反相后接 **PB7** = USART1_RX）
- CAN 总线两端各需要一个 **120Ω 终端电阻**

---

## 三、目录结构

```
RC-EXEM/
├─ Core/
│  ├─ Inc/    can_motor.h  remote.h  pid.h  control.h  protection.h  debug.h
│  │          main.h  gpio.h  can.h  usart.h  FreeRTOSConfig.h  ...
│  └─ Src/    main.c       启动 + 7 个上电自测 + 初始化
│             freertos.c   4 个任务
│             can_motor.c  C620 的 CAN 收发 + 多圈角度累计
│             remote.c     SBUS 25 字节解析成 16 个通道
│             pid.c        PID 算法
│             control.c    判档位 + 摇杆映射 + 串级 PID 调度   ← 核心
│             protection.c 保护（4 条判断）
│             debug.c      给 J-Scope 的 5 个变量
├─ Drivers/      STM32 HAL 库 + CMSIS（CubeMX 生成）
├─ Middlewares/  FreeRTOS（CubeMX 生成）
├─ MDK-ARM/      Keil 工程 TEST.uvprojx
├─ docs/         说明文档
└─ TEST.ioc      CubeMX 配置文件
```

---

## 四、怎么编译和下载

```
① 用 Keil MDK 打开：MDK-ARM\TEST.uvprojx
② 编译：F7
③ 下载：F8（需要先接好 J-Link 或 ST-Link）
④ 复位板子 → 绿灯每 500 ms 闪一次 = 系统正常运行
```

**⭐ 注意：源文件是 UTF-8（带 BOM）编码。Keil 保存文件时会去掉 BOM，导致中文注释变乱码。**

**如果想改硬件配置（引脚、时钟、外设），用 STM32CubeMX 打开 `TEST.ioc`。**
⚠️ 但重新生成代码会重写 `.uvprojx`，手动加进去的 `.c` 文件条目可能会丢，生成后要检查一遍。

---

## 五、核心设计摘要

### 任务划分（FreeRTOS）

| 任务 | 周期 | 优先级 | 职责 |
|---|---|---|---|
| `controlTask` | 1 ms | 40（High） | 保护 + 双环 PID + 发 CAN |
| `remoteTask` | 10 ms | 32（AboveNormal） | 读遥控 + 判档位 + 算目标 |
| `debugTask` | 500 ms | 8（Low） | 绿灯心跳 |
| `defaultTask` | 1 ms | 24（Normal） | 空任务（CubeMX 模板自带） |

对应考核建议的"一个任务接收遥控器指令，一个任务输出控制电机"。

### 数据流

```
遥控器 → 接收机 → USART1中断 → remote.ch[16]
                                    ↓
                        remoteTask 算目标（s_target_deg / s_stick_rpm）
                                    ↓
              controlTask：保护 → 位置环 → 速度环 → 电流
                                    ↓
                          CAN 控制帧（0x200）→ C620 → M3508
                                    ↓
                     反馈帧（0x201，1 kHz）→ CAN 中断 → motor1
                                    ↓
                       （回到 controlTask，形成闭环）
```

### 串级 PID

```
位置环：输入（目标角度, 实际角度）→ 输出（目标转速）
速度环：输入（目标转速, 实际转速）→ 输出（电流）
```

**位置环的输出直接当速度环的目标** —— 这就是串级。

### 保护

只做四条"真出事"的判断，命中就断电流：

| 条件 | 阈值 |
|---|---|
| 遥控掉线 | 50 ms 没收到 SBUS 帧 |
| 电调掉线 | 100 ms 没收到 CAN 反馈 |
| 电调报错误码 | 手册第 8 字节 ≠ 0 |
| 电机过温 | ≥ 115 ℃ |

**故意不做**降额、堵转检测、角度软限位、故障锁存 —— 这几条会和"保持 ±90° 不抖动"冲突。

---

## 六、📖 完整说明文档

详细的**链路 / 函数 / 调用逻辑**在这里：

### 👉 [docs/说明文档.md](docs/说明文档.md)

内容包括：

1. **链路** —— 四条完整链路（遥控 → 目标值 / 目标值 → 电机 / 电机 → 反馈 / 数据 → J-Scope）
2. **函数** —— 47 个函数，每个都标明"谁调用它"
3. **调用的逻辑** —— 分层原则、任务分工、模块依赖、数据传递、钩子函数、启动顺序

---

## 七、要调的参数在哪

| 参数 | 文件 | 当前值 |
|---|---|---|
| 电调 ID | `can_motor.c` `C620_ParseRx()` | 1（只认 0x201） |
| SWA 三个档位的阈值 | `control.h` | 1500 / 500 |
| 哪个通道是哪个摇杆 | `control.h` | 位置 = CH3，速度 = CH2 |
| 最大角度 | `control.h` | 90° |
| 速度上限 | `control.h` | 215 rpm |
| 斜坡限速 | `control.h` | 角度 360°/s，转速 600 rpm/s，电流 1000000/s |
| 摇杆死区 | `control.h` | 0.03 |
| 两路 PID | `pid.h` | `PID_SPEED_*` / `PID_ANGLE_*` |
| 过温阈值 | `protection.h` | 115 ℃ |
| 减速比 / 编码器分辨率 | `can_motor.h` | 19.2032 / 8192 |

---

## 八、还没做的

- 电调 ID 目前写死只认 1 号（0x201）
- SWA 三个档位的阈值、通道映射、PID 参数目前是估值，需要上机实测确认
- 速度上限 215 rpm 是按手册性能曲线上那条转速曲线（约 435 rpm）取一半来的，也需要核对
