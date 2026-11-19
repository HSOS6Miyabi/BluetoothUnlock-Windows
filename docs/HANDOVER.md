# Windows 项目交接指南

本文面向第一次接手 Bluetooth Unlock Windows 端的维护人员。功能使用说明见 [README](../README.md)，安装和恢复操作见 [OPERATIONS](OPERATIONS.md)。

## 项目现状（2026-11-19）

| 项目 | 当前情况 |
| --- | --- |
| 源代码 | [BluetoothUnlock-Windows](https://github.com/HSOS6Miyabi/BluetoothUnlock-Windows)，开发主分支 `main` |
| 对应手机应用 | [BluetoothUnlock-Android](https://github.com/HSOS6Miyabi/BluetoothUnlock-Android) |
| 最新已核对的 Windows 发行版本 | `v1.2.2`，以 GitHub Releases 和根目录 `VERSION` 为准 |
| Windows 构建 | Visual Studio 2022、Windows SDK、CMake、C++20，目标 x64 |
| 发行文件 | `BluetoothUnlock.exe` 与 `BluetoothUnlockCredentialProvider.dll`，另有安装脚本和文档 |
| 系统服务 | `BluetoothUnlockService`，由 EXE 的 `--service` 模式运行 |
| Android 对应版本 | 当前已核对 `v1.0.3-preview.1`，调试签名测试版 |
| 自动测试 | GitHub Actions 负责 Windows 编译、C++ 测试、跨平台协议测试；实体设备测试按 [测试记录](DEVICE_TEST_RECORD.md) 执行 |

**交接状态：** 已有自动构建与协议测试；还需要在真实 Windows 10/11、蓝牙适配器及 Android 手机上记录系统锁屏、BLE GATT、TPM 和持续后台运行的实测结果。不要把 CI 成功解释为实体设备的全部行为已经得到验证。

## 新维护人员应先做的事情

1. 阅读 [架构和模块职责](ARCHITECTURE.md)，确认设置 EXE、系统服务、Credential Provider DLL 之间的调用关系。
2. 阅读 [项目台账](PROJECT_LEDGER.md)，确定当前实现范围、长期需要验证的事项和维护注意事项。
3. 按 [开发与发布说明](DEVELOPMENT.md) 在独立开发环境中编译，执行 `ctest`；确认自己能查看 GitHub Actions 的构建日志和发行包。
4. 检查 Android 仓库的相同协议版本及 [BLE 协议](PROTOCOL.md)，不单独改动一端的 UUID、随机挑战格式或签名原文。
5. 准备一台保留密码或 PIN 的**测试电脑**，按 [设备测试记录](DEVICE_TEST_RECORD.md) 操作并记录实际系统、蓝牙适配器与手机型号。
6. 修改设置界面前，检查浅色和深色主题、100%／150%／200% 缩放；修改登录 DLL 前，先准备安全模式或恢复环境的卸载方法。

## 日常工作怎么处理

- **用户反馈扫描不到手机：** 先确认手机应用的前台服务和 BLE 广播是否成功，然后查看 Windows 表格的设备名称、解锁服务占位名称以及“上次 dBm”状态。列表保留约 3 分钟，解锁所需 RSSI 仍要求最近 4 秒的新广播，见 [OPERATIONS](OPERATIONS.md)。
- **用户反馈锁屏后无法解锁：** 先验证 Windows 原生密码登录可用，再按 [TESTING](TESTING.md) 分别排查新鲜广播、公钥指纹、GATT 写入、Android 确认、凭据提交和当前锁定会话。
- **RustDesk 远程触发异常：** 按 [RustDesk 接入](RUSTDESK_API_UNLOCK.md) 核对 SSE、事件类型、连接编号、API Key 权限和服务器审计来源。使用连接事件进行自动解锁之前，需要确认服务端的事件可信程度。
- **服务安装或卸载异常：** 优先使用 GUI 的“服务管理”，需要手动恢复时参考 [OPERATIONS](OPERATIONS.md) 中的系统服务、注册表及安全模式说明。

## 交接时必须保留的信息

将交接记录放在 GitHub Issue、PR 或本仓库的 [项目台账](PROJECT_LEDGER.md) 中：实际使用的提交 SHA、Windows／Android 版本、测试设备型号、测试结果、仍需完成的工作、对应负责人或待分配状态。不要向仓库提交账户密码、API Key、签名私钥、Android 密钥库、个人设备完整日志或其他敏感配置。

## 变更记录的维护规则

协议、安全边界、凭据保存格式、服务名、登录组件 CLSID、Android 标识或发布流程发生变化时，同一次 PR 应更新对应文档与 [项目台账](PROJECT_LEDGER.md)。新增可复现问题时记录复现条件、影响范围、处理状态及验证方法；完成处理后更新为当前有效的设计说明。
