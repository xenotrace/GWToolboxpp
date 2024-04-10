#pragma once

#include <ToolboxModule.h>

class MouseFix : public ToolboxModule {
    MouseFix() = default;
    ~MouseFix() override = default;

public:
    static MouseFix& Instance()
    {
        static MouseFix instance;
        return instance;
    }

    [[nodiscard]] const char* Name() const override { return "Mouse Settings"; }
    [[nodiscard]] const char* Description() const override { return " - Fixes occasional camera glitch when looking around in-game\n - Adds option to scale cursor size"; }
    [[nodiscard]] const char* Icon() const override { return ICON_FA_MOUSE_POINTER; }

    void LoadSettings(ToolboxIni*) override;
    void SaveSettings(ToolboxIni*) override;
    void Initialize() override;
    void Terminate() override;
    bool WndProc(UINT, WPARAM, LPARAM) override;
    void DrawSettingsInternal() override;
};
