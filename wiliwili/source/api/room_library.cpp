#include "api/live/room_library.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {
bool valid(const live::RoomSummary& room) {
    return !room.id.empty() && room.id.size() <= 16 && room.id.find_first_not_of("0123456789") == std::string::npos;
}
nlohmann::json encode(const std::vector<live::SavedRoom>& rooms) {
    auto result = nlohmann::json::array();
    for (const auto& item : rooms) result.push_back({{"platform", item.platform == live::Platform::HUYA ? "huya" : "douyu"},
        {"id", item.room.id}, {"title", item.room.title}, {"owner", item.room.owner}, {"cover", item.room.cover}});
    return result;
}
std::vector<live::SavedRoom> decode(const nlohmann::json& data, size_t limit) {
    std::vector<live::SavedRoom> result;
    if (!data.is_array()) throw std::runtime_error("Invalid saved rooms");
    for (const auto& item : data) {
        const auto platform = item.at("platform").get<std::string>();
        live::RoomSummary room;
        room.id = item.at("id").get<std::string>();
        room.title = item.value("title", "");
        room.owner = item.value("owner", "");
        room.cover = item.value("cover", "");
        if (!valid(room) || (platform != "huya" && platform != "douyu")) continue;
        result.push_back({platform == "huya" ? live::Platform::HUYA : live::Platform::DOUYU, room});
        if (result.size() >= limit) break;
    }
    return result;
}
}

namespace live {
RoomLibrary::RoomLibrary(std::string file) : path(std::move(file)) {
    try {
        if (!std::filesystem::exists(path)) return;
        if (std::filesystem::file_size(path) > 1024 * 1024) { healthy = false; return; }
        std::ifstream input(path);
        nlohmann::json data;
        input >> data;
        if (data.value("version", 0) != 1) { healthy = false; return; }
        favoriteRooms = decode(data.at("favorites"), 200);
        recentRooms = decode(data.at("recent"), 50);
    } catch (const std::exception&) { healthy = false; }
}

bool RoomLibrary::save() {
    if (!healthy) return false;
    try {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        {
            std::ofstream output(path + ".tmp", std::ios::trunc);
            output << nlohmann::json{{"version", 1}, {"favorites", encode(favoriteRooms)}, {"recent", encode(recentRooms)}}.dump(2);
            output.flush();
            if (!output.good()) return false;
        }
        std::filesystem::rename(path + ".tmp", path);
        return true;
    } catch (const std::exception&) { return false; }
}

bool RoomLibrary::toggleFavorite(Platform platform, const RoomSummary& room) {
    if (!healthy || !valid(room)) return false;
    auto saved = favoriteRooms;
    auto found = std::find_if(favoriteRooms.begin(), favoriteRooms.end(), [&](const SavedRoom& item) {
        return item.platform == platform && item.room.id == room.id;
    });
    if (found != favoriteRooms.end()) favoriteRooms.erase(found);
    else {
        if (favoriteRooms.size() >= 200) return false;
        favoriteRooms.push_back({platform, room});
    }
    if (save()) return true;
    favoriteRooms = std::move(saved);
    return false;
}

bool RoomLibrary::record(Platform platform, const RoomSummary& room) {
    if (!healthy || !valid(room)) return false;
    auto saved = recentRooms;
    recentRooms.erase(std::remove_if(recentRooms.begin(), recentRooms.end(), [&](const SavedRoom& item) {
        return item.platform == platform && item.room.id == room.id;
    }), recentRooms.end());
    recentRooms.insert(recentRooms.begin(), {platform, room});
    if (recentRooms.size() > 50) recentRooms.resize(50);
    if (save()) return true;
    recentRooms = std::move(saved);
    return false;
}
}