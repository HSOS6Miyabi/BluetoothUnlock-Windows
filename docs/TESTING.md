# 测试说明

## 已提供的自动测试

跨平台 C++ 测试涵盖挑战帧、身份签名消息、DER 签名解析、公钥格式检查、响应长度检查，以及 RSSI 阈值与广播时效过滤。

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

纯 JVM Kotlin 测试覆盖版本与挑战帧、签名域分隔、ECDSA P-256 签名验证和指纹计算。无需 Android SDK，可通过本机 `kotlinc` 运行：

```bash
kotlinc android/app/src/main/java/top/kenvix/bleunlock/Protocol.kt tests/KotlinProtocolSmoke.kt -include-runtime -d /tmp/kenvix-protocol-tests.jar
java -jar /tmp/kenvix-protocol-tests.jar
```

Android 工程还包含基于 JUnit 4 的 `ProtocolTest.kt`。Windows 和 Android 的完整构建、对应测试在 `.github/workflows/build.yml` 配置。

## 物理设备测试清单

在 Windows x64 测试电脑、支持 Android BLE 广播的手机及必要时的蓝牙手环上检查：

1. 安装 Windows Credential Provider 后，系统密码、PIN、Windows Hello 登录选项仍可用。
2. Windows 登录后使用 `Win + L` 锁定，确认不会在冷启动、注销状态自动登录。
3. 高安全性绑定时，Windows 和 Android 的公钥指纹一致；故意不确认时不能保存新的身份。
4. Android 自动模式，在正确手机靠近时接收新的挑战并完成 Windows 解锁。
5. Android 手动模式，确认之前 Windows 保持锁定；取消或拒绝时保持锁定，完成手机身份验证后才能解锁。
6. 手机停止后台服务、关闭蓝牙、离开信号范围或变更身份密钥后，都不能自动解锁。
7. 将 RSSI 阈值调高，确认信号不足时不解锁；调低后重新测试。
8. 修改 Windows 账户密码，确认旧配置不可解锁，更新密码后再次测试。
9. 低安全性模式只对确实已配对、持续发送可接收广播的 BLE 设备生效。
10. 多次锁屏解锁、Android 应用重启、手动确认超时及系统重新启动后，检查服务是否正常清理请求，且系统原有登录方式仍能工作。

运行环境的 Windows 蓝牙驱动、LogonUI 访问权限以及 Android 设备后台策略会影响实际结果。协议单元测试通过后，仍需要完成上述物理设备测试才能确认可投入日常使用。
