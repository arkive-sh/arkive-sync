#include "upload/MultipartUploader.hpp"

#include "fs/ArkiveFileReader.hpp"
#include "fs/FileEncryptor.hpp"
#include "helpers/Base64.hpp"
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {

void appendBytes(std::vector<uint8_t> &target,
                 const std::vector<uint8_t> &source) {
  target.insert(target.end(), source.begin(), source.end());
}

UploadPartPlan makePartPlan(uint64_t partNumber, const UploadPlan &plan) {
  const uint64_t partStart = (partNumber - 1) * plan.uploadPartSize;
  const uint64_t partEnd =
      std::min<uint64_t>(partStart + plan.uploadPartSize, plan.originalSize);
  return UploadPartPlan{
      .partNumber = partNumber,
      .partStart = partStart,
      .partEnd = partEnd,
      .firstChunkNumber = (partStart / plan.fileChunkSize) + 1,
  };
}

} // namespace

MultipartUploader::MultipartUploader(ArkiveApi &api, FileEncryptor &fileEncryptor)
    : api_(api), fileEncryptor_(fileEncryptor) {}

std::vector<UploadedPartResult> MultipartUploader::uploadParts(
    const std::filesystem::path &path, const std::vector<uint8_t> &fileKey,
    const StartUploadResponse &started, const UploadPlan &plan,
    uint64_t partConcurrency,
    const std::vector<UploadedPartResult> &completedParts,
    const std::function<void(const UploadedPartResult &)> &onPartUploaded) {
  std::vector<UploadedPartResult> uploadedParts(
      static_cast<std::size_t>(plan.uploadPartCount));
  for (const auto &completedPart : completedParts) {
    if (completedPart.plan.partNumber == 0 ||
        completedPart.plan.partNumber > plan.uploadPartCount) {
      throw std::invalid_argument("completed part number exceeds upload plan");
    }
    uploadedParts[static_cast<std::size_t>(completedPart.plan.partNumber - 1)] =
        completedPart;
  }
  std::atomic<uint64_t> nextPartNumber{1};
  std::mutex resultsMutex;
  std::mutex errorMutex;
  std::exception_ptr firstError;
  std::atomic<bool> stopWorkers{false};

  auto uploadWorker = [&]() {
    while (!stopWorkers.load()) {
      const uint64_t partNumber = nextPartNumber.fetch_add(1);
      if (partNumber > plan.uploadPartCount) {
        return;
      }
      if (!uploadedParts[static_cast<std::size_t>(partNumber - 1)].etag.empty()) {
        continue;
      }

      std::vector<uint8_t> uploadBody;
      UploadedPartResult partResult;

      try {
        partResult.plan = makePartPlan(partNumber, plan);
        const uint64_t partChunkCount =
            ((partResult.plan.partEnd - partResult.plan.partStart) +
             plan.fileChunkSize - 1) /
            plan.fileChunkSize;
        ArkiveFileReader reader(path, fileEncryptor_, fileKey, started.vaultId,
                                started.fileId, plan.fileChunkSize,
                                plan.totalChunks,
                                partResult.plan.firstChunkNumber,
                                partChunkCount);

        while (reader.hasNextChunk()) {
          EncryptedFileChunk encryptedChunk = reader.nextEncryptedChunk();
          EncryptedChunkResult chunkResult;
          chunkResult.chunkNumber = encryptedChunk.chunkNo;
          chunkResult.plaintextSize = encryptedChunk.plaintextSize;
          chunkResult.ciphertextSize = encryptedChunk.ciphertext.size();

          std::vector<uint8_t> encryptedHashBytes =
              fileEncryptor_.hashBytes(encryptedChunk.ciphertext);
          chunkResult.encryptedHash = encodeBase64(encryptedHashBytes);

          appendBytes(partResult.combinedChunkHashes,
                      encryptedHashBytes);
          appendBytes(uploadBody, encryptedChunk.ciphertext);
          partResult.chunks.push_back(std::move(chunkResult));

          if (!encryptedHashBytes.empty()) {
            fileEncryptor_.zeroize(encryptedHashBytes);
          }
          if (!encryptedChunk.ciphertext.empty()) {
            fileEncryptor_.zeroize(encryptedChunk.ciphertext);
          }
        }

        if (uploadBody.empty()) {
          throw std::runtime_error("upload part had no encrypted chunk payload");
        }

        std::vector<uint8_t> uploadHashBytes =
            fileEncryptor_.hashBytes(uploadBody);
        partResult.uploadHash = encodeBase64(uploadHashBytes);

        const PresignPartsResponse presigned = api_.presignParts(
            started.uploadSessionId, {static_cast<int>(partNumber)});

        const auto urlIt = presigned.urls.find(static_cast<int>(partNumber));
        if (urlIt == presigned.urls.end()) {
          throw std::runtime_error("missing presigned URL for upload part " +
                                   std::to_string(partNumber));
        }

        partResult.etag = api_.putEncryptedPartToStorage(urlIt->second, uploadBody);
        api_.uploadPart(started.uploadSessionId,
                        UploadPartRequest{
                            .partNumber = static_cast<int>(partNumber),
                            .encryptedHash = partResult.uploadHash,
                            .etag = partResult.etag,
                        });
        if (onPartUploaded) {
          onPartUploaded(partResult);
        }

        if (!uploadHashBytes.empty()) {
          fileEncryptor_.zeroize(uploadHashBytes);
        }

        {
          std::lock_guard<std::mutex> lock(resultsMutex);
          uploadedParts[static_cast<std::size_t>(partNumber - 1)] =
              std::move(partResult);
        }
      } catch (...) {
        stopWorkers.store(true);
        std::lock_guard<std::mutex> lock(errorMutex);
        if (!firstError) {
          firstError = std::current_exception();
        }
      }

      if (!uploadBody.empty()) {
        fileEncryptor_.zeroize(uploadBody);
      }
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(partConcurrency));
  for (uint64_t workerIndex = 0; workerIndex < partConcurrency; ++workerIndex) {
    workers.emplace_back(uploadWorker);
  }
  for (auto &worker : workers) {
    worker.join();
  }
  if (firstError) {
    std::rethrow_exception(firstError);
  }

  return uploadedParts;
}
