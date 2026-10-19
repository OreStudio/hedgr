/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "hedgr.runtime/seed/seeder.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include "ores.logging/boost_severity.hpp"
#include "ores.logging/make_logger.hpp"
#include <algorithm>
#include <boost/log/sources/severity_feature.hpp>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hedgr::runtime::seed {

namespace {

namespace domain = hedgr::model::domain;
namespace model_state = hedgr::model::state;
namespace sqlite = hedgr::store::sqlite;
namespace transport = hedgr::session::transport;

using condition_source = model_state::condition_source;

/**
 * @brief This manager's logger.
 *
 * A warning that nothing reads is not a warning, so a checkout the scan could
 * not use is reported the way the rest of OreStudio reports one: through the
 * process log, which the application sets up before it starts anything.
 */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.runtime.manager.instance_manager");
    return instance;
}

/** @brief The live slot index of every row of a table, holes skipped. */
[[nodiscard]] std::vector<std::uint32_t>
live_indices(const model_state::slab& layout) {
    std::vector<std::uint32_t> result;
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        result.push_back(index);
    }
    return result;
}

/**
 * @brief The current UTC instant as ISO-8601, for a creation timestamp.
 *
 * Formatted by hand rather than through std::format's chrono support, which
 * still varies by standard library: a timestamp column that writes nothing on
 * one toolchain and a date on another is worse than four lines here.
 */
[[nodiscard]] std::string iso8601_now() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_r(&now, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

/** @brief A store failure as one sentence naming the operation that failed. */
[[nodiscard]] std::string store_failure(std::string_view what,
    const sqlite::store_error& error) {
    const auto message = std::format("{}: {} ({})", what, error.message,
        sqlite::to_string(error.code));
    BOOST_LOG_SEV(lg(), ores::logging::error) << "Store failure: " << message;
    return message;
}

/**
 * @brief The directories an install keeps its instance illustrations in.
 *
 * The runtime and the interface are separate components, so the runtime cannot
 * ask the interface where the pictures are and has to know the install's own
 * layout instead. The running executable is the anchor both use, because where
 * the binary sits is the one thing a deployed bundle and a run out of the build
 * tree agree on; the source tree's own assets directory is the last candidate,
 * for a run with no publish directory beside it. The override exists so a test
 * can point the backfill at a fixture rather than at whatever is installed.
 */
[[nodiscard]] std::vector<std::filesystem::path> instance_icon_directories() {
    std::vector<std::filesystem::path> result;
    if (const char* override_directory = std::getenv("HEDGR_INSTANCE_ICONS_DIR");
        override_directory != nullptr && *override_directory != '\0')
        result.emplace_back(override_directory);

    std::error_code failure;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", failure);
    if (!failure && !self.empty()) {
        result.push_back(self.parent_path().parent_path() / "share" /
            "hedgr.ui" / "instances");
        result.push_back(self.parent_path() / "instances");
    }
    result.emplace_back("assets/instances");
    return result;
}

/**
 * @brief Whether the install ships an illustration under @p key.
 *
 * The guard is the file rather than the shape of the key, because a key that
 * names nothing would only trade one blank entry for another, and the backfill
 * is worth a write only when there is a picture the interface will actually
 * draw. The key and its asset are related by the extension alone, which is the
 * relation the loader already relies on when it takes a file's stem as the key.
 * An install with no illustration directory answers false for every key, which
 * leaves the row as the migration left it rather than naming an asset nobody
 * has.
 */
[[nodiscard]] bool instance_icon_asset_exists(std::string_view key) {
    if (key.empty())
        return false;

    const std::string file = std::format("{}.png", key);
    for (const auto& directory : instance_icon_directories()) {
        std::error_code failure;
        if (std::filesystem::exists(directory / file, failure) && !failure)
            return true;
    }
    return false;
}

/**
 * @brief The label an environment-seeded instance carries in its id, or empty.
 *
 * Only the seeder names a row this way. @c seed_first_run derives the id of a
 * row it read from a checkout as @c inst-env-<label>, where the label is the
 * checkout's own label; an instance an operator typed is named from the clock
 * as @c inst-<nanoseconds>, which never carries the prefix. The prefix is
 * therefore the whole of the proof that a row came from the seeder, and a
 * hand-typed row, whatever its name, cannot match it.
 */
[[nodiscard]] std::string seeded_label_of(std::string_view id) {
    constexpr std::string_view seeded_prefix = "inst-env-";
    if (!id.starts_with(seeded_prefix))
        return {};
    return std::string{id.substr(seeded_prefix.size())};
}

/**
 * @brief The stable id of the folder one instance's seeded logins sit in.
 *
 * Derived from the instance the way the seeded accounts' keys are, so a second
 * seeding writes the same folder over itself rather than a second one beside
 * it. A constant id would put every instance's folder in one row, and the
 * schema has no rule that a child folder's name is unique among its owner's
 * siblings, so the derivation is what holds the two seeds to one folder.
 */
[[nodiscard]] std::string
privileged_folder_id_for(std::string_view instance_id) {
    return std::format("folder-privileged:{}", instance_id);
}

/**
 * @brief One OreStudio checkout, as far as seeding needs to know about it.
 *
 * Read out of the checkout's @c .env, which is the file the deployment itself
 * uses. Nothing here is invented: the address and the subject prefix are the
 * ones the environment's own services listen on, taken from the same file the
 * rest of OreStudio reads.
 */
struct environment final {
    /**
     * @brief The checkout's label, e.g. @c brave_hopper.
     *
     * From @c ORES_CHECKOUT_LABEL, which is the name of the checkout rather
     * than of the deployment: @c ORES_ENV_NAME disagrees with the directory for
     * at least one environment (it holds @c festive-dijkstra where the label
     * holds @c festive_dijkstra), and the tree the operator described is built
     * from labels.
     */
    std::string label;

    /** @brief @c ORES_NATS_URL: the broker this deployment's services use. */
    std::string nats_url;

    /** @brief @c ORES_NATS_SUBJECT_PREFIX: where its services listen. */
    std::string subject_prefix;

    /** @brief @c ORES_NATS_WIRE_FORMAT, or @c msgpack when it says nothing. */
    std::string wire_format = "msgpack";

    /**
     * @brief The deployment's TLS material, as its own @c .env names it.
     *
     * Read here so that the seeded row carries it: the paths are what makes a
     * store created on this host open a session on it with nothing further to
     * fill in, and they are the deployment's own answer rather than one this
     * process had to be told twice.
     */
    std::string tls_ca;
    std::string tls_cert;
    std::string tls_key;

    /** @brief @c ORES_ENV_TYPE: the tier, e.g. @c development. */
    std::string type;
};

/**
 * @brief The directory the OreStudio checkouts live under.
 *
 * Read from @c HEDGR_ORESTUDIO_ROOT when it is set, which is what a test does
 * to point the scan at a fixture instead of at the operator's machine. An empty
 * value means no checkouts, deliberately: it is how a run asks for the single
 * seed from its own options.
 */
[[nodiscard]] std::filesystem::path environments_root() {
    if (const char* root = std::getenv("HEDGR_ORESTUDIO_ROOT"); root != nullptr)
        return std::filesystem::path{root};
#ifdef HEDGR_ORESTUDIO_ROOT
    return std::filesystem::path{HEDGR_ORESTUDIO_ROOT};
#else
    return {};
#endif
}

/** @brief The value of @p line after the @c =, quotes and padding removed. */
[[nodiscard]] std::string env_value_of(std::string_view line) {
    const auto equals = line.find('=');
    if (equals == std::string_view::npos)
        return {};
    std::string value{line.substr(equals + 1)};

    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
                              value.back() == ' '))
        value.pop_back();
    std::size_t start = 0;
    while (start < value.size() && value[start] == ' ')
        ++start;
    value = value.substr(start);

    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

/** @brief The key of a @c KEY=VALUE line, or empty for a comment or blank. */
[[nodiscard]] std::string env_key_of(std::string_view line) {
    std::string_view view{line};
    while (!view.empty() && (view.front() == ' ' || view.front() == '\t'))
        view.remove_prefix(1);
    if (view.empty() || view.front() == '#')
        return {};
    const auto equals = view.find('=');
    if (equals == std::string_view::npos)
        return {};
    std::string key{view.substr(0, equals)};
    while (!key.empty() && key.back() == ' ')
        key.pop_back();
    // `export FOO=bar` is how some of these files are written by hand.
    constexpr std::string_view export_prefix = "export ";
    if (key.starts_with(export_prefix))
        key = key.substr(export_prefix.size());
    return key;
}

/**
 * @brief Reads one checkout's environment.
 *
 * @return The environment, or nothing when the checkout has to be skipped. A
 * missing or unreadable @c .env is a warning and never a failure: the seeding
 * is a convenience, and a store that will not start because one checkout beside
 * it is half-built would be worse than one that starts a row short.
 */
[[nodiscard]] std::optional<environment> read_environment(
    const std::filesystem::path& checkout) {
    const auto file = checkout / ".env";
    std::ifstream input(file);
    if (!input) {
        BOOST_LOG_SEV(lg(), ores::logging::warn) << "Skipping the environment at " << checkout
            << ": its .env cannot be read.";
        return std::nullopt;
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        auto key = env_key_of(line);
        /*
         * Both spellings are kept, because ORES_SHELL_* and ORES_NATS_* name
         * the same broker and only one of them is guaranteed to exist. The
         * unprefixed name wins when it is there.
         */
        if (!key.empty())
            values[std::move(key)] = env_value_of(line);
    }

    const auto lookup = [&values](std::string_view key) -> std::string {
        const auto found = values.find(std::string{key});
        return found == values.end() ? std::string{} : found->second;
    };

    environment result;
    result.label = lookup("ORES_CHECKOUT_LABEL");
    if (result.label.empty())
        result.label = checkout.filename().string();
    constexpr std::string_view checkout_prefix = "ores_dev_";
    if (result.label.starts_with(checkout_prefix))
        result.label = result.label.substr(checkout_prefix.size());

    result.nats_url = lookup("ORES_NATS_URL");
    result.subject_prefix = lookup("ORES_NATS_SUBJECT_PREFIX");
    result.tls_ca = lookup("ORES_NATS_TLS_CA");
    result.tls_cert = lookup("ORES_NATS_TLS_CERT");
    result.tls_key = lookup("ORES_NATS_TLS_KEY");
    result.type = lookup("ORES_ENV_TYPE");
    if (const auto wire_format = lookup("ORES_NATS_WIRE_FORMAT");
        !wire_format.empty())
        result.wire_format = wire_format;

    if (result.nats_url.empty() || result.subject_prefix.empty()) {
        BOOST_LOG_SEV(lg(), ores::logging::warn) << "Skipping the environment at " << checkout
            << ": its .env states no ORES_NATS_URL or no "
               "ORES_NATS_SUBJECT_PREFIX.";
        return std::nullopt;
    }
    return result;
}

/**
 * @brief Every OreStudio checkout under @p root, in a stable order.
 *
 * A checkout is a directory named @c ores_dev_* that holds a @c .env. Anything
 * else under the root is not an environment and is not mentioned.
 */
[[nodiscard]] std::vector<environment> scan_environments(
    const std::filesystem::path& root) {
    std::error_code failure;
    if (!std::filesystem::is_directory(root, failure)) {
        BOOST_LOG_SEV(lg(), ores::logging::info) << "No OreStudio checkouts at " << root
            << "; seeding the deployment this process was pointed at instead.";
        return {};
    }

    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(root, failure)) {
        if (!entry.is_directory(failure))
            continue;
        const auto name = entry.path().filename().string();
        constexpr std::string_view prefix = "ores_dev_";
        if (!name.starts_with(prefix))
            continue;
        candidates.push_back(entry.path());
    }
    if (failure) {
        BOOST_LOG_SEV(lg(), ores::logging::warn) << "Cannot list the OreStudio checkouts at "
            << root << ": " << failure.message();
        return {};
    }
    // Sorted so the seeded ids and the order rows are written in do not depend
    // on what the filesystem happens to return first.
    std::ranges::sort(candidates);

    std::vector<environment> found;
    for (const auto& checkout : candidates) {
        if (auto environment = read_environment(checkout))
            found.push_back(std::move(*environment));
    }
    return found;
}

/**
 * @brief @c brave_hopper as @c Brave @c Hopper reads in a tree.
 *
 * Underscores and hyphens become single spaces and each word is capitalised, so
 * a label that is a name in the store reads as a name on the screen.
 */
[[nodiscard]] std::string display_name(std::string_view label) {
    std::string result;
    result.reserve(label.size());
    bool at_start_of_word = true;
    for (const char character : label) {
        if (character == '_' || character == '-' || character == ' ') {
            if (!result.empty() && result.back() != ' ')
                result.push_back(' ');
            at_start_of_word = true;
            continue;
        }
        if (at_start_of_word) {
            result.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
            at_start_of_word = false;
            continue;
        }
        result.push_back(character);
    }
    return result;
}

/**
 * @brief The label every seeded development environment carries.
 *
 * One name for the tier rather than one per checkout: the label says what kind
 * of deployment this is and the instance's own name says which one it is. The
 * severity is `none` and the colour is blue, and ensure_development_label says
 * why.
 */
constexpr std::string_view development_label_name = "development";

/**
 * @brief One start's seeding, writing into the manager's tables.
 *
 * Holds the tables and repositories under the names the manager gives them, so
 * the bodies read as they did there, and keeps every refusal instead of
 * raising it.
 */
class seeder final {
public:
    seeder(const tables& state, const repositories& stores,
        transport::session_options seed)
        : instances_(state.instances), accounts_(state.accounts),
          folders_(state.folders), labels_(state.labels),
          labellings_(state.labellings), instance_repo_(stores.instances),
          account_repo_(stores.accounts), folder_repo_(stores.folders),
          label_repo_(stores.labels), labelling_repo_(stores.labellings),
          seed_options_(std::move(seed)) {}

    void seed_first_run();

    void backfill_seeded_icon_keys();

    /**
     * @brief Makes sure the folder the seeded environments sit in exists.
     *
     * @return The folder's id, which is a constant rather than a generated key:
     * the seeder runs once per store, and the folder is a fact about the
     * environments it seeds rather than a row an operator named.
     */
    [[nodiscard]] std::string ensure_development_folder();

    /**
     * @brief Makes sure the label the seeded environments carry exists.
     *
     * @return The label's id, which is the id of an existing label of the same
     * name when the operator already has one: a label's name is unique, so a
     * second row carrying that name would be refused and the seeded instances
     * would come up unlabelled.
     */
    [[nodiscard]] std::string ensure_development_label();

    /**
     * @brief Creates the folders a tree is anchored by, when they are missing.
     *
     * Idempotent, and run on every start: an instance added by a build that did
     * not create accounts roots still gets one, which is what stops a start
     * from depending on the order the store was written in.
     */
    void ensure_roots();

    /**
     * @brief Makes sure the folder the seeded logins sit in exists.
     *
     * @return The folder's id, which is derived from the instance id rather
     * than generated. The seeder runs on every start, and the derived key is
     * what makes a second pass write the same folder rather than a second one
     * beside it.
     */
    [[nodiscard]] std::string ensure_privileged_folder(
        const std::string& instance_id);

    /**
     * @brief Gives every stored instance the two logins it is seeded with.
     *
     * Run on every start rather than only on the first, because the walk has to
     * reach an instance this build never seeded as well as one it just made,
     * and because a row that is already there is replaced under the same
     * derived key rather than added to.
     */
    void seed_instance_accounts();

    /**
     * @brief Writes the two logins of one instance, a super admin and a tenant
     * admin.
     *
     * The principals are built from the instance's subject prefix, so they name
     * the deployment IAM is being asked about; the rows land in that instance's
     * accounts root.
     */
    void seed_accounts_for_instance(const model::domain::instance& instance);


    [[nodiscard]] std::vector<refusal> take_refusals() {
        return std::move(refusals_);
    }

private:
    void raise(model_state::condition_key key, std::string message) {
        refusals_.push_back({std::move(key), std::move(message)});
    }

    model_state::instance_table& instances_;
    model_state::account_table& accounts_;
    model_state::folder_table& folders_;
    model_state::label_table& labels_;
    model_state::labelling_table& labellings_;
    store::repository::instance_repository* instance_repo_;
    store::repository::account_repository* account_repo_;
    store::repository::folder_repository* folder_repo_;
    store::repository::label_repository* label_repo_;
    store::repository::labelling_repository* labelling_repo_;
    transport::session_options seed_options_;
    std::vector<refusal> refusals_;
};

}

void seeder::backfill_seeded_icon_keys() {
    const auto& layout = instances_.layout();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;

        const model_state::instance_handle handle{index, layout.generation[index]};
        auto* row = instances_.get(handle);
        if (row == nullptr || !row->icon_key.empty())
            continue;

        const std::string label = seeded_label_of(row->id);
        if (label.empty() || !instance_icon_asset_exists(label))
            continue;

        /*
         * The row is changed in memory and then written, so the frame that
         * follows this start draws the picture rather than waiting for a
         * restart to read it back. The write is the repository's own, exactly
         * as a form's save is, so a refusal is reported the one way every other
         * refusal is.
         */
        row->icon_key = label;
        if (auto saved = instance_repo_->save(*row); !saved) {
            raise({condition_source::instance, row->id},
                store_failure(std::format(
                    "cannot restore the illustration of the seeded instance {}",
                    row->id), saved.error()));
        } else {
            /*
             * A row is only worth a line when it changed, which is what makes
             * the log say which store was repaired rather than repeating the
             * same one on every start. The key is not a secret; it is the
             * environment's own label.
             */
            BOOST_LOG_SEV(lg(), ores::logging::info)
                << "Restored the illustration key " << row->icon_key
                << " of the seeded instance " << row->id << ".";
        }
    }
}

void seeder::seed_first_run() {
    if (instances_.size() != 0)
        return;

    const auto root = environments_root();
    const auto environments =
        root.empty() ? std::vector<environment>{} : scan_environments(root);

    /*
     * A machine with no checklist of environments -- a deployed package, or a
     * test that points the scan at nowhere -- still gets one instance, the one
     * the process's own options describe. That is the seed this class has
     * always had, and it stays the degenerate case rather than a special one:
     * the store always comes up with something to look at.
     */
    if (environments.empty()) {
        /*
         * The id is derived from the clock rather than random: this runs once,
         * on a store with no instances in it, and a timestamp says plainly
         * where the row came from. A uuid would imply a distributed uniqueness
         * requirement that does not exist here.
         */
        const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
                               .count();

        domain::instance row;
        row.id = std::format("inst-{}", stamp);
        row.name = "local development";
        row.nats_url = seed_options_.nats_url;
        row.subject_prefix = seed_options_.subject_prefix;
        row.wire_format = seed_options_.wire_format;
        /*
         * The material this process was started with is written into the row
         * rather than left to the fallback it would have reached anyway. The
         * store is the thing that has to keep working, and a path recorded once
         * is a path the operator can see, correct and copy to the next host;
         * one that only ever existed in this process's environment is not.
         */
        row.tls_ca = seed_options_.tls_ca;
        row.tls_cert = seed_options_.tls_cert;
        row.tls_key = seed_options_.tls_key;
        row.folder_id = std::string{domain::instances_root_id};
        row.created_at = iso8601_now();

        static_cast<void>(instances_.upsert(row));

        if (auto saved = instance_repo_->save(row); !saved) {
            raise({condition_source::instance, row.id},
                store_failure(std::format(
                    "cannot save the seeded instance {}", row.id), saved.error()));
        }
        return;
    }

    /*
     * Every checkout the scan found is a development deployment, so they all go
     * in one folder rather than at the root: Instances / Development / Brave
     * Hopper is the tree an operator described, and a flat list often leaves
     * them asking which of twenty entries is the one they care about.
     */
    const std::string folder_id = ensure_development_folder();
    const std::string label_id = ensure_development_label();

    for (const auto& environment : environments) {
        domain::instance row;
        /*
         * The id is derived from the checkout's label rather than the clock,
         * and that is the whole of the idempotency: seeding a second time
         * writes the same row over itself instead of adding a twin. The outer
         * guard on an empty store is what makes this a first-run step; this is
         * what makes it safe if the guard were ever wrong.
         */
        row.id = std::format("inst-env-{}", environment.label);
        row.name = display_name(environment.label);
        /*
         * The checkout's label is the illustration's key. It is the same word
         * the environment is known by everywhere else, so the interface can
         * find the asset by name and the store never carries the picture.
         */
        row.icon_key = environment.label;
        row.nats_url = environment.nats_url;
        row.subject_prefix = environment.subject_prefix;
        row.wire_format = environment.wire_format;
        /*
         * The checkout's own certificates, because they are what its broker
         * expects and what the operator has already provisioned there. A store
         * created on this host therefore works on this host with nothing typed
         * in, which is the whole point of seeding from the environment at all.
         */
        row.tls_ca = environment.tls_ca;
        row.tls_cert = environment.tls_cert;
        row.tls_key = environment.tls_key;
        row.folder_id = folder_id;
        row.created_at = iso8601_now();

        if (instances_.find(row.id))
            continue;

        static_cast<void>(instances_.upsert(row));

        if (auto saved = instance_repo_->save(row); !saved) {
            raise({condition_source::instance, row.id},
                store_failure(std::format(
                    "cannot save the seeded instance {}", row.id), saved.error()));
            continue;
        }

        domain::labelling attachment;
        attachment.owner = domain::labelling_owner::instance;
        attachment.owner_id = row.id;
        attachment.label_id = label_id;
        labellings_.add(attachment.owner, attachment.owner_id,
            attachment.label_id);
        if (auto saved = labelling_repo_->save(attachment); !saved) {
            raise({condition_source::labelling, row.id},
                store_failure(std::format(
                    "cannot label the seeded instance {}", row.id), saved.error()));
        }
    }
}

std::string seeder::ensure_development_folder() {
    /*
     * A constant id rather than a generated one, so a second seeding writes the
     * same folder rather than a second one beside it. The partial unique index
     * over (tree, owner_id) where there is no parent would refuse a second root
     * anyway; this refuses it earlier and with a name a person recognises.
     */
    const std::string id = "folder-development";
    if (folders_.find(id))
        return id;

    domain::folder row;
    row.id = id;
    row.parent_id = std::string{domain::instances_root_id};
    row.owner_id.clear();
    row.name = "Development";
    row.tree = domain::folder_tree::instances;
    static_cast<void>(folders_.upsert(row));

    if (folder_repo_) {
        if (auto saved = folder_repo_->save(row); !saved) {
            raise({condition_source::folder, id},
                store_failure(
                    std::format("cannot save the Development folder {}", id),
                    saved.error()));
        }
    }
    return id;
}

std::string seeder::ensure_development_label() {
    /*
     * An operator's own label of this name wins, because a label's name is
     * unique and the seeded instances should carry the label they already have
     * rather than one the seeder could not write.
     */
    if (const auto existing = labels_.find_by_name(development_label_name))
        return labels_.get(*existing)->id;

    domain::label row;
    row.id = "label-development";
    row.name = std::string{development_label_name};
    /*
     * Blue and no severity. Blue is the palette's "informational" entry: a
     * development deployment is worth naming in a list, and it is not a
     * warning. The severity is what a destructive-action confirmation reads,
     * and nothing about a development instance makes removing it more dangerous
     * than removing any other row -- `none` is the honest answer, and a colour
     * chosen to look important would be a lie in the one place the interface
     * trusts.
     */
    row.colour = domain::label_colour::blue;
    row.severity = domain::label_severity::none;
    static_cast<void>(labels_.upsert(row));

    if (label_repo_) {
        if (auto saved = label_repo_->save(row); !saved) {
            raise({condition_source::label, row.id},
                store_failure(
                    std::format("cannot save the development label {}", row.id),
                    saved.error()));
        }
    }
    return row.id;
}

void seeder::ensure_roots() {
    /*
     * The instances tree is anchored once and never again. Its root has no
     * owner, because there is exactly one such tree and nothing distinguishes
     * it from another; the partial unique index in the schema is what holds
     * that to one row.
     */
    if (!folders_.find(domain::instances_root_id)) {
        domain::folder root;
        root.id = std::string{domain::instances_root_id};
        root.parent_id.clear();
        root.owner_id.clear();
        root.name = "Instances";
        root.tree = domain::folder_tree::instances;
        static_cast<void>(folders_.upsert(root));

        if (folder_repo_) {
            if (auto saved = folder_repo_->save(root); !saved) {
                raise({condition_source::folder, root.id},
                    store_failure(
                        std::format("cannot save the instances root {}", root.id),
                        saved.error()));
            }
        }
    }

    /*
     * Every instance owns an accounts tree, so this runs over the instances
     * rather than over the folders: an instance stored by a build that did not
     * create roots gets its own here, which is what makes the step idempotent
     * across a version change as well as across a restart.
     */
    for (const auto index : live_indices(instances_.layout())) {
        const auto& row = instances_.rows()[index];
        if (auto refused = ensure_accounts_root(folders_, folder_repo_, row.id))
            raise(std::move(refused->key), std::move(refused->message));
    }
}

std::optional<refusal> ensure_accounts_root(model_state::folder_table& folders,
    store::repository::folder_repository* folder_repo,
    const std::string& instance_id) {
    if (instance_id.empty())
        return std::nullopt;

    const auto id = domain::accounts_root_id_for(instance_id);
    if (folders.find(id))
        return std::nullopt;

    domain::folder root;
    root.id = id;
    root.parent_id.clear();
    root.owner_id = instance_id;
    root.name = "Accounts";
    root.tree = domain::folder_tree::accounts;
    static_cast<void>(folders.upsert(root));

    if (!folder_repo)
        return std::nullopt;
    if (auto saved = folder_repo->save(root); !saved) {
        return refusal{{condition_source::folder, id},
            store_failure(
                std::format("cannot save the accounts root {}", id), saved.error())};
    }
    return std::nullopt;
}

void seeder::seed_instance_accounts() {
    /*
     * The walk is over the instances rather than over a list of the ones just
     * seeded, because the accounts have to cover a store this build did not
     * write as well as one it did. It runs on every start for the same reason:
     * an instance added by a build that seeded no accounts still comes up with
     * them after the next restart, and a second pass over a row that is already
     * there replaces it rather than adding a twin.
     */
    for (const auto index : live_indices(instances_.layout()))
        seed_accounts_for_instance(instances_.rows()[index]);
}

std::string seeder::ensure_privileged_folder(
    const std::string& instance_id) {
    /*
     * The id is a function of the instance rather than a constant, because
     * every instance owns its own accounts tree and so its own Privileged
     * folder. Deriving it is the whole of the idempotency, because a second
     * seeding writes the same folder over itself, and the folder is found
     * before it is made, so the common path is a lookup rather than a write.
     */
    const std::string id = privileged_folder_id_for(instance_id);
    if (folders_.find(id))
        return id;

    domain::folder row;
    row.id = id;
    row.parent_id = domain::accounts_root_id_for(instance_id);
    row.owner_id = instance_id;
    row.name = "Privileged";
    row.tree = domain::folder_tree::accounts;
    static_cast<void>(folders_.upsert(row));

    if (!folder_repo_)
        return id;
    if (auto saved = folder_repo_->save(row); !saved) {
        raise({condition_source::folder, id},
            store_failure(
                std::format("cannot save the Privileged folder {}", id),
                saved.error()));
    }
    return id;
}

void seeder::seed_accounts_for_instance(
    const domain::instance& instance) {
    if (instance.id.empty())
        return;

    /*
     * One login for whoever runs the deployment and one for whoever runs a
     * tenant inside it, which between them are what a person has to be to reach
     * anything at all. The principal is the instance's own subject prefix with
     * the role in front, because that prefix is the name every service of the
     * deployment already answers to and IAM has to be handed a name it can
     * match. An instance that names no prefix falls back to its id, so the
     * principal never shrinks to a bare role with nothing after the at sign.
     */
    /*
     * Both logins sit in the instance's Privileged folder rather than at the
     * accounts root, so the deployment's own logins are somewhere an operator
     * can see apart from the people they add. The folder is made sure of
     * rather than pointed at blindly, because an account whose folder is
     * missing must not be written silently into the root; a folder the store
     * refused raises a condition, exactly as a refused account write does.
     */
    const std::string folder_id = ensure_privileged_folder(instance.id);

    struct seeded_login final {
        std::string_view name;
        std::string_view role;
    };
    /*
     * The deployment's own system administrator is 'super_admin', which is the
     * name the ORE bootstrap creates and the one IAM will accept. The tenant
     * administrator's name is the tenant's, so the second row is a starting
     * point an operator edits rather than a name this client can know.
     */
    constexpr seeded_login logins[] = {
        { "super admin", "super_admin" },
        { "tenant admin", "tenant_admin" },
    };

    /*
     * The account key is derived from the instance and the name, and that is
     * the whole of the idempotency, because seeding twice writes the same two
     * rows over themselves. The Accounts window generates its keys, so a typed
     * account never shares a key with a seeded one.
     */
    for (const auto& login : logins) {
        const auto id = std::format("acct:{}:{}", instance.id, login.name);

        /*
         * Seed once and never again. The rows are the operator's after the
         * first start: a principal corrected here, or a folder they moved the
         * login into, must survive the next launch. Overwriting them is how a
         * corrected principal silently became the seeded guess again.
         */
        if (accounts_.find(id))
            continue;

        domain::account row;
        row.id = id;
        row.instance_id = instance.id;
        row.name = std::string{login.name};
        row.folder_id = folder_id;
        row.principal = std::string{login.role};

        static_cast<void>(accounts_.upsert(row));

        if (auto saved = account_repo_->save(row); !saved) {
            raise({condition_source::account, row.id},
                store_failure(std::format(
                    "cannot save the seeded account {}", row.id), saved.error()));
        }
    }
}

std::vector<refusal>
backfill_seeded_icon_keys(const tables& state, const repositories& stores) {
    seeder run(state, stores, {});
    run.backfill_seeded_icon_keys();
    return run.take_refusals();
}

std::vector<refusal> seed_store(const tables& state,
    const repositories& stores, const transport::session_options& seed) {
    seeder run(state, stores, seed);

    /*
     * The roots come first, because the seed is an instance and an instance has
     * to sit in a folder: seeding before the tree exists would leave the row
     * with no parent, which is a state the Browser cannot draw.
     */
    run.ensure_roots();
    run.seed_first_run();

    /*
     * The seed is an instance, and every instance owns an accounts tree, so the
     * roots are made sure of once more on the other side of it: the first call
     * above ran over the instances the store already had, and this one runs
     * over the instance the seed just added.
     */
    run.ensure_roots();

    /*
     * After the roots rather than before them, so the folder every seeded
     * account sits in already exists, and after the seed of a first run, so the
     * instance that seed just added is covered by the same walk that covers the
     * instances the store already held.
     */
    run.seed_instance_accounts();
    return run.take_refusals();
}

}
