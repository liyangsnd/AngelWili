#pragma once
#include "api/live/room_list.hpp"

namespace live {
struct SavedRoom {
    Platform platform;
    RoomSummary room;
};

class RoomLibrary {
public:
    explicit RoomLibrary(std::string path);
    const std::vector<SavedRoom>& favorites() const { return favoriteRooms; }
    const std::vector<SavedRoom>& recent() const { return recentRooms; }
    bool toggleFavorite(Platform platform, const RoomSummary& room);
    bool record(Platform platform, const RoomSummary& room);
private:
    bool save();
    std::string path;
    bool healthy = true;
    std::vector<SavedRoom> favoriteRooms;
    std::vector<SavedRoom> recentRooms;
};
}