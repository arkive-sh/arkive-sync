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

std::vector<SearchTerm> termsForFile(const std::string &name,
                                     const std::string &mime) {
  const std::string normalizedName = normalizeText(name);
  const std::string normalizedMime = normalizeText(mime);

  std::string ext;
  const std::size_t dot = normalizedName.find_last_of('.');
  if (dot != std::string::npos && dot + 1 < normalizedName.size()) {
    ext = normalizedName.substr(dot + 1);
  }

  std::string wordsSource = normalizedName;
  std::replace(wordsSource.begin(), wordsSource.end(), '.', ' ');
  std::vector<SearchTerm> terms;

  std::size_t start = 0;
  while (start < wordsSource.size()) {
    const std::size_t end = wordsSource.find(' ', start);
    const std::string word =
        wordsSource.substr(start, end == std::string::npos ? end : end - start);
    if (!word.empty()) {
      terms.push_back({.term = word, .field = "name", .weight = 10});
      if (word.size() >= 3) {
        for (std::size_t i = 3; i <= std::min<std::size_t>(word.size(), 32);
             ++i) {
          terms.push_back(
              {.term = word.substr(0, i), .field = "prefix", .weight = 1});
        }
      }
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  if (!ext.empty()) {
    terms.push_back({.term = ext, .field = "ext", .weight = 4});
  }
  if (!normalizedMime.empty()) {
    terms.push_back({.term = normalizedMime, .field = "mime", .weight = 2});
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

  if (!vaultService_.isUnlocked()) {
    vaultService_.restoreSession();
  }
  if (!vaultService_.isUnlocked()) {
    throw std::runtime_error(
        "Vault is locked. Run `arkive-sync login` to unlock or restore the vault session.");
  }

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
  if (!vaultService_.isUnlocked()) {
    vaultService_.restoreSession();
  }
  if (!vaultService_.isUnlocked()) {
    throw std::runtime_error(
        "Vault is locked. Run `arkive-sync login` to unlock or restore the vault session.");
  }

  const std::vector<uint8_t> metadataBytes(metadataJson.begin(),
                                           metadataJson.end());
  return crypto_.encryptChunk(vaultService_.masterKey(),
                              ArkiveAad::toBytes(ArkiveAad::kFolderName),
                              metadataBytes);
}

std::vector<uint8_t>
FileEncryptor::encryptFolderMetadata(const std::string &metadataJson) {
  if (!vaultService_.isUnlocked()) {
    vaultService_.restoreSession();
  }
  if (!vaultService_.isUnlocked()) {
    throw std::runtime_error(
        "Vault is locked. Run `arkive-sync login` to unlock or restore the vault session.");
  }

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

  if (!vaultService_.isUnlocked()) {
    vaultService_.restoreSession();
  }
  if (!vaultService_.isUnlocked()) {
    throw std::runtime_error("Vault is locked");
  }

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

  if (!vaultService_.isUnlocked()) {
    vaultService_.restoreSession();
  }
  if (!vaultService_.isUnlocked()) {
    throw std::runtime_error("Vault is locked");
  }

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
                                        const std::string &name,
                                        const std::string &mime) {
  const std::vector<SearchTerm> terms = termsForFile(name, mime);
  if (terms.empty()) {
    return {};
  }

  std::vector<uint8_t> searchKey;
  std::vector<UploadCompleteSearchToken> entries;
  std::unordered_set<std::string> seen;

  try {
    if (!vaultService_.isUnlocked()) {
      vaultService_.restoreSession();
    }
    if (vaultService_.isUnlocked()) {
      searchKey = crypto_.deriveSearchKey(vaultService_.masterKey());
    }

    for (const auto &term : terms) {
      const std::string dedupeKey = term.field + ":" + term.term;
      if (!seen.insert(dedupeKey).second) {
        continue;
      }

      const std::string payload = vaultId + ":" + term.term;
      std::vector<uint8_t> digest;
      if (!searchKey.empty()) {
        digest = crypto_.hmacSha256(searchKey,
                                    std::vector<uint8_t>(payload.begin(),
                                                         payload.end()));
      } else {
        throw std::runtime_error(
            "Vault is locked. Run `arkive-sync login` to unlock or restore the vault session.");
      }

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
