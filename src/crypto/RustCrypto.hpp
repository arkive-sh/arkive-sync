#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ArkiveBlake3Hasher;
struct ArkiveSha256Hasher;

class RustCrypto {
public:
    class Blake3Hasher {
    public:
        Blake3Hasher();
        ~Blake3Hasher();

        Blake3Hasher(const Blake3Hasher&) = delete;
        Blake3Hasher& operator=(const Blake3Hasher&) = delete;
        Blake3Hasher(Blake3Hasher&& other) noexcept;
        Blake3Hasher& operator=(Blake3Hasher&& other) noexcept;

        void update(const std::vector<uint8_t>& data);
        std::vector<uint8_t> digest() const;
        std::string digestHex() const;
        std::vector<uint8_t> finalize();
        std::string finalizeHex();

    private:
        explicit Blake3Hasher(ArkiveBlake3Hasher* handle);
        ArkiveBlake3Hasher* handle_;
    };

    class Sha256Hasher {
    public:
        Sha256Hasher();
        ~Sha256Hasher();

        Sha256Hasher(const Sha256Hasher&) = delete;
        Sha256Hasher& operator=(const Sha256Hasher&) = delete;
        Sha256Hasher(Sha256Hasher&& other) noexcept;
        Sha256Hasher& operator=(Sha256Hasher&& other) noexcept;

        void update(const std::vector<uint8_t>& data);
        std::vector<uint8_t> digest() const;
        std::string digestHex() const;
        std::vector<uint8_t> finalize();
        std::string finalizeHex();

    private:
        explicit Sha256Hasher(ArkiveSha256Hasher* handle);
        ArkiveSha256Hasher* handle_;
    };

    std::vector<uint8_t> generateMasterKey() const;
    std::vector<uint8_t> generateFileKey() const;
    std::vector<uint8_t> generateShareKey() const;
    std::vector<uint8_t> generateSalt() const;
    void zeroize(std::vector<uint8_t>& bytes) const;

    std::vector<uint8_t> blake3Hash(const std::vector<uint8_t>& input) const;
    std::string blake3HashHex(const std::vector<uint8_t>& input) const;
    std::vector<uint8_t> sha256Hash(const std::vector<uint8_t>& input) const;
    std::string sha256HashHex(const std::vector<uint8_t>& input) const;

    std::vector<uint8_t> derivePasswordKek(
        const std::string& password,
        const std::vector<uint8_t>& salt
    ) const;
    std::vector<uint8_t> deriveSearchKey(const std::vector<uint8_t>& masterKey) const;
    std::vector<uint8_t> hmacSha256(
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& data
    ) const;

    std::vector<uint8_t> encryptFileMetadata(
        const std::vector<uint8_t>& metadata,
        const std::vector<uint8_t>& masterKey,
        const std::vector<uint8_t>& aad
    ) const;
    std::vector<uint8_t> decryptFileMetadata(
        const std::vector<uint8_t>& encrypted,
        const std::vector<uint8_t>& masterKey,
        const std::vector<uint8_t>& aad
    ) const;

    std::vector<uint8_t> wrapMasterKey(
        const std::vector<uint8_t>& masterKey,
        const std::vector<uint8_t>& kek,
        const std::vector<uint8_t>& aad
    ) const;
    std::vector<uint8_t> unwrapMasterKey(
        const std::vector<uint8_t>& encrypted,
        const std::vector<uint8_t>& kek,
        const std::vector<uint8_t>& aad
    ) const;
    std::vector<uint8_t> wrapFileKey(
        const std::vector<uint8_t>& fileKey,
        const std::vector<uint8_t>& masterKey,
        const std::vector<uint8_t>& aad
    ) const;
    std::vector<uint8_t> unwrapFileKey(
        const std::vector<uint8_t>& encrypted,
        const std::vector<uint8_t>& masterKey,
        const std::vector<uint8_t>& aad
    ) const;

    std::vector<uint8_t> encryptChunk(
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& aad,
        const std::vector<uint8_t>& input
    ) const;
    std::vector<uint8_t> decryptChunk(
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& aad,
        const std::vector<uint8_t>& input
    ) const;

    std::vector<uint8_t> generateRecoveryKey() const;
    std::string formatRecoveryKey(const std::vector<uint8_t>& recoveryKey) const;
    std::vector<uint8_t> parseRecoveryKey(const std::string& recoveryKey) const;
    std::vector<uint8_t> wrapMasterKeyForRecovery(
        const std::vector<uint8_t>& masterKey,
        const std::vector<uint8_t>& recoveryKey
    ) const;
    std::vector<uint8_t> recoverMasterKey(
        const std::vector<uint8_t>& encrypted,
        const std::vector<uint8_t>& recoveryKey
    ) const;
    std::vector<uint8_t> wrapMasterKeyWithRecoveryKey(
        const std::vector<uint8_t>& masterKey,
        const std::vector<uint8_t>& recoveryKey,
        const std::string& userId
    ) const;
    std::vector<uint8_t> unwrapMasterKeyWithRecoveryKey(
        const std::vector<uint8_t>& encrypted,
        const std::vector<uint8_t>& recoveryKey,
        const std::string& userId
    ) const;
    std::vector<uint8_t> wrapMasterKeyWithPassword(
        const std::vector<uint8_t>& masterKey,
        const std::string& password,
        const std::vector<uint8_t>& salt,
        const std::string& userId
    ) const;

    Blake3Hasher createBlake3Hasher() const;
    Sha256Hasher createSha256Hasher() const;
};
