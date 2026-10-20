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

/*
 * The catalogue handlers: what a refused store write leaves behind in the
 * tables, what a removal takes with it, and what it leaves alone. A store write
 * is refused by a trigger created on a second connection, because that is the
 * one way to make a real SQLite statement fail without a fake repository.
 */

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/folder.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/label.hpp"
#include "hedgr.model/domain/labelling.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/shortcut.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.runtime/worker/market_stream.hpp"
#include "hedgr.runtime/worker/tick_feed.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::domain::account;
using hedgr::model::domain::folder_tree;
using hedgr::model::domain::instance;
using hedgr::model::domain::labelling_owner;
using hedgr::model::domain::secret_string;
using hedgr::model::domain::session_status;
using hedgr::model::domain::window_kind;
using hedgr::model::event::event;
using hedgr::model::event::event_kind;
using hedgr::model::state::condition_source;
using hedgr::model::state::market_bucket;
using hedgr::runtime::manager::instance_manager;
using hedgr::runtime::worker::market_stream_options;
using hedgr::runtime::worker::tick_feed;
using hedgr::session::transport::session_options;

/** @brief A database file that deletes itself, sidecars included. */
class temporary_database final {
public:
    temporary_database()
        : path_(std::filesystem::temp_directory_path() /
              std::format("hedgr.runtime.catalogue.{}.{}.sqlite",
                  unique_token(), next_serial())) {
        erase();
    }

    ~temporary_database() { erase(); }

    temporary_database(const temporary_database&) = delete;
    temporary_database& operator=(const temporary_database&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    /** @brief Runs @p sql on a connection of its own. */
    void execute(std::string_view sql) const {
        auto opened = hedgr::store::sqlite::database::open(path_);
        REQUIRE(opened.has_value());
        const auto done = opened->execute(std::string{sql});
        INFO(sql);
        if (!done)
            INFO(done.error().message);
        REQUIRE(done.has_value());
    }

    /** @brief Makes every insert, update or delete on @p table fail. */
    void refuse(std::string_view table) const {
        for (const auto* verb : {"INSERT", "UPDATE", "DELETE"}) {
            execute(std::format("CREATE TRIGGER refuse_{0}_{1} BEFORE {1} ON "
                                "{0} BEGIN SELECT RAISE(ABORT, 'refused by "
                                "test'); END",
                table, verb));
        }
    }

    /** @brief Makes only @p verb on @p table fail. */
    void refuse(std::string_view table, std::string_view verb) const {
        execute(std::format("CREATE TRIGGER refuse_{0}_{1} BEFORE {1} ON {0} "
                            "BEGIN SELECT RAISE(ABORT, 'refused by test'); END",
            table, verb));
    }

    void allow(std::string_view table) const {
        for (const auto* verb : {"INSERT", "UPDATE", "DELETE"}) {
            execute(std::format("DROP TRIGGER IF EXISTS refuse_{}_{}", table,
                verb));
        }
    }

private:
    static std::string unique_token() {
        std::random_device source;
        return std::format("{:08x}{:08x}", source(), source());
    }

    static unsigned next_serial() {
        static std::atomic<unsigned> serial{0};
        return serial.fetch_add(1);
    }

    void erase() const {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
    }

    std::filesystem::path path_;
};

[[nodiscard]] session_options seed_options() {
    session_options options;
    options.nats_url = "nats://localhost:20405";
    options.subject_prefix = "ores.dev.brave_hopper";
    return options;
}

[[nodiscard]] bool wait_until(const std::function<bool()>& ready,
    std::chrono::milliseconds limit = std::chrono::seconds(10)) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (ready())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ready();
}

/** @brief What a fake feed saw. Touched on the frame thread only. */
struct feed_state final {
    int stops = 0;
};

class fake_feed final : public tick_feed {
public:
    explicit fake_feed(std::shared_ptr<feed_state> state)
        : state_(std::move(state)) {}

    void request_stop() override { ++state_->stops; }
    [[nodiscard]] bool finished() const noexcept override { return true; }
    void set_token(std::string) override {}
    void watch(std::string, std::string) override {}
    void unwatch(std::string_view) override {}
    std::size_t drain(std::vector<market_bucket>& out) override {
        out.clear();
        return 0;
    }
    bool take_reconnected() override { return false; }

private:
    std::shared_ptr<feed_state> state_;
};

/** @brief Whether @p message starts with @p prefix and carries the trigger's reason. */
[[nodiscard]] bool refused_by_test(const std::string& message,
    std::string_view prefix) {
    return message.starts_with(prefix)
        && message.find("refused by test") != std::string::npos;
}

constexpr auto refused_url = "nats://127.0.0.1:1";

/** @brief One manager, a store, and every feed it opened. */
struct world final {
    world() {
        manager = std::make_unique<instance_manager>(1,
            [this](market_stream_options) -> std::unique_ptr<tick_feed> {
                feeds.push_back(std::make_shared<feed_state>());
                return std::make_unique<fake_feed>(feeds.back());
            });
        REQUIRE(manager->start(database.path(), seed_options()).has_value());
    }

    ~world() { manager->stop(); }

    void push(command request) {
        manager->commands().push(std::move(request));
        manager->pump();
    }

    void push(event fact) {
        manager->events().push(std::move(fact));
        manager->pump();
    }

    void save_instance(const std::string& id, std::string name = {},
        std::string folder = {}) {
        command save;
        save.kind = command_kind::save_instance;
        save.instance_row.id = id;
        save.instance_row.name = name.empty() ? id : std::move(name);
        save.instance_row.nats_url = refused_url;
        save.instance_row.subject_prefix = "ores.test.refused";
        save.instance_row.folder_id = std::move(folder);
        save.instance_id = id;
        push(std::move(save));
    }

    void save_account(const std::string& id, const std::string& instance_id,
        std::string folder = {}) {
        command save;
        save.kind = command_kind::save_account;
        save.account_row.id = id;
        save.account_row.instance_id = instance_id;
        save.account_row.name = id;
        save.account_row.principal = id + "@example.test";
        save.account_row.folder_id = std::move(folder);
        push(std::move(save));
    }

    void save_folder(const std::string& id, const std::string& parent,
        std::string name = {}) {
        command save;
        save.kind = command_kind::save_folder;
        save.folder_row.id = id;
        save.folder_row.parent_id = parent;
        save.folder_row.name = name.empty() ? id : std::move(name);
        push(std::move(save));
    }

    void save_label(const std::string& id, std::string name = {}) {
        command save;
        save.kind = command_kind::save_label;
        save.label_row.id = id;
        save.label_row.name = name.empty() ? id : std::move(name);
        push(std::move(save));
    }

    void save_shortcut(const std::string& id, window_kind kind,
        const std::string& target, std::int64_t position) {
        command save;
        save.kind = command_kind::save_shortcut;
        save.shortcut_row.id = id;
        save.shortcut_row.target_kind = kind;
        save.shortcut_row.target_id = target;
        save.shortcut_row.position = position;
        push(std::move(save));
    }

    void attach(labelling_owner owner, const std::string& owner_id,
        const std::string& label_id) {
        command attach;
        attach.kind = command_kind::attach_label;
        attach.labelling_row.owner = owner;
        attach.labelling_row.owner_id = owner_id;
        attach.labelling_row.label_id = label_id;
        push(std::move(attach));
    }

    void remove(command_kind kind, const std::string& id) {
        command request;
        request.kind = kind;
        request.instance_id = id;
        request.account_id = id;
        request.folder_row.id = id;
        request.label_row.id = id;
        request.shortcut_row.id = id;
        push(std::move(request));
    }

    /** @brief Signs @p account_id in as far as the frame loop can tell. */
    void sign_in(const std::string& instance_id, const std::string& account_id) {
        command connect;
        connect.kind = command_kind::connect_session;
        connect.instance_id = instance_id;
        connect.account_id = account_id;
        connect.principal = account_id + "@example.test";
        connect.password = secret_string{std::string{"not-a-real-secret"}};
        push(std::move(connect));
        REQUIRE(wait_until([&] {
            manager->pump();
            const auto handle = manager->sessions().find(instance_id, account_id);
            return handle
                && manager->sessions().status(*handle) == session_status::failed;
        }));

        event fact;
        fact.kind = event_kind::session_authenticated;
        fact.instance_id = instance_id;
        fact.account_id = account_id;
        fact.tenant_id = "tenant-1";
        fact.party_id = "p-1";
        fact.token = secret_string{std::string{"token-1"}};
        push(std::move(fact));
        const auto handle = manager->sessions().find(instance_id, account_id);
        REQUIRE(handle.has_value());
        REQUIRE(manager->sessions().status(*handle)
            == session_status::authenticated);
    }

    /** @brief Opens a browser window on the pair and returns its id. */
    std::string open_browser(const std::string& instance_id,
        const std::string& account_id) {
        command open;
        open.kind = command_kind::open_window;
        open.instance_id = instance_id;
        open.account_id = account_id;
        open.new_window_kind = window_kind::series_browser;
        push(std::move(open));
        return std::format("series-browser:{}:{}", instance_id, account_id);
    }

    [[nodiscard]] const hedgr::model::domain::instance* instance_row(
        std::string_view id) const {
        const auto handle = manager->instances().find(id);
        return handle ? manager->instances().get(*handle) : nullptr;
    }

    [[nodiscard]] const hedgr::model::domain::account* account_row(
        std::string_view id) const {
        const auto handle = manager->accounts().find(id);
        return handle ? manager->accounts().get(*handle) : nullptr;
    }

    [[nodiscard]] const hedgr::model::domain::folder* folder_row(
        std::string_view id) const {
        const auto handle = manager->folders().find(id);
        return handle ? manager->folders().get(*handle) : nullptr;
    }

    [[nodiscard]] const hedgr::model::domain::shortcut* shortcut_row(
        std::string_view id) const {
        for (const auto& row : manager->shortcuts().rows()) {
            if (row.id == id)
                return &row;
        }
        return nullptr;
    }

    /** @brief The window by id, found by walking, because ids are keys. */
    [[nodiscard]] std::optional<hedgr::model::state::window_handle>
    window(const std::string& id) const {
        return manager->windows().find(id);
    }

    [[nodiscard]] bool unbound(const std::string& window_id) const {
        const auto handle = window(window_id);
        return handle && !manager->windows().session_of(*handle).has_value();
    }

    temporary_database database;
    std::vector<std::shared_ptr<feed_state>> feeds;
    std::unique_ptr<instance_manager> manager;
};

}

TEST_CASE("a refused save_instance puts the previous row back", "[catalogue]") {
    world w;
    w.save_instance("inst-a", "Alpha");
    const auto folders_before = w.manager->folders().size();

    w.database.refuse("instance");
    w.save_instance("inst-a", "Alpha renamed");
    REQUIRE(w.instance_row("inst-a") != nullptr);
    REQUIRE(w.instance_row("inst-a")->name == "Alpha");
    REQUIRE(refused_by_test(w.manager->last_error(), "cannot save instance inst-a:"));

    w.save_instance("inst-b", "Beta");
    REQUIRE(w.instance_row("inst-b") == nullptr);
    REQUIRE(w.manager->instances().size() == 2);
    REQUIRE(w.manager->folders().size() == folders_before);
    REQUIRE(refused_by_test(w.manager->last_error(), "cannot save instance inst-b:"));
}

TEST_CASE("a saved instance with no folder lands in the instances root and "
          "gets its accounts root", "[catalogue]") {
    world w;
    w.save_instance("inst-a");

    REQUIRE(w.instance_row("inst-a")->folder_id == "instances-root");
    const auto root = w.manager->folders().root(folder_tree::accounts, "inst-a");
    REQUIRE(root.has_value());
    REQUIRE(w.manager->folders().get(*root)->id == "accounts-root:inst-a");
    REQUIRE(w.manager->last_error().empty());
}

TEST_CASE("a refused accounts root after a saved instance is reported",
    "[catalogue]") {
    world w;
    w.database.refuse("folder");
    w.save_instance("inst-a");

    REQUIRE(w.instance_row("inst-a") != nullptr);
    REQUIRE(w.manager->last_error().starts_with("cannot "));
}

TEST_CASE("a refused save_account puts the previous row back", "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_account("ident-1", "inst-a");
    REQUIRE(w.account_row("ident-1")->folder_id == "accounts-root:inst-a");
    const auto before = w.manager->accounts().size();

    w.database.refuse("account");
    command rename;
    rename.kind = command_kind::save_account;
    rename.account_row = *w.account_row("ident-1");
    rename.account_row.name = "renamed";
    w.push(rename);
    REQUIRE(w.account_row("ident-1")->name == "ident-1");
    REQUIRE(refused_by_test(w.manager->last_error(), "cannot save account ident-1:"));

    w.save_account("ident-2", "inst-a");
    REQUIRE(w.account_row("ident-2") == nullptr);
    REQUIRE(w.manager->accounts().size() == before);
}

TEST_CASE("a refused save_folder puts the previous row back", "[catalogue]") {
    world w;
    w.save_folder("fold-a", "instances-root", "A");
    w.save_folder("fold-b", "instances-root", "B");

    w.database.refuse("folder");
    w.save_folder("fold-a", "fold-b", "A moved");
    REQUIRE(w.folder_row("fold-a")->parent_id == "instances-root");
    REQUIRE(w.folder_row("fold-a")->name == "A");
    REQUIRE(refused_by_test(w.manager->last_error(), "cannot save folder fold-a:"));

    const auto before = w.manager->folders().size();
    w.save_folder("fold-c", "instances-root", "C");
    REQUIRE(w.folder_row("fold-c") == nullptr);
    REQUIRE(w.manager->folders().size() == before);
}

TEST_CASE("a folder takes its tree and owner from its parent", "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_folder("fold-a", "accounts-root:inst-a", "A");

    REQUIRE(w.folder_row("fold-a")->tree == folder_tree::accounts);
    REQUIRE(w.folder_row("fold-a")->owner_id == "inst-a");

    /* An edit that names no parent in the table keeps what the row had. */
    w.save_folder("fold-a", "accounts-root:inst-a", "A renamed");
    REQUIRE(w.folder_row("fold-a")->name == "A renamed");
    REQUIRE(w.folder_row("fold-a")->owner_id == "inst-a");
}

TEST_CASE("a folder save names what is wrong with it", "[catalogue]") {
    world w;
    const auto refuses = [&](std::string id, std::string parent,
                             std::string name, std::string_view message) {
        command save;
        save.kind = command_kind::save_folder;
        save.folder_row.id = std::move(id);
        save.folder_row.parent_id = std::move(parent);
        save.folder_row.name = std::move(name);
        w.push(std::move(save));
        REQUIRE(w.manager->last_error() == message);
    };

    refuses("", "instances-root", "A", "cannot save a folder: no folder was named");
    refuses("fold-a", "instances-root", "",
        "cannot save folder fold-a: it has no name");
    refuses("fold-a", "fold-missing", "A",
        "cannot save folder fold-a: no parent folder fold-missing");
    refuses("fold-a", "", "A",
        "cannot save folder fold-a: no parent folder <none>");
    REQUIRE(w.folder_row("fold-a") == nullptr);

    /* A folder that exists cannot be edited into a second root. */
    w.save_folder("fold-b", "instances-root", "B");
    refuses("fold-b", "", "B",
        "cannot save folder fold-b: only a root has no parent");
    REQUIRE(w.folder_row("fold-b")->parent_id == "instances-root");
}

TEST_CASE("a refused save_label puts the previous row back", "[catalogue]") {
    world w;
    w.save_label("lbl-a", "Alpha");

    w.database.refuse("label");
    w.save_label("lbl-a", "Alpha renamed");
    REQUIRE(w.manager->labels().get(*w.manager->labels().find("lbl-a"))->name
        == "Alpha");
    REQUIRE(refused_by_test(w.manager->last_error(), "cannot save label lbl-a:"));

    w.save_label("lbl-b");
    REQUIRE_FALSE(w.manager->labels().find("lbl-b").has_value());
    REQUIRE(w.manager->labels().size() == 1);
}

TEST_CASE("a label save names what is wrong with it", "[catalogue]") {
    world w;
    command save;
    save.kind = command_kind::save_label;
    save.label_row.name = "nameless id";
    w.push(save);
    REQUIRE(w.manager->last_error() == "cannot save a label: no label was named");

    save.label_row.id = "lbl-a";
    save.label_row.name.clear();
    w.push(save);
    REQUIRE(w.manager->last_error() == "cannot save label lbl-a: it has no name");
    REQUIRE(w.manager->labels().size() == 0);
}

TEST_CASE("a refused attach_label removes the attachment it added",
    "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_label("lbl-a");

    w.database.refuse("instance_label");
    w.attach(labelling_owner::instance, "inst-a", "lbl-a");

    REQUIRE(w.manager->labellings().size() == 0);
    REQUIRE(refused_by_test(w.manager->last_error(), "cannot attach label lbl-a to inst-a:"));
}

TEST_CASE("an attach names the end that is missing", "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_account("ident-1", "inst-a");
    w.save_label("lbl-a");

    w.attach(labelling_owner::account, "ident-missing", "lbl-a");
    REQUIRE(w.manager->last_error() == "cannot attach a label: no account ident-missing");

    w.attach(labelling_owner::account, "ident-1", "lbl-missing");
    REQUIRE(w.manager->last_error() == "cannot attach a label: no label lbl-missing");

    w.attach(labelling_owner::account, "", "lbl-a");
    REQUIRE(w.manager->last_error()
        == "cannot attach a label: the owner and the label must be named");

    w.attach(labelling_owner::account, "ident-1", "lbl-a");
    REQUIRE(w.manager->last_error().empty());
    REQUIRE(w.manager->labellings().labels_of(labelling_owner::account,
        "ident-1").size() == 1);

    command detach;
    detach.kind = command_kind::detach_label;
    detach.labelling_row.owner = labelling_owner::account;
    w.push(detach);
    REQUIRE(w.manager->last_error()
        == "cannot detach a label: the owner and the label must be named");
    REQUIRE(w.manager->labellings().size() == 1);
}

TEST_CASE("a refused save_shortcut puts the previous entry back",
    "[catalogue]") {
    world w;
    w.save_shortcut("sc-a", window_kind::grid, "", 10);

    w.database.refuse("shortcut");
    w.save_shortcut("sc-a", window_kind::grid, "", 99);
    REQUIRE(w.shortcut_row("sc-a")->position == 10);
    REQUIRE(refused_by_test(w.manager->last_error(), "cannot save shortcut sc-a:"));

    w.save_shortcut("sc-b", window_kind::grid, "", 20);
    REQUIRE(w.shortcut_row("sc-b") == nullptr);
    REQUIRE(w.manager->shortcuts().size() == 1);
}

TEST_CASE("a handler names what is missing and says when the store is shut",
    "[catalogue]") {
    world w;
    const auto refused = [&](command_kind kind, std::string_view message) {
        command request;
        request.kind = kind;
        w.push(std::move(request));
        REQUIRE(w.manager->last_error() == message);
    };

    refused(command_kind::remove_instance,
        "cannot remove an instance: no instance was named");
    refused(command_kind::remove_account,
        "cannot remove an account: no account was named");
    refused(command_kind::remove_folder,
        "cannot remove a folder: no folder was named");
    refused(command_kind::remove_label,
        "cannot remove a label: no label was named");
    refused(command_kind::remove_shortcut,
        "cannot remove a shortcut: no shortcut was named");
    refused(command_kind::save_shortcut,
        "cannot save a shortcut: no shortcut was named");

    instance_manager unstarted;
    const auto shut = [&](command_kind kind, std::string_view message) {
        command request;
        request.kind = kind;
        unstarted.commands().push(std::move(request));
        unstarted.pump();
        REQUIRE(unstarted.last_error() == message);
    };
    shut(command_kind::save_instance,
        "cannot save an instance: the store is not open");
    shut(command_kind::save_account,
        "cannot save an account: the store is not open");
    shut(command_kind::save_folder,
        "cannot save a folder: the store is not open");
    shut(command_kind::remove_folder,
        "cannot remove a folder: the store is not open");
    shut(command_kind::save_label,
        "cannot save a label: the store is not open");
    shut(command_kind::remove_label,
        "cannot remove a label: the store is not open");
    shut(command_kind::attach_label,
        "cannot attach a label: the store is not open");
    shut(command_kind::detach_label,
        "cannot detach a label: the store is not open");
    shut(command_kind::save_shortcut,
        "cannot save a shortcut: the store is not open");
    shut(command_kind::remove_shortcut,
        "cannot remove a shortcut: the store is not open");
    unstarted.stop();
}

TEST_CASE("removing a label takes its attachments with it, in the store too",
    "[catalogue]") {
    temporary_database database;
    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        const auto seeded = manager.instances().rows()[0].id;
        const auto push = [&](command request) {
            manager.commands().push(std::move(request));
            manager.pump();
        };
        for (const auto* id : {"lbl-a", "lbl-b"}) {
            command save;
            save.kind = command_kind::save_label;
            save.label_row.id = id;
            save.label_row.name = id;
            push(save);
            command attach;
            attach.kind = command_kind::attach_label;
            attach.labelling_row.owner = labelling_owner::instance;
            attach.labelling_row.owner_id = seeded;
            attach.labelling_row.label_id = id;
            push(attach);
        }
        REQUIRE(manager.labellings().size() == 2);

        command remove;
        remove.kind = command_kind::remove_label;
        remove.label_row.id = "lbl-a";
        push(remove);

        REQUIRE_FALSE(manager.labels().find("lbl-a").has_value());
        REQUIRE(manager.labellings().size() == 1);
        REQUIRE(manager.labellings().rows()[0].label_id == "lbl-b");
        REQUIRE(manager.last_error().empty());
        manager.stop();
    }

    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.labels().size() == 1);
    REQUIRE(reopened.labellings().size() == 1);
    reopened.stop();
}

TEST_CASE("removing a folder re-homes its accounts to their instance's root",
    "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_folder("fold-a", "accounts-root:inst-a");
    w.save_account("ident-1", "inst-a", "fold-a");
    w.save_account("ident-2", "inst-a");
    REQUIRE(w.account_row("ident-1")->folder_id == "fold-a");

    w.remove(command_kind::remove_folder, "fold-a");

    REQUIRE(w.folder_row("fold-a") == nullptr);
    REQUIRE(w.account_row("ident-1")->folder_id == "accounts-root:inst-a");
    REQUIRE(w.account_row("ident-2")->folder_id == "accounts-root:inst-a");
    REQUIRE(w.manager->last_error().empty());
}

TEST_CASE("a folder removal re-homes in the store as well", "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_folder("fold-a", "accounts-root:inst-a");
    w.save_folder("fold-nested", "fold-a");
    w.save_account("ident-1", "inst-a", "fold-a");
    w.save_folder("fold-i", "instances-root");
    w.save_instance("inst-b", "B", "fold-i");
    w.remove(command_kind::remove_folder, "fold-i");
    w.remove(command_kind::remove_folder, "fold-a");
    w.manager->stop();

    instance_manager reopened;
    REQUIRE(reopened.start(w.database.path(), seed_options()).has_value());
    const auto account = reopened.accounts().find("ident-1");
    REQUIRE(account.has_value());
    REQUIRE(reopened.accounts().get(*account)->folder_id == "accounts-root:inst-a");
    REQUIRE_FALSE(reopened.folders().find("fold-a").has_value());
    REQUIRE_FALSE(reopened.folders().find("fold-nested").has_value());
    const auto inst = reopened.instances().find("inst-b");
    REQUIRE(inst.has_value());
    REQUIRE(reopened.instances().get(*inst)->folder_id == "instances-root");
    reopened.stop();
}

TEST_CASE("a refused re-home is reported and the table still moves",
    "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_folder("fold-a", "accounts-root:inst-a");
    w.save_account("ident-1", "inst-a", "fold-a");

    w.database.refuse("account", "UPDATE");
    w.remove(command_kind::remove_folder, "fold-a");

    REQUIRE(w.account_row("ident-1")->folder_id == "accounts-root:inst-a");
    for (const auto& row : w.manager->conditions().rows())
        UNSCOPED_INFO(row.key.subject << " => " << row.message);
    bool named = false;
    for (const auto& row : w.manager->conditions().rows())
        named = named || (row.key.source == condition_source::account
            && row.key.subject == "ident-1"
            && refused_by_test(row.message,
                "cannot move account ident-1 out of a removed folder:"));
    REQUIRE(named);
}

TEST_CASE("a refused shortcut cascade is reported", "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_account("ident-1", "inst-a");
    w.save_shortcut("sc-w", window_kind::workspace, "ident-1", 10);

    w.database.refuse("shortcut");
    w.remove(command_kind::remove_account, "ident-1");

    REQUIRE(w.account_row("ident-1") == nullptr);
    REQUIRE(w.shortcut_row("sc-w") == nullptr);
    REQUIRE(refused_by_test(w.manager->last_error(), "cannot remove the shortcuts for target ident-1:"));
}

TEST_CASE("removing a signed-in account stops its session and unbinds its "
          "windows", "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_account("ident-1", "inst-a");
    w.save_account("ident-2", "inst-a");
    w.sign_in("inst-a", "ident-1");
    w.sign_in("inst-a", "ident-2");
    const auto first = w.open_browser("inst-a", "ident-1");
    const auto second = w.open_browser("inst-a", "ident-2");
    REQUIRE(w.window(first).has_value());
    REQUIRE(w.window(second).has_value());
    REQUIRE_FALSE(w.unbound(first));
    w.save_shortcut("sc-w", window_kind::workspace, "ident-1", 10);
    w.save_shortcut("sc-other", window_kind::workspace, "ident-2", 20);

    w.remove(command_kind::remove_account, "ident-1");

    REQUIRE(w.account_row("ident-1") == nullptr);
    REQUIRE_FALSE(w.manager->sessions().find("inst-a", "ident-1").has_value());
    REQUIRE(w.window(first).has_value());
    REQUIRE(w.unbound(first));
    REQUIRE(w.shortcut_row("sc-w") == nullptr);

    REQUIRE(w.account_row("ident-2") != nullptr);
    REQUIRE(w.manager->sessions().find("inst-a", "ident-2").has_value());
    REQUIRE_FALSE(w.unbound(second));
    REQUIRE(w.shortcut_row("sc-other") != nullptr);
    REQUIRE(w.manager->last_error().empty());

    w.manager->pump();
    REQUIRE(w.feeds.size() >= 1);
    int stopped = 0;
    for (const auto& feed : w.feeds)
        stopped += feed->stops;
    REQUIRE(stopped == 1);
}

TEST_CASE("a removed account is gone from the store and its window wall is "
          "saved", "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_account("ident-1", "inst-a");
    w.remove(command_kind::remove_account, "ident-1");
    w.manager->stop();

    instance_manager reopened;
    REQUIRE(reopened.start(w.database.path(), seed_options()).has_value());
    REQUIRE_FALSE(reopened.accounts().find("ident-1").has_value());
    reopened.stop();
}

TEST_CASE("a refused account removal is reported", "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_account("ident-1", "inst-a");

    w.database.refuse("account", "DELETE");
    w.remove(command_kind::remove_account, "ident-1");
    REQUIRE(refused_by_test(w.manager->last_error(), "cannot remove account ident-1:"));
}

TEST_CASE("removing an instance removes its sessions, windows, probes and "
          "labels and leaves another instance alone", "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_instance("inst-b");
    w.save_account("ident-a", "inst-a");
    w.save_account("ident-b", "inst-b");
    w.save_label("lbl-1");
    w.attach(labelling_owner::instance, "inst-a", "lbl-1");
    w.attach(labelling_owner::instance, "inst-b", "lbl-1");
    w.sign_in("inst-a", "ident-a");
    w.sign_in("inst-b", "ident-b");
    const auto doomed = w.open_browser("inst-a", "ident-a");
    const auto kept = w.open_browser("inst-b", "ident-b");
    const auto accounts_window = w.manager->open_accounts_window("inst-a");
    w.save_shortcut("sc-accounts", window_kind::accounts, "inst-a", 10);
    w.save_shortcut("sc-workspace", window_kind::workspace, "ident-a", 20);
    w.save_shortcut("sc-kept", window_kind::workspace, "ident-b", 30);

    const auto probed = w.instance_row("inst-a") != nullptr
        && wait_until([&] {
               w.manager->pump();
               const auto handle = w.manager->instances().find("inst-a");
               return handle && w.manager->probes().get(*handle) != nullptr;
           });
    REQUIRE(probed);

    w.remove(command_kind::remove_instance, "inst-a");

    REQUIRE(w.instance_row("inst-a") == nullptr);
    REQUIRE(w.account_row("ident-a") == nullptr);
    REQUIRE_FALSE(w.manager->folders().find("accounts-root:inst-a").has_value());
    REQUIRE_FALSE(w.manager->sessions().find("inst-a", "ident-a").has_value());
    REQUIRE(w.window(doomed).has_value());
    REQUIRE(w.unbound(doomed));
    REQUIRE(w.window(accounts_window).has_value());
    REQUIRE(w.manager->labellings().labels_of(labelling_owner::instance,
        "inst-a").empty());
    REQUIRE(w.shortcut_row("sc-accounts") == nullptr);
    REQUIRE(w.shortcut_row("sc-workspace") == nullptr);
    REQUIRE(w.manager->last_error().empty());

    REQUIRE(w.instance_row("inst-b") != nullptr);
    REQUIRE(w.account_row("ident-b") != nullptr);
    REQUIRE(w.manager->sessions().find("inst-b", "ident-b").has_value());
    REQUIRE_FALSE(w.unbound(kept));
    REQUIRE(w.manager->labellings().labels_of(labelling_owner::instance,
        "inst-b").size() == 1);
    REQUIRE(w.shortcut_row("sc-kept") != nullptr);

    /* The next row takes the slot, and must not inherit the probe's result. */
    w.save_instance("inst-c");
    const auto next = w.manager->instances().find("inst-c");
    REQUIRE(next.has_value());
    REQUIRE(w.manager->probes().get(*next) == nullptr);

    /* The feed of the removed pair is retired and the other one is not. */
    int stopped = 0;
    for (const auto& feed : w.feeds)
        stopped += feed->stops;
    REQUIRE(stopped == 1);
}

TEST_CASE("a refused store write in an instance removal is reported and the "
          "rest still goes", "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_account("ident-a", "inst-a");
    w.save_label("lbl-1");
    w.attach(labelling_owner::instance, "inst-a", "lbl-1");

    const auto removal_is_refused = [&](std::string_view table,
                                        std::string_view verb,
                                        std::string_view message) {
        w.database.refuse(table, verb);
        w.remove(command_kind::remove_instance, "inst-a");
        REQUIRE(refused_by_test(w.manager->last_error(), std::string{message}));
        REQUIRE(w.instance_row("inst-a") == nullptr);
        REQUIRE(w.account_row("ident-a") == nullptr);
    };

    SECTION("folders") {
        removal_is_refused("folder", "DELETE",
            "cannot remove the folders of instance inst-a:");
    }
    SECTION("the instance") {
        removal_is_refused("instance", "DELETE",
            "cannot remove instance inst-a:");
    }
}

TEST_CASE("a removal saves the window wall", "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_account("ident-1", "inst-a");

    const auto wall_refused = [&] {
        for (const auto& row : w.manager->conditions().rows()) {
            if (row.key.source == condition_source::window_wall)
                return true;
        }
        return false;
    };
    w.database.refuse("window_spec");

    SECTION("of an account") {
        w.remove(command_kind::remove_account, "ident-1");
        REQUIRE(wall_refused());
    }
    SECTION("of an instance") {
        w.remove(command_kind::remove_instance, "inst-a");
        REQUIRE(wall_refused());
    }
}
TEST_CASE("a refused re-attach keeps the attachment the store still holds",
    "[catalogue]") {
    world w;
    w.save_instance("inst-a");
    w.save_label("lbl-a");
    w.attach(labelling_owner::instance, "inst-a", "lbl-a");
    REQUIRE(w.manager->labellings().size() == 1);

    w.database.refuse("instance_label", "INSERT");
    w.attach(labelling_owner::instance, "inst-a", "lbl-a");

    REQUIRE(refused_by_test(w.manager->last_error(),
        "cannot attach label lbl-a to inst-a:"));
    REQUIRE(w.manager->labellings().size() == 1);
}
