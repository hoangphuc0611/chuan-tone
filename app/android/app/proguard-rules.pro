# Luật R8 cho bản release.
#
# shared_preferences_android gọi các hàm Kotlin của nó qua JNI (package jni/jnigen) nhưng không
# kèm luật giữ lại → R8 xoá/đổi tên các hàm đó, app báo NoSuchMethodError khi đọc SharedPreferences.
-keep class io.flutter.plugins.sharedpreferences.** { *; }
