/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "imgui.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace hedgr::ui::render {
using hedgr::model::state::window_handle;
void render_crm_matrix(window_handle handle, const render_input& input,
    window_ui_state& state, hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);
    ImGui::PushID("crm-matrix");
    widget::foot_state foot;
    foot.identity = identity_text(identity_of(input, handle));
    const auto& shape = input.windows.state(handle);
    const auto* crm = std::get_if<model::domain::crm_matrix_state>(&shape);
    const auto session = input.windows.session_of(handle);
    const auto* snapshot = (crm != nullptr && session.has_value())
        ? input.crm.get(input.sessions.instance_id(*session),
            input.sessions.account_id(*session), crm->party_id)
        : nullptr;
    ImGui::TextUnformatted("Cross-rates matrix");
    ImGui::SameLine();
    if (snapshot != nullptr && !snapshot->observed_as_of.empty())
        ImGui::TextDisabled("as of %s", snapshot->observed_as_of.c_str());
    else
        ImGui::TextDisabled("No batch yet");
    if (snapshot == nullptr || snapshot->cells.empty()) {
        ImGui::TextDisabled("%s", snapshot != nullptr
                && !snapshot->error.empty()
                ? snapshot->error.c_str() : "Reading the CRM...");
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }
    std::set<std::string> currencies;
    std::map<std::pair<std::string, std::string>, const model::domain::crm_cell*>
        cells;
    for (const auto& cell : snapshot->cells) {
        currencies.insert(cell.base_currency_code);
        currencies.insert(cell.quote_currency_code);
        cells[{cell.base_currency_code, cell.quote_currency_code}] = &cell;
    }
    if (ImGui::BeginTable("##crm", static_cast<int>(currencies.size()) + 1,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY
                | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        for (const auto& c : currencies)
            ImGui::TableSetupColumn(c.c_str(), ImGuiTableColumnFlags_WidthFixed,
                92.0f);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableHeadersRow();
        for (const auto& base : currencies) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(base.c_str());
            for (const auto& quote : currencies) {
                ImGui::TableNextColumn();
                if (base == quote) { ImGui::TextDisabled("—"); continue; }
                const auto found = cells.find({base, quote});
                if (found == cells.end()) { ImGui::TextDisabled("—"); continue; }
                const auto& cell = *found->second;
                const ImVec4 colour = cell.status == "fresh"
                    ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                    : cell.status == "stale"
                    ? ImGui::ColorConvertU32ToFloat4(
                          theme::readiness_colour(
                              model::domain::readiness::bootstrapping))
                    : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                ImGui::TextColored(colour, "%.5f", cell.rate);
                if (cell.delta_pct)
                    ImGui::TextColored(colour, "%+.2f%%", *cell.delta_pct);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s/%s  %s  %s%s",
                        cell.base_currency_code.c_str(),
                        cell.quote_currency_code.c_str(),
                        cell.crm_name.c_str(), cell.status.c_str(),
                        cell.reciprocal ? "  reciprocal" : "");
            }
        }
        ImGui::EndTable();
    }
    widget::draw_status_foot(status, input.now_ns, foot);
    ImGui::PopID();
}
}
