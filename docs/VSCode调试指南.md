# VSCode 调试指南（对标 Keil 的各个窗口）

> 前提：装了 **Cortex-Debug** 插件（`marus25.cortex-debug`），并且 `.vscode/STM32F427.svd` 在

---

# 一、怎么开始调试

```
① 接好 J-Link（SWD），板子通 24V
② ⚠️ 确保 Keil 和 J-Scope 都停掉了（它们也要独占 J-Link）
③ 按 F5     （或者左边点那个"虫子+三角"图标 → 选 "J-Link 调试（先编译）"）
④ 它会先自动编译，然后下载，然后停在 main 函数
```

**调试工具栏（F5 开始后就出现在顶部）：**

| 按钮 | 快捷键 | 作用 |
|---|---|---|
| ▶ 继续 | `F5` | 继续跑 |
| ⏸ 暂停 | `F6` | 暂停 |
| ↻ 单步跳过 | `F10` | 执行一行（不进入函数）|
| ↓ 单步进入 | `F11` | 进入函数内部 |
| ↑ 单步跳出 | `Shift+F11` | 跳出当前函数 |
| ⟳ 重启 | `Ctrl+Shift+F5` | 重新开始调试 |
| ⏹ 停止 | `Shift+F5` | 结束调试（让出 J-Link）|

**⭐ 和 Keil 的对照：**
```
Keil 的 F5（运行）   →  VSCode 的 F5
Keil 的 F10（单步）  →  VSCode 的 F10
Keil 的 F11（步入）  →  VSCode 的 F11
Keil 的 F9（断点）   →  在行号左边点一下（或按 F9）
```

---

# 二、左侧面板（调试时自动出现）

## 1. VARIABLES —— 变量

```
分三组：
   Locals   —— 当前函数的局部变量
   Globals  —— 全局变量（⭐ 我们的 motor1 / remote / pid_speed 都在这）
   Static   —— 文件级 static 变量（⭐ s_mode / s_target_deg 在这！）
```

**⭐ 这就是 Keil 的 Watch 窗口的核心功能，而且比 Keil 强：**
```
· 结构体能无限展开（motor1 → raw_angle / out_rpm / ...）
· 数组能看全部元素
· 鼠标悬停在代码里的变量上就能看到值（不用加到窗口里！）
```

## 2. WATCH —— 监视窗口（⭐ 对标 Keil 的 Watch 1）

```
点那个 + 号 → 输入表达式 → 回车

可以输入：
   motor1.out_angle_deg
   remote.ch[4]
   pid_speed.kp
   motor1.raw_temp > 100          ← 还能写条件表达式，显示 true/false
   dbg_target_deg - dbg_actual_deg  ← ⭐ 还能算差值！Keil 做不到
```

**⭐⭐ 这是 VSCode 比 Keil 强的地方：Watch 里可以写**任意表达式**（算差值、比较、位运算）。**

## 3. CALL STACK —— 调用栈

```
显示"当前是怎么调用到这里来的"
   main() → StartControlTask() → Control_Update() → Pid_Calc()

⭐ 点栈里的任意一层，就能跳到那个函数的现场、看当时的局部变量
⭐ 卡死的时候（比如 HardFault 的 while(1)）看这里能定位是哪崩的
```

## 4. BREAKPOINTS —— 断点列表

```
所有断点都在这（可以勾选临时禁用）
⭐ 右键断点 → Edit Breakpoint → 可以写【条件】
   例：motor1.raw_temp > 100        温度超过 100 才停
       remote.ch[4] > 1500          拨杆在上档才停
```

---

# 三、⭐ 三个面板到底在哪（查过插件源码，确定的）

| 面板 | 谁提供的扩展 | 位置 |
|---|---|---|
| **Cortex Live Watch** | `marus25.cortex-debug` | ⭐ **左侧栏**（RUN AND DEBUG 视图里）|
| **XPeripherals** | `mcu-debug.peripheral-viewer` | **底部面板**（和 TERMINAL 一排）|
| **RTOS Views** | `mcu-debug.rtos-views` | **底部面板** |

**怎么快速找到某个面板：**
```
左侧栏     → Ctrl + Shift + D   （RUN AND DEBUG）
底部面板   → Ctrl + J           （切换面板显示/隐藏）
```

---

## 1. ⭐⭐ CORTEX LIVE WATCH（左侧栏）—— 实时监视，不停止程序

> ⚠️ **在【左侧栏】，不是底部！**
> （插件源码里挂在 `"debug"` 容器下，条件 `debugType == cortex-debug`）

```
① 先按 F5 开始调试（不调试它不出现）
② Ctrl+Shift+D 打开 RUN AND DEBUG
③ 往下滚 —— 在 VARIABLES / WATCH / CALL STACK / BREAKPOINTS
   【下面】就是 CORTEX LIVE WATCH
```

**怎么加变量：**
```
方法 1：那个折叠区右上角点 【+】→ 输入表达式 → 回车
方法 2：⭐ 更方便 —— 在 VARIABLES 里右键变量 → 【Add to Live Watch】
```

**和 WATCH 的区别：**

| | 刷新时机 |
|---|---|
| **WATCH** | 只在程序**暂停**时刷新 |
| **CORTEX LIVE WATCH** | ⭐ **程序跑着也能刷新** |

**⭐ 相当于"简化版的 J-Scope"，但画不了曲线（画曲线还是用 J-Scope）。**

---

## 2. XPERIPHERALS（底部面板）—— 看外设寄存器（⭐ 需要 SVD 文件）

> ⚠️ 这个面板是**另一个扩展** `mcu-debug.peripheral-viewer` 提供的，
> 不是 Cortex-Debug 自带的（如果没装就看不到）

```
① 调试状态下 → 看窗口底部那一排标签
     TERMINAL | OUTPUT | PROBLEMS | ... | XPERIPHERALS
② 点 【XPERIPHERALS】
③ 左边选外设（GPIOF / USART1 / CAN1 / TIM1 / RCC ...）
   右边显示该外设所有寄存器，按【位】展开
```

**看到的效果：**
```
GPIOF
  MODER
    MODER14   [01]    ← PF14 是输出模式 ✓
  ODR
    ODR14     [0]     ← 低电平（绿灯亮）
  IDR
    IDR14     [1]     ← 读到的实际电平
```

**⚠️ 如果底部没有这个标签：**
```
□ 确认在调试状态（状态栏是橙色的）
□ 确认 .vscode/STM32F427.svd 在
□ 确认 launch.json 里有 "svdFile" 这一行
□ 确认装了 mcu-debug.peripheral-viewer
```

---

## 3. ⭐ RTOS Views（底部面板）—— 看 FreeRTOS 的任务！

**⭐ 这个对你特别有用 —— 能直接看到每个任务的栈用了多少：**

```
底部面板 → 【RTOS Views】

   控制任务 controlTask   Running   优先级 40   栈 320/1024 字节
   遥控任务 remoteTask    Blocked   优先级 32   栈 256/1024 字节
   心跳任务 debugTask     Blocked   优先级 8    栈 180/1024 字节
```

**⭐ 用它来验证栈够不够：**
```
栈用到 80% 以上 → 该把 stack_size 调大了 ⚠️
栈用得很低      → 说明当前的栈大小是够的 ✓
```

**面试时如果被问"你怎么确定任务栈够用"，答：**
```
"我上机时用 VSCode 的 RTOS Views 看了每个任务的实际栈使用量，
 最高的大概用了三分之一，所以 stack_size 定成 1024 是够的。"
```

---

## 4. Memory / Disassembly

```
Memory      —— 看任意地址的内存（对标 Keil 的 Memory 窗口）
               右键变量 → View Memory
Disassembly —— 反汇编（右键代码 → Open Disassembly View）
               看 HardFault 卡在哪、看编译出来的汇编很有用
```

---

# 四、和 Keil 的完整对照表

| Keil 的功能 | VSCode 里的位置 | 哪个更好 |
|---|---|---|
| Watch 1/2 窗口 | 左侧 **WATCH** 面板 | ⭐ VSCode（能写表达式）|
| 变量查看 | 左侧 **VARIABLES**（Locals/Globals/Static）| ⭐ VSCode |
| 鼠标悬停看值 | 悬停即可 | 一样 |
| Call Stack + Locals | 左侧 **CALL STACK** | ⭐ VSCode |
| Registers | 底部 **CORTEX REGISTERS** | ⭐ VSCode |
| **Peripherals（外设寄存器）** | 底部 **XPERIPHERALS** | ⭐ VSCode（需 SVD）|
| Memory 窗口 | 底部 **Memory** | 一样 |
| 反汇编 | 右键 → Open Disassembly View | 一样 |
| 断点 / 条件断点 | 左侧 **BREAKPOINTS** | ⭐ VSCode（写条件更自由）|
| **实时看变量（不停止）** | 底部 **CORTEX LIVE WATCH** | ⭐⭐ VSCode 独有 |
| 实时画曲线 | ❌ 没有 | → J-Scope |
| 串口打印调试 | 需要 RTT 库 | Keil 也要额外配 |

---

# 五、⭐ 实战：用 XPERIPHERALS 验证引脚

**这是最能体现 VSCode 优势的场景：**

```
例 1：确认 PF14（绿灯）配对了
   ① 暂停程序
   ② 底部 XPERIPHERALS → GPIOF
   ③ 看 MODER → MODER14 应该是 01（通用输出模式）
   ④ 看 ODR → ODR14 的值（0 = 低电平 = 灯亮）
   ⑤ 按 F5 跑一下再暂停，ODR14 应该翻转了

例 2：看 CAN1 收到数据没有
   XPERIPHERALS → CAN1
      RF0R → FMP0  ← 这个数字就是"FIFO0 里还有几帧没读"
      ⭐ FMP0 一直是 0 → 说明中断读得很快（正常）
      ⭐ FMP0 一直是 3 → 说明中断没在跑（有问题）

例 3：看 USART1 有没有收到字节
   XPERIPHERALS → USART1
      SR → RXNE   ← 收到一个字节时这位会置 1
      DR          ← 收到的数据

例 4：看时钟配对了没有
   XPERIPHERALS → RCC
      PLLCFGR     ← PLL 的分频系数
      CFGR        ← 系统时钟源、各总线的分频
      ⭐ 配合 CORTEX REGISTERS 里的值能验证 180MHz
```

---

# 六、⚠️ 4 个注意点

```
① VSCode 调试 / Keil 调试 / J-Scope 三者不能同时用
   （都要独占 J-Link）→ 用之前先把别的停掉

② 变量显示 <optimized out> 或者值不对
   → 编译优化把变量优化掉了
   → 去 Keil 里把 Optimization 设成 Level 0 (-O0)

③ 连不上目标（"could not connect to target"）
   → launch.json 里把 "showDevDebugOutput": "none" 改成 "full"
   → 然后看 OUTPUT 面板里的 J-Link 日志
   → 常见原因：Keil 还占着 J-Link、SWD 线没接好、板子没上电

④ XPERIPHERALS 面板是空的
   → SVD 文件路径错了，检查 launch.json 里的 "svdFile"
   → 应该是 ${workspaceFolder}/.vscode/STM32F427.svd
```

---

# 七、⭐ 加断点的小技巧

**Logpoints（日志断点）—— 不停止程序，只在 OUTPUT 里打印：**

```
右键行号左边 → Add Logpoint
→ 输入比如：角度 = {dbg_actual_deg}, 目标 = {dbg_target_deg}
→ 程序不停，但 OUTPUT 面板里会不断打印

⭐ 相当于"不用串口就能 printf"，而且不打断实时控制！
```

**条件断点：**

```
右键断点 → Edit Breakpoint → Expression
   输入 motor1.raw_temp > 100
   → 只有温度超过 100 才停下

⭐ 调 PID 的时候特别有用：
   dbg_actual_deg - dbg_target_deg > 5    ← 超调超过 5° 才停
```

**数据断点（看谁改了这个变量）：**

```
左侧 BREAKPOINTS 面板 → + 号 → Data Breakpoint
→ 输入 &motor1.inited
→ 谁写了这个变量就停下来

⭐ 查"变量莫名被改"的 bug 神器
```
