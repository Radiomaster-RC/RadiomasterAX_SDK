# rmAxSdk1 SDK User Manual (Detailed Edition)

**Applicable version**: as defined by the public headers `rmAxSdkApi.h` and `rmAxSdkTypes.h` under `include/rmAxSdk1/` in this release package.
**Release platform**: Android arm64-v8a.

> This document is intended for application developers integrating rmAxSdk1. It covers SDK
> features, integration steps, every public API, data types, message notifications, typical
> business flows, best practices, and frequently asked questions, and can serve as a complete
> reference for development and troubleshooting.

---

## Table of Contents

1. [SDK Introduction](#1-sdk-introduction)
2. [Release Package Layout](#2-release-package-layout)
3. [Runtime and Build Requirements](#3-runtime-and-build-requirements)
4. [Android Integration Guide](#4-android-integration-guide)
5. [Quick Start: Minimal Working Example](#5-quick-start-minimal-working-example)
6. [Core Concepts](#6-core-concepts)
7. [Lifecycle and Common Interfaces](#7-lifecycle-and-common-interfaces)
8. [Message Mechanism in Detail](#8-message-mechanism-in-detail)
9. [Reading, Modifying, and Saving Configuration](#9-reading-modifying-and-saving-configuration)
10. [Input Sources, Output Channels, and Trimming](#10-input-sources-output-channels-and-trimming)
11. [Mixing](#11-mixing)
12. [Curve](#12-curve)
13. [Dual Rate (DR)](#13-dual-rate-dr)
14. [Timers and Counters](#14-timers-and-counters)
15. [System Configuration](#15-system-configuration)
16. [Device Control](#16-device-control)
17. [Model and Template Management](#17-model-and-template-management)
18. [Telemetry and Notifications](#18-telemetry-and-notifications)
19. [ELRS and CRSF](#19-elrs-and-crsf)
20. [Device Firmware Update](#20-device-firmware-update)
21. [APP Update](#21-app-update)
22. [Data Type Reference](#22-data-type-reference)
23. [Return Values and Error Handling Conventions](#23-return-values-and-error-handling-conventions)
24. [Best Practices](#24-best-practices)
25. [Frequently Asked Questions (FAQ)](#25-frequently-asked-questions-faq)
26. [Appendix](#26-appendix)

---

## 1. SDK Introduction

rmAxSdk1 is a native C++ SDK for radio control model equipment (RC transmitters). Through the
SDK, a host application (an Android APP) can perform:

- **Configuration management**: read/modify/save the transmitter's working configuration
  (input sources, output channels, mixing, curves, DR, timers/counters, system parameters).
- **Real-time status**: subscribe to live values of sticks, knobs, and switches; channel output
  values; trim values; RF link status (LQ, dBm, lost-packet counters); battery voltage, etc.
- **Model management**: create, load, save, import, export, and delete models/templates.
- **Telemetry**: obtain data returned by the receiver (position, attitude, voltage, current) and
  configure alarm notifications.
- **ELRS/CRSF**: browse and modify ELRS module parameter menus, query link status, and send or
  receive raw CRSF frames.
- **Updates**: device firmware updates, plus version checking and APK download for the host APP
  itself.

### 1.1 Architectural Characteristics

- **A pure standard C++ boundary**: the public API uses only standard types such as
  `std::string`, `std::vector`, `std::list`, and `std::shared_ptr`; the host does not need Qt or
  any other specific framework.
- **A two-channel interaction model**:
  - Host → SDK: call public functions such as `rcGet*` / `rcSet*` / `rcTask*` directly;
  - SDK → Host: asynchronous push through the unified message callback registered with
    `setMessageCallback()`.
- **Internal multithreading**: the SDK contains one worker thread and one separate message output
  dispatch thread. Both the input and output sides use bounded message queues (the current limit
  is 4096 for each, but hosts must not rely on that number).
- **Exception safety**: all public APIs are `noexcept`; exceptions never cross the dynamic library
  boundary.

### 1.2 Namespace and Headers

All public types and functions live in the `rmaxSdk` namespace:

```cpp
#include <rmAxSdkApi.h>   // already includes rmAxSdkTypes.h; usually this single include is enough
```

---

## 2. Release Package Layout

```text
rmAxSdk-main/
├── include/
│   └── rmAxSdk1/
│       ├── rmAxSdkApi.h      # Public API declarations (function entry points)
│       └── rmAxSdkTypes.h    # Public data structures, message ID definitions
├── lib/
│   └── arm64-v8a/
│       ├── librmAxSdk1.so                 # The SDK itself (about 513 KB)
│       ├── libQt6Core_arm64-v8a.so        # Qt6 core library (about 5.8 MB)
│       ├── libQt6Network_arm64-v8a.so     # Qt6 network library (about 1.8 MB)
│       ├── libQt6SerialPort_arm64-v8a.so  # Qt6 serial port library (about 124 KB)
│       ├── libQt6Concurrent_arm64-v8a.so  # Qt6 concurrent library (about 42 KB)
│       └── libc++_shared.so               # LLVM libc++ runtime (about 1.8 MB)
├── licenses/
│   └── Qt-LICENSE.txt      # Qt open source license text
├── SDK_API_USAGE.en.md     # API usage notes (English)
├── SDK_API_USAGE.zh-CN.md  # API usage notes (Chinese)
└── tst_rmAxSdkApi.cpp      # Reference source of the test console (interactively demonstrates all APIs)
```

### 2.1 Dependency Notes

| Library | Purpose | Must be deployed |
| --- | --- | --- |
| `librmAxSdk1.so` | The SDK itself | Yes |
| `libQt6Core_arm64-v8a.so` | Qt core (event loop, threads, containers, etc.) | Yes |
| `libQt6Network_arm64-v8a.so` | Networking (APP update check/download) | Yes |
| `libQt6SerialPort_arm64-v8a.so` | Serial communication (device connection) | Yes |
| `libQt6Concurrent_arm64-v8a.so` | Concurrent task support | Yes |
| `libc++_shared.so` | C++ standard library runtime (required by both Qt and the SDK) | Yes |

The following system libraries are provided by Android and must **not** be bundled into the
application: `liblog.so`, `libm.so`, `libz.so`, `libdl.so`, `libc.so`.

> **License note**: the SDK distribution package includes Qt6 runtime libraries; Qt is released
> under its open source license (see `licenses/Qt-LICENSE.txt`). Evaluate Qt license compliance
> before a commercial release.

> **About the test source**: `tst_rmAxSdkApi.cpp` depends on `rmAxSdkTestConsole.h`, which is not
> shipped with the package, so it cannot be compiled directly inside this package. Read it as
> **reference code for calling conventions**; Appendix A in Chapter 26 gives its complete command
> description.

---

## 3. Runtime and Build Requirements

### 3.1 Basic Requirements

- **Language standard**: C++20 or later.
- **Target platform**: Android, ABI **arm64-v8a** (this release package contains binaries for
  that ABI only; the Windows build is distributed separately as `rmAxSdk1.dll` and is not part of
  this package).
- **NDK**: use a reasonably recent NDK that supports C++20 (r25 or later is recommended); the
  compiler is Clang.
- **STL**: the SDK and its Qt runtime are built on **LLVM libc++** (`libc++_shared.so`). The host
  should use the NDK default `c++_shared` and ensure that only one copy of `libc++_shared.so` is
  linked in the final build.

### 3.2 ABI/CRT Consistency (Important)

The host and the SDK pass `std::string`, `std::vector`, `std::list`, and `std::shared_ptr` across
the dynamic library boundary. **Do not mix** different C++ runtimes or STL implementations;
otherwise you will see heap corruption, random crashes, and other hard-to-diagnose problems in
`initialize()`, in parameter destruction, or in return-value destruction.

Notes for Android:

- Set `ANDROID_STL` to `c++_shared` (the NDK default) in the host's `externalNativeBuild`;
- Keep exactly one `libc++_shared.so` in the whole project (this package provides one that matches
  the Qt libraries);
- Do not replace the Qt libraries with versions from the system or any other source; the runtime
  dependencies must match `librmAxSdk1.so`.

### 3.3 Host Runtime Environment

The SDK does not depend on the host's message loop. Console programs, APPs with a UI, and
background services can all call `initialize()` directly. The host must ensure:

1. The application data directory (`appDataPath`) passed at initialization is readable and
   writable;
2. `shutdown()` is called and any held SDK objects are released before exiting or unloading the
   dynamic library.

---

## 4. Android Integration Guide

### 4.1 Copying Libraries and Headers

Copy the contents of the release package into the host project, for example:

```text
app/
├── src/main/
│   ├── cpp/                     # CMake project
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

### 4.2 CMakeLists.txt Configuration

```cmake
cmake_minimum_required(VERSION 3.22)
project(rmAxConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Import the SDK as an IMPORTED target
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

If you link with `-L<dir> -lrmAxSdk1` instead, make sure the linker can resolve all symbols. The
IMPORTED target approach above is recommended; use `IMPORTED_NO_SONAME TRUE` to avoid writing the
build machine's absolute path into DT_NEEDED.

### 4.3 build.gradle Configuration Highlights

```groovy
android {
    defaultConfig {
        ndk { abiFilters 'arm64-v8a' }          // This SDK supports arm64-v8a only
        externalNativeBuild {
            cmake { cppFlags '-std=c++20' }
        }
    }
    sourceSets.main.jniLibs.srcDir 'src/main/jniLibs'
    packagingOptions.jniLibs.useLegacyPackaging = true  // Ensures all .so files are extracted on install
}
```

Key points:

- **Keep only `arm64-v8a` in abiFilters**. If the project also includes other ABIs (such as
  armeabi-v7a or x86_64), you must ensure that the APKs for those ABIs do not carry code that
  references rmAxSdk1, otherwise they will crash at load time.
- All 7 `.so` files must be packed into the APK, **all of them**, keeping their file names;
  Android extracts them per ABI into `app nativeLibraryDir` at install time.
- On the Java/Kotlin side you do **not** need to call `System.loadLibrary` manually for every Qt
  library; when you call `System.loadLibrary("rmAxSdk1")`, the dynamic linker automatically loads
  the Qt and libc++ runtime libraries located in the same directory, following DT_NEEDED.
- When you use the APP update check/download feature, declare the network permissions in
  `AndroidManifest.xml`:

```xml
<uses-permission android:name="android.permission.INTERNET" />
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
```

### 4.4 Verifying That Integration Succeeded

Run the minimal example from Chapter 5. If it prints the SDK version string and `initialize()`
returns `Success` / `AlreadyInitialized`, then linking and loading are working correctly.

---

## 5. Quick Start: Minimal Working Example

Below is a complete minimal integration example (Linux/NDK console style) that demonstrates the
fixed order of initialization, callback registration, reading device configuration, and teardown:

```cpp
#include <rmAxSdkApi.h>

#include <iostream>

// SDK message callback: runs on the SDK output dispatch thread; must be short and non-blocking.
void onSdkMessage(const rmaxSdk::SdkMessage& message, void* userData)
{
    (void)userData;
    switch (message.msgId) {
    case rmaxSdk::RC_MSGID_GETDEVCFG_END:
        std::cout << "device config ready\n";
        break;
    case rmaxSdk::RC_MSGID_MIXOUT_UPDATE:
        // Real-time data has been refreshed; re-call getters as needed
        // (do not refresh the UI at high frequency inside the callback).
        break;
    default:
        break;
    }
}

int main()
{
    // 1. Print the version (the only business-agnostic API that is safe to call before initialize)
    std::cout << "SDK version: " << rmaxSdk::version() << '\n';

    // 2. Initialize: pass the application's private data directory
    const auto result = rmaxSdk::initialize("/data/data/<pkg>/files/rmAx");
    if (result != rmaxSdk::InitResult::Success
        && result != rmaxSdk::InitResult::AlreadyInitialized) {
        std::cerr << "initialize failed\n";
        return 1;
    }

    // 3. Register the message callback
    if (!rmaxSdk::setMessageCallback(&onSdkMessage, nullptr)) {
        rmaxSdk::shutdown();
        return 1;
    }

    // 4. Read the configuration from the device asynchronously; consider it ready only after
    //    RC_MSGID_GETDEVCFG_END arrives
    rmaxSdk::taskReadConfigFromDevice();

    // 5. Example of a runtime business call
    const auto channels = rmaxSdk::rcGetChOutList();
    std::cout << "output channels: " << channels.size() << '\n';

    // 6. Teardown: clear the callback, then shutdown
    rmaxSdk::setMessageCallback(nullptr, nullptr);
    rmaxSdk::shutdown();
    return 0;
}
```

**First-integration checklist**:

- [ ] All 7 `.so` files are present in the APK and all are arm64-v8a;
- [ ] `initialize()` returns `Success` or `AlreadyInitialized`;
- [ ] `setMessageCallback()` returns `true`;
- [ ] After calling `taskReadConfigFromDevice()`, `RC_MSGID_GETDEVCFG_END` is received;
- [ ] `RC_MSGID_MIXOUT_UPDATE` is received at a period of roughly 40 ms.

---

## 6. Core Concepts

### 6.1 The Three Data Locations

Every configuration operation involves three locations; understanding them is a prerequisite for
using the SDK correctly:

| Location | Description | How to access |
| --- | --- | --- |
| **SDK current working configuration** | The running configuration in SDK memory | `rcGet*` / `rcSet*` |
| **Local model files** | Model JSON files inside the `appDataPath` directory | Model management APIs such as `rcLoadModelFile`, `rcSaveCurrentCfgToModel` |
| **Transmitter device configuration** | The data actually in effect on the device | `taskReadConfigFromDevice` / `taskSaveConfigToDevice` |

Key conclusions:

- `rcSet*` usually modifies only the **working configuration** or sends a real-time preview; it
  does **not** mean the change has been saved to a model file or to the device;
- To persist to the device you must call `taskSaveConfigToDevice()`;
- To persist to a local model, call `rcSaveCurrentCfgToModel()` or a related model management API.

### 6.2 Synchronous Interfaces vs. Asynchronous Tasks

- **Most `rcGet*` / `rcSet*` calls**: the operation is delivered synchronously to the SDK worker
  thread, and by the time the function returns, this memory access has completed (but device
  communication may not have).
- **`rcTask*` / `task*` interfaces**: these only **start** a task. Device communication, network
  requests, and update processes complete asynchronously, and results are reported through the
  message callback (for example `RC_MSGID_GETDEVCFG_END`, `RC_MSGID_FWUPDATE_PROGRESS`).

Do not substitute a fixed `sleep` for the completion message — tasks may queue up or be delayed
during device handshaking.

### 6.3 Threading Model and Callback Discipline

```
Host thread                Inside the SDK
─────────              ─────────────────────────────
Business code  ──API calls──>  SDK worker thread (handles rcGet/rcSet/rcTask)
   ^                                          |
   |------- output dispatch thread <--- bounded output queue <---+
        (invokes the host's MessageCallback)
```

- The message callback runs on the SDK's **output dispatch thread** — not on the host main thread,
  and not on the SDK worker thread;
- The callback must be **thread-safe, short, and non-blocking**: check `msgId` first, copy the
  payload into your own value type, post it to the host's task queue/event loop, and return
  immediately;
- Never perform network access, disk IO, long computations, or lock waits inside the callback;
- Do not directly manipulate objects that require a fixed thread inside the callback (such as
  Android UI widgets).

### 6.4 Refresh Cadence of Real-Time Data

- Raw device data updates roughly once every **40 ms**, and the SDK notifies the host with
  `RC_MSGID_MIXOUT_UPDATE`;
- After receiving that message you can re-fetch in batches at a UI-friendly cadence:
  `rcGetSrcCfgList()`, `rcGetChOutList()`, `rcGetTrimming()`, `rcGetRadioStatus()`;
- Do not poll real-time getters in a tight loop; the UI should coalesce/throttle updates based on
  page visibility and rendering capability.

---

## 7. Lifecycle and Common Interfaces

### 7.1 API Overview

```cpp
std::string_view rmaxSdk::version() noexcept;

rmaxSdk::InitResult rmaxSdk::initialize(std::string appDataPath) noexcept;

bool rmaxSdk::setMessageCallback(rmaxSdk::MessageCallback callback,
                                 void* userData = nullptr) noexcept;

void rmaxSdk::shutdown() noexcept;
```

### 7.2 Interface Descriptions

**`version()`**
Returns a read-only view of the SDK's internal static build date/time string. It can be called
before `initialize()`. The return value points to static storage, **must not be freed**, and
should not be held long-term (its lifetime equals that of the dynamic library).

**`initialize(appDataPath)`**
Initializes the SDK: creates the worker thread and the output dispatch thread, and loads the local
configuration and models.

- `appDataPath`: the directory where the SDK stores global configuration, models, templates, and
  update caches. It must be readable and writable. The SDK maintains configuration files and
  subdirectories inside it; **deleting the entire directory is equivalent to "restoring local
  factory data"**, and default configuration is rebuilt on the next initialization.
- Return values:

| Return value | Meaning |
| --- | --- |
| `InitResult::Success` | Initialization succeeded |
| `InitResult::AlreadyInitialized` | The SDK is already running; no runtime was recreated this time (treat as success) |
| `InitResult::ThreadStartFailed` | The worker thread or output thread failed to start |
| `InitResult::CommunicationInitFailed` | Internal communication/business object initialization failed |

- This function is idempotent; `shutdown()` may be called repeatedly; re-initialization is allowed
  after a complete shutdown.
- **Except for `version()`, all other APIs should be called only after `initialize()` has
  succeeded.** When not initialized, `setMessageCallback()` / `postMessage()` return `false` (see
  the validation in the test source).

**`setMessageCallback(callback, userData)`**
Registers or replaces the message callback; pass `nullptr` to clear it. `userData` is passed back
to the callback unchanged and can carry host context (it must be an address valid for as long as
the callback lives). Calling before initialization returns `false`.

**`shutdown()`**
Stops the SDK worker thread and output thread and releases internal resources. Repeated calls are
safe.

### 7.3 Recommended Shutdown Order

1. Stop the host from issuing further SDK business calls;
2. Call `setMessageCallback(nullptr, nullptr)` to clear the message callback;
3. Wait for the SDK messages queued by the host itself to finish being processed;
4. Release the `SdkMessage` objects, payloads, and SDK-returned containers/strings held by the
   host;
5. Call `shutdown()`;
6. Only then unload the dynamic library (on Android this is normally handled by process exit).

---

## 8. Message Mechanism in Detail

### 8.1 Message Structure

```cpp
struct SdkMessage {
    std::uint32_t msgId;                        // Message ID; determines the payload type
    std::shared_ptr<const void> payload;        // Payload; may be null
};

using MessageCallback = void (*)(const SdkMessage& message, void* userData);
```

- `msgId` determines the real type of `payload`; **casting it to the wrong type is undefined
  behavior**;
- In the callback, first check that `msgId` and `payload` are valid, then convert with
  `std::static_pointer_cast`;
- Prefer **copying the payload contents into a value type** before posting to another thread; do
  not hold raw pointers long-term;
- Some messages carry no payload; there the payload is only used as a notification signal.

### 8.2 Complete Output Message Table

Message IDs increase consecutively starting from `0x08000001` (defined in `enum RcMessageId` in
`rmAxSdkTypes.h`):

| Message ID | Value | payload type | Description |
| --- | --- | --- | --- |
| `RC_MSGID_LVTG_NOTIFY` | `0x08000001` | none | Transmitter low-voltage event |
| `RC_MSGID_PFC_NOTIFY` | `0x08000002` | `std::vector<std::int32_t>` | Pre-flight check status per channel (the length is whatever is actually returned) |
| `RC_MSGID_TIM_REPORT` | `0x08000003` | `RcTimeReportUI` | Timer report (index/direction/start value/current seconds) |
| `RC_MSGID_CNT_REPORT` | `0x08000004` | `RcTimeReportUI` | Counter report (index/direction/initial value/current count) |
| `RC_MSGID_TIM_NOTIFY` | `0x08000005` | `RcTimeNotifyUI` | Timer alert event (alarm, hourly chime, etc.) |
| `RC_MSGID_TRIM_NOTIFY` | `0x08000006` | `std::uint8_t` | Trim value changed |
| `RC_MSGID_SRCINPUT_UPDATE` | `0x08000007` | undefined | Reserved message ID; do not use at present |
| `RC_MSGID_MIXOUT_UPDATE` | `0x08000008` | none | Input/output/trim/status refreshed; re-call the getters |
| `RC_MSGID_CFGTODEV_END` | `0x08000009` | none | Current configuration written to the device (`taskSaveConfigToDevice` completed) |
| `RC_MSGID_GETDEVCFG_END` | `0x0800000A` | none | Device configuration read completed (`taskReadConfigFromDevice` completed) |
| `RC_MSGID_RCTELEMETER_NOTIFY` | `0x0800000B` | none | The telemetry notification queue is non-empty; call `RCgetTelemetryItemNotify()` to drain it |
| `RC_MSGID_APPINFO_UPDATED` | `0x0800000C` | `AppUpdateInfoUI` | APP update information refreshed |
| `RC_MSGID_APPDOWNLOAD_PROGRESS` | `0x0800000D` | `AppUpdateInfoUI` | APP download byte progress |
| `RC_MSGID_APPDOWNLOAD_END` | `0x0800000E` | `AppUpdateInfoUI` | APP download completed; read `cacheAppFile` |
| `RC_MSGID_FWVERSION_NOT_MATCH` | `0x0800000F` | none | Device firmware version too old or incompatible |
| `RC_MSGID_FWINF_UPDATED` | `0x08000010` | none | Firmware information refreshed; call `rcGetFwInf()` |
| `RC_MSGID_FWUPDATE_PROGRESS` | `0x08000011` | `std::int32_t` | Progress `0..100`; `-1` no available firmware; `-2` update failed |
| `RC_MSGID_FWFILE_DOWNLOAD_END` | `0x08000012` | none | Firmware cache file download completed |
| `RC_MSGID_ELRS_SET_ACK` | `0x08000013` | `ElrsSetEndAckUI` | ELRS menu setting acknowledgement |
| `RC_MSGID_ELRS_STATUS` | `0x08000014` | `ElrsStatusInfoUI` | ELRS status response |
| `RC_MSGID_ELRS_TELEMETRY` | `0x08000015` | `TelemetryDataUI` | ELRS position/attitude/battery snapshot |
| `RC_MSGID_ELRS_DEVINF_UPDATE` | `0x08000016` | `std::string` | ELRS device name |
| `RC_MSGID_ELRS_MENUITEM_UPDATE` | `0x08000017` | `ElrsSetItemUI` | One ELRS menu item (pushed multiple times) |
| `RC_MSGID_CRSF_DATA_RECEIVED` | `0x08000018` | `std::vector<std::uint8_t>` | A complete CRSF frame |
| `RC_MSGID_TELEMETRY_UPDATED` | `0x08000019` | none | The telemetry list changed; re-fetch as needed |
| `RC_MSGID_TELEMETRY_LOST` | `0x0800001A` | none | Receiver telemetry lost |
| `RC_MSGID_TELEMETRY_RECOVER` | `0x0800001B` | none | Receiver telemetry recovered |

### 8.3 Message Parsing Example

```cpp
void onSdkMessage(const rmaxSdk::SdkMessage& message, void*)
{
    if (message.msgId == rmaxSdk::RC_MSGID_FWUPDATE_PROGRESS && message.payload) {
        const auto progress =
            std::static_pointer_cast<const std::int32_t>(message.payload);
        const std::int32_t value = *progress;   // 0..100 / -1 / -2
        // Post value to the host thread to update a progress bar
    }

    if (message.msgId == rmaxSdk::RC_MSGID_ELRS_STATUS && message.payload) {
        const auto status =
            std::static_pointer_cast<const rmaxSdk::ElrsStatusInfoUI>(message.payload);
        const rmaxSdk::ElrsStatusInfoUI snapshot = *status;  // Copy the snapshot
        // Post snapshot
    }

    if ((message.msgId == rmaxSdk::RC_MSGID_TIM_REPORT
         || message.msgId == rmaxSdk::RC_MSGID_CNT_REPORT)
        && message.payload) {
        const auto report =
            std::static_pointer_cast<const rmaxSdk::RcTimeReportUI>(message.payload);
        const rmaxSdk::RcTimeReportUI snapshot = *report;
        // Post snapshot
    }
}
```

### 8.4 Input Message Queue

```cpp
bool rmaxSdk::postMessage(rmaxSdk::SdkMessage message) noexcept;
```

- Places a message asynchronously into the SDK input queue; returns `false` if the SDK is not
  initialized, is shutting down, the queue is full, or delivery fails;
- The current public API **does not define any concrete input message business protocol**;
  returning `true` only means the message entered the queue, not that a corresponding business
  handler exists;
- Normal SDK functionality should use the explicit public APIs; do not rely on `postMessage` to
  implement business logic.

---

## 9. Reading, Modifying, and Saving Configuration

### 9.1 Related APIs

```cpp
void rmaxSdk::taskReadConfigFromDevice() noexcept;              // Read configuration from the device (async)
void rmaxSdk::taskSaveConfigToDevice() noexcept;                // Save configuration to the device (async)
void rmaxSdk::rcLoadModelFile(std::string modelFile) noexcept;  // Load a model configuration from a file
void rmaxSdk::rcSaveCurrentCfgToModel(rmaxSdk::RcModelDataUI rcModel) noexcept;
```

### 9.2 Semantics

| API | Behavior |
| --- | --- |
| `taskReadConfigFromDevice()` | Asynchronously reads the device configuration; publishes `RC_MSGID_GETDEVCFG_END` on completion and writes the result into the current model file |
| `taskSaveConfigToDevice()` | Asynchronously writes to the device; publishes `RC_MSGID_CFGTODEV_END` on completion and saves the current model file |
| `rcLoadModelFile()` | Loads the SDK's current working configuration from the specified model file |
| `rcSaveCurrentCfgToModel()` | Saves the current working configuration into the model specified by the parameter ("save as to some model") |

### 9.3 Typical Editing Flow

```cpp
// 1. Modify a mix (only changes the working configuration/preview)
auto mix = rmaxSdk::rcGetMixCfgData(0, 0);
mix.weightUp = 80;
mix.weightDown = 80;
rmaxSdk::rcSetMixCfgData(mix);

// 2. Write to the device; the current model is also persisted on completion
rmaxSdk::taskSaveConfigToDevice();
```

Saving the current configuration into a new model:

```cpp
auto target = rmaxSdk::rcCreateModelCfg();
target.modelName = "My model";
rmaxSdk::rcSaveCurrentCfgToModel(target);
```

### 9.4 Determining Readiness

After initialization, local models can be read immediately, but **device data should be treated as
ready only after `RC_MSGID_GETDEVCFG_END`**. During startup handshaking, tasks may queue or be
delayed, so do not use a fixed sleep instead of the completion message.

---

## 10. Input Sources, Output Channels, and Trimming

### 10.1 API Overview

```cpp
std::vector<rmaxSdk::RcSrcCfgUI>   rmaxSdk::rcGetSrcCfgList() noexcept;    // Input source list
std::vector<rmaxSdk::RcChOutCfgUI> rmaxSdk::rcGetChOutList() noexcept;    // Output channel list
rmaxSdk::RcTrimmingTopKeyUI        rmaxSdk::rcGetTrimming() noexcept;     // Trim values
rmaxSdk::RadioStatusUI             rmaxSdk::rcGetRadioStatus() noexcept;  // Basic status
void rmaxSdk::rcSetChOutCfgData(rmaxSdk::RcChOutCfgUI chOutCfg) noexcept; // Update an output channel configuration
```

### 10.2 Input Source Index Quick Reference

The vector returned by `rcGetSrcCfgList()` is ordered by input source index. The reference order
given in the header comments is as follows (the device's actual return value takes precedence):

| Index | Name | Description |
| --- | --- | --- |
| 0 | Rud | Rudder (yaw) |
| 1 | Ele | Elevator (pitch) |
| 2 | Thr | Throttle |
| 3 | Ail | Aileron (roll) |
| 4–11 | P1–P8 | Knobs that self-center |
| 12–19 | S1–S8 | Knobs that do not self-center (sliders) |
| 20–35 | SA–SP | Switches |
| 36–41 | CNT1–CNT6 | Counters |

> The reference source `tst_rmAxSdkApi.cpp` confirms the order of the first four items:
> `items[0]=Rud`, `items[1]=Ele`, `items[2]=Thr`, `items[3]=Ail`.

### 10.3 Usage Notes

- `rcGetSrcCfgList()` returns the input source name, type, raw value, logical value, output value,
  and weight; `rawVal` is on a half-scale, so multiply by 2 to map it to full scale (as the
  reference source does);
- `rcGetChOutList()` returns output channel configurations and live values (`chOut`, `logicVal`);
- `rcGetTrimming()` returns the four trim values LV/LH/RV/RH;
- `rcGetRadioStatus()` returns RF link quality, voltages, top key state, and ADC calibration
  status;
- When modifying an output channel, **read first, change only the target fields, then write back**
  so that uninitialized fields do not overwrite other configuration:

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

## 11. Mixing

### 11.1 API Overview

```cpp
rmaxSdk::RcMixCfgDataUI rmaxSdk::rcGetMixCfgData(int chIdx, int mixIdx) noexcept;
void  rmaxSdk::rcSetMixCfgData(rmaxSdk::RcMixCfgDataUI mixData) noexcept;
rmaxSdk::RcMixCfgDataUI rmaxSdk::rcAddMixCfgData(int chIdx, int srcIdx) noexcept;
int   rmaxSdk::rcDelMixCfgData(int chIdx, int srcIdx) noexcept;   // Returns the remaining mix count on success, -1 on failure
```

### 11.2 Parameter Conventions

- `chIdx`: output channel index; `mixIdx`: mix item index within the channel; `srcIdx`: input
  source index;
- **The deletion key is `(chIdx, srcIdx)`, not `(chIdx, mixIdx)`**;
- `swCmpType` comparison condition: `0` disabled, `1` greater than, `2` equal to, `3` less than;
- `mixesMode` mix type: replace / add / multiply (`MIXED_MODE`, currently given as a comment in
  the header);
- `slowMsUp` / `slowMsDown`: up/down slow-motion time in milliseconds; `0` means no slow motion;
- `weightUp` / `weightDown`: up/down ratio.

### 11.3 Example

```cpp
// Add a mix of input source 1 (Ele) to channel 0
auto added = rmaxSdk::rcAddMixCfgData(0, 1);
added.weightUp = 100;
added.weightDown = 100;
rmaxSdk::rcSetMixCfgData(added);

// Delete (by chIdx + srcIdx)
const int remaining = rmaxSdk::rcDelMixCfgData(0, added.srcIdx);
```

---

## 12. Curve

### 12.1 API Overview

```cpp
std::vector<rmaxSdk::RcCurveCfgDataUI> rmaxSdk::rcGetCurveList(int idx = -1) noexcept;
int      rmaxSdk::rcSetCurveCfg(const rmaxSdk::RcCurveCfgDataUI& curveCfg) noexcept;
double   rmaxSdk::rcCurveApply(int curveIdx, double rawVal) noexcept;
```

### 12.2 Notes

- `rcGetCurveList(-1)` returns all curves; passing a specific index returns only that curve;
- The current model has **18 curves** by default, but callers should still rely on the list
  actually returned;
- `rcSetCurveCfg()` returns `0` on success and `-1` on an invalid configuration or failed call;
- `rcCurveApply()` returns `rawVal` unchanged on failure or when the index is invalid;
- `curveIdx == 0xff` means "apply no curve";
- `xVal` / `yVal` are fixed-length arrays of size **12**; `ptNum` must not exceed the array
  capacity;
- Curve type `cType` reference (header comments): `0` none, `1` DIFF (trim greater than 0 controls
  the 0→+100 ratio, less than 0 controls the 0→-100 ratio), `2` EXPO, `3` FUNC, `4` custom curve
  with points.

### 12.3 Example

```cpp
auto curves = rmaxSdk::rcGetCurveList(0);
if (!curves.empty()) {
    auto curve = curves.front();
    curve.cType = 4;          // Custom curve
    curve.ptNum = 3;
    curve.xVal[0] = -100; curve.yVal[0] = -100;
    curve.xVal[1] = 0;    curve.yVal[1] = 0;
    curve.xVal[2] = 100;  curve.yVal[2] = 100;
    const int result = rmaxSdk::rcSetCurveCfg(curve);   // 0 means success
}
```

---

## 13. Dual Rate (DR)

### 13.1 API Overview

```cpp
rmaxSdk::RcChCfgDrUI rmaxSdk::rcGetDrData(int chIdx, int idx) noexcept;
int rmaxSdk::rcSetDrData(const rmaxSdk::RcChCfgDrUI& drCfg) noexcept;
int rmaxSdk::rcDelDrData(const rmaxSdk::RcChCfgDrUI& drCfg) noexcept;
```

### 13.2 Notes

- Each channel currently has **3 DR slots**;
- `rcGetDrData(chIdx, -1)` is used to find a free slot;
- `rcSetDrData()` returns the number of DRs in use on that channel on success, and `-1` on
  failure;
- **Known limitation**: in the current implementation `rcDelDrData()` only returns `0` and does
  not actually clear the DR slot yet; callers must not treat a return of `0` as meaning the
  deletion has been persistently applied;
- The channel locking fields (`lockChSwIdx` / `lockChSwVal` / `lockChSwType` / `lockChVal`) live in
  `RcChOutCfgUI` and are updated through `rcSetChOutCfgData()`.

---

## 14. Timers and Counters

### 14.1 API Overview

```cpp
rmaxSdk::RcTimeCfgDataUI rmaxSdk::rcGetTimerCfgData(int timerIdx, int isCounter) noexcept;
void rmaxSdk::rcSetTimerCfgData(const rmaxSdk::RcTimeCfgDataUI& timerCfg, int isCounter) noexcept;
void rmaxSdk::rcResetTimer(int timerIdx) noexcept;
```

### 14.2 Indices and Semantics

- `isCounter == 0` operates on the timer array; `isCounter != 0` operates on the counter array;
- Each group currently uses indices `0..2` (that is, timers 0–2 and counters 0–2; in device
  protocol numbering this can also be expressed as device timers 0–2 and counters 3–5).
  **Callers must validate indices first**;
- `timerCfg.timeIdx` is the in-group index used when writing;
- `direction`: `1` counts up, `0` counts down;
- `startTime == 0` means count up; a value greater than `0` is the countdown period in seconds; in
  counter mode this field holds the counter maximum and **must not be 0**;
- The comparison conditions `swCmpType` / `rstSwCmpType` / `cmpType` are uniformly: `0` disabled,
  `1` greater than, `2` equal to, `3` less than;
- `outputMin` / `outputMax` range from `-100..100`;
- Timer settings send a real-time preview; counter settings update the current working
  configuration;
- `rcResetTimer()` sends a device reset command and its parameter uses the device timer number.

### 14.3 Runtime Messages

- `RC_MSGID_TIM_REPORT` / `RC_MSGID_CNT_REPORT`: the payload in both cases is `RcTimeReportUI`.
  `direction == 0` displays the remaining amount and `1` displays the used amount; `timeCount` is
  the current number of seconds for a timer and the current count value for a counter;
- `RC_MSGID_TIM_NOTIFY`: the payload is `RcTimeNotifyUI`, containing the index, running state,
  notification code, and current count. The first three fields are `std::uint8_t`; convert them to
  `int` before printing so that stream output does not treat them as characters.

### 14.4 Example

```cpp
// Configure timer 0: a 60-second countdown with an alert every 10 seconds
auto timer = rmaxSdk::rcGetTimerCfgData(0, 0);
timer.timeIdx = 0;
timer.startTime = 60;
timer.beepTimeSec = 10;
rmaxSdk::rcSetTimerCfgData(timer, 0);

// Handle the notification message
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

## 15. System Configuration

```cpp
rmaxSdk::RcSysCfgDataUI rmaxSdk::rcGetRcSysConfig() noexcept;
void rmaxSdk::rcSetRcSysConfig(const rmaxSdk::RcSysCfgDataUI& rcSysCfg) noexcept;
```

Fields that the **current implementation actually reads and writes**:

- `alramVtg`: alarm voltage (unit 0.1 V);
- `rfModuleMode`: RF module mode (`0` internal ELRS, `1` external ELRS, `2`/`0xff` off);
- `stickMode`: stick mode (1 or 2);
- `preFlightCheckChannel[36]`: preset values for the pre-flight check of each channel (`127`
  means do not check).

The remaining fields in `RcSysCfgDataUI` (`fsMode`, `fsChVal`, `joytickBeep`, `modelMatchId`,
`crsfBaudRate`) are currently **not** fully persisted through these two APIs; callers must not
assume they take effect.

The model match value should be updated through `RcModelDataUI::modelMatch` and the model
management APIs.

---

## 16. Device Control

```cpp
void rmaxSdk::rcResetDefault() noexcept;               // Restore default configuration and trigger a re-read
void rmaxSdk::rcSkipPreFlightCheck() noexcept;         // Skip the current pre-flight check
void rmaxSdk::rcSetAdcCalibration(int isStart) noexcept; // Non-zero starts ADC calibration, 0 stops it
void rmaxSdk::rcSetUsbToVcp(std::uint8_t mode) noexcept; // USB virtual COM port passthrough mode
void rmaxSdk::rcRebootRFModule() noexcept;             // Request an RF module reboot
```

| API | Purpose |
| --- | --- |
| `rcResetDefault()` | Restores the default configuration and triggers a re-read (use with care; it overwrites the current working configuration) |
| `rcSkipPreFlightCheck()` | Skips the current pre-flight check alarm |
| `rcSetAdcCalibration(isStart)` | Starts/stops stick ADC calibration; the current state is in `rcGetRadioStatus().adcSetState` |
| `rcSetUsbToVcp(mode)` | Sets the USB virtual COM port passthrough mode; the value is defined by the device protocol |
| `rcRebootRFModule()` | Requests a reboot of the RF (ELRS) module |

---

## 17. Model and Template Management

### 17.1 API Overview

```cpp
std::list<rmaxSdk::RcModelDataUI> rmaxSdk::rcGetRcModelList() noexcept;      // Model list
rmaxSdk::RcModelDataUI rmaxSdk::rcCreateModelCfg() noexcept;                 // Create new model metadata
rmaxSdk::RcModelDataUI rmaxSdk::rcSetModelCfg(rmaxSdk::RcModelDataUI rcModel) noexcept;
void  rmaxSdk::rcSaveCurrentCfgToModel(rmaxSdk::RcModelDataUI rcModel) noexcept;
void  rmaxSdk::rcLoadModelFile(std::string modelFile) noexcept;
void  rmaxSdk::rcExportModelCfg(std::string modelName, std::string modelFile) noexcept;
int   rmaxSdk::rcImportModelCfg(std::string importFile, std::string modelFile) noexcept;
void  rmaxSdk::rcDeleteModel(std::string filePath) noexcept;
int   rmaxSdk::rcGetCurrentModelCfgIndex() noexcept;                         // Returns -1 on failure
std::list<rmaxSdk::RcModelDataUI> rmaxSdk::rcGetTemplateList() noexcept;     // Template list
void  rmaxSdk::rcModelToTemplate(std::string modelFile, std::string tempName) noexcept;
```

`RcModelDataUI` fields: `modelName` (model name), `modelImg` (image path/resource), `filePath`
(file path), `modelIdx` (index), `modelMatch` (ELRS MODELMATCH number).

### 17.2 Behavior Descriptions

| API | Current behavior |
| --- | --- |
| `rcGetRcModelList()` | Scans the model directory and returns the model list |
| `rcCreateModelCfg()` | Generates new model metadata and a target path; **it does not create the file immediately** |
| `rcSetModelCfg()` | Loads the target file, updates the metadata, and saves it; used to modify the metadata of an existing model |
| `rcSaveCurrentCfgToModel()` | Writes the current working configuration plus the supplied metadata into the target model |
| `rcLoadModelFile()` | Loads the target model as the current working configuration |
| `rcExportModelCfg(source, destination)` | Copies the model file to the export path |
| `rcImportModelCfg(source, target)` | Imports the source configuration into the target model; returns `0` on success |
| `rcDeleteModel()` | Deletes the specified model file |
| `rcGetCurrentModelCfgIndex()` | Returns the current model index; returns `-1` on failure |
| `rcGetTemplateList()` | Returns the template list |
| `rcModelToTemplate()` | Saves a model as a template with the given name |

### 17.3 Example

```cpp
// Create a new model and save the current configuration into it
auto model = rmaxSdk::rcCreateModelCfg();
model.modelName = "Plane";
model.modelImg = "qrc:/image/plane.png";
model.modelMatch = 1;
rmaxSdk::rcSaveCurrentCfgToModel(model);
```

> Use `rcSetModelCfg()` only when changing the name/image of an existing model; do **not** treat
> `rcSetModelCfg()` as a substitute for "save the current working configuration" — it is mainly
> for editing model metadata.

---

## 18. Telemetry and Notifications

### 18.1 Telemetry List

```cpp
std::list<rmaxSdk::TelemetryNotifyUI> rmaxSdk::rcGetTelemetryList(int uiOnly) noexcept;
```

- `uiOnly == 0`: returns all telemetry items;
- `uiOnly != 0`: returns only items with `showUI != 0`;
- `RC_MSGID_TELEMETRY_UPDATED` can arrive at a high rate, so **you should not refresh the entire
  UI on every message**;
- Recommended practice: keep the latest state in the background; throttle UI list refreshes (for
  example 500 ms–1.5 s); stop refreshing while the page is not visible; an explicit user request
  may bypass the throttle and fetch immediately.

### 18.2 Notification Configuration

```cpp
rmaxSdk::TelemetryNotifyUI rmaxSdk::rcGetNotifyCfgItem(std::uint64_t hashValue, int index) noexcept;
void rmaxSdk::rcSetNotifyCfgItem(std::uint64_t hashValue,
                                 const rmaxSdk::TelemetryNotifyUI& notifyCfg,
                                 int index) noexcept;
void rmaxSdk::rcDelNotifyItem(int index) noexcept;
std::list<rmaxSdk::TelemetryNotifyUI> rmaxSdk::rcGetNotifyCfgList() noexcept;
```

Decision rules for `rcSetNotifyCfgItem()`:

- `hashValue == 0`: **add** a notification; `index` does not participate in locating one;
- `hashValue != 0`: **modify** an existing notification by `index`; the caller must guarantee the
  index is valid;
- When adding, passing `index == -1` may express the caller's intent, but the first parameter is
  what actually determines that an item is added.

> **Important**: `hashValue` is a `std::uint64_t`. The business layer or script bindings **must
> not store it in a 32-bit `int`**, or it will be truncated; if the target language cannot
> represent 64-bit integers exactly, prefer a string.

### 18.3 Triggered Notification Queue

```cpp
int rmaxSdk::RCgetTelemetryItemNotify(rmaxSdk::TelemetryNotifyUI& notifyItem) noexcept;
```

`RC_MSGID_RCTELEMETER_NOTIFY` only indicates that the notification queue is non-empty; it
**carries no notification payload**. After receiving the message, drain it in a loop:

```cpp
rmaxSdk::TelemetryNotifyUI item{};
while (rmaxSdk::RCgetTelemetryItemNotify(item) != -1) {
    // Copy item and perform speech, popup, or logging
}
```

On success it returns the queue length before dequeuing; when the queue is empty or the call fails
it returns `-1` (the reference source uses `> 0` as the loop condition, which is equally safe).

### 18.4 Choosing Between the Telemetry Public Types

`rmAxSdkTypes.h` declares both `TelemetryNotifyUI` and `TelemetryItemUI`. The current public APIs
(`rcGetTelemetryList`, `rcGetNotifyCfgItem`, `rcSetNotifyCfgItem`, `rcGetNotifyCfgList`,
`RCgetTelemetryItemNotify`) **all use `TelemetryNotifyUI`**.

`TelemetryItemUI` currently has no corresponding public API parameter, return value, or message
payload; it is a reserved public data structure. Do not substitute it for the parameter type of
the interfaces listed above.

---

## 19. ELRS and CRSF

### 19.1 API Overview

```cpp
void rmaxSdk::rcTaskRequestElrsMenuList(int reqSetDevID) noexcept; // Request the ELRS parameter menu
void rmaxSdk::rcElrsSendMenuCmd(int idx, int val) noexcept;        // Modify a menu item
int  rmaxSdk::rcRequestElrsStatus() noexcept;                      // Request the ELRS status
void rmaxSdk::rcSendCrsfRawData(std::vector<std::uint8_t> data) noexcept; // Send a raw frame
```

### 19.2 Menu Browsing Flow

`rcTaskRequestElrsMenuList(reqSetDevID)` asynchronously requests the parameter menu of the
specified ELRS device. The commonly used transmitter address is `0xEE`
(CRSF_ADDRESS_CRSF_TRANSMITTER); callers should rely on the actual CRSF device address.

The response sequence is typically:

1. `RC_MSGID_ELRS_DEVINF_UPDATE`: device name (`std::string`);
2. Multiple `RC_MSGID_ELRS_MENUITEM_UPDATE`: one `ElrsSetItemUI` pushed at a time;
3. The host calls `rcElrsSendMenuCmd(idx, val)` according to user actions to make changes;
4. `RC_MSGID_ELRS_SET_ACK`: setting acknowledgement (`ElrsSetEndAckUI`).

Note: after leaving the ELRS page, the host should stop its own repeated refreshes/requests. While
a menu request is in progress, further requests are ignored by the internal state, and frequent
repeated fetching also causes the page to stutter.

### 19.3 ELRS Status

Return values of `rcRequestElrsStatus()`:

- `0`: the request was sent;
- `-1`: an ELRS menu request is in progress, or the SDK is unavailable/the call failed.

A successful send does not mean a response has been received. The response comes back through
`RC_MSGID_ELRS_STATUS` with a payload of `ElrsStatusInfoUI`:

- `badPkt`: number of bad packets;
- `goodPkt`: number of good packets;
- `modelMismatch`: model match status; non-zero means a mismatch.

### 19.4 Raw CRSF Data

`rcSendCrsfRawData()` sends the raw bytes supplied by the caller; an empty array is not sent.

`RC_MSGID_CRSF_DATA_RECEIVED` returns a complete frame in the format:

```text
ADDR LEN TYPE DATA CRC
```

The total number of bytes should be `LEN + 2`, and CRC8-D5 covers `TYPE + DATA`.

---

## 20. Device Firmware Update

### 20.1 API Overview

```cpp
rmaxSdk::FirmwareInfUI rmaxSdk::rcGetFwInf() noexcept;      // Returns the current firmware information
void  rmaxSdk::rcTaskRequestDeviceInf() noexcept;           // Request device information
void  rmaxSdk::rcTaskStartUpdateFw() noexcept;              // Start the firmware update
```

`FirmwareInfUI` contains the system ID (`SysId`), the device unique ID (`DevId`), total flight
time (`FlyTime`, seconds), build date (`BuildTime`, YYMMDD), firmware version (`VER_FW`,
`0x01020003` → `01.02.0003`), hardware version (`VER_HW`, `0x00000102` → `01.02`), device
description, version string, and cached firmware version text.

### 20.2 Standard Flow

1. Call `rcTaskRequestDeviceInf()`;
2. Receive `RC_MSGID_FWINF_UPDATED`;
3. Call `rcGetFwInf()` to obtain the device and cached firmware information;
4. Call `rcTaskStartUpdateFw()` according to business conditions (version comparison, user
   confirmation);
5. Handle `RC_MSGID_FWUPDATE_PROGRESS`.

Firmware update progress values (`std::int32_t`):

- `0..100`: update progress percentage;
- `-1`: no available firmware;
- `-2`: update failed.

Related messages also include: `RC_MSGID_FWVERSION_NOT_MATCH` (device firmware version too old or
incompatible; the pre-update check failed) and `RC_MSGID_FWFILE_DOWNLOAD_END` (firmware cache file
download completed).

---

## 21. APP Update

### 21.1 API Overview

```cpp
rmaxSdk::AppUpdateInfoUI rmaxSdk::rcGetAppUpdateInfo() noexcept;
void  rmaxSdk::rcTaskGetAppInfoFromWeb() noexcept;
int   rmaxSdk::rcTaskUpdateApp(std::string url) noexcept;
void  rmaxSdk::rcAbortUpdateApp() noexcept;
```

`AppUpdateInfoUI` fields: `downloadBytesReceived` / `downloadBytesTotal` (download progress),
`apkUrl`, `version`, `cnUpdateContent` / `enUpdateContent` (release notes), `cacheAppFile` (the
local cache file after the download completes).

### 21.2 Standard Flow

1. Call `rcTaskGetAppInfoFromWeb()`;
2. Receive `RC_MSGID_APPINFO_UPDATED`, then read the payload or call `rcGetAppUpdateInfo()`;
3. Call `rcTaskUpdateApp(info.apkUrl)` to start the download;
4. Handle `RC_MSGID_APPDOWNLOAD_PROGRESS` (you may also poll `rcGetAppUpdateInfo()` for byte
   progress);
5. After `RC_MSGID_APPDOWNLOAD_END`, read `cacheAppFile` and let the host perform the installation
   (for example by triggering the Android package installer).

Return values of `rcTaskUpdateApp()`: `0` the download task started successfully; `-1` a download
task already exists, the SDK is unavailable, or the call failed. `rcAbortUpdateApp()` aborts the
running download.

---

## 22. Data Type Reference

All structures below are defined in `rmAxSdkTypes.h` in the `rmaxSdk` namespace. Default values
follow the header files.

### 22.1 Input Source Configuration `RcSrcCfgUI`

| Field | Type | Description |
| --- | --- | --- |
| `srcName` | `std::string` | Input source name (for example "Ail") |
| `srcIdx` | `int` | Input channel index |
| `srcType` | `int` | Input channel type |
| `isEnable` | `int` | Whether display is enabled |
| `rawVal` | `int` | Raw value (half scale; multiply by 2 for full scale) |
| `rawLogicVal` | `int` | Logical value |
| `outputVal` | `int` | Output value |
| `weight` | `int` | Weight |

### 22.2 Trimming `RcTrimmingTopKeyUI`

| Field | Description |
| --- | --- |
| `trimLV` | Left vertical trim |
| `trimLH` | Left horizontal trim |
| `trimRV` | Right vertical trim |
| `trimRH` | Right horizontal trim |

### 22.3 Output Channel Configuration `RcChOutCfgUI`

| Field | Description |
| --- | --- |
| `chIdx` | Channel index |
| `middle` | Midpoint (±100%) |
| `min` / `max` | Minimum/maximum value (±100%) |
| `curveIdx` | Curve type/index |
| `mixesNum` | Number of mixes |
| `weight` | Weight |
| `chOut` | Channel output value (real-time) |
| `logicVal` | Logical value (real-time, `float`) |
| `lockChSwIdx` / `lockChSwVal` / `lockChSwType` / `lockChVal` | Channel lock: switch index/switch value/switch type/locked channel value |
| `dr1SwIdx` / `dr1SwAct` / `dr1Weight` | Dual rate 1: switch index/switch action/weight |
| `dr2SwIdx` / `dr2SwAct` / `dr2Weight` | Dual rate 2 |
| `dr3SwIdx` / `dr3SwAct` / `dr3Weight` | Dual rate 3 |

### 22.4 Mix Configuration `RcMixCfgDataUI`

| Field | Default | Description |
| --- | --- | --- |
| `srcName` | — | Input source name |
| `srcType` | 0 | Input source type |
| `srcIdx` | 0 | Input source index |
| `offset` | 0 | Offset (±100%) |
| `curveIdx` | 0 | Curve index |
| `swCmpType` | 0 | Enable-switch comparison condition: 0 disabled / 1 greater than / 2 equal to / 3 less than |
| `switchIdx` | 0 | Enable switch index |
| `swCmpVal` | 0 | Enable switch comparison value |
| `mixesMode` | 0 | Mix type (replace/add/multiply) |
| `chIdx` | 0 | Output channel index |
| `mixIdx` | 0 | Mix index within the channel |
| `slowMsUp` | 0 | Up slow motion (ms; 0 means none) |
| `slowMsDown` | 0 | Down slow motion (ms; 0 means none) |
| `weightUp` | 0 | Up ratio |
| `weightDown` | 0 | Down ratio |
| `mixesCount` | 0 | Number of mixes on this channel |

### 22.5 System Configuration `RcSysCfgDataUI`

| Field | Description |
| --- | --- |
| `fsMode` | Failsafe mode: 0 none / 1 Hold (keep last value) / 2 No pulses (no output) / 3 Custom |
| `fsChVal[16]` | Custom failsafe channel output values (±100%) |
| `stickMode` | Stick mode (1 or 2) |
| `preFlightCheckChannel[36]` | Pre-flight check presets: 127 means no check; ±100 is the preset value, and a large deviation raises an alarm |
| `rfModuleMode` | 0 internal ELRS / 1 external ELRS / 2 / 0xff off |
| `alramVtg` | Alarm voltage (0.1 V) |
| `joytickBeep` | Stick beeper |
| `modelMatchId` | ELRS MODELMATCH number |
| `crsfBaudRate` | 1:400K / 2:921600 / 3:1870000 |

> Currently `rcGetRcSysConfig` / `rcSetRcSysConfig` actually map only `alramVtg`,
> `rfModuleMode`, `stickMode`, and `preFlightCheckChannel`; see the notes in Chapter 15 for the
> remaining fields.

### 22.6 Timer Configuration `RcTimeCfgDataUI`

| Field | Default | Description |
| --- | --- | --- |
| `mode` | 0 | Working mode |
| `swCmpType` | 0 | Enable-switch comparison condition (0/1/2/3) |
| `switchIdx` | 0 | Enable switch index |
| `switchAct` | 0 | Enable switch action |
| `startTime` | 0 | 0 counts up; >0 is the countdown period in seconds; in counter mode it is the maximum |
| `direction` | 0 | Counting direction: 1 counts up / 0 counts down |
| `countdown` | 0 | Count alert mode (silent/beeper/speech/vibration and combinations) |
| `beepTimeSec` | 0 | Alert interval (for example 5/10/20/30 seconds) |
| `rstSwCmpType` | 0 | Reset-switch comparison condition |
| `resetSwIdx` | 0 | Reset switch index |
| `resetSwAct` | 0 | Reset switch action |
| `cmpValue` | 0 | Input comparison value for channel activation mode |
| `cmpType` | 0 | Comparator type: 0 disabled / 1 greater than / 2 equal to / 3 less than |
| `loopMode` | 0 | Loop mode |
| `outputMode` | 0 | Output mode (maps between outputMin/Max according to the threshold) |
| `outputMin` / `outputMax` | 0 | Output range (±100%) |
| `threshold` | 0 | Comparator threshold |
| `timeIdx` | 0 | In-group index (used when writing) |
| `trDirection` | 0 | Running direction (read-back) |
| `trStartTime` | 0 | Start value (read-back) |
| `timeCount` | 0 | Current count (read-back) |
| `notifyCode` | 0 | Notification information (read-back) |

### 22.7 Timer/Counter Message Payloads

```cpp
struct RcTimeReportUI {
    int timeIdx = 0;          // In-group index
    int direction = 0;        // 0 shows remaining / 1 shows used
    std::int32_t startTime = 0;  // Start value; 0 counts up, >0 counts down
    std::int32_t timeCount = 0;  // Seconds for a timer; current count for a counter
};

struct RcTimeNotifyUI {
    std::uint8_t timeIdx;     // Timer index (convert to int before printing)
    std::uint8_t isRunning;   // Non-zero means running
    std::uint8_t notifyCode;  // Notification code reported by the device
    std::int32_t timeCount = 0;  // Current count when the notification was triggered
};
```

### 22.8 DR Configuration `RcChCfgDrUI`

| Field | Description |
| --- | --- |
| `chIdx` | Channel number |
| `idx` | DR number (slot) |
| `drCount` | Number of DRs |
| `swCmpType` | Enable-switch comparison condition (0/1/2/3) |
| `swSrcIdx` | Enable switch |
| `swCmpVal` | Enable switch comparison value |
| `drWeight` | DR weight |

### 22.9 Curve Configuration `RcCurveCfgDataUI`

| Field | Description |
| --- | --- |
| `idx` | Curve index |
| `cType` | Curve type: 0 none / 1 DIFF / 2 EXPO / 3 FUNC / 4 custom curve with points |
| `ptNum` | Number of curve points (≤ 12) |
| `xVal[12]` / `yVal[12]` | Curve point coordinates (fixed length 12) |

### 22.10 Model Metadata `RcModelDataUI`

| Field | Description |
| --- | --- |
| `modelName` | Model name |
| `modelImg` | Model image (path or resource URI) |
| `filePath` | Model file path |
| `modelIdx` | Model index (`int64_t`) |
| `modelMatch` | ELRS MODELMATCH number |

### 22.11 Firmware Information `FirmwareInfUI`

| Field | Description |
| --- | --- |
| `SysId` | 32-bit system ID |
| `DevId` | 32-bit device unique ID |
| `FlyTime` | Total flight time (seconds) |
| `BuildTime` | Firmware build date (YYMMDD) |
| `VER_FW` | Firmware version: `0x01020003` → `01.02.0003` |
| `VER_HW` | Hardware version: `0x00000102` → `01.02` |
| `DevText` | Device description string |
| `FwVerText` | Firmware version string |
| `BulidTimeText` | Firmware build time text |
| `cacheFwFileVerTxt` | Version string of the downloaded cached firmware |

### 22.12 APP Update Information `AppUpdateInfoUI`

| Field | Description |
| --- | --- |
| `downloadBytesReceived` / `downloadBytesTotal` | Bytes downloaded / total bytes |
| `apkUrl` | APK download URL |
| `version` | Latest version number |
| `cnUpdateContent` / `enUpdateContent` | Chinese/English release notes |
| `cacheAppFile` | Local cache file after the download completes |

### 22.13 ELRS Types

`ElrsSetItemUI` (menu item):

| Field | Description |
| --- | --- |
| `idx` / `parentIdx` | Own/parent item index |
| `val` | Current value |
| `type` | Item type |
| `itemEnabled` / `itemVisible` | Whether it is available/visible |
| `name` | Item name |
| `options` | List of candidate values (for option-type items) |
| `deviceName` | Device name |
| `units` | Units |

`ElrsSetEndAckUI`: `type` / `status` / `lastSetIdx` / `options`.

`ElrsStatusInfoUI`: `badPkt` (number of bad packets), `goodPkt` (number of good packets),
`modelMismatch` (non-zero means a model mismatch).

### 22.14 Telemetry Types

`TelemetryDataUI` (ELRS return snapshot):

| Field | Description |
| --- | --- |
| `lat` / `lng` | Latitude / longitude |
| `pitch` / `roll` / `yaw` | Attitude angles |
| `voltage` / `current` | Voltage / current |

`RadioStatusUI` (basic transmitter status):

| Field | Default | Description |
| --- | --- | --- |
| `rfLQ` | 0 | Receiver link quality (LQ, returned by the receiver) |
| `rfLDBM` | 0 | Receiver signal strength in dBm (returned) |
| `rfLLostCnt` | 0 | Data loss counter; =0 means the receiver is lost |
| `rcVtg` | 0 | Transmitter voltage (`float`) |
| `rxVtg` | 0 | Aircraft/receiver returned voltage (`float`) |
| `topKeyState` | 0 | Top key state |
| `adcSetState` | 0 | ADC calibration state |

`TelemetryNotifyUI` (common type for telemetry items and notification configuration):

| Field | Description |
| --- | --- |
| `hashValue` | Hash of the associated returned item (`uint64_t`; do not store in 32 bits) |
| `notifyName` / `notifyText` | Notification name/text |
| `popupWindow` | Whether to show a popup |
| `triggerType` | Trigger type: 1 greater than / 2 equal to / 3 less than |
| `intervalSec` | Notification interval (seconds) |
| `triggerValue` / `currValue` | Trigger value/current value (`double`) |
| `valueUnit` | Value unit |
| `audioFile` | Alert audio file |
| `valTxt` / `nameTxt` | Value text/name text |
| `idx` | Index |
| `showUI` | Whether to show in the UI |

> `TelemetryItemUI` is a reserved structure with no public API using it at present (see 18.4).

### 22.15 Message Infrastructure

```cpp
enum RcMessageId : std::uint32_t { /* see the message table in 8.2 */ };

struct SdkMessage {
    std::uint32_t msgId;
    std::shared_ptr<const void> payload;
};
```

---

## 23. Return Values and Error Handling Conventions

All public APIs are `noexcept`; **exceptions do not cross the dynamic library boundary**.

| API type | When the SDK is unavailable or the call fails |
| --- | --- |
| `std::vector` / `std::list` getters | Empty container |
| Struct getters | A value-initialized default struct |
| `rcGetCurrentModelCfgIndex()` | `-1` |
| `rcDelMixCfgData()` | `-1` |
| `rcSetDrData()` / `rcDelDrData()` | `-1` |
| `rcSetCurveCfg()` | `-1` |
| `rcImportModelCfg()` | `-1` |
| `rcTaskUpdateApp()` | `-1` |
| `rcRequestElrsStatus()` | `-1` |
| `RCgetTelemetryItemNotify()` | `-1` |
| `rcCurveApply()` | Returns `rawVal` unchanged |
| `initialize()` | Returns an `InitResult` enum value |
| `setMessageCallback()` / `postMessage()` | `false` |
| `void` setter/task APIs | No action is taken; failure cannot be detected from the return value |

Notes:

- An empty container or a default struct may mean either "there really is no data" or "the call
  failed"; judge by combining the lifecycle state, device completion messages, and business state;
- Indices, paths, URLs, and protocol parameters are validated by the caller. Some internal
  implementations access arrays directly by index, so **out-of-range parameters are not a
  recoverable business error**.

---

## 24. Best Practices

1. **Do not treat getters as device truth before the device is ready**: after `initialize()`,
   local models can be read immediately, but the device configuration must wait for
   `RC_MSGID_GETDEVCFG_END`.
2. **Never touch the UI directly from the callback**: validate the message → copy the payload into
   a host value type → post it to the host thread queue/event loop → return immediately. No
   networking, disk IO, lock waits, or long computations.
3. **High-frequency messages must be coalesced or throttled**: `RC_MSGID_MIXOUT_UPDATE` arrives
   about every 40 ms; `RC_MSGID_TELEMETRY_UPDATED` may appear continuously with telemetry frames.
   Overwrite with the latest value plus periodic batch refreshes, and pause UI updates while the
   page is not visible.
4. **Distinguish the save semantics**: `rcSet*` changes the working configuration/preview;
   `rcSaveCurrentCfgToModel(model)` saves into the specified local model;
   `taskSaveConfigToDevice()` asynchronously writes to the device (and saves the current model on
   completion); `rcSetModelCfg(model)` only edits an existing model's metadata.
5. **Do not use 32-bit int for large integers across the binding layer**: `hashValue` is 64-bit;
   the script binding layer should use a 64-bit integer or a string.
6. **Use only public payload types**: use `RcTimeReportUI` and `RcTimeNotifyUI` for timer/counter
   messages; include only the public headers and do not reference or copy internal protocol
   structures.
7. **Release cross-boundary objects before unloading the dynamic library**: make sure the callback
   has been cleared, the SDK has been shut down, and every `SdkMessage`, payload, and
   SDK-returned container/string held by the host has been destroyed before unloading the library.
8. **Android STL consistency**: exactly one `libc++_shared.so` in the whole project, identical to
   the one provided in the distribution; keep arm64-v8a as the only ABI.
9. **Read before writing when modifying configuration**: fetch the full structure with `rcGet*` →
   change only the target fields → write back with `rcSet*`, so that default-constructed local
   variables do not overwrite other configuration items.
10. **Follow the request cadence on the ELRS page**: display after the menu fetch completes; wait
    for `ELRS_SET_ACK` after a setting; stop repeated requests when leaving the page.

---

## 25. Frequently Asked Questions (FAQ)

**Q1: Loading `librmAxSdk1.so` reports `dlopen failed: library "libQt6Core_arm64-v8a.so" not found`?**
The Qt runtime libraries are missing from the APK. Confirm that all 7 `.so` files are in
`jniLibs/arm64-v8a/` and that `packagingOptions` does not exclude them. Do not rename the Qt
library files.

**Q2: ABI-related `unsatisfied link error`, or `INSTALL_FAILED_NO_MATCHING_ABIS` at install time?**
The host project includes an ABI that the SDK does not support. Narrow `abiFilters` to
`arm64-v8a` only, or make sure the variants for other ABIs do not reference the SDK.

**Q3: `initialize()` returns `ThreadStartFailed` / `CommunicationInitFailed`?**
Check that the `appDataPath` directory exists and is readable and writable; confirm the process has
not exceeded its thread limit; retry with full logging enabled. After fixing the problem,
`initialize()` may be called again (calling `shutdown()` first is safer).

**Q4: After initialization, `rcGetChOutList()` and similar calls return empty or default values?**
The device configuration has not finished loading. Call `taskReadConfigFromDevice()` and wait for
`RC_MSGID_GETDEVCFG_END`; do not sleep for a fixed time.

**Q5: Refreshing the UI inside the callback stutters or crashes?**
The callback runs on the SDK output dispatch thread. Post the data to the UI thread as described
in 24.2.

**Q6: The firmware update progress shows `-1` / `-2`?**
`-1` means no available firmware (first check `rcGetFwInf().cacheFwFileVerTxt` and the network
download flow); `-2` means the update failed — check the device connection and retry.

**Q7: `rcDelDrData()` returns 0 but the DR does not appear to be deleted?**
This is a known limitation of the current implementation (see 13.2): the deletion is not actually
persistently applied. Avoid relying on this interface for deletion flows, or contact the SDK
maintainer to confirm the behavior in a later version.

**Q8: Mix deletion always fails?**
The deletion key is `(chIdx, srcIdx)` rather than `(chIdx, mixIdx)` (see 11.2).

**Q9: `TelemetryNotifyUI`'s `hashValue` does not match after being stored in SQLite/JS?**
That field is a 64-bit unsigned integer. Use `INTEGER` in SQLite and `BigInt` or a string in the
JS/script layer; truncating to 32 bits is not allowed.

**Q10: Configuration changes are lost after restarting the APP?**
Only `rcSet*` was called and nothing was saved. Choose `taskSaveConfigToDevice()` or
`rcSaveCurrentCfgToModel()` according to the semantics in 24.4.

---

## 26. Appendix

### Appendix A: Test Console Command Reference (`tst_rmAxSdkApi.cpp`)

The test program is an interactive console: after startup, you type commands to call the
corresponding SDK APIs. It can serve as a reference script for integration self-testing:

| Command | Purpose |
| --- | --- |
| `help` | Show the command list |
| `version` / `status` | Show the SDK version / initialization status |
| `init` / `shutdown` | Initialize / shut down the SDK |
| `postMsg` | Post a test message to the SDK input queue |
| `saveConfig` / `readConfig` | `taskSaveConfigToDevice` / `taskReadConfigFromDevice` |
| `getSrcCfg` | List the input source configuration (and demonstrate raw value conversion for Ail/Ele/Thr/Rud) |
| `getChOutCfg` | List the output channel configuration |
| `getTelemetry [uiOnly]` | List telemetry items (0 for all, non-zero for UI items only) |
| `getMixCfg <ch> <mix>` | Show one mix configuration |
| `getTimerCfg <timer>` | Show one timer configuration |
| `getRadioStatus` | Show the basic transmitter status |
| `getCurve [idx]` | List all curves or a specific curve |
| `setCurve <idx> <type> <ptNum> <x0> <y0> ...` | Update a curve configuration |
| `applyCurve <idx> <raw>` | Apply the curve calculation to an input value |
| `getFwInf` / `requestFwInf` | Read cached firmware information / request firmware information from the device |
| `requestElrsParameters` | Request the ELRS parameter menu (address 0xEE) |
| `requestElrsStatus` | Request the ELRS status |
| `startUpdateFw` | Start the device firmware update |
| `getAppInfo` | Query APP version information from the server |
| `updateApp <url>` / `abortAppUpdate` | Start/abort the APP update download |
| `getModels` / `createModel` | List models / create a new model |
| `setModel <file> <name> <image> <index> <match>` | Modify model metadata |
| `exportModel <src> <dst>` / `importModel <src> <dst>` | Export/import a model |
| `deleteModel <file>` | Delete a model file |
| `getTemplates` / `modelToTemplate <file> <name>` | Template list / save a model as a template |
| `quit` / `exit` | Shut down the SDK and exit |

> Note: this source includes `rmAxSdkTestConsole.h`, which is not shipped with the package, so it
> cannot be compiled directly; it is for reference only.

### Appendix B: Comparison Conditions and Enum Quick Reference

| Scenario | Values |
| --- | --- |
| Switch comparison condition `swCmpType` / `rstSwCmpType` / `cmpType` | 0 disabled / 1 greater than / 2 equal to / 3 less than |
| Failsafe `fsMode` | 0 none / 1 Hold / 2 No pulses / 3 Custom |
| RF module `rfModuleMode` | 0 internal ELRS / 1 external ELRS / 2 / 0xff off |
| Mix type `mixesMode` | 0 replace / 1 add / 2 multiply (header comments) |
| Curve type `cType` | 0 none / 1 DIFF / 2 EXPO / 3 FUNC / 4 custom |
| Special curve index | 0xff means apply no curve |
| CRSF frame format | ADDR LEN TYPE DATA CRC (total length LEN+2; CRC8-D5 covers TYPE+DATA) |
| ELRS transmitter address | 0xEE (rely on the actual CRSF device address) |

### Appendix C: License Information

- The Qt6 runtime libraries inside the SDK distribution package follow the Qt open source license;
  the text is in `licenses/Qt-LICENSE.txt`. Complete a compliance assessment before a commercial
  release.

### Appendix D: Glossary

| Term | Meaning |
| --- | --- |
| RC | Radio Control, remote-controlled models |
| ELRS | ExpressLRS, an open source RC radio link protocol |
| CRSF | Crossfire serial protocol (TBS), compatible with ELRS |
| Mixing | Control logic that combines multiple input sources into one output channel |
| DR | Dual Rate (switching the control surface travel ratio) |
| Failsafe / FS | The channel output policy when the link is lost |
| Pre-Flight Check / PFC | A channel position check performed before takeoff |
| Telemetry | Flight data returned by the receiver (voltage, position, attitude, etc.) |
| LQ / dBm | Link quality / signal strength |
| MODELMATCH | The ELRS mechanism that binds a transmitter to a model |
| ABI | Application Binary Interface; this package provides arm64-v8a only |

---

*This manual was compiled from the public headers in the release package,
`SDK_API_USAGE.zh-CN.md`, and `tst_rmAxSdkApi.cpp`. If behavior changes in a later SDK version,
the latest headers and the official usage notes take precedence.*
