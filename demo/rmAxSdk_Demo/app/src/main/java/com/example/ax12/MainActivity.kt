package com.example.ax12

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.lifecycleScope
import com.example.ax12.sdk.RmAxSdkBridge
import com.example.ax12.ui.theme.Ax12Theme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : ComponentActivity() {

    private val sdk = RmAxSdkBridge.instance

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            Ax12Theme {
                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
                    SdkInfoScreen(
                        sdk = sdk,
                        modifier = Modifier.padding(innerPadding)
                    )
                }
            }
        }

        // 后台线程初始化 SDK，避免阻塞主线程
        lifecycleScope.launch(Dispatchers.IO) {
            val ok = sdk.initialize(filesDir.absolutePath)
            withContext(Dispatchers.Main) {
                sdkState.value = if (ok) {
                    sdk.requestDeviceInfo()
                    // 必读：rcGetTrimming()/rcGetSrcCfgList() 等 getter 读的是本地
                    // 缓存，要靠这次拉取才会填充。完成后触发 DEV_CFG_READY。
                    sdk.readConfigFromDevice()
                    "SDK 初始化成功"
                } else {
                    "SDK 初始化失败"
                }
            }
        }
    }

    override fun onDestroy() {
        sdk.shutdown()
        super.onDestroy()
    }

    companion object {
        /** SDK 初始化状态，供 Compose 订阅 */
        val sdkState = mutableStateOf("正在初始化 SDK…")
    }
}

@Composable
fun SdkInfoScreen(sdk: RmAxSdkBridge, modifier: Modifier = Modifier) {
    val sdkVersion = remember { sdk.version() }
    val sdkState by remember { MainActivity.sdkState }
    var firmwareInfo by remember { mutableStateOf("尚未获取，等待设备连接…") }
    var trimming by remember { mutableStateOf(List(4) { 0 }) }
    var channels by remember { mutableStateOf(emptyList<RmAxSdkBridge.Channel>()) }
    var fwMismatch by remember { mutableStateOf(false) }
    var telemetry by remember { mutableStateOf(emptyList<RmAxSdkBridge.TelemetryItem>()) }
    var telemetryState by remember { mutableStateOf("等待回传数据…") }
    var sensorGroups by remember {
        mutableStateOf(
            emptyMap<RmAxSdkBridge.SensorGroup, List<RmAxSdkBridge.BatterySensor>>()
        )
    }
    var crsfDiag by remember { mutableStateOf("") }

    // 定时刷新微调数值：设备原始数据约 40ms 更新一次，界面按 500ms 轮询即可
    LaunchedEffect(sdkState) {
        if (!sdkState.startsWith("SDK 初始化成功")) return@LaunchedEffect
        while (true) {
            trimming = sdk.getTrimming()
            kotlinx.coroutines.delay(500)
        }
    }

    // 通道值随摇杆实时变化，用 100ms 轮询保证跟手，仍远低于 25Hz 消息频率
    LaunchedEffect(sdkState) {
        if (!sdkState.startsWith("SDK 初始化成功")) return@LaunchedEffect
        while (true) {
            channels = sdk.getMainChannels()
            kotlinx.coroutines.delay(100)
        }
    }

    // 电池数据：CRSF 帧约 20Hz，已在 JNI 层解析缓存，这里 300ms 取一次足够
    LaunchedEffect(sdkState) {
        if (!sdkState.startsWith("SDK 初始化成功")) return@LaunchedEffect
        while (true) {
            sensorGroups = sdk.getSensorsByGroup()
            crsfDiag = sdk.getCrsfDiag()
            kotlinx.coroutines.delay(300)
        }
    }

    // 遥测列表：TELEMETRY_UPDATED 是高频消息，文档建议 500ms~1.5s 节流。
    // 这里按 800ms 轮询，不在事件回调里直接刷新整个列表。
    LaunchedEffect(sdkState) {
        if (!sdkState.startsWith("SDK 初始化成功")) return@LaunchedEffect
        while (true) {
            telemetry = sdk.getTelemetryList(uiOnly = true)
            kotlinx.coroutines.delay(800)
        }
    }

    // 注册 SDK 事件回调（运行在 SDK 输出线程，直接写 Compose state 会切到主线程，安全）
    DisposableEffect(sdk) {
        val listener = RmAxSdkBridge.Listener { event, text ->
            when (event) {
                RmAxSdkBridge.Event.FIRMWARE_INFO -> firmwareInfo = text
                RmAxSdkBridge.Event.FIRMWARE_VERSION_MISMATCH -> {
                    firmwareInfo = text
                    fwMismatch = true
                }
                // 微调变化和配置读取完成时立即刷新。
                // MIXOUT_UPDATED 为 25Hz 高频消息，不在此处读取，
                // 由下方 500ms 轮询合并刷新，避免每秒 25 次 JNI 调用。
                RmAxSdkBridge.Event.TRIM_CHANGED -> trimming = sdk.getTrimming()
                // 配置读取完成后设备状态才就绪，此时再请求固件信息
                RmAxSdkBridge.Event.DEV_CFG_READY -> {
                    trimming = sdk.getTrimming()
                    sdk.requestDeviceInfo()
                }
                RmAxSdkBridge.Event.MIXOUT_UPDATED -> Unit
                // 列表刷新交给上面的 800ms 轮询，这里只记状态
                RmAxSdkBridge.Event.TELEMETRY_UPDATED -> telemetryState = "接收中"
                RmAxSdkBridge.Event.TELEMETRY_LOST -> telemetryState = "遥测丢失"
                RmAxSdkBridge.Event.TELEMETRY_RECOVER -> telemetryState = "遥测已恢复"
            }
        }
        sdk.listener = listener
        onDispose { sdk.listener = null }
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text("rmAxSdk 信息", style = MaterialTheme.typography.headlineSmall)

        InfoCard(title = "SDK 版本", content = sdkVersion.ifEmpty { "—" })
        InfoCard(title = "SDK 状态", content = sdkState)

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("微调状态 (Trimming)", style = MaterialTheme.typography.titleMedium)
                Text(
                    text = "左垂直: ${trimming[0]}   左水平: ${trimming[1]}\n" +
                            "右垂直: ${trimming[2]}   右水平: ${trimming[3]}",
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(top = 8.dp)
                )
                if (fwMismatch) {
                    Text(
                        text = "注意：SDK 报告设备固件版本不兼容 " +
                                "(RC_MSGID_FWVERSION_NOT_MATCH)，部分状态可能读不到。",
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(top = 8.dp)
                    )
                }
            }
        }

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("主通道输入 (4 通道)", style = MaterialTheme.typography.titleMedium)
                if (channels.isEmpty()) {
                    Text(
                        text = "等待设备配置读取完成…",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 8.dp)
                    )
                } else {
                    channels.forEach { ch ->
                        Text(
                            text = "${ch.name}: 原始 ${ch.rawVal}   输出 ${ch.outputVal}%",
                            style = MaterialTheme.typography.bodyMedium,
                            modifier = Modifier.padding(top = 8.dp)
                        )
                    }
                }
            }
        }

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("CRSF 自定义遥测", style = MaterialTheme.typography.titleMedium)
                if (sensorGroups.isEmpty()) {
                    Text(
                        text = "未收到传感器数据。需飞控通过 CRSF 0x88 帧回传。",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 8.dp)
                    )
                } else {
                    sensorGroups.forEach { (group, items) ->
                        Text(
                            text = "${group.label}  (SID %04X-%04X)".format(
                                group.range.first, group.range.last
                            ),
                            style = MaterialTheme.typography.titleSmall,
                            modifier = Modifier.padding(top = 10.dp)
                        )
                        items.forEach { s ->
                            val stale =
                                if (s.ageMs > 3000) "  (${s.ageMs / 1000}s 前)" else ""
                            Text(
                                text = "%04X  %-6s %s%s".format(
                                    s.sid, s.name, s.valueText, stale
                                ),
                                style = MaterialTheme.typography.bodyMedium,
                                modifier = Modifier.padding(top = 4.dp)
                            )
                        }
                    }
                }
                // 诊断：收到了什么帧、缺哪些 SID 定义、原始字节
                if (crsfDiag.isNotEmpty()) {
                    Text(
                        text = crsfDiag,
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(top = 12.dp)
                    )
                }
            }
        }

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    "遥测回传 (${telemetry.size} 项) — $telemetryState",
                    style = MaterialTheme.typography.titleMedium
                )
                if (telemetry.isEmpty()) {
                    Text(
                        text = "暂无回传项目。需接收机连接并有遥测数据。",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 8.dp)
                    )
                } else {
                    telemetry.forEach { item ->
                        Text(
                            text = "${item.name.ifEmpty { item.rawName }}: " +
                                    item.valueText.ifEmpty { item.value.toString() },
                            style = MaterialTheme.typography.bodyMedium,
                            modifier = Modifier.padding(top = 6.dp)
                        )
                    }
                }
            }
        }

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("设备固件信息", style = MaterialTheme.typography.titleMedium)
                Text(
                    text = firmwareInfo,
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(top = 8.dp)
                )
            }
        }

        Button(
            onClick = { sdk.requestDeviceInfo() },
            enabled = sdkState.startsWith("SDK 初始化成功"),
            modifier = Modifier.fillMaxWidth()
        ) {
            Text("重新请求设备固件信息")
        }
    }
}

@Composable
private fun InfoCard(title: String, content: String) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            Text(
                text = content,
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.padding(top = 4.dp)
            )
        }
    }
}

@Preview(showBackground = true)
@Composable
fun SdkInfoScreenPreview() {
    Ax12Theme {
        InfoCard(title = "SDK 版本", content = "2026-01-01 12:00:00")
    }
}
