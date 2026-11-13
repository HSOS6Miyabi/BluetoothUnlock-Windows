#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build')
)

$ErrorActionPreference = 'Stop'
$clsid = '{12E137F0-91AB-4B74-A37F-019B284A5C8E}'
$target = Join-Path $env:ProgramFiles 'Kenvix\BluetoothUnlock'
$buildDir = [IO.Path]::GetFullPath($BuildDirectory)

function Find-Executable([string]$filename) {
    $candidates = @(
        (Join-Path $buildDir "windows\Release\$filename"),
        (Join-Path $buildDir "windows\$filename"),
        (Join-Path $buildDir "Release\$filename")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw "未找到 $filename。请先使用 Visual Studio x64 Release 配置编译 Windows 项目。"
}

$dll = Find-Executable 'KenvixBleCredentialProvider.dll'
$exe = Find-Executable 'KenvixBleSettings.exe'
$remoteExe = Find-Executable 'KenvixRustDeskSettings.exe'
$serviceExe = Find-Executable 'KenvixRustDeskService.exe'
$serviceName = 'KenvixRustDeskUnlock'
$existingService = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($existingService -and $existingService.Status -ne 'Stopped') {
    Stop-Service -Name $serviceName -ErrorAction Stop
    (Get-Service -Name $serviceName).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(50))
}
New-Item -ItemType Directory -Path $target -Force | Out-Null
$installedDll = Join-Path $target 'KenvixBleCredentialProvider.dll'
Copy-Item -LiteralPath $dll -Destination $installedDll -Force
Copy-Item -LiteralPath $exe -Destination (Join-Path $target 'KenvixBleSettings.exe') -Force
Copy-Item -LiteralPath $remoteExe -Destination (Join-Path $target 'KenvixRustDeskSettings.exe') -Force
$installedService = Join-Path $target 'KenvixRustDeskService.exe'
Copy-Item -LiteralPath $serviceExe -Destination $installedService -Force
if (-not $existingService) {
    New-Service -Name $serviceName -DisplayName 'Kenvix RustDesk Unlock' `
        -Description 'Watches authenticated RustDesk API SSE connection events for the configured Windows console.' `
        -BinaryPathName ('"' + $installedService + '"') -StartupType Automatic | Out-Null
} else {
    & sc.exe config $serviceName binPath= ('"' + $installedService + '"') start= auto obj= LocalSystem | Out-Null
    if ($LASTEXITCODE -ne 0) { throw '无法更新 RustDesk 后台服务配置。' }
}
& sc.exe failure $serviceName reset= 86400 actions= restart/5000/restart/30000/restart/60000 | Out-Null
Start-Service -Name $serviceName -ErrorAction Stop

# Set only the COM registration and our own Credential Provider entry.
# Existing Windows password, PIN and Hello providers remain registered.
$inproc = "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32"
$provider = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid"
New-Item -Path $inproc -Force | Out-Null
Set-Item -Path $inproc -Value $installedDll
New-ItemProperty -Path $inproc -Name 'ThreadingModel' -Value 'Apartment' -PropertyType String -Force | Out-Null
New-Item -Path $provider -Force | Out-Null
Set-Item -Path $provider -Value 'Kenvix Bluetooth Unlock'

Write-Host 'Kenvix 蓝牙解锁已注册。' -ForegroundColor Green
Write-Host "设置程序：$(Join-Path $target 'KenvixBleSettings.exe')"
Write-Host '请以管理员身份启动设置程序，完成设备身份和 Windows 账户配置。'
Write-Host '第一次使用前，请确保可以使用系统密码登录，并保存当前工作。'

Write-Host "RustDesk API 设置：$(Join-Path $target 'KenvixRustDeskSettings.exe')"
Write-Host 'RustDesk 后台服务已安装并启动。'
