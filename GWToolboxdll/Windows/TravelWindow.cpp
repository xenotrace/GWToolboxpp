#include "stdafx.h"

#include <GWCA/Constants/Constants.h>

#include <GWCA/GameContainers/GamePos.h>

#include <GWCA/GameEntities/Guild.h>
#include <GWCA/GameEntities/Map.h>

#include <GWCA/Context/GameContext.h>
#include <GWCA/Context/PartyContext.h>
#include <GWCA/Context/GuildContext.h>

#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/GuildMgr.h>
#include <GWCA/Managers/PartyMgr.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/ItemMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/UIMgr.h>

#include <Utils/GuiUtils.h>

#include <Modules/Resources.h>
#include <Windows/TravelWindow.h>
#include <Windows/TravelWindowConstants.h>

namespace {
    std::vector<char*> searchable_explorable_areas{};
    std::vector<GuiUtils::EncString*> searchable_explorable_areas_decode{};
    std::vector<GW::Constants::MapID> searchable_explorable_area_ids{};

    enum class FetchedMapNames : uint8_t {
        Pending,
        Decoding,
        Decoded,
        Ready
    } fetched_searchable_explorable_areas = FetchedMapNames::Pending;

    bool ImInPresearing() { return GW::Map::GetCurrentMapInfo()->region == GW::Region_Presearing; }

    bool IsInGH()
    {
        const auto* p = GW::GuildMgr::GetPlayerGuild();
        return p && p == GW::GuildMgr::GetCurrentGH();
    }

    bool IsLuxon()
    {
        GW::GuildContext* c = GW::GetGuildContext();
        return c && c->player_guild_index && c->guilds[c->player_guild_index]->faction;
    }

    bool IsAlreadyInOutpost(const GW::Constants::MapID outpost_id, const GW::Constants::District _district, const uint32_t _district_number = 0)
    {
        return GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost
            && GW::Map::GetMapID() == outpost_id
            && TravelWindow::RegionFromDistrict(_district) == GW::Map::GetRegion()
            && TravelWindow::LanguageFromDistrict(_district) == GW::Map::GetLanguage()
            && (!_district_number || _district_number == static_cast<uint32_t>(GW::Map::GetDistrict()));
    }


    struct MapStruct {
        GW::Constants::MapID map_id = GW::Constants::MapID::None;
        int region_id = 0;
        int language_id = 0;
        uint32_t district_number = 0;
    };

    struct UIErrorMessage {
        int error_index;
        wchar_t* message;
    };

    bool retry_map_travel = false;
    MapStruct pending_map_travel;

    GW::UI::UIMessage messages_to_hook[] = {
        GW::UI::UIMessage::kErrorMessage,
        GW::UI::UIMessage::kMapChange,
        GW::UI::UIMessage::kTravel
    };
    GW::HookEntry OnUIMessage_HookEntry;

    void OnUIMessage(GW::HookStatus* status, const GW::UI::UIMessage message_id, void* wparam, void*)
    {
        switch (message_id) {
        case GW::UI::UIMessage::kMapChange: {
            pending_map_travel.map_id = GW::Constants::MapID::None;
        }
                                          break;
        case GW::UI::UIMessage::kTravel: {
            const auto t = static_cast<MapStruct*>(wparam);
            if (t && t != &pending_map_travel) {
                pending_map_travel = *t;
            }
        }
                                       break;
        case GW::UI::UIMessage::kErrorMessage: {
            if (!(retry_map_travel && pending_map_travel.map_id != GW::Constants::MapID::None)) {
                break;
            }
            const auto msg = static_cast<UIErrorMessage*>(wparam);
            if (msg && msg->message && *msg->message == 0xb25) {
                // Travel failed, but we want to retry
                // NB: 0xb25 = "That district is full. Please select another."
                status->blocked = true;
                SendUIMessage(GW::UI::UIMessage::kTravel, &pending_map_travel);
            }
        }
                                             break;
        }
    }
}

void TravelWindow::Initialize()
{
    ToolboxWindow::Initialize();
    scroll_texture = Resources::GetItemImage(L"Passage Scroll to the Deep");
    district = GW::Constants::District::Current;
    district_number = 0;

    GW::Chat::CreateCommand(L"tp", &CmdTP);
    GW::Chat::CreateCommand(L"to", &CmdTP);
    GW::Chat::CreateCommand(L"travel", &CmdTP);

    for (const auto message_id : messages_to_hook) {
        RegisterUIMessageCallback(&OnUIMessage_HookEntry, message_id, OnUIMessage);
    }
}

void TravelWindow::Terminate()
{
    ToolboxWindow::Terminate();
    for (const auto it : searchable_explorable_areas) {
        delete[] it;
    }
    searchable_explorable_areas.clear();
    GW::UI::RemoveUIMessageCallback(&OnUIMessage_HookEntry);
}

void TravelWindow::TravelButton(const char* text, const int x_idx, const GW::Constants::MapID mapid)
{
    if (x_idx != 0) {
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    }
    const float w = (ImGui::GetWindowWidth() - ImGui::GetStyle().ItemInnerSpacing.x) / 2 - ImGui::GetStyle().WindowPadding.x;
    bool clicked = false;
    switch (mapid) {
    case GW::Constants::MapID::The_Deep:
    case GW::Constants::MapID::Urgozs_Warren:
        clicked |= ImGui::IconButton(text, *scroll_texture, ImVec2(w, 0));
        break;
    default:
        clicked |= ImGui::Button(text, ImVec2(w, 0));
        break;
    }
    if (clicked) {
        Travel(mapid, district, district_number);
        if (close_on_travel) {
            visible = false;
        }
    }
}

bool TravelWindow::PlayerHasAnyMissingOutposts(const bool presearing) const {
    if (presearing) {
        if (!GW::Map::GetIsMapUnlocked(GW::Constants::MapID::Ascalon_City_pre_searing))return true;
        if (!GW::Map::GetIsMapUnlocked(GW::Constants::MapID::Ashford_Abbey_outpost))return true;
        if (!GW::Map::GetIsMapUnlocked(GW::Constants::MapID::Foibles_Fair_outpost))return true;
        if (!GW::Map::GetIsMapUnlocked(GW::Constants::MapID::Fort_Ranik_pre_Searing_outpost))return true;
        if (!GW::Map::GetIsMapUnlocked(GW::Constants::MapID::The_Barradin_Estate_outpost))return true;
    }
    else {
        for (int i = 0; i < (int)outpost_names.size(); ++i) {
            if (!GW::Map::GetIsMapUnlocked(IndexToOutpostID(i)))return true;
        }
    }

    return false;
}

void TravelWindow::DrawMissingOutpostsList(const bool presearing) const {
    auto AddMissingOutpostByIdx = [](int map_idx) {
        if (map_idx < 0 || map_idx >= static_cast<int>(outpost_names.size()))return;
        if (!GW::Map::GetIsMapUnlocked(IndexToOutpostID(map_idx)))ImGui::Selectable(outpost_names[map_idx]);
        };

    auto AddMissingOutpostById = [](const char* name, const GW::Constants::MapID map_id) {
        if (!GW::Map::GetIsMapUnlocked(map_id))ImGui::Selectable(name);
        };

    if (presearing) {
        if (ImGui::BeginCombo("Missing outposts...", NULL, ImGuiComboFlags_NoPreview | ImGuiComboFlags_HeightLargest)) {
            AddMissingOutpostById("Ascalon City", GW::Constants::MapID::Ascalon_City_pre_searing);
            AddMissingOutpostById("Ashford Abbey", GW::Constants::MapID::Ashford_Abbey_outpost);
            AddMissingOutpostById("Foible's Fair", GW::Constants::MapID::Foibles_Fair_outpost);
            AddMissingOutpostById("Fort Ranik", GW::Constants::MapID::Fort_Ranik_pre_Searing_outpost);
            AddMissingOutpostById("The Barradin Estate", GW::Constants::MapID::The_Barradin_Estate_outpost);
            ImGui::EndCombo();
        }
    }
    else {
        if (ImGui::BeginCombo("Missing outposts...", NULL, ImGuiComboFlags_NoPreview | ImGuiComboFlags_HeightLargest)) {
            for (int i = 0; i < static_cast<int>(outpost_names.size()); ++i) {
                AddMissingOutpostByIdx(i);
            }
            ImGui::EndCombo();
        }
    }
}

bool outpost_name_array_getter(void* /* _data */, int idx, const char** out_text) {
    if (idx < 0 || static_cast<size_t>(idx) > outpost_names.size()) {
        return false;
    }

    *out_text = outpost_names[idx];
    return true;
}

void TravelWindow::Draw(IDirect3DDevice9*)
{
    if (!visible) {
        return;
    }

    ImGui::SetNextWindowCenter(ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(Name(), GetVisiblePtr(), GetWinFlags())) {
        if (ImInPresearing()) {
            TravelButton("Ascalon City", 0, GW::Constants::MapID::Ascalon_City_pre_searing);
            TravelButton("Ashford Abbey", 1, GW::Constants::MapID::Ashford_Abbey_outpost);
            TravelButton("Foible's Fair", 0, GW::Constants::MapID::Foibles_Fair_outpost);
            TravelButton("Fort Ranik", 1, GW::Constants::MapID::Fort_Ranik_pre_Searing_outpost);
            TravelButton("The Barradin Estate", 0, GW::Constants::MapID::The_Barradin_Estate_outpost);

            if (PlayerHasAnyMissingOutposts(true)) {
                ImGui::PushItemWidth(-1.0f);
                DrawMissingOutpostsList(true);
                ImGui::PopItemWidth();
            }
        }
        else {
            ImGui::PushItemWidth(-1.0f);
            static int travelto_index = -1;
            if (ImGui::MyCombo("travelto", "Travel To...", &travelto_index, outpost_name_array_getter, nullptr, outpost_names.size())) {
                const auto map_id = IndexToOutpostID(travelto_index);
                Travel(map_id, district, district_number);
                travelto_index = -1;
                if (close_on_travel) {
                    visible = false;
                }
            }

            if (PlayerHasAnyMissingOutposts(false)) {
                DrawMissingOutpostsList(false);
            }

            static int district_index = 0;
            if (ImGui::Combo("###district", &district_index, district_words.data(), district_words.size())) {
                district_number = 0;
                if (static_cast<size_t>(district_index) < district_ids.size()) {
                    district = district_ids[district_index];
                    if (district_index == 3) { // American 1
                        district_number = 1;
                    }
                }
            }
            ImGui::PopItemWidth();

            TravelButton("ToA", 0, GW::Constants::MapID::Temple_of_the_Ages);
            TravelButton("DoA", 1, GW::Constants::MapID::Domain_of_Anguish);
            TravelButton("Kamadan", 0, GW::Constants::MapID::Kamadan_Jewel_of_Istan_outpost);
            TravelButton("Embark", 1, GW::Constants::MapID::Embark_Beach);
            TravelButton("Vlox's", 0, GW::Constants::MapID::Vloxs_Falls);
            TravelButton("Gadd's", 1, GW::Constants::MapID::Gadds_Encampment_outpost);
            TravelButton("Urgoz", 0, GW::Constants::MapID::Urgozs_Warren);
            TravelButton("Deep", 1, GW::Constants::MapID::The_Deep);

            for (auto i = 0; i < fav_count; i++) {
                ImGui::PushID(i);
                ImGui::PushItemWidth(-40.0f - ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::MyCombo("", "Select a favorite", &fav_index[static_cast<size_t>(i)], outpost_name_array_getter, nullptr, outpost_names.size());
                ImGui::PopItemWidth();
                ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::Button("Go", ImVec2(40.0f, 0))) {
                    TravelFavorite(static_cast<size_t>(i));
                }
                ImGui::PopID();
            }
        }
        if (::pending_map_travel.map_id != GW::Constants::MapID::None) {
            const auto map_id_it = std::ranges::find(outpost_ids, ::pending_map_travel.map_id);
            if (map_id_it != std::ranges::end(outpost_ids)) {
                const auto map_idx = std::distance(outpost_ids.begin(), map_id_it);
                const auto abort_str = std::format("Abort retrying travel to {}", outpost_names[map_idx]);
                if (ImGui::Button(abort_str.c_str())) {
                    ::pending_map_travel.map_id = GW::Constants::MapID::None;
                }
            }
            else {
                if (ImGui::Button("Abort retrying travel")) {
                    ::pending_map_travel.map_id = GW::Constants::MapID::None;
                }
            }
        }
    }
    ImGui::End();
}

void TravelWindow::Update(const float)
{
    if (scroll_to_outpost_id != GW::Constants::MapID::None) {
        ScrollToOutpost(scroll_to_outpost_id); // We're in the process of scrolling to an outpost
    }
    // Dynamically generate a list of all explorable areas that the game has rather than storing another massive const array.
    switch (fetched_searchable_explorable_areas) {
    case FetchedMapNames::Pending:
        for (auto i = 0u; i < static_cast<uint32_t>(GW::Constants::MapID::Count); i++) {
            const GW::AreaInfo* map = GW::Map::GetMapInfo(static_cast<GW::Constants::MapID>(i));
            if (!map || !map->name_id || !map->GetIsOnWorldMap() || map->type != GW::RegionType::ExplorableZone) {
                continue;
            }
            searchable_explorable_area_ids.push_back(static_cast<GW::Constants::MapID>(i));
            auto s = new GuiUtils::EncString(map->name_id);
            s->string(); // Trigger decode
            searchable_explorable_areas_decode.push_back(s);
        }
        fetched_searchable_explorable_areas = FetchedMapNames::Decoding;
        break;
    case FetchedMapNames::Decoding: {
        bool ok = true;
        for (size_t i = 0; i < searchable_explorable_areas_decode.size() && ok; i++) {
            ok = !searchable_explorable_areas_decode[i]->string().empty();
        }
        if (ok) {
            fetched_searchable_explorable_areas = FetchedMapNames::Decoded;
        }
    }
                                  break;
    case FetchedMapNames::Decoded:
        for (size_t i = 0; i < searchable_explorable_areas_decode.size(); i++) {
            std::string sanitised = GuiUtils::ToLower(GuiUtils::RemovePunctuation(searchable_explorable_areas_decode[i]->string()));
            auto out = new char[sanitised.length() + 1]; // NB: Delete this char* in destructor
            strcpy(out, sanitised.c_str());
            delete searchable_explorable_areas_decode[i];
            searchable_explorable_areas.push_back(out);
        }
        searchable_explorable_areas_decode.clear();
        fetched_searchable_explorable_areas = FetchedMapNames::Ready;
        break;
    }
}

GW::Constants::MapID TravelWindow::GetNearestOutpost(const GW::Constants::MapID map_to)
{
    const GW::AreaInfo* this_map = GW::Map::GetMapInfo(map_to);
    float nearest_distance = std::numeric_limits<float>::max();
    auto nearest_map_id = GW::Constants::MapID::None;

    auto get_pos = [](const GW::AreaInfo* map) {
        GW::Vec2f pos = { static_cast<float>(map->x), static_cast<float>(map->y) };
        if (!pos.x) {
            pos.x = static_cast<float>(map->icon_start_x + (map->icon_end_x - map->icon_start_x) / 2);
            pos.y = static_cast<float>(map->icon_start_y + (map->icon_end_y - map->icon_start_y) / 2);
        }
        if (!pos.x) {
            pos.x = static_cast<float>(map->icon_start_x_dupe + (map->icon_end_x_dupe - map->icon_start_x_dupe) / 2);
            pos.y = static_cast<float>(map->icon_start_y_dupe + (map->icon_end_y_dupe - map->icon_start_y_dupe) / 2);
        }
        return pos;
        };

    GW::Vec2f this_pos = get_pos(this_map);
    if (!this_pos.x) {
        this_pos = { static_cast<float>(this_map->icon_start_x), static_cast<float>(this_map->icon_start_y) };
    }
    for (size_t i = 0; i < static_cast<size_t>(GW::Constants::MapID::Count); i++) {
        const GW::AreaInfo* map_info = GW::Map::GetMapInfo(static_cast<GW::Constants::MapID>(i));
        if (!map_info || !map_info->thumbnail_id || !map_info->GetIsOnWorldMap()) {
            continue;
        }
        if (map_info->campaign != this_map->campaign || map_info->region == GW::Region_Presearing) {
            continue;
        }
        switch (map_info->type) {
        case GW::RegionType::City:
        case GW::RegionType::CompetitiveMission:
        case GW::RegionType::CooperativeMission:
        case GW::RegionType::EliteMission:
        case GW::RegionType::MissionOutpost:
        case GW::RegionType::Outpost:
            break;
        default:
            continue;
        }
        //if ((map_info->flags & 0x5000000) != 0)
        //   continue; // e.g. "wrong" augury rock is map 119, no NPCs
        if (!GW::Map::GetIsMapUnlocked(static_cast<GW::Constants::MapID>(i))) {
            continue;
        }
        const float dist = GetDistance(this_pos, get_pos(map_info));
        if (dist < nearest_distance) {
            nearest_distance = dist;
            nearest_map_id = static_cast<GW::Constants::MapID>(i);
        }
    }
    return nearest_map_id;
}

bool TravelWindow::IsWaitingForMapTravel()
{
    return GW::GetGameContext()->party != nullptr && (GW::GetGameContext()->party->flag & 0x8) > 0;
}

void TravelWindow::ScrollToOutpost(const GW::Constants::MapID outpost_id, const GW::Constants::District _district, const uint32_t _district_number)
{
    if (!GW::Map::GetIsMapLoaded() || (!GW::PartyMgr::GetIsPartyLoaded() && GW::Map::GetInstanceType() != GW::Constants::InstanceType::Explorable)) {
        map_travel_countdown_started = false;
        pending_map_travel = false;
        return; // Map loading, so we're no longer waiting for travel timer to start or finish.
    }
    if (IsWaitingForMapTravel()) {
        map_travel_countdown_started = true;
        return; // Currently in travel countdown. Wait until the countdown is complete or cancelled.
    }
    if (map_travel_countdown_started) {
        pending_map_travel = false;
        map_travel_countdown_started = false;
        scroll_to_outpost_id = GW::Constants::MapID::None;
        return; // We were waiting for countdown, but it was cancelled.
    }
    if (pending_map_travel) {
        return; // Checking too soon; still waiting for either a map travel or a countdown for it.
    }

    const GW::Constants::MapID map_id = GW::Map::GetMapID();
    if (scroll_to_outpost_id == GW::Constants::MapID::None) {
        scroll_to_outpost_id = outpost_id;
        scroll_from_outpost_id = map_id;
    }
    if (scroll_to_outpost_id != outpost_id) {
        return; // Already travelling to another outpost
    }
    if (map_id == outpost_id) {
        scroll_to_outpost_id = GW::Constants::MapID::None;
        if (!IsAlreadyInOutpost(outpost_id, _district, _district_number)) {
            UITravel(outpost_id, _district, _district_number);
        }
        return; // Already at this outpost. Called GW::Map::Travel just in case district is different.
    }

    uint32_t scroll_model_id = 0;
    bool is_ready_to_scroll = map_id == GW::Constants::MapID::Embark_Beach;
    switch (scroll_to_outpost_id) {
    case GW::Constants::MapID::The_Deep:
        scroll_model_id = 22279;
        is_ready_to_scroll |= map_id == GW::Constants::MapID::Cavalon_outpost;
        break;
    case GW::Constants::MapID::Urgozs_Warren:
        scroll_model_id = 3256;
        is_ready_to_scroll |= map_id == GW::Constants::MapID::House_zu_Heltzer_outpost;
        break;
    default:
        Log::Error("Invalid outpost for scrolling");
        return;
    }
    if (!is_ready_to_scroll && scroll_from_outpost_id != map_id) {
        scroll_to_outpost_id = GW::Constants::MapID::None;
        return; // Not in scrollable outpost, but we're not in the outpost we started from either - user has decided to travel somewhere else.
    }

    const GW::Item* scroll_to_use = GW::Items::GetItemByModelId(
        scroll_model_id,
        static_cast<int>(GW::Constants::Bag::Backpack),
        static_cast<int>(GW::Constants::Bag::Storage_14));
    if (!scroll_to_use) {
        scroll_to_outpost_id = GW::Constants::MapID::None;
        Log::Error("No scroll found in inventory for travel");
        return; // No scroll found.
    }
    if (is_ready_to_scroll) {
        scroll_to_outpost_id = GW::Constants::MapID::None;
        GW::Items::UseItem(scroll_to_use);
        return; // Done.
    }
    pending_map_travel = Travel(GW::Constants::MapID::Embark_Beach, _district, _district_number);
    //GW::Map::Travel(GW::Constants::MapID::Embark_Beach, district, district_number); // Travel to embark.
}

bool TravelWindow::Travel(const GW::Constants::MapID map_id, const GW::Constants::District _district /*= 0*/, const uint32_t _district_number)
{
    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading) {
        return false;
    }
    if (!GW::Map::GetIsMapUnlocked(map_id)) {
        const GW::AreaInfo* map = GW::Map::GetMapInfo(map_id);
        wchar_t map_name_buf[8];
        constexpr wchar_t err_message_buf[256] = L"[Error] Your character does not have that map unlocked";
        if (map && map->name_id && GW::UI::UInt32ToEncStr(map->name_id, map_name_buf, 8)) {
            Log::ErrorW(L"[Error] Your character does not have \x1\x2%s\x2\x108\x107 unlocked", map_name_buf);
        }
        else {
            Log::ErrorW(err_message_buf);
        }
        return false;
    }
    if (IsAlreadyInOutpost(map_id, _district, _district_number)) {
        Log::Error("[Error] You are already in the outpost");
        return false;
    }
    switch (map_id) {
    case GW::Constants::MapID::The_Deep:
    case GW::Constants::MapID::Urgozs_Warren:
        ScrollToOutpost(map_id, _district, _district_number);
        break;
    default:
        UITravel(map_id, _district, _district_number);
        break;
    }
    return true;
    //return GW::Map::Travel(map_id, District, district_number);
}

void TravelWindow::UITravel(const GW::Constants::MapID MapID, const GW::Constants::District district, const uint32_t district_number)
{
    GW::GameThread::Enqueue([=] {
        auto map_struct = MapStruct{ MapID, RegionFromDistrict(district), LanguageFromDistrict(district), district_number };
        SendUIMessage(GW::UI::UIMessage::kTravel, &map_struct);
        });
}

int TravelWindow::RegionFromDistrict(const GW::Constants::District _district)
{
    switch (_district) {
    case GW::Constants::District::International:
        return GW::Constants::Region::International;
    case GW::Constants::District::American:
        return GW::Constants::Region::America;
    case GW::Constants::District::EuropeEnglish:
    case GW::Constants::District::EuropeFrench:
    case GW::Constants::District::EuropeGerman:
    case GW::Constants::District::EuropeItalian:
    case GW::Constants::District::EuropeSpanish:
    case GW::Constants::District::EuropePolish:
    case GW::Constants::District::EuropeRussian:
        return GW::Constants::Region::Europe;
    case GW::Constants::District::AsiaKorean:
        return GW::Constants::Region::Korea;
    case GW::Constants::District::AsiaChinese:
        return GW::Constants::Region::China;
    case GW::Constants::District::AsiaJapanese:
        return GW::Constants::Region::Japan;
    default:
        break;
    }
    return GW::Map::GetRegion();
}

int TravelWindow::LanguageFromDistrict(const GW::Constants::District _district)
{
    switch (_district) {
    case GW::Constants::District::EuropeEnglish:
        return GW::Constants::EuropeLanguage::English;
    case GW::Constants::District::EuropeFrench:
        return GW::Constants::EuropeLanguage::French;
    case GW::Constants::District::EuropeGerman:
        return GW::Constants::EuropeLanguage::German;
    case GW::Constants::District::EuropeItalian:
        return GW::Constants::EuropeLanguage::Italian;
    case GW::Constants::District::EuropeSpanish:
        return GW::Constants::EuropeLanguage::Spanish;
    case GW::Constants::District::EuropePolish:
        return GW::Constants::EuropeLanguage::Polish;
    case GW::Constants::District::EuropeRussian:
        return GW::Constants::EuropeLanguage::Russian;
    case GW::Constants::District::AsiaKorean:
    case GW::Constants::District::AsiaChinese:
    case GW::Constants::District::AsiaJapanese:
    case GW::Constants::District::International:
    case GW::Constants::District::American:
        return 0;
    default:
        break;
    }
    return GW::Map::GetLanguage();
}

bool TravelWindow::TravelFavorite(const unsigned int idx)
{
    if (idx >= fav_index.size()) {
        return false;
    }
    Travel(IndexToOutpostID(fav_index[idx]), district, district_number);
    if (close_on_travel) {
        visible = false;
    }
    return true;
}

void TravelWindow::DrawSettingsInternal()
{
    ImGui::Checkbox("Close on travel", &close_on_travel);
    ImGui::ShowHelp("Will close the travel window when clicking on a travel destination");
    ImGui::PushItemWidth(100.0f);
    if (ImGui::InputInt("Number of favorites", &fav_count)) {
        if (fav_count < 0) {
            fav_count = 0;
        }
        if (fav_count > 100) {
            fav_count = 100;
        }
        fav_index.resize(static_cast<size_t>(fav_count), -1);
    }
    ImGui::PopItemWidth();
    ImGui::Checkbox("Automatically retry if the district is full", &retry_map_travel);
    ImGui::ShowHelp("Use /tp stop to stop retrying.");
}

void TravelWindow::LoadSettings(ToolboxIni* ini)
{
    ToolboxWindow::LoadSettings(ini);


    fav_count = ini->GetLongValue(Name(), VAR_NAME(fav_count), 3);
    fav_index.resize(static_cast<size_t>(fav_count), -1);
    for (auto i = 0; i < fav_count; i++) {
        char key[32];
        snprintf(key, 32, "Fav%d", i);
        fav_index[static_cast<size_t>(i)] = ini->GetLongValue(Name(), key, -1);
    }
    LOAD_BOOL(close_on_travel);
    LOAD_BOOL(retry_map_travel);
}

void TravelWindow::SaveSettings(ToolboxIni* ini)
{
    ToolboxWindow::SaveSettings(ini);
    SAVE_UINT(fav_count);
    for (auto i = 0; i < fav_count; i++) {
        const auto ui = static_cast<size_t>(i);
        char key[32];
        snprintf(key, 32, "Fav%d", i);
        ini->SetLongValue(Name(), key, fav_index[ui]);
    }
    SAVE_BOOL(close_on_travel);
    SAVE_BOOL(retry_map_travel);
}

GW::Constants::MapID TravelWindow::IndexToOutpostID(const int index)
{
    if (static_cast<size_t>(index) < outpost_ids.size()) {
        return outpost_ids[index];
    }
    return GW::Constants::MapID::Great_Temple_of_Balthazar_outpost;
}

void TravelWindow::CmdTP(const wchar_t*, const int argc, const LPWSTR* argv)
{
    // zero argument error
    if (argc == 1) {
        Log::Error("[Error] Please provide an argument");
        return;
    }
    GW::Constants::MapID outpost = GW::Map::GetMapID();
    auto district = GW::Constants::District::Current;
    uint32_t district_number = 0;

    std::wstring argOutpost = GuiUtils::ToLower(argv[1]);
    const std::wstring argDistrict = GuiUtils::ToLower(argv[argc - 1]);
    if (argOutpost == L"stop") {
        ::pending_map_travel.map_id = GW::Constants::MapID::None;
        Instance().pending_map_travel = false;
        return;
    }
    // Guild hall
    if (argOutpost == L"gh") {
        if (IsInGH()) {
            GW::GuildMgr::LeaveGH();
        }
        else {
            GW::GuildMgr::TravelGH();
        }
        return;
    }
    TravelWindow& instance = Instance();
    if (argOutpost.size() > 2 && argOutpost.compare(0, 3, L"fav", 3) == 0) {
        const std::wstring fav_s_num = argOutpost.substr(3, std::wstring::npos);
        if (fav_s_num.empty()) {
            instance.TravelFavorite(0);
            return;
        }
        uint32_t fav_num;
        if (GuiUtils::ParseUInt(fav_s_num.c_str(), &fav_num) && fav_num > 0) {
            instance.TravelFavorite(fav_num - 1);
            return;
        }
        Log::Error("[Error] Did not recognize favourite");
        return;
    }
    for (auto i = 2; i < argc - 1; i++) {
        // Outpost name can be anything after "/tp" but before the district e.g. "/tp house zu heltzer ae1"
        argOutpost.append(L" ");
        argOutpost.append(GuiUtils::ToLower(argv[i]));
    }
    const bool isValidDistrict = ParseDistrict(argDistrict, district, district_number);
    if (isValidDistrict && argc == 2) {
        // e.g. "/tp ae1"
        instance.Travel(outpost, district, district_number); // NOTE: ParseDistrict sets district and district_number vars by reference.
        return;
    }
    if (!isValidDistrict && argc > 2) {
        // e.g. "/tp house zu heltzer"
        argOutpost.append(L" ");
        argOutpost.append(argDistrict);
    }
    if (ParseOutpost(argOutpost, outpost, district, district_number)) {
        const wchar_t first_char_of_last_arg = *argv[argc - 1];
        switch (outpost) {
        case GW::Constants::MapID::Vizunah_Square_Foreign_Quarter_outpost:
        case GW::Constants::MapID::Vizunah_Square_Local_Quarter_outpost:
            if (first_char_of_last_arg == 'l') // - e.g. /tp viz local
            {
                outpost = GW::Constants::MapID::Vizunah_Square_Local_Quarter_outpost;
            }
            else if (first_char_of_last_arg == 'f') {
                outpost = GW::Constants::MapID::Vizunah_Square_Foreign_Quarter_outpost;
            }
            break;
        case GW::Constants::MapID::Fort_Aspenwood_Luxon_outpost:
        case GW::Constants::MapID::Fort_Aspenwood_Kurzick_outpost:
            if (first_char_of_last_arg == 'l') // - e.g. /tp fa lux
            {
                outpost = GW::Constants::MapID::Fort_Aspenwood_Luxon_outpost;
            }
            else if (first_char_of_last_arg == 'k') {
                outpost = GW::Constants::MapID::Fort_Aspenwood_Kurzick_outpost;
            }
            else {
                outpost = IsLuxon() ? GW::Constants::MapID::Fort_Aspenwood_Luxon_outpost : GW::Constants::MapID::Fort_Aspenwood_Kurzick_outpost;
            }
            break;
        case GW::Constants::MapID::The_Jade_Quarry_Kurzick_outpost:
        case GW::Constants::MapID::The_Jade_Quarry_Luxon_outpost:
            if (first_char_of_last_arg == 'l') // - e.g. /tp jq lux
            {
                outpost = GW::Constants::MapID::The_Jade_Quarry_Luxon_outpost;
            }
            else if (first_char_of_last_arg == 'k') {
                outpost = GW::Constants::MapID::Fort_Aspenwood_Kurzick_outpost;
            }
            else {
                outpost = IsLuxon() ? GW::Constants::MapID::The_Jade_Quarry_Luxon_outpost : GW::Constants::MapID::The_Jade_Quarry_Kurzick_outpost;
            }
            break;
        default:
            break;
        }
        instance.Travel(outpost, district, district_number); // NOTE: ParseOutpost sets outpost, district and district_number vars by reference.
        return;
    }
    Log::Error("[Error] Did not recognize outpost '%ls'", argOutpost.c_str());
}

bool TravelWindow::ParseOutpost(const std::wstring& s, GW::Constants::MapID& outpost, GW::Constants::District& district, const uint32_t&)
{
    // By Map ID e.g. "/tp 77" for house zu heltzer
    uint32_t map_id = 0;
    if (GuiUtils::ParseUInt(s.c_str(), &map_id)) {
        return outpost = static_cast<GW::Constants::MapID>(map_id), true;
    }

    // By full outpost name (without punctuation) e.g. "/tp GrEaT TemplE oF BalthaZAR"
    std::string compare = GuiUtils::ToLower(GuiUtils::RemovePunctuation(GuiUtils::WStringToString(s)));

    // Shortcut words e.g "/tp doa" for domain of anguish
    const std::string first_word = compare.substr(0, compare.find(' '));
    const auto& shorthand_outpost = shorthand_outpost_names.find(first_word);
    if (shorthand_outpost != shorthand_outpost_names.end()) {
        const OutpostAlias& outpost_info = shorthand_outpost->second;
        outpost = outpost_info.map_id;
        if (outpost_info.district != GW::Constants::District::Current) {
            district = outpost_info.district;
        }
        return true;
    }

    // Remove "the " from front of entered string
    const size_t found = compare.rfind("the ");
    if (found == 0) {
        compare.replace(found, 4, "");
    }

    // Helper function
    auto FindMatchingMap = [](const char* compare, const char* const* map_names, const GW::Constants::MapID* map_ids, const size_t map_count) -> GW::Constants::MapID {
        const char* bestMatchMapName = nullptr;
        auto bestMatchMapID = GW::Constants::MapID::None;

        const auto searchStringLength = compare ? strlen(compare) : 0;
        if (!searchStringLength) {
            return bestMatchMapID;
        }
        for (size_t i = 0; i < map_count; i++) {
            const auto thisMapLength = strlen(map_names[i]);
            if (searchStringLength > thisMapLength) {
                continue; // String entered by user is longer than this outpost name.
            }
            if (strncmp(map_names[i], compare, searchStringLength) != 0) {
                continue; // No match
            }
            if (thisMapLength == searchStringLength) {
                return map_ids[i]; // Exact match, break.
            }
            if (!bestMatchMapName || strcmp(map_names[i], bestMatchMapName) < 0) {
                bestMatchMapID = map_ids[i];
                bestMatchMapName = map_names[i];
            }
        }
        return bestMatchMapID;
        };
    auto best_match_map_id = GW::Constants::MapID::None;
    if (ImInPresearing()) {
        best_match_map_id = FindMatchingMap(compare.c_str(), presearing_map_names.data(), presearing_map_ids.data(), presearing_map_ids.size());
    }
    else {
        best_match_map_id = FindMatchingMap(compare.c_str(), searchable_map_names.data(), searchable_map_ids.data(), searchable_map_ids.size());
        if (best_match_map_id == GW::Constants::MapID::None) {
            best_match_map_id = FindMatchingMap(compare.c_str(), searchable_dungeon_names.data(), dungeon_map_ids.data(), dungeon_map_ids.size());
        }
        if (best_match_map_id == GW::Constants::MapID::None && fetched_searchable_explorable_areas == FetchedMapNames::Ready) {
            // find explorable area matching this, and then find nearest unlocked outpost.
            best_match_map_id = FindMatchingMap(compare.c_str(), searchable_explorable_areas.data(), searchable_explorable_area_ids.data(), searchable_explorable_area_ids.size());
            if (best_match_map_id != GW::Constants::MapID::None) {
                best_match_map_id = GetNearestOutpost(best_match_map_id);
            }
        }
    }

    if (best_match_map_id != GW::Constants::MapID::None) {
        return outpost = best_match_map_id, true; // Exact match
    }
    return false;
}

bool TravelWindow::ParseDistrict(const std::wstring& s, GW::Constants::District& district, uint32_t& number)
{
    std::string compare = GuiUtils::ToLower(GuiUtils::RemovePunctuation(GuiUtils::WStringToString(s)));
    const std::string first_word = compare.substr(0, compare.find(' '));

    const std::regex district_regex("([a-z]{2,3})(\\d)?");
    std::smatch m;
    if (!std::regex_search(first_word, m, district_regex)) {
        return false;
    }
    // Shortcut words e.g "/tp ae" for american english
    const auto& shorthand_outpost = shorthand_district_names.find(m[1].str());
    if (shorthand_outpost == shorthand_district_names.end()) {
        return false;
    }
    district = shorthand_outpost->second.district;
    if (m.size() > 2 && !GuiUtils::ParseUInt(m[2].str().c_str(), &number)) {
        number = 0;
    }

    return true;
}
