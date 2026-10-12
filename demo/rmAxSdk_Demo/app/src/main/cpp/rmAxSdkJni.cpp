// JNI 桥接层：封装 rmAxSdk1 C++ SDK，向 Kotlin 暴露简单接口。
#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // dlinfo / RTLD_DI_LINKMAP
#endif
//
// 宿主(Kotlin) -> SDK:  nativeVersion / nativeInitialize / nativeRequestDeviceInf /
//                      nativeGetFirmwareInfo / nativeShutdown
// SDK -> 宿主(Kotlin):  SDK 输出线程触发 onSdkMessage()，把固件信息消息
//                       转发到 RmAxSdkBridge.dispatchFromNative()。
#include <jni.h>

#include <android/log.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <csignal>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>
#include <cstring>
#include <cerrno>
#include <unwind.h>
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "rmAxSdkApi.h"

#define LOG_TAG "RmAxSdkJni"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace QtAndroidPrivate {
// libQt6Core 导出：缓存 JavaVM/JNIEnv 并注册 Qt Android 绑定类。
// 本 APP 不包含 Qt Android 绑定 Java 类，initJNI 会中途失败并返回 false，
// 但它一开始就会缓存 JavaVM（QJniEnvironment::getJniEnv 依赖它），
// 因此忽略返回值即可；缺失的只是 QtActivity 相关功能，SDK 用不到。
extern bool initJNI(JavaVM* vm, JNIEnv* env);
}  // namespace QtAndroidPrivate

namespace {

// ---------- 临时崩溃捕获（定位 initialize 闪退用，稳定后可移除） ----------
_Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg) {
    auto* frames = static_cast<std::vector<uintptr_t>*>(arg);
    if (frames->size() >= 32) return _URC_END_OF_STACK;
    frames->push_back(static_cast<uintptr_t>(_Unwind_GetIP(context)));
    return _URC_NO_REASON;
}

void crashHandler(int sig, siginfo_t*, void*) {
    std::vector<uintptr_t> frames;
    _Unwind_Backtrace(unwindCallback, &frames);
    std::string out = "CRASH sig=" + std::to_string(sig) + " frames=";
    for (uintptr_t pc : frames) {
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
            uintptr_t off = pc - reinterpret_cast<uintptr_t>(info.dli_fbase);
            char buf[160];
            std::snprintf(buf, sizeof(buf), "\n  %s +0x%zx (%s)",
                          info.dli_fname, off,
                          info.dli_sname ? info.dli_sname : "?");
            out += buf;
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "\n  0x%zx", pc);
            out += buf;
        }
    }
    __android_log_write(ANDROID_LOG_FATAL, LOG_TAG, out.c_str());
    std::_Exit(1);
}

void installCrashHandler() {
    struct sigaction sa {};
    sa.sa_sigaction = crashHandler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
}

// Qt 的 QSerialPort 打开串口前会在 QDir::tempPath() 下建 LCK..<端口名> 锁文件，
// 建不成就放弃打开。Android 应用对默认的 /data/local/tmp 没有写权限
// （/var/lock、/tmp 更是不存在），SDK 因此永远打不开串口。
// QDir::tempPath() 读 TMPDIR 环境变量，把它指向应用私有目录即可：
// 该目录必然可写、随应用卸载清理，且不需要 root。
// 必须在 SDK 初始化（创建 QCoreApplication）之前设置。
void redirectQtTempDir(const std::string& appDataPath) {
    const std::string tmpDir = appDataPath + "/qt_tmp";
    if (mkdir(tmpDir.c_str(), 0700) != 0 && errno != EEXIST) {
        LOGE("tmpdir: mkdir(%s) failed: %s", tmpDir.c_str(), std::strerror(errno));
        return;
    }
    if (setenv("TMPDIR", tmpDir.c_str(), 1) != 0) {
        LOGE("tmpdir: setenv failed: %s", std::strerror(errno));
        return;
    }
    LOGI("tmpdir: TMPDIR -> %s", tmpDir.c_str());
}

// 备用手段：若 TMPDIR 重定向仍不足以让 QSerialPort 打开串口，
// 运行时把 open() 中“加锁失败则放弃”的分支改为 NOP。
// 仅适用于本工程附带的 libQt6SerialPort_arm64-v8a.so（偏移 0x15648），
// Qt 版本变化后需重新用 objdump 校验。
void patchQtSerialPortLockCheck() {
    // 库是直接从未压缩的 APK 映射的，/proc/self/maps 行尾只显示 base.apk 而非
    // .so 名，按名字扫 maps 永远失败。改用 dlopen 拿句柄、dlinfo 取 link_map
    // 的加载基址。句柄故意不关闭：后续 SDK 还要用这个库。
    if (dlopen("libQt6SerialPort_arm64-v8a.so", RTLD_NOW | RTLD_GLOBAL) == nullptr) {
        LOGE("patch: dlopen(libQt6SerialPort) failed: %s", dlerror());
        return;
    }
    // Android NDK 没有 dlinfo，用 dl_iterate_phdr 按库名取加载基址。
    uintptr_t base = 0;
    dl_iterate_phdr(
        [](struct dl_phdr_info* info, std::size_t, void* data) -> int {
            if (info->dlpi_name != nullptr &&
                std::strstr(info->dlpi_name, "libQt6SerialPort") != nullptr) {
                *static_cast<uintptr_t*>(data) =
                    static_cast<uintptr_t>(info->dlpi_addr);
                return 1;  // 停止遍历
            }
            return 0;
        },
        &base);
    if (base == 0) {
        LOGE("patch: libQt6SerialPort base not found via dl_iterate_phdr");
        return;
    }
    LOGI("patch: libQt6SerialPort base=0x%zx", base);
    // open() 里有两道闸门都会跳到 0x15798 的 "Failed to create a lock file" 分支：
    //   +0x15628  cbz  x8, 0x15798   锁目录为空（Android 上无可写目录）
    //   +0x15648  tbz  w0, #0, ...   QLockFile::tryLock() 返回 false
    // 两条都要 NOP：只改一条仍会落进错误分支，串口照样打不开。
    struct PatchSite {
        std::size_t offset;
        std::uint32_t expect;
        const char* desc;
    };
    static constexpr PatchSite kSites[] = {
        {0x15628, 0xb4000b88u, "cbz (empty lock dir)"},
        {0x15648, 0x360005a0u, "tbz (tryLock failed)"},
    };

    // 先校验全部指令，避免只改一半导致库处于不一致状态。
    for (const auto& s : kSites) {
        const auto actual = *reinterpret_cast<volatile std::uint32_t*>(base + s.offset);
        if (actual != s.expect) {
            LOGE("patch: insn mismatch @+0x%zx: got 0x%08x want 0x%08x (Qt 版本变了?)",
                 s.offset, actual, s.expect);
            return;
        }
    }

    for (const auto& s : kSites) {
        auto* insn = reinterpret_cast<volatile std::uint32_t*>(base + s.offset);
        const uintptr_t page = reinterpret_cast<uintptr_t>(insn) & ~0xFFFULL;
        if (mprotect(reinterpret_cast<void*>(page), 0x1000,
                     PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
            LOGE("patch: mprotect @+0x%zx failed: %s", s.offset, std::strerror(errno));
            return;
        }
        *insn = 0xd503201Fu;  // NOP
        mprotect(reinterpret_cast<void*>(page), 0x1000, PROT_READ | PROT_EXEC);
        auto* p = reinterpret_cast<char*>(const_cast<std::uint32_t*>(insn));
        __builtin___clear_cache(p, p + 4);
        LOGI("patch: NOPed @+0x%zx %s", s.offset, s.desc);
    }
}
// --------------------------------------------------------------------------

JavaVM* g_vm = nullptr;
jclass g_bridgeClass = nullptr;    // RmAxSdkBridge 的全局引用
jmethodID g_dispatchMethod = nullptr;  // static dispatchFromNative(int, String)

// 把 SDK 消息分发到 Kotlin。回调运行在线程上，必要时自动 attach JVM。
void dispatchToKotlin(std::uint32_t msgId, const std::string& text) {
    if (g_vm == nullptr || g_bridgeClass == nullptr || g_dispatchMethod == nullptr) {
        return;
    }
    JNIEnv* env = nullptr;
    bool needDetach = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        needDetach = true;
    }
    jstring jtext = env->NewStringUTF(text.c_str());
    env->CallStaticVoidMethod(g_bridgeClass, g_dispatchMethod,
                              static_cast<jint>(msgId), jtext);
    env->DeleteLocalRef(jtext);
    if (needDetach) {
        g_vm->DetachCurrentThread();
    }
}

// 格式化 FirmwareInfUI 为多行文本。
std::string formatFirmwareInfo(const rmaxSdk::FirmwareInfUI& fw) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
                  "设备描述: %s\n"
                  "固件版本: %s (VER_FW=0x%08X)\n"
                  "硬件版本: 0x%08X\n"
                  "系统 ID: 0x%08X\n"
                  "设备 ID: 0x%08X\n"
                  "固件构建时间: %s\n"
                  "累计飞行时间: %u 秒\n"
                  "缓存固件版本: %s",
                  fw.DevText.c_str(), fw.FwVerText.c_str(), fw.VER_FW,
                  fw.VER_HW, fw.SysId, fw.DevId, fw.BulidTimeText.c_str(),
                  fw.FlyTime, fw.cacheFwFileVerTxt.c_str());
    return std::string(buf);
}

// ---------- CRSF 自定义遥测帧（Rotorflight，TYPE=0x88） ----------
// 帧: ADDR LEN TYPE FrameID [SID(2B,大端) 值...]... CRC
// 总长 LEN+2。每个 SID 查表决定值的字节数与解码方式。
// Rotorflight 统一大端序，所以不能直接 memcpy 到 packed 结构体（ARM 上会读反）。
constexpr std::uint8_t kCrsfTypeCustomTelem = 0x88;  // Rotorflight 自定义遥测
constexpr std::uint8_t kCrsfTypeBattery = 0x08;      // 标准 CRSF 电池帧

// 电池系统 SID 范围 0x1000-0x102F（Rotorflight 传感器表）
constexpr std::uint16_t kSidBatteryLo = 0x1000;
constexpr std::uint16_t kSidBatteryHi = 0x102F;
// ESC 系统 0x1040-0x106F，电源系统 0x1080-0x109F
constexpr std::uint16_t kSidEscLo = 0x1040;
constexpr std::uint16_t kSidEscHi = 0x106F;

// 解码方式
enum class Dec { U8, U16, S16, U32, U12U12, CellV };

struct SidDef {
    std::uint16_t sid;
    const char* name;
    const char* unit;
    Dec dec;
    double scale;   // 原始值除以此数得到物理值
    int precision;  // 显示小数位
};

// 电池系统传感器定义。长度由 Dec 决定，不再靠猜。
constexpr SidDef kBatterySids[] = {
    {0x1011, "Vbat", "V",   Dec::U16,    100.0, 2},  // 主电池电压
    {0x1012, "Curr", "A",   Dec::S16,     10.0, 1},  // 主电池电流（有符号）
    // 文档 5.2 写的是 decU32（4 字节），但实测本机飞控只发 2 字节：
    //   EA C8 88 | 10 13 00 00 | 10 81 02 DC | 10 82 01 F1
    // 按 4 字节解析会吃掉下一条记录的 SID，导致后续全部错位
    // （曾把 02DC 误读成 SID）。这里按实测的 2 字节。
    {0x1013, "Capa", "mAh", Dec::U16,      1.0, 0},  // 已用容量
    {0x1014, "Bat%", "%",   Dec::U8,       1.0, 0},  // 电量百分比
    {0x1020, "Cel#", "",    Dec::U8,       1.0, 0},  // 电芯数量
    {0x1021, "Vcel", "V",   Dec::U12U12, 1000.0, 3}, // 最低/平均电芯电压
    {0x102F, "Cels", "V",   Dec::CellV,  1000.0, 3}, // 所有电芯电压

    // 电压电流综合数据 0x1080-0x1087。文档只给了名称，没写解码类型和
    // scale，以下按 U16/100（电压）、S16/10（电流）推断 —— 实测
    // 0x1081=0x02DB 按 /100 得 7.31V，符合 BEC 输出电压的量级。
    // 如发现数值量级不对，调整这里的 scale 即可。
    {0x1080, "Vesc", "V",   Dec::U16,    100.0, 2},  // ESC 电压汇总
    {0x1081, "Vbec", "V",   Dec::U16,    100.0, 2},  // BEC 输出电压
    {0x1082, "Vbus", "V",   Dec::U16,    100.0, 2},  // 总线电压
    {0x1083, "Vmcu", "V",   Dec::U16,    100.0, 2},  // MCU 供电电压
    {0x1084, "Iesc", "A",   Dec::S16,     10.0, 1},  // ESC 电流汇总
    {0x1085, "Ibec", "A",   Dec::S16,     10.0, 1},  // BEC 输出电流
    {0x1086, "Ibus", "A",   Dec::S16,     10.0, 1},  // 总线电流
    {0x1087, "Imcu", "A",   Dec::S16,     10.0, 1},  // MCU 供电电流
    // 0x1088-0x1093 在文档里属于"电压电流综合数据 0x1080-0x1093"范围，
    // 但详解表只列到 0x1087，这几个没有官方名称。实测飞控确实在发
    // 0x1090-0x1093，不定义会让解析停在这里丢掉后续记录。
    // 按 U16 两字节假定（同组其他项都是 2 字节），名称暂用 SID 代替。
    {0x1090, "S1090", "",   Dec::U16,      1.0, 0},
    {0x1091, "S1091", "",   Dec::U16,      1.0, 0},
    {0x1092, "S1092", "",   Dec::U16,      1.0, 0},
    {0x1093, "S1093", "",   Dec::U16,      1.0, 0},

    // 0x10A0-0x10A3 文档未定义（电源系统到 0x109F，高度航向从 0x10B0 起）。
    // 长度由实测帧反推，均为 1 字节：
    //   EA C8 84 | 10 A0 00                    payloadLen=6，值后无剩余
    //   EA C8 79 | 10 A3 33 | 10 14 00         0x10A3 值=0x33，紧接下一个 SID
    {0x10A0, "S10A0", "",   Dec::U8,       1.0, 0},
    {0x10A1, "S10A1", "",   Dec::U8,       1.0, 0},
    {0x10A2, "S10A2", "",   Dec::U8,       1.0, 0},
    {0x10A3, "S10A3", "",   Dec::U8,       1.0, 0},

    // ESC#1 0x1041-0x1047。解码类型与单位取自文档 5.3 的表格；
    // scale 文档未给出，按电池那套惯例推断（电压 /100、电流 /10），
    // eRPM 和温度按原始值。实测量级不对就改这里。
    {0x1041, "EscV", "V",   Dec::U16,    100.0, 2},  // ESC 输入电压
    {0x1042, "EscI", "A",   Dec::U16,     10.0, 1},  // ESC 输入电流
    {0x1043, "EscC", "mAh", Dec::U32,      1.0, 0},  // ESC 累计消耗容量
    {0x1044, "EscR", "rpm", Dec::U16,      1.0, 0},  // 电子转速 eRPM
    {0x1045, "EscP", "%",   Dec::U16,      1.0, 0},  // PWM 输出百分比
    {0x1046, "Esc%", "%",   Dec::U8,       1.0, 0},  // 油门百分比
    {0x1047, "EscT", "C",  Dec::S16,      1.0, 0},  // ESC 温度

    // ESC#2 0x1051-0x1057，结构与 ESC#1 完全相同（文档 5.3）
    {0x1051, "Esc2V", "V",   Dec::U16,   100.0, 2},
    {0x1052, "Esc2I", "A",   Dec::U16,    10.0, 1},
    {0x1053, "Esc2C", "mAh", Dec::U32,     1.0, 0},
    {0x1054, "Esc2R", "rpm", Dec::U16,     1.0, 0},
    {0x1055, "Esc2P", "%",   Dec::U16,     1.0, 0},
    {0x1056, "Esc2%", "%",   Dec::U8,      1.0, 0},
    {0x1057, "Esc2T", "C",  Dec::S16,     1.0, 0},
};

// 各解码方式的值字节数；CellV 为变长，返回 -1 单独处理。
int decByteLen(Dec d) {
    switch (d) {
    case Dec::U8:     return 1;
    case Dec::U16:    return 2;
    case Dec::S16:    return 2;
    case Dec::U32:    return 4;
    case Dec::U12U12: return 3;
    case Dec::CellV:  return -1;
    }
    return -1;
}

const SidDef* findSidDef(std::uint16_t sid) {
    for (const auto& d : kBatterySids) {
        if (d.sid == sid) {
            return &d;
        }
    }
    return nullptr;
}

// 一个已解析的传感器读数
struct SensorValue {
    std::string name;
    std::string text;   // 已按 scale/precision 格式化
    std::int64_t updatedMs = 0;
};

std::mutex g_batteryMutex;
// SID → 最近读数。含电池/ESC/电源等所有已定义传感器。
// std::map 按 SID 升序，显示顺序稳定；分组由 Kotlin 侧按 SID 范围做。
std::map<std::uint16_t, SensorValue> g_batterySensors;

// 诊断：统计收到的 CRSF 帧类型，每 5 秒打印一次。
// 用来判断接收机到底发的是 0x88 还是别的类型，不然"收不到数据"
// 无法区分是没帧、帧类型不对、还是解析错了。
std::mutex g_frameStatMutex;
std::map<std::uint8_t, int> g_frameTypeCount;   // 5 秒窗口，打印后清零
std::map<std::uint8_t, int> g_frameTypeTotal;   // 累计，供界面显示
std::int64_t g_lastFrameStatMs = 0;
std::int64_t g_lastTelemLogMs = 0;  // 临时诊断用

// 未知 SID 统计。表里没有的 SID 会让解析停在那里，
// 这个计数能直接看出还缺哪些定义。
std::mutex g_unknownSidMutex;
std::map<std::uint16_t, int> g_unknownSids;

// 诊断用：最近一帧 0x88 的原始字节，显示到界面上
std::mutex g_rawFrameMutex;
std::string g_lastRawFrameHex;
int g_lastRawFrameLen = 0;

void recordFrameType(std::uint8_t type, std::int64_t nowMs) {
    std::lock_guard<std::mutex> lock(g_frameStatMutex);
    ++g_frameTypeCount[type];
    ++g_frameTypeTotal[type];
    if (g_lastFrameStatMs == 0) {
        g_lastFrameStatMs = nowMs;
        return;
    }
    if (nowMs - g_lastFrameStatMs < 5000) {
        return;
    }
    std::string s;
    for (const auto& [t, n] : g_frameTypeCount) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), " 0x%02X=%d", t, n);
        s += buf;
    }
    LOGI("CRSF 帧类型统计(5s):%s", s.c_str());
    g_frameTypeCount.clear();
    g_lastFrameStatMs = nowMs;
}

inline std::uint32_t be16(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 8) | p[1];
}
inline std::int32_t beS16(const std::uint8_t* p) {
    return static_cast<std::int16_t>(be16(p));
}
inline std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

// 按定义表格式化一个读数
std::string formatSensor(const SidDef& def, const std::uint8_t* v) {
    char buf[96];
    switch (def.dec) {
    case Dec::U8:
        std::snprintf(buf, sizeof(buf), "%.*f%s", def.precision,
                      v[0] / def.scale, def.unit);
        break;
    case Dec::U16:
        std::snprintf(buf, sizeof(buf), "%.*f%s", def.precision,
                      be16(v) / def.scale, def.unit);
        break;
    case Dec::S16:
        std::snprintf(buf, sizeof(buf), "%.*f%s", def.precision,
                      beS16(v) / def.scale, def.unit);
        break;
    case Dec::U32:
        std::snprintf(buf, sizeof(buf), "%.*f%s", def.precision,
                      be32(v) / def.scale, def.unit);
        break;
    case Dec::U12U12: {
        // 两个 12 位值打包进 3 字节：高 12 位 + 低 12 位
        const std::uint32_t raw =
            (static_cast<std::uint32_t>(v[0]) << 16) |
            (static_cast<std::uint32_t>(v[1]) << 8) | v[2];
        const std::uint32_t a = (raw >> 12) & 0xFFF;
        const std::uint32_t b = raw & 0xFFF;
        std::snprintf(buf, sizeof(buf), "最低 %.*f%s / 平均 %.*f%s",
                      def.precision, a / def.scale, def.unit,
                      def.precision, b / def.scale, def.unit);
        break;
    }
    case Dec::CellV:
        buf[0] = '\0';  // 变长，调用方单独处理
        break;
    }
    return std::string(buf);
}

// Rotorflight 自定义遥测帧（0x88）：遍历 SID 记录，取出电池系统
// (0x1000-0x102F) 的所有项目。一帧可能混合多种传感器。
void parseCustomTelem(const std::uint8_t* d, int payloadLen, std::int64_t nowMs) {
    // 实测 SDK 给的 payload 布局为: DST SRC FrameID [SID 值]...
    // （文档只写了 FrameID + 记录，没提 payload 内还重复一次地址）
    // DST=0xEA 遥控器, SRC=0xC8 飞控, 第三字节是递增的 Frame ID。
    if (payloadLen < 3) {
        return;
    }
    // 存一份原始字节供界面显示
    {
        std::string hex;
        for (int i = 0; i < payloadLen && i < 28; ++i) {
            char hb[4];
            std::snprintf(hb, sizeof(hb), "%02X ", d[i]);
            hex += hb;
        }
        std::lock_guard<std::mutex> lock(g_rawFrameMutex);
        g_lastRawFrameHex = hex;
        g_lastRawFrameLen = payloadLen;
    }
    int p = 3;
    std::map<std::uint16_t, SensorValue> found;

    while (p + 2 <= payloadLen) {
        const std::uint16_t sid = static_cast<std::uint16_t>(be16(d + p));
        const SidDef* def = findSidDef(sid);
        if (def == nullptr) {
            // 未知 SID：长度不明，无法安全跳过（文档的 Lua 实现默认跳 2 字节，
            // 但那是估算，猜错会让后续记录全部错位读出垃圾值）。
            // 记下来供诊断，然后停止解析本帧；已读到的记录照常生效。
            {
                std::lock_guard<std::mutex> lock(g_unknownSidMutex);
                ++g_unknownSids[sid];
            }
            // 未知 SID 的帧打印一次原始字节，用来反推它的值长度。
            // 每个 SID 只打一次，不会刷屏。
            {
                static std::map<std::uint16_t, bool> logged;
                if (!logged[sid]) {
                    logged[sid] = true;
                    std::string hex;
                    for (int i = 0; i < payloadLen && i < 28; ++i) {
                        char hb[4];
                        std::snprintf(hb, sizeof(hb), "%02X ", d[i]);
                        hex += hb;
                    }
                    LOGI("未知SID %04X 首次出现: payloadLen=%d p=%d raw: %s",
                         sid, payloadLen, p, hex.c_str());
                }
            }
            break;
        }
        p += 2;

        if (def->dec == Dec::CellV) {
            // 所有电芯电压：变长，每芯 12 位。剩余字节全部归它，
            // 因此只能作为帧内最后一个记录处理。
            const int rest = payloadLen - p;
            const int cells = (rest * 8) / 12;
            if (cells <= 0) {
                break;
            }
            std::string s;
            for (int i = 0; i < cells; ++i) {
                const int bitOff = i * 12;
                const int byteOff = bitOff / 8;
                if (p + byteOff + 1 >= payloadLen) {
                    break;
                }
                const std::uint32_t pair =
                    (static_cast<std::uint32_t>(d[p + byteOff]) << 8) |
                    d[p + byteOff + 1];
                const std::uint32_t mv =
                    (bitOff % 8 == 0) ? (pair >> 4) & 0xFFF : pair & 0xFFF;
                char cb[24];
                std::snprintf(cb, sizeof(cb), "%s%.3fV", i ? " " : "",
                              mv / def->scale);
                s += cb;
            }
            found[sid] = {def->name, s, nowMs};
            break;
        }

        const int vlen = decByteLen(def->dec);
        if (vlen < 0 || p + vlen > payloadLen) {
            break;  // 值被截断
        }
        found[sid] = {def->name, formatSensor(*def, d + p), nowMs};
        p += vlen;
    }

    if (!found.empty()) {
        std::lock_guard<std::mutex> lock(g_batteryMutex);
        for (auto& [sid, val] : found) {
            g_batterySensors[sid] = val;
        }
    }
    // 解析结果日志，2 秒一条用于诊断。界面上也有同样信息（nativeGetCrsfDiag）。
    if (nowMs - g_lastTelemLogMs > 2000) {
        g_lastTelemLogMs = nowMs;
        std::string hex;
        for (int i = 0; i < payloadLen && i < 32; ++i) {
            char hb[4];
            std::snprintf(hb, sizeof(hb), "%02X ", d[i]);
            hex += hb;
        }
        std::string parsed;
        for (const auto& [sid, v] : found) {
            char pb[64];
            std::snprintf(pb, sizeof(pb), "%04X=%s ", sid, v.text.c_str());
            parsed += pb;
        }
        std::string unk;
        {
            std::lock_guard<std::mutex> lock(g_unknownSidMutex);
            for (const auto& [sid, n] : g_unknownSids) {
                char ub[24];
                std::snprintf(ub, sizeof(ub), "%04X(%d) ", sid, n);
                unk += ub;
            }
        }
        LOGI("0x88 payloadLen=%d 解析到%zu项: %s| 停在p=%d | 未知SID: %s| raw: %s",
             payloadLen, found.size(), parsed.c_str(), p,
             unk.empty() ? "无 " : unk.c_str(), hex.c_str());
    }
}

// 标准 CRSF 电池帧（0x08）：定长 payload，大端，单位与 0x88 不同。
void parseStdBattery(const std::uint8_t* d, int payloadLen, std::int64_t nowMs) {
    if (payloadLen < 8) {
        return;
    }
    char buf[48];
    std::lock_guard<std::mutex> lock(g_batteryMutex);
    std::snprintf(buf, sizeof(buf), "%.1fV", be16(d) / 10.0);  // 标准为 0.1V
    g_batterySensors[0x1011] = {"Vbat", buf, nowMs};
    std::snprintf(buf, sizeof(buf), "%.1fA", be16(d + 2) / 10.0);
    g_batterySensors[0x1012] = {"Curr", buf, nowMs};
    const std::uint32_t mah = (static_cast<std::uint32_t>(d[4]) << 16) |
                              (static_cast<std::uint32_t>(d[5]) << 8) | d[6];
    std::snprintf(buf, sizeof(buf), "%umAh", mah);
    g_batterySensors[0x1013] = {"Capa", buf, nowMs};
    std::snprintf(buf, sizeof(buf), "%d%%", d[7]);
    g_batterySensors[0x1014] = {"Bat%", buf, nowMs};
}

// 解析一条完整 CRSF 帧。两种电池来源都支持：
// 0x88 Rotorflight 自定义遥测，0x08 标准电池帧。
void parseCrsfFrame(const std::vector<std::uint8_t>& f, std::int64_t nowMs) {
    if (f.size() < 4) {
        return;
    }
    const std::uint8_t len = f[1];
    if (f.size() < static_cast<std::size_t>(len) + 2u) {
        return;  // 帧不完整
    }
    // payload = LEN - 2（扣掉 TYPE 与 CRC），从下标 3 开始
    const int payloadLen = static_cast<int>(len) - 2;
    if (payloadLen <= 0) {
        return;
    }
    recordFrameType(f[2], nowMs);
    const std::uint8_t* d = f.data() + 3;
    switch (f[2]) {
    case kCrsfTypeCustomTelem:
        parseCustomTelem(d, payloadLen, nowMs);
        break;
    case kCrsfTypeBattery:
        parseStdBattery(d, payloadLen, nowMs);
        break;
    default:
        break;
    }
}
// ----------------------------------------------------------

// 记录上一条已打印的消息 ID 与时间，避免 MIXOUT_UPDATE 等高频率消息刷屏
std::uint32_t g_lastLoggedMsgId = 0;
int64_t g_lastLogMs = 0;

// SDK 统一输出消息回调（运行在 SDK 输出分发线程）。
void onSdkMessage(const rmaxSdk::SdkMessage& message, void* /*userData*/) {
    // 诊断：打印所有消息 ID（相同 ID 最多每 2 秒打印一次）
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (message.msgId != g_lastLoggedMsgId || now - g_lastLogMs > 2000) {
        LOGI("onSdkMessage: msgId=0x%08X payload=%s", message.msgId,
             message.payload ? "yes" : "no");
        g_lastLoggedMsgId = message.msgId;
        g_lastLogMs = now;
    }
    switch (message.msgId) {
    case rmaxSdk::RC_MSGID_FWINF_UPDATED: {
        const auto fw = rmaxSdk::rcGetFwInf();
        dispatchToKotlin(message.msgId, formatFirmwareInfo(fw));
        break;
    }
    case rmaxSdk::RC_MSGID_TRIM_NOTIFY:
        // 微调发生变化，通知界面立即刷新
        dispatchToKotlin(message.msgId, "trim");
        break;
    case rmaxSdk::RC_MSGID_MIXOUT_UPDATE:
        // 输入/输出/微调/遥控器状态已刷新
        dispatchToKotlin(message.msgId, "mixout");
        break;
    case rmaxSdk::RC_MSGID_GETDEVCFG_END:
        dispatchToKotlin(message.msgId, "设备配置读取完成");
        break;
    case rmaxSdk::RC_MSGID_FWVERSION_NOT_MATCH:
        dispatchToKotlin(message.msgId,
                         "设备固件版本过低或不兼容，无法读取固件信息");
        break;
    case rmaxSdk::RC_MSGID_CRSF_DATA_RECEIVED: {
        // 约 20Hz。在回调里就地解析并缓存，不上抛到 Kotlin，
        // 由 UI 侧按自己的节奏拉取（文档要求回调内立即复制 payload 快照）。
        if (!message.payload) {
            break;
        }
        const auto frame = std::static_pointer_cast<const std::vector<std::uint8_t>>(
            message.payload);
        parseCrsfFrame(*frame, now);
        break;
    }
    case rmaxSdk::RC_MSGID_TELEMETRY_UPDATED:
        // 遥测列表有变化；高频消息，仅通知，由 UI 侧节流后重新获取
        dispatchToKotlin(message.msgId, "telemetry");
        break;
    case rmaxSdk::RC_MSGID_TELEMETRY_LOST:
        dispatchToKotlin(message.msgId, "接收机遥测丢失");
        break;
    case rmaxSdk::RC_MSGID_TELEMETRY_RECOVER:
        dispatchToKotlin(message.msgId, "接收机遥测恢复");
        break;
    default:
        break;
    }
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    installCrashHandler();
    patchQtSerialPortLockCheck();
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    // 缓存 Qt 的 JavaVM（SDK 内部创建 QCoreApplication 时 QJniEnvironment 需要）
    const bool qtJniOk = QtAndroidPrivate::initJNI(vm, env);
    LOGI("QtAndroidPrivate::initJNI -> %s", qtJniOk ? "ok" : "failed (ignored)");

    jclass cls = env->FindClass("com/example/ax12/sdk/RmAxSdkBridge");
    if (cls == nullptr) {
        LOGE("FindClass(RmAxSdkBridge) failed");
        return JNI_ERR;
    }
    g_bridgeClass = static_cast<jclass>(env->NewGlobalRef(cls));
    g_dispatchMethod = env->GetStaticMethodID(cls, "dispatchFromNative",
                                              "(ILjava/lang/String;)V");
    env->DeleteLocalRef(cls);
    if (g_bridgeClass == nullptr || g_dispatchMethod == nullptr) {
        LOGE("GetStaticMethodID(dispatchFromNative) failed");
        return JNI_ERR;
    }
    LOGI("JNI_OnLoad ok");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_ax12_sdk_RmAxSdkBridge_nativeVersion(JNIEnv* env, jclass) {
    LOGI("nativeVersion enter");
    const auto v = std::string(rmaxSdk::version());
    LOGI("nativeVersion ok: %s", v.c_str());
    return env->NewStringUTF(v.c_str());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_example_ax12_sdk_RmAxSdkBridge_nativeInitialize(JNIEnv* env, jclass,
                                                         jstring appDataPath) {
    const char* path = env->GetStringUTFChars(appDataPath, nullptr);
    std::string pathStr(path != nullptr ? path : "");
    if (path != nullptr) {
        env->ReleaseStringUTFChars(appDataPath, path);
    }
    // 必须在 initialize() 之前：SDK 在其中创建 QCoreApplication 并打开串口，
    // QSerialPort 的锁文件要落在可写目录才不会打开失败。
    redirectQtTempDir(pathStr);
    LOGI("nativeInitialize: calling rmaxSdk::initialize...");
    const auto result = rmaxSdk::initialize(pathStr);
    LOGI("nativeInitialize: initialize returned %d", static_cast<int>(result));
    if (result == rmaxSdk::InitResult::Success ||
        result == rmaxSdk::InitResult::AlreadyInitialized) {
        rmaxSdk::setMessageCallback(&onSdkMessage, nullptr);
        LOGI("SDK initialized, appDataPath=%s", pathStr.c_str());
        return 0;
    }
    LOGE("SDK initialize failed: %d", static_cast<int>(result));
    return static_cast<jint>(result);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ax12_sdk_RmAxSdkBridge_nativeRequestDeviceInf(JNIEnv*, jclass) {
    rmaxSdk::rcTaskRequestDeviceInf();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ax12_sdk_RmAxSdkBridge_nativeReadConfigFromDevice(JNIEnv*, jclass) {
    rmaxSdk::taskReadConfigFromDevice();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_ax12_sdk_RmAxSdkBridge_nativeGetFirmwareInfo(JNIEnv* env, jclass) {
    return env->NewStringUTF(formatFirmwareInfo(rmaxSdk::rcGetFwInf()).c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_ax12_sdk_RmAxSdkBridge_nativeGetTrimming(JNIEnv* env, jclass) {
    const auto t = rmaxSdk::rcGetTrimming();
    // 顺序: trimLV(左垂直), trimLH(左水平), trimRV(右垂直), trimRH(右水平)
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d,%d,%d,%d",
                  t.trimLV, t.trimLH, t.trimRV, t.trimRH);
    return env->NewStringUTF(buf);
}

// 返回前 4 个输入通道（RUD/ELE/THR/AIL）。
// 格式: "名称:原始值:输出值;名称:原始值:输出值;..."，通道不足时返回已有部分。
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_ax12_sdk_RmAxSdkBridge_nativeGetMainChannels(JNIEnv* env, jclass) {
    const auto srcs = rmaxSdk::rcGetSrcCfgList();
    std::string out;
    for (std::size_t i = 0; i < srcs.size() && i < 4; ++i) {
        if (!out.empty()) {
            out += ';';
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s:%d:%d", srcs[i].srcName.c_str(),
                      srcs[i].rawVal, srcs[i].outputVal);
        out += buf;
    }
    return env->NewStringUTF(out.c_str());
}

// 返回遥测（回传）列表。uiOnly != 0 时只返回 showUI != 0 的项目。
// 遥测名称和文本是自由文本，可能含 ':' 或 ';'，因此用控制字符做分隔：
//   记录间用 \x1E(RS)，字段间用 \x1F(US)
// 字段顺序: idx, nameTxt, notifyName, valTxt, currValue, valueUnit, showUI
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_ax12_sdk_RmAxSdkBridge_nativeGetTelemetryList(JNIEnv* env, jclass,
                                                              jint uiOnly) {
    const auto items = rmaxSdk::rcGetTelemetryList(static_cast<int>(uiOnly));
    std::string out;
    out.reserve(items.size() * 64);
    for (const auto& it : items) {
        if (!out.empty()) {
            out += '\x1E';
        }
        char num[64];
        std::snprintf(num, sizeof(num), "%d\x1F", it.idx);
        out += num;
        out += it.nameTxt;    out += '\x1F';
        out += it.notifyName; out += '\x1F';
        out += it.valTxt;     out += '\x1F';
        std::snprintf(num, sizeof(num), "%.6g\x1F%d\x1F%d",
                      it.currValue, it.valueUnit, it.showUI);
        out += num;
    }
    return env->NewStringUTF(out.c_str());
}

// 返回最近一帧 CRSF 电池数据（TYPE=0x08）。
// 无数据时返回空串；否则:
//   "电压V:电流A:容量mAh:剩余%:温度℃:电池ID:数据龄ms"
// 温度字段在发送端未提供时为空。
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_ax12_sdk_RmAxSdkBridge_nativeGetBattery(JNIEnv* env, jclass) {
    std::map<std::uint16_t, SensorValue> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_batteryMutex);
        snapshot = g_batterySensors;
    }
    if (snapshot.empty()) {
        return env->NewStringUTF("");
    }
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    // 记录间 \x1E，字段间 \x1F: SID(十六进制) 名称 值 数据龄ms
    std::string out;
    for (const auto& [sid, v] : snapshot) {
        if (!out.empty()) {
            out += '\x1E';
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04X\x1F", sid);
        out += buf;
        out += v.name;
        out += '\x1F';
        out += v.text;
        std::snprintf(buf, sizeof(buf), "\x1F%lld",
                      static_cast<long long>(nowMs - v.updatedMs));
        out += buf;
    }
    return env->NewStringUTF(out.c_str());
}

// 诊断信息：最近一帧 0x88 原始字节、未知 SID 列表、各帧类型计数。
// 用来在界面上直接看清收到了什么，不必翻 logcat。
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_ax12_sdk_RmAxSdkBridge_nativeGetCrsfDiag(JNIEnv* env, jclass) {
    std::string out;

    {
        std::lock_guard<std::mutex> lock(g_frameStatMutex);
        out += "帧类型累计: ";
        if (g_frameTypeTotal.empty()) {
            out += "(未收到任何 CRSF 帧)";
        } else {
            for (const auto& [t, n] : g_frameTypeTotal) {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "0x%02X×%d ", t, n);
                out += buf;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_unknownSidMutex);
        out += "\n未知 SID: ";
        if (g_unknownSids.empty()) {
            out += "无";
        } else {
            for (const auto& [sid, n] : g_unknownSids) {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "%04X×%d ", sid, n);
                out += buf;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_rawFrameMutex);
        if (g_lastRawFrameLen > 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "\n0x88 payload(%dB):\n", g_lastRawFrameLen);
            out += buf;
            out += g_lastRawFrameHex;
        } else {
            out += "\n尚未收到 0x88 帧";
        }
    }
    return env->NewStringUTF(out.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ax12_sdk_RmAxSdkBridge_nativeShutdown(JNIEnv*, jclass) {
    rmaxSdk::setMessageCallback(nullptr, nullptr);
    rmaxSdk::shutdown();
}
