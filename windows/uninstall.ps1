#Requires -RunAsAdministrator
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$exe = Join-Path $env:ProgramFiles 'BluetoothUnlock\BluetoothUnlock.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw '未找到已安装的 BluetoothUnlock.exe。'
}
& $exe --uninstall
if ($LASTEXITCODE -ne 0) { throw '卸载程序返回失败状态。' }
