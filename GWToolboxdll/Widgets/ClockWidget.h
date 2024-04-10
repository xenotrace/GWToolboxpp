#pragma once

#include <ToolboxWidget.h>

class ClockWidget : public ToolboxWidget {
    ClockWidget() = default;
    ~ClockWidget() override = default;

public:
    static ClockWidget& Instance()
    {
        static ClockWidget instance;
        return instance;
    }

    [[nodiscard]] const char* Name() const override { return "Clock"; }
    [[nodiscard]] const char* Icon() const override { return ICON_FA_CLOCK; }

    // Draw user interface. Will be called every frame if the element is visible
    void Draw(IDirect3DDevice9* pDevice) override;

    void LoadSettings(ToolboxIni* ini) override;
    void SaveSettings(ToolboxIni* ini) override;
    void DrawSettingsInternal() override;

private:
    bool use_24h_clock = true;
    bool show_seconds = false;
};
