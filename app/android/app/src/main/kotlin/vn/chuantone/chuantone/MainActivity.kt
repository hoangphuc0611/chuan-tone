package vn.chuantone.chuantone

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.OpenableColumns
import android.provider.Settings
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import java.io.File
import java.util.concurrent.Executors

/**
 * "Mở bằng ChuẩnTone" (M8 B1): nhận file nhạc từ app khác (intent VIEW / SEND, kiểu `audio` bất kỳ), chép vào
 * `cache/incoming/<mốc>/<tên gốc>` rồi xếp hàng chờ; Dart lấy qua kênh `chuantone/incoming`
 * (`take`, báo `available` khi có file mới) — cùng giao thức với plugin iOS.
 */
class MainActivity : FlutterActivity() {
    private var channel: MethodChannel? = null
    private val pending = mutableListOf<Map<String, String>>()
    private val io = Executors.newSingleThreadExecutor()

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        channel = MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "chuantone/incoming").apply {
            setMethodCallHandler { call, result ->
                if (call.method == "take") {
                    val files = synchronized(pending) { pending.toList().also { pending.clear() } }
                    result.success(files)
                } else {
                    result.notImplemented()
                }
            }
        }
        // Quyền micro (M8 B5, hướng dẫn lần đầu): xem trạng thái, mở trang Cài đặt của app. Xin quyền
        // do plugin tonecore_flutter làm.
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "chuantone/system").setMethodCallHandler { call, result ->
            when (call.method) {
                "micPermission" -> result.success(
                    if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
                        "granted"
                    } else {
                        "undetermined"
                    }
                )
                "openAppSettings" -> {
                    startActivity(
                        Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", packageName, null))
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    )
                    result.success(null)
                }
                else -> result.notImplemented()
            }
        }
        handleIntent(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(intent: Intent?) {
        if (intent == null || intent.getBooleanExtra(HANDLED, false)) return
        val uri: Uri = when (intent.action) {
            Intent.ACTION_VIEW -> intent.data
            Intent.ACTION_SEND ->
                if (Build.VERSION.SDK_INT >= 33) {
                    intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
                } else {
                    @Suppress("DEPRECATION")
                    intent.getParcelableExtra<Uri>(Intent.EXTRA_STREAM)
                }
            else -> null
        } ?: return
        intent.putExtra(HANDLED, true) // tạo lại Activity không nhận lại cùng file
        io.execute {
            try {
                val name = (displayName(uri) ?: "bai-hat.mp3").replace('/', '_')
                val dir = File(cacheDir, "incoming/${System.nanoTime()}").apply { mkdirs() }
                val dest = File(dir, name)
                val input = contentResolver.openInputStream(uri) ?: return@execute
                input.use { src -> dest.outputStream().use { src.copyTo(it) } }
                synchronized(pending) { pending.add(mapOf("path" to dest.absolutePath, "name" to name)) }
                runOnUiThread { channel?.invokeMethod("available", null) }
            } catch (e: Exception) {
                // Không đọc được file (quyền / file hỏng): bỏ qua, app không báo gì thêm.
            }
        }
    }

    private fun displayName(uri: Uri): String? {
        if (uri.scheme == "file") return uri.lastPathSegment
        return contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { c -> if (c.moveToFirst()) c.getString(0) else null }
    }

    private companion object {
        const val HANDLED = "vn.chuantone.incoming.handled"
    }
}
