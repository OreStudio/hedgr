# Review of hedgr.model and hedgr.store

Scope: projects/hedgr.model and projects/hedgr.store (src, include, tests). Read-only.
Paths are relative to /home/marco/Development/Hedgr/hedgr unless they name OreStudio.
OreStudio reference: /home/marco/Development/OreStudio/ores_dev_solid_dirac (HEAD 3a49daea81; the pinned submodule is 718e142dca).
I checked every finding against the code. Where a finding depends on a caller outside scope, the caller's file:line is cited.

Format per finding: ID | severity | file:line | defect | failure scenario | fix | size

---

## Security and credentials

MS-1 | med | projects/hedgr.model/src/domain/secret.cpp:39-41, 72-90
- Defect: Moving a `secret_string` leaves the password in the moved-from small-string buffer. `wipe()` cleanses only `value.size()` bytes, and after `std::move` libstdc++ sets the source length to 0. So `other.clear()` calls `wipe` on an empty string, which does nothing.
- Scenario: A master password of 15 characters or fewer (SSO) is moved into a job struct or coroutine frame. Every byte after the first stays in the source object's in-object buffer until that memory is reused. The comment at line 74 claims this exact case is handled. The existing move test (tests/secret_tests.cpp:64) uses a 28-character heap string, so it cannot see the defect.
- Fix: Cleanse by capacity, not size: `OPENSSL_cleanse(s.data(), s.capacity())`. Call it in `wipe`, and call it on `other.value_` after the move in both move operations. Add an SSO-length move test that inspects the source bytes, using the same placement-new technique as the destructor test.
- Size: S

MS-2 | med | projects/hedgr.model/include/hedgr.model/command/command.hpp:175 ; projects/hedgr.model/include/hedgr.model/event/event.hpp:196
- Defect: `command::password` (the master and account passwords) and `event::token` (the bearer token) are plain `std::string`. This happens even though the model ships `secret_string` for exactly this purpose.
- Scenario: ADR 4 requires the password buffer to be cleansed with `OPENSSL_cleanse` before it leaves scope. A `command` travels by value through `channel<command>`: it is pushed into a `std::vector` that reallocates, then swapped and drained. Each relocation and destruction frees password bytes uncleansed. No caller can honour the ADR, because the type does not cleanse.
- Fix: Make both fields `domain::secret_string` (with MS-1 fixed first). The runtime then reads `.view()` at the one use site.
- Size: M

MS-3 | med | projects/hedgr.store/include/hedgr.store/repository/entity.hpp:62-122 ; docs/adr/0004-stored-credentials.md ; CONTEXT.md (Master password)
- Defect: Only `account.credential` is sealed. `instance` (nats_url, subject_prefix, TLS key and cert paths), `folder`, `label` and `account.principal` are stored as plaintext columns. CONTEXT.md and ADR 4 say the master password "protects the whole local store: every instance, every folder and every label". In fact that gate exists only in the interface.
- Scenario: Someone copies `hedgr.db` (ADR 3 notes that operators copy it between machines). The copy reveals every host, broker URL, tier label, principal name and TLS key path without the master password. An operator who trusts the documentation believes the opposite.
- Fix: Either correct ADR 4 and CONTEXT.md to say the catalogue is in the clear and only credentials are encrypted (S), or encrypt the catalogue at rest, for example by sealing the row payloads or using SQLCipher keyed from the derived key (L).
- Size: S (doc) / L (encrypt)

MS-4 | low | projects/hedgr.store/src/crypto/secret_box.cpp:254-267
- Defect: `decrypt` returns early on an `EVP_DecryptUpdate` failure (256) and on a tag failure (267) without cleansing `plaintext`. The comment at 260-266 says the buffer "is wiped before the error is returned".
- Scenario: The ciphertext is tampered with under the correct key. GCM has already written the unauthenticated plaintext, which is the real credential bytes, into the vector. The vector is then freed uncleansed.
- Fix: Call `cleanse(plaintext)` before both early returns, or hold the buffer in a scope guard that always cleanses. Optionally cleanse `key` on the `derive_key` failure path too.
- Size: S

MS-12 | low | projects/hedgr.model/src/state/session_table.cpp:90, 152, 176, 218
- Defect: The bearer token sits in a plain `std::string` column. `remove()` calls `.clear()`, which keeps the bytes in the buffer, and the comment at 89 claims "drop it now". `set_token` and `set_account_result` overwrite the column and free the old token uncleansed. `clear()` frees every token through `columns_ = {}`.
- Scenario: After sign-out or a token refresh (commit d0b0da1 refreshes tokens routinely), the old bearer tokens stay readable in freed heap memory.
- Fix: Make the column `std::vector<domain::secret_string>`, or call `OPENSSL_cleanse` explicitly before clear and overwrite. Pair this with MS-2 for `event::token`.
- Size: S

## Store correctness and boundaries

MS-8 | med | projects/hedgr.store/src/repository/window_repository.cpp:134 (also folder_repository.cpp:102, label_repository.cpp:106, shortcut_repository.cpp:101)
- Defect: One row that fails to map aborts the entire `list()`.
- Scenario: One `window_spec` has an unparseable `state_json` or a kind this build dropped. Restore then gets an error instead of N-1 windows, and the whole window wall is lost at startup. A single bad label row hides every label in the same way.
- Fix: Return the good rows plus a list of per-row `corrupt_row` errors (or log and skip), so one row cannot take the wall down.
- Size: S

MS-9 | low (latent) | projects/hedgr.model/src/domain/window_json_io.cpp:44-45
- Defect: `rfl::json::read<Shape>` is called with no `rfl::DefaultIfMissing`, so a field missing from an older row is a parse error. No schema version covers the JSON shapes.
- Scenario: Someone adds a non-optional field to `chart_state`, for example a span. Every saved chart row then fails to parse, and through MS-8 every saved window is lost. No migration notices, because `current_version` only tracks table shapes.
- Fix: Read with `rfl::json::read<Shape, rfl::DefaultIfMissing>`, so the member initialisers fill missing fields. Add a test that parses `{}` for every kind.
- Size: S

MS-16 | low | projects/hedgr.store/src/repository/master_repository.cpp:111-138 (comment at 49-52)
- Defect: `create` checks with `has_master()` and then writes with `upsert(... {"id"})`, which emits `ON CONFLICT(id) DO UPDATE` (confirmed in libsqlgen strings). A second create therefore replaces the row. The comment says the literal id makes a second create "collide with the first".
- Scenario: Two Hedgr processes open the same store (WAL allows it) and both run first-launch create. The second silently replaces the verification blob, so credentials sealed under the first password no longer match the stored verifier.
- Fix: Use a plain `sqlgen::insert`, so the primary key refuses the second create and the check-then-act race goes away.
- Size: S

MS-17 | low | projects/hedgr.store/src/repository/account_repository.cpp:128-154
- Defect: `set_credential` and `clear_credential` are UPDATEs that report success when no row matches.
- Scenario: An account is removed while a "remember" seal runs on the pool. The UPDATE matches zero rows and returns success, so the runtime reports the credential as stored when nothing was written.
- Fix: Check the affected-row count, or read the row back, and return `not_found` when it is 0.
- Size: S

## Model state, handles and ADR 2

MS-10 | low-med | projects/hedgr.model/src/state/session_table.cpp:114-216 ; projects/hedgr.model/src/state/window_table.cpp:85-140
- Defect: Every hot-table accessor (`status`, `instance_id`, `token`, `state`, `kind`, `is_open`, setters) indexes `columns_[h.index]` with no generation check. An invalid handle (index 0xFFFFFFFF) is out-of-bounds UB. ADR 2 says "a stale handle compares unequal to the new occupant instead of silently addressing it", but only `contains()` honours that.
- Scenario: projects/hedgr.ui/src/render/chart_window.cpp:275-277 calls `input.sessions.instance_id(*session)` on `windows.session_of()` without `sessions.contains()`. If a session slot is released and reused while a window is still bound, the chart reads the new occupant's account. That is the cross-account read market_table's doc forbids. Today this is safe only because the runtime calls `clear_session` before every `sessions_.remove` (instance_manager.cpp:1799, 1898, 2851).
- Fix: Make `session_of` return a handle only when the session is live, by taking `const session_table&`. Add a debug assert of `owns(layout_, h)` in every accessor.
- Size: M

MS-11 | low | projects/hedgr.model/src/state/session_table.cpp:80 ; projects/hedgr.model/src/state/window_table.cpp:62
- Defect: `insert` allocates a row and then calls `by_key_.emplace` / `by_id_.emplace`, which do nothing when the key already exists. The new row is orphaned.
- Scenario: Two windows are inserted with the same id, for example a restore that races a user open. `find()` returns the first. Removing the first erases the map entry, and the second row stays live, is drawn every frame, and cannot be found or removed.
- Fix: Look the key up first and return the existing handle, or assert that it is unique.
- Size: S

MS-19 | low | projects/hedgr.model/src/state/window_table.cpp:93-97 ; projects/hedgr.model/include/hedgr.model/state/tables.hpp (`mutable std::string focus_request_`)
- Defect: `take_focus_request() const` changes state through a `mutable` member. It is called from projects/hedgr.ui/src/render/workspace.cpp:251.
- Scenario: The UI consumes model state through a const reference, which breaks "ui is a pure function of state". Two renders in one frame, or a renderer that early-outs before the call, lose or keep the request depending on call order.
- Fix: Have the runtime clear the request after the frame (an event or command `focus_consumed`), or make the UI read it and the runtime reset it. Make the method non-const so the write shows.
- Size: S

## Market pipeline (model half)

MS-5 | med | projects/hedgr.model/src/state/market_table.cpp:54-69, 167, 184, 203 ; crm_table.cpp:50-75 ; term_structure_table.cpp:43-63 ; tenant_image_map.cpp:26-44
- Defect: `market_table`, `crm_table`, `term_structure_table` and `tenant_image_map` only ever insert. There is no per-session or per-series removal, and the runtime clears them only in `stop()` (projects/hedgr.runtime/src/manager/instance_manager.cpp:716-723).
- Scenario: An operator opens and closes charts across many series and accounts during a trading day. Each series keeps up to 10,800 `market_point`s. A point is 48 bytes of scalars plus a 32-byte `std::string term` (market.hpp:153), so roughly 0.8 MB per series against the plan's 170 KB. That is about 400 MB at the plan's 512-series cap per session, and it is never released. Data from signed-out sessions also stays readable in memory.
- Fix: Add `forget_session(instance, account)` and `forget_series(...)` to each table. Call them on unwatch and session removal. Drop or intern `term` in `market_point`.
- Size: M

MS-6 | med | projects/hedgr.model/src/state/market_table.cpp (points/catalogue), crm_table.cpp (get), term_structure_table.cpp (get), tenant_image_map.cpp (get/image_for), image_cache.cpp:32
- Defect: Every lookup builds a composite `std::string` key: instance + account + series UUIDs joined by `\x1f`. That is about 110 bytes, well past SSO, so every lookup heap-allocates.
- Scenario: chart_window.cpp:276 calls `market.points(...)` per chart per frame. The CRM, term-structure and series-browser windows do the same. With hundreds of windows, that is hundreds of malloc/free pairs per frame on the frame thread.
- Fix: Use a struct key with a transparent hash and `std::string_view` heterogeneous lookup, or resolve the key to a stable index once per window and cache it in the window's UI state.
- Size: M

MS-7 | low-med | projects/hedgr.model/src/state/market_table.cpp:100-106, 128-145
- Defect: `bucketize` stamps each bucket with the observation time that won it, not the interval start. `merge_level` then deduplicates by exact `at_ns`. A backfilled bucket and a live bucket for the same interval almost never share a timestamp, so both survive.
- Scenario: A backfill arrives after the subscription has delivered the 10:00:00 1s bucket (last tick 10:00:00.9). The backfill's last observation in that second was at 10:00:00.2. The fine ring then holds two points for one second, and the medium and coarse rings likewise. Candles double up and the EMA is skewed. The plan's "live wins" rule does not hold. No test covers same-interval overlap.
- Fix: Key the merge map by `at_ns / market_level_ns(level)`, with live entries overwriting seeded ones. Add a test with one backfilled and one live point in the same interval.
- Size: S

MS-13 | low-med | projects/hedgr.model/src/state/bucket_aggregator.cpp:51-60
- Defect: A bucket is published only when a later tick crosses its interval, and there is no flush.
- Scenario: A series that ticks every few minutes shows nothing for its newest tick until the next tick arrives. The chart header's "live" or arrival age reflects the previous closed bucket, so a quiet market reads as stale data. The newest coarse bucket is invisible for up to an hour.
- Fix: Also publish the in-progress bucket, as an update the table applies with `append_level`'s equal-timestamp replace. Alternatively, have the stream flush on a timer once an interval has elapsed.
- Size: S-M

MS-14 | low | projects/hedgr.model/src/state/bucket_aggregator.cpp:48-49
- Defect: Late ticks are dropped with no counter. The plan (Risks, Late ticks) says they are "dropped and counted".
- Scenario: A clock skew or a reordered feed silently thins the chart, and no diagnostic shows why.
- Fix: Add a `late_` counter, exposed beside `ticks()`.
- Size: S

MS-15 | low | projects/hedgr.model/src/state/bucket_aggregator.cpp:53-56 ; projects/hedgr.model/src/state/bucket_ring.cpp:32, 47
- Defect: Each push copies `series_id_`, a 36-character UUID that does not fit in SSO. The consumer moves the string out of the slot, so the producer's next move-assign into that slot cannot reuse a buffer either. That is one malloc per closed bucket on the NATS dispatch thread.
- Scenario: The plan says the ring is preallocated and "the producer never sleeps, yields or blocks". A malloc under contention can take a lock.
- Fix: Carry a fixed `std::array<char, 36>`, or a watch index plus generation, instead of a `std::string`. Alternatively, have the consumer copy rather than move so the slot keeps its capacity.
- Size: S

MS-18 | low | projects/hedgr.model/include/hedgr.model/command/channel.hpp:58-60
- Defect: `drain()` swaps in a fresh empty vector each time, so `buffer_` loses its capacity and regrows from zero after every drain.
- Scenario: Per-frame reallocation churn on both the event and the command channel, under the lock that the writer contends for.
- Fix: Use `drain(std::vector<T>& out)` with `out.clear(); std::swap(out, buffer_);`, so the two buffers ping-pong with their capacity kept.
- Size: S

## Tests and dead weight

MS-20 | low | projects/hedgr.store/tests/secret_store_tests.cpp:400
- Defect: The test is named "the schema this build writes is version 6", but `current_version` is 8 (schema.cpp:46).
- Scenario: A reader trusts the name. The body compares against `schema_version()`, so it passes for any version.
- Fix: Rename it to "the schema version this build writes is the one stored".
- Size: S

MS-21 | low | projects/hedgr.store/include/hedgr.store/repository/query.hpp:90-92 ; entity.hpp:127-131
- Defect: There is no test that `account_repository::save` keeps an existing `credential`. That behaviour is load-bearing: it rests on `upsert` emitting `ON CONFLICT DO UPDATE` over only the entity's columns, not `INSERT OR REPLACE`.
- Scenario: A sqlgen upgrade or a switch to `insert_or_replace` turns the save into REPLACE. Every account edit then silently wipes its stored credential, and `ON DELETE CASCADE` drops its `account_label` rows. No test fails.
- Fix: Add a test that calls `set_credential`, then `save` with an edited name, then asserts `credential_of` is unchanged and the labels survive.
- Size: S

## OreStudio reuse and consistency (scope addition)

MS-22 | low | projects/hedgr.store/src/crypto/secret_box.cpp:100-165
- Defect: Base64 encode and decode are hand-rolled, but `ores::utility::convert::base64_converter` provides them (/home/marco/Development/OreStudio/ores_dev_solid_dirac/projects/ores.utility/include/ores.utility/convert/base64_converter.hpp).
- Scenario: Two base64 implementations exist for one byte format that ADR 6 says must stay compatible with OreStudio. They can diverge on padding or whitespace handling. Hedgr's decoder is stricter, and the ores one uses a BIO and throws.
- Fix: Call `base64_converter` and wrap its exceptions into `box_error` at this boundary. Keep the strict alphabet and padding pre-check if it is wanted. ADR 6's "one file" seam is unaffected.
- Size: S

MS-23 | low | projects/hedgr.model/include/hedgr.model/domain/password_policy.hpp ; projects/hedgr.model/src/domain/password_policy.cpp:59-98
- Defect: `domain::password_policy` duplicates `ores::security::validation::password_policy` (/home/marco/Development/OreStudio/ores_dev_solid_dirac/projects/ores.security/include/ores.security/validation/password_validator.hpp) and the wire type `get_password_policy_response` (projects/ores.iam/api/include/ores.iam.api/messaging/password_policy_protocol.hpp:44). `assess_password` also re-implements the per-rule checks of `password_validator::validate` (projects/ores.security/src/validation/password_validator.cpp).
- Scenario: The rules can drift. One divergence exists today: the server tests specials with `std::strchr(special_chars.c_str(), c)`, which also matches `'\0'`, while Hedgr uses `find`, which does not. `min_length` is `int` in Hedgr and `size_t` in ores.security. If the server's rule changes, Hedgr's checklist approves passwords the server refuses.
- Fix: Use `ores::security::validation::password_policy` as the model's policy type. Ideally get a `validate(password, policy)` overload upstream, because the current `validate` only checks the hard-coded `policy()`. Until then, keep Hedgr's per-rule checklist, which is UI-only, and add a test against the server's sample policy.
- Size: S-M

Checked and not a finding:
- AES-256-GCM / PBKDF2 key derivation. No ores.* library in solid_dirac provides it. ores.security has only scrypt `password_hasher`, which is a verifier format and not a KDF for encryption, so ADR 6 still holds.
- `ores.utility/domain/hierarchy.hpp` (`build_tree`) is UUID-typed and only builds trees. It does not cover `folder_table`'s string ids or its cycle refusal.
