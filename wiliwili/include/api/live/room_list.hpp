#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace live {
enum class Platform { HUYA, DOUYU };

struct RoomSummary {
    std::string id;
    std::string title;
    std::string owner;
    std::string cover;
    uint64_t viewers = 0;
};

struct RoomPage {
    std::vector<RoomSummary> rooms;
    bool hasMore = false;
};

size_t appendUniqueRooms(std::vector<RoomSummary>& rooms, const std::vector<RoomSummary>& more);

struct LiveCategory {
    std::string id;
    std::string name;
};

std::vector<LiveCategory> parseCategories(Platform platform, const std::string& response);
void requestCategories(Platform platform, const std::function<void(std::vector<LiveCategory>)>& success,
                       const std::function<void(std::string)>& error);

RoomPage parseRoomPage(Platform platform, const std::string& response, int page);
void requestRoomPage(Platform platform, int page, const std::function<void(RoomPage)>& success,
                     const std::function<void(std::string)>& error, const std::string& category = "");
}