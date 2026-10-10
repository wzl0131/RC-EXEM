# VSCode 开发环境说明

> 这套配置让你**在 VSCode 里写代码**（比 Keil 的编辑器好用很多），
> 但**编译和下载还是用 Keil 的编译器**（保证和 Keil 里编译的结果完全一致）。

---

# 一、第一次使用：装 3 个插件

打开 VSCode → 左边点扩展图标（或按 `Ctrl+Shift+X`）→ 搜下面这几个装上：

| 插件 | ID | 作用 |
|---|---|---|
| **C/C++** | `ms-vscode.cpptools` | ⭐ 必需。代码补全、函数跳转、错误提示 |
| **Cortex-Debug** | `marus25.cortex-debug` | 可选。在 VSCode 里调试（代替 Keil 的调试器）|
| **Chinese (Simplified)** | `ms-ceintl.vscode-language-pack-zh-hans` | 可选。中文界面 |

**⭐ 最简单的方式：用 VSCode 打开 `C:\RM\teach\TEST` 文件夹，右下角会自动弹出"是否安装推荐扩展" → 点安装。**

---

# 二、怎么打开工程

```
VSCode → File → Open Folder → 选 C:\RM\teach\TEST
   ⚠️ 选到 TEST 这一层，不要再往里点
```

**打开后左边应该能看到：**
```
TEST/
├─ .vscode/          ← 我配好的环境（c_cpp_properties / tasks / launch）
├─ Core/             ← 你写代码的地方
├─ Drivers/
├─ Middlewares/
├─ MDK-ARM/
├─ docs/
├─ README.md
└─ TEST.ioc
```

---

# 三、怎么编译和下载

## 方式 1：快捷键（推荐）

```
Ctrl + Shift + B      →  编译（默认任务）
```

## 方式 2：菜单

```
Terminal → Run Task...  →  选一个：
   ① 编译 (Build)              —— 增量编译（常用）
   ② 全部重新编译 (Rebuild)     —— 改了头文件/配置后用
   ③ 编译并下载到板子 (Flash)   —— 编译 + 烧录一次搞定
   ④ 打开 Keil                —— 要改工程配置时用
   ⑤ 打开 CubeMX              —— 要改引脚/外设配置时用
```

## 输出长这样

```
==> UV4 -b  (build)
  Program Size: Code=24294 RO-data=714 RW-data=16 ZI-data=20856
  "TEST\TEST.axf" - 0 Error(s), 0 Warning(s).
==> [OK] 编译成功：0 Error, 0 Warning
```

---

# 四、⚠️ 4 个必须知道的点

## ① 保存成 UTF-8 带 BOM（已经配好了，别改）

```
.vscode/settings.json 里设了  "files.encoding": "utf8bom"

⭐ 为什么：Keil 编辑器靠 BOM 识别 UTF-8
   → 没有 BOM 时 Keil 按 GBK 解析 → 中文注释全变乱码

⚠️ 如果你手动改过某个文件的编码（右下角状态栏能看到），
   记得改回 UTF-8 with BOM
```

## ② VSCode 调试 和 Keil 调试【不能同时用】

```
两个都要独占 J-Link
→ 用 VSCode 调试前，先在 Keil 里 Stop Debugging
→ 用 Keil 调试前，先在 VSCode 里停止调试
```

## ③ J-Scope 和它们也抢 J-Link

```
调参数时：  Keil 或 VSCode 调试
录曲线时：  Keil / VSCode 都停掉 → 开 J-Scope
```

## ④ ⭐ 用了 CubeMX 重新生成代码之后 —— 跑一下任务⑥

```
用 CubeMX 重新生成代码 → 会重写 MDK-ARM\TEST.uvprojx
→ ⚠️ 手动加进工程的 6 个 .c 文件条目【可能会丢】
   （我们的 6 个文件排在 main.c 之前，说明是手动插进去的）

丢了之后的症状很坑：
   Keil 不会报"找不到文件"，而是【直接不编译那些文件】
   → 链接时冒出一堆 undefined reference to 'C620_ParseRx' 之类的错
   → 你会以为是代码写错了

⭐ 所以：用完 CubeMX → 立刻跑一下 VSCode 任务⑥
   Terminal → Run Task... → 【⑥ 检查工程文件（CubeMX 重新生成后跑）】

   它会：
     · 检查 6 个文件还在不在
     · 缺了【自动补回去】（改之前先备份成 TEST.uvprojx.bak）
     · 补完回 Keil 重新打开工程就能看到
```

**⭐ 也可以用命令行跑：**

```powershell
.\.vscode\check_project.ps1          # 只检查，不改（安全）
.\.vscode\check_project.ps1 -Fix     # 缺了自动补回去（会先备份）
```

**要检查的 6 个文件：**
```
can_motor.c   pid.c   remote.c   control.c   safety.c   debug.c
```

**⭐ 另外：改了 `TEST.ioc` 之后，还要注意这几项会不会被重置：**
```
□ Debug 选项卡里 Port 是不是还是 SWD
□ Utilities → Settings → Flash Download 里 Reset and Run 还在不在
□ IncludePath 和 Define（USE_HAL_DRIVER, STM32F427xx）
□ C/C++ → Optimization 是不是还是 -O0
```

---

# 五、VSCode 里常用的快捷键

| 快捷键 | 作用 |
|---|---|
| `Ctrl+Shift+B` | 编译 |
| `F12` | 跳到函数定义 |
| `Alt+←` | 跳回来 |
| `Ctrl+Shift+F` | 在整个工程里搜索（比 Keil 好用太多）|
| `Ctrl+P` | 快速打开文件（打文件名几个字母就能跳）|
| `Ctrl+/` | 注释/取消注释 |
| `Ctrl+D` | 选中下一个相同的词（批量改名很有用）|
| `Alt+Shift+↑/↓` | 复制当前行 |
| `Ctrl+G` | 跳到第几行 |

---

# 六、文件说明

| 文件 | 作用 |
|---|---|
| `.vscode/c_cpp_properties.json` | IntelliSense 配置（头文件路径 + 宏定义）|
| `.vscode/settings.json` | ⭐ 编码设成 UTF-8 BOM、隐藏编译产物、文件关联 |
| `.vscode/tasks.json` | 编译/烧录任务（调用 Keil 的 UV4）|
| `.vscode/launch.json` | 调试配置（需要 Cortex-Debug 插件）|
| `.vscode/build.ps1` | 实际干活的脚本（调 UV4 + 解析日志）|
| `.vscode/extensions.json` | 推荐的插件列表 |

**⭐ 这套配置已经提交到 git 了 —— 换台电脑 clone 下来就能直接用。**

---

# 七、如果 IntelliSense 报错（红波浪线）

```
① 确认 .vscode/c_cpp_properties.json 里的路径对
② 按 Ctrl+Shift+P → 输入 "C/C++: Select IntelliSense Configuration"
   → 选 "STM32F427 (Keil AC6)"
③ 还不行 → Ctrl+Shift+P → "C/C++: Reset IntelliSense Database"
④ 重启 VSCode
```

**⭐ 注意：IntelliSense 报的红波浪线不一定真的错 —— 以 Keil 的编译结果为准。**
**（比如它可能不认识 ARM 的内联汇编、`__weak` 这些 GCC/Keil 扩展）**
