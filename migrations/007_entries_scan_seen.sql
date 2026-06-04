ALTER TABLE entries ADD COLUMN scan_seen INTEGER NOT NULL DEFAULT 0;

UPDATE entries
SET scan_seen = 1
WHERE scan_seen IS NULL;
