#
# Copyright (C) 2024 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/lineage/config/common_full_tablet_wifionly.mk)

# Inherit from p11_pro_plus device
$(call inherit-product, device/lenovo/p11_pro_plus/device.mk)

PRODUCT_NAME := lineage_p11_pro_plus
PRODUCT_DEVICE := p11_pro_plus
PRODUCT_BRAND := Lenovo
PRODUCT_MODEL := Lenovo TB-J716F
PRODUCT_MANUFACTURER := LENOVO

PRODUCT_GMS_CLIENTID_BASE := android-lenovo

# A17 的 FCM level 7 要求内核 >= 4.19.325 且启用 CFI/SCS/KFENCE 等特性；
# 本机内核为 CAF 4.19.312，暂时无法满足。关闭 OTA 内核要求检查，
# 待内核合并到 4.19.325 后再恢复。
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false
# 4.19 没有 MREMAP_DONTUNMAP，userfaultfd GC 不可用。
PRODUCT_ENABLE_UFFD_GC := false

BUILD_FINGERPRINT := Lenovo/LenovoTB-J716F_PRC/J716F:13/TKQ1.221013.002/15.0.485_240205:user/release-keys
