CREATE UNIQUE INDEX IF NOT EXISTS idx_sync_roots_local_path_hash
ON sync_roots(local_path_hash);
