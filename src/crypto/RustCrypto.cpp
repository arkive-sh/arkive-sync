#include "RustCrypto.hpp"

#include <arkive_crypto.h>

#include <stdexcept>
#include <utility>

namespace {

const uint8_t* bytesPtr(const std::vector<uint8_t>& bytes) {
    return bytes.empty() ? nullptr : bytes.data();
}

const uint8_t* stringPtr(const std::string& value) {
    return value.empty() ? nullptr : reinterpret_cast<const uint8_t*>(value.data());
}

std::vector<uint8_t> takeBytesResult(ArkiveResult result, const char* fnName) {
    if (!result.ok) {
        throw std::runtime_error(std::string(fnName) + " failed");
    }

    std::vector<uint8_t> output(result.data.ptr, result.data.ptr + result.data.len);
    arkive_free_buffer(result.data);
    return output;
}

std::string takeStringResult(ArkiveResult result, const char* fnName) {
    std::vector<uint8_t> bytes = takeBytesResult(result, fnName);
    return std::string(bytes.begin(), bytes.end());
}

void checkBoolResult(bool ok, const char* fnName) {
    if (!ok) {
        throw std::runtime_error(std::string(fnName) + " failed");
    }
}

}  // namespace

RustCrypto::Blake3Hasher::Blake3Hasher()
    : Blake3Hasher(arkive_blake3_hasher_new()) {}

RustCrypto::Blake3Hasher::Blake3Hasher(ArkiveBlake3Hasher* handle)
    : handle_(handle) {
    if (handle_ == nullptr) {
        throw std::runtime_error("arkive_blake3_hasher_new failed");
    }
}

RustCrypto::Blake3Hasher::~Blake3Hasher() {
    if (handle_ != nullptr) {
        arkive_blake3_hasher_free(handle_);
    }
}

RustCrypto::Blake3Hasher::Blake3Hasher(Blake3Hasher&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

RustCrypto::Blake3Hasher& RustCrypto::Blake3Hasher::operator=(Blake3Hasher&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            arkive_blake3_hasher_free(handle_);
        }
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

void RustCrypto::Blake3Hasher::update(const std::vector<uint8_t>& data) {
    checkBoolResult(
        arkive_blake3_hasher_update(handle_, bytesPtr(data), data.size()),
        "arkive_blake3_hasher_update"
    );
}

std::vector<uint8_t> RustCrypto::Blake3Hasher::digest() const {
    return takeBytesResult(
        arkive_blake3_hasher_digest(handle_),
        "arkive_blake3_hasher_digest"
    );
}

std::string RustCrypto::Blake3Hasher::digestHex() const {
    return takeStringResult(
        arkive_blake3_hasher_digest_hex(handle_),
        "arkive_blake3_hasher_digest_hex"
    );
}

std::vector<uint8_t> RustCrypto::Blake3Hasher::finalize() {
    return takeBytesResult(
        arkive_blake3_hasher_finalize(handle_),
        "arkive_blake3_hasher_finalize"
    );
}

std::string RustCrypto::Blake3Hasher::finalizeHex() {
    return takeStringResult(
        arkive_blake3_hasher_finalize_hex(handle_),
        "arkive_blake3_hasher_finalize_hex"
    );
}

RustCrypto::Sha256Hasher::Sha256Hasher()
    : Sha256Hasher(arkive_sha256_hasher_new()) {}

RustCrypto::Sha256Hasher::Sha256Hasher(ArkiveSha256Hasher* handle)
    : handle_(handle) {
    if (handle_ == nullptr) {
        throw std::runtime_error("arkive_sha256_hasher_new failed");
    }
}

RustCrypto::Sha256Hasher::~Sha256Hasher() {
    if (handle_ != nullptr) {
        arkive_sha256_hasher_free(handle_);
    }
}

RustCrypto::Sha256Hasher::Sha256Hasher(Sha256Hasher&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

RustCrypto::Sha256Hasher& RustCrypto::Sha256Hasher::operator=(Sha256Hasher&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            arkive_sha256_hasher_free(handle_);
        }
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

void RustCrypto::Sha256Hasher::update(const std::vector<uint8_t>& data) {
    checkBoolResult(
        arkive_sha256_hasher_update(handle_, bytesPtr(data), data.size()),
        "arkive_sha256_hasher_update"
    );
}

std::vector<uint8_t> RustCrypto::Sha256Hasher::digest() const {
    return takeBytesResult(
        arkive_sha256_hasher_digest(handle_),
        "arkive_sha256_hasher_digest"
    );
}

std::string RustCrypto::Sha256Hasher::digestHex() const {
    return takeStringResult(
        arkive_sha256_hasher_digest_hex(handle_),
        "arkive_sha256_hasher_digest_hex"
    );
}

std::vector<uint8_t> RustCrypto::Sha256Hasher::finalize() {
    return takeBytesResult(
        arkive_sha256_hasher_finalize(handle_),
        "arkive_sha256_hasher_finalize"
    );
}

std::string RustCrypto::Sha256Hasher::finalizeHex() {
    return takeStringResult(
        arkive_sha256_hasher_finalize_hex(handle_),
        "arkive_sha256_hasher_finalize_hex"
    );
}

std::vector<uint8_t> RustCrypto::generateMasterKey() const {
    return takeBytesResult(arkive_generate_master_key(), "arkive_generate_master_key");
}

std::vector<uint8_t> RustCrypto::generateFileKey() const {
    return takeBytesResult(arkive_generate_file_key(), "arkive_generate_file_key");
}

std::vector<uint8_t> RustCrypto::generateShareKey() const {
    return takeBytesResult(arkive_generate_share_key(), "arkive_generate_share_key");
}

std::vector<uint8_t> RustCrypto::generateSalt() const {
    return takeBytesResult(arkive_generate_salt(), "arkive_generate_salt");
}

void RustCrypto::zeroize(std::vector<uint8_t>& bytes) const {
    checkBoolResult(
        arkive_zeroize(bytes.empty() ? nullptr : bytes.data(), bytes.size()),
        "arkive_zeroize"
    );
}

std::vector<uint8_t> RustCrypto::blake3Hash(const std::vector<uint8_t>& input) const {
    return takeBytesResult(
        arkive_blake3_hash(bytesPtr(input), input.size()),
        "arkive_blake3_hash"
    );
}

std::string RustCrypto::blake3HashHex(const std::vector<uint8_t>& input) const {
    return takeStringResult(
        arkive_blake3_hash_hex(bytesPtr(input), input.size()),
        "arkive_blake3_hash_hex"
    );
}

std::vector<uint8_t> RustCrypto::sha256Hash(const std::vector<uint8_t>& input) const {
    return takeBytesResult(
        arkive_sha256_hash(bytesPtr(input), input.size()),
        "arkive_sha256_hash"
    );
}

std::string RustCrypto::sha256HashHex(const std::vector<uint8_t>& input) const {
    return takeStringResult(
        arkive_sha256_hash_hex(bytesPtr(input), input.size()),
        "arkive_sha256_hash_hex"
    );
}

std::vector<uint8_t> RustCrypto::derivePasswordKek(
    const std::string& password,
    const std::vector<uint8_t>& salt
) const {
    return takeBytesResult(
        arkive_derive_password_kek(
            stringPtr(password),
            password.size(),
            bytesPtr(salt),
            salt.size()
        ),
        "arkive_derive_password_kek"
    );
}

std::vector<uint8_t> RustCrypto::deriveSearchKey(const std::vector<uint8_t>& masterKey) const {
    return takeBytesResult(
        arkive_derive_search_key(bytesPtr(masterKey), masterKey.size()),
        "arkive_derive_search_key"
    );
}

std::vector<uint8_t> RustCrypto::hmacSha256(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& data
) const {
    return takeBytesResult(
        arkive_hmac_sha256(bytesPtr(key), key.size(), bytesPtr(data), data.size()),
        "arkive_hmac_sha256"
    );
}

std::vector<uint8_t> RustCrypto::encryptFileMetadata(
    const std::vector<uint8_t>& metadata,
    const std::vector<uint8_t>& masterKey,
    const std::vector<uint8_t>& aad
) const {
    return takeBytesResult(
        arkive_encrypt_file_metadata(
            bytesPtr(metadata),
            metadata.size(),
            bytesPtr(masterKey),
            masterKey.size(),
            bytesPtr(aad),
            aad.size()
        ),
        "arkive_encrypt_file_metadata"
    );
}

std::vector<uint8_t> RustCrypto::decryptFileMetadata(
    const std::vector<uint8_t>& encrypted,
    const std::vector<uint8_t>& masterKey,
    const std::vector<uint8_t>& aad
) const {
    return takeBytesResult(
        arkive_decrypt_file_metadata(
            bytesPtr(encrypted),
            encrypted.size(),
            bytesPtr(masterKey),
            masterKey.size(),
            bytesPtr(aad),
            aad.size()
        ),
        "arkive_decrypt_file_metadata"
    );
}

std::vector<uint8_t> RustCrypto::wrapMasterKey(
    const std::vector<uint8_t>& masterKey,
    const std::vector<uint8_t>& kek,
    const std::vector<uint8_t>& aad
) const {
    return takeBytesResult(
        arkive_wrap_master_key(
            bytesPtr(masterKey),
            masterKey.size(),
            bytesPtr(kek),
            kek.size(),
            bytesPtr(aad),
            aad.size()
        ),
        "arkive_wrap_master_key"
    );
}

std::vector<uint8_t> RustCrypto::unwrapMasterKey(
    const std::vector<uint8_t>& encrypted,
    const std::vector<uint8_t>& kek,
    const std::vector<uint8_t>& aad
) const {
    return takeBytesResult(
        arkive_unwrap_master_key(
            bytesPtr(encrypted),
            encrypted.size(),
            bytesPtr(kek),
            kek.size(),
            bytesPtr(aad),
            aad.size()
        ),
        "arkive_unwrap_master_key"
    );
}

std::vector<uint8_t> RustCrypto::wrapFileKey(
    const std::vector<uint8_t>& fileKey,
    const std::vector<uint8_t>& masterKey,
    const std::vector<uint8_t>& aad
) const {
    return takeBytesResult(
        arkive_wrap_file_key(
            bytesPtr(fileKey),
            fileKey.size(),
            bytesPtr(masterKey),
            masterKey.size(),
            bytesPtr(aad),
            aad.size()
        ),
        "arkive_wrap_file_key"
    );
}

std::vector<uint8_t> RustCrypto::unwrapFileKey(
    const std::vector<uint8_t>& encrypted,
    const std::vector<uint8_t>& masterKey,
    const std::vector<uint8_t>& aad
) const {
    return takeBytesResult(
        arkive_unwrap_file_key(
            bytesPtr(encrypted),
            encrypted.size(),
            bytesPtr(masterKey),
            masterKey.size(),
            bytesPtr(aad),
            aad.size()
        ),
        "arkive_unwrap_file_key"
    );
}

std::vector<uint8_t> RustCrypto::encryptChunk(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& aad,
    const std::vector<uint8_t>& input
) const {
    return takeBytesResult(
        arkive_encrypt_chunk(
            bytesPtr(key),
            key.size(),
            bytesPtr(aad),
            aad.size(),
            bytesPtr(input),
            input.size()
        ),
        "arkive_encrypt_chunk"
    );
}

std::vector<uint8_t> RustCrypto::decryptChunk(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& aad,
    const std::vector<uint8_t>& input
) const {
    return takeBytesResult(
        arkive_decrypt_chunk(
            bytesPtr(key),
            key.size(),
            bytesPtr(aad),
            aad.size(),
            bytesPtr(input),
            input.size()
        ),
        "arkive_decrypt_chunk"
    );
}

std::vector<uint8_t> RustCrypto::generateRecoveryKey() const {
    return takeBytesResult(arkive_generate_recovery_key(), "arkive_generate_recovery_key");
}

std::string RustCrypto::formatRecoveryKey(const std::vector<uint8_t>& recoveryKey) const {
    return takeStringResult(
        arkive_format_recovery_key(bytesPtr(recoveryKey), recoveryKey.size()),
        "arkive_format_recovery_key"
    );
}

std::vector<uint8_t> RustCrypto::parseRecoveryKey(const std::string& recoveryKey) const {
    return takeBytesResult(
        arkive_parse_recovery_key(stringPtr(recoveryKey), recoveryKey.size()),
        "arkive_parse_recovery_key"
    );
}

std::vector<uint8_t> RustCrypto::wrapMasterKeyForRecovery(
    const std::vector<uint8_t>& masterKey,
    const std::vector<uint8_t>& recoveryKey
) const {
    return takeBytesResult(
        arkive_wrap_master_key_for_recovery(
            bytesPtr(masterKey),
            masterKey.size(),
            bytesPtr(recoveryKey),
            recoveryKey.size()
        ),
        "arkive_wrap_master_key_for_recovery"
    );
}

std::vector<uint8_t> RustCrypto::recoverMasterKey(
    const std::vector<uint8_t>& encrypted,
    const std::vector<uint8_t>& recoveryKey
) const {
    return takeBytesResult(
        arkive_recover_master_key(
            bytesPtr(encrypted),
            encrypted.size(),
            bytesPtr(recoveryKey),
            recoveryKey.size()
        ),
        "arkive_recover_master_key"
    );
}

std::vector<uint8_t> RustCrypto::wrapMasterKeyWithRecoveryKey(
    const std::vector<uint8_t>& masterKey,
    const std::vector<uint8_t>& recoveryKey,
    const std::string& userId
) const {
    return takeBytesResult(
        arkive_wrap_master_key_with_recovery_key(
            bytesPtr(masterKey),
            masterKey.size(),
            bytesPtr(recoveryKey),
            recoveryKey.size(),
            stringPtr(userId),
            userId.size()
        ),
        "arkive_wrap_master_key_with_recovery_key"
    );
}

std::vector<uint8_t> RustCrypto::unwrapMasterKeyWithRecoveryKey(
    const std::vector<uint8_t>& encrypted,
    const std::vector<uint8_t>& recoveryKey,
    const std::string& userId
) const {
    return takeBytesResult(
        arkive_unwrap_master_key_with_recovery_key(
            bytesPtr(encrypted),
            encrypted.size(),
            bytesPtr(recoveryKey),
            recoveryKey.size(),
            stringPtr(userId),
            userId.size()
        ),
        "arkive_unwrap_master_key_with_recovery_key"
    );
}

std::vector<uint8_t> RustCrypto::wrapMasterKeyWithPassword(
    const std::vector<uint8_t>& masterKey,
    const std::string& password,
    const std::vector<uint8_t>& salt,
    const std::string& userId
) const {
    return takeBytesResult(
        arkive_wrap_master_key_with_password(
            bytesPtr(masterKey),
            masterKey.size(),
            stringPtr(password),
            password.size(),
            bytesPtr(salt),
            salt.size(),
            stringPtr(userId),
            userId.size()
        ),
        "arkive_wrap_master_key_with_password"
    );
}

RustCrypto::Blake3Hasher RustCrypto::createBlake3Hasher() const {
    return Blake3Hasher();
}

RustCrypto::Sha256Hasher RustCrypto::createSha256Hasher() const {
    return Sha256Hasher();
}
