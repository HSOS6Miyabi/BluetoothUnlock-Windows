#include "storage.h"
#include "remote_storage.h"
#include "tpm_protection.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main() {
    std::vector<std::uint8_t> password;
    std::wstring original = L"Windows credential encryption test";
    if (!kb::EncryptPassword(original, password) || password.empty()) {
        std::cerr << "Password encryption failed\n";
        return 1;
    }
    std::wstring recovered;
    if (!kb::DecryptPassword(password, recovered) || recovered != original) {
        std::cerr << "Password round trip failed\n";
        return 2;
    }
    kb::Wipe(recovered);
    kb::Wipe(original);

    if (kb::IsTpmProtected(password)) {
        auto tampered = password;
        tampered.back() ^= 0x40;
        if (kb::DecryptPassword(tampered, recovered)) {
            std::cerr << "Tampered TPM ciphertext was accepted\n";
            return 3;
        }
    }
    const std::array<std::uint8_t, 4> marker{'T','P','M','2'};
    std::vector<std::uint8_t> invalid;
    if (!kb::IsTpmProtected(marker) || kb::UnprotectWithTpm(marker, invalid)) {
        std::cerr << "Malformed TPM envelope was accepted\n";
        return 4;
    }

    const std::wstring apiKey = L"rdapi_fixture_for_credential_round_trip";
    std::vector<std::uint8_t> encryptedApiKey;
    if (!kb::EncryptRemoteApiKey(apiKey, encryptedApiKey)) {
        std::cerr << "API key encryption failed\n";
        return 5;
    }
    if (!kb::DecryptRemoteApiKey(encryptedApiKey, recovered) || recovered != apiKey) {
        std::cerr << "API key round trip failed\n";
        return 6;
    }
    kb::Wipe(recovered);
    if (kb::IsTpmProtected(encryptedApiKey)) {
        auto tampered = encryptedApiKey;
        tampered.back() ^= 0x20;
        if (kb::DecryptRemoteApiKey(tampered, recovered)) {
            std::cerr << "Tampered API key ciphertext was accepted\n";
            return 7;
        }
    }
    std::cout << "Windows credential protection checks passed\n";
    return 0;
}
