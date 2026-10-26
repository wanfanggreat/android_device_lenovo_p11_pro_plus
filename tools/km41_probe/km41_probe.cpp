/*
 * km41_probe - standalone diagnostic probe for the QTI HIDL Keymaster 4.1 HAL.
 *
 * Purpose (P2-A-1): answer whether the vendor keymaster@4.1 HAL can be used
 * directly, without going through keystore2 and without modifying any system
 * partition:
 *   list     - can we talk to hwservicemanager and enumerate the HAL?
 *   connect  - does tryGetService()/getHardwareInfo() return, and what
 *              SecurityLevel does the implementation report?
 *   hmac     - do the keystore2 startup calls getHmacSharingParameters() and
 *              computeSharedHmac() return?
 *   crypto   - can the HAL generate an RSA key, sign, verify and delete it?
 *   all      - list, connect, hmac, crypto in that order.
 *
 * Every step is printed and flushed before the HIDL call is issued so that a
 * hang in the HAL is visible as "last step printed". The probe never calls
 * exit()/CHECK on HAL errors; run it under a host-side timeout.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <time.h>
#include <vector>

#include <android/hardware/gatekeeper/1.0/IGatekeeper.h>
#include <android/hardware/keymaster/4.0/IKeymasterDevice.h>
#include <android/hardware/keymaster/4.1/IKeymasterDevice.h>
#include <android/hidl/manager/1.0/IServiceManager.h>
#include <keymasterV4_1/Keymaster4.h>
#include <keymasterV4_1/authorization_set.h>

using android::hardware::hidl_string;
using android::hardware::hidl_vec;
using android::hardware::Return;
using android::sp;

namespace km40 = android::hardware::keymaster::V4_0;
namespace km41 = android::hardware::keymaster::V4_1;
namespace gk10 = android::hardware::gatekeeper::V1_0;

static int64_t g_start_ms;

static int64_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

#define STEP(...)                                            \
    do {                                                     \
        fprintf(stdout, "[%6lld ms] ",                       \
                static_cast<long long>(now_ms() - g_start_ms)); \
        fprintf(stdout, __VA_ARGS__);                        \
        fprintf(stdout, "\n");                               \
        fflush(stdout);                                      \
    } while (0)

static const char* sec_name(km40::SecurityLevel level) {
    switch (level) {
        case km40::SecurityLevel::SOFTWARE:
            return "SOFTWARE";
        case km40::SecurityLevel::TRUSTED_ENVIRONMENT:
            return "TRUSTED_ENVIRONMENT";
        case km40::SecurityLevel::STRONGBOX:
            return "STRONGBOX";
        default:
            return "UNKNOWN";
    }
}

static const char* err_name(km40::ErrorCode err) {
    struct Entry {
        int32_t code;
        const char* name;
    };
    static const Entry kEntries[] = {
        {0, "OK"},
        {-1, "ROOT_OF_TRUST_ALREADY_SET"},
        {-2, "UNSUPPORTED_PURPOSE"},
        {-3, "INCOMPATIBLE_PURPOSE"},
        {-4, "UNSUPPORTED_ALGORITHM"},
        {-5, "INCOMPATIBLE_ALGORITHM"},
        {-6, "UNSUPPORTED_KEY_SIZE"},
        {-7, "UNSUPPORTED_BLOCK_MODE"},
        {-8, "INCOMPATIBLE_BLOCK_MODE"},
        {-9, "UNSUPPORTED_MAC_LENGTH"},
        {-10, "UNSUPPORTED_PADDING_MODE"},
        {-11, "INCOMPATIBLE_PADDING_MODE"},
        {-12, "UNSUPPORTED_DIGEST"},
        {-13, "INCOMPATIBLE_DIGEST"},
        {-14, "INVALID_EXPIRATION_TIME"},
        {-15, "INVALID_USER_ID"},
        {-16, "INVALID_AUTHORIZATION_TIMEOUT"},
        {-17, "UNSUPPORTED_KEY_FORMAT"},
        {-18, "INCOMPATIBLE_KEY_FORMAT"},
        {-19, "UNSUPPORTED_KEY_ENCRYPTION_ALGORITHM"},
        {-20, "UNSUPPORTED_KEY_VERIFICATION_ALGORITHM"},
        {-21, "INVALID_INPUT_LENGTH"},
        {-22, "KEY_EXPORT_OPTIONS_INVALID"},
        {-23, "DELEGATION_NOT_ALLOWED"},
        {-24, "KEY_NOT_YET_VALID"},
        {-25, "KEY_EXPIRED"},
        {-26, "KEY_USER_NOT_AUTHENTICATED"},
        {-27, "OUTPUT_PARAMETER_NULL"},
        {-28, "INVALID_OPERATION_HANDLE"},
        {-29, "INSUFFICIENT_BUFFER_SPACE"},
        {-30, "VERIFICATION_FAILED"},
        {-31, "TOO_MANY_OPERATIONS"},
        {-32, "UNEXPECTED_NULL_POINTER"},
        {-33, "INVALID_KEY_BLOB"},
        {-34, "IMPORTED_KEY_NOT_ENCRYPTED"},
        {-35, "IMPORTED_KEY_DECRYPTION_FAILED"},
        {-36, "IMPORTED_KEY_NOT_SIGNED"},
        {-37, "IMPORTED_KEY_VERIFICATION_FAILED"},
        {-38, "INVALID_ARGUMENT"},
        {-39, "UNSUPPORTED_TAG"},
        {-40, "INVALID_TAG"},
        {-41, "MEMORY_ALLOCATION_FAILED"},
        {-44, "IMPORT_PARAMETER_MISMATCH"},
        {-45, "SECURE_HW_ACCESS_DENIED"},
        {-46, "OPERATION_CANCELLED"},
        {-47, "CONCURRENT_ACCESS_CONFLICT"},
        {-48, "SECURE_HW_BUSY"},
        {-49, "SECURE_HW_COMMUNICATION_FAILED"},
        {-50, "UNSUPPORTED_EC_FIELD"},
        {-51, "MISSING_NONCE"},
        {-52, "INVALID_NONCE"},
        {-53, "MISSING_MAC_LENGTH"},
        {-54, "KEY_RATE_LIMIT_EXCEEDED"},
        {-55, "CALLER_NONCE_PROHIBITED"},
        {-56, "KEY_MAX_OPS_EXCEEDED"},
        {-57, "INVALID_MAC_LENGTH"},
        {-58, "MISSING_MIN_MAC_LENGTH"},
        {-59, "UNSUPPORTED_MIN_MAC_LENGTH"},
        {-60, "UNSUPPORTED_KDF"},
        {-61, "UNSUPPORTED_EC_CURVE"},
        {-62, "KEY_REQUIRES_UPGRADE"},
        {-63, "ATTESTATION_CHALLENGE_MISSING"},
        {-64, "KEYMASTER_NOT_CONFIGURED"},
        {-65, "ATTESTATION_APPLICATION_ID_MISSING"},
        {-66, "CANNOT_ATTEST_IDS"},
        {-67, "ROLLBACK_RESISTANCE_UNAVAILABLE"},
        {-68, "HARDWARE_TYPE_UNAVAILABLE"},
        {-69, "PROOF_OF_PRESENCE_REQUIRED"},
        {-70, "CONCURRENT_PROOF_OF_PRESENCE_REQUESTED"},
        {-71, "NO_USER_CONFIRMATION"},
        {-72, "DEVICE_LOCKED"},
        {-100, "UNIMPLEMENTED"},
        {-101, "VERSION_MISMATCH"},
        {-1000, "UNKNOWN_ERROR"},
    };
    for (const auto& entry : kEntries) {
        if (entry.code == static_cast<int32_t>(err)) return entry.name;
    }
    return "VENDOR_ERROR";
}

static void print_err(const char* step, km40::ErrorCode err) {
    STEP("%s -> %d (%s)", step, static_cast<int32_t>(err), err_name(err));
}

static int do_list() {
    STEP("hwservicemanager: getService() ...");
    auto serviceManager = android::hidl::manager::V1_0::IServiceManager::getService();
    if (!serviceManager) {
        STEP("hwservicemanager: getService() returned NULL");
        return 2;
    }
    STEP("hwservicemanager: connected");

    const char* descriptors[] = {
        "android.hardware.keymaster@4.1::IKeymasterDevice",
        "android.hardware.keymaster@4.0::IKeymasterDevice",
    };
    for (const char* descriptor : descriptors) {
        // IServiceManager@1.0 has no listManifestByInterface(); the manifest
        // side is covered by `lshal`. listByInterface() shows what is actually
        // registered with hwservicemanager right now.
        bool called = false;
        STEP("listByInterface(%s) ...", descriptor);
        auto rc2 = serviceManager->listByInterface(
            descriptor, [&](const hidl_vec<hidl_string>& names) {
                called = true;
                STEP("  registered %zu instance(s)", names.size());
                for (const auto& name : names) STEP("    '%s'", name.c_str());
            });
        STEP("  returned, transport=%s callback=%d", rc2.description().c_str(),
             static_cast<int>(called));
    }
    return 0;
}

static bool probe_hardware_info(const char* label, const sp<km40::IKeymasterDevice>& device) {
    bool called = false;
    STEP("%s: getHardwareInfo() ...", label);
    auto rc = device->getHardwareInfo([&](km40::SecurityLevel level, const hidl_string& name,
                                         const hidl_string& author) {
        called = true;
        STEP("%s:   securityLevel=%s(%d) keymasterName='%s' author='%s'", label, sec_name(level),
             static_cast<int>(level), name.c_str(), author.c_str());
    });
    STEP("%s: getHardwareInfo returned, transport=%s callback=%d", label,
         rc.description().c_str(), static_cast<int>(called));
    return called;
}

static int do_connect() {
    auto device41 = km41::IKeymasterDevice::tryGetService("default");
    STEP("tryGetService(@4.1::IKeymasterDevice/default) -> %s",
         device41 ? "non-null" : "NULL");
    bool ok = false;
    if (device41) ok |= probe_hardware_info("4.1", device41);

    auto device40 = km40::IKeymasterDevice::tryGetService("default");
    STEP("tryGetService(@4.0::IKeymasterDevice/default) -> %s",
         device40 ? "non-null" : "NULL");
    if (device40) ok |= probe_hardware_info("4.0", device40);

    if (device40) {
        // This is exactly what km_compat does when it wraps the device.
        STEP("Keymaster4 wrapper halVersion() ...");
        sp<km41::support::Keymaster4> wrapped(
            new km41::support::Keymaster4(device40, "default"));
        const auto& version = wrapped->halVersion();
        STEP("  reported kmVersion=%u.%u seclevel=%s name='%s' author='%s'",
             version.majorVersion, version.minorVersion, sec_name(version.securityLevel),
             version.keymasterName.c_str(), version.authorName.c_str());
    }
    return ok ? 0 : 3;
}

static int do_hmac() {
    auto device = km40::IKeymasterDevice::tryGetService("default");
    if (!device) {
        STEP("tryGetService(4.0/default) -> NULL");
        return 2;
    }

    km40::HmacSharingParameters params;
    bool gotParams = false;
    STEP("getHmacSharingParameters() ...");
    auto rc = device->getHmacSharingParameters(
        [&](km40::ErrorCode err, const km40::HmacSharingParameters& p) {
            STEP("  -> err=%d (%s) seed_len=%zu nonce_len=%zu", static_cast<int32_t>(err),
                 err_name(err), p.seed.size(), p.nonce.size());
            if (err == km40::ErrorCode::OK) {
                params = p;
                gotParams = true;
            }
        });
    STEP("getHmacSharingParameters returned, transport=%s", rc.description().c_str());
    if (!gotParams) return 3;

    hidl_vec<km40::HmacSharingParameters> allParams(1);
    allParams[0] = params;
    bool gotCheck = false;
    STEP("computeSharedHmac(1 device) ...");
    auto rc2 = device->computeSharedHmac(
        allParams, [&](km40::ErrorCode err, const hidl_vec<uint8_t>& sharingCheck) {
            STEP("  -> err=%d (%s) sharingCheck_len=%zu", static_cast<int32_t>(err),
                 err_name(err), sharingCheck.size());
            if (err == km40::ErrorCode::OK) gotCheck = true;
        });
    STEP("computeSharedHmac returned, transport=%s", rc2.description().c_str());
    return gotCheck ? 0 : 4;
}

static int do_crypto(bool noAuthRequired) {
    auto device41 = km41::IKeymasterDevice::tryGetService("default");
    sp<km40::IKeymasterDevice> device = device41;
    if (!device) {
        device = km40::IKeymasterDevice::tryGetService("default");
    }
    if (!device) {
        STEP("no keymaster device available");
        return 2;
    }

    km41::AuthorizationSetBuilder keyBuilder;
    keyBuilder.RsaSigningKey(2048, 65537)
        .Digest(km41::Digest::SHA_2_256)
        .Padding(km41::PaddingMode::RSA_PKCS1_1_5_SIGN);
    if (noAuthRequired) {
        keyBuilder.Authorization(km40::TAG_NO_AUTH_REQUIRED);
    }
    auto keyParams = keyBuilder.hidl_data();

    hidl_vec<uint8_t> keyBlob;
    STEP("generateKey(RSA-2048, SIGN/VERIFY, SHA-256, PKCS#1%s) ...",
         noAuthRequired ? ", NO_AUTH_REQUIRED" : "");
    auto rc = device->generateKey(
        keyParams, [&](km40::ErrorCode err, const hidl_vec<uint8_t>& blob,
                       const km40::KeyCharacteristics& characteristics) {
            STEP("  -> err=%d (%s) blob_len=%zu", static_cast<int32_t>(err), err_name(err),
                 blob.size());
            if (err == km40::ErrorCode::OK) {
                keyBlob = blob;
                STEP("  key characteristics: softwareEnforced=%zu hardwareEnforced=%zu",
                     characteristics.softwareEnforced.size(),
                     characteristics.hardwareEnforced.size());
                for (const auto& param : characteristics.softwareEnforced) {
                    fprintf(stdout, "      sw tag=0x%08x\n", static_cast<uint32_t>(param.tag));
                }
                for (const auto& param : characteristics.hardwareEnforced) {
                    fprintf(stdout, "      hw tag=0x%08x\n", static_cast<uint32_t>(param.tag));
                }
                fflush(stdout);
                km41::AuthorizationSet sw(characteristics.softwareEnforced);
                km41::AuthorizationSet hw(characteristics.hardwareEnforced);
                STEP("  auth tags: sw[USER_SECURE_ID=%d AUTH_TIMEOUT=%d] "
                     "hw[USER_SECURE_ID=%d AUTH_TIMEOUT=%d UNLOCKED_DEVICE_REQUIRED=%d "
                     "ALLOW_WHILE_ON_BODY=%d]",
                     static_cast<int>(sw.Contains(km40::Tag::USER_SECURE_ID)),
                     static_cast<int>(sw.Contains(km40::Tag::AUTH_TIMEOUT)),
                     static_cast<int>(hw.Contains(km40::Tag::USER_SECURE_ID)),
                     static_cast<int>(hw.Contains(km40::Tag::AUTH_TIMEOUT)),
                     static_cast<int>(hw.Contains(km40::Tag::UNLOCKED_DEVICE_REQUIRED)),
                     static_cast<int>(hw.Contains(km40::Tag::ALLOW_WHILE_ON_BODY)));
            }
        });
    STEP("generateKey returned, transport=%s", rc.description().c_str());
    if (keyBlob.size() == 0) return 3;

    km41::AuthorizationSetBuilder opBuilder;
    opBuilder.Digest(km41::Digest::SHA_2_256)
        .Padding(km41::PaddingMode::RSA_PKCS1_1_5_SIGN);
    auto opParams = opBuilder.hidl_data();

    km40::HardwareAuthToken authToken{};
    km40::VerificationToken verificationToken{};
    const char kMessage[] = "km41_probe sign/verify round trip";
    hidl_vec<uint8_t> message;
    message.setToExternal(reinterpret_cast<uint8_t*>(const_cast<char*>(kMessage)),
                          strlen(kMessage));

    uint64_t signOp = 0;
    STEP("key blob before begin: len=%zu first_byte=0x%02x",
         keyBlob.size(), keyBlob.size() ? keyBlob[0] : 0);
    STEP("begin(SIGN) ...");
    auto rcBegin = device->begin(
        km40::KeyPurpose::SIGN, keyBlob, opParams, authToken,
        [&](km40::ErrorCode err, const hidl_vec<km40::KeyParameter>& /*outParams*/,
            uint64_t handle) {
            STEP("  -> err=%d (%s) handle=%llu", static_cast<int32_t>(err), err_name(err),
                 static_cast<unsigned long long>(handle));
            if (err == km40::ErrorCode::OK) signOp = handle;
        });
    STEP("begin(SIGN) returned, transport=%s", rcBegin.description().c_str());
    if (signOp == 0) {
        auto del = device->deleteKey(keyBlob);
        print_err("deleteKey(cleanup)", del);
        return 4;
    }

    hidl_vec<uint8_t> signature;
    STEP("finish(SIGN) ...");
    auto rcFinish = device->finish(
        signOp, opParams, message, hidl_vec<uint8_t>(), authToken, verificationToken,
        [&](km40::ErrorCode err, const hidl_vec<km40::KeyParameter>& /*outParams*/,
            const hidl_vec<uint8_t>& output) {
            STEP("  -> err=%d (%s) signature_len=%zu", static_cast<int32_t>(err), err_name(err),
                 output.size());
            if (err == km40::ErrorCode::OK) signature = output;
        });
    STEP("finish(SIGN) returned, transport=%s", rcFinish.description().c_str());
    if (signature.size() == 0) {
        auto del = device->deleteKey(keyBlob);
        print_err("deleteKey(cleanup)", del);
        return 5;
    }

    uint64_t verifyOp = 0;
    STEP("begin(VERIFY) ...");
    auto rcVerifyBegin = device->begin(
        km40::KeyPurpose::VERIFY, keyBlob, opParams, authToken,
        [&](km40::ErrorCode err, const hidl_vec<km40::KeyParameter>& /*outParams*/,
            uint64_t handle) {
            STEP("  -> err=%d (%s) handle=%llu", static_cast<int32_t>(err), err_name(err),
                 static_cast<unsigned long long>(handle));
            if (err == km40::ErrorCode::OK) verifyOp = handle;
        });
    STEP("begin(VERIFY) returned, transport=%s", rcVerifyBegin.description().c_str());
    if (verifyOp != 0) {
        STEP("finish(VERIFY, signature) ...");
        auto rcVerifyFinish = device->finish(
            verifyOp, opParams, message, signature, authToken, verificationToken,
            [&](km40::ErrorCode err, const hidl_vec<km40::KeyParameter>& /*outParams*/,
                const hidl_vec<uint8_t>& /*output*/) {
                STEP("  -> err=%d (%s)", static_cast<int32_t>(err), err_name(err));
            });
        STEP("finish(VERIFY) returned, transport=%s", rcVerifyFinish.description().c_str());
    }

    STEP("deleteKey(test key) ...");
    auto del = device->deleteKey(keyBlob);
    print_err("deleteKey", del);
    return signature.size() ? 0 : 5;
}


static sp<km40::IKeymasterDevice> get_device() {
    auto device41 = km41::IKeymasterDevice::tryGetService("default");
    sp<km40::IKeymasterDevice> device = device41;
    if (!device) device = km40::IKeymasterDevice::tryGetService("default");
    return device;
}

static int do_crypto_aes() {
    sp<km40::IKeymasterDevice> device = get_device();
    if (!device) {
        STEP("no keymaster device available");
        return 2;
    }

    km41::AuthorizationSetBuilder keyBuilder;
    keyBuilder.AesEncryptionKey(256)
        .BlockMode(km41::BlockMode::CBC)
        .Padding(km41::PaddingMode::PKCS7);
    auto keyParams = keyBuilder.hidl_data();

    hidl_vec<uint8_t> keyBlob;
    STEP("generateKey(AES-256-CBC-PKCS7) ...");
    auto rc = device->generateKey(
        keyParams, [&](km40::ErrorCode err, const hidl_vec<uint8_t>& blob,
                       const km40::KeyCharacteristics&) {
            STEP("  -> err=%d (%s) blob_len=%zu", static_cast<int32_t>(err), err_name(err),
                 blob.size());
            if (err == km40::ErrorCode::OK) keyBlob = blob;
        });
    STEP("generateKey returned, transport=%s", rc.description().c_str());
    if (keyBlob.size() == 0) return 3;

    hidl_vec<uint8_t> iv(16);
    km41::AuthorizationSetBuilder opBuilder;
    opBuilder.BlockMode(km41::BlockMode::CBC).Padding(km41::PaddingMode::PKCS7);
    opBuilder.Authorization(km40::TAG_NONCE, iv);
    auto opParams = opBuilder.hidl_data();

    km40::HardwareAuthToken authToken{};
    km40::VerificationToken verificationToken{};
    uint64_t op = 0;
    STEP("begin(ENCRYPT, AES-CBC) ...");
    auto rcBegin = device->begin(
        km40::KeyPurpose::ENCRYPT, keyBlob, opParams, authToken,
        [&](km40::ErrorCode err, const hidl_vec<km40::KeyParameter>&, uint64_t handle) {
            STEP("  -> err=%d (%s) handle=%llu", static_cast<int32_t>(err), err_name(err),
                 static_cast<unsigned long long>(handle));
            if (err == km40::ErrorCode::OK) op = handle;
        });
    STEP("begin returned, transport=%s", rcBegin.description().c_str());

    if (op != 0) {
        hidl_vec<uint8_t> plaintext(16);
        for (size_t i = 0; i < plaintext.size(); ++i) plaintext[i] = static_cast<uint8_t>(i);
        STEP("finish(ENCRYPT) ...");
        auto rcFinish = device->finish(
            op, opParams, plaintext, hidl_vec<uint8_t>(), authToken, verificationToken,
            [&](km40::ErrorCode err, const hidl_vec<km40::KeyParameter>&,
                const hidl_vec<uint8_t>& output) {
                STEP("  -> err=%d (%s) ciphertext_len=%zu", static_cast<int32_t>(err),
                     err_name(err), output.size());
            });
        STEP("finish returned, transport=%s", rcFinish.description().c_str());
    }

    auto del = device->deleteKey(keyBlob);
    print_err("deleteKey", del);
    return op != 0 ? 0 : 4;
}

static int do_crypto_ec() {
    sp<km40::IKeymasterDevice> device = get_device();
    if (!device) {
        STEP("no keymaster device available");
        return 2;
    }

    km41::AuthorizationSetBuilder keyBuilder;
    keyBuilder.EcdsaSigningKey(km41::EcCurve::P_256).Digest(km41::Digest::SHA_2_256);
    auto keyParams = keyBuilder.hidl_data();

    hidl_vec<uint8_t> keyBlob;
    STEP("generateKey(EC-P256, SIGN/VERIFY, SHA-256) ...");
    auto rc = device->generateKey(
        keyParams, [&](km40::ErrorCode err, const hidl_vec<uint8_t>& blob,
                       const km40::KeyCharacteristics&) {
            STEP("  -> err=%d (%s) blob_len=%zu", static_cast<int32_t>(err), err_name(err),
                 blob.size());
            if (err == km40::ErrorCode::OK) keyBlob = blob;
        });
    STEP("generateKey returned, transport=%s", rc.description().c_str());
    if (keyBlob.size() == 0) return 3;

    km41::AuthorizationSetBuilder opBuilder;
    opBuilder.Digest(km41::Digest::SHA_2_256);
    auto opParams = opBuilder.hidl_data();

    km40::HardwareAuthToken authToken{};
    km40::VerificationToken verificationToken{};
    const char kMessage[] = "km41_probe ec round trip";
    hidl_vec<uint8_t> message;
    message.setToExternal(reinterpret_cast<uint8_t*>(const_cast<char*>(kMessage)),
                          strlen(kMessage));

    uint64_t op = 0;
    STEP("begin(SIGN, EC) ...");
    auto rcBegin = device->begin(
        km40::KeyPurpose::SIGN, keyBlob, opParams, authToken,
        [&](km40::ErrorCode err, const hidl_vec<km40::KeyParameter>&, uint64_t handle) {
            STEP("  -> err=%d (%s) handle=%llu", static_cast<int32_t>(err), err_name(err),
                 static_cast<unsigned long long>(handle));
            if (err == km40::ErrorCode::OK) op = handle;
        });
    STEP("begin returned, transport=%s", rcBegin.description().c_str());

    if (op != 0) {
        STEP("finish(SIGN, EC) ...");
        auto rcFinish = device->finish(
            op, opParams, message, hidl_vec<uint8_t>(), authToken, verificationToken,
            [&](km40::ErrorCode err, const hidl_vec<km40::KeyParameter>&,
                const hidl_vec<uint8_t>& output) {
                STEP("  -> err=%d (%s) signature_len=%zu", static_cast<int32_t>(err),
                     err_name(err), output.size());
            });
        STEP("finish returned, transport=%s", rcFinish.description().c_str());
    }

    auto del = device->deleteKey(keyBlob);
    print_err("deleteKey", del);
    return op != 0 ? 0 : 4;
}


static int do_unlock() {
    auto device41 = km41::IKeymasterDevice::tryGetService("default");
    if (!device41) {
        STEP("tryGetService(4.1/default) -> NULL");
        return 2;
    }
    km40::VerificationToken verificationToken{};
    STEP("deviceLocked(passwordOnly=false, empty verificationToken) ...");
    auto rc = device41->deviceLocked(false, verificationToken);
    auto code = rc.withDefault(km41::ErrorCode::UNKNOWN_ERROR);
    STEP("deviceLocked -> %d (%s)", static_cast<int32_t>(code),
         err_name(static_cast<km40::ErrorCode>(static_cast<int32_t>(code))));
    STEP("earlyBootEnded() ...");
    auto rc2 = device41->earlyBootEnded();
    auto code2 = rc2.withDefault(km41::ErrorCode::UNKNOWN_ERROR);
    STEP("earlyBootEnded -> %d (%s)", static_cast<int32_t>(code2),
         err_name(static_cast<km40::ErrorCode>(static_cast<int32_t>(code2))));
    return 0;
}


// Replicates keystore2's level-zero (SuperKey) generation: HMAC-256 sign key,
// NO_AUTH_REQUIRED and EARLY_BOOT_ONLY (the 4.1 strategy used by keystore2).
static int do_bootkey() {
    sp<km40::IKeymasterDevice> device = get_device();
    if (!device) {
        STEP("no keymaster device available");
        return 2;
    }

    km41::AuthorizationSetBuilder keyBuilder;
    keyBuilder.Authorization(km40::TAG_ALGORITHM, km41::Algorithm::HMAC)
        .Authorization(km40::TAG_DIGEST, km41::Digest::SHA_2_256)
        .Authorization(km40::TAG_KEY_SIZE, 256u)
        .Authorization(km40::TAG_MIN_MAC_LENGTH, 256u)
        .Authorization(km40::TAG_PURPOSE, km41::KeyPurpose::SIGN)
        .Authorization(km40::TAG_NO_AUTH_REQUIRED)
        .Authorization(km41::TAG_EARLY_BOOT_ONLY);
    auto keyParams = keyBuilder.hidl_data();

    hidl_vec<uint8_t> keyBlob;
    STEP("generateKey(HMAC-256, NO_AUTH_REQUIRED, EARLY_BOOT_ONLY) ...");
    auto rc = device->generateKey(
        keyParams, [&](km40::ErrorCode err, const hidl_vec<uint8_t>& blob,
                       const km40::KeyCharacteristics&) {
            STEP("  -> err=%d (%s) blob_len=%zu", static_cast<int32_t>(err), err_name(err),
                 blob.size());
            if (err == km40::ErrorCode::OK) keyBlob = blob;
        });
    STEP("generateKey returned, transport=%s", rc.description().c_str());
    if (keyBlob.size() == 0) return 3;

    km41::AuthorizationSetBuilder opBuilder;
    opBuilder.Authorization(km40::TAG_MAC_LENGTH, 256u).Digest(km41::Digest::SHA_2_256);
    auto opParams = opBuilder.hidl_data();

    km40::HardwareAuthToken authToken{};
    km40::VerificationToken verificationToken{};
    const char kMessage[] = "boot level key probe";
    hidl_vec<uint8_t> message;
    message.setToExternal(reinterpret_cast<uint8_t*>(const_cast<char*>(kMessage)),
                          strlen(kMessage));

    uint64_t op = 0;
    STEP("begin(SIGN, HMAC) ...");
    auto rcBegin = device->begin(
        km40::KeyPurpose::SIGN, keyBlob, opParams, authToken,
        [&](km40::ErrorCode err, const hidl_vec<km40::KeyParameter>&, uint64_t handle) {
            STEP("  -> err=%d (%s) handle=%llu", static_cast<int32_t>(err), err_name(err),
                 static_cast<unsigned long long>(handle));
            if (err == km40::ErrorCode::OK) op = handle;
        });
    STEP("begin returned, transport=%s", rcBegin.description().c_str());

    if (op != 0) {
        STEP("finish(SIGN, HMAC) ...");
        auto rcFinish = device->finish(
            op, opParams, message, hidl_vec<uint8_t>(), authToken, verificationToken,
            [&](km40::ErrorCode err, const hidl_vec<km40::KeyParameter>&,
                const hidl_vec<uint8_t>& output) {
                STEP("  -> err=%d (%s) mac_len=%zu", static_cast<int32_t>(err), err_name(err),
                     output.size());
            });
        STEP("finish returned, transport=%s", rcFinish.description().c_str());
    }

    auto del = device->deleteKey(keyBlob);
    print_err("deleteKey", del);
    return op != 0 ? 0 : 4;
}


// Gatekeeper 1.0 verify() returns the legacy packed hw_auth_token_t in
// GatekeeperResponse.data. Layout matches hardware/hw_auth_token.h; challenge,
// user_id and authenticator_id are machine order, authenticator_type and
// timestamp are network order (see gatekeeperd.cpp).
struct __attribute__((packed)) HwAuthToken {
    uint8_t version;
    uint64_t challenge;
    uint64_t user_id;
    uint64_t authenticator_id;
    uint32_t authenticator_type;
    uint64_t timestamp;
    uint8_t hmac[32];
};

static uint32_t be32_to_host(uint32_t value) { return __builtin_bswap32(value); }
static uint64_t be64_to_host(uint64_t value) { return __builtin_bswap64(value); }

static int do_gatekeeper() {
    auto gatekeeper = gk10::IGatekeeper::tryGetService("default");
    STEP("tryGetService(gatekeeper@1.0/default) -> %s", gatekeeper ? "non-null" : "NULL");
    if (!gatekeeper) return 2;

    const uint32_t kTestUid = 99999;
    const char kPin[] = "km41probe-pin";
    hidl_vec<uint8_t> empty;
    hidl_vec<uint8_t> desired;
    desired.setToExternal(reinterpret_cast<uint8_t*>(const_cast<char*>(kPin)), strlen(kPin));

    STEP("deleteUser(%u) pre-clean ...", kTestUid);
    auto rcClean = gatekeeper->deleteUser(kTestUid, [&](const gk10::GatekeeperResponse& r) {
        STEP("  -> code=%d timeout=%u data_len=%zu", static_cast<int>(r.code), r.timeout,
             r.data.size());
    });
    STEP("deleteUser returned, transport=%s", rcClean.description().c_str());

    hidl_vec<uint8_t> passwordHandle;
    bool enrolled = false;
    STEP("enroll(uid=%u, pin='%s') ...", kTestUid, kPin);
    auto rcEnroll = gatekeeper->enroll(
        kTestUid, empty, empty, desired, [&](const gk10::GatekeeperResponse& r) {
            STEP("  -> code=%d timeout=%u data_len=%zu", static_cast<int>(r.code), r.timeout,
                 r.data.size());
            if (r.code == gk10::GatekeeperStatusCode::STATUS_OK && r.data.size() != 0) {
                passwordHandle = r.data;
                enrolled = true;
            }
        });
    STEP("enroll returned, transport=%s", rcEnroll.description().c_str());
    if (!enrolled) return 3;

    hidl_vec<uint8_t> tokenBlob;
    STEP("verify(uid=%u, challenge=0) ...", kTestUid);
    auto rcVerify = gatekeeper->verify(
        kTestUid, 0, passwordHandle, desired, [&](const gk10::GatekeeperResponse& r) {
            STEP("  -> code=%d timeout=%u data_len=%zu", static_cast<int>(r.code), r.timeout,
                 r.data.size());
            if (r.code == gk10::GatekeeperStatusCode::STATUS_OK) tokenBlob = r.data;
        });
    STEP("verify returned, transport=%s", rcVerify.description().c_str());

    int result = 0;
    if (tokenBlob.size() != sizeof(HwAuthToken)) {
        STEP("unexpected auth token size %zu (expected %zu)", tokenBlob.size(),
             sizeof(HwAuthToken));
        result = 4;
    } else {
        const HwAuthToken* raw = reinterpret_cast<const HwAuthToken*>(tokenBlob.data());
        km40::HardwareAuthToken authToken{};
        authToken.challenge = raw->challenge;
        authToken.userId = raw->user_id;
        authToken.authenticatorId = raw->authenticator_id;
        authToken.authenticatorType = static_cast<km40::HardwareAuthenticatorType>(
            be32_to_host(raw->authenticator_type));
        authToken.timestamp = be64_to_host(raw->timestamp);
        hidl_vec<uint8_t> mac;
        mac.setToExternal(const_cast<uint8_t*>(raw->hmac), sizeof(raw->hmac));
        authToken.mac = mac;
        STEP("authToken: version=%u challenge=%llu userId=%llu authenticatorId=%llu "
             "type=0x%x timestamp=%llu mac_len=%zu",
             raw->version, static_cast<unsigned long long>(authToken.challenge),
             static_cast<unsigned long long>(authToken.userId),
             static_cast<unsigned long long>(authToken.authenticatorId),
             static_cast<uint32_t>(authToken.authenticatorType),
             static_cast<unsigned long long>(authToken.timestamp), authToken.mac.size());

        sp<km40::IKeymasterDevice> device = get_device();
        if (!device) {
            STEP("no keymaster device available");
            result = 2;
        } else {
            km41::AuthorizationSetBuilder keyBuilder;
            keyBuilder.RsaSigningKey(2048, 65537)
                .Digest(km41::Digest::SHA_2_256)
                .Padding(km41::PaddingMode::RSA_PKCS1_1_5_SIGN)
                .Authorization(km40::TAG_USER_SECURE_ID,
                               static_cast<uint64_t>(authToken.userId))
                .Authorization(km40::TAG_USER_AUTH_TYPE,
                               km40::HardwareAuthenticatorType::PASSWORD)
                .Authorization(km40::TAG_AUTH_TIMEOUT, 30u);
            auto keyParams = keyBuilder.hidl_data();

            hidl_vec<uint8_t> keyBlob;
            STEP("generateKey(RSA, USER_SECURE_ID=%llu, AUTH_TIMEOUT=30) ...",
                 static_cast<unsigned long long>(authToken.userId));
            auto rcGen = device->generateKey(
                keyParams, [&](km40::ErrorCode err, const hidl_vec<uint8_t>& blob,
                               const km40::KeyCharacteristics&) {
                    STEP("  -> err=%d (%s) blob_len=%zu", static_cast<int32_t>(err),
                         err_name(err), blob.size());
                    if (err == km40::ErrorCode::OK) keyBlob = blob;
                });
            STEP("generateKey returned, transport=%s", rcGen.description().c_str());

            if (keyBlob.size() == 0) {
                result = 5;
            } else {
                km41::AuthorizationSetBuilder opBuilder;
                opBuilder.Digest(km41::Digest::SHA_2_256)
                    .Padding(km41::PaddingMode::RSA_PKCS1_1_5_SIGN);
                auto opParams = opBuilder.hidl_data();
                km40::VerificationToken verificationToken{};
                const char kMessage[] = "gatekeeper auth-bound sign";
                hidl_vec<uint8_t> message;
                message.setToExternal(reinterpret_cast<uint8_t*>(const_cast<char*>(kMessage)),
                                      strlen(kMessage));

                uint64_t op = 0;
                STEP("begin(SIGN, authToken) ...");
                auto rcBegin = device->begin(
                    km40::KeyPurpose::SIGN, keyBlob, opParams, authToken,
                    [&](km40::ErrorCode err, const hidl_vec<km40::KeyParameter>&,
                        uint64_t handle) {
                        STEP("  -> err=%d (%s) handle=%llu", static_cast<int32_t>(err),
                             err_name(err), static_cast<unsigned long long>(handle));
                        if (err == km40::ErrorCode::OK) op = handle;
                    });
                STEP("begin returned, transport=%s", rcBegin.description().c_str());

                if (op != 0) {
                    STEP("finish(SIGN, authToken) ...");
                    auto rcFinish = device->finish(
                        op, opParams, message, hidl_vec<uint8_t>(), authToken,
                        verificationToken,
                        [&](km40::ErrorCode err, const hidl_vec<km40::KeyParameter>&,
                            const hidl_vec<uint8_t>& output) {
                            STEP("  -> err=%d (%s) signature_len=%zu",
                                 static_cast<int32_t>(err), err_name(err), output.size());
                        });
                    STEP("finish returned, transport=%s", rcFinish.description().c_str());
                }

                auto del = device->deleteKey(keyBlob);
                print_err("deleteKey", del);
                if (op == 0) result = 6;
            }
        }
    }

    STEP("deleteUser(%u) cleanup ...", kTestUid);
    auto rcDelUser = gatekeeper->deleteUser(
        kTestUid, [&](const gk10::GatekeeperResponse& r) {
            STEP("  -> code=%d timeout=%u data_len=%zu", static_cast<int>(r.code), r.timeout,
                 r.data.size());
        });
    STEP("deleteUser returned, transport=%s", rcDelUser.description().c_str());
    return result;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    g_start_ms = now_ms();

    const char* mode = (argc > 1) ? argv[1] : "all";
    STEP("km41_probe start mode=%s", mode);

    if (strcmp(mode, "list") == 0) return do_list();
    if (strcmp(mode, "connect") == 0) return do_connect();
    if (strcmp(mode, "hmac") == 0) return do_hmac();
    if (strcmp(mode, "crypto") == 0) {
        int rc = do_crypto(false);
        if (rc == 4) {
            STEP("begin() failed without auth; retrying a key with TAG_NO_AUTH_REQUIRED");
            rc = do_crypto(true);
        }
        return rc;
    }
    if (strcmp(mode, "crypto-noauth") == 0) return do_crypto(true);
    if (strcmp(mode, "unlock") == 0) return do_unlock();
    if (strcmp(mode, "bootkey") == 0) return do_bootkey();
    if (strcmp(mode, "gatekeeper") == 0) return do_gatekeeper();
    if (strcmp(mode, "aes") == 0) return do_crypto_aes();
    if (strcmp(mode, "ec") == 0) return do_crypto_ec();
    if (strcmp(mode, "all") == 0) {
        int rc = do_list();
        if (rc != 0) return rc;
        rc = do_connect();
        if (rc != 0) return rc;
        rc = do_hmac();
        if (rc != 0) return rc;
        rc = do_crypto(false);
        if (rc == 4) rc = do_crypto(true);
        return rc;
    }

    STEP("unknown mode '%s' (use list|connect|hmac|unlock|crypto|crypto-noauth|bootkey|gatekeeper|aes|ec|all)", mode);
    return 1;
}
