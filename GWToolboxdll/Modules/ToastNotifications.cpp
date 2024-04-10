#include "stdafx.h"

#include "ToastNotifications.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/Utilities/Hook.h>
#include <GWCA/Packets/Opcodes.h>
#include <GWCA/Packets/StoC.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Party.h>

#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/MemoryMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/StoCMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/PartyMgr.h>
#include <GWCA/Managers/PlayerMgr.h>

#include <wintoast/wintoastlib.h>
#include <Utils/GuiUtils.h>
#include <Utils/ToolboxUtils.h>
#include <Defines.h>

namespace {
    bool is_platform_compatible = false;

    bool show_notifications_when_focussed = false;
    bool show_notifications_when_in_background = true;
    bool show_notifications_when_minimised = true;
    bool show_notifications_when_in_outpost = true;
    bool show_notifications_when_in_explorable = true;

    bool show_notifications_on_whisper = true;
    bool show_notifications_on_guild_chat = false;
    bool show_notifications_on_ally_chat = false;
    bool show_notifications_on_last_to_ready = false;
    bool show_notifications_on_invite = false;
    bool show_notifications_on_everyone_ready = false;
    bool show_notifications_on_self_resurrected = false;

    bool flash_window_on_whisper = true;
    bool flash_window_on_guild_chat = false;
    bool flash_window_on_ally_chat = false;
    bool flash_window_on_last_to_ready = false;
    bool flash_window_on_invite = false;
    bool flash_window_on_everyone_ready = false;
    bool flash_window_on_self_resurrected = false;

    const wchar_t* party_ready_toast_title = L"Party Ready";

    // Pointers deleted in ChatSettings::Terminate
    std::map<std::wstring, ToastNotifications::Toast*> toasts;

    bool ShouldNotifyInstanceType(const bool notify)
    {
        if (!notify) {
            return false;
        }

        switch (GW::Map::GetInstanceType()) {
            case GW::Constants::InstanceType::Explorable:
                return show_notifications_when_in_explorable;
            case GW::Constants::InstanceType::Outpost:
                return show_notifications_when_in_outpost;
            default:
                return false;
        }
    }

    bool CanNotify()
    {
        const auto whnd = GW::MemoryMgr::GetGWWindowHandle();
        if (!whnd) {
            return false;
        }
        if (IsIconic(whnd)) {
            return ShouldNotifyInstanceType(show_notifications_when_minimised);
        }
        if (GetActiveWindow() != whnd) {
            return ShouldNotifyInstanceType(show_notifications_when_in_background);
        }

        return ShouldNotifyInstanceType(show_notifications_when_focussed);
    }

    void FlashWindow()
    {
        if (CanNotify()) {
            GuiUtils::FlashWindow();
        }
    }

    uint32_t GetPartyId()
    {
        const auto p = GW::PartyMgr::GetPartyInfo();
        return p ? p->party_id : static_cast<uint32_t>(-1);
    }

    void OnWhisperToastActivated(const ToastNotifications::Toast* toast, const bool activated)
    {
        if (!activated) {
            return; // dismissed
        }
        GuiUtils::FocusWindow();
        GW::GameThread::Enqueue([charname = toast->title] {
            SendUIMessage(GW::UI::UIMessage::kOpenWhisper, const_cast<wchar_t*>(charname.data()));
        });
    }

    void OnGenericToastActivated(const ToastNotifications::Toast*, const bool activated)
    {
        if (!activated) {
            return; // dismissed
        }
        GuiUtils::FocusWindow();
    }

    GW::HookEntry OnWhisper_Entry;

    void OnWhisper(GW::HookStatus*, const wchar_t* from, const wchar_t* msg)
    {
        if (show_notifications_on_whisper) {
            ToastNotifications::SendToast(from, msg, OnWhisperToastActivated);
        }
        if (flash_window_on_whisper) {
            FlashWindow();
        }
    }

    void TriggerToastCallback(const ToastNotifications::Toast* toast, const bool result)
    {
        if (toast->callback) {
            toast->callback(toast, result);
        }
        // naughty but idc
        const auto nonconst = const_cast<ToastNotifications::Toast*>(toast);
        nonconst->callback = nullptr;
        nonconst->dismiss();
    }

    // ReSharper disable once CppParameterMayBeConst
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void OnToastMessageDecoded(void* callback_param, wchar_t* decoded)
    {
        const auto title = static_cast<wchar_t*>(callback_param);
        ToastNotifications::SendToast(title, decoded, OnGenericToastActivated);
        delete[] title;
    }

    // ReSharper disable once CppParameterMayBeConst
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void SendEncodedToastMessage(const wchar_t* title, wchar_t* encoded_message)
    {
        if (!(encoded_message && encoded_message[0])) {
            return;
        }
        const auto title_copy = new wchar_t[wcslen(title) + 1];
        wcscpy(title_copy, title);
        GW::UI::AsyncDecodeStr(encoded_message, OnToastMessageDecoded, title_copy);
    }

    void OnMessageGlobal(GW::HookStatus*, GW::Packet::StoC::PacketBase* base)
    {
        const wchar_t* title = nullptr;
        const auto packet = static_cast<GW::Packet::StoC::MessageGlobal*>(base);
        switch (packet->channel) {
            case GW::Chat::Channel::CHANNEL_GUILD:
                if (show_notifications_on_guild_chat) {
                    title = L"Guild Chat";
                }
                if (flash_window_on_guild_chat) {
                    FlashWindow();
                }
                break;
            case GW::Chat::Channel::CHANNEL_ALLIANCE:
                if (show_notifications_on_ally_chat) {
                    title = L"Alliance Chat";
                }
                if (flash_window_on_ally_chat) {
                    FlashWindow();
                }
                break;
        }
        if (!title) {
            return;
        }
        const wchar_t* message_encoded = ToolboxUtils::GetMessageCore();
        const size_t msg_len = wcslen(packet->sender_name) + wcslen(packet->sender_guild) + wcslen(message_encoded) + 10;
        const auto message_including_sender = new wchar_t[msg_len];
        int written;
        if (packet->sender_guild[0]) {
            written = swprintf(message_including_sender, msg_len, L"\x108\x107%s [%s]: \x1\x2%s", packet->sender_name, packet->sender_guild, message_encoded);
        }
        else {
            written = swprintf(message_including_sender, msg_len, L"\x108\x107%s: \x1\x2%s", packet->sender_name, message_encoded);
        }
        ASSERT(written != -1);
        SendEncodedToastMessage(title, message_including_sender);
        delete[] message_including_sender;
    }

    void CheckLastToReady()
    {
        const uint32_t my_player_id = GW::PlayerMgr::GetPlayerNumber();
        const auto p = GW::PartyMgr::GetPartyInfo();
        if (!(p && p->players.size() > 1)) {
            return;
        }
        bool player_ticked = false;
        for (const auto& player : p->players) {
            if (player.login_number == my_player_id) {
                player_ticked = player.ticked();
                continue;
            }
            if (!player.ticked()) {
                return; // Other player not ticked yet.
            }
        }
        // This far; Everyone else is ticked up
        if (!player_ticked) {
            if (show_notifications_on_last_to_ready) {
                ToastNotifications::SendToast(party_ready_toast_title, L"You're the last player in your party to tick up", OnGenericToastActivated);
            }
            if (flash_window_on_last_to_ready) {
                FlashWindow();
            }
        }
        else {
            // Everyone including me is ticked
            if (show_notifications_on_everyone_ready) {
                ToastNotifications::SendToast(party_ready_toast_title, L"Everyone in your party is ticked up and ready to go!", OnGenericToastActivated);
            }
            if (flash_window_on_everyone_ready) {
                FlashWindow();
            }
        }
    }

    void OnPartyPlayerReady(GW::HookStatus*, GW::Packet::StoC::PacketBase* base)
    {
        const auto packet = static_cast<GW::Packet::StoC::PartyPlayerReady*>(base);
        if (packet->party_id == GetPartyId()) {
            CheckLastToReady();
        }
    }

    void OnMapChange(GW::HookStatus*, GW::Packet::StoC::PacketBase*)
    {
        ToastNotifications::DismissToast(party_ready_toast_title);
    }

    void OnAgentUpdateEffects(GW::HookStatus*, GW::Packet::StoC::PacketBase* base)
    {
        if (!show_notifications_on_self_resurrected && !flash_window_on_self_resurrected) {
            return;
        }
        const auto packet = static_cast<GW::Packet::StoC::AgentState*>(base);
        const GW::AgentLiving* current_character = GW::Agents::GetCharacter();
        if (!packet || !current_character) {
            return;
        }

        constexpr int AGENT_UPDATE_STATE_DEAD = 0x10;
        constexpr int AGENT_LIVING_TYPE_MAP_DEAD = 0x8;

        if (packet->agent_id == current_character->agent_id) {
            if ((packet->state & AGENT_UPDATE_STATE_DEAD) == 0 && (current_character->type_map & AGENT_LIVING_TYPE_MAP_DEAD) != 0) {
                if (show_notifications_on_self_resurrected) {
                    ToastNotifications::SendToast(L"Resurrected", L"You have been resurrected!", OnGenericToastActivated);
                }

                if (flash_window_on_self_resurrected) {
                    FlashWindow();
                }
            }
        }
    }

    void OnPartyInviteReceived(const GW::HookStatus* status, GW::Packet::StoC::PacketBase*)
    {
        if (status->blocked) {
            return;
        }
        if (GW::Map::GetInstanceType() != GW::Constants::InstanceType::Outpost || !GW::PartyMgr::GetIsLeader()) {
            return;
        }

        if (show_notifications_on_invite) {
            ToastNotifications::SendToast(L"Party Invite", L"You have been invited to a party!", OnGenericToastActivated);
        }
        if (flash_window_on_invite) {
            FlashWindow();
        }
    }

    struct StoC_Callback {
        uint32_t header = 0;
        GW::StoC::PacketCallback cb;
        GW::HookEntry hook_entry;

        StoC_Callback(const uint32_t _header, const GW::StoC::PacketCallback& _cb)
            : header(_header), cb(_cb) { }
    };

    std::vector<StoC_Callback> stoc_callbacks = {
        {GAME_SMSG_CHAT_MESSAGE_GLOBAL, OnMessageGlobal},
        {GAME_SMSG_PARTY_PLAYER_READY, OnPartyPlayerReady},
        {GAME_SMSG_INSTANCE_LOADED, OnMapChange},
        {GAME_SMSG_AGENT_UPDATE_EFFECTS, OnAgentUpdateEffects},
        {GAME_SMSG_PARTY_JOIN_REQUEST,OnPartyInviteReceived}
    };
} // namespace
ToastNotifications::Toast::Toast(const std::wstring& _title, const std::wstring& _message)
    : title(_title), message(_message) {};

ToastNotifications::Toast::~Toast()
{
    TriggerToastCallback(this, false);
    delete toast_template;
}

void ToastNotifications::Toast::toastActivated() const
{
    TriggerToastCallback(this, true);
}

void ToastNotifications::Toast::toastActivated(int) const
{
    TriggerToastCallback(this, true);
}

void ToastNotifications::Toast::toastDismissed(WinToastDismissalReason) const
{
    TriggerToastCallback(this, false);
};

void ToastNotifications::Toast::toastFailed() const
{
    Log::Error("Failed to show toast");
    TriggerToastCallback(this, false);
}

bool ToastNotifications::Toast::send()
{
    using namespace WinToastLib;
    const auto instance = WinToast::instance();
    if (!instance->isCompatible()) {
        return false;
    }
    if (!instance->isInitialized()) {
        instance->setAppName(L"Guild Wars");
        const auto version = GuiUtils::StringToWString(GWTOOLBOXDLL_VERSION);
        const auto aumi = WinToast::configureAUMI(L"gwtoolbox", L"GWToolbox++", L"GWToolbox++", version);
        instance->setAppUserModelId(aumi);
        if (!instance->initialize()) {
            return false;
        }
    }
    if (!toast_template) {
        toast_template = new WinToastTemplate(WinToastTemplate::WinToastTemplateType::Text02);
    }
    toast_template->setTextField(title, WinToastTemplate::FirstLine);
    toast_template->setTextField(message, WinToastTemplate::SecondLine);
    dismiss();
    toast_id = instance->showToast(*toast_template, this);
    return toast_id != -1;
}

bool ToastNotifications::Toast::dismiss()
{
    const bool ok = toast_id == -1 ? true : WinToastLib::WinToast::instance()->hideToast(toast_id);
    toast_id = -1;
    return ok;
}

bool ToastNotifications::DismissToast(const wchar_t* title)
{
    const auto found = toasts.find(title);
    if (found != toasts.end()) {
        delete found->second;
        toasts.erase(found);
    }
    return true;
}

ToastNotifications::Toast* ToastNotifications::SendToast(const wchar_t* title, const wchar_t* message, const OnToastCallback callback, void* extra_args)
{
    if (!IsCompatible()) {
        return nullptr;
    }
    if (!CanNotify()) {
        return nullptr;
    }
    const auto found = toasts.find(title);
    Toast* toast = found != toasts.end() ? found->second : nullptr;
    if (!toast) {
        toast = new Toast(title, message);
        toasts[toast->title] = toast;
    }
    else if (toast->message == message) {
        return toast; // Avoid spamming desktop notifications
    }
    toast->message = message;
    toast->callback = callback;
    toast->extra_args = extra_args;
    if (!toast->send()) {
        TriggerToastCallback(toast, false);
        return nullptr;
    }
    return toast;
}

void ToastNotifications::Initialize()
{
    ToolboxModule::Initialize();

    is_platform_compatible = WinToastLib::WinToast::isCompatible();
    GW::Chat::RegisterWhisperCallback(&OnWhisper_Entry, OnWhisper);
    for (auto& callback : stoc_callbacks) {
        GW::StoC::RegisterPacketCallback(&callback.hook_entry, callback.header, callback.cb, 0x8000);
    }
}

bool ToastNotifications::IsCompatible()
{
    return is_platform_compatible;
}

void ToastNotifications::Terminate()
{
    ToolboxModule::Terminate();
    for (auto& callback : stoc_callbacks) {
        GW::StoC::RemoveCallback(callback.header, &callback.hook_entry);
    }
    GW::Chat::RemoveWhisperCallback(&OnWhisper_Entry);
    for (const auto& toast : toasts | std::views::values) {
        TriggerToastCallback(toast, false);
        delete toast;
    }
    toasts.clear();
}

void ToastNotifications::DrawSettingsInternal()
{
    ToolboxModule::DrawSettingsInternal();
    ImGui::TextDisabled("GWToolbox++ can send notifications and flash the taskbar on certain in-game triggers.");

    constexpr float checkbox_w = 150.f;

    ImGui::PushID("desktop_notifications");
    ImGui::Text("Send a desktop notification on:");
    ImGui::Indent();
    ImGui::StartSpacedElements(checkbox_w);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Whisper", &show_notifications_on_whisper);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Guild Chat", &show_notifications_on_guild_chat);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Alliance Chat", &show_notifications_on_ally_chat);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Party Invite", &show_notifications_on_invite);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Last to Tick", &show_notifications_on_last_to_ready);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Everyone Ticked", &show_notifications_on_everyone_ready);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Resurrection", &show_notifications_on_self_resurrected);
    ImGui::Unindent();
    ImGui::PopID();

    ImGui::PushID("flash_taskbar");
    ImGui::Text("Flash taskbar on:");
    ImGui::Indent();
    ImGui::StartSpacedElements(checkbox_w);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Whisper", &flash_window_on_whisper);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Guild Chat", &flash_window_on_guild_chat);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Alliance Chat", &flash_window_on_ally_chat);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Party Invite", &flash_window_on_invite);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Last to Tick", &flash_window_on_last_to_ready);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Everyone Ticked", &flash_window_on_everyone_ready);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Resurrection", &flash_window_on_self_resurrected);
    ImGui::Unindent();
    ImGui::PopID();

    ImGui::Text("Allow these notifications when Guild Wars is:");
    ImGui::Indent();
    ImGui::StartSpacedElements(checkbox_w);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("Minimised", &show_notifications_when_minimised);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("In Background", &show_notifications_when_in_background);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("In Focus", &show_notifications_when_focussed);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("In Outpost", &show_notifications_when_in_outpost);
    ImGui::NextSpacedElement();
    ImGui::Checkbox("In Explorable", &show_notifications_when_in_explorable);
    ImGui::Unindent();
}

void ToastNotifications::LoadSettings(ToolboxIni* ini)
{
    ToolboxModule::LoadSettings(ini);

    LOAD_BOOL(show_notifications_when_minimised);
    LOAD_BOOL(show_notifications_when_in_background);
    LOAD_BOOL(show_notifications_when_focussed);
    LOAD_BOOL(show_notifications_when_in_outpost);
    LOAD_BOOL(show_notifications_when_in_explorable);

    LOAD_BOOL(show_notifications_on_whisper);
    LOAD_BOOL(show_notifications_on_guild_chat);
    LOAD_BOOL(show_notifications_on_ally_chat);
    LOAD_BOOL(show_notifications_on_invite);
    LOAD_BOOL(show_notifications_on_last_to_ready);
    LOAD_BOOL(show_notifications_on_everyone_ready);
    LOAD_BOOL(show_notifications_on_self_resurrected);

    LOAD_BOOL(flash_window_on_whisper);
    LOAD_BOOL(flash_window_on_guild_chat);
    LOAD_BOOL(flash_window_on_ally_chat);
    LOAD_BOOL(flash_window_on_invite);
    LOAD_BOOL(flash_window_on_last_to_ready);
    LOAD_BOOL(flash_window_on_everyone_ready);
    LOAD_BOOL(flash_window_on_self_resurrected);
}

void ToastNotifications::SaveSettings(ToolboxIni* ini)
{
    ToolboxModule::SaveSettings(ini);

    SAVE_BOOL(show_notifications_when_minimised);
    SAVE_BOOL(show_notifications_when_in_background);
    SAVE_BOOL(show_notifications_when_focussed);
    SAVE_BOOL(show_notifications_when_in_outpost);
    SAVE_BOOL(show_notifications_when_in_explorable);

    SAVE_BOOL(show_notifications_on_whisper);
    SAVE_BOOL(show_notifications_on_guild_chat);
    SAVE_BOOL(show_notifications_on_ally_chat);
    SAVE_BOOL(show_notifications_on_last_to_ready);
    SAVE_BOOL(show_notifications_on_everyone_ready);
    SAVE_BOOL(show_notifications_on_self_resurrected);

    SAVE_BOOL(flash_window_on_whisper);
    SAVE_BOOL(flash_window_on_guild_chat);
    SAVE_BOOL(flash_window_on_ally_chat);
    SAVE_BOOL(flash_window_on_last_to_ready);
    SAVE_BOOL(flash_window_on_everyone_ready);
    SAVE_BOOL(flash_window_on_self_resurrected);
}
