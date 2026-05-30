#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* ptr;
    size_t len;
} ArkiveBuffer;

typedef struct {
    bool ok;
    ArkiveBuffer data;
} ArkiveResult;

typedef struct ArkiveBlake3Hasher ArkiveBlake3Hasher;
typedef struct ArkiveSha256Hasher ArkiveSha256Hasher;

ArkiveResult arkive_generate_master_key(void);
ArkiveResult arkive_generate_file_key(void);
ArkiveResult arkive_generate_share_key(void);
ArkiveResult arkive_generate_salt(void);
bool arkive_zeroize(uint8_t* bytes, size_t bytes_len);

ArkiveResult arkive_blake3_hash(
    const uint8_t* input,
    size_t input_len
);
ArkiveResult arkive_blake3_hash_hex(
    const uint8_t* input,
    size_t input_len
);
ArkiveResult arkive_sha256_hash(
    const uint8_t* input,
    size_t input_len
);
ArkiveResult arkive_sha256_hash_hex(
    const uint8_t* input,
    size_t input_len
);

ArkiveResult arkive_derive_password_kek(
    const uint8_t* password,
    size_t password_len,
    const uint8_t* salt,
    size_t salt_len
);
ArkiveResult arkive_derive_search_key(
    const uint8_t* master_key,
    size_t master_key_len
);
ArkiveResult arkive_hmac_sha256(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* data,
    size_t data_len
);

ArkiveResult arkive_encrypt_file_metadata(
    const uint8_t* metadata,
    size_t metadata_len,
    const uint8_t* master_key,
    size_t master_key_len,
    const uint8_t* aad,
    size_t aad_len
);
ArkiveResult arkive_decrypt_file_metadata(
    const uint8_t* encrypted,
    size_t encrypted_len,
    const uint8_t* master_key,
    size_t master_key_len,
    const uint8_t* aad,
    size_t aad_len
);

ArkiveResult arkive_wrap_master_key(
    const uint8_t* master_key,
    size_t master_key_len,
    const uint8_t* kek,
    size_t kek_len,
    const uint8_t* aad,
    size_t aad_len
);
ArkiveResult arkive_unwrap_master_key(
    const uint8_t* encrypted,
    size_t encrypted_len,
    const uint8_t* kek,
    size_t kek_len,
    const uint8_t* aad,
    size_t aad_len
);
ArkiveResult arkive_wrap_file_key(
    const uint8_t* file_key,
    size_t file_key_len,
    const uint8_t* master_key,
    size_t master_key_len,
    const uint8_t* aad,
    size_t aad_len
);
ArkiveResult arkive_unwrap_file_key(
    const uint8_t* encrypted,
    size_t encrypted_len,
    const uint8_t* master_key,
    size_t master_key_len,
    const uint8_t* aad,
    size_t aad_len
);

ArkiveResult arkive_encrypt_chunk(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* aad,
    size_t aad_len,
    const uint8_t* input,
    size_t input_len
);
ArkiveResult arkive_decrypt_chunk(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* aad,
    size_t aad_len,
    const uint8_t* input,
    size_t input_len
);

ArkiveResult arkive_generate_recovery_key(void);
ArkiveResult arkive_format_recovery_key(
    const uint8_t* recovery_key,
    size_t recovery_key_len
);
ArkiveResult arkive_parse_recovery_key(
    const uint8_t* recovery_key,
    size_t recovery_key_len
);
ArkiveResult arkive_wrap_master_key_for_recovery(
    const uint8_t* master_key,
    size_t master_key_len,
    const uint8_t* recovery_key,
    size_t recovery_key_len
);
ArkiveResult arkive_recover_master_key(
    const uint8_t* encrypted,
    size_t encrypted_len,
    const uint8_t* recovery_key,
    size_t recovery_key_len
);
ArkiveResult arkive_wrap_master_key_with_recovery_key(
    const uint8_t* master_key,
    size_t master_key_len,
    const uint8_t* recovery_key,
    size_t recovery_key_len,
    const uint8_t* user_id,
    size_t user_id_len
);
ArkiveResult arkive_unwrap_master_key_with_recovery_key(
    const uint8_t* encrypted,
    size_t encrypted_len,
    const uint8_t* recovery_key,
    size_t recovery_key_len,
    const uint8_t* user_id,
    size_t user_id_len
);
ArkiveResult arkive_wrap_master_key_with_password(
    const uint8_t* master_key,
    size_t master_key_len,
    const uint8_t* password,
    size_t password_len,
    const uint8_t* salt,
    size_t salt_len,
    const uint8_t* user_id,
    size_t user_id_len
);

ArkiveBlake3Hasher* arkive_blake3_hasher_new(void);
void arkive_blake3_hasher_free(ArkiveBlake3Hasher* hasher);
bool arkive_blake3_hasher_update(
    ArkiveBlake3Hasher* hasher,
    const uint8_t* data,
    size_t data_len
);
ArkiveResult arkive_blake3_hasher_digest(ArkiveBlake3Hasher* hasher);
ArkiveResult arkive_blake3_hasher_digest_hex(ArkiveBlake3Hasher* hasher);
ArkiveResult arkive_blake3_hasher_finalize(ArkiveBlake3Hasher* hasher);
ArkiveResult arkive_blake3_hasher_finalize_hex(ArkiveBlake3Hasher* hasher);

ArkiveSha256Hasher* arkive_sha256_hasher_new(void);
void arkive_sha256_hasher_free(ArkiveSha256Hasher* hasher);
bool arkive_sha256_hasher_update(
    ArkiveSha256Hasher* hasher,
    const uint8_t* data,
    size_t data_len
);
ArkiveResult arkive_sha256_hasher_digest(ArkiveSha256Hasher* hasher);
ArkiveResult arkive_sha256_hasher_digest_hex(ArkiveSha256Hasher* hasher);
ArkiveResult arkive_sha256_hasher_finalize(ArkiveSha256Hasher* hasher);
ArkiveResult arkive_sha256_hasher_finalize_hex(ArkiveSha256Hasher* hasher);

void arkive_free_buffer(ArkiveBuffer buffer);

#ifdef __cplusplus
}
#endif
