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
#include "hedgr.runtime/service/catalogue_service.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.runtime/seed/seeder.hpp"
#include "ores.logging/boost_severity.hpp"
#include "support.hpp"
#include <algorithm>
#include <boost/log/sources/severity_feature.hpp>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hedgr::runtime::service {

namespace {

namespace command = hedgr::model::command;
namespace domain = hedgr::model::domain;
namespace model_state = hedgr::model::state;

using condition_source = model_state::condition_source;

using support::lg;
using support::live_indices;
using support::store_failure;

/**
 * @brief Every live account row, holes skipped, for a whole-table walk.
 *
 * Removing an instance cascades to its accounts, and the rows to drop are found
 * by walking the slab rather than by a lookup, because the question asked is
 * "whose instance is this" rather than "which row is this id".
 */
[[nodiscard]] std::vector<model_state::account_handle>
live_accounts(const model_state::account_table& accounts) {
    std::vector<model_state::account_handle> result;
    const auto& layout = accounts.layout();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        result.push_back(model_state::account_handle{index, layout.generation[index]});
    }
    return result;
}

}

void catalogue_service::attach(store::sqlite::database& database) {
    instance_repo_ = std::make_unique<store::repository::instance_repository>(
        database);
    account_repo_ = std::make_unique<store::repository::account_repository>(
        database);
    folder_repo_ = std::make_unique<store::repository::folder_repository>(
        database);
    label_repo_ = std::make_unique<store::repository::label_repository>(
        database);
    labelling_repo_ = std::make_unique<store::repository::labelling_repository>(
        database);
    shortcut_repo_ = std::make_unique<store::repository::shortcut_repository>(
        database);
}

std::expected<void, std::string> catalogue_service::load() {
    auto instances = instance_repo_->list();
    if (!instances) {
        return std::unexpected(store_failure("cannot read the stored instances",
            instances.error()));
    }
    instances_.clear();
    for (const auto& row : instances.value())
        static_cast<void>(instances_.upsert(row));

    auto accounts = account_repo_->list();
    if (!accounts) {
        return std::unexpected(store_failure("cannot read the stored accounts",
            accounts.error()));
    }
    accounts_.clear();

    /*
     * Whether each account can sign in without a prompt is a fact about the
     * local store, read once here as a list of ids. The credentials themselves
     * are not read: the interface only has to know which accounts have one.
     */
    const auto with_credentials = account_repo_->ids_with_credential();
    if (!with_credentials) {
        return std::unexpected(store_failure("cannot read the stored credentials",
            with_credentials.error()));
    }
    const std::unordered_set<std::string> credential_ids(
        with_credentials->begin(), with_credentials->end());
    for (const auto& row : accounts.value()) {
        auto copy = row;
        copy.has_credential = credential_ids.contains(row.id);
        static_cast<void>(accounts_.upsert(copy));
    }

    auto folders = folder_repo_->list();
    if (!folders) {
        return std::unexpected(store_failure("cannot read the stored folders",
            folders.error()));
    }
    folders_.clear();
    for (const auto& row : folders.value())
        static_cast<void>(folders_.upsert(row));

    auto labels = label_repo_->list();
    if (!labels) {
        return std::unexpected(store_failure("cannot read the stored labels",
            labels.error()));
    }
    labels_.clear();
    for (const auto& row : labels.value())
        static_cast<void>(labels_.upsert(row));

    auto labellings = labelling_repo_->list();
    if (!labellings) {
        return std::unexpected(store_failure("cannot read the stored label attachments",
            labellings.error()));
    }
    labellings_.clear();
    for (const auto& row : labellings.value())
        labellings_.add(row.owner, row.owner_id, row.label_id);

    /*
     * The bar is read in the order it was stored in, and the table keeps that
     * order rather than a caller sorting it, because position and id are what
     * the bar draws by and the read already used them.
     */
    auto shortcuts = shortcut_repo_->list();
    if (!shortcuts) {
        return std::unexpected(store_failure("cannot read the stored shortcuts",
            shortcuts.error()));
    }
    shortcuts_.clear();
    for (const auto& row : shortcuts.value())
        shortcuts_.upsert(row);

    return {};
}

catalogue_effect catalogue_service::apply(const command::command& cmd) {
    refusals_.clear();
    removed_.reset();

    switch (cmd.kind) {
    case command::command_kind::save_instance:
        do_save_instance(cmd);
        break;
    case command::command_kind::remove_instance:
        do_remove_instance(cmd);
        break;
    case command::command_kind::save_account:
        do_save_account(cmd);
        break;
    case command::command_kind::remove_account:
        do_remove_account(cmd);
        break;
    case command::command_kind::save_folder:
        do_save_folder(cmd);
        break;
    case command::command_kind::remove_folder:
        do_remove_folder(cmd);
        break;
    case command::command_kind::save_label:
        do_save_label(cmd);
        break;
    case command::command_kind::remove_label:
        do_remove_label(cmd);
        break;
    case command::command_kind::attach_label:
        do_attach_label(cmd);
        break;
    case command::command_kind::detach_label:
        do_detach_label(cmd);
        break;
    case command::command_kind::save_shortcut:
        do_save_shortcut(cmd);
        break;
    case command::command_kind::remove_shortcut:
        do_remove_shortcut(cmd);
        break;
    default:
        break;
    }

    return {std::move(refusals_), std::move(removed_)};
}

void catalogue_service::raise(model_state::condition_key key,
    std::string message) {
    refusals_.push_back(refusal{std::move(key), std::move(message)});
}
void catalogue_service::do_save_instance(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Save instance " << cmd.instance_row.name << "";
    if (!instance_repo_) {
        raise({condition_source::instance, cmd.instance_row.id},
            "cannot save an instance: the store is not open");
        return;
    }

    /*
     * The row is remembered before it is replaced so that a store failure can
     * put the table back the way it was. A table that shows an instance the
     * store never accepted would be a lie the next start would contradict.
     */
    const auto existing = instances_.find(cmd.instance_row.id);
    const auto previous = existing
        ? std::optional<domain::instance>(*instances_.get(*existing))
        : std::nullopt;

    /*
     * An instance with no folder named lands in the instances tree's root,
     * which is where a row created before folders existed belongs. Without this
     * it would be in no folder at all, and the tree would draw a tree that
     * omitted it.
     */
    domain::instance row = cmd.instance_row;
    if (row.folder_id.empty())
        row.folder_id = std::string{domain::instances_root_id};

    const auto handle = instances_.upsert(row);

    if (auto saved = instance_repo_->save(row); !saved) {
        if (previous)
            static_cast<void>(instances_.upsert(*previous));
        else
            instances_.remove(handle);
        raise({condition_source::instance, row.id},
            store_failure(
                std::format("cannot save instance {}", row.id), saved.error()));
        return;
    }

    /*
     * The instance is stored, so it needs somewhere for its accounts to live.
     * The root is created here rather than lazily on the first account, because
     * a tree with no root is a shape the Browser cannot draw.
     */
    /*
     * The row's own id, not the command's: the row is what was stored, and a
     * caller that named the id only in the command's field would otherwise get
     * an accounts tree rooted at nothing. The two are the same value for every
     * caller that fills both, which is why only one of them is read here.
     */
    if (auto refused =
            seed::ensure_accounts_root(folders_, folder_repo_.get(), row.id))
        raise(std::move(refused->key), std::move(refused->message));
}

void catalogue_service::do_remove_instance(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Remove instance " << cmd.instance_id << "";
    /*
     * The interface names the row by id and the row itself also carries one, so
     * either is accepted rather than dropping a removal that named the
     * instance in the field the other caller used.
     */
    const std::string id = cmd.instance_id.empty() ? cmd.instance_row.id
                                                   : cmd.instance_id;
    if (id.empty()) {
        raise({condition_source::instance, id},
            "cannot remove an instance: no instance was named");
        return;
    }

    removed_ = removal{id, {}, std::nullopt};

    if (const auto handle = instances_.find(id)) {
        removed_->probe = *handle;
        instances_.remove(*handle);
    }

    /*
     * The cascade is a walk rather than a lookup: what the accounts table is
     * asked is which rows name this instance, and its handles are what a
     * removal takes. Each account takes its own shortcuts first, because after
     * the row is released the id it was keyed by is gone.
     */
    for (const auto account : live_accounts(accounts_)) {
        if (accounts_.get(account)->instance_id != id)
            continue;
        const std::string account_id = accounts_.get(account)->id;
        drop_target_shortcuts(domain::window_kind::workspace, account_id);
        accounts_.remove(account);
    }

    /*
     * The accounts window of this instance is a shortcut target of its own, and
     * it goes with the instance for the same reason its accounts do.
     */
    drop_target_shortcuts(domain::window_kind::accounts, id);

    /*
     * The instance's own folders go with it: every root of an accounts tree
     * names the instance as its owner, so the tree is found by the owner rather
     * than by walking the folders.
     */
    {
        const auto accounts_root = folders_.root(domain::folder_tree::accounts, id);
        const auto doomed = accounts_root
            ? folders_.descendants_of(folders_.get(*accounts_root)->id)
            : std::vector<std::string>{};
        if (accounts_root)
            folders_.remove(*accounts_root);
        for (const auto& folder_id : doomed) {
            if (const auto handle = folders_.find(folder_id))
                folders_.remove(*handle);
        }
    }
    if (folder_repo_) {
        if (auto removed = folder_repo_->remove_tree(
                domain::folder_tree::accounts, id);
            !removed) {
            raise({condition_source::instance, id},
                store_failure(
                    std::format("cannot remove the folders of instance {}", id),
                    removed.error()));
        }
    }

    /* A label attachment is the instance's, so it goes with the instance. */
    labellings_.remove_owner(domain::labelling_owner::instance, id);
    if (labelling_repo_) {
        if (auto removed = labelling_repo_->remove_owner(
                domain::labelling_owner::instance, id);
            !removed) {
            raise({condition_source::instance, id},
                store_failure(
                    std::format("cannot remove the labels of instance {}", id),
                    removed.error()));
        }
    }

    if (instance_repo_) {
        if (auto removed = instance_repo_->remove(id); !removed) {
            raise({condition_source::instance, id},
                store_failure(
                    std::format("cannot remove instance {}", id), removed.error()));
        }
    }
    if (account_repo_) {
        if (auto removed = account_repo_->remove_for_instance(id); !removed) {
            raise({condition_source::instance, id},
                store_failure(
                    std::format("cannot remove the accounts of instance {}", id),
                    removed.error()));
        }
    }
}

void catalogue_service::do_save_account(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Save account " << cmd.account_row.principal << "";
    if (!account_repo_) {
        raise({condition_source::account, cmd.account_row.id},
            "cannot save an account: the store is not open");
        return;
    }

    const auto existing = accounts_.find(cmd.account_row.id);
    const auto previous = existing
        ? std::optional<domain::account>(*accounts_.get(*existing))
        : std::nullopt;

    /*
     * An account with no folder named lands in the accounts root of its own
     * instance, which is the one folder every account of that instance may sit
     * in. The root is derived from the instance rather than looked up, so a
     * save that races the root's creation still has somewhere to put the row.
     */
    domain::account row = cmd.account_row;
    /*
     * The flag is not part of a saved row, so an edit that built its value from
     * the form would clear it and make the next sign-in ask for a password the
     * store already holds.
     */
    if (previous)
        row.has_credential = previous->has_credential;
    if (row.folder_id.empty())
        row.folder_id = domain::accounts_root_id_for(row.instance_id);

    const auto handle = accounts_.upsert(row);

    if (auto saved = account_repo_->save(row); !saved) {
        if (previous)
            static_cast<void>(accounts_.upsert(*previous));
        else
            accounts_.remove(handle);
        raise({condition_source::account, row.id},
            store_failure(
                std::format("cannot save account {}", row.id), saved.error()));
    }
}

void catalogue_service::do_remove_account(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Remove account " << cmd.account_id << "";
    const std::string id = cmd.account_id.empty() ? cmd.account_row.id
                                                  : cmd.account_id;
    if (id.empty()) {
        raise({condition_source::account, id},
            "cannot remove an account: no account was named");
        return;
    }

    /*
     * The instance is read out before the row goes, because removing it clears
     * the value the slot held.
     */
    const auto handle = accounts_.find(id);
    const std::string instance_id = handle
        ? accounts_.get(*handle)->instance_id
        : cmd.instance_id;
    removed_ = removal{instance_id, id, std::nullopt};

    if (handle)
        accounts_.remove(*handle);

    /*
     * A shortcut naming this account as its workspace target goes with it. The
     * entry is dropped by the same command that removed the row it opens, which
     * is what stops the bar from offering a surface whose account is gone.
     */
    drop_target_shortcuts(domain::window_kind::workspace, id);

    if (account_repo_) {
        if (auto removed = account_repo_->remove(id); !removed) {
            raise({condition_source::account, id},
                store_failure(
                    std::format("cannot remove account {}", id), removed.error()));
        }
    }
}

void catalogue_service::do_save_folder(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Save folder " << cmd.folder_row.name << "";
    if (!folder_repo_) {
        raise({condition_source::folder, cmd.folder_row.id},
            "cannot save a folder: the store is not open");
        return;
    }

    const auto& incoming = cmd.folder_row;
    if (incoming.id.empty()) {
        raise({condition_source::folder, incoming.id},
            "cannot save a folder: no folder was named");
        return;
    }
    if (incoming.id == domain::instances_root_id) {
        raise({condition_source::folder, incoming.id},
            std::format(
                "cannot re-parent folder {}: it is the root of the instances tree",
                incoming.id));
        return;
    }
    if (incoming.name.empty()) {
        raise({condition_source::folder, incoming.id},
            std::format("cannot save folder {}: it has no name",
                incoming.id));
        return;
    }

    const auto existing = folders_.find(incoming.id);
    const auto previous = existing
        ? std::optional<domain::folder>(*folders_.get(*existing))
        : std::nullopt;

    /*
     * The tree and the owner are a property of where a folder sits, not of the
     * folder, so they come from the parent rather than from the command. Asking
     * the caller to repeat them would be a second place they could be wrong,
     * and getting them wrong would put a folder in a tree it does not belong
     * to.
     */
    domain::folder row = incoming;
    if (const auto parent = folders_.find(incoming.parent_id)) {
        const auto& parent_row = *folders_.get(*parent);
        row.tree = parent_row.tree;
        row.owner_id = parent_row.owner_id;
    } else if (previous) {
        row.tree = previous->tree;
        row.owner_id = previous->owner_id;
    } else {
        raise({condition_source::folder, incoming.id},
            std::format(
                "cannot save folder {}: no parent folder {}", incoming.id,
                incoming.parent_id.empty() ? "<none>" : incoming.parent_id));
        return;
    }
    if (row.parent_id.empty()) {
        raise({condition_source::folder, incoming.id},
            std::format(
                "cannot save folder {}: only a root has no parent", incoming.id));
        return;
    }

    /*
     * A cycle is refused by the table as well, because a table is reachable
     * without this handler. It is reported here too so that the sentence names
     * the folder the operator asked about rather than the id the table saw.
     */
    if (row.id == row.parent_id) {
        raise({condition_source::folder, row.id},
            std::format("cannot parent folder {} to itself", row.id));
        return;
    }

    auto handle = folders_.upsert(row);
    if (!handle) {
        raise({condition_source::folder, row.id}, handle.error());
        return;
    }

    if (auto saved = folder_repo_->save(row); !saved) {
        if (previous)
            static_cast<void>(folders_.upsert(*previous));
        else
            folders_.remove(*handle);
        raise({condition_source::folder, row.id},
            store_failure(
                std::format("cannot save folder {}", row.id), saved.error()));
    }
}

void catalogue_service::do_remove_folder(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Remove folder " << cmd.folder_row.id << "";
    if (!folder_repo_) {
        raise({condition_source::folder, cmd.folder_row.id},
            "cannot remove a folder: the store is not open");
        return;
    }

    const std::string id = cmd.folder_row.id.empty() ? cmd.payload
                                                    : cmd.folder_row.id;
    if (id.empty()) {
        raise({condition_source::folder, id},
            "cannot remove a folder: no folder was named");
        return;
    }

    /*
     * The root of a tree is what the tree is anchored by: removing it would
     * leave every folder below it unreachable, so the refusal is here in the
     * runtime and not only in whatever surfaced the request.
     */
    if (id == domain::instances_root_id) {
        raise({condition_source::folder, id},
            std::format(
                "cannot remove folder {}: it is the root of the instances tree", id));
        return;
    }
    if (const auto handle = folders_.find(id)) {
        if (folders_.get(*handle)->parent_id.empty()) {
            raise({condition_source::folder, id},
                std::format(
                    "cannot remove folder {}: it is a root of its tree", id));
            return;
        }
    }

    /*
     * The children are collected before anything goes, because a removal
     * releases the slot and a walk from a released row would find nothing.
     */
    const auto doomed = folders_.descendants_of(id);
    auto going = doomed;
    going.push_back(id);

    /*
     * Removing a folder removes what is under it and does not take the leaves
     * with it: an operator who deletes a folder wants the folder gone, not
     * their instances deleted. The leaves are re-homed to their tree's root
     * first, which is the one folder every leaf of a tree may sit in, so no
     * leaf is ever left naming a folder that no longer exists.
     */
    rehome_leaves(going);

    if (const auto handle = folders_.find(id))
        folders_.remove(*handle);
    for (const auto& folder_id : doomed) {
        if (const auto handle = folders_.find(folder_id))
            folders_.remove(*handle);
    }

    /*
     * The store is asked for the same cascade by id rather than left to a
     * foreign key: a folder's parent is a folder, so a self-referencing cascade
     * would need a constraint this schema does not carry, and the removal would
     * then depend on the order the rows were walked in.
     */
    if (auto removed = folder_repo_->remove(id); !removed) {
        raise({condition_source::folder, id},
            store_failure(
                std::format("cannot remove folder {}", id), removed.error()));
    }
    for (const auto& folder_id : doomed) {
        if (auto removed = folder_repo_->remove(folder_id); !removed) {
            raise({condition_source::folder, folder_id},
                store_failure(
                    std::format("cannot remove folder {}", folder_id),
                    removed.error()));
        }
    }
}

void catalogue_service::rehome_leaves(const std::vector<std::string>& folder_ids) {
    const auto inside = [&](std::string_view folder_id) {
        return !folder_id.empty()
            && std::find(folder_ids.begin(), folder_ids.end(), folder_id)
                != folder_ids.end();
    };

    const auto root_of = [&](domain::folder_tree tree,
                             std::string_view owner_id) -> std::optional<std::string> {
        const auto root = folders_.root(tree, owner_id);
        if (!root)
            return std::nullopt;
        return folders_.get(*root)->id;
    };

    for (const auto index : live_indices(instances_.layout())) {
        const auto handle = model_state::instance_handle{
            index, instances_.layout().generation[index]};
        const auto* row = instances_.get(handle);
        if (row == nullptr || !inside(row->folder_id))
            continue;

        const auto root = root_of(domain::folder_tree::instances, {});
        if (!root) {
            raise({condition_source::instance, row->id},
                std::format(
                    "the instances tree has no root, so {} cannot be moved out of "
                    "the folder being removed",
                    row->id));
            continue;
        }

        instances_.set_folder(handle, *root);
        if (instance_repo_) {
            if (auto moved = instance_repo_->set_parent(row->id, *root); !moved) {
                raise({condition_source::instance, row->id},
                    store_failure(
                        std::format("cannot move instance {} out of a removed "
                                    "folder",
                            row->id),
                        moved.error()));
            }
        }
    }

    for (const auto index : live_indices(accounts_.layout())) {
        const auto handle = model_state::account_handle{
            index, accounts_.layout().generation[index]};
        const auto* row = accounts_.get(handle);
        if (row == nullptr || !inside(row->folder_id))
            continue;

        const auto root = root_of(domain::folder_tree::accounts, row->instance_id);
        if (!root) {
            raise({condition_source::account, row->id},
                std::format(
                    "instance {} has no accounts root, so account {} cannot be "
                    "moved out of the folder being removed",
                    row->instance_id, row->id));
            continue;
        }

        accounts_.set_folder(handle, *root);
        if (account_repo_) {
            if (auto moved = account_repo_->set_parent(row->id, *root); !moved) {
                raise({condition_source::account, row->id},
                    store_failure(
                        std::format("cannot move account {} out of a removed "
                                    "folder",
                            row->id),
                        moved.error()));
            }
        }
    }
}

void catalogue_service::do_save_label(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Save label " << cmd.label_row.name << "";
    if (!label_repo_) {
        raise({condition_source::label, cmd.label_row.id},
            "cannot save a label: the store is not open");
        return;
    }

    const auto& row = cmd.label_row;
    if (row.id.empty()) {
        raise({condition_source::label, row.id},
            "cannot save a label: no label was named");
        return;
    }
    if (row.name.empty()) {
        raise({condition_source::label, row.id},
            std::format("cannot save label {}: it has no name",
                row.id));
        return;
    }

    const auto existing = labels_.find(row.id);
    const auto previous = existing
        ? std::optional<domain::label>(*labels_.get(*existing))
        : std::nullopt;

    const auto handle = labels_.upsert(row);

    if (auto saved = label_repo_->save(row); !saved) {
        if (previous)
            static_cast<void>(labels_.upsert(*previous));
        else
            labels_.remove(handle);
        raise({condition_source::label, row.id},
            store_failure(
                std::format("cannot save label {}", row.id), saved.error()));
    }
}

void catalogue_service::do_remove_label(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Remove label " << cmd.label_row.id << "";
    if (!label_repo_) {
        raise({condition_source::label, cmd.label_row.id},
            "cannot remove a label: the store is not open");
        return;
    }

    const std::string id = cmd.label_row.id.empty() ? cmd.payload
                                                   : cmd.label_row.id;
    if (id.empty()) {
        raise({condition_source::label, id},
            "cannot remove a label: no label was named");
        return;
    }

    /*
     * A label is removed together with its attachments, in both the table and
     * the store, so no attachment is left naming a label that is gone. The
     * store would cascade on its own; saying it here as well keeps the two
     * agreeing even if a later schema drops the cascade.
     */
    const auto attachments = labellings_.rows();
    if (const auto handle = labels_.find(id))
        labels_.remove(*handle);
    for (const auto& row : attachments) {
        if (row.label_id != id)
            continue;
        labellings_.remove(row.owner, row.owner_id, row.label_id);
        if (auto detached = labelling_repo_->remove(row); !detached) {
            raise({condition_source::label, id},
                store_failure(
                    std::format("cannot detach label {} from {}", id, row.owner_id),
                    detached.error()));
        }
    }

    if (auto removed = label_repo_->remove(id); !removed) {
        raise({condition_source::label, id},
            store_failure(
                std::format("cannot remove label {}", id), removed.error()));
    }
}

void catalogue_service::do_attach_label(const command::command& cmd) {
    if (!labelling_repo_) {
        raise({condition_source::labelling, cmd.labelling_row.owner_id},
            "cannot attach a label: the store is not open");
        return;
    }

    const auto& row = cmd.labelling_row;
    if (row.owner_id.empty() || row.label_id.empty()) {
        raise({condition_source::labelling, row.owner_id},
            "cannot attach a label: the owner and the label must be "
                          "named");
        return;
    }

    /*
     * Both ends are resolved before the attachment is written: an attachment to
     * a row that does not exist is a foreign key the store would refuse, and
     * refusing it here says which end was missing.
     */
    if (row.owner == domain::labelling_owner::instance) {
        if (!instances_.find(row.owner_id)) {
            raise({condition_source::labelling, row.owner_id},
                std::format("cannot attach a label: no instance {}",
                    row.owner_id));
            return;
        }
    } else if (!accounts_.find(row.owner_id)) {
        raise({condition_source::labelling, row.owner_id},
            std::format("cannot attach a label: no account {}",
                row.owner_id));
        return;
    }
    if (!labels_.find(row.label_id)) {
        raise({condition_source::labelling, row.owner_id},
            std::format("cannot attach a label: no label {}",
                row.label_id));
        return;
    }

    /*
     * Attaching a pair that is already attached changes nothing, so a refused
     * write must leave it attached: the store still holds it, and removing it
     * from the table would hide a label the store still holds until the next
     * start.
     */
    const auto attached = labellings_.labels_of(row.owner, row.owner_id);
    const bool already = std::find(attached.begin(), attached.end(),
        row.label_id) != attached.end();
    labellings_.add(row.owner, row.owner_id, row.label_id);

    if (auto saved = labelling_repo_->save(row); !saved) {
        if (!already)
            labellings_.remove(row.owner, row.owner_id, row.label_id);
        raise({condition_source::labelling, row.owner_id},
            store_failure(
                std::format("cannot attach label {} to {}", row.label_id,
                    row.owner_id),
                saved.error()));
    }
}

void catalogue_service::do_detach_label(const command::command& cmd) {
    if (!labelling_repo_) {
        raise({condition_source::labelling, cmd.labelling_row.owner_id},
            "cannot detach a label: the store is not open");
        return;
    }

    const auto& row = cmd.labelling_row;
    if (row.owner_id.empty() || row.label_id.empty()) {
        raise({condition_source::labelling, row.owner_id},
            "cannot detach a label: the owner and the label must be "
                          "named");
        return;
    }

    labellings_.remove(row.owner, row.owner_id, row.label_id);

    if (auto removed = labelling_repo_->remove(row); !removed) {
        raise({condition_source::labelling, row.owner_id},
            store_failure(
                std::format("cannot detach label {} from {}", row.label_id,
                    row.owner_id),
                removed.error()));
    }
}

void catalogue_service::do_save_shortcut(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Save shortcut " << cmd.shortcut_row.id << "";
    if (!shortcut_repo_) {
        raise({condition_source::shortcut, cmd.shortcut_row.id},
            "cannot save a shortcut: the store is not open");
        return;
    }

    const auto& row = cmd.shortcut_row;
    if (row.id.empty()) {
        raise({condition_source::shortcut, row.id},
            "cannot save a shortcut: no shortcut was named");
        return;
    }

    /*
     * The entry is read back before it is replaced, so a store that refuses the
     * write leaves the bar as it was. A bar still offering the previous entry
     * is the truth; one showing an entry no store accepted is a lie the next
     * start would contradict.
     */
    std::optional<domain::shortcut> previous;
    for (const auto& existing : shortcuts_.rows()) {
        if (existing.id == row.id) {
            previous = existing;
            break;
        }
    }

    shortcuts_.upsert(row);

    if (auto saved = shortcut_repo_->save(row); !saved) {
        if (previous)
            shortcuts_.upsert(*previous);
        else
            shortcuts_.remove(row.id);
        raise({condition_source::shortcut, row.id},
            store_failure(
                std::format("cannot save shortcut {}", row.id), saved.error()));
    }
}

void catalogue_service::do_remove_shortcut(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Remove shortcut " << cmd.shortcut_row.id << "";
    if (!shortcut_repo_) {
        raise({condition_source::shortcut, cmd.shortcut_row.id},
            "cannot remove a shortcut: the store is not open");
        return;
    }

    const std::string id = cmd.shortcut_row.id.empty() ? cmd.payload
                                                       : cmd.shortcut_row.id;
    if (id.empty()) {
        raise({condition_source::shortcut, id},
            "cannot remove a shortcut: no shortcut was named");
        return;
    }

    shortcuts_.remove(id);

    if (auto removed = shortcut_repo_->remove(id); !removed) {
        raise({condition_source::shortcut, id},
            store_failure(
                std::format("cannot remove shortcut {}", id), removed.error()));
    }
}

void catalogue_service::drop_target_shortcuts(domain::window_kind kind,
    const std::string& target_id) {
    /*
     * An empty target id is the Instances window and a handful of other kinds
     * that concern no row. Nothing was removed, so nothing is dropped: treating
     * empty as a match would empty the bar every time an instance went.
     */
    if (target_id.empty())
        return;

    shortcuts_.remove_target(kind, target_id);

    if (!shortcut_repo_)
        return;
    if (auto removed = shortcut_repo_->remove_for_target(kind, target_id);
        !removed) {
        raise({condition_source::shortcut, target_id},
            store_failure(
                std::format("cannot remove the shortcuts for target {}", target_id),
                removed.error()));
    }
}

}
