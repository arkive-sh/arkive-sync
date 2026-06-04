CREATE TABLE IF NOT EXISTS upload_resume_sessions (
  id TEXT PRIMARY KEY,
  entry_id TEXT,
  local_path TEXT NOT NULL UNIQUE,
  local_size INTEGER NOT NULL,
  local_mtime TEXT,
  local_hash TEXT,
  folder_id TEXT,
  vault_id TEXT NOT NULL,
  file_id TEXT NOT NULL,
  upload_session_id TEXT NOT NULL UNIQUE,
  provider_upload_id TEXT NOT NULL,
  file_chunk_size INTEGER NOT NULL,
  total_chunks INTEGER NOT NULL,
  upload_part_size INTEGER NOT NULL,
  upload_part_count INTEGER NOT NULL,
  encrypted_file_key_blob TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_upload_resume_entry_id
ON upload_resume_sessions(entry_id);

CREATE TABLE IF NOT EXISTS upload_resume_parts (
  upload_session_id TEXT NOT NULL,
  part_number INTEGER NOT NULL,
  etag TEXT NOT NULL,
  upload_hash TEXT NOT NULL,
  chunk_manifest_json TEXT NOT NULL,
  combined_chunk_hashes TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (upload_session_id, part_number)
);

CREATE INDEX IF NOT EXISTS idx_upload_resume_parts_session
ON upload_resume_parts(upload_session_id);
