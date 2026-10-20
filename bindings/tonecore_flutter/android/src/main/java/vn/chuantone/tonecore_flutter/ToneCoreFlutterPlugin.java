package vn.chuantone.tonecore_flutter;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import androidx.annotation.NonNull;

import io.flutter.embedding.engine.plugins.FlutterPlugin;
import io.flutter.embedding.engine.plugins.activity.ActivityAware;
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding;
import io.flutter.plugin.common.EventChannel;
import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;
import io.flutter.plugin.common.PluginRegistry;

/**
 * Phần Java nhỏ của plugin:
 *  - Báo loại thiết bị âm thanh đầu ra (loa, tai nghe có dây, Bluetooth, USB) lên Dart, vì NDK
 *    không đọc được thông tin này. Dart chuyển tiếp cho engine bằng tc_set_route_hint.
 *    Giá trị gửi lên khớp enum tc_route trong tonecore.h.
 *  - Xin quyền micro (RECORD_AUDIO) lúc chạy.
 */
public class ToneCoreFlutterPlugin
    implements FlutterPlugin,
        EventChannel.StreamHandler,
        MethodChannel.MethodCallHandler,
        ActivityAware,
        PluginRegistry.RequestPermissionsResultListener {
  private static final int MIC_REQUEST_CODE = 0x7C01;
  // Khớp tc_route
  private static final int ROUTE_UNKNOWN = 0;
  private static final int ROUTE_SPEAKER = 1;
  private static final int ROUTE_WIRED = 2;
  private static final int ROUTE_BLUETOOTH = 3;
  private static final int ROUTE_USB = 4;

  private EventChannel channel;
  private MethodChannel permissionChannel;
  private Context appContext;
  private ActivityPluginBinding activityBinding;
  private MethodChannel.Result pendingMicResult;
  private AudioManager audioManager;
  private EventChannel.EventSink sink;
  private final Handler mainHandler = new Handler(Looper.getMainLooper());

  private final AudioDeviceCallback deviceCallback = new AudioDeviceCallback() {
    @Override
    public void onAudioDevicesAdded(AudioDeviceInfo[] added) {
      emit();
    }

    @Override
    public void onAudioDevicesRemoved(AudioDeviceInfo[] removed) {
      emit();
    }
  };

  @Override
  public void onAttachedToEngine(@NonNull FlutterPluginBinding binding) {
    appContext = binding.getApplicationContext();
    audioManager = (AudioManager) appContext.getSystemService(Context.AUDIO_SERVICE);
    channel = new EventChannel(binding.getBinaryMessenger(), "tonecore_flutter/route");
    channel.setStreamHandler(this);
    permissionChannel = new MethodChannel(binding.getBinaryMessenger(), "tonecore_flutter/permission");
    permissionChannel.setMethodCallHandler(this);
  }

  @Override
  public void onDetachedFromEngine(@NonNull FlutterPluginBinding binding) {
    onCancel(null);
    channel.setStreamHandler(null);
    channel = null;
    permissionChannel.setMethodCallHandler(null);
    permissionChannel = null;
  }

  // ---- Quyền micro ----

  @Override
  public void onMethodCall(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
    if (!"requestMicrophone".equals(call.method)) {
      result.notImplemented();
      return;
    }
    if (appContext.checkSelfPermission(Manifest.permission.RECORD_AUDIO)
        == PackageManager.PERMISSION_GRANTED) {
      result.success(true);
      return;
    }
    Activity activity = activityBinding != null ? activityBinding.getActivity() : null;
    if (activity == null || pendingMicResult != null) {
      result.success(false);
      return;
    }
    pendingMicResult = result;
    activity.requestPermissions(new String[] {Manifest.permission.RECORD_AUDIO}, MIC_REQUEST_CODE);
  }

  @Override
  public boolean onRequestPermissionsResult(
      int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
    if (requestCode != MIC_REQUEST_CODE || pendingMicResult == null) return false;
    boolean granted =
        grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED;
    pendingMicResult.success(granted);
    pendingMicResult = null;
    return true;
  }

  @Override
  public void onAttachedToActivity(@NonNull ActivityPluginBinding binding) {
    activityBinding = binding;
    binding.addRequestPermissionsResultListener(this);
  }

  @Override
  public void onDetachedFromActivityForConfigChanges() {
    onDetachedFromActivity();
  }

  @Override
  public void onReattachedToActivityForConfigChanges(@NonNull ActivityPluginBinding binding) {
    onAttachedToActivity(binding);
  }

  @Override
  public void onDetachedFromActivity() {
    if (activityBinding != null) activityBinding.removeRequestPermissionsResultListener(this);
    activityBinding = null;
    if (pendingMicResult != null) {
      pendingMicResult.success(false);
      pendingMicResult = null;
    }
  }

  @Override
  public void onListen(Object arguments, EventChannel.EventSink events) {
    sink = events;
    audioManager.registerAudioDeviceCallback(deviceCallback, mainHandler);
    emit();
  }

  @Override
  public void onCancel(Object arguments) {
    if (audioManager != null) audioManager.unregisterAudioDeviceCallback(deviceCallback);
    sink = null;
  }

  private void emit() {
    if (sink != null) sink.success(currentRoute());
  }

  /**
   * Android tự chuyển âm thanh media sang thiết bị cắm sau cùng; ưu tiên ở đây theo cách hệ thống
   * thường chọn: USB > có dây > Bluetooth > loa ngoài.
   */
  private int currentRoute() {
    if (audioManager == null) return ROUTE_UNKNOWN;
    boolean usb = false;
    boolean wired = false;
    boolean bluetooth = false;
    for (AudioDeviceInfo d : audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
      switch (d.getType()) {
        case AudioDeviceInfo.TYPE_USB_HEADSET:
        case AudioDeviceInfo.TYPE_USB_DEVICE:
        case AudioDeviceInfo.TYPE_USB_ACCESSORY:
          usb = true;
          break;
        case AudioDeviceInfo.TYPE_WIRED_HEADSET:
        case AudioDeviceInfo.TYPE_WIRED_HEADPHONES:
        case AudioDeviceInfo.TYPE_LINE_ANALOG:
          wired = true;
          break;
        case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP:
        case AudioDeviceInfo.TYPE_BLUETOOTH_SCO:
          bluetooth = true;
          break;
        default:
          if (Build.VERSION.SDK_INT >= 31
              && (d.getType() == AudioDeviceInfo.TYPE_BLE_HEADSET
                  || d.getType() == AudioDeviceInfo.TYPE_BLE_SPEAKER)) {
            bluetooth = true;
          }
          break;
      }
    }
    if (usb) return ROUTE_USB;
    if (wired) return ROUTE_WIRED;
    if (bluetooth) return ROUTE_BLUETOOTH;
    return ROUTE_SPEAKER;
  }
}
