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
#include "hedgr.runtime/service/window_service.hpp"
#include "ores.logging/boost_severity.hpp"
#include "support.hpp"
#include <boost/log/sources/severity_feature.hpp>
#include <chrono>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace hedgr::runtime::service {

namespace {

namespace command = hedgr::model::command;
namespace domain = hedgr::model::domain;
namespace model_state = hedgr::model::state;

using condition_source = model_state::condition_source;
using support::lg;
using support::live_windows;
using support::store_failure;

/** @brief The deadline one policy names, as the window column stores it. */
[[nodiscard]] std::int64_t deadline_for(deadline_policy policy,
    std::chrono::milliseconds cadence) {
    switch (policy) {
    case deadline_policy::due_now:
        return 0;
    case deadline_policy::parked:
        return std::numeric_limits<std::int64_t>::max();
    case deadline_policy::after_cadence: {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now + cadence).count();
    }
    }
    return 0;
}

/**
 * @brief The default kind-specific state for a window opened by kind.
 *
 * The instance and the account are parameters because two kinds cannot be
 * described without them: an accounts window is the accounts of one instance,
 * and a workspace is one account's session. Passing them in rather than
 * reaching for them keeps this function a pure mapping from a command to the
 * state its window starts with.
 */
[[nodiscard]] domain::window_state
default_state_for(domain::window_kind kind, std::string_view last_error,
    std::string_view instance_id, std::string_view account_id,
    std::string_view series_id = {},
    std::string_view series_label = {},
    std::optional<std::int64_t> span_ns = std::nullopt) {
    switch (kind) {
    case domain::window_kind::chart: {
        domain::chart_state chart;
        chart.label = series_label.empty() ? "Series"
                                          : std::string{series_label};
        chart.series_id = std::string{series_id};
        return chart;
    }
    case domain::window_kind::mini_plot: {
        domain::mini_plot_state card;
        card.label = series_label.empty() ? "Series"
                                          : std::string{series_label};
        card.series_id = std::string{series_id};
        if (span_ns)
            card.span_ns = *span_ns;
        return card;
    }
    case domain::window_kind::series_browser:
        return domain::series_browser_state{};
    case domain::window_kind::crm_matrix:
        return domain::crm_matrix_state{};
    case domain::window_kind::term_structure: {
        domain::term_structure_state terms;
        terms.label = series_label.empty() ? "Term structure"
                                           : std::string{series_label};
        terms.curve_key = std::string{series_id};
        return terms;
    }
    case domain::window_kind::accounts: {
        domain::accounts_state accounts;
        accounts.instance_id = std::string{instance_id};
        return accounts;
    }
    case domain::window_kind::workspace: {
        domain::workspace_state session;
        session.account_id = std::string{account_id};
        return session;
    }
    case domain::window_kind::command_centre: {
        domain::command_centre_state centre;
        centre.instance_id = std::string{instance_id};
        return centre;
    }
    case domain::window_kind::grid: {
        domain::grid_state grid;
        grid.resource = "trades";
        return grid;
    }
    case domain::window_kind::notice:
    case domain::window_kind::instance_manager: {
        /*
         * The session table keeps the friendly sentence and nothing else, so a
         * notice opened after a failure shows the sentence and leaves the
         * dimmed low-level line empty rather than inventing one.
         */
        domain::notice_state notice;
        notice.title = "Notice";
        if (!last_error.empty()) {
            notice.title = "Session error";
            notice.text = std::string(last_error);
        }
        return notice;
    }
    }
    return domain::notice_state{};
}

}

std::string window_key_for(domain::window_kind kind,
    std::string_view instance_id, std::string_view account_id,
    std::string_view series_id, std::string_view crm_name,
    std::string_view party_id) {
    switch (kind) {
    case domain::window_kind::instance_manager:
        return "instance-manager";
    case domain::window_kind::accounts:
        return std::format("accounts:{}", instance_id);
    case domain::window_kind::workspace:
        return std::format("workspace:{}:{}", instance_id, account_id);
    case domain::window_kind::command_centre:
        return std::format("command-centre:{}:{}", instance_id, account_id);
    case domain::window_kind::series_browser:
        return std::format("series-browser:{}:{}", instance_id, account_id);
    case domain::window_kind::chart:
        /*
         * One chart per series per session. Reopening GBP/USD as the same
         * login focuses the plot already drawn; another login on the same
         * deployment is another session and gets its own window, with its own
         * identity bar and its own token.
         */
        return series_id.empty()
            ? std::string{}
            : std::format("chart:{}:{}:{}", instance_id, account_id, series_id);
    case domain::window_kind::mini_plot:
        /* One card per series per session, as for a chart (plan D4). */
        return series_id.empty()
            ? std::string{}
            : std::format("mini:{}:{}:{}", instance_id, account_id, series_id);
    case domain::window_kind::term_structure:
        return series_id.empty()
            ? std::string{}
            : std::format("terms:{}:{}:{}", instance_id, account_id, series_id);
    case domain::window_kind::crm_matrix:
        return std::format("crm:{}:{}:{}:{}", instance_id, account_id, party_id,
            crm_name);
    case domain::window_kind::grid:
    case domain::window_kind::notice:
        break;
    }
    return {};
}

window_service::window_service(const model_state::session_table& sessions,
    model_state::series_table& series)
    : sessions_(sessions), series_(series) {
}

void window_service::hold_series(model_state::window_handle window,
    std::string_view instance_id, std::string_view account_id) {
    const auto& shape = windows_.state(window);
    std::string_view series_id;
    if (const auto* chart = std::get_if<domain::chart_state>(&shape))
        series_id = chart->series_id;
    else if (const auto* card = std::get_if<domain::mini_plot_state>(&shape))
        series_id = card->series_id;
    if (series_id.empty())
        return;

    const auto wanted = series_.intern(instance_id, account_id, series_id);
    const auto held = windows_.series(window);
    if (held.size() == 1 && held.front() == wanted)
        return;

    /* Acquire before release, so a series the window keeps never reaches zero. */
    series_.acquire(wanted);
    drop_series(window);
    windows_.set_series(window, std::span{&wanted, 1});
}

void window_service::drop_series(model_state::window_handle window) {
    for (const auto held : windows_.series(window))
        series_.release(held);
    windows_.set_series(window, {});
}

void window_service::release_scope(std::string_view instance_id,
    std::string_view account_id) {
    for (const auto window : live_windows(windows_)) {
        const auto held = windows_.series(window);
        if (held.empty())
            continue;
        std::vector<model_state::series_handle> kept;
        for (const auto handle : held) {
            const auto key = series_.key_of(handle);
            const bool inside = key && key->instance_id == instance_id
                && (account_id.empty() || key->account_id == account_id);
            if (inside)
                series_.release(handle);
            else
                kept.push_back(handle);
        }
        windows_.set_series(window, kept);
    }
}

void window_service::attach(store::sqlite::database& database) {
    window_repo_ = std::make_unique<store::repository::window_repository>(
        database);
}

std::expected<void, std::string> window_service::load() {
    auto windows = window_repo_->list();
    if (!windows) {
        return std::unexpected(store_failure("cannot read the stored windows",
            windows.error()));
    }
    for (const auto window : live_windows(windows_))
        drop_series(window);
    windows_.clear();
    for (const auto& spec : windows.value()) {
        const auto handle = windows_.insert(spec);
        if (spec.session) {
            hold_series(handle, spec.session->instance_id,
                spec.session->account_id);
        }

        /*
         * Sessions are not persisted, so on a cold start the bind below finds
         * nothing and the window comes back unbound. That is correct rather
         * than a gap: a session is a live connection, and a window that
         * outlives one renders the session-loss state.
         */
        if (!spec.session)
            continue;
        if (const auto session = sessions_.find(spec.session->instance_id,
                spec.session->account_id)) {
            windows_.bind_session(handle, *session);
        }
    }
    return {};
}

void window_service::ensure_instance_manager_window() {
    if (windows_.find("instance-manager"))
        return;

    domain::window_spec spec;
    spec.id = "instance-manager";
    spec.kind = domain::window_kind::instance_manager;
    spec.cadence = traits_of(spec.kind).cadence;

    /*
     * No session, and never one: the Instance Manager is the root of the
     * interface and is the surface a person uses to open a session in the first
     * place.
     */
    spec.session = std::nullopt;

    /*
     * The handle is kept because the deadline has to be written with it. Its
     * stable id is how everything else finds the window; nothing binds to it.
     */
    const auto handle = windows_.insert(spec);
    windows_.set_next_refresh_ns(handle,
        deadline_for(traits_of(spec.kind).on_open, spec.cadence));
}

bool window_service::show_existing(std::string_view window_id) {
    const auto existing = windows_.find(window_id);
    if (!existing)
        return false;
    windows_.set_open(*existing, true);
    return true;
}

open_outcome window_service::open(const command::command& cmd) {
    open_outcome outcome;
    if (!window_repo_) {
        outcome.refused = refusal{{condition_source::window, {}},
            "cannot open a window: the store is not open"};
        return outcome;
    }
    const auto& traits = traits_of(cmd.new_window_kind);

    /*
     * A window whose id is derived from what it shows is opened by focusing the
     * one that already exists. That is what makes two requests for the same
     * subject one window, and it is the reason the ids of those kinds are
     * functions rather than serials.
     */
    const auto key = window_key_for(cmd.new_window_kind, cmd.instance_id,
        cmd.account_id, cmd.series_id, cmd.crm_name, cmd.party_id);
    if (!key.empty()) {
        if (const auto existing = windows_.find(key)) {
            windows_.set_open(*existing, true);
            windows_.request_focus(key);

            /*
             * A curve's membership is the catalogue's answer and it can change
             * between openings: the window is keyed by the row that was
             * clicked, not by the rungs that row belongs to. Reopening a curve
             * therefore restates its members, or the window keeps drawing the
             * ladder it was first given - one rung, forever, however the
             * grouping improves.
             */
            if (traits.refresh_read == read_kind::term_structure
                && !cmd.member_ids.empty()) {
                if (auto* terms = std::get_if<domain::term_structure_state>(
                        &windows_.state(*existing))) {
                    if (terms->member_ids != cmd.member_ids
                        || terms->member_tenors != cmd.member_tenors) {
                        terms->member_ids = cmd.member_ids;
                        terms->member_tenors = cmd.member_tenors;
                        terms->label = cmd.series_label;
                        BOOST_LOG_SEV(lg(), ores::logging::info)
                            << "Curve window '" << key << "' reopened with "
                            << cmd.member_ids.size() << " rungs";
                    }
                }
            }

            /*
             * A window restored from the store is open and unbound: the
             * session it was saved against does not exist at start. A sign-in
             * that asks for that window again is the moment the binding can be
             * made, and without it the window draws the session-loss state
             * forever while its session is live beside it.
             */
            if (const auto session =
                    sessions_.find(cmd.instance_id, cmd.account_id)) {
                windows_.bind_session(*existing, *session);
                hold_series(*existing, cmd.instance_id, cmd.account_id);

                /*
                 * The restored window's first refresh ran while it had no
                 * session, so its deadline has already moved a cadence out.
                 * Binding makes the read possible, and asking for it now is
                 * what makes the window show the deployment rather than sit on
                 * "reading" until the next tick.
                 */
                windows_.set_next_refresh_ns(*existing,
                    deadline_for(traits.on_focus, traits.cadence));
                if (traits.reads_on_open)
                    outcome.read = read_request{traits.refresh_read, *existing};
            }
            return outcome;
        }
    }

    domain::window_spec spec;
    spec.id = key.empty() ? std::format("w-{}", next_window_serial_++)
                          : key;
    spec.kind = cmd.new_window_kind;
    spec.cadence = traits.cadence;

    std::string_view last_error;
    if (const auto session = sessions_.find(cmd.instance_id, cmd.account_id))
        last_error = sessions_.last_error(*session);
    spec.state = default_state_for(cmd.new_window_kind, last_error,
        cmd.instance_id, cmd.account_id, cmd.series_id, cmd.series_label,
        cmd.series_span_ns);
    if (cmd.new_window_kind == domain::window_kind::crm_matrix) {
        auto& crm = std::get<domain::crm_matrix_state>(spec.state);
        crm.crm_name = cmd.crm_name;
        if (!cmd.party_id.empty()) {
            crm.party_id = cmd.party_id;
        } else if (const auto session = sessions_.find(cmd.instance_id,
                       cmd.account_id)) {
            crm.party_id = std::string{sessions_.party_id(*session)};
        }
    }
    if (cmd.new_window_kind == domain::window_kind::term_structure) {
        auto& terms = std::get<domain::term_structure_state>(spec.state);
        terms.member_ids = cmd.member_ids;
        terms.member_tenors = cmd.member_tenors;
    }

    if (const auto session = sessions_.find(cmd.instance_id, cmd.account_id))
        spec.session = domain::session_key{std::string{cmd.instance_id},
            std::string{cmd.account_id}};

    const auto handle = windows_.insert(spec);

    /*
     * A window opened from a session is bound to it, so the frame loop can
     * reach its state without a key lookup per window. A window opened with no
     * session named stays unbound and renders the session-loss state.
     */
    if (const auto session = sessions_.find(cmd.instance_id, cmd.account_id)) {
        windows_.bind_session(handle, *session);
        hold_series(handle, cmd.instance_id, cmd.account_id);
    }

    windows_.set_next_refresh_ns(handle,
        deadline_for(traits.on_open, traits.cadence));
    if (traits.reads_on_open)
        outcome.read = read_request{traits.refresh_read, handle};

    if (auto saved = window_repo_->save(windows_.spec(handle, sessions_)); !saved) {
        outcome.refused = refusal{{condition_source::window, spec.id},
            store_failure(
                std::format("cannot save window {}", spec.id), saved.error())};
    }

    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Opened window '" << spec.id << "' of kind '"
        << domain::to_string(spec.kind) << "'";
    return outcome;
}

open_outcome window_service::open_landing(std::string_view instance_id,
    std::string_view account_id, domain::window_kind kind) {
    command::command request;
    request.kind = command::command_kind::open_window;
    request.instance_id = std::string{instance_id};
    request.account_id = std::string{account_id};
    request.new_window_kind = kind;
    return open(request);
}

std::optional<refusal>
window_service::close(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Closing window '" << cmd.window_id << "'";
    const auto handle = windows_.find(cmd.window_id);
    if (!handle) {
        return refusal{{condition_source::window, cmd.window_id},
            std::format("cannot close: no window {}", cmd.window_id)};
    }

    drop_series(*handle);
    windows_.remove(*handle);

    if (window_repo_) {
        if (auto removed = window_repo_->remove(cmd.window_id); !removed) {
            return refusal{{condition_source::window, cmd.window_id},
                store_failure(
                    std::format("cannot remove window {}", cmd.window_id),
                    removed.error())};
        }
    }
    return std::nullopt;
}

std::optional<read_request>
window_service::refresh(const command::command& cmd) {
    const auto handle = windows_.find(cmd.window_id);
    if (!handle)
        return std::nullopt;

    /*
     * The cadence comes from the window's own persisted description, read back
     * through spec(), rather than from a column: the live table carries what
     * the frame loop reads every frame, and the cadence is not one of those.
     */
    const auto spec = windows_.spec(*handle, sessions_);
    const auto& traits = traits_of(spec.kind);
    windows_.set_next_refresh_ns(*handle,
        deadline_for(traits.on_refresh, spec.cadence));
    return read_request{traits.refresh_read, *handle};
}

bool window_service::configure_crm(const command::command& cmd) {
    const auto handle = windows_.find(cmd.window_id);
    if (!handle)
        return false;
    auto* crm = std::get_if<domain::crm_matrix_state>(
        &windows_.state(*handle));
    if (crm == nullptr)
        return false;
    crm->party_id = cmd.party_id;
    crm->crm_name = cmd.crm_name;
    crm->reciprocal = cmd.crm_reciprocal;
    return true;
}

bool window_service::configure_chart(const command::command& cmd) {
    const auto handle = windows_.find(cmd.window_id);
    if (!handle)
        return false;
    auto* chart = std::get_if<domain::chart_state>(&windows_.state(*handle));
    if (chart == nullptr || chart->view == cmd.chart_view)
        return false;
    chart->view = cmd.chart_view;
    return true;
}

void window_service::rebind(std::string_view instance_id,
    std::string_view account_id, model_state::session_handle session) {
    const auto& layout = windows_.layout();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;

        const model_state::window_handle window{
            index, layout.generation[index]};
        if (windows_.session_of(window))
            continue;

        const auto spec = windows_.spec(window, sessions_);
        if (!spec.session)
            continue;
        if (spec.session->instance_id != instance_id
            || spec.session->account_id != account_id) {
            continue;
        }

        windows_.bind_session(window, session);
        hold_series(window, instance_id, account_id);
        windows_.set_next_refresh_ns(window,
            deadline_for(traits_of(spec.kind).on_rebind, spec.cadence));
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Rebound restored window '" << spec.id << "' to '"
            << instance_id << "' / '" << account_id << "'";
    }
}

namespace {

/**
 * @brief The catalogue row a restored window's label names now, or nullptr.
 *
 * Two providers list the same instrument, so a reference provider's copy is
 * passed over: it defines the instrument and never ticks, and a window
 * re-pointed at one would be as dead as the row it came from.
 */
[[nodiscard]] const domain::market_series_row* row_named(
    const model_state::series_catalogue& catalogue, std::string_view label,
    std::string_view party_id) {
    const domain::market_series_row* fallback = nullptr;
    for (const auto& row : catalogue.series) {
        if (row.label != label)
            continue;
        if (!party_id.empty() && !row.party_id.empty()
            && row.party_id != party_id)
            continue;
        for (const auto& provider : catalogue.providers) {
            if (provider.code == row.provider_code
                && domain::is_reference_provider(provider))
                fallback = &row;
        }
        if (fallback != &row)
            return &row;
    }
    return fallback;
}

}

void window_service::resolve_series(std::string_view instance_id,
    std::string_view account_id) {
    const auto* catalogue = series_.catalogue(instance_id, account_id);
    if (catalogue == nullptr
        || catalogue->state != model_state::catalogue_state::ready)
        return;
    const auto session = sessions_.find(instance_id, account_id);
    if (!session)
        return;
    const auto party = sessions_.party_id(*session);

    for (const auto window : live_windows(windows_)) {
        if (windows_.session_of(window) != session)
            continue;
        auto& shape = windows_.state(window);
        std::string* series_id = nullptr;
        const std::string* label = nullptr;
        if (auto* chart = std::get_if<domain::chart_state>(&shape)) {
            series_id = &chart->series_id;
            label = &chart->label;
        } else if (auto* card = std::get_if<domain::mini_plot_state>(&shape)) {
            series_id = &card->series_id;
            label = &card->label;
        }
        if (series_id == nullptr || series_id->empty())
            continue;
        if (catalogue->row_of(*series_id) != nullptr)
            continue;
        const auto* row = row_named(*catalogue, *label, party);
        if (row == nullptr) {
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Restored window '" << windows_.id(window) << "' names "
                << "series '" << *series_id << "' ('" << *label
                << "') that the catalogue does not list and no label matches";
            continue;
        }
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Restored window '" << windows_.id(window) << "' re-pointed from "
            << "series '" << *series_id << "' to '" << row->id << "' ('"
            << *label << "')";
        *series_id = row->id;
        hold_series(window, instance_id, account_id);
    }
}

void window_service::unbind(model_state::session_handle session) {
    for (const auto window : live_windows(windows_)) {
        if (windows_.session_of(window) == session)
            windows_.clear_session(window);
    }
}

namespace {

std::optional<refusal> save_spec(store::repository::window_repository& repo,
    const domain::window_spec& spec) {
    if (auto saved = repo.save(spec); !saved) {
        return refusal{{condition_source::window_wall, {}},
            store_failure(
                std::format("cannot save window {}", spec.id), saved.error())};
    }
    return std::nullopt;
}

}

std::optional<refusal> window_service::save_windows() {
    for (const auto& spec : windows_.specs(sessions_)) {
        if (auto refused = save_spec(*window_repo_, spec))
            return refused;
    }
    return std::nullopt;
}

std::optional<refusal> window_service::save_window(std::string_view window_id) {
    const auto handle = windows_.find(window_id);
    if (!handle)
        return std::nullopt;
    return save_spec(*window_repo_, windows_.spec(*handle, sessions_));
}

}
