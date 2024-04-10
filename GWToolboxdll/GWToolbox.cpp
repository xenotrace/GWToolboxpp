#include "stdafx.h"

#include <GWCA/GWCA.h>
#include <GWCA/Utilities/Hooker.h>

#include <GWCA/Context/PreGameContext.h>
#include <GWCA/Context/CharContext.h>

#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/MemoryMgr.h>
#include <GWCA/Managers/RenderMgr.h>

#include <Defines.h>
#include <Utils/GuiUtils.h>
#include <GWToolbox.h>
#include <Logger.h>

#include <Modules/Resources.h>
#include <Modules/ChatCommands.h>
#include <Modules/ToolboxTheme.h>
#include <Modules/ToolboxSettings.h>
#include <Modules/CrashHandler.h>
#include <Modules/DialogModule.h>
#include "Modules/AprilFools.h"
#include "Modules/ChatSettings.h"
#include "Modules/GameSettings.h"
#include "Modules/GwDatTextureModule.h"
#include "Modules/HallOfMonumentsModule.h"
#include "Modules/InventoryManager.h"
#include "Modules/LoginModule.h"
#include "Modules/Updater.h"
#include "Windows/SettingsWindow.h"

#include <Windows/MainWindow.h>
#include <Widgets/Minimap/Minimap.h>
#include <hidusage.h>



// declare method here as recommended by imgui
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
    HMODULE dllmodule = nullptr;
    WNDPROC OldWndProc = nullptr;
    bool tb_destroyed = false;
    bool defer_close = false;
    HWND gw_window_handle = nullptr;

    utf8::string imgui_inifile;
    bool imgui_inifile_changed = false;
    bool settings_folder_changed = false;

    bool must_self_destruct = false; // is true when toolbox should quit
    GW::HookEntry Update_Entry;

    bool initialized = false;

    bool event_handler_attached = false;

    bool AttachWndProcHandler()
    {
        if (event_handler_attached) {
            return true;
        }
        Log::Log("installing event handler\n");
        gw_window_handle = GW::MemoryMgr::GetGWWindowHandle();
        OldWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(gw_window_handle, GWL_WNDPROC, reinterpret_cast<LONG>(SafeWndProc)));
        Log::Log("Installed input event handler, oldwndproc = 0x%X\n", OldWndProc);

        // RegisterRawInputDevices to be able to receive WM_INPUT via WndProc
        static RAWINPUTDEVICE rid;
        rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
        rid.usUsage = HID_USAGE_GENERIC_MOUSE;
        rid.dwFlags = RIDEV_INPUTSINK;
        rid.hwndTarget = gw_window_handle;
        ASSERT(RegisterRawInputDevices(&rid, 1, sizeof(rid)));

        event_handler_attached = true;
        return true;
    }

    bool DetachWndProcHandler()
    {
        if (!event_handler_attached) {
            return true;
        }
        Log::Log("Restoring input hook\n");
        SetWindowLongPtr(gw_window_handle, GWL_WNDPROC, reinterpret_cast<LONG>(OldWndProc));
        event_handler_attached = false;
        return true;
    }

    bool imgui_initialized = false;

    bool AttachImgui(IDirect3DDevice9* device)
    {
        if (imgui_initialized) {
            return true;
        }
        ImGui::CreateContext();
        //ImGui_ImplDX9_Init(GW::MemoryMgr().GetGWWindowHandle(), device);
        ImGui_ImplDX9_Init(device);
        ImGui_ImplWin32_Init(GW::MemoryMgr::GetGWWindowHandle());

        GW::Render::SetResetCallback([](const IDirect3DDevice9*) {
            ImGui_ImplDX9_InvalidateDeviceObjects();
        });

        auto& io = ImGui::GetIO();
        io.MouseDrawCursor = false;
        io.IniFilename = imgui_inifile.bytes;

        Resources::EnsureFileExists(
            Resources::GetPath(L"Font.ttf"),
            "https://raw.githubusercontent.com/gwdevhub/GWToolboxpp/master/resources/Font.ttf",
            [](const bool success, const std::wstring& error) {
                if (success) {
                    GuiUtils::LoadFonts();
                }
                else {
                    Log::ErrorW(L"Cannot download font, please download it manually!\n%s", error.c_str());
                }
            });
        imgui_initialized = true;
        return true;
    }

    bool DetachImgui()
    {
        if (!imgui_initialized) {
            return true;
        }
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imgui_initialized = false;
        return true;
    }

    std::vector<ToolboxModule*> modules_enabled{};
    std::vector<ToolboxWidget*> widgets_enabled{};
    std::vector<ToolboxWindow*> windows_enabled{};

    std::vector<ToolboxModule*> all_modules_enabled{};
    std::vector<ToolboxUIElement*> ui_elements_enabled{};

    std::vector<ToolboxModule*> modules_terminating{};

    void ReorderModules(std::vector<ToolboxModule*>& modules)
    {
        std::ranges::sort(modules, [](const ToolboxModule* lhs, const ToolboxModule* rhs) {
            return std::string(lhs->SettingsName()).compare(rhs->SettingsName()) < 0;
        });
    }

    ToolboxIni* OpenSettingsFile()
    {
        static std::unique_ptr<ToolboxIni> inifile = nullptr;
        const auto full_path = Resources::GetSettingFile(GWTOOLBOX_INI_FILENAME);
        if (!GWToolbox::SettingsFolderChanged() && inifile) {
            return inifile.get();
        }
        auto tmp = std::make_unique<ToolboxIni>(false, false, false);
        ASSERT(tmp->LoadIfExists(full_path) == SI_OK);
        tmp->location_on_disk = full_path;
        inifile = std::move(tmp);
        return inifile.get();
    }

    bool ToggleTBModule(ToolboxModule& m, std::vector<ToolboxModule*>& vec, const bool enable)
    {
        const auto found = std::ranges::find(vec, &m);
        if (found != vec.end()) {
            // Module found
            if (enable) {
                return true;
            }
            m.SaveSettings(OpenSettingsFile());
            modules_terminating.push_back(&m);
            m.SignalTerminate();
            vec.erase(found);
            ReorderModules(vec);
            return false;
        }
        // Module not found
        if (!enable) {
            return false;
        }
        const auto is_terminating = std::ranges::find(modules_terminating, &m);
        if (is_terminating != modules_terminating.end()) {
            return false; // Not finished terminating
        }
        vec.push_back(&m);
        m.Initialize();
        m.LoadSettings(OpenSettingsFile());
        ReorderModules(vec);
        return true; // Added successfully
    }
}

const std::vector<ToolboxModule*>& GWToolbox::GetAllModules()
{
    return all_modules_enabled;
}

const std::vector<ToolboxUIElement*>& GWToolbox::GetUIElements()
{
    return ui_elements_enabled;
}

const std::vector<ToolboxModule*>& GWToolbox::GetModules()
{
    return modules_enabled;
}

const std::vector<ToolboxWindow*>& GWToolbox::GetWindows()
{
    return windows_enabled;
}

const std::vector<ToolboxWidget*>& GWToolbox::GetWidgets()
{
    return widgets_enabled;
}

void UpdateEnabledWidgetVectors(ToolboxModule* m, bool added)
{
    const auto update_vec = [added](std::vector<void*>& vec, void* m) {
        const auto found = std::ranges::find(vec, m);
        if (added) {
            if (found == vec.end()) {
                vec.push_back(m);
            }
        }
        else {
            if (found != vec.end()) {
                vec.erase(found);
            }
        }
    };
    update_vec(reinterpret_cast<std::vector<void*>&>(all_modules_enabled), m);
    if (m->IsUIElement()) {
        update_vec(reinterpret_cast<std::vector<void*>&>(ui_elements_enabled), m);
        if (m->IsWidget()) {
            update_vec(reinterpret_cast<std::vector<void*>&>(widgets_enabled), m);
        }
        if (m->IsWindow()) {
            update_vec(reinterpret_cast<std::vector<void*>&>(windows_enabled), m);
        }
    }
    else {
        update_vec(reinterpret_cast<std::vector<void*>&>(modules_enabled), m);
    }
}

bool GWToolbox::IsInitialized() { return initialized; }

bool GWToolbox::ToggleModule(ToolboxWidget& m, const bool enable)
{
    const bool added = ToggleTBModule(m, reinterpret_cast<std::vector<ToolboxModule*>&>(widgets_enabled), enable);
    UpdateEnabledWidgetVectors(&m, added);
    return added;
}

bool GWToolbox::ToggleModule(ToolboxWindow& m, const bool enable)
{
    const bool added = ToggleTBModule(m, reinterpret_cast<std::vector<ToolboxModule*>&>(windows_enabled), enable);
    UpdateEnabledWidgetVectors(&m, added);
    return added;
}

bool GWToolbox::ToggleModule(ToolboxModule& m, const bool enable)
{
    const bool added = ToggleTBModule(m, modules_enabled, enable);
    UpdateEnabledWidgetVectors(&m, added);
    return added;
}

HMODULE GWToolbox::GetDLLModule()
{
    return dllmodule;
}

DWORD __stdcall SafeThreadEntry(const LPVOID module) noexcept
{
    dllmodule = static_cast<HMODULE>(module);
    __try {
        ThreadEntry(nullptr);
    } __except (EXCEPT_EXPRESSION_ENTRY) {
        Log::Log("SafeThreadEntry __except body\n");
    }
    return EXIT_SUCCESS;
}

DWORD __stdcall ThreadEntry(LPVOID)
{
    Log::Log("Initializing API\n");

    GW::HookBase::Initialize();
    if (!GW::Initialize()) {
        if (MessageBoxA(nullptr, "Initialize Failed at finding all addresses, contact Developers about this.", "GWToolbox++ API Error", 0) == IDOK) { }
        return 0;
    }

    Log::Log("Installing dx hooks\n");

    // Some modules rely on the gwdx_ptr being present for stuff like getting viewport coords.
    // Because this ptr isn't set until the Render loop runs at least once, let it run and then reassign SetRenderCallback.
    GWToolbox::Initialize();

    Log::Log("Installed dx hooks\n");

    Log::InitializeChat();

    Log::Log("Installed chat hooks\n");

    GW::HookBase::EnableHooks();

    Log::Log("Hooks Enabled!\n");

    while (!tb_destroyed) {
        // wait until destruction
        Sleep(100);

        // Feel free to uncomment to get this behavior for testing, but don't commit.
        //#ifdef _DEBUG
        //        if (GetAsyncKeyState(VK_END) & 1) {
        //            GWToolbox::Instance().StartSelfDestruct();
        //        }
        //#endif
    }

    // @Remark:
    // Hooks are disable from Guild Wars thread (safely), so we just make sure we exit the last hooks
    while (GW::HookBase::GetInHookCount()) {
        Sleep(16);
    }

    // @Remark:
    // We can't guarantee that the code in Guild Wars thread isn't still in the trampoline, but
    // practically a short sleep is fine.
    Sleep(16);

    Log::Log("Destroying API\n");
    GW::Terminate();

    Log::Log("Closing log/console, bye!\n");
    Log::Terminate();
    return 0;
}

LRESULT CALLBACK SafeWndProc(const HWND hWnd, const UINT Message, const WPARAM wParam, const LPARAM lParam) noexcept
{
    __try {
        return WndProc(hWnd, Message, wParam, lParam);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return CallWindowProc(OldWndProc, hWnd, Message, wParam, lParam);
    }
}

LRESULT CALLBACK WndProc(const HWND hWnd, const UINT Message, const WPARAM wParam, const LPARAM lParam)
{
    static bool right_mouse_down = false;

    if (Message == WM_CLOSE || (Message == WM_SYSCOMMAND && wParam == SC_CLOSE)) {
        // This is naughty, but we need to defer the closing signal until toolbox has terminated properly.
        // we can't sleep here, because toolbox modules will probably be using the render loop to close off things
        // like hooks
        GWToolbox::StartSelfDestruct();
        defer_close = true;
        return 0;
    }

    if (!(!GW::GetPreGameContext() && imgui_initialized && GWToolbox::IsInitialized() && !tb_destroyed)) {
        return CallWindowProc(OldWndProc, hWnd, Message, wParam, lParam);
    }

    if (Message == WM_RBUTTONUP) {
        right_mouse_down = false;
    }
    if (Message == WM_RBUTTONDOWN) {
        right_mouse_down = true;
    }
    if (Message == WM_RBUTTONDBLCLK) {
        right_mouse_down = true;
    }

    GWToolbox::Instance().right_mouse_down = right_mouse_down;

    // === Send events to ImGui ===
    const auto& io = ImGui::GetIO();
    const bool skip_mouse_capture = right_mouse_down || GW::UI::GetIsWorldMapShowing() || GW::Map::GetIsInCinematic();
    if (ImGui_ImplWin32_WndProcHandler(hWnd, Message, wParam, lParam) && !skip_mouse_capture) {
        return TRUE;
    }

    // === Send events to toolbox ===
    auto& tb = GWToolbox::Instance();
    switch (Message) {
        // Send button up mouse events to everything, to avoid being stuck on mouse-down
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_INPUT:
            for (const auto m : tb.GetAllModules()) {
                m->WndProc(Message, wParam, lParam);
            }
            break;

        // Other mouse events:
        // - If right mouse down, leave it to gw
        // - ImGui first (above), if WantCaptureMouse that's it
        // - Toolbox module second (e.g.: minimap), if captured, that's it
        // - otherwise pass to gw
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL: {
            if (io.WantCaptureMouse && !skip_mouse_capture) {
                return true;
            }
            bool captured = false;
            for (const auto m : tb.GetAllModules()) {
                if (m->WndProc(Message, wParam, lParam)) {
                    captured = true;
                }
            }
            if (captured) {
                return true;
            }
        }
        //if (!skip_mouse_capture) {

        //}
        break;

        // keyboard messages
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (io.WantTextInput) {
                break; // if imgui wants them, send to imgui (above) and to gw
            }
        // else fallthrough
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_IME_CHAR:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK:
        case WM_XBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
        case WM_MBUTTONUP:
            if (io.WantTextInput) {
                return true; // if imgui wants them, send just to imgui (above)
            }

        // send input to chat commands for camera movement
            if (ChatCommands::Instance().WndProc(Message, wParam, lParam)) {
                return true;
            }
        case WM_ACTIVATE:
            // send to toolbox modules and plugins
        {
            bool captured = false;
            for (const auto m : tb.GetAllModules()) {
                if (m->WndProc(Message, wParam, lParam)) {
                    captured = true;
                }
            }
            if (captured) {
                return true;
            }
        }
        // note: capturing those events would prevent typing if you have a hotkey assigned to normal letters.
        // We may want to not send events to toolbox if the player is typing in-game
        // Otherwise, we may want to capture events.
        // For that, we may want to only capture *successfull* hotkey activations.
        break;

        case WM_SIZE:
            // ImGui doesn't need this, it reads the viewport size directly
            break;
        default:
            // Custom messages registered via RegisterWindowMessage
            if (Message >= 0xC000 && Message <= 0xFFFF) {
                for (const auto m : tb.GetAllModules()) {
                    m->WndProc(Message, wParam, lParam);
                }
            }
            break;
    }

    return CallWindowProc(OldWndProc, hWnd, Message, wParam, lParam);
}

void GWToolbox::Initialize()
{
    if (initialized || must_self_destruct) {
        return;
    }

    imgui_inifile = Unicode16ToUtf8(Resources::GetSettingFile(L"interface.ini").c_str());

    Log::Log("Creating Toolbox\n");

    GW::GameThread::RegisterGameThreadCallback(&Update_Entry, [](GW::HookStatus* a) { Update(a); });

    Resources::EnsureFolderExists(Resources::GetComputerFolderPath());
    Resources::EnsureFolderExists(Resources::GetPath(L"img"));
    Resources::EnsureFolderExists(Resources::GetPath(L"location logs"));
    Resources::EnsureFolderExists(Resources::GetPath(L"configs"));

    // if the file does not exist we'll load module settings once downloaded, but we need the file open
    // in order to read defaults
    const auto ini = OpenSettingsFile();

    Log::Log("Creating Modules\n");
    ToggleModule(CrashHandler::Instance());
    ToggleModule(Resources::Instance());
    ToggleModule(ToolboxTheme::Instance());
    ToggleModule(ToolboxSettings::Instance());
    ToggleModule(MainWindow::Instance());
    ToggleModule(DialogModule::Instance());

    ToggleModule(GwDatTextureModule::Instance());
    ToggleModule(Updater::Instance());
    ToggleModule(ChatCommands::Instance());
    ToggleModule(GameSettings::Instance());
    ToggleModule(ChatSettings::Instance());
    ToggleModule(InventoryManager::Instance());
    ToggleModule(HallOfMonumentsModule::Instance());
    ToggleModule(LoginModule::Instance());
    ToggleModule(AprilFools::Instance());
    ToggleModule(SettingsWindow::Instance());

    ToolboxSettings::LoadModules(ini); // initialize all other modules as specified by the user

    if (GW::Map::GetInstanceType() != GW::Constants::InstanceType::Loading) {
        const auto* c = GW::GetCharContext();
        if (c && c->player_name) {
            Log::InfoW(L"Hello!");
        }
    }
    GW::Render::SetRenderCallback([](IDirect3DDevice9* device) {
        __try {
            Draw(device);
        } __except (EXCEPT_EXPRESSION_ENTRY) { }
    });

    initialized = true;
}

std::filesystem::path GWToolbox::LoadSettings()
{
    const auto ini = OpenSettingsFile();
    if (!ini->location_on_disk.empty()) {
        for (const auto m : modules_enabled) {
            m->LoadSettings(ini);
        }
        for (const auto m : widgets_enabled) {
            m->LoadSettings(ini);
        }
        for (const auto m : windows_enabled) {
            m->LoadSettings(ini);
        }
    }
    return ini->location_on_disk;
}

bool GWToolbox::SetSettingsFolder(const std::filesystem::path& path)
{
    static auto last_path = std::filesystem::path{};
    if (last_path != path) {
        if (Resources::SetSettingsFolder(path)) {
            imgui_inifile = Unicode16ToUtf8(Resources::GetSettingFile(L"interface.ini").c_str());
            settings_folder_changed = true;
            imgui_inifile_changed = true;
            last_path = path;
            return true;
        }
        return false;
    }
    return true;
}

bool GWToolbox::SettingsFolderChanged()
{
    return settings_folder_changed;
}

std::filesystem::path GWToolbox::SaveSettings()
{
    const auto ini = OpenSettingsFile();
    for (const auto m : modules_enabled) {
        m->SaveSettings(ini);
    }
    for (const auto m : widgets_enabled) {
        m->SaveSettings(ini);
    }
    for (const auto m : windows_enabled) {
        m->SaveSettings(ini);
    }
    ASSERT(Resources::SaveIniToFile(ini->location_on_disk, ini) == 0);
    const auto dir = ini->location_on_disk.parent_path();
    const auto dirstr = dir.wstring();
    const std::wstring printable = std::regex_replace(dirstr, std::wregex(L"\\\\"), L"/");
    Log::LogW(L"Toolbox settings saved to %s", printable.c_str());
    settings_folder_changed = false;
    return ini->location_on_disk;
}

void GWToolbox::StartSelfDestruct()
{
    if (must_self_destruct) {
        return;
    }
    if (initialized) {
        SaveSettings();
        while (modules_enabled.size()) {
            ASSERT(ToggleModule(*modules_enabled[0], false) == false);
        }
        while (widgets_enabled.size()) {
            ASSERT(ToggleModule(*widgets_enabled[0], false) == false);
        }
        while (windows_enabled.size()) {
            ASSERT(ToggleModule(*windows_enabled[0], false) == false);
        }
        ASSERT(all_modules_enabled.empty());
    }
    must_self_destruct = true;
}

void GWToolbox::Terminate()
{
    if (!initialized) {
        return;
    }
    SaveSettings();

    GW::GameThread::RemoveGameThreadCallback(&Update_Entry);

    ASSERT(CanTerminate());

    ASSERT(all_modules_enabled.empty());

    if (GW::Map::GetInstanceType() != GW::Constants::InstanceType::Loading) {
        Log::Info("Bye!");
    }
}


bool GWToolbox::CanTerminate()
{
    return modules_terminating.empty();
}

void GWToolbox::Draw(IDirect3DDevice9* device)
{
    // === destruction ===
    if (initialized && must_self_destruct) {
        if (!GuiUtils::FontsLoaded()) {
            return;
        }
        if (!CanTerminate()) {
            return;
        }

        Instance().Terminate();
        ASSERT(DetachImgui());
        ASSERT(DetachWndProcHandler());

        GW::DisableHooks();
        initialized = false;
        tb_destroyed = true;
    }
    // === runtime ===
    if (initialized && !must_self_destruct) {
        // Attach WndProc in the render loop to make sure the window is loaded and ready
        ASSERT(AttachWndProcHandler());
        // Attach imgui if not already done so
        ASSERT(AttachImgui(device));

        if (imgui_inifile_changed) {
            auto& io = ImGui::GetIO();
            io.IniFilename = imgui_inifile.bytes;
            imgui_inifile_changed = false;
        }

        if (!GW::UI::GetIsUIDrawn()) {
            return;
        }
        if (GW::GetPreGameContext()) {
            return; // Login screen
        }
        if (GW::Map::GetIsInCinematic()) {
            return;
        }
        if (IsIconic(GW::MemoryMgr::GetGWWindowHandle())) {
            return;
        }

        if (!GuiUtils::FontsLoaded()) {
            return; // Fonts not loaded yet.
        }

        Resources::DxUpdate(device);

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();

        const bool world_map_showing = GW::UI::GetIsWorldMapShowing();

        if (!world_map_showing) {
            Minimap::Render(device);
        }

        ImGui::NewFrame();

        // Key up/down events don't get passed to gw window when out of focus, but we need the following to be correct,
        // or things like alt-tab make imgui think that alt is still down.
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_ModCtrl, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
        io.AddKeyEvent(ImGuiKey_ModShift, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        io.AddKeyEvent(ImGuiKey_ModAlt, (GetKeyState(VK_MENU) & 0x8000) != 0);

        for (const auto uielement : ui_elements_enabled) {
            if (world_map_showing && !uielement->ShowOnWorldMap()) {
                continue;
            }
            uielement->Draw(device);
        }

#ifdef _DEBUG
        // Feel free to uncomment to play with ImGui's features
        //ImGui::ShowDemoWindow();
        //ImGui::ShowStyleEditor(); // Warning, this WILL change your theme. Back up theme.ini first!
#endif

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    if (tb_destroyed && defer_close) {
        // Toolbox was closed by a user closing GW - close it here for the by sending the `WM_CLOSE` message again.
        SendMessageW(gw_window_handle, WM_CLOSE, NULL, NULL);
    }
}

void GWToolbox::Update(GW::HookStatus*)
{
    static DWORD last_tick_count;
    if (last_tick_count == 0) {
        last_tick_count = GetTickCount();
    }

    // @Enhancement:
    // Improve precision with QueryPerformanceCounter
    const auto tick = GetTickCount();
    const auto delta = tick - last_tick_count;
    const auto delta_f = static_cast<float>(delta) / 1000.f;

    if (initialized
        && imgui_initialized
        && !must_self_destruct) {
        for (const auto m : all_modules_enabled) {
            m->Update(delta_f);
        }
    }

    for (const auto m : modules_terminating) {
        if (m->CanTerminate()) {
            m->Terminate();
            const auto found = std::ranges::find(modules_terminating, m);
            ASSERT(found != modules_terminating.end());
            modules_terminating.erase(found);
            break;
        }
        m->Update(delta_f);
    }
    last_tick_count = tick;
}
