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
#ifndef HEDGR_UI_RENDER_RENDERER_HPP
#define HEDGR_UI_RENDER_RENDERER_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/state/handle.hpp"
#include "hedgr.model/state/image_cache.hpp"
#include "hedgr.model/state/tenant_image_map.hpp"
#include "hedgr.model/state/series_table.hpp"
#include "hedgr.model/state/crm_table.hpp"
#include "hedgr.model/state/term_structure_table.hpp"
#include "hedgr.model/state/probe_table.hpp"
#include "hedgr.model/state/password_policy_state.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.ui/render/chart_plot.hpp"
#include "hedgr.ui/render/term_ladder.hpp"
#include "hedgr.ui/render/crm_grid.hpp"
#include "hedgr.ui/render/market_view.hpp"
#include "hedgr.ui/widget/browser_state.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include "hedgr.ui/widget/panel_defaults.hpp"
#include "hedgr.ui/export.hpp"
#include <imgui.h>
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace hedgr::ui::render {

/**
 * @brief The instance pictures the backend has already uploaded, by key.
 *
 * The interface names an illustration by the key an instance carries, and the
 * decode and the upload belong to the component that owns the graphics context.
 * This is the seam between the two: a plain table of keys beside texture ids,
 * filled once when the graphics are initialised, so a frame only ever reads it
 * and never decodes an image on the path that draws.
 *
 * It is a flat vector rather than a map because the set is the ten committed
 * illustrations and a walk of ten beats hashing a string on every item of every
 * frame.
 */
struct instance_icon_textures final {
    /** @brief One illustration, as the key an instance names and its texture. */
    struct entry final {
        std::string key;
        std::uint64_t texture = 0;
    };

    /** @brief Every illustration that was found and uploaded. */
    std::vector<entry> entries;

    /**
     * @brief The directory the pictures were read from, or empty.
     *
     * Carried rather than looked up again, because the pane shows the operator
     * where to put a file and the answer has to be the directory this table was
     * actually filled from. A second lookup could resolve to a different
     * candidate and point at a folder the running interface is not using.
     */
    std::string directory;

    /**
     * @brief The texture for @p key, or zero when there is none.
     *
     * Zero is the ordinary answer for an instance with no illustration and for
     * a key whose asset is not installed, and the caller draws its own fallback
     * for both. There is deliberately no missing-texture texture here: a
     * placeholder would be a picture of a picture, and a reader would have to
     * learn what it meant.
     */
    [[nodiscard]] std::uint64_t find(std::string_view key) const noexcept {
        for (const auto& row : entries) {
            if (row.key == key)
                return row.texture;
        }
        return 0;
    }
};

/**
 * @brief The in-place edit of one instance, in the details pane.
 *
 * The pane is the form, so its drafts live here rather than in a dialog. They
 * are kept apart from the create form's own drafts because the create form can
 * be opened over the pane and neither may overwrite the other: a create form
 * cancelled after an edit was begun would otherwise leave the edit showing the
 * new row's blanks.
 *
 * The target is the row the drafts belong to. The pane compares the drafts
 * against that row every frame, which is what decides which fields are marked
 * as changed and whether leaving would lose anything.
 */
struct instance_edit_state final {
    /** @brief Whether the pane is editable. */
    bool active = false;

    /**
     * @brief Whether the drafts still have to be read from the store.
     *
     * The toolbar callback that begins an edit is handed no tables, so it can
     * only name the row. The fill happens on the next frame the pane draws,
     * where the row is readable, and stays pending until the store holds it.
     */
    bool pending_fill = false;

    /** @brief The row being edited. */
    std::string target;

    /** @brief The drafts, one per editable field. */
    std::array<char, 128> name{};
    std::array<char, 128> host{};
    std::array<char, 16> port{};
    std::array<char, 128> subject{};
    std::array<char, 64> wire_format{};
    std::array<char, 256> tls_ca{};
    std::array<char, 256> tls_cert{};
    std::array<char, 256> tls_key{};
    std::array<char, 128> labels{};

    /*
     * Where the operator was trying to go when the edited row still held
     * changes, held while the warning is up. The selection is put back on the
     * edited row so the pane keeps showing what the warning is about, and these
     * are the click that was held back for the operator to confirm.
     */
    std::string pending_leaf;
    std::string pending_folder;

    /** @brief Whether the leave warning is up. */
    bool confirm_discard = false;
};

/**
 * @brief The interface's own scratch state for one window.
 *
 * Not domain state: none of it is persisted and none of it is read by the
 * runtime. It is what a person has typed but not yet submitted, plus the
 * viewport a table is currently scrolled and filtered to.
 *
 * The buffers are fixed-size arrays rather than std::string because they are
 * handed straight to ImGui::InputText, which edits a caller-owned buffer in
 * place and needs a bound to write within.
 */
/**
 * @brief The interface's scratch for one command centre.
 *
 * Keyed by window, not by screen: two command centres for two deployments each
 * keep their own tabs and their own selection, which is why this lives in the
 * window's state rather than in a table beside the screens.
 */
struct command_centre_ui_state final {
    /** @brief The open screens, in the order the tab strip shows them. */
    std::vector<std::string> screens{"overview"};

    /** @brief The screen a request asked for, empty when none did. */
    std::string active{"overview"};

    /**
     * @brief Whether the next frame should select @c active.
     *
     * ImGui owns which tab is showing. Opening a screen from a menu has to
     * override that once, and only once, or the operator could never switch
     * tabs by hand again.
     */
    bool select_next = false;

    /**
     * @brief Whether the window has already reported a gate it is showing.
     *
     * The renderer runs every frame, so a sentence written on each of them
     * would bury the log. One report per transition is what an operator
     * reading it afterwards is looking for.
     */
    bool bootstrap_reported = false;
    bool session_loss_reported = false;
};

struct window_ui_state final {
    /** @brief Filter text, as typed. */
    std::array<char, 128> filter{};

    /** @brief Whether the detail panel is expanded. */
    bool show_detail = false;

    /** @brief The selected row of the window's primary table, or -1. */
    int selected_row = -1;

    /** @brief The party whose CRM rates are shown. */
    std::string crm_party_id;

    /** @brief The CRM filter, or empty for every CRM. */
    std::string crm_name;

    /** @brief Whether to ask for reciprocal backfill. */
    bool crm_reciprocal = false;

    /** @brief The pair selected in the matrix, empty when none. */
    std::string crm_pair;

    /** @brief Whether the CRM window has taken its scope from its state. */
    bool crm_initialised = false;

    /** @brief Every currency seen since the party was chosen. */
    std::set<std::string> crm_currencies;

    /** @brief The matrix as last derived from the CRM batch. */
    crm_grid crm_view;

    /** @brief Whether the CRM picker popup is open. */
    bool crm_picker_open = false;

    /** @brief Whether this opening of the picker has asked for the names. */
    bool crm_picker_requested = false;

    /** @brief Stable id of a row the user asked to remove, awaiting confirm. */
    std::string pending_remove_id;

    /** @brief Principal of a connection being typed, awaiting submit. */
    std::string draft_principal;

    /** @brief Password of a connection being typed, awaiting submit. */
    std::array<char, 128> draft_password{};

    /** @brief Whether the password is drawn in the clear. */
    bool password_visible = false;

    /**
     * @brief This window's browser: its selection, its filter and its forms.
     *
     * The browser is the same widget in both surfaces that host it, so its
     * scratch state is a member of every window's state rather than a second
     * map keyed by window id. A window that does not host a browser simply
     * never reads it.
     */
    hedgr::ui::widget::browser_state browser;

    /**
     * @brief Whether a collapsible panel is open.
     *
     * This is the starting value and it is not what opens a panel: a collapsing
     * header keeps its own state under the header's id and overwrites this on
     * the first frame it draws. What opens one is the header being preceded by
     * @c ImGui::SetNextItemOpen, fed from @c panel_default_open -- see
     * @c panel_defaults.hpp for why the initialiser alone does nothing.
     */
    bool panel_open = hedgr::ui::widget::panel_default_open(
        hedgr::ui::widget::panel_kind::session_facts);

    /*
     * The fields an instance or an account form edits.
     *
     * Fixed-size arrays rather than std::string, because they are handed
     * straight to ImGui::InputText, which edits a caller-owned buffer in place
     * and needs a bound to write within. They live here rather than in the
     * browser's own state because what a form collects is the host's business:
     * the browser knows a name and nothing else about an instance.
     */
    std::array<char, 128> draft_name{};
    std::array<char, 128> draft_host{};
    std::array<char, 16> draft_port{};
    std::array<char, 128> draft_subject{};
    std::array<char, 64> draft_wire_format{};

    /*
     * The three TLS paths, wider than the others because a path is longer than
     * a name or an address and an operator who cannot see the whole of what
     * they typed cannot check it.
     */
    std::array<char, 256> draft_tls_ca{};
    std::array<char, 256> draft_tls_cert{};
    std::array<char, 256> draft_tls_key{};

    /**
     * @brief The in-place edit of the Instances window's selected row.
     *
     * A member of every window's state rather than of the browser's, because
     * the browser knows a name and a folder and the fields here are the host's.
     * A window that does not host the Instances pane simply never reads it.
     */
    instance_edit_state instance_edit;

    /** @brief This window's tabs, if it is a command centre. */
    command_centre_ui_state command_centre;

    /**
     * @brief Whether the series chooser lists only synthetic series.
     *
     * Off by default: a deployment may hold only vendor series, and a chooser
     * that opens empty because of a filter nobody set reads as a chooser with
     * no series at all.
     */
    bool market_synthetic_only = false;

    /**
     * @brief The party whose series this window lists.
     *
     * Empty until the window resolves it, which it does from the session's own
     * party. The deployment holds one series per party for the same URI, so a
     * tenant-wide list repeats an instrument once per party under the same
     * label; this window shows one party at a time. Changing it is the active
     * choice to look at another party, and it is a choice about this window,
     * not about the session.
     */
    std::string market_party_id;

    /**
     * @brief The asset class the chooser shows, or empty for all.
     *
     * Choosing IR switches the list from points to term structures: the
     * per-tenor series of one curve collapse into one row.
     */
    std::string market_asset_class;

    /**
     * @brief This window's market-data choices.
     *
     * A window keeps its own choices. They are seeded from the frame's
     * remembered defaults when the window first draws, and a change here
     * updates those defaults for the next window without disturbing the
     * windows already open.
     */
    market_view_state market_view;

    /** @brief A chart window's plot arrays, rebuilt when its points change. */
    chart_plot plot;

    /** @brief A term structure window's ladder, rebuilt when its read changes. */
    term_ladder ladder;

    /** @brief The series last chosen in the chooser's combobox. */
    std::string market_selected_series_id;
    std::string market_selected_series_label;

    /** @brief The curve last chosen in the rates view. */
    std::string market_selected_curve_key;
    std::string market_selected_curve_label;
};

/**
 * @brief Everything a renderer is allowed to read, and the one thing it may do.
 *
 * The tables are const because the interface never mutates state: a renderer
 * reacts to a click by pushing a command onto the queue and the runtime applies
 * it. That is what keeps rendering free of side effects and the state tables
 * owned by a single writer.
 */
/**
 * @brief The uploaded image textures a frame may draw.
 *
 * The interface library owns no graphics context. The application implements
 * this over its texture cache, keyed by tenant and image identifier, so the
 * same seam serves flags, logos and photographs.
 */
struct image_texture_source {
    virtual ~image_texture_source() = default;

    /**
     * @brief The texture for an image, uploading it if needed.
     *
     * Marks the texture as used by this frame. The bytes travel here because
     * only the caller holds the byte cache; the source owns the graphics
     * context and nothing else.
     */
    [[nodiscard]] virtual ImTextureID get(std::string_view tenant_id,
        std::string_view image_id, std::string_view mime_type,
        const std::vector<std::uint8_t>& data) = 0;
};

/**
 * @brief Everything a renderer is allowed to read, and the one thing it may do.
 *
 * The tables are const because the interface never mutates state: a renderer
 * reacts to a click by pushing a command onto the queue and the runtime applies
 * it. That is what keeps rendering free of side effects and the state tables
 * owned by a single writer. The image cache is the deliberate exception: asking
 * it for an image records the hit, which is a cache statistic and not model
 * state.
 */
struct render_input final {
    /** @brief The instances known to this process. */
    const hedgr::model::state::instance_table& instances;

    /** @brief The accounts known to this process. */
    const hedgr::model::state::account_table& accounts;

    /** @brief The folders of every tree, including each tree's root. */
    const hedgr::model::state::folder_table& folders;

    /** @brief The labels an operator has declared. */
    const hedgr::model::state::label_table& labels;

    /** @brief The label attachments. */
    const hedgr::model::state::labelling_table& labellings;

    /** @brief The live sessions. */
    const hedgr::model::state::session_table& sessions;

    /** @brief The market series, their catalogues and the points each holds. */
    const hedgr::model::state::series_table& market;

    /** @brief The latest term structure each browser session read. */
    const hedgr::model::state::term_structure_table& term_structures;

    /** @brief The latest CRM batch each session and party read. */
    const hedgr::model::state::crm_table& crm;

    /** @brief Which image each tenant currency names. */
    const hedgr::model::state::tenant_image_map& currency_images;

    /** @brief The bounded cache of image bytes, by tenant and identifier. */
    hedgr::model::state::image_cache& images;

    /** @brief Images whose read failed, keyed by tenant and identifier. */
    const std::unordered_set<std::string>& failed_images;

    /** @brief Images whose read is in flight, keyed the same way. */
    const std::unordered_set<std::string>& pending_images;

    /** @brief The uploaded textures, or nullptr in a test. */
    image_texture_source* textures = nullptr;

    /**
     * @brief The probes' last results and next deadlines.
     *
     * Read-only like every other table here, and read for the colour of each
     * row's icon and -- through the tooltip it carries -- why that colour.
     */
    const hedgr::model::state::probe_table& probes;

    /**
     * @brief What the deployment said a new password must satisfy.
     *
     * Read by the create prompt, which is the one surface drawn while the store
     * is shut and therefore the one with no catalogue row to look a policy up
     * on. An unloaded state is not an empty policy: the first means strength
     * cannot be assessed, and the second would be a claim about a deployment
     * that never made it.
     */
    const hedgr::model::state::password_policy_state& password_policy;

    /** @brief The live windows, including the one being drawn. */
    const hedgr::model::state::window_table& windows;

    /**
     * @brief What the operator has put on the shortcuts bar.
     *
     * Read for the bar and for the control that adds to it, both of which ask
     * the same question of the same table. It is the store's own order rather
     * than one the interface imposes, so the entries are drawn in the order the
     * table returns them.
     */
    const hedgr::model::state::shortcut_table& shortcuts;

    /** @brief Where a renderer puts work for the runtime. */
    hedgr::model::command::command_queue& commands;

    /**
     * @brief Where the master password stands.
     *
     * The whole of the local store sits behind it, so this says whether the
     * catalogue may be read and not only whether the credentials may be: while
     * it is not @c unlocked the Instances window is still drawn, and what is
     * behind its rows is blanked. See @c shows_instances.
     */
    hedgr::model::domain::store_lock store_lock =
        hedgr::model::domain::store_lock::absent;

    /**
     * @brief Whether a derivation is running on the runtime's pool right now.
     *
     * The key derivation is deliberately slow, so a prompt that shows nothing
     * for half a second reads as a hung application. This is what lets it say
     * it is working.
     */
    bool unlocking = false;

    /**
     * @brief Why the last lock attempt was refused, or empty.
     *
     * Shown beside the prompt rather than in a failure banner: a mistyped
     * password is not a store failure, and an operator who is told the store
     * broke will look in the wrong place.
     */
    std::string lock_message;

    /** @brief The frame's monotonic clock, in nanoseconds. */
    std::int64_t now_ns = 0;

    /**
     * @brief The photographic background, as a backend texture id.
     *
     * Zero means there is none to draw. It travels as a plain integer so this
     * header does not have to include ImGui: the drawing code, which does
     * include it, casts it to the texture id type the backend expects.
     */
    std::uint64_t background_texture = 0;

    /**
     * @brief Why this process has no store, or empty.
     *
     * The one failure that leaves nothing else to draw. When the store cannot
     * be opened the window table is empty, so every surface the frame walks is
     * absent and the screen is the background and nothing else; this is the
     * reason the frame has to report on its own, with no window row and no
     * status bar to carry it. A sentence rather than a code, because it is
     * read by the person who has to act on it, and it already names the file
     * and the remedy.
     *
     * A condition and not history: it holds for as long as this process is
     * running without a store, which is until it is restarted.
     */
    std::string startup_failure;

    /**
     * @brief The runtime's latest failure, or empty.
     *
     * The running note @c instance_manager keeps: a save or a removal the
     * store refused, a root that may not be removed, a credential that would
     * not open, and the store failing to come up in the first place. Every one
     * of those is a thing the operator did and has to be told about, so the
     * frame raises it on the status bar as an error rather than letting it sit
     * in a log nobody on this host reads.
     *
     * It clears when the runtime clears it, which a successful unlock does: a
     * failure that has been overtaken by the store opening is not news any
     * more, and the bar is taken down with it.
     */
    std::string runtime_error;

    /**
     * @brief The instance pictures the graphics component has uploaded, or
     * null.
     *
     * A pointer rather than a reference because the frame owns the table that
     * holds them in the application component, and a run with no graphics
     * context, such as a unit test, simply passes null and draws a fallback.
     * Every read goes through @c instance_icon_texture, which answers zero for
     * a null table, so no caller has to write the guard.
     */
    const instance_icon_textures* instance_icons = nullptr;

    /**
     * @brief The choices the market-data screens share and remember.
     *
     * A reference rather than a value because the frame owns it: the choices
     * outlive a window, and the next chart or curve window opens with the ones
     * the last window left. It is interface scratch, not domain state, so a
     * renderer may write it in place.
     */
    market_view_state& market_view;

    /**
     * @brief The wall clock for judging how old an observation is.
     *
     * The frame's @c now_ns is monotonic, which is the right clock for deadlines
     * and fades and the wrong one for comparing against an observation stamped
     * on the market's own clock.
     */
    std::int64_t wall_now_ns = 0;
};

/**
 * @brief Draws one window's contents inside an already-begun ImGui window.
 *
 * The caller owns the ImGui::Begin and ImGui::End pair, the window title and
 * its size constraints, so a renderer is only ever responsible for what goes
 * in the body. That is what lets the culling in workspace::render skip a
 * window's body entirely without the renderer knowing it happened.
 *
 * The status bar is passed to every renderer although only one of them draws
 * it, because the bar belongs to the frame and not to the window that happens
 * to carry it: the workspace owns it, and a renderer that had no way to reach
 * it would have to be handed it by a global instead.
 */
/**
 * @brief The lock control's own state, as the status bar draws it.
 *
 * The lock is the only control for the store's lock, so it is the one thing a
 * renderer can ask the frame to do that is not a command onto the queue: what
 * it opens is a form owned by the frame, and there is no runtime state behind
 * a window that is open.
 */
struct lock_control final {
    /** @brief Where the lock stands, which decides the control's colour. */
    hedgr::model::domain::store_lock lock =
        hedgr::model::domain::store_lock::absent;

    /**
     * @brief Why the last attempt was refused, or empty.
     *
     * Carried here so the control that names the state can also say what the
     * state came from. A locked store and a store whose password was refused
     * are told apart by their colour and by their words, and the reason behind
     * the second is the thing the operator has to act on.
     */
    std::string reason;

    /**
     * @brief Asks the frame to open the password form.
     *
     * Set to true by the control when it is clicked; the frame reads it after
     * the draw and opens the form. A flag rather than a callback because the
     * renderer is a plain function pointer and carries no state across the
     * boundary.
     */
    bool open_requested = false;

    /**
     * @brief Asks the frame to lock the store.
     *
     * The other half of the one control: an open store is locked by the same
     * icon that opens the form, and the frame turns this into the command the
     * runtime expects.
     */
    bool close_requested = false;
};

using renderer_fn = void (*)(hedgr::model::state::window_handle,
                             const render_input&,
                             window_ui_state&,
                             hedgr::ui::widget::status_bar&,
                             lock_control&);

/**
 * @brief The renderer registered for a window kind.
 *
 * Never null: every kind in the enum has an entry in the table, and the table
 * is checked at compile time.
 */
[[nodiscard]] HEDGR_UI_EXPORT renderer_fn
renderer_for(hedgr::model::domain::window_kind kind) noexcept;

}

#endif
