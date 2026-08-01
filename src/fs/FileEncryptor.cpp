#include "fs/FileEncryptor.hpp"
#include "crypto/Aad.hpp"
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encodeBase64URL(const std::vector<uint8_t> &bytes) {
  std::string encoded;
  encoded.reserve(((bytes.size() + 2) / 3) * 4);

  std::size_t index = 0;
  while (index + 3 <= bytes.size()) {
    const uint32_t block = (static_cast<uint32_t>(bytes[index]) << 16) |
                           (static_cast<uint32_t>(bytes[index + 1]) << 8) |
                           static_cast<uint32_t>(bytes[index + 2]);
    encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 6) & 0x3F]);
    encoded.push_back(kBase64Alphabet[block & 0x3F]);
    index += 3;
  }

  const std::size_t remainder = bytes.size() - index;
  if (remainder == 1) {
    const uint32_t block = static_cast<uint32_t>(bytes[index]) << 16;
    encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
    encoded.push_back('=');
    encoded.push_back('=');
  } else if (remainder == 2) {
    const uint32_t block = (static_cast<uint32_t>(bytes[index]) << 16) |
                           (static_cast<uint32_t>(bytes[index + 1]) << 8);
    encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 6) & 0x3F]);
    encoded.push_back('=');
  }

  for (char &ch : encoded) {
    if (ch == '+') {
      ch = '-';
    } else if (ch == '/') {
      ch = '_';
    }
  }
  while (!encoded.empty() && encoded.back() == '=') {
    encoded.pop_back();
  }

  return encoded;
}

std::string normalizeText(const std::string &value) {
  std::string normalized;
  normalized.reserve(value.size());
  bool lastWasSpace = false;

  for (const unsigned char raw : value) {
    const char ch = static_cast<char>(std::tolower(raw));
    if (std::isalnum(raw) || ch == '.') {
      normalized.push_back(ch);
      lastWasSpace = false;
    } else if (!lastWasSpace) {
      normalized.push_back(' ');
      lastWasSpace = true;
    }
  }

  while (!normalized.empty() && normalized.front() == ' ') {
    normalized.erase(normalized.begin());
  }
  while (!normalized.empty() && normalized.back() == ' ') {
    normalized.pop_back();
  }

  return normalized;
}

struct SearchTerm {
  std::string term;
  std::string field;
  int weight;
};

std::vector<SearchTerm> termsForText(const std::string &text) {
  const std::string normalized = normalizeText(text);
  std::vector<SearchTerm> terms;
  if (normalized.size() < 3) {
    return terms;
  }

  for (std::size_t i = 0; i + 3 <= normalized.size(); ++i) {
    terms.push_back({.term = normalized.substr(i, 3), .field = "name", .weight = 10});
  }
  return terms;
}

} // namespace

FileEncryptor::FileEncryptor(RustCrypto &crypto, VaultService &vaultService)
    : crypto_(crypto), vaultService_(vaultService) {}

std::vector<uint8_t> FileEncryptor::createFileKey() {
  return crypto_.generateFileKey();
}

std::vector<uint8_t>
FileEncryptor::wrapFileKey(const std::vector<uint8_t> &fileKey,
                           const std::string &vaultId,
                           const std::string &fileId) {
  if (fileKey.empty()) {
    throw std::invalid_argument("fileKey cannot be empty");
  }

  vaultService_.ensureUnlocked();

  const std::vector<uint8_t> aad =
      ArkiveAad::toBytes(ArkiveAad::makeFileKey(vaultId, fileId));

  return crypto_.wrapFileKey(fileKey, vaultService_.masterKey(), aad);
}

std::vector<uint8_t>
FileEncryptor::encryptMetadata(const std::string &metadataJson,
                               const std::vector<uint8_t> &fileKey,
                               const std::string &vaultId,
                               const std::string &fileId) {
  if (fileKey.empty()) {
    throw std::invalid_argument("fileKey cannot be empty");
  }

  const std::vector<uint8_t> metadataBytes(metadataJson.begin(),
                                           metadataJson.end());
  const std::vector<uint8_t> aad =
      ArkiveAad::toBytes(ArkiveAad::makeFileMetadata(vaultId, fileId));

  return crypto_.encryptChunk(fileKey, aad, metadataBytes);
}

std::vector<uint8_t>
FileEncryptor::encryptFolderName(const std::string &metadataJson) {
  vaultService_.ensureUnlocked();

  const std::vector<uint8_t> metadataBytes(metadataJson.begin(),
                                           metadataJson.end());
  return crypto_.encryptChunk(vaultService_.masterKey(),
                              ArkiveAad::toBytes(ArkiveAad::kFolderName),
                              metadataBytes);
}

std::vector<uint8_t>
FileEncryptor::encryptFolderMetadata(const std::string &metadataJson) {
  vaultService_.ensureUnlocked();

  const std::vector<uint8_t> metadataBytes(metadataJson.begin(),
                                           metadataJson.end());
  return crypto_.encryptChunk(vaultService_.masterKey(),
                              ArkiveAad::toBytes(ArkiveAad::kFolderMetadata),
                              metadataBytes);
}

std::vector<uint8_t>
FileEncryptor::encryptChunk(const std::vector<uint8_t> &plaintextChunk,
                            const std::vector<uint8_t> &fileKey,
                            const std::vector<uint8_t> &aad) {
  if (fileKey.empty()) {
    throw std::invalid_argument("fileKey cannot be empty");
  }

  return crypto_.encryptChunk(fileKey, aad, plaintextChunk);
}

std::vector<uint8_t>
FileEncryptor::encryptResumeFileKey(const std::vector<uint8_t> &fileKey,
                                    const std::string &uploadSessionId) {
  if (fileKey.empty()) {
    throw std::invalid_argument("fileKey cannot be empty");
  }

  vaultService_.ensureUnlocked();

  return crypto_.encryptChunk(vaultService_.masterKey(),
                              ArkiveAad::toBytes(
                                  ArkiveAad::makeResumeFileKey(uploadSessionId)),
                              fileKey);
}

std::vector<uint8_t>
FileEncryptor::decryptResumeFileKey(
    const std::vector<uint8_t> &encryptedFileKeyBlob,
    const std::string &uploadSessionId) {
  if (encryptedFileKeyBlob.empty()) {
    throw std::invalid_argument("encryptedFileKeyBlob cannot be empty");
  }

  vaultService_.ensureUnlocked();

  return crypto_.decryptChunk(vaultService_.masterKey(),
                              ArkiveAad::toBytes(
                                  ArkiveAad::makeResumeFileKey(uploadSessionId)),
                              encryptedFileKeyBlob);
}

std::vector<uint8_t> FileEncryptor::hashBytes(const std::vector<uint8_t> &bytes) {
  return crypto_.blake3Hash(bytes);
}

std::vector<UploadCompleteSearchToken>
FileEncryptor::createSearchTokenEntries(const std::string &vaultId,
                                        const std::string &text) {
  const std::vector<SearchTerm> terms = termsForText(text);
  if (terms.empty()) {
    return {};
  }

  std::vector<uint8_t> searchKey;
  std::vector<UploadCompleteSearchToken> entries;
  std::unordered_set<std::string> seen;

  try {
    vaultService_.ensureUnlocked();
    searchKey = crypto_.deriveSearchKey(vaultService_.masterKey());

    for (const auto &term : terms) {
      const std::string dedupeKey = term.field + ":" + term.term;
      if (!seen.insert(dedupeKey).second) {
        continue;
      }

      const std::string payload = vaultId + ":" + term.term;
      std::vector<uint8_t> digest;
      digest = crypto_.hmacSha256(searchKey,
                                  std::vector<uint8_t>(payload.begin(),
                                                       payload.end()));

      entries.push_back({
          .token = encodeBase64URL(digest),
          .field = term.field,
          .weight = term.weight,
      });
      crypto_.zeroize(digest);

      if (entries.size() >= 128) {
        break;
      }
    }
  } catch (...) {
    if (!searchKey.empty()) {
      crypto_.zeroize(searchKey);
    }
    throw;
  }

  if (!searchKey.empty()) {
    crypto_.zeroize(searchKey);
  }

  return entries;
}

void FileEncryptor::zeroize(std::vector<uint8_t> &bytes) { crypto_.zeroize(bytes); }
