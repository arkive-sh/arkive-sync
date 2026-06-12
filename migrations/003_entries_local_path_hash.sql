CREATE INDEX IF NOT EXISTS idx_entries_root_path
ON entries(sync_root_id, local_path);

CREATE INDEX IF NOT EXISTS idx_entries_root_state_path
ON entries(sync_root_id, sync_state, local_path);
