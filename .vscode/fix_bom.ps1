# ============================================================================
#  fix_bom.ps1 —— 给 Core 下的 .c/.h 补 UTF-8 BOM
# ----------------------------------------------------------------------------
#  ⚠️ 为什么需要这个脚本？
#
#     CubeMX 的「GENERATE CODE」会把所有 .c/.h 存成【不带 BOM 的 UTF-8】，
#     而 Keil 编辑器靠 BOM 才能正确识别 UTF-8
#     → 结果：Keil 里中文注释全变成乱码
#
#     而且 CubeMX 还会：
#       · 删掉 USER CODE 区域【之外】的注释
#       · 重写 .uvprojx / .uvoptx（可能丢手动加的文件条目和调试器设置）
#
#  ⭐ 所以 CubeMX 生成完，按顺序做这两件事：
#       ① 跑这个脚本补 BOM
#       ② 跑 check_project.ps1 检查工程文件条目
#
#  用法（VSCode 里 Run Task，或者终端里跑）：
#     .\.vscode\fix_bom.ps1
#
#  ⚠️ 本文件必须存成 UTF-8【带 BOM】
# ============================================================================

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$core = Join-Path $root 'Core'

if (-not (Test-Path $core)) {
    Write-Host "[ERROR] 找不到 Core 目录: $core" -ForegroundColor Red
    exit 1
}

$fixed = @()

Get-ChildItem $core -Recurse -Include *.c, *.h | ForEach-Object {
    $b = [System.IO.File]::ReadAllBytes($_.FullName)
    $hasBom = ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)

    if ($hasBom) {
        return      # 已经有 BOM，跳过
    }

    # ⭐ 只给【含中文】的文件补 —— 纯英文文件（CubeMX 生成的 can.c / gpio.c 那些）
    #    补不补都不影响，但不补能让 git 的 diff 更干净
    $text  = [System.IO.File]::ReadAllText($_.FullName, [System.Text.Encoding]::UTF8)
    $hasCN = $text -match '[\u4e00-\u9fff]'

    if ($hasCN) {
        [System.IO.File]::WriteAllBytes($_.FullName, ([byte[]](0xEF, 0xBB, 0xBF) + $b))
        $fixed += $_.Name
    }
}

if ($fixed.Count -gt 0) {
    Write-Host "[已补 BOM] $($fixed.Count) 个文件：" -ForegroundColor Yellow
    $fixed | Sort-Object | ForEach-Object { Write-Host "    $_" -ForegroundColor Yellow }
    Write-Host ""
    Write-Host "==> 回 Keil 里重新打开这些文件，中文就不会乱码了" -ForegroundColor Cyan
}
else {
    Write-Host "[OK] Core 下的 .c/.h 全部都有 BOM，不用补" -ForegroundColor Green
}

exit 0
