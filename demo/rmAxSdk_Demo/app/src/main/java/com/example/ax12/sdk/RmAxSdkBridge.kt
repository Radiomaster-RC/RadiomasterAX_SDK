package com.example.ax12.sdk

/**
 * rmAxSdk1 C++ SDK 的 Kotlin 桥接。
 *
 * 使用流程：
 * 1. [initialize] 传入应用私有数据目录；
 * 2. 设置 [listener] 接收固件信息等 SDK 事件；
 * 3. [requestDeviceInfo] 向设备请求固件信息，设备应答后经
 *    [Event.FIRMWARE_INFO] 回调返回格式化文本；
 * 4. 退出时调用 [shutdown]。
 */
class RmAxSdkBridge private constructor() {

    enum class Event {
        /** 固件信息已更新（payload 为格式化文本） */
        FIRMWARE_INFO,
        /** 设备固件版本过低或不兼容 */
        FIRMWARE_VERSION_MISMATCH,
        /** 微调数值发生变化 */
        TRIM_CHANGED,
        /** 输入/输出/微调/遥控器状态已刷新 */
        MIXOUT_UPDATED,
        /** 设备配置读取完成 */
        DEV_CFG_READY,
        /** 遥测列表已变化（高频，需节流后重新获取） */
        TELEMETRY_UPDATED,
        /** 接收机遥测丢失 */
        TELEMETRY_LOST,
        /** 接收机遥测恢复 */
        TELEMETRY_RECOVER,
    }

    fun interface Listener {
        fun onSdkEvent(event: Event, text: String)
    }

    @Volatile
    var listener: Listener? = null

    /** 返回 SDK 版本字符串，无需初始化即可调用。 */
    fun version(): String = nativeVersion()

    /** 初始化 SDK 并注册消息回调，成功返回 true。 */
    fun initialize(appDataPath: String): Boolean =
        nativeInitialize(appDataPath) == 0

    /** 异步请求设备信息；结果通过 [Event.FIRMWARE_INFO] 回调。 */
    fun requestDeviceInfo() = nativeRequestDeviceInf()

    /** 异步从设备读取全部配置（完成后触发 [Event.DEV_CFG_READY]）。 */
    fun readConfigFromDevice() = nativeReadConfigFromDevice()

    /** 直接读取当前缓存的固件信息（设备未连接时可能为空/默认值）。 */
    fun getFirmwareInfo(): String = nativeGetFirmwareInfo()

    /**
     * 读取微调状态。返回 [左垂直微调, 左水平微调, 右垂直微调, 右水平微调]。
     * 需在 SDK 初始化成功后调用。
     */
    fun getTrimming(): List<Int> =
        nativeGetTrimming().split(',').map { it.toIntOrNull() ?: 0 }

    /** 一个输入通道的实时值。 */
    data class Channel(val name: String, val rawVal: Int, val outputVal: Int)

    /**
     * 电池数据。来源有两种，JNI 层都会解析：
     * - Rotorflight 自定义遥测帧 0x88，SID 0x1011/0x1012/0x1013/0x1014/0x1020
     * - 标准 CRSF 电池帧 0x08
     * [cellCount] 为 0 表示未收到电芯数量。
     * [ageMs] 是这帧数据距今多久，用来判断是否已经不再更新。
     */
    data class BatterySensor(
        /** 传感器 ID，如 0x1011 */
        val sid: Int,
        /** 短名，如 "Vbat" */
        val name: String,
        /** 已格式化的值，含单位，如 "26.20V" */
        val valueText: String,
        /** 这帧数据距今多久 */
        val ageMs: Long
    )

    /**
     * CRSF 诊断文本：帧类型计数、未知 SID、最近一帧 0x88 的原始字节。
     * 用来判断「没数据」是没收到帧、帧类型不对，还是解析缺 SID 定义。
     */
    fun getCrsfDiag(): String = nativeGetCrsfDiag()

    /** 传感器分组，按 Rotorflight 的 SID 范围划分。 */
    enum class SensorGroup(val label: String, val range: IntRange) {
        BATTERY("电池系统", 0x1000..0x102F),
        ESC("ESC 系统", 0x1040..0x106F),
        POWER("电源系统", 0x1080..0x109F),
    }

    /**
     * 一次读取并按组分类，避免多次 JNI 调用。
     * 返回的 Map 只含实际收到过数据的组。
     */
    fun getSensorsByGroup(): Map<SensorGroup, List<BatterySensor>> {
        val all = getBatterySensors()
        if (all.isEmpty()) return emptyMap()
        return SensorGroup.entries
            .associateWith { g -> all.filter { it.sid in g.range } }
            .filterValues { it.isNotEmpty() }
    }

    /**
     * 读取所有已解析的传感器最近读数，按 SID 升序。
     * 只包含实际收到过的项目；从未收到任何帧时返回空列表。
     */
    fun getBatterySensors(): List<BatterySensor> {
        val raw = nativeGetBattery()
        if (raw.isEmpty()) return emptyList()
        return raw.split(RECORD_SEP).mapNotNull { record ->
            val f = record.split(FIELD_SEP)
            if (f.size < 4) return@mapNotNull null
            BatterySensor(
                sid = f[0].toIntOrNull(16) ?: return@mapNotNull null,
                name = f[1],
                valueText = f[2],
                ageMs = f[3].toLongOrNull() ?: 0L
            )
        }
    }

    /** 一个遥测（回传）项目。[valueText] 是 SDK already 格式化好的显示值。 */
    data class TelemetryItem(
        val idx: Int,
        val name: String,
        val rawName: String,
        val valueText: String,
        val value: Double,
        val unit: Int,
        val showUi: Boolean
    )

    /**
     * 读取遥测列表。[uiOnly] 为 true 时只返回标记了显示的项目。
     * 需在 SDK 初始化成功、且接收机有回传数据时才有内容。
     * 对应 SDK 的 `rcGetTelemetryList(int uiOnly)`。
     */
    fun getTelemetryList(uiOnly: Boolean = true): List<TelemetryItem> {
        val raw = nativeGetTelemetryList(if (uiOnly) 1 else 0)
        if (raw.isEmpty()) return emptyList()
        return raw.split(RECORD_SEP).mapNotNull { record ->
            val f = record.split(FIELD_SEP)
            if (f.size < 7) return@mapNotNull null
            TelemetryItem(
                idx = f[0].toIntOrNull() ?: -1,
                name = f[1],
                rawName = f[2],
                valueText = f[3],
                value = f[4].toDoubleOrNull() ?: 0.0,
                unit = f[5].toIntOrNull() ?: 0,
                showUi = (f[6].toIntOrNull() ?: 0) != 0
            )
        }
    }

    /**
     * 读取前 4 个主输入通道（RUD/ELE/THR/AIL）的实时值。
     * 需在设备配置读取完成（[Event.DEV_CFG_READY]）后调用才有有效数据。
     */
    fun getMainChannels(): List<Channel> =
        nativeGetMainChannels()
            .split(';')
            .filter { it.isNotEmpty() }
            .mapNotNull { entry ->
                val parts = entry.split(':')
                if (parts.size < 3) return@mapNotNull null
                Channel(
                    name = parts[0],
                    rawVal = parts[1].toIntOrNull() ?: 0,
                    outputVal = parts[2].toIntOrNull() ?: 0
                )
            }

    /** 清除回调并关闭 SDK。 */
    fun shutdown() = nativeShutdown()

    private external fun nativeVersion(): String
    private external fun nativeInitialize(appDataPath: String): Int
    private external fun nativeRequestDeviceInf()
    private external fun nativeReadConfigFromDevice()
    private external fun nativeGetFirmwareInfo(): String
    private external fun nativeGetTrimming(): String
    private external fun nativeGetMainChannels(): String
    private external fun nativeGetTelemetryList(uiOnly: Int): String
    private external fun nativeGetBattery(): String
    private external fun nativeGetCrsfDiag(): String
    private external fun nativeShutdown()

    companion object {
        init {
            // 注意：不要显式 System.loadLibrary("Qt6Core_arm64-v8a")，
            // Qt 的 JNI_OnLoad 在找不到 Qt Android 绑定类时会返回 JNI_ERR 导致加载失败。
            // JavaVM 的缓存改由 JNI 层直接调用 QtAndroidPrivate::initJNI 完成。
            System.loadLibrary("rmAxSdkJni")
        }

        val instance: RmAxSdkBridge by lazy { RmAxSdkBridge() }

        // nativeGetTelemetryList 的分隔符，与 JNI 层一致
        private const val RECORD_SEP = ''
        private const val FIELD_SEP = ''

        // 与 rmAxSdkTypes.h 中 RcMessageId 一致
        private const val MSG_ID_MIXOUT_UPDATE = 0x08000008
        private const val MSG_ID_GETDEVCFG_END = 0x0800000A
        private const val MSG_ID_TRIM_NOTIFY = 0x08000006
        private const val MSG_ID_FWVERSION_NOT_MATCH = 0x0800000F
        private const val MSG_ID_FWINF_UPDATED = 0x08000010
        private const val MSG_ID_TELEMETRY_UPDATED = 0x08000019
        private const val MSG_ID_TELEMETRY_LOST = 0x0800001A
        private const val MSG_ID_TELEMETRY_RECOVER = 0x0800001B

        @JvmStatic
        private fun dispatchFromNative(msgId: Int, text: String?) {
            val event = when (msgId) {
                MSG_ID_FWINF_UPDATED -> Event.FIRMWARE_INFO
                MSG_ID_FWVERSION_NOT_MATCH -> Event.FIRMWARE_VERSION_MISMATCH
                MSG_ID_TRIM_NOTIFY -> Event.TRIM_CHANGED
                MSG_ID_MIXOUT_UPDATE -> Event.MIXOUT_UPDATED
                MSG_ID_GETDEVCFG_END -> Event.DEV_CFG_READY
                MSG_ID_TELEMETRY_UPDATED -> Event.TELEMETRY_UPDATED
                MSG_ID_TELEMETRY_LOST -> Event.TELEMETRY_LOST
                MSG_ID_TELEMETRY_RECOVER -> Event.TELEMETRY_RECOVER
                else -> return
            }
            instance.listener?.onSdkEvent(event, text ?: "")
        }
    }
}
