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
#ifndef HEDGR_RUNTIME_SERVICE_CATALOGUE_SERVICE_HPP
#define HEDGR_RUNTIME_SERVICE_CATALOGUE_SERVICE_HPP

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.runtime/refusal.hpp"
#include "hedgr.runtime/seed/seeder.hpp"
#include "hedgr.store/repository/account_repository.hpp"
#include "hedgr.store/repository/folder_repository.hpp"
#include "hedgr.store/repository/instance_repository.hpp"
#include "hedgr.store/repository/label_repository.hpp"
#include "hedgr.store/repository/labelling_repository.hpp"
#include "hedgr.store/repository/shortcut_repository.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hedgr::runtime::service {

/**
 * @brief An instance or an account the catalogue removed, and who else held it.
 *
 * The catalogue owns the rows and not the things that hang off them. A session,
 * a probe, a stream and a window each belong to another job, so the removal
 * says what went and the facade winds those down.
 */
struct removal final {
    std::string instance_id;

    /** @brief The account that went, or empty when the whole instance did. */
    std::string account_id;

    /**
     * @brief The removed instance's row as the probe table keyed it.
     *
     * Valid for the probe table although the instance table has released the
     * slot, because the probe row carries the generation it was made with.
     */
    std::optional<model::state::instance_handle> probe;
};

/** @brief What is left for the facade after one catalogue command. */
struct catalogue_effect final {
    /** @brief What the command refused, in the order it refused it. */
    std::vector<refusal> refusals;

    /** @brief Set when a removal ran, so the facade has sessions to wind down. */
    std::optional<removal> removed;
};

/**
 * @brief The catalogue: instances, accounts, folders, labels, their
 * attachments and the shortcuts bar, with the repositories that store them.
 *
 * Runs on the frame thread and is the only writer of its six tables, apart from
 * the lock, which sets the flag that says an account holds a credential. A
 * refused store write is put back in the table, or reported, here, and the
 * condition it raises is returned in the effect because the condition table
 * belongs to the facade.
 *
 * Holds no reference to the manager and none to another service. A removal
 * cascades into sessions, probes, streams and windows, and this class returns
 * what it removed for the facade to fan out.
 */
class catalogue_service final {
public:
    /** @brief Opens the six repositories on the store. */
    void attach(store::sqlite::database& database);

    /**
     * @brief Replaces the six tables with what the store holds.
     *
     * The error is the sentence for the table that could not be read.
     */
    [[nodiscard]] std::expected<void, std::string> load();

    /** @brief Applies one of the twelve catalogue commands. */
    [[nodiscard]] catalogue_effect apply(const model::command::command& cmd);

    [[nodiscard]] const model::state::instance_table& instances() const noexcept {
        return instances_;
    }

    [[nodiscard]] const model::state::account_table& accounts() const noexcept {
        return accounts_;
    }

    /** @brief For the lock, which marks an account that holds a credential. */
    [[nodiscard]] model::state::account_table& accounts() noexcept {
        return accounts_;
    }

    [[nodiscard]] const model::state::folder_table& folders() const noexcept {
        return folders_;
    }

    [[nodiscard]] const model::state::label_table& labels() const noexcept {
        return labels_;
    }

    [[nodiscard]] const model::state::labelling_table& labellings() const
        noexcept {
        return labellings_;
    }

    [[nodiscard]] const model::state::shortcut_table& shortcuts() const
        noexcept {
        return shortcuts_;
    }

    /** @brief The account repository, for the credential reads and the lock. */
    [[nodiscard]] store::repository::account_repository&
    account_repository() noexcept {
        return *account_repo_;
    }

    /** @brief The tables the seeder reads and writes. */
    [[nodiscard]] seed::tables seed_tables() noexcept {
        return {instances_, accounts_, folders_, labels_, labellings_};
    }

    /** @brief The repositories the seeder writes through. */
    [[nodiscard]] seed::repositories seed_repositories() noexcept {
        return {instance_repo_.get(), account_repo_.get(), folder_repo_.get(),
            label_repo_.get(), labelling_repo_.get()};
    }

private:
    /**
     * @brief Records @p message as an error condition for @p key.
     *
     * The same call every handler made when it was the manager's. It appends to
     * the refusals the effect returns, because the clock and the table belong
     * to the facade.
     */
    void raise(model::state::condition_key key, std::string message);

    void do_save_instance(const model::command::command& cmd);
    void do_remove_instance(const model::command::command& cmd);
    void do_save_account(const model::command::command& cmd);
    void do_remove_account(const model::command::command& cmd);
    void do_save_folder(const model::command::command& cmd);
    void do_remove_folder(const model::command::command& cmd);
    void do_save_label(const model::command::command& cmd);
    void do_remove_label(const model::command::command& cmd);
    void do_attach_label(const model::command::command& cmd);
    void do_detach_label(const model::command::command& cmd);
    void do_save_shortcut(const model::command::command& cmd);
    void do_remove_shortcut(const model::command::command& cmd);

    /**
     * @brief Drops every shortcut pointing at a target that is going away.
     *
     * Called from the two removals whose rows a shortcut can name, so the bar
     * never offers an entry that opens something the store no longer holds. The
     * store call is made through @c store_failure like every other write, so a
     * refusal reaches the status bar rather than leaving the entry behind.
     */
    void drop_target_shortcuts(model::domain::window_kind kind,
                               const std::string& target_id);

    /**
     * @brief Moves every leaf out of @p folder_ids and into its tree's root.
     *
     * Called before a folder removal, so that removing a folder removes the
     * folder rather than the operator's instances and accounts. A leaf whose
     * tree has no root -- which a store written before roots existed could
     * hold -- is left where it is rather than moved to nowhere, and the
     * runtime reports that the tree is broken instead of silently detaching it.
     */
    void rehome_leaves(const std::vector<std::string>& folder_ids);

    model::state::instance_table instances_;
    model::state::account_table accounts_;
    model::state::folder_table folders_;
    model::state::label_table labels_;
    model::state::labelling_table labellings_;
    model::state::shortcut_table shortcuts_;

    std::unique_ptr<store::repository::instance_repository> instance_repo_;
    std::unique_ptr<store::repository::account_repository> account_repo_;
    std::unique_ptr<store::repository::folder_repository> folder_repo_;
    std::unique_ptr<store::repository::label_repository> label_repo_;
    std::unique_ptr<store::repository::labelling_repository> labelling_repo_;
    std::unique_ptr<store::repository::shortcut_repository> shortcut_repo_;

    /** @brief What the command in hand refused. Taken by @c apply. */
    std::vector<refusal> refusals_;

    /** @brief What the command in hand removed. Taken by @c apply. */
    std::optional<removal> removed_;
};

}

#endif
