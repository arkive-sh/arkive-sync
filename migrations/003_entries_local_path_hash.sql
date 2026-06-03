ALTER TABLE entries ADD COLUMN local_path_hash TEXT;

DROP INDEX IF EXISTS idx_entries_local_path;
DROP INDEX IF EXISTS idx_entries_sync_root_local_path;

CREATE UNIQUE INDEX IF NOT EXISTS idx_entries_sync_root_local_path_hash
ON entries(sync_root_id, local_path_hash);
