// P2-C0-B: exercise the *running* keystore2 service chain.
//
// Path: IKeystoreService/default -> getSecurityLevel(TEE)
//       -> IKeystoreSecurityLevel.generateKey(AES-256 + TAG_STORAGE_KEY)
//       -> IKeystoreSecurityLevel.convertStorageKeyToEphemeral(...)
//
// This is deliberately separate from kmcompat_probe (direct compat library
// call) and km41_probe (direct HIDL HAL).  The full vold path additionally
// requires the SELinux keystore2_key permission convert_storage_key_to_ephemeral,
// which is granted to vold only; if shell is denied, the status is recorded and
// no attempt is made to change SELinux or flash a debug image.
//
// Never prints key material; only lengths, status codes and security level.

#include <android/binder_manager.h>
#include <aidl/android/hardware/security/keymint/SecurityLevel.h>
#include <aidl/android/system/keystore2/Domain.h>
#include <aidl/android/system/keystore2/EphemeralStorageKeyResponse.h>
#include <aidl/android/system/keystore2/IKeystoreSecurityLevel.h>
#include <aidl/android/system/keystore2/IKeystoreService.h>
#include <aidl/android/system/keystore2/KeyDescriptor.h>
#include <aidl/android/system/keystore2/KeyMetadata.h>
#include <keymint_support/keymint_tags.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace ks2 = ::aidl::android::system::keystore2;
namespace keymint = ::aidl::android::hardware::security::keymint;

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

static const char* sec_name(keymint::SecurityLevel level) {
    switch (level) {
        case keymint::SecurityLevel::SOFTWARE:
            return "SOFTWARE";
        case keymint::SecurityLevel::TRUSTED_ENVIRONMENT:
            return "TRUSTED_ENVIRONMENT";
        case keymint::SecurityLevel::STRONGBOX:
            return "STRONGBOX";
        case keymint::SecurityLevel::KEYSTORE:
            return "KEYSTORE";
        default:
            return "UNKNOWN";
    }
}

static void print_status(const char* what, const ndk::ScopedAStatus& status) {
    if (status.isOk()) {
        STEP("%s -> OK", what);
        return;
    }
    STEP("%s -> status=%d exception=%d service_specific=%d desc='%s'", what,
         status.getStatus(), static_cast<int>(status.getExceptionCode()),
         status.getServiceSpecificError(), status.getDescription().c_str());
}

static const char* kServiceName = "android.system.keystore2.IKeystoreService/default";

static int do_storagekey(bool with_fbe_ice, bool use_blob) {
    ndk::SpAIBinder binder(AServiceManager_checkService(kServiceName));
    if (binder.get() == nullptr) {
        STEP("keystore2 service not found: %s", kServiceName);
        return 2;
    }
    auto keystore = ks2::IKeystoreService::fromBinder(binder);
    if (!keystore) {
        STEP("IKeystoreService::fromBinder failed");
        return 2;
    }
    STEP("connected to %s", kServiceName);

    std::shared_ptr<ks2::IKeystoreSecurityLevel> sec;
    auto status = keystore->getSecurityLevel(keymint::SecurityLevel::TRUSTED_ENVIRONMENT, &sec);
    print_status("getSecurityLevel(TEE)", status);
    if (!status.isOk() || !sec) return 3;

    ks2::KeyDescriptor desc;
    desc.domain = use_blob ? ks2::Domain::BLOB : ks2::Domain::APP;
    desc.nspace = use_blob ? static_cast<int64_t>(getuid()) : -1;
    desc.alias = use_blob ? std::nullopt
                          : std::optional<std::string>(std::string("p2c0b_storagekey"));
    desc.blob = std::nullopt;

    std::vector<keymint::KeyParameter> params = {
            keymint::makeKeyParameter(keymint::TAG_PURPOSE, keymint::KeyPurpose::ENCRYPT),
            keymint::makeKeyParameter(keymint::TAG_PURPOSE, keymint::KeyPurpose::DECRYPT),
            keymint::makeKeyParameter(keymint::TAG_ALGORITHM, keymint::Algorithm::AES),
            keymint::makeKeyParameter(keymint::TAG_KEY_SIZE, 256),
            keymint::makeKeyParameter(keymint::TAG_STORAGE_KEY),
    };
    if (with_fbe_ice) {
        keymint::KeyParameter p;
        p.tag = static_cast<keymint::Tag>((7 << 28) | 16201);
        p.value = keymint::KeyParameterValue::make<keymint::KeyParameterValue::boolValue>(true);
        params.push_back(p);
    }

    ks2::KeyMetadata meta;
    STEP("generateKey(%s, AES-256, STORAGE_KEY%s) ...", use_blob ? "BLOB" : "APP",
         with_fbe_ice ? " + KM_TAG_FBE_ICE" : "");
    status = sec->generateKey(desc, std::nullopt, params, 0, {}, &meta);
    print_status("generateKey", status);
    STEP("  key blob len=%zu authorizations=%zu",
         meta.key.blob.has_value() ? meta.key.blob->size() : 0, meta.authorizations.size());
    if (!status.isOk()) return 5;

    bool storageTag = false;
    for (const auto& a : meta.authorizations) {
        if (a.keyParameter.tag == keymint::Tag::STORAGE_KEY) storageTag = true;
        STEP("  generate authorization: securityLevel=%s tag=%d", sec_name(a.securityLevel),
             static_cast<int>(a.keyParameter.tag));
    }
    STEP("  generate TAG_STORAGE_KEY=%d", storageTag ? 1 : 0);

    // APP-domain keys are stored in the keystore database and generateKey()
    // does not return the blob.  getKeyEntry() may expose it.
    std::optional<std::vector<uint8_t>> keyBlob = meta.key.blob;
    if (!keyBlob.has_value() || keyBlob->empty()) {
        if (!use_blob) {
            ks2::KeyEntryResponse entry;
            STEP("getKeyEntry(APP) ...");
            status = keystore->getKeyEntry(desc, &entry);
            print_status("getKeyEntry", status);
            if (status.isOk() && entry.metadata.key.blob.has_value()) {
                keyBlob = entry.metadata.key.blob;
                storageTag = false;
                for (const auto& a : entry.metadata.authorizations) {
                    if (a.keyParameter.tag == keymint::Tag::STORAGE_KEY) storageTag = true;
                }
                STEP("  entry key blob len=%zu auths=%zu TAG_STORAGE_KEY=%d",
                     keyBlob->size(), entry.metadata.authorizations.size(), storageTag ? 1 : 0);
            }
        }
    }
    if (!keyBlob.has_value() || keyBlob->empty()) {
        STEP("no key blob available for conversion");
        if (!use_blob) {
            auto del = keystore->deleteKey(desc);
            print_status("deleteKey(APP cleanup)", del);
        }
        return 6;
    }

    ks2::KeyDescriptor blobDesc;
    blobDesc.domain = ks2::Domain::BLOB;
    blobDesc.nspace = static_cast<int64_t>(getuid());
    blobDesc.alias = std::nullopt;
    blobDesc.blob = keyBlob;

    ks2::EphemeralStorageKeyResponse resp;
    STEP("convertStorageKeyToEphemeral(blob, nspace=%lld) ...",
         static_cast<long long>(blobDesc.nspace));
    status = sec->convertStorageKeyToEphemeral(blobDesc, &resp);
    print_status("convertStorageKeyToEphemeral", status);
    STEP("  ephemeral len=%zu upgradedBlob=%d", resp.ephemeralKey.size(),
         resp.upgradedBlob.has_value() ? 1 : 0);
    size_t eph_len = resp.ephemeralKey.size();

    STEP("deleteKey(%s storage test key) ...", use_blob ? "BLOB" : "APP");
    if (use_blob) {
        status = sec->deleteKey(blobDesc);
    } else {
        status = keystore->deleteKey(desc);
    }
    print_status("deleteKey", status);

    if (keyBlob.has_value() && !keyBlob->empty()) {
        memset(keyBlob->data(), 0, keyBlob->size());
    }
    if (!resp.ephemeralKey.empty()) {
        memset(resp.ephemeralKey.data(), 0, resp.ephemeralKey.size());
    }
    if (resp.upgradedBlob.has_value() && !resp.upgradedBlob->empty()) {
        memset(resp.upgradedBlob->data(), 0, resp.upgradedBlob->size());
    }

    return eph_len == 0 ? 7 : 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    g_start_ms = now_ms();

    const char* mode = (argc > 1) ? argv[1] : "storagekey";
    STEP("keystore2_probe start mode=%s", mode);

    if (strcmp(mode, "storagekey") == 0) return do_storagekey(false, false);
    if (strcmp(mode, "storagekey-fbe") == 0) return do_storagekey(true, false);
    if (strcmp(mode, "storagekey-blob") == 0) return do_storagekey(false, true);
    if (strcmp(mode, "storagekey-blob-fbe") == 0) return do_storagekey(true, true);

    STEP("unknown mode '%s' (use storagekey|storagekey-fbe|storagekey-blob|storagekey-blob-fbe)",
         mode);
    return 1;
}
