CREATE TABLE IF NOT EXISTS settings (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS account (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  base_url TEXT NOT NULL DEFAULT 'http://localhost:8080',
  user_id TEXT,
  email TEXT,
  vault_salt TEXT,
  encrypted_master_key TEXT,
  vault_session_key_id TEXT,
  vault_session_blob TEXT,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO account (id, base_url)
VALUES (1, 'http://localhost:8080')
ON CONFLICT(id) DO NOTHING;

CREATE TABLE IF NOT EXISTS sync_roots (
  id TEXT PRIMARY KEY,
  local_path TEXT NOT NULL UNIQUE,
  folder_id TEXT,
  enabled INTEGER NOT NULL DEFAULT 1,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS entries (
  id TEXT PRIMARY KEY,
  remote_id TEXT,
  sync_root_id TEXT NOT NULL,
  remote_type TEXT NOT NULL,
  local_path TEXT NOT NULL,
  is_directory INTEGER NOT NULL DEFAULT 0,
  parent_folder_id TEXT,
  local_size INTEGER,
  local_mtime TEXT,
  content_hash TEXT,
  remote_updated_at TEXT,
  remote_file_id TEXT,
  remote_folder_id TEXT,
  remote_parent_folder_id TEXT,
  remote_deleted_at TEXT,
  remote_purged_at TEXT,
  last_remote_seen_at TEXT,
  sync_state TEXT NOT NULL,
  last_seen_scan_job_id TEXT,
  last_synced_at TEXT,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS transfer_queue (
  id TEXT PRIMARY KEY,
  entry_id TEXT,
  job_type TEXT NOT NULL,
  status TEXT NOT NULL,
  local_path TEXT NOT NULL,
  remote_id TEXT,
  folder_id TEXT,
  bytes_total INTEGER,
  bytes_done INTEGER NOT NULL DEFAULT 0,
  error_message TEXT,
  retry_count INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS scan_jobs (
  id TEXT PRIMARY KEY,
  sync_root_id TEXT NOT NULL,
  status TEXT NOT NULL,
  cursor_path TEXT,
  started_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  completed_at TEXT
);

CREATE TABLE IF NOT EXISTS dirty_paths (
  id TEXT PRIMARY KEY,
  sync_root_id TEXT NOT NULL,
  relative_path TEXT,
  event_type TEXT NOT NULL,
  status TEXT NOT NULL,
  error_message TEXT,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

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

CREATE UNIQUE INDEX IF NOT EXISTS idx_entries_sync_root_local_path
ON entries(sync_root_id, local_path);

CREATE INDEX IF NOT EXISTS idx_entries_root_path
ON entries(sync_root_id, local_path);

CREATE INDEX IF NOT EXISTS idx_entries_root_state_path
ON entries(sync_root_id, sync_state, local_path);

CREATE INDEX IF NOT EXISTS idx_transfer_queue_status
ON transfer_queue(status);

CREATE INDEX IF NOT EXISTS idx_scan_jobs_sync_root_status
ON scan_jobs(sync_root_id, status);

CREATE UNIQUE INDEX IF NOT EXISTS idx_scan_jobs_active_root
ON scan_jobs(sync_root_id)
WHERE status = 'running';

CREATE INDEX IF NOT EXISTS idx_dirty_paths_sync_root_created_at
ON dirty_paths(sync_root_id, created_at);

CREATE INDEX IF NOT EXISTS idx_entries_remote_id
ON entries(remote_id);

CREATE INDEX IF NOT EXISTS idx_entries_sync_state
ON entries(sync_state);

CREATE UNIQUE INDEX IF NOT EXISTS idx_sync_roots_local_path
ON sync_roots(local_path);

CREATE UNIQUE INDEX IF NOT EXISTS idx_transfer_active_job
ON transfer_queue(entry_id, job_type)
WHERE status IN ('queued', 'running');

CREATE UNIQUE INDEX IF NOT EXISTS idx_dirty_paths_pending_path
ON dirty_paths(sync_root_id, relative_path)
WHERE status = 'pending'
  AND relative_path IS NOT NULL;

CREATE UNIQUE INDEX IF NOT EXISTS idx_dirty_paths_pending_full_rescan
ON dirty_paths(sync_root_id)
WHERE status = 'pending'
  AND relative_path IS NULL
  AND event_type = 'full_rescan';
