#include "api/live/room_library.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main() {
    const auto directory = std::filesystem::temp_directory_path() / "angelwili-library-test";
    const auto path = (directory / "rooms.json").string();
    try {
        std::filesystem::remove_all(directory);
        live::RoomLibrary library(path);
        live::RoomSummary room{"123", "Room", "Owner", "", 0};
        if (!library.toggleFavorite(live::Platform::HUYA, room) || !library.toggleFavorite(live::Platform::DOUYU, room))
            throw std::runtime_error("Favorites write failed");
        if (!library.record(live::Platform::HUYA, room) || !library.record(live::Platform::HUYA, room))
            throw std::runtime_error("History write failed");
        live::RoomLibrary loaded(path);
        if (loaded.favorites().size() != 2 || loaded.recent().size() != 1) throw std::runtime_error("Persistence mismatch");
        if (!loaded.toggleFavorite(live::Platform::HUYA, room) || loaded.favorites().size() != 1)
            throw std::runtime_error("Favorite removal failed");
        for (int index = 0; index < 55; ++index) { room.id = std::to_string(index); if (!loaded.record(live::Platform::DOUYU, room)) throw std::runtime_error("History update failed"); }
        if (loaded.recent().size() != 50 || loaded.recent().front().room.id != "54") throw std::runtime_error("History limit mismatch");
        { std::ofstream output(path); output << "corrupt"; }
        live::RoomLibrary corrupt(path);
        if (corrupt.record(live::Platform::HUYA, room)) throw std::runtime_error("Corrupt file overwritten");
        std::filesystem::remove_all(directory);
        std::cout << "PASS favorites, platform isolation, history deduplication, persistence and corruption protection\n";
        return 0;
    } catch (const std::exception& error) { std::filesystem::remove_all(directory); std::cerr << error.what() << '\n'; return 1; }
}