#
# Copyright (C) 2024 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Resource overlays（功能适配走 overlay，不动 frameworks/base 源文件）
DEVICE_PACKAGE_OVERLAYS += device/lenovo/p11_pro_plus/overlay

# ION：4.19 内核只有 legacy /dev/ion，没有 dmabuf heap
$(call soong_config_set_bool,libion,legacy_impl,true)

# 触摸/手写笔方向补偿：显示面板安装了 270°
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/usr/idc/goodix_ts.idc:$(TARGET_COPY_OUT_SYSTEM)/usr/idc/goodix_ts.idc \
    $(LOCAL_PATH)/usr/idc/goodix_ts_pen.idc:$(TARGET_COPY_OUT_SYSTEM)/usr/idc/goodix_ts_pen.idc

# A/B
$(call inherit-product, $(SRC_TARGET_DIR)/product/virtual_ab_ota.mk)

PRODUCT_PACKAGES += \
    update_engine \
    update_engine_sideload \
    update_verifier

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_system=true \
    POSTINSTALL_PATH_system=system/bin/otapreopt_script \
    FILESYSTEM_TYPE_system=ext4 \
    POSTINSTALL_OPTIONAL_system=true

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_vendor=true \
    POSTINSTALL_PATH_vendor=bin/checkpoint_gc \
    FILESYSTEM_TYPE_vendor=ext4 \
    POSTINSTALL_OPTIONAL_vendor=true

PRODUCT_PACKAGES += \
    checkpoint_gc \
    otapreopt_script

# ANT+
PRODUCT_PACKAGES += \
    com.dsi.ant@1.0.vendor

# API levels
PRODUCT_SHIPPING_API_LEVEL := 30

# Audio
PRODUCT_PACKAGES += \
    android.hardware.audio@6.0-impl \
    android.hardware.audio.effect@6.0-impl \
    android.hardware.audio.service \
    android.hardware.bluetooth.audio-impl \
    android.hardware.soundtrigger@2.3-impl

PRODUCT_PACKAGES += \
    audio.bluetooth.default \
    audio.primary.kona \
    audio.r_submix.default \
    audio.usbv2.default \

# libsqlite.vendor：vendor.dolby.hardware.dms@2.0-service 经
# libdlbdsservice.so 依赖 libsqlite.so，缺库时该 HAL 5 秒崩溃循环；
# 同库也被 location 侧的 libizat_core/liblbs_core/xtra-daemon/xtwifi-client 使用。
PRODUCT_PACKAGES += \
    liba2dpoffload \
    libaudiopreprocessing \
    libbatterylistener \
    libbundlewrapper \
    libcomprcapture \
    libdownmix \
    libdynproc \
    libeffectproxy \
    libexthwplugin \
    libhdmiedid \
    libhfp \
    libldnhncr \
    libqcompostprocbundle \
    libqcomvisualizer \
    libqcomvoiceprocessing \
    libreverbwrapper \
    libsndmonitor \
    libspkrprot \
    libsqlite.vendor \
    libvisualizer \
    libvolumelistener

# Audio configs
PRODUCT_COPY_FILES += \
    $(call find-copy-subdir-files,*,$(LOCAL_PATH)/audio/,$(TARGET_COPY_OUT_VENDOR)/etc)

PRODUCT_COPY_FILES += \
    frameworks/av/services/audiopolicy/config/bluetooth_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml

# 原厂 vendor 中我们漏提取的音频/ADSP 文件（rfsa DSP 模块、ADSP PD 监听器、
# Cirrus 扬声器保护、WebRTC 前处理等）。
# 背景：采集拓扑 0x10018001 需要动态加载 DSP 模块，缺文件会让 DSP 拒绝
# ADM_CMD_DEVICE_OPEN_V8（见 MIC-TEST-RESULT.md / 2026-10-17 分析）。
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/Elevoc_vocplus_capi2.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/Elevoc_vocplus_capi2.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libQ6MSFR_manager_skel.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libQ6MSFR_manager_skel.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libapps_mem_heap.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libapps_mem_heap.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libbitml_nsp_skel.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libbitml_nsp_skel.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libcamera_nn_skel.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libcamera_nn_skel.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libcvpdsp_skel.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libcvpdsp_skel.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libdspCV_skel.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libdspCV_skel.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libdsp_streamer_binning.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libdsp_streamer_binning.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libfastcvadsp.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libfastcvadsp.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libfastcvdsp_skel.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libfastcvdsp_skel.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libhexagon_nn_skel.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libhexagon_nn_skel.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libscveObjectSegmentation_skel.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libscveObjectSegmentation_skel.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libscveT2T_skel.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libscveT2T_skel.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libsnpe_dsp_v65_domains_v2_skel.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libsnpe_dsp_v65_domains_v2_skel.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/rfsa/adsp/libsnpe_dsp_v66_domains_v2_skel.so:$(TARGET_COPY_OUT_VENDOR)/lib/rfsa/adsp/libsnpe_dsp_v66_domains_v2_skel.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/libadsp_default_listener.so:$(TARGET_COPY_OUT_VENDOR)/lib/libadsp_default_listener.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/libaudio_log_utils.so:$(TARGET_COPY_OUT_VENDOR)/lib/libaudio_log_utils.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/libaudioalsa.so:$(TARGET_COPY_OUT_VENDOR)/lib/libaudioalsa.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/libaudioconfigstore.so:$(TARGET_COPY_OUT_VENDOR)/lib/libaudioconfigstore.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/libaudioparsers.so:$(TARGET_COPY_OUT_VENDOR)/lib/libaudioparsers.so\
    $(LOCAL_PATH)/prebuilts/stock/lib/libwebrtc_audio_preprocessing.so:$(TARGET_COPY_OUT_VENDOR)/lib/libwebrtc_audio_preprocessing.so\
    $(LOCAL_PATH)/prebuilts/stock/lib64/libaudio_log_utils.so:$(TARGET_COPY_OUT_VENDOR)/lib64/libaudio_log_utils.so\
    $(LOCAL_PATH)/prebuilts/stock/lib64/libwebrtc_audio_preprocessing.so:$(TARGET_COPY_OUT_VENDOR)/lib64/libwebrtc_audio_preprocessing.so\
    $(LOCAL_PATH)/prebuilts/stock/lib64/soundfx/libasphere.so:$(TARGET_COPY_OUT_VENDOR)/lib64/soundfx/libasphere.so\
    $(LOCAL_PATH)/prebuilts/stock/lib64/soundfx/libshoebox.so:$(TARGET_COPY_OUT_VENDOR)/lib64/soundfx/libshoebox.so

# 指纹（GoodIX）：原厂这套是"QTI HAL + GoodIX 私有库 + qfp-daemon"的组合，
# 我们之前只提取了 APK 和固件，HAL/服务/库全漏了，所以设置里根本没有指纹入口。
# 文件来自原厂 vendor 分区（prebuilts/stock 下原样保存）。
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilts/stock/bin/hw/android.hardware.biometrics.fingerprint@2.1-service:$(TARGET_COPY_OUT_VENDOR)/bin/hw/android.hardware.biometrics.fingerprint@2.1-service \
    $(LOCAL_PATH)/prebuilts/stock/bin/qfp-daemon:$(TARGET_COPY_OUT_VENDOR)/bin/qfp-daemon \
    $(LOCAL_PATH)/prebuilts/stock/lib64/libgf_ca.so:$(TARGET_COPY_OUT_VENDOR)/lib64/libgf_ca.so \
    $(LOCAL_PATH)/prebuilts/stock/lib64/libgf_hal.so:$(TARGET_COPY_OUT_VENDOR)/lib64/libgf_hal.so \
    $(LOCAL_PATH)/prebuilts/stock/lib64/libgoodixhwfingerprint.so:$(TARGET_COPY_OUT_VENDOR)/lib64/libgoodixhwfingerprint.so \
    $(LOCAL_PATH)/prebuilts/stock/lib64/libqfp-service.so:$(TARGET_COPY_OUT_VENDOR)/lib64/libqfp-service.so \
    $(LOCAL_PATH)/prebuilts/stock/lib64/libvendor.goodix.hardware.biometrics.fingerprint@2.1.so:$(TARGET_COPY_OUT_VENDOR)/lib64/libvendor.goodix.hardware.biometrics.fingerprint@2.1.so \
    $(LOCAL_PATH)/prebuilts/stock/lib64/vendor.qti.hardware.fingerprint@1.0.so:$(TARGET_COPY_OUT_VENDOR)/lib64/vendor.qti.hardware.fingerprint@1.0.so \
    $(LOCAL_PATH)/prebuilts/stock/lib64/hw/fingerprint.default.so:$(TARGET_COPY_OUT_VENDOR)/lib64/hw/fingerprint.default.so \
    $(LOCAL_PATH)/prebuilts/stock/lib/vendor.qti.hardware.fingerprint@1.0.so:$(TARGET_COPY_OUT_VENDOR)/lib/vendor.qti.hardware.fingerprint@1.0.so \
    $(LOCAL_PATH)/prebuilts/stock/etc/init/android.hardware.biometrics.fingerprint@2.1-service.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/android.hardware.biometrics.fingerprint@2.1-service.rc \
    $(LOCAL_PATH)/prebuilts/stock/etc/permissions/qti_fingerprint_interface.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/qti_fingerprint_interface.xml \
    frameworks/native/data/etc/android.hardware.fingerprint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.fingerprint.xml

# 指纹 HAL 的 VINTF 声明写在 manifest.xml 里（见该文件 android.hardware.biometrics.fingerprint
# 条目），不能用 PRODUCT_COPY_FILES 安装 vintf/manifest 下的 xml —— build 会直接报错。

# GoodIX 的 vendor HIDL 库依赖旧 libhidlbase 的兼容符号。
PRODUCT_PACKAGES += libhidlbase_shim

# Bluetooth
PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.1.vendor \
    vendor.qti.hardware.bluetooth_audio@2.1.vendor \
    vendor.qti.hardware.btconfigstore@1.0.vendor \
    vendor.qti.hardware.btconfigstore@2.0.vendor

# Boot control
PRODUCT_PACKAGES += \
    android.hardware.boot@1.1-impl-qti \
    android.hardware.boot@1.1-impl-qti.recovery \
    android.hardware.boot@1.1-service

PRODUCT_PACKAGES_DEBUG += \
    bootctl

# Camera
PRODUCT_PACKAGES += \
    android.hardware.camera.common@1.0.vendor

# Display
PRODUCT_PACKAGES += \
    vendor.qti.hardware.display.composer-service

PRODUCT_PACKAGES += \
    android.hardware.graphics.mapper@3.0-impl-qti-display \
    android.hardware.graphics.mapper@4.0-impl-qti-display \
    vendor.qti.hardware.display.allocator-service

PRODUCT_PACKAGES += \
    libdisplayconfig.qti \
    libdisplayconfig.system.qti \
    libqdMetaData \
    libqdMetaData.system \
    libsdmcore \
    libsdmutils \
    libtinyxml \
    libvulkan \
    vendor.display.config@1.5 \
    vendor.display.config@1.11.vendor \
    vendor.display.config@2.0 \
    vendor.display.config@2.0.vendor \
    vendor.qti.hardware.display.mapper@1.1.vendor \
    vendor.qti.hardware.display.mapper@2.0.vendor \
    vendor.qti.hardware.display.mapper@3.0.vendor \
    vendor.qti.hardware.display.mapper@4.0.vendor

# DRM
PRODUCT_PACKAGES += \
    android.hardware.drm-service.clearkey

PRODUCT_PACKAGES += \
    android.hardware.drm@1.4.vendor

# fastbootd
PRODUCT_PACKAGES += \
    android.hardware.fastboot@1.1-impl-mock \
    fastbootd

# Gatekeeper
PRODUCT_PACKAGES += \
    android.hardware.gatekeeper@1.0.vendor

# GNSS
# android.hardware.gnss@*.vendor：android.hardware.gnss@2.1-service-qti、
# vendor.qti.gnss@*.so 与 libgarden.so 直接依赖这些 HIDL 接口库。
# Android 14 已无 VNDK 目录，vendor 命名空间（/linkerconfig/ld.config.txt）
# 不再包含 /system/${LIB}，所以必须把 .vendor 变体真正打进 vendor 镜像，
# 否则 GNSS 服务 5 秒崩溃循环、loc_launcher 也无法完成定位初始化。
# libcurl.vendor：location/QWES 侧 ssgtzd 与 xtwifi-inet-agent 依赖 libcurl.so
# （见 /vendor/etc/init/ssgtzd.rc、/vendor/etc/izat.conf 的 PROCESS_NAME）。
PRODUCT_PACKAGES += \
    android.hardware.gnss@1.0.vendor \
    android.hardware.gnss@1.1.vendor \
    android.hardware.gnss@2.0.vendor \
    android.hardware.gnss@2.1.vendor \
    libcurl.vendor

# GNSS 服务的 init rc。
# 原厂 vendor 镜像里没有 gnss 的 .rc（/vendor/etc/init 下无任何 gnss 服务定义，
# 全镜像 grep 确认）：@2.1 passthrough 实现的加载由服务二进制自己完成
# （dlopen /vendor/lib64/hw/android.hardware.gnss@2.1-impl-qti.so），
# 但**没有 rc 就不会有进程被 init 拉起**。
# VINTF 条目加在 manifest.xml（BoardConfig.mk 的 DEVICE_MANIFEST_FILE）里，
# 因为 build/make/core/Makefile:161 禁止用 PRODUCT_COPY_FILES 安装 VINTF 片段。
# rc 取自源码树 hardware/qcom/sm8150/gps/android/2.1/（与 sm7250 同名文件逐字节相同），
# 该目录仅在其自身 gps_vendor_product.mk 被 inherit 时才安装，本机产品配置未继承，
# 故在此显式 PRODUCT_COPY_FILES。
PRODUCT_COPY_FILES += \
    hardware/qcom/sm8150/gps/android/2.1/android.hardware.gnss@2.1-service-qti.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/android.hardware.gnss@2.1-service-qti.rc

# Health
# Android 17 / target-level 7 要求 AIDL health 与 health.storage。
PRODUCT_PACKAGES += \
    android.hardware.health-service.example \
    android.hardware.health-service.example_recovery \
    android.hardware.health.storage-service.default \
    vendor.lineage.health-service.default

# HIDL
PRODUCT_PACKAGES += \
    android.hidl.base@1.0 \
    android.hidl.base@1.0.vendor \
    android.hidl.manager@1.0 \
    android.hidl.manager@1.0.vendor \
    android.hidl.allocator@1.0 \
    android.hidl.allocator@1.0.vendor \
    libhidltransport \
    libhidltransport.vendor \
    libhwbinder \
    libhwbinder.vendor

# IPACM
PRODUCT_PACKAGES += \
    ipacm \
    IPACM_cfg.xml

# Keymaster
PRODUCT_PACKAGES += \
    android.hardware.keymaster@4.1.vendor

# Lights
# vendor 预编译 AIDL android.hardware.lights-service.qti 已由 vendor.mk 安装。
# 该二进制 NEEDED android.hardware.light-V1-ndk{,_platform}.so；LOS24 的
# AIDL light 未生成 NDK 后端，这两个库从 LOS21 vendor 以预编译 blob 补入
# （见 proprietary-files.txt）。

PRODUCT_PACKAGES += \
    libavservices_minijail \
    libavservices_minijail.vendor \
    libavservices_minijail_vendor \
    libc2dcolorconvert \
    libcodec2_hidl@1.0.vendor \
    libcodec2_hidl@1.1.vendor \
    libcodec2_vndk.vendor

# Media
# libjsoncpp.vendor：vendor.qti.media.c2@1.0-service 经 libqcodec2_platform.so
# 依赖 libjsoncpp.so，缺库时 Codec2 HAL 无法启动；ssgtzd 也依赖它。
PRODUCT_PACKAGES += \
    libavservices_minijail \
    libavservices_minijail.vendor \
    libavservices_minijail_vendor \
    libcodec2_hidl@1.0.vendor \
    libcodec2_vndk.vendor \
    libjsoncpp.vendor

PRODUCT_COPY_FILES += \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_c2.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_c2.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_c2_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_c2_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_c2_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_c2_video.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_video.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_video_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_video_le.xml

# Modules
# PRODUCT_COPY_FILES += \
#     $(call find-copy-subdir-files,*,$(LOCAL_PATH)/,$(TARGET_COPY_OUT_VENDOR)/lib/modules)

# Neural networks
PRODUCT_PACKAGES += \
    android.hardware.neuralnetworks@1.3.vendor

# OMX
PRODUCT_PACKAGES += \
    libOmxCore \
    libOmxVdec \
    libOmxVenc \
    libstagefrighthw

# Overlays
PRODUCT_ENFORCE_RRO_TARGETS := *

# Partitions
PRODUCT_USE_DYNAMIC_PARTITIONS := true

# Permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.audio.low_latency.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.low_latency.xml \
    frameworks/native/data/etc/android.hardware.audio.pro.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.pro.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.camera.flash-autofocus.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.camera.full.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.full.xml \
    frameworks/native/data/etc/android.hardware.camera.raw.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.raw.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.opengles.aep.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.opengles.aep.xml \
    frameworks/native/data/etc/android.hardware.se.omapi.uicc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.se.omapi.uicc.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepcounter.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.stepcounter.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepdetector.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.stepdetector.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.vulkan.compute-0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.compute-0.xml \
    frameworks/native/data/etc/android.hardware.vulkan.level-1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.level-1.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.version-1_1.xml \
    frameworks/native/data/etc/android.hardware.wifi.aware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.aware.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.hardware.wifi.passpoint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.passpoint.xml \
    frameworks/native/data/etc/android.hardware.wifi.rtt.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.rtt.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.software.device_id_attestation.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.device_id_attestation.xml \
    frameworks/native/data/etc/android.software.ipsec_tunnels.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.ipsec_tunnels.xml \
    frameworks/native/data/etc/android.software.midi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.midi.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.sip.voip.xml \
    frameworks/native/data/etc/android.software.verified_boot.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.verified_boot.xml

# Core hardware permissions
# 使用 AOSP 的 tablet 版核心特性列表：本设备是平板（PRODUCT_CHARACTERISTICS := tablet，
# lineage 侧继承 common_full_tablet_wifionly.mk）且只有 WiFi，handheld 版会声明
# android.software.telecom（本机没有电话栈，属于谎报能力），tablet 版没有这一条。
# 两版仅在 camera / touchscreen.multitouch[.distinct] 上有差异，而设备树已逐条 COPY 的
# android.hardware.camera.flash-autofocus.xml 与 ...touchscreen.multitouch.jazzhand.xml
# 已覆盖这些差异项，故 tablet 版不会丢特性。
# 目标文件名沿用 AOSP/原厂惯例 handheld_core_hardware.xml（goldfish 平板产品同样是
# tablet 源文件 + handheld_core_hardware.xml 目标名；原厂 /vendor/etc/permissions
# 下也是这个名字）。
# 缺此文件时 SystemServer 拿不到 android.software.app_widgets、android.software.home_screen
# 等约 20 个核心特性，会跳过 AppWidgetService，Trebuchet 通过
# getSystemService(AppWidgetManager.class) 取到 null 后每秒 NPE 崩溃。
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/tablet_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/handheld_core_hardware.xml

# Power
# Android 17 / target-level 7 要求 AIDL power 与 power.stats。
PRODUCT_PACKAGES += \
    android.hardware.power-service-qti \
    android.hardware.power.stats-service.example

# Product characteristics
PRODUCT_CHARACTERISTICS := tablet

# QMI
PRODUCT_PACKAGES += \
    libjson \
    libqti_vndfwk_detect \
    libqti_vndfwk_detect.vendor \
    libvndfwk_detect_jni.qti \
    libvndfwk_detect_jni.qti.vendor

# RIL
PRODUCT_PACKAGES += \
    libprotobuf-cpp-full-3.9.1-vendorcompat \
    libprotobuf-cpp-lite-3.9.1-vendorcompat \
    librmnetctl \
    libxml2

# Rootdir
PRODUCT_PACKAGES += \
    init.class_main.sh \
    init.crda.sh \
    init.mdm.sh \
    init.qcom.class_core.sh \
    init.qcom.coex.sh \
    init.qcom.early_boot.sh \
    init.qcom.efs.sync.sh \
    init.qcom.post_boot.sh \
    init.qcom.sdio.sh \
    init.qcom.sensors.sh \
    init.qcom.sh \
    init.qcom.usb.sh \
    init.qti.chg_policy.sh \
    init.qti.dcvs.sh \
    init.qti.display_boot.sh \
    init.qti.media.sh \
    init.qti.qcv.sh \
    qca6234-service.sh

PRODUCT_PACKAGES += \
    fstab.qcom \
    fstab.qcom_ramdisk \
    init.qcom.factory.rc \
    init.qcom.rc \
    init.qcom.usb.rc \
    init.qti.ufs.rc \
    init.target.rc \
    init.target.wigig.rc \
    init.recovery.qcom.rc \
    ueventd.qcom.rc

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/fstab.qcom:$(TARGET_COPY_OUT_RAMDISK)/first_stage_ramdisk/fstab.qcom

# Sensors
# android.hardware.sensors@1.0.vendor / android.frameworks.sensorservice@1.0.vendor：
# libFactoryHALCommon.so 与 slim_daemon 依赖这两个 HIDL 接口库。
# android.hardware.sensors@2.0/@2.1.vendor 与 @2.0-ScopedWakelock.vendor、libpower.vendor：
# android.hardware.sensors@2.0-service.multihal（高通 SSC 传感器 HAL，本次随 SSC 栈一并打包）
# 的 DT_NEEDED 直接列出这四个库。它们只存在于 /system（A14 无 VNDK 目录，
# vendor 命名空间看不到），因此必须用 .vendor 变体打进 vendor 镜像，
# 否则 multihal 会 CANNOT LINK 立即退出，sensorservice 收不到任何传感器。
PRODUCT_PACKAGES += \
    android.frameworks.sensorservice@1.0.vendor \
    android.hardware.sensors@1.0.vendor \
    android.hardware.sensors@2.0.vendor \
    android.hardware.sensors@2.1.vendor \
    android.hardware.sensors@2.0-ScopedWakelock.vendor \
    libpower.vendor

# Sensors (Qualcomm SSC / SLPI) 的 init rc。
# 整个 SSC 栈此前未打包（见 proprietary-files.txt 末段），rc 自然也缺失，
# 因此没有任何进程被 init 拉起 —— 这是 sensors 完全不可用、
# 并连带导致 camera provider 周期性重启的直接原因之一。
# 以下 5 个 rc 与 /mnt/vstock/etc/init/ 下的原厂文件逐字节相同
# （已 diff 验证），已放入本设备树 rootdir/etc/，
# 故按 GNSS rc 的同一方式用 PRODUCT_COPY_FILES 安装。
# host_init_verifier 预检通过（5/5 exit=0）。
#   vendor.sensors.sscrpcd.rc : service vendor.sensors /vendor/bin/sscrpcd sensorspd
#                               class early_hal, user system, group system wakelock
#   vendor.sensors.qti.rc     : service vendor.sensors.qti + vendor-sensor-sh
#   init.vendor.sensors.rc    : on early-boot 写 /sys/kernel/boot_slpi/boot 1，
#                               并 chown /mnt/vendor/persist/sensors/*
#   ...-service-multihal.rc   : service vendor.sensors-hal-2-0-multihal, class hal
#   ...sensorscalibrate...rc  : service sensorscal-hal-1-0, class hal
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/android.hardware.sensors@2.0-service-multihal.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/android.hardware.sensors@2.0-service-multihal.rc \
    $(LOCAL_PATH)/rootdir/etc/init.vendor.sensors.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.vendor.sensors.rc \
    $(LOCAL_PATH)/rootdir/etc/vendor.qti.hardware.sensorscalibrate@1.0-service.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/vendor.qti.hardware.sensorscalibrate@1.0-service.rc \
    $(LOCAL_PATH)/rootdir/etc/vendor.sensors.qti.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/vendor.sensors.qti.rc \
    $(LOCAL_PATH)/rootdir/etc/vendor.sensors.sscrpcd.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/vendor.sensors.sscrpcd.rc

# Service Tracker
PRODUCT_PACKAGES += \
    vendor.qti.hardware.servicetracker@1.2.vendor

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH)

# Thermal
# vendor 预编译 android.hardware.thermal@2.0-service.qti 已由 vendor.mk 安装。

# USB
PRODUCT_PACKAGES += \
    android.hardware.usb-service.qti

# Vendor service manager
PRODUCT_PACKAGES += \
    vndservicemanager

# Vibrator
$(call inherit-product, vendor/qcom/opensource/vibrator/vibrator-vendor-product.mk)

# VNDK
PRODUCT_COPY_FILES += \
    prebuilts/vndk/v32/arm64/arch-arm64-armv8-a/shared/vndk-sp/libhidlbase.so:$(TARGET_COPY_OUT_SYSTEM)/lib64/libhidlbase-v32.so \
    prebuilts/vndk/v32/arm64/arch-arm64-armv8-a/shared/vndk-sp/libhidlbase.so:$(TARGET_COPY_OUT_VENDOR)/lib64/libhidlbase-v32.so

# Wifi
PRODUCT_PACKAGES += \
    android.hardware.wifi-service \
    hostapd \
    libwpa_client \
    libwifi-hal-qcom \
    wpa_supplicant \
    wpa_supplicant.conf

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/wifi/p2p_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/p2p_supplicant_overlay.conf \
    $(LOCAL_PATH)/wifi/WCNSS_qcom_cfg_qca6390.ini:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/qca6390/WCNSS_qcom_cfg.ini \
    $(LOCAL_PATH)/wifi/wpa_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant_overlay.conf

# 基础 WCNSS 配置（不带 chip 名的路径）。
# 本设备内核把 qcacld 驱动**编进内核**（defconfig: CONFIG_QCA_CLD_WLAN=y，
# CONFIG_QCA_CLD_WLAN_PROFILE="qca6390"），此时驱动用的是不带 chip 名的
# 基础路径 wlan/qca_cld/WCNSS_qcom_cfg.ini（见编译产物 Image 的字符串；
# 只有 .ko 形式的驱动才走 DYNAMIC_SINGLE_CHIP 的 <chip>/ 子目录路径）。
# 而 device/lenovo/p11_pro_plus/Android.mk 的 WIFI_FIRMWARE_SYMLINKS 规则
# 正是把这个基础路径作为符号链接目标，所以 /vendor/etc/wifi/WCNSS_qcom_cfg.ini
# 必须存在。原厂固件里该文件缺失（其 zui 走 .ko 驱动），我们也未提供，
# 会导致 WLAN 读不到配置。qca6390 与内核 profile 一致，故以其内容填充。
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/wifi/WCNSS_qcom_cfg_qca6390.ini:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/WCNSS_qcom_cfg.ini

# WiFi Display
# libpng.vendor：wifidisplayhalservice 经 libsi.so / libwfdmmsrc_proprietary.so
# 依赖 libpng.so，缺库时该服务 5 秒崩溃循环。
PRODUCT_PACKAGES += \
    libavservices_minijail \
    libnl \
    libpng.vendor \
    libwfdaac_vendor

PRODUCT_BOOT_JARS += \
    WfdCommon

# Inherit the proprietary files
$(call inherit-product, vendor/lenovo/p11_pro_plus/p11_pro_plus-vendor.mk)
