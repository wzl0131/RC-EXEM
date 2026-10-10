# ============================================================================
#  build.ps1 —— 在 VSCode 里调用 Keil 的 UV4 命令行来编译 / 烧录
# ----------------------------------------------------------------------------
#  用法（VSCode 里按 Ctrl+Shift+B，或者在终端里跑）：
#     .\.vscode\build.ps1                 增量编译
#     .\.vscode\build.ps1 -Mode rebuild   全部重新编译
#     .\.vscode\build.ps1 -Mode flash     编译 + 下载到板子
#
#  ⚠️ 这个文件必须存成 UTF-8【带 BOM】，否则 Windows PowerShell 5.1
#     会按 GBK 解析，中文变乱码、甚至报语法错误。
# ============================================================================

param(
    [ValidateSet('build', 'rebuild', 'flash')]
    [string]$Mode = 'build'
)

$ErrorActionPreference = 'Continue'

# ---- 路径 ----
$root = Split-Path -Parent $PSScriptRoot                  # 工程根目录
$proj = Join-Path $root 'MDK-ARM\TEST.uvprojx'
$log  = Join-Path $root 'MDK-ARM\build.log'
$uv4  = 'C:\Keil_v5\UV4\UV4.exe'

if (-not (Test-Path $uv4)) {
    Write-Host "[ERROR] 找不到 Keil: $uv4" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $proj)) {
    Write-Host "[ERROR] 找不到工程文件: $proj" -ForegroundColor Red
    exit 1
}

# ---- UV4 的参数 ----
$uv4Arg = switch ($Mode) {
    'build'   { '-b' }    # 增量编译
    'rebuild' { '-r' }    # 全部重新编译
    'flash'   { '-f' }    # 下载到 Flash
}

Remove-Item $log -ErrorAction SilentlyContinue

Write-Host "==> UV4 $uv4Arg  ($Mode)" -ForegroundColor Cyan
& $uv4 $uv4Arg $proj -j0 -o $log | Out-Null
$code = $LASTEXITCODE

# ---- 读日志 ----
# ⚠️ UV4 写的日志是 GBK(936) 编码，必须指定编码读，否则中文提示全是乱码
if (Test-Path $log) {
    $text = [System.IO.File]::ReadAllText($log, [System.Text.Encoding]::GetEncoding(936))
    $text -split "`r?`n" |
        Select-String 'error|warning|Program Size|Error\(s\)|Warning\(s\)' |
        ForEach-Object { Write-Host ("  " + $_.Line.Trim()) }
}

# ---- UV4 退出码：0 = 无错误无警告；1 = 有警告；2 及以上 = 有错误 ----
if ($code -eq 0) {
    Write-Host "==> [OK] 编译成功：0 Error, 0 Warning" -ForegroundColor Green
}
elseif ($code -eq 1) {
    Write-Host "==> [WARN] 编译通过，但有警告 —— 看上面的 warning 行" -ForegroundColor Yellow
}
else {
    Write-Host "==> [FAIL] 编译失败（UV4 退出码 $code）" -ForegroundColor Red
}

exit $code
