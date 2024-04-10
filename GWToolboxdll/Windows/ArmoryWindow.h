#pragma once

#include <ToolboxWindow.h>

class ArmoryWindow : public ToolboxWindow {
public:
    static ArmoryWindow& Instance()
    {
        static ArmoryWindow instance;
        return instance;
    }

    [[nodiscard]] const char* Name() const override { return "Armory"; }
    [[nodiscard]] const char* Icon() const override { return ICON_FA_VEST; }

    void Initialize() override;
    void Terminate() override;

    void Draw(IDirect3DDevice9* pDevice) override;
};
