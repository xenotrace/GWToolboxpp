#include "stdafx.h"

#include <GWToolbox.h>
#include <ImGuiAddons.h>
#include <ToolboxUIElement.h>
#include <Windows/MainWindow.h>

const char* ToolboxUIElement::UIName() const
{
    if (Icon()) {
        static char buf[128];
        sprintf(buf, "%s  %s", Icon(), Name());
        return buf;
    }
    return Name();
}

void ToolboxUIElement::Initialize()
{
    ToolboxModule::Initialize();
}

void ToolboxUIElement::Terminate()
{
    ToolboxModule::Terminate();
}

void ToolboxUIElement::LoadSettings(ToolboxIni* ini)
{
    ToolboxModule::LoadSettings(ini);
    LOAD_BOOL(visible);
    LOAD_BOOL(show_menubutton);
}

void ToolboxUIElement::SaveSettings(ToolboxIni* ini)
{
    ToolboxModule::SaveSettings(ini);
    SAVE_BOOL(visible);
    SAVE_BOOL(show_menubutton);
}

void ToolboxUIElement::RegisterSettingsContent()
{
    ToolboxModule::RegisterSettingsContent(
        SettingsName(),
        Icon(),
        [this](const std::string&, const bool is_showing) {
            ShowVisibleRadio();
            if (!is_showing) {
                return;
            }
            DrawSizeAndPositionSettings();
            DrawSettingsInternal();
        },
        SettingsWeighting());
}

void ToolboxUIElement::DrawSizeAndPositionSettings()
{
    ImVec2 pos(0, 0);
    ImVec2 size(100.0f, 100.0f);
    if (const auto window = ImGui::FindWindowByName(Name())) {
        pos = window->Pos;
        size = window->Size;
    }
    if (is_movable || is_resizable) {
        char buf[128];
        sprintf(buf, "You need to show the %s for this control to work", TypeName());
        if (is_movable) {
            if (ImGui::DragFloat2("Position", reinterpret_cast<float*>(&pos), 1.0f, 0.0f, 0.0f, "%.0f")) {
                ImGui::SetWindowPos(Name(), pos);
            }
            ImGui::ShowHelp(buf);
        }
        if (is_resizable) {
            if (ImGui::DragFloat2("Size", reinterpret_cast<float*>(&size), 1.0f, 0.0f, 0.0f, "%.0f")) {
                ImGui::SetWindowSize(Name(), size);
            }
            ImGui::ShowHelp(buf);
        }
    }
    auto count = 0;
    if (is_movable) {
        if (++count % 2 == 0) {
            ImGui::SameLine();
        }
        ImGui::Checkbox("Lock Position", &lock_move);
    }
    if (is_resizable) {
        if (++count % 2 == 0) {
            ImGui::SameLine();
        }
        ImGui::Checkbox("Lock Size", &lock_size);
    }
    if (has_closebutton) {
        if (++count % 2 == 0) {
            ImGui::SameLine();
        }
        ImGui::Checkbox("Show close button", &show_closebutton);
    }
    if (can_show_in_main_window) {
        if (++count % 2 == 0) {
            ImGui::SameLine();
        }
        if (ImGui::Checkbox("Show in main window", &show_menubutton)) {
            MainWindow::Instance().pending_refresh_buttons = true;
        }
    }
}

void ToolboxUIElement::ShowVisibleRadio()
{
    ImGui::SameLine(ImGui::GetContentRegionAvail().x
                    - ImGui::GetTextLineHeight()
                    - ImGui::GetStyle().FramePadding.y * 2);
    ImGui::PushID(Name());
    ImGui::Checkbox("##check", &visible);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Visible");
    }
    ImGui::PopID();
}

bool ToolboxUIElement::DrawTabButton(const bool show_icon, const bool show_text, const bool center_align_text)
{
    ImGui::PushStyleColor(ImGuiCol_Button, visible ? ImGui::GetStyle().Colors[ImGuiCol_Button] : ImVec4(0, 0, 0, 0));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 textsize = ImGui::CalcTextSize(Name());
    const float width = ImGui::GetContentRegionAvail().x;

    float img_size = 0;
    if (show_icon) {
        img_size = ImGui::GetTextLineHeightWithSpacing();
    }
    float text_x;
    if (center_align_text) {
        text_x = pos.x + img_size + (width - img_size - textsize.x) / 2;
    }
    else {
        text_x = pos.x + img_size + ImGui::GetStyle().ItemSpacing.x;
    }
    const bool clicked = ImGui::Button("", ImVec2(width, ImGui::GetTextLineHeightWithSpacing()));
    if (show_icon) {
        if (Icon()) {
            ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x, pos.y + ImGui::GetStyle().ItemSpacing.y / 2),
                                                ImColor(ImGui::GetStyle().Colors[ImGuiCol_Text]), Icon());
        }
    }
    if (show_text) {
        ImGui::GetWindowDrawList()->AddText(ImVec2(text_x, pos.y + ImGui::GetStyle().ItemSpacing.y / 2),
                                            ImColor(ImGui::GetStyle().Colors[ImGuiCol_Text]), Name());
    }

    if (clicked) {
        visible = !visible;
    }
    ImGui::PopStyleColor();
    return clicked;
}
