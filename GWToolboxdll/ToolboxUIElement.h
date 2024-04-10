#pragma once

#include <ToolboxModule.h>

class ToolboxUIElement : public ToolboxModule {
    friend class ToolboxSettings;

public:
    [[nodiscard]] bool IsUIElement() const override { return true; }

    // Draw user interface. Will be called every frame if the element is visible
    virtual void Draw(IDirect3DDevice9*) { }

    [[nodiscard]] virtual const char* UIName() const;
    //virtual const char* SettingsName() const override { return UIName(); }

    void Initialize() override;
    void Terminate() override;

    void LoadSettings(ToolboxIni* ini) override;

    void SaveSettings(ToolboxIni* ini) override;

    // returns true if clicked
    virtual bool DrawTabButton(bool show_icon = true, bool show_text = true, bool center_align_text = true);

    virtual bool ToggleVisible() { return visible = !visible; }

    [[nodiscard]] virtual bool ShowOnWorldMap() const { return false; }

    [[nodiscard]] const char* TypeName() const override { return "ui element"; }

    void RegisterSettingsContent() override;

    virtual void DrawSizeAndPositionSettings();

    bool visible = false;
    bool lock_move = false;
    bool lock_size = false;
    bool show_menubutton = false;

    bool* GetVisiblePtr(const bool force_show = false)
    {
        if (!has_closebutton || show_closebutton || force_show) {
            return &visible;
        }
        return nullptr;
    }

    bool show_closebutton = true;

protected:
    bool can_show_in_main_window = true;
    bool has_closebutton = false;
    bool is_resizable = true;
    bool is_movable = true;

    virtual void ShowVisibleRadio();
};
