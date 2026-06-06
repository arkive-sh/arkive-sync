ALTER TABLE entries ADD COLUMN remote_file_id TEXT;
ALTER TABLE entries ADD COLUMN remote_folder_id TEXT;
ALTER TABLE entries ADD COLUMN remote_parent_folder_id TEXT;
ALTER TABLE entries ADD COLUMN remote_deleted_at TEXT;
ALTER TABLE entries ADD COLUMN remote_purged_at TEXT;
ALTER TABLE entries ADD COLUMN last_remote_seen_at TEXT;
