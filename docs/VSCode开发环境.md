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


---

# 八、⚠️ 三个必须知道的环境坑（2026-10-10 踩过）

## ① `compilerPath` 不能留空 —— 不然一片红波浪线

**症状：**

```
打开 control.c，报：无法打开源文件 "stdint.h" (dependency of "control.h")
代码里 CtrlMode_t / CTRL_MODE_STOP / 各种类型名全是红波浪线
问题列表里几十个错
```

**原因：**

```
c_cpp_properties.json 里 "compilerPath": "" 是空的
→ 微软 C/C++ 插件不知道去哪找【标准库头文件】
→ 连 stdint.h 都找不到 → 整个工程什么都解析不了
```

**正确的配置：**

```json
"compilerPath": "C:/Keil_v5/ARM/ARMCLANG/bin/armclang.exe",
"compilerArgs": ["--target=arm-arm-none-eabi", "-mcpu=cortex-m4",
                 "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"],
"includePath": [
    ...（项目自己的 8 条路径）...
    "C:/Keil_v5/ARM/ARMCLANG/include",
    "C:/Keil_v5/ARM/ARMCLANG/lib/clang/20/include"
]
```

**⭐ 怎么确定用哪个 clang 版本目录？** 让 armclang 自己打印搜索路径：

```powershell
armclang -c --target=arm-arm-none-eabi -mcpu=cortex-m4 t.c -o t.o -v
# 看输出里的 -resource-dir
# 我们这台机器是 C:\Keil_v5\ARM\ARMCLANG\lib\clang\20
```

## ② clangd 和 C/C++ 插件会打架

**症状：**

```
弹提示：You have both the Microsoft C++ (cpptools) extension and
       clangd extension enabled. The Microsoft IntelliSense features
       conflict with clangd's code completion, diagnostics etc.
```

**怎么办：**

```
❌ 不要点 "Disable IntelliSense"
   → 那会废掉我们辛苦配的 c_cpp_properties.json

✅ 去扩展面板禁用 clangd：
     Ctrl+Shift+X → 搜 clangd → 齿轮 ⚙ → 禁用
     → 然后 Ctrl+Shift+P → "Reload Window"
```

**为什么禁用 clangd 而不是禁用微软 IntelliSense：**

```
c_cpp_properties.json 是给【微软 C/C++ 插件】用的
clangd 不读这个文件 —— 它要 compile_commands.json
Keil 工程生成那个清单很麻烦
→ 所以用微软插件更省事 ✓
```

**⭐ 顺便：** 如果弹"是否切换到 C/C++ 预发行版" → 选【否】，用稳定版。

## ③ 改完配置要重载窗口

```
Ctrl+Shift+P → 输入 "Reload Window" → 回车

还不行：
Ctrl+Shift+P → "C/C++: Select IntelliSense Configuration"
   → 选 "STM32F427 (Keil AC6)"
Ctrl+Shift+P → "C/C++: Reset IntelliSense Database"
```

---

# 九、⭐ 改文件后要补 UTF-8 BOM

**`.c` / `.h`（Keil 要）和 `.ps1`（Windows PowerShell 5.1 要）里的中文，都靠 BOM 才能正确显示。**

```
如果被某些编辑器保存成【无 BOM 的 UTF-8】：
   · Keil 里 → 中文注释变乱码
   · PowerShell 里 → 中文乱码，甚至报语法错误
```

**补 BOM 的方法（PowerShell 里跑）：**

```powershell
$p = 'C:\RM\teach\TEST\.vscode\build.ps1'
$b = [System.IO.File]::ReadAllBytes($p)
if (-not ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)) {
    [System.IO.File]::WriteAllBytes($p, ([byte[]](0xEF,0xBB,0xBF) + $b))
}
```

**⭐ 一次补一批：**

```powershell
Get-ChildItem 'C:\RM\teach\TEST\Core' -Recurse -Include *.c,*.h | ForEach-Object {
    $b = [System.IO.File]::ReadAllBytes($_.FullName)
    if (-not ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)) {
        [System.IO.File]::WriteAllBytes($_.FullName, ([byte[]](0xEF,0xBB,0xBF) + $b))
        Write-Host "补了 BOM: $($_.Name)"
    }
}
```

---

# 十、⚠️⚠️ CubeMX 的 GENERATE CODE 会干这些坏事

**2026-10-10 踩过的坑，务必记住。**

## 它改了什么

```
❌ 去掉所有 .c/.h 的 UTF-8 BOM
   → Keil 里中文注释全部变成乱码

❌ 删掉 USER CODE 区域【之外】的注释
   → 我写在 stm32f4xx_it.c 里的大段说明注释被删了

❌ 重写 .uvprojx / .uvoptx
   → 手动加进工程的文件条目可能丢
   → Keil 的调试器设置（J-Link、SWD、Reset and Run）可能被重置

✅ 但保护了 USER CODE 区域
   → 写在 /* USER CODE BEGIN ... */ 和 /* USER CODE END ... */ 之间的代码不会丢
```

## ⭐ 生成完之后按顺序做这两件事

```
① VSCode → Terminal → Run Task → 【⑥ 补 BOM】
      给它去掉的 BOM 补回来
      
② VSCode → Terminal → Run Task → 【⑦ 检查工程文件】
      检查那 6 个自写 .c 文件还在不在工程里，缺了自动补
      
③ 回 Keil 检查这几项有没有被重置：
      □ Debug → Settings → Port 是不是还是 SWD
      □ Utilities → Settings → Flash Download → ☑ Reset and Run
      □ C/C++ → Optimization 是不是还是 -O0
      
④ 编译验证：Ctrl+Shift+B → 0 Error 0 Warning
```

## ⭐ 更省事的做法：用 git 看 CubeMX 到底改了什么

```
VSCode → 左侧【源代码管理】图标（Ctrl+Shift+G）
   → 会列出被改的文件
   → 点每个文件看红绿对比
   
⭐ 一眼就能看出 CubeMX 动了什么
```

## ⭐ 如果 CubeMX 这次【没有产生有用的新东西】

```
直接全部恢复：
   git checkout -- .

（这次就是这种情况 —— CubeMX 只是把文件重写了一遍，
  丢了 BOM 和注释，什么都没带来）
```

## ⭐ 判断标准

```
CubeMX 生成的结果【有价值】 → 你确实改了引脚/外设/时钟配置
   → 跑任务⑥⑦，然后回 Keil 补设置

CubeMX 生成的结果【没价值】 → 你只是打开看看、点错了 GENERATE
   → git checkout -- .  全部恢复
```