# Arkive Native Engine Notes

## Product Thesis

Arkive should become a serious zero-knowledge file platform that combines:

- zero-knowledge encryption
- streaming-first reads for media and large files
- sharing-first product UX
- self-host option
- native desktop integration
- virtual filesystem access
- strong cross-platform behavior

The wedge is not just "encrypted cloud storage". The wedge is:

- encrypted like privacy-first products
- usable like a real local filesystem
- streamable in native apps like VLC and image viewers
- shareable like collaboration-first products
- self-hostable for users who want control

The gap in the market is real. Most products only cover some of these:

- privacy-first but weak local filesystem UX
- self-hosted but weak zero-knowledge story
- sync-first but not sharing-first
- encrypted but poor streaming/random-access behavior

Arkive should close that gap.

## Current State In This Repo

This repo currently contains the early native core for Arkive Sync as a C++20 CLI app built with CMake.

### Build and runtime shape

- C++20 project with CMake
- dependencies: `spdlog`, `nlohmann/json`, `Catch2`, `libcurl`, `SQLite3`
- static Rust crypto library at `third_party/arkive-crypto`
- local SQLite database for persistent state

### Current architecture

- `src/main.cpp`
  - composition root
  - wires repos, services, HTTP client, API client, vault service, and app entry
- `src/app`
  - CLI command routing and top-level use cases
- `src/api`
  - Arkive API models and HTTP client
- `src/db`
  - SQLite database bootstrap and helpers
- `src/repo`
  - persistence layer for account, sync roots, entries, and queue jobs
- `src/service`
  - auth, sync scanning, queue operations, uploads, vault lifecycle
- `src/fs`
  - file scanning, hashing, and encryption-related filesystem helpers
- `src/crypto`
  - Rust crypto bridge

### Features already built

- local database initialization
- account persistence with base URL
- HTTP client and API layer
- login flow
- logout flow
- session check flow
- vault unlock/lock flow
- persisted vault material refresh path
- sync root registration
- local filesystem scan
- local file hashing
- sync entry upsert into SQLite
- deleted entry marking
- upload queue population for changed files
- queue stats
- queue retry failed
- queue clear done

### CLI commands present today

- `arkive-sync status`
- `arkive-sync set-base-url <url>`
- `arkive-sync login`
- `arkive-sync logout`
- `arkive-sync sync add <path>`
- `arkive-sync sync run <path>`
- `arkive-sync queue`
- `arkive-sync queue retry-failed`
- `arkive-sync queue clear-done`

### Commands stubbed but not implemented yet

- `sync run-all`
- `sync list`
- `sync remove`
- `queue process`
- `daemon`
- actual upload execution flow from CLI

## Non-Negotiable Product Requirements

These need to stay true through the implementation:

- zero-knowledge end to end
- master key never leaves trusted client runtime in plaintext
- chunk/range-friendly crypto
- fast random-access reads for video seeking
- path and metadata model that supports encrypted names
- cache model that does not accidentally destroy the zero-knowledge story
- native-app compatibility through a mounted filesystem later
- self-host architecture must be real, not marketing only

## High-Level Architecture Direction

We should build this in layers.

### 1. Shared native core

This comes first. It must reach parity with what the web client supports before daemon/UI/VFS work.

The shared core should own:

- auth/session lifecycle
- vault unlock state
- master key lifecycle
- metadata models
- encrypted filename handling
- file manifest/chunk mapping
- range fetch logic
- decrypt pipeline
- upload pipeline
- share logic
- sync logic
- retry/backoff
- local cache policy
- logging and diagnostics

This layer must stay platform-neutral.

### 2. Daemon

After the core is stable, move it behind a daemon process.

The daemon should own:

- long-lived session and unlock state
- background jobs
- sync execution
- transfer execution
- mount lifecycle later
- IPC API for UI and other clients

### 3. Tauri desktop app

The Tauri app should be a product shell, not the engine.

The UI should own:

- login and unlock screens
- settings
- sync root management
- share flows
- transfer/activity views
- mount controls
- logs/status/errors

The UI should talk to the daemon over a stable local IPC boundary.

### 4. Virtual filesystem and caching

After the daemon is solid, add mounted filesystem support.

Platform strategy:

- Linux: `libfuse`
- Windows: `WinFsp`
- macOS: `macFUSE` or `Fuse-T` initially, native Apple virtualization later if justified

The filesystem adapters should stay thin and forward ops into the shared core/daemon.

## Core Design Principles

### Shared core, thin adapters

We should not build one giant platform-specific app. We should build:

- one shared core
- one daemon around it
- thin per-OS mount adapters
- a separate Tauri UI shell

### Crypto inside the engine, not the UI

The Tauri app must not hold the core crypto logic for mounted file access.

- decryption belongs in the native engine/daemon
- master key stays in daemon memory
- UI requests operations from daemon

### Range-friendly media path

The crypto format is already chunk/range-friendly. We should reuse the same approach from the web product in the native engine:

- fetch only required encrypted ranges
- decrypt only required chunks
- support seeks efficiently
- support partial reads cleanly
- make VLC and native image viewers work later through normal file semantics

### Cache carefully

Default policy should prefer encrypted chunk caching.

Plaintext cache should be:

- avoided by default
- tightly controlled
- only introduced where app compatibility requires it

### One serious product API

The daemon should expose a stable local API so the system can later support:

- Tauri desktop UI
- CLI tools
- background services
- test harnesses
- possible future mobile/companion flows

## Target Capability Map

### Native core capabilities we need before daemonization

- account/session management
- vault unlock and master key lifecycle
- encrypted metadata parsing and storage
- upload preparation from local plaintext files
- download/read pipeline from encrypted remote data
- chunk fetch + decrypt + verify pipeline
- share creation and share access primitives
- sync state machine parity with web capabilities
- conflict behavior
- error taxonomy
- instrumentation/logging

### Daemon capabilities after core parity

- long-running process lifecycle
- local IPC
- background sync
- background uploads/downloads
- transfer scheduling
- queue workers
- persistent status
- safe unlock/lock behavior

### Desktop capabilities after daemon

- login/unlock UX
- settings and environment management
- sync root setup
- sharing UX
- transfer monitor
- diagnostics page

### Filesystem capabilities after daemon and desktop

- mount/unmount
- stat/readdir
- open/read/seek/close
- placeholder strategy if needed
- chunk cache/read-ahead
- media-friendly behavior
- native file explorer integration

## Ordered Execution Plan

Follow this sequence strictly.

### Phase 0: Stabilize what already exists

1. Freeze the current native CLI surface and document exact behavior.
2. Clean up repo structure and separate stable code from experiments.
3. Expand tests around existing auth, vault, scan, repo, and queue code.
4. Define the canonical error model and logging strategy.
5. Make sure the current local DB schema is intentional and versionable.

### Phase 1: Nail the shared native core

This is the most important phase.

1. Define the native core module boundaries.
2. Port the web-supported crypto/read/upload/share logic into the native core.
3. Reuse the same chunk/range decryption approach from the web product.
4. Build the metadata model for encrypted names and file hierarchy.
5. Implement a canonical file object model:
   - file id
   - parent id
   - encrypted name
   - decrypted display name
   - content size
   - chunk map
   - hashes
   - timestamps
6. Implement the remote read pipeline:
   - resolve file metadata
   - request chunk/range
   - decrypt
   - verify
   - return plaintext slice
7. Implement the upload pipeline:
   - read local file
   - chunk
   - encrypt
   - hash
   - upload parts
   - finalize
8. Implement share primitives supported by the product:
   - share creation
   - permission model
   - link/share metadata
9. Make sync logic feature-complete relative to the web-supported model.
10. Add deep tests:
   - chunk read correctness
   - random seek correctness
   - metadata decrypt correctness
   - upload/download roundtrip
   - vault lock/unlock behavior

### Phase 2: Refactor CLI around the real core

1. Stop letting the CLI directly own too much orchestration.
2. Make CLI commands call the new shared core services.
3. Implement missing sync and queue commands properly.
4. Add a real upload command path that uses the production upload pipeline.
5. Add diagnostic commands useful for development.

### Phase 3: Introduce the daemon

1. Define daemon responsibilities and keep them narrow.
2. Move long-lived session, queue, and background orchestration into daemon.
3. Design a stable local IPC protocol.
4. Add daemon lifecycle:
   - start
   - stop
   - health
   - lock
   - unlock
   - status
5. Move sync workers and transfer workers into background execution.
6. Ensure UI restarts do not kill background jobs later.
7. Add tests for crash recovery and state restoration.

### Phase 4: Build the Tauri desktop shell

1. Create desktop shell only after daemon API is stable enough.
2. Keep Tauri thin and product-focused.
3. Implement:
   - onboarding
   - login
   - unlock
   - sync root management
   - share management
   - transfer/activity monitor
   - settings
   - diagnostics
4. Make Tauri talk only to daemon API, not to internal libraries directly.
5. Add UX for zero-knowledge lifecycle:
   - locked
   - unlocked
   - session expired
   - re-auth required

### Phase 5: Virtual filesystem

1. Define a platform-neutral VFS core API.
2. Keep business logic out of OS-specific mount code.
3. Implement read-only mount first.
4. Implement:
   - stat
   - readdir
   - open
   - read
   - seek
   - close
5. Validate VLC and image-viewer behavior using:
   - random seeks
   - partial reads
   - thumbnail access patterns
6. Add read-ahead and chunk cache tuning.
7. Only then move to write support.
8. After write support, add rename/delete/create semantics carefully.

### Phase 6: Smart caching and hydration

1. Start with encrypted chunk cache.
2. Add in-memory plaintext buffering for active reads.
3. Add optional temp hydration for problematic apps.
4. Define eviction policies.
5. Add cache metrics:
   - hit rate
   - bytes fetched
   - decrypt cost
   - read amplification
6. Tune for media playback and image browsing.

### Phase 7: Self-host architecture

1. Define hosted and self-host product boundaries explicitly.
2. Ensure protocol/API compatibility for both modes where possible.
3. Keep self-host deployability realistic:
   - sane setup
   - docs
   - upgrade path
   - observability
4. Avoid building self-host as an afterthought.

## What "Explode" Actually Means

This product can win only if the fundamentals feel better than competitors.

Success conditions:

- unlock is fast and reliable
- file access feels local
- video seeking works
- images open normally
- sharing is simple
- self-host is credible
- encryption trust story is clear
- desktop UX is clean

Failure conditions:

- flaky mounts
- slow random reads
- brittle cache behavior
- awkward unlock model
- too much plaintext residue on disk
- UI coupled too tightly to engine internals
- one OS behaving much worse than the others

## Immediate Next Steps

Do these next, in order:

Priority: High
- Keep `UploadService` file-only.
- Add directory/folder sync and remote folder creation in `SyncService`.
- Manual folder uploads should recurse through `SyncService`, not bypass it through `UploadService`.
- Add persisted vault-session restore so uploads, queue processing, and daemon
  flows do not require unlocking on every command.

1. Create explicit core module boundaries on paper and in code.
2. Write tests for the current vault and sync behavior before major refactors.
3. Port the web-supported decrypt/read pipeline into the native core.
4. Port the web-supported upload/share behavior into the native core.
5. Define the canonical internal file metadata/chunk model.
6. Refactor CLI flows to use the new core primitives.
7. Only after that, start daemon design.

## Rule For The Build

Do not jump to:

- Tauri polish
- daemon complexity
- virtual filesystem integration
- advanced caching

until the native shared core fully supports the same core product behavior the web already supports.

That is the foundation. Everything else sits on top of it.
