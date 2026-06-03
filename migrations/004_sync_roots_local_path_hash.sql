ALTER TABLE sync_roots ADD COLUMN local_path_hash TEXT;

DROP INDEX IF EXISTS idx_sync_roots_local_path;

CREATE UNIQUE INDEX IF NOT EXISTS idx_sync_roots_local_path_hash
ON sync_roots(local_path_hash);
