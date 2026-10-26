/*
 * kmcompat_probe - exercise the real AOSP keystore2 km_compat conversion path,
 * in an isolated process, without touching system partitions and without
 * registering or overriding the "android.security.compat" binder service.
 *
 * The entry point is KeyMintDevice::createKeyMintDevice(TRUSTED_ENVIRONMENT),
 * the same one KeystoreCompatService::getKeyMintDevice() calls. It performs the
 * real enumeration (android.hidl.manager@1.2 IServiceManager,
 * listManifestByInterface(), Keymaster4 wrappers, halVersion()) and returns an
 * AIDL IKeyMintDevice that converts AIDL KeyMint calls into HIDL Keymaster
 * calls. This is the layer between keystore2 and the QTI HAL that P2-A-2 needs.
 *
 * Modes:
 *   info       - create the compat device and call getHardwareInfo()
 *   basic      - RSA key generation, sign, verify, delete through the wrapper
 *   hmac       - ISharedSecret getSharedSecretParameters/computeSharedSecret
 *   bootkey    - HMAC + NO_AUTH_REQUIRED + EARLY_BOOT_ONLY (keystore2 level-0 key)
 *   gatekeeper - Gatekeeper auth token + auth-bound RSA key through the wrapper
 *
 * Every step prints and flushes before the call, so a hang shows the last line.
 * Run under a host-side timeout.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include <android/hardware/gatekeeper/1.0/IGatekeeper.h>
#include <hidl/ServiceManagement.h>
#include <android/hidl/manager/1.2/IServiceManager.h>
#include <keymint_support/keymint_tags.h>

using android::hardware::hidl_string;
using android::hardware::hidl_vec;

#include "km_compat.h"

using ::aidl::android::hardware::security::keymint::Algorithm;
using ::aidl::android::hardware::security::keymint::Digest;
using ::aidl::android::hardware::security::keymint::KeyCreationResult;
using ::aidl::android::hardware::security::keymint::KeyPurpose;
using ::aidl::android::hardware::security::keymint::PaddingMode;
using ::aidl::android::hardware::security::keymint::SecurityLevel;
using ::aidl::android::hardware::security::sharedsecret::SharedSecretParameters;
using ::ndk::ScopedAStatus;

namespace KMV1 = ::aidl::android::hardware::security::keymint;
namespace gk10 = ::android::hardware::gatekeeper::V1_0;

static int64_t g_start_ms;

static int64_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

#define STEP(...)                                              \
    do {                                                       \
        fprintf(stdout, "[%6lld ms] ",                         \
                static_cast<long long>(now_ms() - g_start_ms)); \
        fprintf(stdout, __VA_ARGS__);                          \
        fprintf(stdout, "\n");                                 \
        fflush(stdout);                                        \
    } while (0)

static const char* sec_name(SecurityLevel level) {
    switch (level) {
        case SecurityLevel::SOFTWARE:
            return "SOFTWARE";
        case SecurityLevel::TRUSTED_ENVIRONMENT:
            return "TRUSTED_ENVIRONMENT";
        case SecurityLevel::STRONGBOX:
            return "STRONGBOX";
        case SecurityLevel::KEYSTORE:
            return "KEYSTORE";
        default:
            return "UNKNOWN";
    }
}

static void print_status(const char* what, const ScopedAStatus& status) {
    if (status.isOk()) {
        STEP("%s -> OK", what);
        return;
    }
    STEP("%s -> status=%d exception=%d service_specific=%d desc='%s'", what,
         status.getStatus(), static_cast<int>(status.getExceptionCode()),
         status.getServiceSpecificError(), status.getDescription().c_str());
}

static std::string status_text(const ScopedAStatus& status) {
    if (status.isOk()) return "OK";
    return "status=" + std::to_string(status.getStatus()) + " exception=" +
           std::to_string(static_cast<int>(status.getExceptionCode())) +
           " service_specific=" + std::to_string(status.getServiceSpecificError()) +
           " desc='" + status.getDescription() + "'";
}

static std::shared_ptr<KMV1::IKeyMintDevice> create_tee_device() {
    STEP("KeyMintDevice::createKeyMintDevice(TRUSTED_ENVIRONMENT) ...");
    auto device = KeyMintDevice::createKeyMintDevice(SecurityLevel::TRUSTED_ENVIRONMENT);
    STEP("createKeyMintDevice -> %s", device ? "non-null" : "NULL");
    return device;
}

static bool get_info(const std::shared_ptr<KMV1::IKeyMintDevice>& device,
                     KMV1::KeyMintHardwareInfo* info) {
    STEP("IKeyMintDevice::getHardwareInfo() ...");
    auto status = device->getHardwareInfo(info);
    if (!status.isOk()) {
        print_status("getHardwareInfo", status);
        return false;
    }
    STEP("getHardwareInfo -> securityLevel=%s version=%d name='%s' author='%s' "
         "timestampTokenRequired=%d",
         sec_name(info->securityLevel), info->versionNumber, info->keyMintName.c_str(),
         info->keyMintAuthorName.c_str(), static_cast<int>(info->timestampTokenRequired));
    return true;
}

static int do_info() {
    auto device = create_tee_device();
    if (!device) return 2;
    KMV1::KeyMintHardwareInfo info;
    return get_info(device, &info) ? 0 : 3;
}

static int do_basic() {
    auto device = create_tee_device();
    if (!device) return 2;
    KMV1::KeyMintHardwareInfo info;
    if (!get_info(device, &info)) return 3;
    if (info.securityLevel != SecurityLevel::TRUSTED_ENVIRONMENT) {
        STEP("refusing to continue: wrapper reports %s, not TEE", sec_name(info.securityLevel));
        return 4;
    }

    std::vector<KMV1::KeyParameter> keyParams = {
        KMV1::makeKeyParameter(KMV1::TAG_PURPOSE, KeyPurpose::SIGN),
        KMV1::makeKeyParameter(KMV1::TAG_PURPOSE, KeyPurpose::VERIFY),
        KMV1::makeKeyParameter(KMV1::TAG_ALGORITHM, Algorithm::RSA),
        KMV1::makeKeyParameter(KMV1::TAG_KEY_SIZE, 2048),
        KMV1::makeKeyParameter(KMV1::TAG_RSA_PUBLIC_EXPONENT, static_cast<int64_t>(65537)),
        KMV1::makeKeyParameter(KMV1::TAG_DIGEST, Digest::SHA_2_256),
        KMV1::makeKeyParameter(KMV1::TAG_PADDING, PaddingMode::RSA_PKCS1_1_5_SIGN),
        KMV1::makeKeyParameter(KMV1::TAG_NO_AUTH_REQUIRED),
        KMV1::makeKeyParameter(KMV1::TAG_CERTIFICATE_NOT_BEFORE, static_cast<int64_t>(0)),
        KMV1::makeKeyParameter(KMV1::TAG_CERTIFICATE_NOT_AFTER,
                               static_cast<int64_t>(253402300799000)),
    };

    KMV1::KeyCreationResult creation;
    STEP("generateKey(RSA-2048, NO_AUTH_REQUIRED) ...");
    auto status = device->generateKey(keyParams, std::nullopt, &creation);
    print_status("generateKey", status);
    if (!status.isOk()) return 5;
    STEP("  keyBlob=%zu bytes characteristics=%zu chains=%zu", creation.keyBlob.size(),
         creation.keyCharacteristics.size(), creation.certificateChain.size());
    for (const auto& characteristic : creation.keyCharacteristics) {
        STEP("  characteristics: securityLevel=%s authorizations=%zu",
             sec_name(characteristic.securityLevel), characteristic.authorizations.size());
    }
    if (creation.keyBlob.empty()) return 6;

    std::vector<KMV1::KeyParameter> opParams = {
        KMV1::makeKeyParameter(KMV1::TAG_DIGEST, Digest::SHA_2_256),
        KMV1::makeKeyParameter(KMV1::TAG_PADDING, PaddingMode::RSA_PKCS1_1_5_SIGN),
    };
    const char kMessage[] = "kmcompat_probe sign/verify round trip";
    std::vector<uint8_t> message(kMessage, kMessage + strlen(kMessage));

    KMV1::BeginResult begin;
    STEP("begin(SIGN) ...");
    status = device->begin(KeyPurpose::SIGN, creation.keyBlob, opParams, std::nullopt, &begin);
    print_status("begin(SIGN)", status);
    if (!status.isOk() || !begin.operation) {
        auto del = device->deleteKey(creation.keyBlob);
        print_status("deleteKey(cleanup)", del);
        return 7;
    }
    STEP("  challenge=%lld params=%zu", static_cast<long long>(begin.challenge),
         begin.params.size());

    std::vector<uint8_t> signature;
    STEP("operation->finish(SIGN) ...");
    status = begin.operation->finish(message, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                               &signature);
    print_status("finish(SIGN)", status);
    if (!status.isOk() || signature.empty()) {
        auto del = device->deleteKey(creation.keyBlob);
        print_status("deleteKey(cleanup)", del);
        return 8;
    }
    STEP("  signature=%zu bytes", signature.size());

    KMV1::BeginResult verifyBegin;
    STEP("begin(VERIFY) ...");
    status = device->begin(KeyPurpose::VERIFY, creation.keyBlob, opParams, std::nullopt,
                           &verifyBegin);
    print_status("begin(VERIFY)", status);
    if (status.isOk() && verifyBegin.operation) {
        std::vector<uint8_t> verified;
        STEP("operation->finish(VERIFY, signature) ...");
        status = verifyBegin.operation->finish(message, signature, std::nullopt, std::nullopt,
                                               std::nullopt, &verified);
        print_status("finish(VERIFY)", status);
    }

    STEP("deleteKey(test key) ...");
    status = device->deleteKey(creation.keyBlob);
    print_status("deleteKey", status);
    return signature.empty() ? 9 : 0;
}

static int do_hmac() {
    STEP("SharedSecret::createSharedSecret(TRUSTED_ENVIRONMENT) ...");
    auto secret = SharedSecret::createSharedSecret(SecurityLevel::TRUSTED_ENVIRONMENT);
    STEP("createSharedSecret -> %s", secret ? "non-null" : "NULL");
    if (!secret) return 2;

    SharedSecretParameters params;
    STEP("getSharedSecretParameters() ...");
    auto status = secret->getSharedSecretParameters(&params);
    print_status("getSharedSecretParameters", status);
    if (!status.isOk()) return 3;
    STEP("  seed=%zu nonce=%zu", params.seed.size(), params.nonce.size());

    std::vector<SharedSecretParameters> all{params};
    std::vector<uint8_t> sharingCheck;
    STEP("computeSharedSecret(1 device) ...");
    status = secret->computeSharedSecret(all, &sharingCheck);
    print_status("computeSharedSecret", status);
    STEP("  sharingCheck=%zu bytes", sharingCheck.size());
    return status.isOk() ? 0 : 4;
}

static int do_bootkey() {
    auto device = create_tee_device();
    if (!device) return 2;

    std::vector<KMV1::KeyParameter> keyParams = {
        KMV1::makeKeyParameter(KMV1::TAG_ALGORITHM, Algorithm::HMAC),
        KMV1::makeKeyParameter(KMV1::TAG_DIGEST, Digest::SHA_2_256),
        KMV1::makeKeyParameter(KMV1::TAG_KEY_SIZE, 256),
        KMV1::makeKeyParameter(KMV1::TAG_MIN_MAC_LENGTH, 256),
        KMV1::makeKeyParameter(KMV1::TAG_PURPOSE, KeyPurpose::SIGN),
        KMV1::makeKeyParameter(KMV1::TAG_NO_AUTH_REQUIRED),
        KMV1::makeKeyParameter(KMV1::TAG_EARLY_BOOT_ONLY),
    };

    KMV1::KeyCreationResult creation;
    STEP("generateKey(HMAC, NO_AUTH_REQUIRED, EARLY_BOOT_ONLY) ...");
    auto status = device->generateKey(keyParams, std::nullopt, &creation);
    print_status("generateKey", status);
    if (!status.isOk() || creation.keyBlob.empty()) return 3;

    std::vector<KMV1::KeyParameter> opParams = {
        KMV1::makeKeyParameter(KMV1::TAG_MAC_LENGTH, 256),
        KMV1::makeKeyParameter(KMV1::TAG_DIGEST, Digest::SHA_2_256),
    };
    const char kMessage[] = "boot level key probe";
    std::vector<uint8_t> message(kMessage, kMessage + strlen(kMessage));

    KMV1::BeginResult begin;
    STEP("begin(SIGN, HMAC) ...");
    status = device->begin(KeyPurpose::SIGN, creation.keyBlob, opParams, std::nullopt, &begin);
    print_status("begin(SIGN)", status);
    if (status.isOk() && begin.operation) {
        std::vector<uint8_t> mac;
        STEP("operation->finish(SIGN, HMAC) ...");
        status = begin.operation->finish(message, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                                 &mac);
        print_status("finish(SIGN)", status);
        STEP("  mac=%zu bytes", mac.size());
    }

    STEP("deleteKey(test key) ...");
    status = device->deleteKey(creation.keyBlob);
    print_status("deleteKey", status);
    return 0;
}

// Legacy packed hw_auth_token_t returned by Gatekeeper 1.0 verify().
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
    const char kPin[] = "kmcompat-probe-pin";
    hidl_vec<uint8_t> empty;
    hidl_vec<uint8_t> desired;
    desired.setToExternal(reinterpret_cast<uint8_t*>(const_cast<char*>(kPin)), strlen(kPin));

    STEP("deleteUser(%u) pre-clean ...", kTestUid);
    auto rcClean = gatekeeper->deleteUser(kTestUid, [&](const gk10::GatekeeperResponse& r) {
        STEP("  -> code=%d data_len=%zu", static_cast<int>(r.code), r.data.size());
    });
    STEP("deleteUser returned, transport=%s", rcClean.description().c_str());

    hidl_vec<uint8_t> passwordHandle;
    bool enrolled = false;
    STEP("enroll(%u) ...", kTestUid);
    auto rcEnroll = gatekeeper->enroll(
        kTestUid, empty, empty, desired, [&](const gk10::GatekeeperResponse& r) {
            STEP("  -> code=%d data_len=%zu", static_cast<int>(r.code), r.data.size());
            if (r.code == gk10::GatekeeperStatusCode::STATUS_OK && r.data.size() != 0) {
                passwordHandle = r.data;
                enrolled = true;
            }
        });
    STEP("enroll returned, transport=%s", rcEnroll.description().c_str());
    if (!enrolled) return 3;

    hidl_vec<uint8_t> tokenBlob;
    STEP("verify(%u) ...", kTestUid);
    auto rcVerify = gatekeeper->verify(
        kTestUid, 0, passwordHandle, desired, [&](const gk10::GatekeeperResponse& r) {
            STEP("  -> code=%d data_len=%zu", static_cast<int>(r.code), r.data.size());
            if (r.code == gk10::GatekeeperStatusCode::STATUS_OK) tokenBlob = r.data;
        });
    STEP("verify returned, transport=%s", rcVerify.description().c_str());

    int result = 0;
    if (tokenBlob.size() != sizeof(HwAuthToken)) {
        STEP("unexpected auth token size %zu", tokenBlob.size());
        result = 4;
    } else {
        const HwAuthToken* raw = reinterpret_cast<const HwAuthToken*>(tokenBlob.data());
        KMV1::HardwareAuthToken authToken;
        authToken.challenge = raw->challenge;
        authToken.userId = raw->user_id;
        authToken.authenticatorId = raw->authenticator_id;
        authToken.authenticatorType =
            static_cast<KMV1::HardwareAuthenticatorType>(be32_to_host(raw->authenticator_type));
        authToken.timestamp.milliSeconds = be64_to_host(raw->timestamp);
        authToken.mac.assign(raw->hmac, raw->hmac + sizeof(raw->hmac));
        STEP("authToken: userId=%lld type=0x%x timestamp=%lld mac=%zu",
             static_cast<long long>(authToken.userId),
             static_cast<uint32_t>(authToken.authenticatorType),
             static_cast<long long>(authToken.timestamp.milliSeconds), authToken.mac.size());

        auto device = create_tee_device();
        if (!device) {
            result = 2;
        } else {
            std::vector<KMV1::KeyParameter> keyParams = {
                KMV1::makeKeyParameter(KMV1::TAG_PURPOSE, KeyPurpose::SIGN),
                KMV1::makeKeyParameter(KMV1::TAG_PURPOSE, KeyPurpose::VERIFY),
                KMV1::makeKeyParameter(KMV1::TAG_ALGORITHM, Algorithm::RSA),
                KMV1::makeKeyParameter(KMV1::TAG_KEY_SIZE, 2048),
                KMV1::makeKeyParameter(KMV1::TAG_RSA_PUBLIC_EXPONENT,
                                       static_cast<int64_t>(65537)),
                KMV1::makeKeyParameter(KMV1::TAG_DIGEST, Digest::SHA_2_256),
                KMV1::makeKeyParameter(KMV1::TAG_PADDING, PaddingMode::RSA_PKCS1_1_5_SIGN),
                KMV1::makeKeyParameter(KMV1::TAG_USER_SECURE_ID,
                                       static_cast<int64_t>(authToken.userId)),
                KMV1::makeKeyParameter(KMV1::TAG_USER_AUTH_TYPE,
                                       KMV1::HardwareAuthenticatorType::PASSWORD),
                KMV1::makeKeyParameter(KMV1::TAG_AUTH_TIMEOUT, 30),
        KMV1::makeKeyParameter(KMV1::TAG_CERTIFICATE_NOT_BEFORE, static_cast<int64_t>(0)),
        KMV1::makeKeyParameter(KMV1::TAG_CERTIFICATE_NOT_AFTER,
                               static_cast<int64_t>(253402300799000)),
            };

            KMV1::KeyCreationResult creation;
            STEP("generateKey(RSA, USER_SECURE_ID=%lld, AUTH_TIMEOUT=30) ...",
                 static_cast<long long>(authToken.userId));
            auto status = device->generateKey(keyParams, std::nullopt, &creation);
            print_status("generateKey", status);
            if (!status.isOk() || creation.keyBlob.empty()) {
                result = 5;
            } else {
                std::vector<KMV1::KeyParameter> opParams = {
                    KMV1::makeKeyParameter(KMV1::TAG_DIGEST, Digest::SHA_2_256),
                    KMV1::makeKeyParameter(KMV1::TAG_PADDING,
                                           PaddingMode::RSA_PKCS1_1_5_SIGN),
                };
                const char kMessage[] = "kmcompat auth-bound sign";
                std::vector<uint8_t> message(kMessage, kMessage + strlen(kMessage));

                KMV1::BeginResult begin;
                STEP("begin(SIGN, authToken) ...");
                status = device->begin(KeyPurpose::SIGN, creation.keyBlob, opParams,
                                       authToken, &begin);
                print_status("begin(SIGN, authToken)", status);
                if (status.isOk() && begin.operation) {
                    std::vector<uint8_t> signature;
                    STEP("operation->finish(SIGN, authToken) ...");
                    status = begin.operation->finish(message, std::nullopt, authToken,
                                                     std::nullopt, std::nullopt, &signature);
                    print_status("finish(SIGN, authToken)", status);
                    STEP("  signature=%zu bytes", signature.size());
                    if (signature.empty()) result = 6;
                } else {
                    result = 6;
                }

                STEP("deleteKey(test key) ...");
                status = device->deleteKey(creation.keyBlob);
                print_status("deleteKey", status);
            }
        }
    }

    STEP("deleteUser(%u) cleanup ...", kTestUid);
    auto rcDelUser = gatekeeper->deleteUser(kTestUid, [&](const gk10::GatekeeperResponse& r) {
        STEP("  -> code=%d data_len=%zu", static_cast<int>(r.code), r.data.size());
    });
    STEP("deleteUser returned, transport=%s", rcDelUser.description().c_str());
    return result;
}


// Diagnose the manager acquisition path that km_compat's initializeKeymasters()
// uses: IServiceManager::getService() (generic VINTF+ACL self-lookup) versus
// defaultServiceManager1_2() (direct context-manager binder).
static int do_manager() {
    STEP("V1_2::IServiceManager::getService() ...");
    auto generic = ::android::hidl::manager::V1_2::IServiceManager::getService();
    STEP("  -> %s", generic ? "non-null" : "NULL");
    STEP("V1_2::IServiceManager::tryGetService() ...");
    auto genericTry = ::android::hidl::manager::V1_2::IServiceManager::tryGetService();
    STEP("  -> %s", genericTry ? "non-null" : "NULL");
    STEP("defaultServiceManager1_2() ...");
    auto manager = ::android::hardware::defaultServiceManager1_2();
    STEP("  -> %s", manager ? "non-null" : "NULL");
    if (!manager) return 2;

    const char* descriptors[] = {
        "android.hidl.manager@1.2::IServiceManager",
        "android.hardware.keymaster@4.1::IKeymasterDevice",
        "android.hardware.keymaster@4.0::IKeymasterDevice",
    };
    for (const char* descriptor : descriptors) {
        STEP("getTransport(%s, default) ...", descriptor);
        auto transport = manager->getTransport(descriptor, "default");
        auto transportValue = transport.withDefault(
            ::android::hidl::manager::V1_0::IServiceManager::Transport::EMPTY);
        STEP("  -> %d (%s)", static_cast<int>(transportValue),
             transport.description().c_str());
    }

    STEP("get(android.hidl.manager@1.2::IServiceManager, default) ...");
    auto self = manager->get("android.hidl.manager@1.2::IServiceManager", "default");
    STEP("  -> %s", self.withDefault({}) ? "non-null" : "NULL");

    bool called = false;
    STEP("listManifestByInterface(android.hardware.keymaster@4.1::IKeymasterDevice) ...");
    auto status = manager->listManifestByInterface(
        "android.hardware.keymaster@4.1::IKeymasterDevice",
        [&](const hidl_vec<hidl_string>& names) {
            called = true;
            STEP("  -> %zu instance(s)", names.size());
            for (const auto& name : names) STEP("     '%s'", name.c_str());
        });
    STEP("listManifestByInterface returned, transport=%s callback=%d",
         status.description().c_str(), static_cast<int>(called));
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    g_start_ms = now_ms();

    const char* mode = (argc > 1) ? argv[1] : "basic";
    STEP("kmcompat_probe start mode=%s", mode);

    if (strcmp(mode, "manager") == 0) return do_manager();
    if (strcmp(mode, "info") == 0) return do_info();
    if (strcmp(mode, "basic") == 0) return do_basic();
    if (strcmp(mode, "hmac") == 0) return do_hmac();
    if (strcmp(mode, "bootkey") == 0) return do_bootkey();
    if (strcmp(mode, "gatekeeper") == 0) return do_gatekeeper();

    STEP("unknown mode '%s' (use manager|info|basic|hmac|bootkey|gatekeeper)", mode);
    return 1;
}
