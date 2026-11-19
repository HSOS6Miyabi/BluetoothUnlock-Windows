# 开发、测试与发布说明

## 开发环境

Windows 10/11 x64、Visual Studio 2022（安装 C++ 桌面开发工具和 Windows SDK）、CMake 3.21 或更新版本。代码使用 C++20、Win32、C++/WinRT、CNG、WinHTTP、Windows Credential Provider；Windows GUI 不依赖大型第三方界面框架。

在 Windows PowerShell 运行：

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

构建完成后，`build/windows/Release/` 中应有 `BluetoothUnlock.exe` 和 `BluetoothUnlockCredentialProvider.dll`。测试程序包括 `ble_presence_tests`、`protocol_tests`、`remote_protocol_tests`、`app_self_test`、`interop_windows_tests`、`tpm_storage_tests`。

Linux 可以检查可移植的核心逻辑：

```bash
cmake -S . -B build-linux -DBUILD_TESTING=ON
cmake --build build-linux --parallel 2
ctest --test-dir build-linux --output-on-failure
python3 scripts/check_docs.py
```

Linux 不会生成 Windows 登录 DLL，也无法代替 Windows 实机安装与锁屏测试。

## 修改代码的顺序

1. 从最新 `main` 创建分支，阅读 [项目台账](PROJECT_LEDGER.md)，确定影响范围。
2. 修改跨平台核心或 BLE 协议时，先更新纯 C++ 测试；同步检查 Android `Protocol.kt`、Windows `core/protocol.*` 和两仓库的 `interop_vectors.properties`。
3. 修改 Windows GUI 时，检查正常窗口尺寸、缩放、浅色／深色主题以及操作过程中界面是否仍能响应。
4. 修改 Credential Provider、TPM 或 Windows 服务时，保留可用的传统登录方式，在测试机器执行安装、锁屏、停止服务和卸载恢复检查。
5. 修改配置结构时，明确升级前后是否可以读取已有密码、Android 公钥、RustDesk API Key；必要时提供安全迁移和回退路径。
6. 更新相关文档、[台账](PROJECT_LEDGER.md) 与测试记录，运行本地测试后提交 PR，等待 GitHub Actions 完成。

## 与 Android 协作时必须同步的内容

- GATT 服务 UUID、三个特征 UUID、随机挑战长度、分段标识、签名格式与指纹算法。
- Android 包名 `top.kenvix.bleunlock`、Keystore 别名 `kenvix_unlock_identity_v1`、协议签名原文 `KENVIX-BLE-UNLOCK/v1:` 为既有兼容标识。更换这些值会影响现有安装或已绑定设备，不属于常规界面更名。
- Windows 有单独的 Credential Provider CLSID、服务名、配置格式、TPM 密钥名和跨进程对象名称，修改时应提供迁移。
- 两个仓库各自有 CI；Windows CI 从 Android `main` 读取协议测试数据。涉及双端的修改先让 Android 相应 PR 合并并通过 CI，再验证 Windows 的跨仓库检查。

## Windows 发布程序

`.github/workflows/ci-release.yml` 在 PR 中编译与测试；在 `main` 构建成功后检查 `VERSION`，生成包含 EXE、DLL、安装脚本与文档的 ZIP 和 `SHA256SUMS.txt`。如果相应的 `v<版本号>` 标签已经存在，工作流程会保留已有 Release，不覆盖发行文件。

发布新版本时：

1. 说明本次变化，确认升级和恢复方式；按实际发布需要修改根目录 `VERSION`。
2. 同步修改 `windows/settings.manifest` 中 `assemblyIdentity.version`，保持四段式版本一致。
3. 在 PR 中通过 Windows C++ 测试及跨端检查，进行与本次变更对应的实机验证。
4. 合并后检查 GitHub Actions 的 Build、Interop、Release 三个任务。
5. 打开 GitHub Releases，确认 ZIP、SHA256 文件、版本和对应提交；填写 [项目台账](PROJECT_LEDGER.md)。
6. Windows CI 当前没有 Authenticode 签名步骤。需要发布带可信发布者签名的 EXE/DLL 时应单独配置签名证书，并保证密钥不进入仓库。

**只修改文档时不需要提高版本号。** CI 仍会检查文档和编译，已有版本的 Release 将保持不变。

## 不能省略的安全检查

- 不能将模拟 SSE 事件、MAC 地址或历史设备列表当成已完成高安全性身份认证。
- 不能把 API Key 放进 SSE URL、仓库、日志或测试数据；HTTP 请求要求 HTTPS 并禁止重定向。
- 不要在未经真实 Windows 环境验证的情况下修改登录提供程序的系统注册项或移除现有登录选项。
- 不要假设 TPM 加密就能阻止管理员或 SYSTEM 进程调用本机密钥。
- 修改 RustDesk SSE 自动解锁时，先验证服务端事件产生方式；详见 [安全说明](SECURITY.md) 与 [RustDesk API](RUSTDESK_API_UNLOCK.md)。

更多操作细节见 [OPERATIONS](OPERATIONS.md) 和 [设备测试记录](DEVICE_TEST_RECORD.md)。
