#Requires -RunAsAdministrator
[CmdletBinding()]
param([string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build'))
$ErrorActionPreference = 'Stop'
$buildDir = [IO.Path]::GetFullPath($BuildDirectory)
$candidates = @(
    (Join-Path $buildDir 'windows\Release\KenvixBluetoothUnlock.exe'),
    (Join-Path $buildDir 'windows\KenvixBluetoothUnlock.exe'),
    (Join-Path $buildDir 'Release\KenvixBluetoothUnlock.exe')
)
$exe = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $exe) { throw '未找到 KenvixBluetoothUnlock.exe。请先编译 Windows 项目。' }
& $exe --install
if ($LASTEXITCODE -ne 0) { throw '安装程序返回失败状态。' }
