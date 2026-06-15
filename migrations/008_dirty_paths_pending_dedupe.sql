CREATE UNIQUE INDEX IF NOT EXISTS idx_dirty_paths_pending_path
ON dirty_paths(sync_root_id, relative_path)
WHERE status = 'pending'
  AND relative_path IS NOT NULL;

CREATE UNIQUE INDEX IF NOT EXISTS idx_dirty_paths_pending_full_rescan
ON dirty_paths(sync_root_id)
WHERE status = 'pending'
  AND relative_path IS NULL
  AND event_type = 'full_rescan';
