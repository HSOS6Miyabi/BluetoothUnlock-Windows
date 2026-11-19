# 项目台账与维护事项

记录截至 **2026-11-19** 的代码、版本及待验证事项。维护人员在合并影响行为或发布流程的 PR 后更新本文件，并以 GitHub 主分支、CI 和 Release 页面为最终状态来源。

## 版本与交付范围

| 项目 | 当前内容 |
| --- | --- |
| Windows | `v1.2.2`；统一设置 EXE、Credential Provider DLL、Windows 服务 |
| Android | `v1.0.3-preview.1`；使用调试签名的测试 APK |
| Windows 发行流程 | `.github/workflows/ci-release.yml` 在 `main` 编译、测试并尝试按 `VERSION` 发布 |
| 跨端检查 | Windows CI 获取 Android `main` 的签名测试数据，执行一致性和 OpenSSL 测试 |
| RustDesk 服务端依赖 | [HSOS6Miyabi/rustdesk-api](https://github.com/HSOS6Miyabi/rustdesk-api)，SSE 为 `/api/events/v1/stream` |

## 已提供的功能与验证层级

| 子系统 | 已实现能力 | 已有自动化检查 | 设备实测情况 |
| --- | --- | --- | --- |
| Windows 界面 | HiDPI、浅色／深色、设备表格、三页管理 | 编译和程序 `--self-test` | 按测试记录补充实际系统截图 |
| BLE 扫描 | 主动扫描、名称合并、3 分钟驻留、扫描恢复 | `ble_presence_tests` | 需记录不同适配器及手机 |
| BLE 高安全性 | ECDSA P-256 随机挑战及公钥核对 | 协议、CNG/OpenSSL 和跨端数据测试 | 需测试实际 GATT 与锁屏 |
| BLE 低安全性 | Windows 配对状态、设备地址与 RSSI | 信号判断测试 | 需测试具体手环及随机地址 |
| 系统登录 | Credential Provider 提交 Windows 凭据 | Windows DLL 编译及启动自检 | 需测试控制台锁屏与安全恢复 |
| 凭据保存 | DPAPI、可选 TPM、旧版设置读取 | `tpm_storage_tests` 加解密与密文检测 | 需测试真实 TPM 的密钥持久化和清除场景 |
| RustDesk | SSE 订阅、连接跟踪、断线处理、锁定请求 | `remote_protocol_tests` | 需与实际 API 服务器验证事件可信性和锁定流程 |
| Android 手机 | 前台 GATT 服务、自动／手动确认、名称广播回退 | Gradle 单元测试与 Lint | 需记录厂商后台限制及重启行为 |

这里的“自动化检查”只代表对应代码路径通过构建或单元测试；真实 Windows 登录、TPM 物理设备和 BLE 无线行为需要单独记录。

## 持续维护事项

| 编号 | 重要程度 | 事项和完成条件 |
| --- | --- | --- |
| W-01 | 高 | 在 Windows 10/11 真机验证正常密码登录、自动解锁、注销后不自动登录和凭据提供程序卸载恢复；将操作系统版本与结果填入测试记录 |
| W-02 | 高 | 验证 RustDesk API 的连接审计上报由可信组件产生，尤其是自动解锁所用事件的服务端身份校验；在结果明确前谨慎启用远程自动解锁 |
| W-03 | 高 | 在支持及不支持 TPM 的机器上验证凭据保存、解密、旧版设置迁移及不可恢复场景 |
| W-04 | 中 | 测试常见 Windows 蓝牙适配器和 Android 手机组合，记录发现名称、GATT 通信、扫描中断与恢复结果 |
| W-05 | 中 | 验证 100%、150%、200% 缩放和 Windows 浅色／深色模式下三页布局及状态消息 |
| W-06 | 中 | 为 Windows 发行文件安排可验证的代码签名与证书管理方案；当前 CI 负责生成 ZIP 与 SHA256 校验文件 |

维护事项在真实完成并复核后，更新记录及对应测试结果。完整测试步骤见 [DEVICE_TEST_RECORD](DEVICE_TEST_RECORD.md)。

## 重要设计选择

| 主题 | 当前约定 |
| --- | --- |
| Windows 登录扩展 | 使用原生 Credential Provider DLL，保留 Windows 原有登录选项 |
| Windows 后台服务 | 与 GUI 共用 `BluetoothUnlock.exe`，依靠 `--service` 参数区分运行方式 |
| Android 通信 | BLE GATT 协议 v1，随机挑战使用 Android Keystore 生成的 ECDSA 私钥签名 |
| 设备驻留 | 列表历史可显示约 3 分钟；参与解锁的 BLE 信号仍需满足 4 秒有效期 |
| 用户可见名称 | 使用“Bluetooth Unlock”，保留协议内部兼容标识 |
| 版本处理 | Windows 的 `VERSION` 和应用清单版本同步；修改文档不自动提高版本号 |
| 发版前检查 | Windows CI、跨仓库协议测试及必要的真实设备测试分别记录 |

## 台账更新办法

每次功能 PR 应标明受影响的模块、对应测试、配置兼容影响、系统权限变化和跨仓库依赖。用户设备问题应说明设备型号、操作系统版本、操作步骤、实际现象以及相关 GitHub Issue。不要保存完整凭据、实际 API Key 或可识别用户的原始日志。

本台账记录有效的项目状态。具体模块接口以 [架构说明](ARCHITECTURE.md)、[协议](PROTOCOL.md) 和代码为准。
