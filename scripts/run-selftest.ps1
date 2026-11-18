# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 AdonisZeng
#
# 独立自测脚本：编译并运行不依赖串口硬件的单元测试。
# 覆盖：帧时间戳重建、采样周期估计、ChannelStore 时间轴，
# 以及 CSV / Markdown / 精简 Markdown 导出与 CSV 导入往返。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\run-selftest.ps1
#
# 退出码 0 表示全部通过。

[CmdletBinding()]
param(
    [string]$QtPath = 'D:\Software\Qt\6.11.1\msvc2022_64',
    [string]$VcvarsPath = 'D:\Software\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $env:TEMP 'baud-selftest'

if (-not (Test-Path -LiteralPath $QtPath)) {
    throw "找不到 Qt：$QtPath"
}
if (-not (Test-Path -LiteralPath $VcvarsPath)) {
    throw "找不到 vcvars64.bat：$VcvarsPath"
}

if (Test-Path -LiteralPath $buildDir) {
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}
New-Item -ItemType Directory -Path $buildDir | Out-Null

# moc 生成 Q_OBJECT 类的元对象代码
$moc = Join-Path $QtPath 'bin\moc.exe'
foreach ($header in @('src\io\DataExporter.h', 'src\io\CsvImporter.h')) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($header)
    & $moc (Join-Path $repoRoot $header) -o (Join-Path $buildDir "moc_$name.cpp")
    if ($LASTEXITCODE -ne 0) {
        throw "moc 失败：$header"
    }
}

$sources = @(
    'src\serial\TimestamperTest.cpp',
    'src\serial\FrameTimestamper.cpp',
    'src\model\ChannelStore.cpp',
    'src\model\ChannelConfig.cpp',
    'src\protocol\JustFloatParser.cpp',
    'src\protocol\FireWaterParser.cpp',
    'src\io\DataExporter.cpp',
    'src\io\CsvImporter.cpp'
) | ForEach-Object { Join-Path $repoRoot $_ }

$sources += (Join-Path $buildDir 'moc_DataExporter.cpp')
$sources += (Join-Path $buildDir 'moc_CsvImporter.cpp')

$exePath = Join-Path $buildDir 'BaudSelfTest.exe'
$batPath = Join-Path $buildDir 'build.bat'

$sourceList = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
$bat = @"
@echo off
call "$VcvarsPath" >nul 2>&1
cd /d "$repoRoot"
cl /nologo /std:c++17 /Zc:__cplusplus /permissive- /EHsc /utf-8 /MD ^
 /I"$QtPath\include" /I"$QtPath\include\QtCore" /I"$QtPath\include\QtSerialPort" ^
 /Isrc /I. /Isrc\io ^
 $sourceList ^
 /Fe"$exePath" /Fo"$buildDir\\" ^
 /link /LIBPATH:"$QtPath\lib" Qt6Core.lib Qt6SerialPort.lib
"@
$bat | Set-Content -LiteralPath $batPath -Encoding ascii

Write-Host '编译自测 ...'
& cmd.exe /c "`"$batPath`""
if ($LASTEXITCODE -ne 0) {
    throw "自测编译失败（退出码 $LASTEXITCODE）。"
}

Write-Host '运行自测 ...'
$env:PATH = (Join-Path $QtPath 'bin') + ';' + $env:PATH
& $exePath
$code = $LASTEXITCODE

$resultFile = Join-Path $env:TEMP 'baud_timestamper_test.txt'
if (Test-Path -LiteralPath $resultFile) {
    # 自测日志为 UTF-8，需显式指定编码，否则中文在控制台显示为乱码
    Get-Content -LiteralPath $resultFile -Encoding UTF8 | ForEach-Object { Write-Host "  $_" }
    Remove-Item -LiteralPath $resultFile -Force
}

if ($code -ne 0) {
    throw "自测失败（退出码 $code）。"
}
Write-Host '自测全部通过。'
