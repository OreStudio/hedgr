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
#ifndef HEDGR_UI_RENDER_ACCOUNTS_PANEL_HPP
#define HEDGR_UI_RENDER_ACCOUNTS_PANEL_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/widget/accounts_panel.hpp"

namespace hedgr::ui::render {

/**
 * @brief Draws the body of the ACCOUNTS menu's panel, inside its open popup.
 *
 * A filter box, then a scrolling list grouped under the tenant each account
 * last signed in to. A row carries the account's name, its principal and the
 * instance it belongs to, because the same tenant may exist on more than one
 * deployment and a row without its deployment would be an account nobody can
 * place. Accounts that have never signed in are listed last, under "No tenant
 * yet": Hedgr does not store a tenant on an account, so the grouping is what
 * IAM last answered rather than a field of the row.
 *
 * A pick opens that instance's accounts window, selects the account in it, and
 * closes the popup unless Ctrl is held, so several can be picked. A store with
 * no accounts, and a filter that matches none, draw the no data mark with the
 * words in its tooltip.
 *
 * Call it only while the popup is open. The filter is cleared and focused each
 * time the panel opens, which it knows from @c state.shown.
 */
HEDGR_UI_EXPORT void draw_accounts_panel(const render_input& input,
    widget::accounts_panel_state& state);

}

#endif
