# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 AdonisZeng
#
# 本地发布打包脚本：编译 Release、暂存运行文件、生成便携 zip、
# 调用 Inno Setup 生成安装包，并写入 SHA256SUMS.txt。
# 脚本不会执行 git commit/tag/push，也不会调用 gh release。

[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$PortableOnly,
    [string]$MSBuildPath = 'D:\Software\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe',
    [string]$IsccPath = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$versionFile = Join-Path $repoRoot 'src\Version.h'
$releaseDir = Join-Path $repoRoot 'x64\Release'
$distDir = Join-Path $repoRoot 'dist'

function Resolve-Iscc {
    param([string]$Explicit)

    if ($Explicit) {
        if (-not (Test-Path -LiteralPath $Explicit)) {
            throw "指定的 ISCC.exe 不存在：$Explicit"
        }
        return (Resolve-Path -LiteralPath $Explicit).Path
    }

    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
        'C:\Program Files\Inno Setup 6\ISCC.exe'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $command = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw '未找到 ISCC.exe。请安装 Inno Setup 6，或通过 -IsccPath 指定路径。'
}

if (-not (Test-Path -LiteralPath $versionFile)) {
    throw "找不到版本文件：$versionFile"
}

$versionText = Get-Content -LiteralPath $versionFile -Raw
$versionMatch = [regex]::Match(
    $versionText,
    '#define\s+BAUD_VERSION_STRING\s+"([0-9]+\.[0-9]+\.[0-9]+)"')
if (-not $versionMatch.Success) {
    throw 'src\Version.h 中没有找到形如 "x.y.z" 的 BAUD_VERSION_STRING。'
}
$version = $versionMatch.Groups[1].Value

if (Get-Process -Name Baud -ErrorAction SilentlyContinue) {
    throw 'Baud 正在运行，请先关闭后再打包。'
}

$dirty = & git -C $repoRoot status --porcelain 2>$null
if ($dirty) {
    Write-Warning '工作区存在未提交改动；发布前应先提交，保证 Release 对应仓库中的源码。'
}

if (-not $SkipBuild) {
    if (-not (Test-Path -LiteralPath $MSBuildPath)) {
        throw "找不到 MSBuild：$MSBuildPath"
    }
    Write-Host "编译 Release $version ..."
    & $MSBuildPath (Join-Path $repoRoot 'Baud.slnx') `
        '/p:Configuration=Release' '/p:Platform=x64' '/m'
    if ($LASTEXITCODE -ne 0) {
        throw "Release 编译失败（退出码 $LASTEXITCODE）。"
    }
}

$releaseExe = Join-Path $releaseDir 'Baud.exe'
if (-not (Test-Path -LiteralPath $releaseExe)) {
    throw "找不到 Release 产物：$releaseExe"
}

$exeVersion = (Get-Item -LiteralPath $releaseExe).VersionInfo.FileVersion
if ($exeVersion -notlike "$version*") {
    throw "Baud.exe 版本 $exeVersion 与 src\Version.h 的 $version 不一致。"
}

if (Test-Path -LiteralPath $distDir) {
    Remove-Item -LiteralPath $distDir -Recurse -Force
}
New-Item -ItemType Directory -Path $distDir | Out-Null

$stageName = "Baud-$version-portable-x64"
$stageDir = Join-Path $distDir $stageName
Write-Host "暂存运行文件到 $stageName ..."

$excludedExtensions = @('.pdb', '.lib', '.exp', '.ilk', '.iobj', '.ipdb', '.map')
Get-ChildItem -LiteralPath $releaseDir -Recurse -File | Where-Object {
    $excludedExtensions -notcontains $_.Extension.ToLowerInvariant()
} | ForEach-Object {
    $relative = [System.IO.Path]::GetRelativePath($releaseDir, $_.FullName)
    $destination = Join-Path $stageDir $relative
    $destinationDir = Split-Path -Parent $destination
    New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
}

$requiredFiles = @(
    'Baud.exe',
    'Qt6Core.dll',
    'LICENSE',
    'THIRD_PARTY_NOTICES.md',
    'platforms\qwindows.dll',
    'tls\qschannelbackend.dll'
)
foreach ($required in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $stageDir $required))) {
        throw "暂存目录缺少运行文件：$required"
    }
}

$zipPath = Join-Path $distDir "$stageName.zip"
Write-Host '生成便携 zip ...'
Compress-Archive -Path $stageDir -DestinationPath $zipPath -CompressionLevel Optimal -Force

$assets = @($zipPath)

if (-not $PortableOnly) {
    $iscc = Resolve-Iscc -Explicit $IsccPath
    $issPath = Join-Path $repoRoot 'installer\Baud.iss'
    if (-not (Test-Path -LiteralPath $issPath)) {
        throw "找不到安装器脚本：$issPath"
    }

    Write-Host "生成安装包 Baud-Setup-$version.exe ..."
    & $iscc "/DAppVersion=$version" "/DStageDir=dist/$stageName" $issPath
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup 编译失败（退出码 $LASTEXITCODE）。"
    }

    $setupPath = Join-Path $distDir "Baud-Setup-$version.exe"
    if (-not (Test-Path -LiteralPath $setupPath)) {
        throw "安装器未生成：$setupPath"
    }
    $assets = @($setupPath) + $assets
}

$sumsPath = Join-Path $distDir 'SHA256SUMS.txt'
$sumLines = foreach ($asset in $assets) {
    $hash = (Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash.ToLowerInvariant()
    $name = Split-Path -Leaf $asset
    "$hash  $name"
}
Set-Content -LiteralPath $sumsPath -Value $sumLines -Encoding ascii

Remove-Item -LiteralPath $stageDir -Recurse -Force

Write-Host ''
Write-Host '打包完成，dist 内容：'
Get-ChildItem -LiteralPath $distDir -File | ForEach-Object {
    '  {0}  {1:N1} MB' -f $_.Name, ($_.Length / 1MB)
}
Write-Host ''
Write-Host '下一步（需要显式确认后再执行）：'
Write-Host "  git add <files>; git commit; git tag v$version; git push origin main --tags"
Write-Host "  gh release create v$version dist/*.exe dist/*.zip dist/SHA256SUMS.txt --title `"Baud $version`" --generate-notes"
