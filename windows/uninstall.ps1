#Requires -RunAsAdministrator
[CmdletBinding()]
param([switch]$KeepSettings)

$ErrorActionPreference = 'Stop'
$clsid = '{12E137F0-91AB-4B74-A37F-019B284A5C8E}'
$serviceName = 'KenvixRustDeskUnlock'
$service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($service) {
    if ($service.Status -ne 'Stopped') {
        Stop-Service -Name $serviceName -ErrorAction Stop
        (Get-Service -Name $serviceName).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(50))
    }
    & sc.exe delete $serviceName | Out-Null
    if ($LASTEXITCODE -ne 0) { throw '无法删除 RustDesk 后台服务。' }
} 
$registryPaths = @(
    "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid",
    "HKLM:\SOFTWARE\Classes\CLSID\$clsid"
)
foreach ($key in $registryPaths) {
    if (Test-Path -LiteralPath $key) {
        Remove-Item -LiteralPath $key -Recurse -Force
    }
}
$binaryDirectory = Join-Path $env:ProgramFiles 'Kenvix\BluetoothUnlock'
if (Test-Path -LiteralPath $binaryDirectory) {
    try {
        Remove-Item -LiteralPath $binaryDirectory -Recurse -Force
    } catch {
        Write-Warning 'LogonUI 可能还在使用程序文件。注册信息已移除，请重新启动后再次运行卸载脚本以清理文件。'
    }
}
if (-not $KeepSettings) {
    $dataDirectory = Join-Path $env:ProgramData 'Kenvix\BluetoothUnlock'
    if (Test-Path -LiteralPath $dataDirectory) {
        Remove-Item -LiteralPath $dataDirectory -Recurse -Force
    }
}
Write-Host '凭据提供程序的注册信息已删除。请重新启动 Windows 以完成卸载。' -ForegroundColor Green
