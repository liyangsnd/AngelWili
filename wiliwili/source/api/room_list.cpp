#include "api/live/room_list.hpp"
#include <stdexcept>
#include <unordered_set>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

namespace {
std::string scalarText(const nlohmann::json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return value.dump();
    return "";
}
}

namespace live {
size_t appendUniqueRooms(std::vector<RoomSummary>& rooms, const std::vector<RoomSummary>& more) {
    std::unordered_set<std::string> ids;
    for (const auto& room : rooms) ids.insert(room.id);
    size_t appended = 0;
    for (const auto& room : more) {
        if (!room.id.empty() && ids.insert(room.id).second) {
            rooms.push_back(room);
            ++appended;
        }
    }
    return appended;
}

std::vector<LiveCategory> parseCategories(Platform platform, const std::string& response) {
    auto document = nlohmann::json::parse(response);
    const bool huya = platform == Platform::HUYA;
    if (document.at(huya ? "status" : "error").get<int>() != (huya ? 200 : 0))
        throw std::runtime_error("Live categories unavailable");
    const auto& items = document.at(huya ? "gameList" : "data");
    if (!items.is_array()) throw std::runtime_error("Invalid live categories");
    std::vector<LiveCategory> result;
    for (const auto& item : items) {
        auto id = scalarText(item.at(huya ? "gid" : "cate_id"));
        auto name = item.value(huya ? "gameFullName" : "game_name", "");
        if (id.empty() || id.size() > 16 || id.find_first_not_of("0123456789") != std::string::npos || name.empty()) continue;
        if (huya && item.value("isHide", 0) != 0) continue;
        result.push_back({id, name});
    }
    return result;
}

void requestCategories(Platform platform, const std::function<void(std::vector<LiveCategory>)>& success,
                       const std::function<void(std::string)>& error) {
    const bool huya = platform == Platform::HUYA;
    cpr::GetCallback([platform, success, error](const cpr::Response& response) {
        if (response.error || response.status_code != 200) { error("Category request failed"); return; }
        try { success(parseCategories(platform, response.text)); }
        catch (const std::exception&) { error("Category data unavailable; retry later"); }
    }, cpr::Url{huya ? "https://www.huya.com/cache.php?m=Game&do=getGameList" : "https://open.douyucdn.cn/api/RoomApi/game"},
       cpr::Timeout{10000}, cpr::Header{{"User-Agent", "Mozilla/5.0"}});
}

RoomPage parseRoomPage(Platform platform, const std::string& response, int page) {
    if (page < 1 || page > 1000) throw std::runtime_error("Invalid live list page");
    auto document = nlohmann::json::parse(response);
    const bool huya = platform == Platform::HUYA;
    if ((huya ? document.at("status").get<int>() != 200 : document.at("error").get<int>() != 0))
        throw std::runtime_error("Live list unavailable");
    const auto& data = document.at("data");
    const auto& items = huya ? data.at("datas") : data;
    if (!items.is_array()) throw std::runtime_error("Invalid live list data");
    RoomPage result;
    result.hasMore = huya ? page < data.at("totalPage").get<int>() : items.size() >= 20;
    for (const auto& item : items) {
        RoomSummary room;
        room.id = scalarText(item.at(huya ? "profileRoom" : "room_id"));
        if (room.id.empty() || room.id.size() > 16 || room.id.find_first_not_of("0123456789") != std::string::npos)
            continue;
        room.title = item.value(huya ? "roomName" : "room_name", "");
        if (huya && room.title.empty()) room.title = item.value("introduction", "");
        room.owner = item.value(huya ? "nick" : "nickname", "");
        if (room.title.empty()) room.title = room.owner.empty() ? room.id : room.owner;
        room.cover = item.value(huya ? "screenshot" : "room_src", "");
        if (room.cover.compare(0, 7, "http://") == 0) room.cover.replace(0, 7, "https://");
        if (room.cover.compare(0, 8, "https://") != 0) room.cover.clear();
        const auto count = item.find(huya ? "totalCount" : "online");
        if (count != item.end()) {
            auto text = scalarText(*count);
            if (!text.empty() && text.size() <= 19 && text.find_first_not_of("0123456789") == std::string::npos)
                room.viewers = std::stoull(text);
        }
        result.rooms.push_back(std::move(room));
    }
    return result;
}

void requestRoomPage(Platform platform, int page, const std::function<void(RoomPage)>& success,
                     const std::function<void(std::string)>& error, const std::string& category) {
    if (page < 1 || page > 1000) { error("Invalid live list page"); return; }
    if (!category.empty() && (category.size() > 16 || category.find_first_not_of("0123456789") != std::string::npos)) {
        error("Invalid live category"); return;
    }
    const bool huya = platform == Platform::HUYA;
    const std::string url = huya
        ? "https://www.huya.com/cache.php?m=LiveList&do=getLiveListByPage&gameId=" + (category.empty() ? "0" : category) + "&tagAll=0&page=" + std::to_string(page)
        : "https://open.douyucdn.cn/api/RoomApi/live" + (category.empty() ? "" : "/" + category) + "?offset=" + std::to_string((page - 1) * 20) + "&limit=20";
    cpr::GetCallback([platform, page, success, error](const cpr::Response& response) {
        if (response.error || response.status_code != 200) {
            error("Live list request failed (HTTP " + std::to_string(response.status_code) + ")");
            return;
        }
        try { success(parseRoomPage(platform, response.text, page)); }
        catch (const std::exception&) { error("Live list data unavailable; refresh to retry"); }
    }, cpr::Url{url}, cpr::Timeout{10000},
       cpr::Header{{"User-Agent", "Mozilla/5.0"}, {"Referer", huya ? "https://www.huya.com/" : "https://www.douyu.com/"}});
}
}