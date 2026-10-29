/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Replacement entry point for vendor.dolby.media.c2@1.0-service.
 *
 * The stock binary is compiled against Android 13 headers and allocates
 * android::hardware::media::c2::V1_0::utils::ComponentStore inline with a
 * compile-time sizeof of 0x108 (264 bytes). This tree's class is 0x120
 * (288 bytes) and its RefBase virtual base lives at +0x110, so the tail of the
 * object lands outside the stock allocation and every sp<> refcount operation
 * reads an invalid mRefs. Building the entry point from this tree gives the
 * object its correct size.
 *
 * Everything else is intentionally identical to the stock binary: the same
 * V1_0 IComponentStore interface, the same "default1" instance name, the same
 * Dolby component store library, the same vndbinder setup and HIDL thread
 * pool, and no extra seccomp policy (the stock binary does not install one).
 */

#define LOG_TAG "vendor.dolby.media.c2@1.0-service"

#include <android-base/logging.h>
#include <binder/ProcessState.h>
#include <hidl/HidlTransportSupport.h>
#include <utils/StrongPointer.h>

#include <codec2/hidl/1.0/ComponentStore.h>
#include <C2Component.h>

#include <csignal>
#include <dlfcn.h>
#include <memory>

using android::sp;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::media::c2::V1_0::IComponentStore;
using android::hardware::media::c2::V1_0::utils::ComponentStore;

namespace {

// Dolby's component store is only shipped as a prebuilt library and has no
// Soong module, so bind the factory at runtime instead of adding a link-time
// dependency on a blob. The symbol is android::GetCodec2DolbyComponentStore().
constexpr char kStoreLibrary[] = "libcodec2_store_dolby.so";
constexpr char kStoreFactory[] = "_ZN7android28GetCodec2DolbyComponentStoreEv";

using GetStoreFn = std::shared_ptr<C2ComponentStore> (*)();

constexpr char kInstanceName[] = "default1";

// A blocking operation on one codec must not block the others; this matches
// AOSP's Codec2 HIDL service.
constexpr int kThreadCount = 8;

}  // namespace

int main() {
    LOG(DEBUG) << "vendor.dolby.media.c2@1.0-service starting...";

    // Same as the stock binary: ignore SIGPIPE and enable vndbinder so vendor
    // to vendor binder calls work.
    signal(SIGPIPE, SIG_IGN);
    android::ProcessState::initWithDriver("/dev/vndbinder");

    android::ProcessState::self()->startThreadPool();
    configureRpcThreadpool(kThreadCount, true /* callerWillJoin */);

    void* library = dlopen(kStoreLibrary, RTLD_NOW);
    if (library == nullptr) {
        LOG(ERROR) << "Cannot load " << kStoreLibrary << ": " << dlerror();
        return 1;
    }
    GetStoreFn getStore =
            reinterpret_cast<GetStoreFn>(dlsym(library, kStoreFactory));
    if (getStore == nullptr) {
        LOG(ERROR) << "Cannot find " << kStoreFactory << " in " << kStoreLibrary;
        return 1;
    }

    std::shared_ptr<C2ComponentStore> c2Store = getStore();
    if (c2Store == nullptr) {
        LOG(ERROR) << "Cannot create Dolby Codec2's component store.";
        return 1;
    }

    sp<IComponentStore> store = new ComponentStore(c2Store);
    if (store == nullptr) {
        LOG(ERROR) << "Cannot create Codec2's IComponentStore service.";
        return 1;
    }

    if (store->registerAsService(kInstanceName) != android::OK) {
        LOG(ERROR) << "Cannot register Dolby Codec2's IComponentStore service"
                      " with instance name \"" << kInstanceName << "\".";
        return 1;
    }
    LOG(INFO) << "Dolby Codec2's IComponentStore service created. "
                 "Instance name: \"" << kInstanceName << "\".";

    joinRpcThreadpool();
    return 0;
}
