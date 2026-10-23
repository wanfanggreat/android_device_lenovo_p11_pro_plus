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

BUILD_FINGERPRINT := Lenovo/LenovoTB-J716F_PRC/J716F:13/TKQ1.221013.002/15.0.485_240205:user/release-keys
