# 自动化测试与设备联调

## Windows 构建与测试

在安装 Visual Studio 2022 和 Windows SDK 的环境中运行：

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

其中 `interop_windows_tests` 通过 Windows CNG 验证 Android P-256 公钥、SHA-256 签名、公钥指纹、挑战片段和响应帧。所有 C++ 测试在 Release 编译时也会执行断言。

Linux 可以运行协议测试与 OpenSSL 签名兼容性测试：

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Android 构建与测试

Android 仓库执行：

```bash
gradle --no-daemon :app:testDebugUnitTest :app:lintDebug :app:assembleDebug
```

`InteropVectorTest.kt` 使用 Java 标准密码学接口验证 Windows 使用的同一组测试数据。`tests/interop_vectors.properties` 与 Android 的 `app/src/test/resources/interop_vectors.properties` 保持一致。Windows CI 同时检验两个仓库中该文件的内容一致。

本地仅安装 Kotlin/JDK 时，还可以在 Android 仓库执行：

```bash
kotlinc app/src/main/java/top/kenvix/bleunlock/Protocol.kt tests/InteropVectorSmoke.kt -include-runtime -d /tmp/interop.jar
java -jar /tmp/interop.jar
```

## Windows 服务管理检查

在测试用 Windows 电脑中打开“服务管理”页面，依次检查：

1. 尚未安装时显示“尚未安装”，允许安装，禁用启动和停止按钮。
2. 安装后系统服务名为 `KenvixRustDeskUnlock`，并显示登录组件已注册。
3. 点击“停止服务”后，状态变为“已停止”；点击“启动服务”后，状态变为“正在运行”。
4. 点击“重新安装 / 修复”后仍能运行服务，原先保存的 Windows 和 RustDesk 设置保持有效。
5. 卸载并勾选“保留设置”后，重新安装仍能读取原有配置。
6. 选择不保留设置卸载后，服务和组件均已移除，原有 Windows 登录方式继续可用。

## Windows 与 Android 实际设备联调

在一台运行 Windows 10/11 的测试电脑和一台支持 BLE 外围设备广播的 Android 手机上，按照以下步骤检查：

1. Windows 和 Android 均启动蓝牙，Android 应用启动前台服务。
2. Windows 在“蓝牙解锁”页面发现手机，并读取 65 字节公钥。核对双方显示的 SHA-256 公钥指纹。
3. 保存 Windows 登录账户，在手机上使用“自动认证”模式，按 `Win + L` 锁屏，确认完成挑战签名认证后解锁。
4. 改用“手动确认”，检查 Android 通知和系统身份验证。拒绝、取消和超时均应保持锁定。
5. 关闭手机蓝牙、停止前台服务、移出信号范围、修改信号阈值及更换手机身份后，确认不会错误解锁。
6. 启用 RustDesk SSE 并完成远程连接认证，检查锁屏状态下的自动解锁，以及最后一条连接结束后的自动锁定。
7. 检查 Windows 密码、PIN 和 Windows Hello 是否继续正常使用，并确认开机登录界面不会被自动解锁。

CI 的签名和协议测试无需蓝牙设备。真正的无线连接、手机后台运行和 Windows 系统锁屏操作仍需在实体设备上测试。
