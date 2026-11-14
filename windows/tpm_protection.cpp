#include "tpm_protection.h"
#include <windows.h>
#include <bcrypt.h>
#include <ncrypt.h>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace kb {
namespace {
constexpr std::array<std::uint8_t, 4> magic{'T','P','M','2'};
constexpr wchar_t machineKeyName[] = L"BluetoothUnlock.CredentialProtection.v1";
constexpr std::size_t aesKeyLength = 32;
constexpr std::size_t nonceLength = 12;
constexpr std::size_t tagLength = 16;
constexpr std::size_t maxPayload = 16384;

struct NcryptHandle {
    NCRYPT_HANDLE handle{};
    ~NcryptHandle() { if (handle) NCryptFreeObject(handle); }
    NcryptHandle() = default;
    NcryptHandle(const NcryptHandle&) = delete;
    NcryptHandle& operator=(const NcryptHandle&) = delete;
};
struct BcryptAlg {
    BCRYPT_ALG_HANDLE handle{};
    ~BcryptAlg() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
};
struct BcryptKey {
    BCRYPT_KEY_HANDLE handle{};
    ~BcryptKey() { if (handle) BCryptDestroyKey(handle); }
};

template<class T> void Clear(T& bytes) {
    if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size());
}

bool OpenMachineKey(NcryptHandle& provider, NcryptHandle& key, bool allowCreate) {
    if (NCryptOpenStorageProvider(reinterpret_cast<NCRYPT_PROV_HANDLE*>(&provider.handle),
                                  MS_PLATFORM_CRYPTO_PROVIDER, 0) != ERROR_SUCCESS) return false;
    auto status = NCryptOpenKey(static_cast<NCRYPT_PROV_HANDLE>(provider.handle),
        reinterpret_cast<NCRYPT_KEY_HANDLE*>(&key.handle), machineKeyName, 0, NCRYPT_MACHINE_KEY_FLAG);
    if (status == ERROR_SUCCESS) return true;
    if (!allowCreate || status != NTE_BAD_KEYSET) return false;

    status = NCryptCreatePersistedKey(static_cast<NCRYPT_PROV_HANDLE>(provider.handle),
        reinterpret_cast<NCRYPT_KEY_HANDLE*>(&key.handle), BCRYPT_RSA_ALGORITHM,
        machineKeyName, 0, NCRYPT_MACHINE_KEY_FLAG);
    if (status == NTE_EXISTS) {
        return NCryptOpenKey(static_cast<NCRYPT_PROV_HANDLE>(provider.handle),
            reinterpret_cast<NCRYPT_KEY_HANDLE*>(&key.handle), machineKeyName, 0,
            NCRYPT_MACHINE_KEY_FLAG) == ERROR_SUCCESS;
    }
    if (status != ERROR_SUCCESS) return false;
    DWORD bits = 2048;
    if (NCryptSetProperty(static_cast<NCRYPT_KEY_HANDLE>(key.handle),
            NCRYPT_LENGTH_PROPERTY, reinterpret_cast<PBYTE>(&bits), sizeof(bits), 0) != ERROR_SUCCESS)
        return false;

    // Machine-level keys may only be used by SYSTEM and local administrators.
    PSECURITY_DESCRIPTOR sd = nullptr;
    const wchar_t* dacl = L"O:BAG:BAD:PAI(A;;FA;;;SY)(A;;FA;;;BA)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(dacl, SDDL_REVISION_1, &sd, nullptr))
        return false;
    status = NCryptSetProperty(static_cast<NCRYPT_KEY_HANDLE>(key.handle),
        NCRYPT_SECURITY_DESCR_PROPERTY, reinterpret_cast<PBYTE>(sd),
        GetSecurityDescriptorLength(sd), DACL_SECURITY_INFORMATION);
    LocalFree(sd);
    if (status != ERROR_SUCCESS) return false;
    return NCryptFinalizeKey(static_cast<NCRYPT_KEY_HANDLE>(key.handle), 0) == ERROR_SUCCESS;
}

bool CryptGcm(bool encrypt, std::span<const std::uint8_t> input,
              std::array<std::uint8_t, aesKeyLength>& symmetric,
              std::array<std::uint8_t, nonceLength>& nonce,
              std::array<std::uint8_t, tagLength>& tag,
              std::vector<std::uint8_t>& output) {
    BcryptAlg algorithm;
    if (BCryptOpenAlgorithmProvider(&algorithm.handle, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0)
        return false;
    const auto mode = BCRYPT_CHAIN_MODE_GCM;
    if (BCryptSetProperty(algorithm.handle, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(mode)),
            static_cast<ULONG>((wcslen(mode) + 1) * sizeof(wchar_t)), 0) < 0)
        return false;
    DWORD objectLength = 0, received = 0;
    if (BCryptGetProperty(algorithm.handle, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &received, 0) < 0 ||
            objectLength == 0 || objectLength > 65536) return false;
    std::vector<std::uint8_t> keyObject(objectLength);
    bool success = false;
    {
    BcryptKey key;
    if (BCryptGenerateSymmetricKey(algorithm.handle, &key.handle, keyObject.data(),
            objectLength, symmetric.data(), static_cast<ULONG>(symmetric.size()), 0) >= 0) {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
        BCRYPT_INIT_AUTH_MODE_INFO(auth);
        auth.pbNonce = nonce.data();
        auth.cbNonce = static_cast<ULONG>(nonce.size());
        auth.pbTag = tag.data();
        auth.cbTag = static_cast<ULONG>(tag.size());
        output.resize(input.size());
        ULONG written = 0;
        auto* source = const_cast<PUCHAR>(input.data());
        NTSTATUS status = encrypt
            ? BCryptEncrypt(key.handle, source, static_cast<ULONG>(input.size()),
                &auth, nullptr, 0, output.data(), static_cast<ULONG>(output.size()), &written, 0)
            : BCryptDecrypt(key.handle, source, static_cast<ULONG>(input.size()),
                &auth, nullptr, 0, output.data(), static_cast<ULONG>(output.size()), &written, 0);
        success = status >= 0 && written == input.size();
        if (!success) { Clear(output); output.clear(); }
    }
    }
    Clear(keyObject);
    return success;
}

bool RsaCrypt(NCRYPT_KEY_HANDLE key, bool encrypt,
              std::span<const std::uint8_t> input, std::vector<std::uint8_t>& result) {
    BCRYPT_OAEP_PADDING_INFO pad{BCRYPT_SHA256_ALGORITHM, nullptr, 0};
    DWORD size = 0, written = 0;
    auto* source = const_cast<PBYTE>(input.data());
    auto call = [&](PBYTE dst, DWORD length, DWORD* outputLength) {
        return encrypt
            ? NCryptEncrypt(key, source, static_cast<DWORD>(input.size()), &pad,
                            dst, length, outputLength, NCRYPT_PAD_OAEP_FLAG)
            : NCryptDecrypt(key, source, static_cast<DWORD>(input.size()), &pad,
                            dst, length, outputLength, NCRYPT_PAD_OAEP_FLAG);
    };
    if (call(nullptr, 0, &size) != ERROR_SUCCESS || size == 0 || size > 1024)
        return false;
    result.resize(size);
    if (call(result.data(), size, &written) != ERROR_SUCCESS || written == 0 || written > size) {
        Clear(result); result.clear(); return false;
    }
    result.resize(written);
    return true;
}
} // namespace

bool IsTpmProtected(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= magic.size() &&
        std::equal(magic.begin(), magic.end(), bytes.begin());
}

bool ProtectWithTpm(std::span<const std::uint8_t> input,
                    std::vector<std::uint8_t>& output) {
    output.clear();
    if (input.empty() || input.size() > maxPayload) return false;
    NcryptHandle provider, rsa;
    if (!OpenMachineKey(provider, rsa, true)) return false;
    std::array<std::uint8_t, aesKeyLength> symmetric{};
    std::array<std::uint8_t, nonceLength> nonce{};
    std::array<std::uint8_t, tagLength> tag{};
    if (BCryptGenRandom(nullptr, symmetric.data(), static_cast<ULONG>(symmetric.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
        BCryptGenRandom(nullptr, nonce.data(), static_cast<ULONG>(nonce.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        SecureZeroMemory(symmetric.data(), symmetric.size()); return false;
    }
    std::vector<std::uint8_t> wrapped, ciphertext;
    bool ok = RsaCrypt(static_cast<NCRYPT_KEY_HANDLE>(rsa.handle), true, symmetric, wrapped) &&
              CryptGcm(true, input, symmetric, nonce, tag, ciphertext);
    SecureZeroMemory(symmetric.data(), symmetric.size());
    if (!ok || wrapped.size() > UINT16_MAX) return false;
    output.reserve(magic.size() + 2 + nonce.size() + tag.size() + wrapped.size() + ciphertext.size());
    output.insert(output.end(), magic.begin(), magic.end());
    output.push_back(static_cast<std::uint8_t>(wrapped.size() & 0xff));
    output.push_back(static_cast<std::uint8_t>(wrapped.size() >> 8));
    output.insert(output.end(), nonce.begin(), nonce.end());
    output.insert(output.end(), tag.begin(), tag.end());
    output.insert(output.end(), wrapped.begin(), wrapped.end());
    output.insert(output.end(), ciphertext.begin(), ciphertext.end());
    return true;
}

bool UnprotectWithTpm(std::span<const std::uint8_t> input,
                      std::vector<std::uint8_t>& output) {
    output.clear();
    if (!IsTpmProtected(input) || input.size() < 6 + nonceLength + tagLength ||
        input.size() > maxPayload + 1100) return false;
    const std::size_t wrappedSize = input[4] | (std::size_t(input[5]) << 8);
    const std::size_t offset = 6 + nonceLength + tagLength;
    if (wrappedSize < 128 || wrappedSize > 1024 || offset + wrappedSize >= input.size())
        return false;
    std::array<std::uint8_t, nonceLength> nonce{};
    std::array<std::uint8_t, tagLength> tag{};
    std::copy_n(input.data()+6,nonceLength,nonce.begin());
    std::copy_n(input.data()+6+nonceLength,tagLength,tag.begin());
    NcryptHandle provider, rsa;
    if (!OpenMachineKey(provider,rsa,false)) return false;
    std::vector<std::uint8_t> unwrapped;
    if (!RsaCrypt(static_cast<NCRYPT_KEY_HANDLE>(rsa.handle),false,
            input.subspan(offset,wrappedSize),unwrapped)) return false;
    if (unwrapped.size() != aesKeyLength) {Clear(unwrapped);return false;}
    std::array<std::uint8_t,aesKeyLength> symmetric{};
    std::copy(unwrapped.begin(),unwrapped.end(),symmetric.begin());
    Clear(unwrapped);
    const bool ok = CryptGcm(false,input.subspan(offset+wrappedSize),
                             symmetric,nonce,tag,output);
    SecureZeroMemory(symmetric.data(),symmetric.size());
    return ok;
}
} // namespace kb