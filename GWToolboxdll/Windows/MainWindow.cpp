#include "stdafx.h"

#include <Utils/GuiUtils.h>
#include <GWToolbox.h>

#include <Windows/MainWindow.h>
#include <Modules/PluginModule.h>

void MainWindow::LoadSettings(ToolboxIni* ini)
{
    ToolboxWindow::LoadSettings(ini);
    LOAD_BOOL(one_panel_at_time_only);
    LOAD_BOOL(show_icons);
    LOAD_BOOL(center_align_text);
    show_menubutton = false;
    pending_refresh_buttons = true;
}

void MainWindow::SaveSettings(ToolboxIni* ini)
{
    ToolboxWindow::SaveSettings(ini);
    SAVE_BOOL(one_panel_at_time_only);
    SAVE_BOOL(show_icons);
    SAVE_BOOL(center_align_text);
}

void MainWindow::DrawSettingsInternal()
{
    ImGui::Checkbox("Close other windows when opening a new one", &one_panel_at_time_only);
    ImGui::ShowHelp("Only affects windows (with a title bar), not widgets");

    ImGui::Checkbox("Show Icons", &show_icons);
    ImGui::Checkbox("Center-align text", &center_align_text);
}

void MainWindow::RegisterSettingsContent()
{
    ToolboxModule::RegisterSettingsContent(
        SettingsName(),
        Icon(),
        [this](const std::string&, const bool is_showing) {
            // ShowVisibleRadio();
            if (!is_showing) {
                return;
            }
            ImGui::Text("Main Window Visibility");
            ShowVisibleRadio();
            DrawSizeAndPositionSettings();
            DrawSettingsInternal();
        }, SettingsWeighting());
}

void MainWindow::RefreshButtons()
{
    pending_refresh_buttons = false;
    const std::vector<ToolboxUIElement*>& ui = GWToolbox::GetUIElements();
    modules_to_draw.clear();
    for (auto& ui_module : ui) {
        if (!ui_module->show_menubutton) {
            continue;
        }
        float weighting = GetModuleWeighting(ui_module);
        auto it = modules_to_draw.begin();
        for (it = modules_to_draw.begin(); it != modules_to_draw.end(); ++it) {
            if (it->first > weighting) {
                break;
            }
        }
        modules_to_draw.insert(it, {weighting, ui_module});
    }
}

void MainWindow::Draw(IDirect3DDevice9*)
{
    if (!visible) {
        return;
    }
    if (pending_refresh_buttons) {
        RefreshButtons();
    }
    static bool open = true;
    ImGui::SetNextWindowSize(ImVec2(110.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(Name(), show_closebutton ? &open : nullptr, GetWinFlags())) {
        ImGui::PushFont(GetFont(GuiUtils::FontSize::header2));
        bool drawn = false;
        const size_t msize = modules_to_draw.size();
        for (size_t i = 0; i < msize; i++) {
            ImGui::PushID(static_cast<int>(i));
            if (drawn) {
                ImGui::Separator();
            }
            drawn = true;
            const auto& ui_module = modules_to_draw[i].second;
            if (ui_module->DrawTabButton(show_icons, true, center_align_text)) {
                if (one_panel_at_time_only && ui_module->visible && ui_module->IsWindow()) {
                    for (const auto& module : modules_to_draw | std::views::values) {
                        if (module == ui_module) {
                            continue;
                        }
                        if (!module->IsWindow()) {
                            continue;
                        }
                        module->visible = false;
                    }
                }
                for (const auto plugin : PluginModule::GetPlugins() | std::views::filter([](ToolboxPlugin* p) {
                    return p && p->GetVisiblePtr() && p->ShowInMainMenu();
                })) {
                    *plugin->GetVisiblePtr() = false;
                }
            }
            ImGui::PopID();
        }
        for (const auto plugin : PluginModule::GetPlugins() | std::views::filter([](ToolboxPlugin* p) {
            return p && p->GetVisiblePtr() && p->ShowInMainMenu();
        })) {
            ImGui::PushID(plugin);
            if (drawn) {
                ImGui::Separator();
            }
            if (plugin->DrawTabButton(show_icons, true, center_align_text)) {
                if (one_panel_at_time_only && plugin->GetVisiblePtr() && *plugin->GetVisiblePtr()) {
                    for (const auto& module : modules_to_draw | std::views::values) {
                        if (!module->IsWindow()) {
                            continue;
                        }
                        module->visible = false;
                    }
                    for (const auto plug : PluginModule::GetPlugins() | std::views::filter([](ToolboxPlugin* p) {
                        return p && p->GetVisiblePtr() && p->ShowInMainMenu();
                    })) {
                        if (plugin == plug) {
                            continue;
                        }
                        *plug->GetVisiblePtr() = false;
                    }
                }
            }
            ImGui::PopID();
        }
        ImGui::PopFont();
    }
    ImGui::End();

    if (!open) {
        GWToolbox::StartSelfDestruct();
    }
}
