# ============================================================================
#  check_project.ps1 —— 检查/修复 Keil 工程里"自己写的文件"
# ----------------------------------------------------------------------------
#  为什么需要这个脚本：
#     用 CubeMX 重新生成代码时，它会重写 MDK-ARM\TEST.uvprojx
#     → 手动加进去的 .c 文件条目【可能会丢】
#     → 丢了之后 Keil 编译不会报"找不到文件"，
#       而是直接不编译那些文件 → 链接时报一堆 undefined reference
#
#  用法：
#     .\.vscode\check_project.ps1          # 只检查（安全）
#     .\.vscode\check_project.ps1 -Fix     # 缺了自动补回去（会先备份）
#
#  ⚠️ 本文件必须存成 UTF-8【带 BOM】
# ============================================================================

param(
    [switch]$Fix
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$proj = Join-Path $root 'MDK-ARM\TEST.uvprojx'

if (-not (Test-Path $proj)) {
    Write-Host "[ERROR] 找不到工程文件: $proj" -ForegroundColor Red
    exit 1
}

# ---- 需要出现在工程里的"自己写的"文件（按希望出现的顺序）----
$ourFiles = @(
    @{ n = 'can_motor.c';  p = '../Core/Src/can_motor.c'  },
    @{ n = 'pid.c';        p = '../Core/Src/pid.c'        },
    @{ n = 'remote.c';     p = '../Core/Src/remote.c'     },
    @{ n = 'control.c';    p = '../Core/Src/control.c'    },
    @{ n = 'debug.c';      p = '../Core/Src/debug.c'      },
    @{ n = 'protection.c'; p = '../Core/Src/protection.c' }
)

$text = [System.IO.File]::ReadAllText($proj, [System.Text.Encoding]::UTF8)

# ---- 检查哪些丢了 ----
$missing = @($ourFiles | Where-Object {
    $text -notmatch [regex]::Escape("<FileName>$($_.n)</FileName>")
})

if ($missing.Count -eq 0) {
    Write-Host "[OK] $($ourFiles.Count) 个自写文件都在 Keil 工程里" -ForegroundColor Green
    exit 0
}

Write-Host "[!] Keil 工程里少了 $($missing.Count) 个文件：" -ForegroundColor Yellow
foreach ($f in $missing) {
    Write-Host "      - $($f.n)  ($($f.p))" -ForegroundColor Yellow
}

if (-not $Fix) {
    Write-Host ""
    Write-Host "只检查不修改。要自动补回去就跑：" -ForegroundColor Cyan
    Write-Host "    .\.vscode\check_project.ps1 -Fix" -ForegroundColor Cyan
    exit 1
}

# ---- 自动修复 ----
$backup = "$proj.bak"
Copy-Item $proj $backup -Force
Write-Host "==> 已备份原工程到: $backup" -ForegroundColor Cyan

# 插在 main.c 那条 <File> 前面（保持原来"自写文件在前"的顺序）
$idx = $text.IndexOf('<FileName>main.c</FileName>')
if ($idx -lt 0) {
    Write-Host "[ERROR] 在工程里找不到 main.c，不敢乱改（从 .bak 恢复）" -ForegroundColor Red
    exit 2
}
$fileStart = $text.LastIndexOf('<File>', $idx)

$nl = "`r`n"
$block = ''
foreach ($f in $missing) {
    $block += "            <File>$nl"
    $block += "              <FileName>$($f.n)</FileName>$nl"
    $block += "              <FileType>1</FileType>$nl"
    $block += "              <FilePath>$($f.p)</FilePath>$nl"
    $block += "            </File>$nl"
}

$new = $text.Substring(0, $fileStart) + $block + $text.Substring($fileStart)

# ⚠️ 工程文件是 UTF-8【无 BOM】的，这里也要保持一致
[System.IO.File]::WriteAllText($proj, $new, (New-Object System.Text.UTF8Encoding($false)))

# ---- 复查 ----
$check = [System.IO.File]::ReadAllText($proj, [System.Text.Encoding]::UTF8)
$still = @($ourFiles | Where-Object {
    $check -notmatch [regex]::Escape("<FileName>$($_.n)</FileName>")
})

if ($still.Count -eq 0) {
    Write-Host "[OK] 已补回 $($missing.Count) 个文件，工程修好了" -ForegroundColor Green
    Write-Host "     回 Keil 里【重新打开工程】就能看到它们了" -ForegroundColor Cyan
    exit 0
}
else {
    Write-Host "[FAIL] 还有没补上的（$($still.Count) 个），从 .bak 恢复吧" -ForegroundColor Red
    exit 3
}
