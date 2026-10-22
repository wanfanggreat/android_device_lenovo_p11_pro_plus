# goodix_ts 触摸屏方向补偿
#
# 显示面板相对设备自然方向安装了 270°（原厂 vendor/build.prop:
# ro.surface_flinger.primary_display_orientation=ORIENTATION_270）。
# 触摸原始坐标是面板原生方向（2560x1600），InputReader 默认按显示旋转
# 直接映射，会差 90°。这里用 touch.orientation 抵消这个安装偏移。
touch.deviceType = touchScreen
touch.orientationAware = 1
touch.orientation = ORIENTATION_270
