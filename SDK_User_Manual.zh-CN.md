# rmAxSdk1 SDK 用户手册（详细版）

**适用版本**：以本发布包 `include/rmAxSdk1/` 下的公开头文件 `rmAxSdkApi.h`、`rmAxSdkTypes.h` 为准。
**发布平台**：Android arm64-v8a。

> 本文档面向集成 rmAxSdk1 的应用开发者，覆盖 SDK 功能、集成步骤、全部公开 API、
> 数据类型、消息通知、典型业务流程、最佳实践与常见问题，可作为开发与排障的完整参考。

---

## 目录

1. [SDK 简介](#1-sdk-简介)
2. [发布包目录结构](#2-发布包目录结构)
3. [运行环境与构建要求](#3-运行环境与构建要求)
4. [Android 集成指南](#4-android-集成指南)
5. [快速开始：最小可运行示例](#5-快速开始最小可运行示例)
6. [核心概念](#6-核心概念)
7. [生命周期与通用接口](#7-生命周期与通用接口)
8. [消息机制详解](#8-消息机制详解)
9. [配置读取、修改与保存](#9-配置读取修改与保存)
10. [输入源、输出通道与微调](#10-输入源输出通道与微调)
11. [混控（Mixing）](#11-混控mixing)
12. [曲线（Curve）](#12-曲线curve)
13. [双比率 DR](#13-双比率-dr)
14. [定时器与计数器](#14-定时器与计数器)
15. [系统配置](#15-系统配置)
16. [设备控制](#16-设备控制)
17. [模型与模板管理](#17-模型与模板管理)
18. [遥测与通知](#18-遥测与通知)
19. [ELRS 与 CRSF](#19-elrs-与-crsf)
20. [设备固件升级](#20-设备固件升级)
21. [APP 升级](#21-app-升级)
22. [数据类型参考](#22-数据类型参考)
23. [返回值与错误处理约定](#23-返回值与错误处理约定)
24. [最佳实践](#24-最佳实践)
25. [常见问题（FAQ）](#25-常见问题faq)
26. [附录](#26-附录)

---

## 1. SDK 简介

rmAxSdk1 是一套用于遥控模型设备（RC 遥控器）的 native C++ SDK。宿主应用（Android APP）
通过 SDK 可以完成：

- **配置管理**：读取/修改/保存遥控器的工作配置（输入源、输出通道、混控、曲线、DR、
  定时器/计数器、系统参数）。
- **实时状态**：订阅摇杆、旋钮、开关的实时值，通道输出值，微调值，RF 链路状态
  （LQ、dBm、丢包计数）、电池电压等。
- **模型管理**：模型/模板的创建、加载、保存、导入、导出、删除。
- **遥测（Telemetry）**：获取接收机回传数据（位置、姿态、电压、电流），配置报警通知。
- **ELRS/CRSF**：浏览并修改 ELRS 模块参数菜单、查询链路状态、收发原始 CRSF 帧。
- **升级**：设备固件升级、宿主 APP 自身的版本检查与 APK 下载。

### 1.1 架构特点

- **纯标准 C++ 边界**：公开 API 只使用 `std::string`、`std::vector`、`std::list`、
  `std::shared_ptr` 等标准类型，宿主不需要使用 Qt 或其他特定框架。
- **双通道交互模型**：
  - 宿主 → SDK：直接调用 `rcGet*` / `rcSet*` / `rcTask*` 等公开函数；
  - SDK → 宿主：通过 `setMessageCallback()` 注册的统一消息回调异步推送。
- **内部多线程**：SDK 内部包含一个工作线程和一个独立的消息输出分发线程，输入/输出
  均为有界消息队列（当前上限均为 4096，宿主不应依赖该数值）。
- **异常安全**：所有公开 API 均为 `noexcept`，异常不会越过动态库边界。

### 1.2 命名空间与头文件

所有公开类型与函数位于 `rmaxSdk` 命名空间：

```cpp
#include <rmAxSdkApi.h>   // 已包含 rmAxSdkTypes.h，通常只需包含这一个
```

---

## 2. 发布包目录结构

```text
rmAxSdk-main/
├── include/
│   └── rmAxSdk1/
│       ├── rmAxSdkApi.h      # 公开 API 声明（函数入口）
│       └── rmAxSdkTypes.h    # 公开数据结构、消息 ID 定义
├── lib/
│   └── arm64-v8a/
│       ├── librmAxSdk1.so                 # SDK 本体（约 513 KB）
│       ├── libQt6Core_arm64-v8a.so        # Qt6 核心库（约 5.8 MB）
│       ├── libQt6Network_arm64-v8a.so     # Qt6 网络库（约 1.8 MB）
│       ├── libQt6SerialPort_arm64-v8a.so  # Qt6 串口库（约 124 KB）
│       ├── libQt6Concurrent_arm64-v8a.so  # Qt6 并发库（约 42 KB）
│       └── libc++_shared.so               # LLVM libc++ 运行库（约 1.8 MB）
├── licenses/
│   └── Qt-LICENSE.txt      # Qt 开源许可证文本
├── SDK_API_USAGE.en.md     # API 使用说明（英文）
├── SDK_API_USAGE.zh-CN.md  # API 使用说明（中文）
└── tst_rmAxSdkApi.cpp      # 测试控制台参考源码（交互式演示全部 API）
```

### 2.1 依赖说明

| 库 | 作用 | 是否必须部署 |
| --- | --- | --- |
| `librmAxSdk1.so` | SDK 本体 | 必须 |
| `libQt6Core_arm64-v8a.so` | Qt 核心（事件循环、线程、容器等） | 必须 |
| `libQt6Network_arm64-v8a.so` | 网络（APP 更新检查/下载） | 必须 |
| `libQt6SerialPort_arm64-v8a.so` | 串口通信（设备连接） | 必须 |
| `libQt6Concurrent_arm64-v8a.so` | 并发任务支持 | 必须 |
| `libc++_shared.so` | C++ 标准库运行时（Qt/SKD 均依赖） | 必须 |

以下系统库由 Android 提供，**不要**打包进应用：`liblog.so`、`libm.so`、`libz.so`、
`libdl.so`、`libc.so`。

> **许可提示**：SDK 分发包内含 Qt6 运行时库，Qt 采用其开源许可证发布
> （详见 `licenses/Qt-LICENSE.txt`）。商用发布时请评估 Qt 许可合规性。

> **关于测试源码**：`tst_rmAxSdkApi.cpp` 依赖未随包发布的 `rmAxSdkTestConsole.h`，
> 无法在本包内直接编译，请将其作为**调用方式参考代码**阅读，第 26 章附录 A 给出了
> 其完整命令说明。

---

## 3. 运行环境与构建要求

### 3.1 基本要求

- **语言标准**：C++20 或更高。
- **目标平台**：Android，ABI 为 **arm64-v8a**（本发布包仅含该 ABI 二进制；
  Windows 版为单独的 `rmAxSdk1.dll` 分发，不在本包内）。
- **NDK**：使用支持 C++20 的较新版本 NDK（建议 r25 及以上），编译器为 Clang。
- **STL**：SDK 及其 Qt 运行时基于 **LLVM libc++**（`libc++_shared.so`），宿主应使用
  NDK 默认的 `c++_shared`，并保证最终只链接一份 `libc++_shared.so`。

### 3.2 ABI/CRT 一致性（重要）

宿主与 SDK 跨动态库边界传递 `std::string`、`std::vector`、`std::list`、
`std::shared_ptr`。**不要混用**不同的 C++ 运行时或 STL 实现，否则会在
`initialize()`、参数析构或返回值析构时表现为堆损坏、随机崩溃等难以排查的问题。

Android 下的注意事项：

- 宿主 `externalNativeBuild` 的 `ANDROID_STL` 使用 `c++_shared`（NDK 默认）；
- 全工程只保留一份 `libc++_shared.so`（本包已提供，与 Qt 库匹配）；
- 不要把 Qt 库更换为系统或其他来源的版本，运行时依赖必须与 `librmAxSdk1.so` 匹配。

### 3.3 宿主运行环境

SDK 不依赖宿主的消息循环。控制台程序、带 UI 的 APP、后台服务均可直接调用
`initialize()`。宿主需要保证：

1. 初始化时传入的应用数据目录（`appDataPath`）可读写；
2. 退出或卸载动态库之前调用 `shutdown()` 并释放持有的 SDK 对象。

---

## 4. Android 集成指南

### 4.1 拷贝库与头文件

将发布包内容拷贝到宿主工程，例如：

```text
app/
├── src/main/
│   ├── cpp/                     # CMake 工程
│   │   ├── CMakeLists.txt
│   │   └── native-lib.cpp
│   └── jniLibs/
│       └── arm64-v8a/
│           ├── librmAxSdk1.so
│           ├── libQt6Core_arm64-v8a.so
│           ├── libQt6Network_arm64-v8a.so
│           ├── libQt6SerialPort_arm64-v8a.so
│           ├── libQt6Concurrent_arm64-v8a.so
│           └── libc++_shared.so
└── sdk-include/
    └── rmAxSdk1/
        ├── rmAxSdkApi.h
        └── rmAxSdkTypes.h
```

### 4.2 CMakeLists.txt 配置

```cmake
cmake_minimum_required(VERSION 3.22)
project(rmAxConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 以 IMPORTED 方式引入 SDK
add_library(rmAxSdk1 SHARED IMPORTED)
set_target_properties(rmAxSdk1 PROPERTIES
    IMPORTED_LOCATION
        ${CMAKE_CURRENT_SOURCE_DIR}/../jniLibs/arm64-v8a/librmAxSdk1.so
    INTERFACE_INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk-include/rmAxSdk1
    IMPORTED_NO_SONAME TRUE
)

find_library(log-lib log)

add_library(native-lib SHARED native-lib.cpp)
target_link_libraries(native-lib
    rmAxSdk1
    ${log-lib}
)
```

链接时若使用 `-L<dir> -lrmAxSdk1` 方式，需保证链接器能解析全部符号；推荐上述
IMPORTED target 方式，并用 `IMPORTED_NO_SONAME TRUE` 避免把构建机绝对路径写入
DT_NEEDED。

### 4.3 build.gradle 配置要点

```groovy
android {
    defaultConfig {
        ndk { abiFilters 'arm64-v8a' }          // 本 SDK 仅支持 arm64-v8a
        externalNativeBuild {
            cmake { cppFlags '-std=c++20' }
        }
    }
    sourceSets.main.jniLibs.srcDir 'src/main/jniLibs'
    packagingOptions.jniLibs.useLegacyPackaging = true  // 保证 .so 全部解包安装
}
```

要点：

- **abiFilters 只保留 `arm64-v8a`**。若工程还包含其他 ABI（如 armeabi-v7a、x86_64），
  必须保证这些 ABI 的 APK 中不携带引用 rmAxSdk1 的代码，否则会在加载时崩溃。
- 7 个 `.so` 必须**全部**打入 APK 并保持同名；Android 安装时会按 ABI 解包到
  `app nativeLibraryDir`。
- Java/Kotlin 侧**不需要**为每个 Qt 库手写 `System.loadLibrary`，
  `System.loadLibrary("rmAxSdk1")` 时动态链接器会自动按 DT_NEEDED 加载同目录下的
  Qt 与 libc++ 运行库。
- 使用 APP 更新检查/下载功能时，需要在 `AndroidManifest.xml` 声明网络权限：

```xml
<uses-permission android:name="android.permission.INTERNET" />
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
```

### 4.4 验证集成是否成功

按第 5 章的最小示例运行，能打印 SDK 版本字符串且 `initialize()` 返回
`Success` / `AlreadyInitialized`，即说明链接与加载正常。

---

## 5. 快速开始：最小可运行示例

下面是一个完整的最小集成示例（Linux/NDK 控制台风格），演示初始化、注册回调、
读取设备配置、退出清场的固定顺序：

```cpp
#include <rmAxSdkApi.h>

#include <iostream>

// SDK 消息回调：在 SDK 输出分发线程执行，必须短小、非阻塞。
void onSdkMessage(const rmaxSdk::SdkMessage& message, void* userData)
{
    (void)userData;
    switch (message.msgId) {
    case rmaxSdk::RC_MSGID_GETDEVCFG_END:
        std::cout << "device config ready\n";
        break;
    case rmaxSdk::RC_MSGID_MIXOUT_UPDATE:
        // 实时数据已刷新，按需重新调用 getter（不要在回调里高频刷 UI）。
        break;
    default:
        break;
    }
}

int main()
{
    // 1. 打印版本（initialize 之前唯一可安全调用的业务无关 API）
    std::cout << "SDK version: " << rmaxSdk::version() << '\n';

    // 2. 初始化：传入应用私有数据目录
    const auto result = rmaxSdk::initialize("/data/data/<pkg>/files/rmAx");
    if (result != rmaxSdk::InitResult::Success
        && result != rmaxSdk::InitResult::AlreadyInitialized) {
        std::cerr << "initialize failed\n";
        return 1;
    }

    // 3. 注册消息回调
    if (!rmaxSdk::setMessageCallback(&onSdkMessage, nullptr)) {
        rmaxSdk::shutdown();
        return 1;
    }

    // 4. 异步从设备读取配置，等待 RC_MSGID_GETDEVCFG_END 后再视为就绪
    rmaxSdk::taskReadConfigFromDevice();

    // 5. 运行期业务调用示例
    const auto channels = rmaxSdk::rcGetChOutList();
    std::cout << "output channels: " << channels.size() << '\n';

    // 6. 退出清场：清除回调 -> shutdown
    rmaxSdk::setMessageCallback(nullptr, nullptr);
    rmaxSdk::shutdown();
    return 0;
}
```

**首次接入检查清单**：

- [ ] APK 内 7 个 `.so` 齐备，ABI 全部为 arm64-v8a；
- [ ] `initialize()` 返回 `Success` 或 `AlreadyInitialized`；
- [ ] `setMessageCallback()` 返回 `true`；
- [ ] 调用 `taskReadConfigFromDevice()` 后能收到 `RC_MSGID_GETDEVCFG_END`；
- [ ] 能收到约 40 ms 周期的 `RC_MSGID_MIXOUT_UPDATE`。

---

## 6. 核心概念

### 6.1 三种数据位置

所有配置操作涉及三个位置，理解它们是正确使用 SDK 的前提：

| 位置 | 说明 | 访问方式 |
| --- | --- | --- |
| **SDK 当前工作配置** | SDK 内存中的运行中配置 | `rcGet*` / `rcSet*` |
| **本地模型文件** | `appDataPath` 目录下的模型 JSON 文件 | `rcLoadModelFile`、`rcSaveCurrentCfgToModel` 等模型管理 API |
| **遥控器设备配置** | 设备实际生效的数据 | `taskReadConfigFromDevice` / `taskSaveConfigToDevice` |

关键结论：

- `rcSet*` 通常只修改**工作配置**或发送实时预览，**不等于**已保存到模型文件或设备；
- 要持久化到设备，必须调用 `taskSaveConfigToDevice()`；
- 要持久化到本地模型，调用 `rcSaveCurrentCfgToModel()` 或模型管理相关 API。

### 6.2 同步接口与异步任务

- **大多数 `rcGet*` / `rcSet*`**：把操作同步投递到 SDK 工作线程，函数返回时本次
  内存访问已经完成（但设备通信不一定完成）。
- **`rcTask*` / `task*` 接口**：只负责**启动**任务，设备通信、网络请求、升级过程
  异步完成，结果通过消息回调通知（如 `RC_MSGID_GETDEVCFG_END`、
  `RC_MSGID_FWUPDATE_PROGRESS`）。

不要用固定 `sleep` 代替完成消息——设备握手期间任务可能排队或延迟。

### 6.3 线程模型与回调纪律

```
宿主线程                SDK 内部
─────────              ─────────────────────────────
业务代码  ──API 调用──>  SDK 工作线程（处理 rcGet/rcSet/rcTask）
   ^                                          |
   |------- 输出分发线程 <--- 有界输出队列 <---+
        （调用宿主的 MessageCallback）
```

- 消息回调在 SDK 的**输出分发线程**执行，不在宿主主线程，也不在 SDK 工作线程；
- 回调必须**线程安全、短小、非阻塞**：先校验 `msgId`，把 payload 复制为宿主自己的
  值类型，再投递到宿主的任务队列/事件循环，然后立即返回；
- 严禁在回调中执行网络访问、磁盘 IO、长时间计算或等待锁；
- 不要在回调中直接操作要求固定线程访问的对象（如 Android UI 控件）。

### 6.4 实时数据的刷新节奏

- 设备原始数据更新约为 **40 ms** 一次，SDK 以 `RC_MSGID_MIXOUT_UPDATE` 通知宿主；
- 收到该消息后，可按一个 UI 节奏批量重新获取：
  `rcGetSrcCfgList()`、`rcGetChOutList()`、`rcGetTrimming()`、`rcGetRadioStatus()`；
- 不要用无间隔循环轮询实时 getter；UI 应根据页面可见性与渲染能力合并/节流更新。

---

## 7. 生命周期与通用接口

### 7.1 API 一览

```cpp
std::string_view rmaxSdk::version() noexcept;

rmaxSdk::InitResult rmaxSdk::initialize(std::string appDataPath) noexcept;

bool rmaxSdk::setMessageCallback(rmaxSdk::MessageCallback callback,
                                 void* userData = nullptr) noexcept;

void rmaxSdk::shutdown() noexcept;
```

### 7.2 各接口说明

**`version()`**
返回 SDK 内部静态构建日期/时间字符串的只读视图。`initialize()` 之前即可调用。
返回值指向静态存储，**不得释放**，不要长期持有（生命周期与动态库相同）。

**`initialize(appDataPath)`**
初始化 SDK：创建工作线程、输出分发线程、加载本地配置与模型。

- `appDataPath`：SDK 保存全局配置、模型、模板、升级缓存的目录，必须可读写。
  SDK 会在其中维护配置文件和子目录；**删除整个目录等价于“恢复本地出厂数据”**，
  下次初始化时重建默认配置。
- 返回值：

| 返回值 | 含义 |
| --- | --- |
| `InitResult::Success` | 初始化成功 |
| `InitResult::AlreadyInitialized` | SDK 已在运行，本次未重复创建运行时（按成功处理） |
| `InitResult::ThreadStartFailed` | 工作线程或输出线程启动失败 |
| `InitResult::CommunicationInitFailed` | 内部通信/业务对象初始化失败 |

- 该函数幂等；`shutdown()` 可重复调用；完整关闭后允许再次初始化。
- **除 `version()` 外，其他 API 都应在 `initialize()` 成功后调用**。未初始化时
  `setMessageCallback()` / `postMessage()` 返回 `false`（见测试源码中的校验）。

**`setMessageCallback(callback, userData)`**
注册/替换消息回调；传 `nullptr` 清除。`userData` 原样回传给回调，可用于携带宿主
上下文（必须为回调存活期间有效的地址）。初始化前调用返回 `false`。

**`shutdown()`**
停止 SDK 工作线程与输出线程，释放内部资源。重复调用安全。

### 7.3 推荐的退出顺序

1. 停止宿主继续发起 SDK 业务调用；
2. `setMessageCallback(nullptr, nullptr)` 清除消息回调；
3. 等待宿主自己排队的 SDK 消息处理完成；
4. 释放宿主持有的 `SdkMessage`、payload 及 SDK 返回的容器/字符串；
5. 调用 `shutdown()`；
6. 最后再卸载动态库（Android 上一般交由进程退出处理）。

---

## 8. 消息机制详解

### 8.1 消息结构

```cpp
struct SdkMessage {
    std::uint32_t msgId;                        // 消息 ID，决定 payload 类型
    std::shared_ptr<const void> payload;        // 载荷，可能为空
};

using MessageCallback = void (*)(const SdkMessage& message, void* userData);
```

- `msgId` 决定 `payload` 的真实类型，**转成错误类型属于未定义行为**；
- 回调中应先检查 `msgId` 和 `payload` 非空，再 `std::static_pointer_cast` 转换；
- 建议把 payload 内容**复制为值类型**再投递到其他线程，不要长期持有裸指针；
- 部分消息无 payload，payload 仅作为通知信号使用。

### 8.2 输出消息总表

消息 ID 从 `0x08000001` 开始连续递增（定义于 `rmAxSdkTypes.h` 的
`enum RcMessageId`）：

| 消息 ID | 值 | payload 类型 | 说明 |
| --- | --- | --- | --- |
| `RC_MSGID_LVTG_NOTIFY` | `0x08000001` | 无 | 遥控器低电压事件 |
| `RC_MSGID_PFC_NOTIFY` | `0x08000002` | `std::vector<std::int32_t>` | 预飞行检查各通道状态（长度以实际返回为准） |
| `RC_MSGID_TIM_REPORT` | `0x08000003` | `RcTimeReportUI` | 定时器报告（索引/方向/启动值/当前秒数） |
| `RC_MSGID_CNT_REPORT` | `0x08000004` | `RcTimeReportUI` | 计数器报告（索引/方向/初始值/当前计数值） |
| `RC_MSGID_TIM_NOTIFY` | `0x08000005` | `RcTimeNotifyUI` | 定时器提示事件（闹铃/整点等） |
| `RC_MSGID_TRIM_NOTIFY` | `0x08000006` | `std::uint8_t` | 微调值变化 |
| `RC_MSGID_SRCINPUT_UPDATE` | `0x08000007` | 未定义 | 保留消息 ID，当前勿使用 |
| `RC_MSGID_MIXOUT_UPDATE` | `0x08000008` | 无 | 输入/输出/微调/状态已刷新，重新调用 getter |
| `RC_MSGID_CFGTODEV_END` | `0x08000009` | 无 | 当前配置已写入设备（`taskSaveConfigToDevice` 完成） |
| `RC_MSGID_GETDEVCFG_END` | `0x0800000A` | 无 | 设备配置读取完成（`taskReadConfigFromDevice` 完成） |
| `RC_MSGID_RCTELEMETER_NOTIFY` | `0x0800000B` | 无 | 遥测通知队列非空，调用 `RCgetTelemetryItemNotify()` 排空 |
| `RC_MSGID_APPINFO_UPDATED` | `0x0800000C` | `AppUpdateInfoUI` | APP 更新信息已刷新 |
| `RC_MSGID_APPDOWNLOAD_PROGRESS` | `0x0800000D` | `AppUpdateInfoUI` | APP 下载字节进度 |
| `RC_MSGID_APPDOWNLOAD_END` | `0x0800000E` | `AppUpdateInfoUI` | APP 下载完成，读取 `cacheAppFile` |
| `RC_MSGID_FWVERSION_NOT_MATCH` | `0x0800000F` | 无 | 设备固件版本过低或不兼容 |
| `RC_MSGID_FWINF_UPDATED` | `0x08000010` | 无 | 固件信息已刷新，调用 `rcGetFwInf()` |
| `RC_MSGID_FWUPDATE_PROGRESS` | `0x08000011` | `std::int32_t` | `0..100` 进度；`-1` 无可用固件；`-2` 更新失败 |
| `RC_MSGID_FWFILE_DOWNLOAD_END` | `0x08000012` | 无 | 固件缓存文件下载完成 |
| `RC_MSGID_ELRS_SET_ACK` | `0x08000013` | `ElrsSetEndAckUI` | ELRS 菜单设置回复 |
| `RC_MSGID_ELRS_STATUS` | `0x08000014` | `ElrsStatusInfoUI` | ELRS 状态回复 |
| `RC_MSGID_ELRS_TELEMETRY` | `0x08000015` | `TelemetryDataUI` | ELRS 位置/姿态/电池快照 |
| `RC_MSGID_ELRS_DEVINF_UPDATE` | `0x08000016` | `std::string` | ELRS 设备名称 |
| `RC_MSGID_ELRS_MENUITEM_UPDATE` | `0x08000017` | `ElrsSetItemUI` | 一个 ELRS 菜单项（多次推送） |
| `RC_MSGID_CRSF_DATA_RECEIVED` | `0x08000018` | `std::vector<std::uint8_t>` | 完整 CRSF 帧 |
| `RC_MSGID_TELEMETRY_UPDATED` | `0x08000019` | 无 | 遥测列表已变化，按需重新获取 |
| `RC_MSGID_TELEMETRY_LOST` | `0x0800001A` | 无 | 接收机遥测丢失 |
| `RC_MSGID_TELEMETRY_RECOVER` | `0x0800001B` | 无 | 接收机遥测恢复 |

### 8.3 消息解析示例

```cpp
void onSdkMessage(const rmaxSdk::SdkMessage& message, void*)
{
    if (message.msgId == rmaxSdk::RC_MSGID_FWUPDATE_PROGRESS && message.payload) {
        const auto progress =
            std::static_pointer_cast<const std::int32_t>(message.payload);
        const std::int32_t value = *progress;   // 0..100 / -1 / -2
        // 投递 value 到宿主线程更新进度条
    }

    if (message.msgId == rmaxSdk::RC_MSGID_ELRS_STATUS && message.payload) {
        const auto status =
            std::static_pointer_cast<const rmaxSdk::ElrsStatusInfoUI>(message.payload);
        const rmaxSdk::ElrsStatusInfoUI snapshot = *status;  // 复制快照
        // 投递 snapshot
    }

    if ((message.msgId == rmaxSdk::RC_MSGID_TIM_REPORT
         || message.msgId == rmaxSdk::RC_MSGID_CNT_REPORT)
        && message.payload) {
        const auto report =
            std::static_pointer_cast<const rmaxSdk::RcTimeReportUI>(message.payload);
        const rmaxSdk::RcTimeReportUI snapshot = *report;
        // 投递 snapshot
    }
}
```

### 8.4 输入消息队列

```cpp
bool rmaxSdk::postMessage(rmaxSdk::SdkMessage message) noexcept;
```

- 把消息异步放入 SDK 输入队列；未初始化、正在关闭、队列已满或投递失败返回 `false`；
- 当前公开 API **没有定义具体输入消息业务协议**，返回 `true` 只表示消息进入队列，
  不表示存在对应业务处理器；
- 普通 SDK 功能应优先调用明确的公开 API，不要依赖 `postMessage` 实现业务。

---

## 9. 配置读取、修改与保存

### 9.1 相关 API

```cpp
void rmaxSdk::taskReadConfigFromDevice() noexcept;              // 从设备读取配置（异步）
void rmaxSdk::taskSaveConfigToDevice() noexcept;                // 保存配置到设备（异步）
void rmaxSdk::rcLoadModelFile(std::string modelFile) noexcept;  // 从文件加载模型配置
void rmaxSdk::rcSaveCurrentCfgToModel(rmaxSdk::RcModelDataUI rcModel) noexcept;
```

### 9.2 语义说明

| API | 行为 |
| --- | --- |
| `taskReadConfigFromDevice()` | 异步读取设备配置；完成时发布 `RC_MSGID_GETDEVCFG_END`，并把读取结果写入当前模型文件 |
| `taskSaveConfigToDevice()` | 异步写入设备；完成时发布 `RC_MSGID_CFGTODEV_END`，并保存当前模型文件 |
| `rcLoadModelFile()` | 从指定模型文件加载 SDK 当前工作配置 |
| `rcSaveCurrentCfgToModel()` | 把当前工作配置保存到参数指定的模型文件（“另存到某模型”） |

### 9.3 典型编辑流程

```cpp
// 1. 修改混控（只改工作配置/预览）
auto mix = rmaxSdk::rcGetMixCfgData(0, 0);
mix.weightUp = 80;
mix.weightDown = 80;
rmaxSdk::rcSetMixCfgData(mix);

// 2. 写入设备；完成后当前模型也会落盘
rmaxSdk::taskSaveConfigToDevice();
```

把当前配置保存到一个新模型：

```cpp
auto target = rmaxSdk::rcCreateModelCfg();
target.modelName = "My model";
rmaxSdk::rcSaveCurrentCfgToModel(target);
```

### 9.4 就绪判断

初始化后本地模型可立即读取，但**设备数据应等到 `RC_MSGID_GETDEVCFG_END` 后再视为
就绪**。启动握手期间任务可能排队或延迟，不要使用固定 sleep 代替完成消息。

---

## 10. 输入源、输出通道与微调

### 10.1 API 一览

```cpp
std::vector<rmaxSdk::RcSrcCfgUI>   rmaxSdk::rcGetSrcCfgList() noexcept;    // 输入源列表
std::vector<rmaxSdk::RcChOutCfgUI> rmaxSdk::rcGetChOutList() noexcept;    // 输出通道列表
rmaxSdk::RcTrimmingTopKeyUI        rmaxSdk::rcGetTrimming() noexcept;     // 微调值
rmaxSdk::RadioStatusUI             rmaxSdk::rcGetRadioStatus() noexcept;  // 基础状态
void rmaxSdk::rcSetChOutCfgData(rmaxSdk::RcChOutCfgUI chOutCfg) noexcept; // 更新输出通道配置
```

### 10.2 输入源索引速查

`rcGetSrcCfgList()` 返回的向量按输入源索引排列。头文件中注释给出的参考顺序如下
（以设备实际返回为准）：

| 索引 | 名称 | 说明 |
| --- | --- | --- |
| 0 | Rud | 航向（方向舵） |
| 1 | Ele | 升降（俯仰） |
| 2 | Thr | 油门 |
| 3 | Ail | 副翼（横滚） |
| 4–11 | P1–P8 | 会回中的旋钮 |
| 12–19 | S1–S8 | 不会回中的旋钮（滑块） |
| 20–35 | SA–SP | 开关 |
| 36–41 | CNT1–CNT6 | 计数器 |

> 参考源码 `tst_rmAxSdkApi.cpp` 中确认了前 4 项顺序：
> `items[0]=Rud`、`items[1]=Ele`、`items[2]=Thr`、`items[3]=Ail`。

### 10.3 使用要点

- `rcGetSrcCfgList()` 返回输入源名称、类型、原始值、逻辑值、输出值和权重；
  `rawVal` 为半量程刻度，乘 2 后映射为全量程（参考源码做法）；
- `rcGetChOutList()` 返回输出通道配置及实时值（`chOut`、`logicVal`）；
- `rcGetTrimming()` 返回 LV/LH/RV/RH 四个微调值；
- `rcGetRadioStatus()` 返回 RF 链路质量、电压、顶部按键与 ADC 校准状态；
- 修改输出通道时应**先读取、只改目标字段、再写回**，避免用未初始化字段覆盖其他配置：

```cpp
auto channels = rmaxSdk::rcGetChOutList();
if (!channels.empty()) {
    auto channel = channels.front();
    channel.weight = 80;
    channel.min = -100;
    channel.max = 100;
    rmaxSdk::rcSetChOutCfgData(channel);
}
```

---

## 11. 混控（Mixing）

### 11.1 API 一览

```cpp
rmaxSdk::RcMixCfgDataUI rmaxSdk::rcGetMixCfgData(int chIdx, int mixIdx) noexcept;
void  rmaxSdk::rcSetMixCfgData(rmaxSdk::RcMixCfgDataUI mixData) noexcept;
rmaxSdk::RcMixCfgDataUI rmaxSdk::rcAddMixCfgData(int chIdx, int srcIdx) noexcept;
int   rmaxSdk::rcDelMixCfgData(int chIdx, int srcIdx) noexcept;   // 成功返回剩余混控数，失败 -1
```

### 11.2 参数约定

- `chIdx`：输出通道索引；`mixIdx`：通道内混控项索引；`srcIdx`：输入源索引；
- **删除键是 `(chIdx, srcIdx)`，不是 `(chIdx, mixIdx)`**；
- `swCmpType` 比较条件：`0` 禁用、`1` 大于、`2` 等于、`3` 小于；
- `mixesMode` 混控类型：替换 / 叠加 / 相乘（`MIXED_MODE`，当前头文件以注释给出）；
- `slowMsUp` / `slowMsDown`：上/下慢放时间（毫秒），`0` 表示不慢放；
- `weightUp` / `weightDown`：上/下比例。

### 11.3 示例

```cpp
// 为通道 0 添加输入源 1（Ele）的混控
auto added = rmaxSdk::rcAddMixCfgData(0, 1);
added.weightUp = 100;
added.weightDown = 100;
rmaxSdk::rcSetMixCfgData(added);

// 删除（按 chIdx + srcIdx）
const int remaining = rmaxSdk::rcDelMixCfgData(0, added.srcIdx);
```

---

## 12. 曲线（Curve）

### 12.1 API 一览

```cpp
std::vector<rmaxSdk::RcCurveCfgDataUI> rmaxSdk::rcGetCurveList(int idx = -1) noexcept;
int      rmaxSdk::rcSetCurveCfg(const rmaxSdk::RcCurveCfgDataUI& curveCfg) noexcept;
double   rmaxSdk::rcCurveApply(int curveIdx, double rawVal) noexcept;
```

### 12.2 说明

- `rcGetCurveList(-1)` 返回全部曲线；传入具体索引只返回该曲线；
- 当前模型默认有 **18 条曲线**，调用方仍应以实际返回列表为准；
- `rcSetCurveCfg()` 成功返回 `0`，无效配置或调用失败返回 `-1`；
- `rcCurveApply()` 失败或索引无效时原样返回 `rawVal`；
- `curveIdx == 0xff` 表示“不应用曲线”；
- `xVal` / `yVal` 是固定长度 **12** 的数组，`ptNum` 不得超过数组容量；
- 曲线类型 `cType` 参考（头文件注释）：`0` 无、`1` DIFF（微调大于 0 控制
  0→+100 比例，小于 0 控制 0→-100 比例）、`2` EXPO、`3` FUNC、`4` 自定义带点曲线。

### 12.3 示例

```cpp
auto curves = rmaxSdk::rcGetCurveList(0);
if (!curves.empty()) {
    auto curve = curves.front();
    curve.cType = 4;          // 自定义曲线
    curve.ptNum = 3;
    curve.xVal[0] = -100; curve.yVal[0] = -100;
    curve.xVal[1] = 0;    curve.yVal[1] = 0;
    curve.xVal[2] = 100;  curve.yVal[2] = 100;
    const int result = rmaxSdk::rcSetCurveCfg(curve);   // 0 成功
}
```

---

## 13. 双比率 DR

### 13.1 API 一览

```cpp
rmaxSdk::RcChCfgDrUI rmaxSdk::rcGetDrData(int chIdx, int idx) noexcept;
int rmaxSdk::rcSetDrData(const rmaxSdk::RcChCfgDrUI& drCfg) noexcept;
int rmaxSdk::rcDelDrData(const rmaxSdk::RcChCfgDrUI& drCfg) noexcept;
```

### 13.2 说明

- 每个通道当前有 **3 个 DR 槽位**；
- `rcGetDrData(chIdx, -1)` 用于查找一个空闲槽位；
- `rcSetDrData()` 成功时返回该通道已使用的 DR 数量，调用失败返回 `-1`；
- **已知限制**：当前实现中的 `rcDelDrData()` 只返回 `0`，尚未实际清除 DR 槽位；
  调用方不要把返回 `0` 当作删除已持久生效；
- 通道锁定字段（`lockChSwIdx` / `lockChSwVal` / `lockChSwType` / `lockChVal`）位于
  `RcChOutCfgUI`，通过 `rcSetChOutCfgData()` 更新。

---

## 14. 定时器与计数器

### 14.1 API 一览

```cpp
rmaxSdk::RcTimeCfgDataUI rmaxSdk::rcGetTimerCfgData(int timerIdx, int isCounter) noexcept;
void rmaxSdk::rcSetTimerCfgData(const rmaxSdk::RcTimeCfgDataUI& timerCfg, int isCounter) noexcept;
void rmaxSdk::rcResetTimer(int timerIdx) noexcept;
```

### 14.2 索引与语义

- `isCounter == 0` 操作定时器数组；`isCounter != 0` 操作计数器数组；
- 当前每组索引为 `0..2`（即定时器 0–2、计数器 0–2；按设备协议编号亦可表述为
  设备定时器编号 0–2、计数器 3–5），**调用方必须先校验索引**；
- `timerCfg.timeIdx` 是写入时使用的组内索引；
- `direction`：`1` 正计时，`0` 倒计时；
- `startTime == 0` 表示正计时；大于 `0` 表示倒计时周期（秒）；计数器模式下该字段
  表示计数器最大值，**不能为 0**；
- `swCmpType` / `rstSwCmpType` / `cmpType` 比较条件统一为：
  `0` 禁用、`1` 大于、`2` 等于、`3` 小于；
- `outputMin` / `outputMax` 范围 `-100..100`；
- 定时器设置会发送实时预览；计数器设置更新当前工作配置；
- `rcResetTimer()` 发送设备复位命令，参数使用设备定时器编号。

### 14.3 运行期消息

- `RC_MSGID_TIM_REPORT` / `RC_MSGID_CNT_REPORT`：payload 均为 `RcTimeReportUI`。
  `direction == 0` 显示剩余量，`1` 显示已用量；`timeCount` 对定时器是当前秒数，
  对计数器是当前计数值；
- `RC_MSGID_TIM_NOTIFY`：payload 为 `RcTimeNotifyUI`，包含索引、运行状态、提示码、
  当前计数。前三个字段是 `std::uint8_t`，打印时先转 `int`，避免被流输出当字符处理。

### 14.4 示例

```cpp
// 配置定时器 0：60 秒倒计时，每 10 秒提示
auto timer = rmaxSdk::rcGetTimerCfgData(0, 0);
timer.timeIdx = 0;
timer.startTime = 60;
timer.beepTimeSec = 10;
rmaxSdk::rcSetTimerCfgData(timer, 0);

// 处理提示消息
if (message.msgId == rmaxSdk::RC_MSGID_TIM_NOTIFY && message.payload) {
    const auto notify =
        std::static_pointer_cast<const rmaxSdk::RcTimeNotifyUI>(message.payload);
    const int timerIndex = static_cast<int>(notify->timeIdx);
    const bool running   = notify->isRunning != 0;
    const int code       = static_cast<int>(notify->notifyCode);
    const std::int32_t count = notify->timeCount;
}
```

---

## 15. 系统配置

```cpp
rmaxSdk::RcSysCfgDataUI rmaxSdk::rcGetRcSysConfig() noexcept;
void rmaxSdk::rcSetRcSysConfig(const rmaxSdk::RcSysCfgDataUI& rcSysCfg) noexcept;
```

**当前实现实际读写**的字段：

- `alramVtg`：报警电压（单位 0.1 V）；
- `rfModuleMode`：RF 模块模式（`0` 内部 ELRS、`1` 外部 ELRS、`2`/`0xff` 关闭）；
- `stickMode`：摇杆模式（1 或 2）；
- `preFlightCheckChannel[36]`：预飞行检查各通道预设值（`127` 表示不检查）。

`RcSysCfgDataUI` 中其余字段（`fsMode`、`fsChVal`、`joytickBeep`、`modelMatchId`、
`crsfBaudRate`）目前**没有**通过这两个 API 完整持久读写，调用方不要假设其生效。

模型匹配值应通过 `RcModelDataUI::modelMatch` 与模型管理 API 更新。

---

## 16. 设备控制

```cpp
void rmaxSdk::rcResetDefault() noexcept;               // 恢复默认配置并触发重新读取
void rmaxSdk::rcSkipPreFlightCheck() noexcept;         // 跳过当前预飞行检查
void rmaxSdk::rcSetAdcCalibration(int isStart) noexcept; // 非零开始 ADC 校准，0 停止
void rmaxSdk::rcSetUsbToVcp(std::uint8_t mode) noexcept; // USB 虚拟串口透传模式
void rmaxSdk::rcRebootRFModule() noexcept;             // 请求重启 RF 模块
```

| API | 用途 |
| --- | --- |
| `rcResetDefault()` | 恢复默认配置并触发重新读取（谨慎使用，会覆盖当前工作配置） |
| `rcSkipPreFlightCheck()` | 跳过当前一次预飞行检查警报 |
| `rcSetAdcCalibration(isStart)` | 开始/停止摇杆 ADC 校准；当前状态见 `rcGetRadioStatus().adcSetState` |
| `rcSetUsbToVcp(mode)` | 设置 USB 虚拟串口透传模式，取值由设备协议定义 |
| `rcRebootRFModule()` | 请求重启 RF（ELRS）模块 |

---

## 17. 模型与模板管理

### 17.1 API 一览

```cpp
std::list<rmaxSdk::RcModelDataUI> rmaxSdk::rcGetRcModelList() noexcept;      // 模型列表
rmaxSdk::RcModelDataUI rmaxSdk::rcCreateModelCfg() noexcept;                 // 新建模型元数据
rmaxSdk::RcModelDataUI rmaxSdk::rcSetModelCfg(rmaxSdk::RcModelDataUI rcModel) noexcept;
void  rmaxSdk::rcSaveCurrentCfgToModel(rmaxSdk::RcModelDataUI rcModel) noexcept;
void  rmaxSdk::rcLoadModelFile(std::string modelFile) noexcept;
void  rmaxSdk::rcExportModelCfg(std::string modelName, std::string modelFile) noexcept;
int   rmaxSdk::rcImportModelCfg(std::string importFile, std::string modelFile) noexcept;
void  rmaxSdk::rcDeleteModel(std::string filePath) noexcept;
int   rmaxSdk::rcGetCurrentModelCfgIndex() noexcept;                         // 失败返回 -1
std::list<rmaxSdk::RcModelDataUI> rmaxSdk::rcGetTemplateList() noexcept;     // 模板列表
void  rmaxSdk::rcModelToTemplate(std::string modelFile, std::string tempName) noexcept;
```

`RcModelDataUI` 字段：`modelName`（模型名）、`modelImg`（图片路径/资源）、
`filePath`（文件路径）、`modelIdx`（索引）、`modelMatch`（ELRS MODELMATCH 编号）。

### 17.2 行为说明

| API | 当前行为 |
| --- | --- |
| `rcGetRcModelList()` | 扫描模型目录并返回模型列表 |
| `rcCreateModelCfg()` | 生成新模型元数据和目标路径；**不会立即创建文件** |
| `rcSetModelCfg()` | 加载目标文件、更新元数据并保存；用于已有模型元数据修改 |
| `rcSaveCurrentCfgToModel()` | 把当前工作配置和传入元数据写到目标模型 |
| `rcLoadModelFile()` | 把目标模型加载为当前工作配置 |
| `rcExportModelCfg(source, destination)` | 复制模型文件到导出路径 |
| `rcImportModelCfg(source, target)` | 把来源配置导入目标模型，成功返回 `0` |
| `rcDeleteModel()` | 删除指定模型文件 |
| `rcGetCurrentModelCfgIndex()` | 返回当前模型索引，失败返回 `-1` |
| `rcGetTemplateList()` | 返回模板列表 |
| `rcModelToTemplate()` | 把模型保存为指定名称的模板 |

### 17.3 示例

```cpp
// 新建模型并保存当前配置
auto model = rmaxSdk::rcCreateModelCfg();
model.modelName = "Plane";
model.modelImg = "qrc:/image/plane.png";
model.modelMatch = 1;
rmaxSdk::rcSaveCurrentCfgToModel(model);
```

> 只修改已有模型的名称/图片时使用 `rcSetModelCfg()`；**不要**把 `rcSetModelCfg()`
> 当成“保存当前工作配置”的替代品——它主要用于模型元数据修改。

---

## 18. 遥测与通知

### 18.1 遥测列表

```cpp
std::list<rmaxSdk::TelemetryNotifyUI> rmaxSdk::rcGetTelemetryList(int uiOnly) noexcept;
```

- `uiOnly == 0`：返回全部遥测项目；
- `uiOnly != 0`：只返回 `showUI != 0` 的项目；
- `RC_MSGID_TELEMETRY_UPDATED` 可能高频出现，**不应每条消息都刷新完整 UI**；
- 实际项目建议：后台保存最新状态；UI 列表刷新做节流（例如 500 ms–1.5 s）；
  页面不可见时停止刷新；显式用户请求可绕过节流立即获取。

### 18.2 通知配置

```cpp
rmaxSdk::TelemetryNotifyUI rmaxSdk::rcGetNotifyCfgItem(std::uint64_t hashValue, int index) noexcept;
void rmaxSdk::rcSetNotifyCfgItem(std::uint64_t hashValue,
                                 const rmaxSdk::TelemetryNotifyUI& notifyCfg,
                                 int index) noexcept;
void rmaxSdk::rcDelNotifyItem(int index) noexcept;
std::list<rmaxSdk::TelemetryNotifyUI> rmaxSdk::rcGetNotifyCfgList() noexcept;
```

`rcSetNotifyCfgItem()` 的判定规则：

- `hashValue == 0`：**新增**通知，`index` 不参与定位；
- `hashValue != 0`：按 `index` **修改**已有通知，调用方必须保证索引有效；
- 新增时可传 `index == -1` 表达调用方意图，但真正决定新增的是第一个参数。

> **重要**：`hashValue` 是 `std::uint64_t`。业务层或脚本绑定**不得用 32 位 `int`
> 保存**，否则会截断；目标语言若不能精确表示 64 位整数，优先使用字符串。

### 18.3 触发通知队列

```cpp
int rmaxSdk::RCgetTelemetryItemNotify(rmaxSdk::TelemetryNotifyUI& notifyItem) noexcept;
```

`RC_MSGID_RCTELEMETER_NOTIFY` 只表示通知队列非空，**不携带通知 payload**。收到消息后
应循环取出：

```cpp
rmaxSdk::TelemetryNotifyUI item{};
while (rmaxSdk::RCgetTelemetryItemNotify(item) != -1) {
    // 复制 item，执行语音、弹窗或日志
}
```

成功时返回出队前的队列长度；队列为空或调用失败时返回 `-1`（参考源码使用 `> 0`
作为循环条件，同样安全）。

### 18.4 遥测公共类型的选择

`rmAxSdkTypes.h` 同时声明了 `TelemetryNotifyUI` 和 `TelemetryItemUI`。当前公开 API
（`rcGetTelemetryList`、`rcGetNotifyCfgItem`、`rcSetNotifyCfgItem`、
`rcGetNotifyCfgList`、`RCgetTelemetryItemNotify`）**均使用 `TelemetryNotifyUI`**。

`TelemetryItemUI` 当前没有对应的公开 API 参数、返回值或消息 payload，属于预留公共
数据结构，不要替换为上述接口的参数类型。

---

## 19. ELRS 与 CRSF

### 19.1 API 一览

```cpp
void rmaxSdk::rcTaskRequestElrsMenuList(int reqSetDevID) noexcept; // 请求 ELRS 参数菜单
void rmaxSdk::rcElrsSendMenuCmd(int idx, int val) noexcept;        // 修改菜单项
int  rmaxSdk::rcRequestElrsStatus() noexcept;                      // 请求 ELRS 状态
void rmaxSdk::rcSendCrsfRawData(std::vector<std::uint8_t> data) noexcept; // 发送原始帧
```

### 19.2 菜单浏览流程

`rcTaskRequestElrsMenuList(reqSetDevID)` 异步请求指定 ELRS 设备的参数菜单。常用
发射机地址为 `0xEE`（CRSF_ADDRESS_CRSF_TRANSMITTER），调用方应以实际 CRSF 设备
地址为准。

响应顺序通常为：

1. `RC_MSGID_ELRS_DEVINF_UPDATE`：设备名称（`std::string`）；
2. 多个 `RC_MSGID_ELRS_MENUITEM_UPDATE`：每次推送一个 `ElrsSetItemUI`；
3. 宿主按用户操作调用 `rcElrsSendMenuCmd(idx, val)` 修改；
4. `RC_MSGID_ELRS_SET_ACK`：设置回复（`ElrsSetEndAckUI`）。

注意：离开 ELRS 页面后应停止宿主自己的重复刷新/请求。菜单请求进行中继续发起请求
会被内部状态忽略，频繁重复拉取也会造成页面卡顿。

### 19.3 ELRS 状态

`rcRequestElrsStatus()` 返回值：

- `0`：请求已发送；
- `-1`：ELRS 菜单请求正在进行，或 SDK 不可用/调用失败。

成功发送不代表已收到响应。响应通过 `RC_MSGID_ELRS_STATUS` 返回，payload 为
`ElrsStatusInfoUI`：

- `badPkt`：坏包数；
- `goodPkt`：好包数；
- `modelMismatch`：模型匹配状态，非零表示不匹配。

### 19.4 CRSF 原始数据

`rcSendCrsfRawData()` 发送调用方提供的原始字节，空数组不会发送。

`RC_MSGID_CRSF_DATA_RECEIVED` 返回完整帧，格式：

```text
ADDR LEN TYPE DATA CRC
```

总字节数应为 `LEN + 2`，CRC8-D5 覆盖 `TYPE + DATA`。

---

## 20. 设备固件升级

### 20.1 API 一览

```cpp
rmaxSdk::FirmwareInfUI rmaxSdk::rcGetFwInf() noexcept;      // 返回当前固件信息
void  rmaxSdk::rcTaskRequestDeviceInf() noexcept;           // 请求设备信息
void  rmaxSdk::rcTaskStartUpdateFw() noexcept;              // 开始固件更新
```

`FirmwareInfUI` 包含系统 ID（`SysId`）、设备唯一 ID（`DevId`）、总飞行时间
（`FlyTime`，秒）、构建日期（`BuildTime`，YYMMDD）、固件版本（`VER_FW`，
`0x01020003` → `01.02.0003`）、硬件版本（`VER_HW`，`0x00000102` → `01.02`）、
设备描述、版本字符串与缓存固件版本文本。

### 20.2 标准流程

1. 调用 `rcTaskRequestDeviceInf()`；
2. 收到 `RC_MSGID_FWINF_UPDATED`；
3. 调用 `rcGetFwInf()` 获取设备与缓存固件信息；
4. 按业务条件（版本比较、用户确认）调用 `rcTaskStartUpdateFw()`；
5. 处理 `RC_MSGID_FWUPDATE_PROGRESS`。

固件更新进度值（`std::int32_t`）：

- `0..100`：更新进度百分比；
- `-1`：没有可用固件；
- `-2`：更新失败。

相关消息还有：`RC_MSGID_FWVERSION_NOT_MATCH`（设备固件版本过低或不兼容，升级前
检查失败）与 `RC_MSGID_FWFILE_DOWNLOAD_END`（固件缓存文件下载完成）。

---

## 21. APP 升级

### 21.1 API 一览

```cpp
rmaxSdk::AppUpdateInfoUI rmaxSdk::rcGetAppUpdateInfo() noexcept;
void  rmaxSdk::rcTaskGetAppInfoFromWeb() noexcept;
int   rmaxSdk::rcTaskUpdateApp(std::string url) noexcept;
void  rmaxSdk::rcAbortUpdateApp() noexcept;
```

`AppUpdateInfoUI` 字段：`downloadBytesReceived` / `downloadBytesTotal`（下载进度）、
`apkUrl`、`version`、`cnUpdateContent` / `enUpdateContent`（更新说明）、
`cacheAppFile`（下载完成的本地缓存文件路径）。

### 21.2 标准流程

1. 调用 `rcTaskGetAppInfoFromWeb()`；
2. 收到 `RC_MSGID_APPINFO_UPDATED`，读取 payload 或调用 `rcGetAppUpdateInfo()`；
3. 调用 `rcTaskUpdateApp(info.apkUrl)` 启动下载；
4. 处理 `RC_MSGID_APPDOWNLOAD_PROGRESS`（可轮询 `rcGetAppUpdateInfo()` 获取字节进度）；
5. 收到 `RC_MSGID_APPDOWNLOAD_END` 后读取 `cacheAppFile`，由宿主执行安装
   （如触发 Android 包安装器）。

`rcTaskUpdateApp()` 返回值：`0` 下载任务成功启动；`-1` 已有下载任务、SDK 不可用
或调用失败。`rcAbortUpdateApp()` 中止正在运行的下载。

---

## 22. 数据类型参考

以下结构体均定义于 `rmAxSdkTypes.h`，命名空间 `rmaxSdk`。默认值以头文件为准。

### 22.1 输入源配置 `RcSrcCfgUI`

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `srcName` | `std::string` | 输入源名称（如 "Ail"） |
| `srcIdx` | `int` | 输入通道索引 |
| `srcType` | `int` | 输入通道类型 |
| `isEnable` | `int` | 是否使能显示 |
| `rawVal` | `int` | 原始值（半量程，乘 2 为全量程） |
| `rawLogicVal` | `int` | 逻辑值 |
| `outputVal` | `int` | 输出值 |
| `weight` | `int` | 权重 |

### 22.2 微调 `RcTrimmingTopKeyUI`

| 字段 | 说明 |
| --- | --- |
| `trimLV` | 左垂直微调 |
| `trimLH` | 左水平微调 |
| `trimRV` | 右垂直微调 |
| `trimRH` | 右水平微调 |

### 22.3 输出通道配置 `RcChOutCfgUI`

| 字段 | 说明 |
| --- | --- |
| `chIdx` | 通道索引 |
| `middle` | 中点（±100%） |
| `min` / `max` | 最小/最大值（±100%） |
| `curveIdx` | 曲线类型/索引 |
| `mixesNum` | 混控数量 |
| `weight` | 权重 |
| `chOut` | 通道输出值（实时） |
| `logicVal` | 逻辑值（实时，`float`） |
| `lockChSwIdx` / `lockChSwVal` / `lockChSwType` / `lockChVal` | 通道锁定：开关索引/开关值/开关类型/锁定通道值 |
| `dr1SwIdx` / `dr1SwAct` / `dr1Weight` | 双比例 1：开关索引/开关动作/权重 |
| `dr2SwIdx` / `dr2SwAct` / `dr2Weight` | 双比例 2 |
| `dr3SwIdx` / `dr3SwAct` / `dr3Weight` | 双比例 3 |

### 22.4 混控配置 `RcMixCfgDataUI`

| 字段 | 默认值 | 说明 |
| --- | --- | --- |
| `srcName` | — | 输入源名称 |
| `srcType` | 0 | 输入源类型 |
| `srcIdx` | 0 | 输入源索引 |
| `offset` | 0 | 偏移（±100%） |
| `curveIdx` | 0 | 曲线索引 |
| `swCmpType` | 0 | 使能开关比较条件：0 禁用 / 1 大于 / 2 等于 / 3 小于 |
| `switchIdx` | 0 | 使能开关索引 |
| `swCmpVal` | 0 | 使能开关比较值 |
| `mixesMode` | 0 | 混控类型（替换/叠加/相乘） |
| `chIdx` | 0 | 输出通道索引 |
| `mixIdx` | 0 | 通道内混控索引 |
| `slowMsUp` | 0 | 上慢放（毫秒，0 不慢放） |
| `slowMsDown` | 0 | 下慢放（毫秒，0 不慢放） |
| `weightUp` | 0 | 上比例 |
| `weightDown` | 0 | 下比例 |
| `mixesCount` | 0 | 该通道混控数量 |

### 22.5 系统配置 `RcSysCfgDataUI`

| 字段 | 说明 |
| --- | --- |
| `fsMode` | 失控保护模式：0 无 / 1 Hold（保持最后值）/ 2 No pulses（无输出）/ 3 Custom（自定义） |
| `fsChVal[16]` | 自定义失控通道输出值（±100%） |
| `stickMode` | 摇杆模式（1 或 2） |
| `preFlightCheckChannel[36]` | 预飞行检查预设值：127 不检查；±100 为预设值，偏差过大即告警 |
| `rfModuleMode` | 0 内部 ELRS / 1 外部 ELRS / 2 / 0xff 关闭 |
| `alramVtg` | 报警电压（0.1 V） |
| `joytickBeep` | 摇杆蜂鸣器 |
| `modelMatchId` | ELRS MODELMATCH 编号 |
| `crsfBaudRate` | 1:400K / 2:921600 / 3:1870000 |

> 当前 `rcGetRcSysConfig` / `rcSetRcSysConfig` 只实际映射 `alramVtg`、
> `rfModuleMode`、`stickMode`、`preFlightCheckChannel`，其余字段见第 15 章说明。

### 22.6 定时器配置 `RcTimeCfgDataUI`

| 字段 | 默认值 | 说明 |
| --- | --- | --- |
| `mode` | 0 | 工作模式 |
| `swCmpType` | 0 | 使能开关比较条件（0/1/2/3） |
| `switchIdx` | 0 | 使能开关索引 |
| `switchAct` | 0 | 使能开关动作 |
| `startTime` | 0 | 0 正计时；>0 倒计时周期（秒）；计数器模式下为最大值 |
| `direction` | 0 | 计时方向：1 正计时 / 0 倒计时 |
| `countdown` | 0 | 计数提示模式（静音/蜂鸣/语音/震动及组合） |
| `beepTimeSec` | 0 | 提示间隔（如 5/10/20/30 秒） |
| `rstSwCmpType` | 0 | 重置开关比较条件 |
| `resetSwIdx` | 0 | 重置开关索引 |
| `resetSwAct` | 0 | 重置开关动作 |
| `cmpValue` | 0 | 通道激活模式的输入比较值 |
| `cmpType` | 0 | 比较器类型：0 禁用 / 1 大于 / 2 等于 / 3 小于 |
| `loopMode` | 0 | 循环模式 |
| `outputMode` | 0 | 输出模式（按阈值在 outputMin/Max 间映射） |
| `outputMin` / `outputMax` | 0 | 输出范围（±100%） |
| `threshold` | 0 | 比较器阈值 |
| `timeIdx` | 0 | 组内索引（写入时使用） |
| `trDirection` | 0 | 运行方向（回读） |
| `trStartTime` | 0 | 启动值（回读） |
| `timeCount` | 0 | 当前计数（回读） |
| `notifyCode` | 0 | 提示信息（回读） |

### 22.7 定时器/计数器消息载荷

```cpp
struct RcTimeReportUI {
    int timeIdx = 0;          // 组内索引
    int direction = 0;        // 0 显示剩余量 / 1 显示已用量
    std::int32_t startTime = 0;  // 启动值；0 正计时，>0 倒计时
    std::int32_t timeCount = 0;  // 定时器为秒数；计数器为当前计数值
};

struct RcTimeNotifyUI {
    std::uint8_t timeIdx;     // 定时器索引（打印前转 int）
    std::uint8_t isRunning;   // 非零表示运行中
    std::uint8_t notifyCode;  // 设备上报的提示代码
    std::int32_t timeCount = 0;  // 触发提示时的当前计数
};
```

### 22.8 DR 配置 `RcChCfgDrUI`

| 字段 | 说明 |
| --- | --- |
| `chIdx` | 通道编号 |
| `idx` | DR 编号（槽位） |
| `drCount` | DR 数量 |
| `swCmpType` | 使能开关比较条件（0/1/2/3） |
| `swSrcIdx` | 使能开关 |
| `swCmpVal` | 使能开关比较值 |
| `drWeight` | DR 权重 |

### 22.9 曲线配置 `RcCurveCfgDataUI`

| 字段 | 说明 |
| --- | --- |
| `idx` | 曲线索引 |
| `cType` | 曲线类型：0 无 / 1 DIFF / 2 EXPO / 3 FUNC / 4 自定义带点曲线 |
| `ptNum` | 曲线点数量（≤ 12） |
| `xVal[12]` / `yVal[12]` | 曲线点坐标（固定长度 12） |

### 22.10 模型元数据 `RcModelDataUI`

| 字段 | 说明 |
| --- | --- |
| `modelName` | 模型名 |
| `modelImg` | 模型图片（路径或资源 URI） |
| `filePath` | 模型文件路径 |
| `modelIdx` | 模型索引（`int64_t`） |
| `modelMatch` | ELRS MODELMATCH 编号 |

### 22.11 固件信息 `FirmwareInfUI`

| 字段 | 说明 |
| --- | --- |
| `SysId` | 32 位系统 ID |
| `DevId` | 32 位设备唯一 ID |
| `FlyTime` | 总飞行时间（秒） |
| `BuildTime` | 固件构建日期（YYMMDD） |
| `VER_FW` | 固件版本：`0x01020003` → `01.02.0003` |
| `VER_HW` | 硬件版本：`0x00000102` → `01.02` |
| `DevText` | 设备描述字符串 |
| `FwVerText` | 固件版本字符串 |
| `BulidTimeText` | 固件构建时间文本 |
| `cacheFwFileVerTxt` | 已下载缓存固件的版本字符串 |

### 22.12 APP 更新信息 `AppUpdateInfoUI`

| 字段 | 说明 |
| --- | --- |
| `downloadBytesReceived` / `downloadBytesTotal` | 已下载/总字节数 |
| `apkUrl` | APK 下载地址 |
| `version` | 最新版本号 |
| `cnUpdateContent` / `enUpdateContent` | 中/英文更新说明 |
| `cacheAppFile` | 下载完成后的本地缓存文件 |

### 22.13 ELRS 类型

`ElrsSetItemUI`（菜单项）：

| 字段 | 说明 |
| --- | --- |
| `idx` / `parentIdx` | 自身/父项索引 |
| `val` | 当前值 |
| `type` | 项类型 |
| `itemEnabled` / `itemVisible` | 是否可用/可见 |
| `name` | 项名称 |
| `options` | 候选值列表（选项型项） |
| `deviceName` | 设备名称 |
| `units` | 单位 |

`ElrsSetEndAckUI`：`type` / `status` / `lastSetIdx` / `options`。

`ElrsStatusInfoUI`：`badPkt`（坏包数）、`goodPkt`（好包数）、
`modelMismatch`（非零表示模型不匹配）。

### 22.14 遥测类型

`TelemetryDataUI`（ELRS 回传快照）：

| 字段 | 说明 |
| --- | --- |
| `lat` / `lng` | 经纬度 |
| `pitch` / `roll` / `yaw` | 姿态角 |
| `voltage` / `current` | 电压 / 电流 |

`RadioStatusUI`（遥控器基础状态）：

| 字段 | 默认值 | 说明 |
| --- | --- | --- |
| `rfLQ` | 0 | 接收机链路质量（LQ，接收机回传） |
| `rfLDBM` | 0 | 接收机信号强度 dBm（回传） |
| `rfLLostCnt` | 0 | 数据丢失计数器；=0 表示接收机丢失 |
| `rcVtg` | 0 | 遥控器电压（`float`） |
| `rxVtg` | 0 | 飞机/接收机回传电压（`float`） |
| `topKeyState` | 0 | 顶部按键状态 |
| `adcSetState` | 0 | ADC 校准状态 |

`TelemetryNotifyUI`（遥测项与通知配置通用类型）：

| 字段 | 说明 |
| --- | --- |
| `hashValue` | 关联回传项哈希（`uint64_t`，勿用 32 位保存） |
| `notifyName` / `notifyText` | 通知名称/文本 |
| `popupWindow` | 是否弹窗 |
| `triggerType` | 触发类型：1 大于 / 2 等于 / 3 小于 |
| `intervalSec` | 通知间隔（秒） |
| `triggerValue` / `currValue` | 触发值/当前值（`double`） |
| `valueUnit` | 值单位 |
| `audioFile` | 提示音频文件 |
| `valTxt` / `nameTxt` | 值文本/名称文本 |
| `idx` | 索引 |
| `showUI` | 是否在 UI 显示 |

> `TelemetryItemUI` 为预留结构，当前无公开 API 使用（见 18.4）。

### 22.15 消息基础设施

```cpp
enum RcMessageId : std::uint32_t { /* 见 8.2 消息总表 */ };

struct SdkMessage {
    std::uint32_t msgId;
    std::shared_ptr<const void> payload;
};
```

---

## 23. 返回值与错误处理约定

所有公开 API 均为 `noexcept`，**异常不会越过动态库边界**。

| API 类型 | SDK 不可用或调用失败时 |
| --- | --- |
| `std::vector` / `std::list` getter | 空容器 |
| 结构体 getter | 值初始化的默认结构 |
| `rcGetCurrentModelCfgIndex()` | `-1` |
| `rcDelMixCfgData()` | `-1` |
| `rcSetDrData()` / `rcDelDrData()` | `-1` |
| `rcSetCurveCfg()` | `-1` |
| `rcImportModelCfg()` | `-1` |
| `rcTaskUpdateApp()` | `-1` |
| `rcRequestElrsStatus()` | `-1` |
| `RCgetTelemetryItemNotify()` | `-1` |
| `rcCurveApply()` | 原样返回 `rawVal` |
| `initialize()` | 返回 `InitResult` 枚举 |
| `setMessageCallback()` / `postMessage()` | `false` |
| `void` 设置/任务 API | 不执行，无法通过返回值获知失败 |

注意：

- 空容器或默认结构既可能表示“确实没有数据”，也可能表示失败，应结合生命周期状态、
  设备完成消息和业务状态综合判断；
- 索引、路径、URL 和协议参数由调用方校验。部分内部实现直接按索引访问数组，
  **越界参数不是可恢复的业务错误**。

---

## 24. 最佳实践

1. **设备就绪前不要把 getter 当作设备真值**：`initialize()` 后本地模型可立即读取，
   设备配置须等 `RC_MSGID_GETDEVCFG_END`。
2. **回调不直接操作 UI**：校验消息 → 复制 payload 为宿主值类型 → 投递到宿主线程
   队列/事件循环 → 立即返回。禁止网络、磁盘 IO、等锁、长计算。
3. **高频消息必须合并或节流**：`RC_MSGID_MIXOUT_UPDATE` 约 40 ms 一次；
   `RC_MSGID_TELEMETRY_UPDATED` 可能按遥测帧持续出现。用最新值覆盖 + 定时批量刷新，
   页面不可见时暂停 UI 更新。
4. **保存语义要区分**：`rcSet*` 改工作配置/预览；`rcSaveCurrentCfgToModel(model)`
   保存到指定本地模型；`taskSaveConfigToDevice()` 异步写设备（完成后当前模型也会
   保存）；`rcSetModelCfg(model)` 只改已有模型元数据。
5. **大整数跨绑定层不用 32 位 int**：`hashValue` 是 64 位，脚本绑定层用 64 位整数
   或字符串。
6. **只使用公开 payload 类型**：定时器/计数器消息用 `RcTimeReportUI`、
   `RcTimeNotifyUI`；只包含公开头文件，不要引用或复制内部协议结构。
7. **动态库卸载前释放跨边界对象**：回调已清除、SDK 已关闭、宿主持有的
   `SdkMessage`、payload、SDK 返回容器和字符串均已销毁，再卸载动态库。
8. **Android STL 一致性**：全工程唯一 `libc++_shared.so`，与分发包提供的一致；
   ABI 只保留 arm64-v8a。
9. **修改配置先读后写**：`rcGet*` 取完整结构 → 只改目标字段 → `rcSet*` 写回，
   避免默认构造的局部变量覆盖其他配置项。
10. **ELRS 页面遵守请求节奏**：菜单拉取完成后展示；设置等 `ELRS_SET_ACK`；
    离开页面停止重复请求。

---

## 25. 常见问题（FAQ）

**Q1：加载 `librmAxSdk1.so` 时报 `dlopen failed: library "libQt6Core_arm64-v8a.so" not found`？**
APK 中缺少 Qt 运行时库。请确认 7 个 `.so` 全部位于 `jniLibs/arm64-v8a/`，且
`packagingOptions` 未把它们排除。不要重命名 Qt 库文件。

**Q2：ABI 相关的 `unsatisfied link error` / 安装时 `INSTALL_FAILED_NO_MATCHING_ABIS`？**
宿主工程包含 SDK 不支持的 ABI。把 `abiFilters` 收敛为仅 `arm64-v8a`，或保证其他 ABI
的变体不引用 SDK。

**Q3：`initialize()` 返回 `ThreadStartFailed` / `CommunicationInitFailed`？**
检查 `appDataPath` 目录是否存在且可读写；确认进程没有超出线程数限制；完整日志下
重试。修复后可再次调用 `initialize()`（先 `shutdown()` 更稳妥）。

**Q4：初始化后 `rcGetChOutList()` 等返回空或默认值？**
设备配置尚未读取完成。调用 `taskReadConfigFromDevice()` 并等待
`RC_MSGID_GETDEVCFG_END`，不要 sleep 固定时间。

**Q5：回调里刷新 UI 卡顿或崩溃？**
回调运行在 SDK 输出分发线程。请按 24.2 的方式把数据投递到 UI 线程处理。

**Q6：固件更新进度出现 `-1` / `-2`？**
`-1` 表示没有可用固件（先确认 `rcGetFwInf().cacheFwFileVerTxt` 与网络下载流程）；
`-2` 表示更新失败，可检查设备连接后重试。

**Q7：`rcDelDrData()` 返回 0 但 DR 似乎没删掉？**
这是当前实现的已知限制（见 13.2），删除不会实际持久生效，请避免依赖该接口做
删除流程，或联系 SDK 维护方确认后续版本行为。

**Q8：混控删除总是失败？**
删除键是 `(chIdx, srcIdx)` 而非 `(chIdx, mixIdx)`（见 11.2）。

**Q9：`TelemetryNotifyUI` 的 `hashValue` 存进 SQLite/JS 后匹配不上？**
该字段是 64 位无符号整数。SQLite 用 `INTEGER`，JS/脚本层用 `BigInt` 或字符串，
禁止截断为 32 位。

**Q10：修改配置后重启 APP 发现改动丢失？**
只调用了 `rcSet*` 而未保存。按 24.4 的语义选择
`taskSaveConfigToDevice()` 或 `rcSaveCurrentCfgToModel()`。

---

## 26. 附录

### 附录 A：测试控制台命令参考（`tst_rmAxSdkApi.cpp`）

测试程序为交互式控制台，启动后输入命令调用对应 SDK API，可作为集成自测脚本参考：

| 命令 | 作用 |
| --- | --- |
| `help` | 显示命令列表 |
| `version` / `status` | 显示 SDK 版本 / 初始化状态 |
| `init` / `shutdown` | 初始化 / 关闭 SDK |
| `postMsg` | 投递一条测试消息到 SDK 输入队列 |
| `saveConfig` / `readConfig` | `taskSaveConfigToDevice` / `taskReadConfigFromDevice` |
| `getSrcCfg` | 列出输入源配置（并演示 Ail/Ele/Thr/Rud 原始值换算） |
| `getChOutCfg` | 列出输出通道配置 |
| `getTelemetry [uiOnly]` | 列出遥测项（0 全部，非 0 仅 UI 项） |
| `getMixCfg <ch> <mix>` | 显示一条混控配置 |
| `getTimerCfg <timer>` | 显示一个定时器配置 |
| `getRadioStatus` | 显示遥控器基础状态 |
| `getCurve [idx]` | 列出全部曲线或指定曲线 |
| `setCurve <idx> <type> <ptNum> <x0> <y0> ...` | 更新曲线配置 |
| `applyCurve <idx> <raw>` | 对输入值应用曲线计算 |
| `getFwInf` / `requestFwInf` | 读取缓存固件信息 / 向设备请求固件信息 |
| `requestElrsParameters` | 请求 ELRS 参数菜单（地址 0xEE） |
| `requestElrsStatus` | 请求 ELRS 状态 |
| `startUpdateFw` | 启动设备固件更新 |
| `getAppInfo` | 从服务器查询 APP 版本信息 |
| `updateApp <url>` / `abortAppUpdate` | 启动/中止 APP 更新下载 |
| `getModels` / `createModel` | 列出模型 / 新建模型 |
| `setModel <file> <name> <image> <index> <match>` | 修改模型元数据 |
| `exportModel <src> <dst>` / `importModel <src> <dst>` | 导出/导入模型 |
| `deleteModel <file>` | 删除模型文件 |
| `getTemplates` / `modelToTemplate <file> <name>` | 模板列表 / 模型存为模板 |
| `quit` / `exit` | 关闭 SDK 并退出 |

> 注意：该源码包含未随包发布的 `rmAxSdkTestConsole.h`，不能直接编译，仅作参考。

### 附录 B：比较条件与枚举速查

| 场景 | 取值 |
| --- | --- |
| 开关比较条件 `swCmpType` / `rstSwCmpType` / `cmpType` | 0 禁用 / 1 大于 / 2 等于 / 3 小于 |
| 失控保护 `fsMode` | 0 无 / 1 Hold / 2 No pulses / 3 Custom |
| RF 模块 `rfModuleMode` | 0 内部 ELRS / 1 外部 ELRS / 2 / 0xff 关闭 |
| 混控类型 `mixesMode` | 0 替换 / 1 叠加 / 2 相乘（头文件注释） |
| 曲线类型 `cType` | 0 无 / 1 DIFF / 2 EXPO / 3 FUNC / 4 自定义 |
| 特殊曲线索引 | 0xff 表示不应用曲线 |
| CRSF 帧格式 | ADDR LEN TYPE DATA CRC（总长 LEN+2，CRC8-D5 覆盖 TYPE+DATA） |
| ELRS 发射机地址 | 0xEE（以实际 CRSF 设备地址为准） |

### 附录 C：许可信息

- SDK 分发包内 Qt6 运行时库遵循 Qt 开源许可证，文本见
  `licenses/Qt-LICENSE.txt`。商用发布前请完成合规评估。

### 附录 D：术语表

| 术语 | 含义 |
| --- | --- |
| RC | Radio Control，遥控模型 |
| ELRS | ExpressLRS，开源 RC 射频链路协议 |
| CRSF | Crossfire 串行协议（TBS），ELRS 兼容 |
| 混控 / Mixing | 多输入源叠加到一个输出通道的控制逻辑 |
| DR | Dual Rate，双比率（舵量比例切换） |
| 失控保护 / FS | Failsafe，链路丢失时的通道输出策略 |
| 预飞行检查 / PFC | Pre-Flight Check，起飞前通道位置检查 |
| 遥测 / Telemetry | 接收机回传的飞行数据（电压、位置、姿态等） |
| LQ / dBm | 链路质量 / 信号强度 |
| MODELMATCH | ELRS 发射机与模型的匹配编号机制 |
| ABI | Application Binary Interface，本包仅提供 arm64-v8a |

---

*本手册基于发布包内公开头文件、`SDK_API_USAGE.zh-CN.md` 与 `tst_rmAxSdkApi.cpp`
整理生成。若 SDK 版本更新导致行为变化，以最新头文件与官方使用说明为准。*
