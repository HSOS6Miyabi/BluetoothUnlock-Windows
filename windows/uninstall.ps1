#Requires -RunAsAdministrator
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$exe = Join-Path $env:ProgramFiles 'Kenvix\BluetoothUnlock\KenvixBluetoothUnlock.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw '未找到已安装的 KenvixBluetoothUnlock.exe。'
}
& $exe --uninstall
if ($LASTEXITCODE -ne 0) { throw '卸载程序返回失败状态。' }
