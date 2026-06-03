CREATE TABLE IF NOT EXISTS settings (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS account (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  base_url TEXT NOT NULL DEFAULT 'http://localhost:8080',
  email TEXT,
  vault_salt TEXT,
  encrypted_master_key TEXT,
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
  encrypted_name TEXT,
  local_size INTEGER,
  local_mtime TEXT,
  local_hash TEXT,
  remote_updated_at TEXT,
  sync_state TEXT NOT NULL,
  last_synced_at TEXT,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS transfer_queue (
  id TEXT PRIMARY KEY,
  entry_id TEXT,
  direction TEXT NOT NULL,
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

CREATE UNIQUE INDEX IF NOT EXISTS idx_entries_sync_root_local_path
ON entries(sync_root_id, local_path);

CREATE INDEX IF NOT EXISTS idx_transfer_queue_status
ON transfer_queue(status);

CREATE INDEX IF NOT EXISTS idx_entries_remote_id
ON entries(remote_id);

CREATE INDEX IF NOT EXISTS idx_entries_sync_state
ON entries(sync_state);

CREATE UNIQUE INDEX IF NOT EXISTS idx_transfer_active_upload
ON transfer_queue(entry_id, direction)
WHERE direction = 'upload'
AND status IN ('queued', 'running');
