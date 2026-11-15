#include "api/live/room_list.hpp"
#include <future>
#include <iostream>
#include <stdexcept>

int main(int argc, char**) {
    try {
        auto page = live::parseRoomPage(live::Platform::HUYA,
            R"({"status":200,"data":{"totalPage":2,"datas":[{"profileRoom":"123","introduction":"Room","nick":"Owner","screenshot":"http://example.test/image.jpg","totalCount":"42"}]}})", 1);
        if (page.rooms.size() != 1 || !page.hasMore || page.rooms[0].title != "Room" || page.rooms[0].viewers != 42)
            throw std::runtime_error("Huya page mismatch");
        page = live::parseRoomPage(live::Platform::DOUYU,
            R"({"error":0,"data":[{"room_id":456,"room_name":"Room","nickname":"Owner","online":21},{"room_id":"bad"}]})", 1);
        if (page.rooms.size() != 1 || page.hasMore || page.rooms[0].id != "456")
            throw std::runtime_error("Douyu page mismatch");
        page = live::parseRoomPage(live::Platform::DOUYU, R"({"error":0,"data":[]})", 2);
        if (!page.rooms.empty() || page.hasMore) throw std::runtime_error("Empty page mismatch");
        std::cout << "PASS room mapping, pagination, empty page and invalid ID\n";
        std::vector<live::RoomSummary> accumulated = {{"1", "First", "", "", 0}};
        if (live::appendUniqueRooms(accumulated, {{"1", "Duplicate", "", "", 0}, {"2", "Second", "", "", 0}}) != 1 ||
            accumulated.size() != 2 || accumulated[0].title != "First" || accumulated[1].id != "2")
            throw std::runtime_error("Continuous list append mismatch");
        if (live::appendUniqueRooms(accumulated, {{"2", "Duplicate", "", "", 0}}) != 0)
            throw std::runtime_error("Duplicate-only page mismatch");
        if (live::appendUniqueRooms(accumulated, {}) != 0) throw std::runtime_error("Empty append mismatch");
        std::cout << "PASS continuous list append, order and deduplication\n";
        auto categories = live::parseCategories(live::Platform::HUYA,
            R"({"status":200,"gameList":[{"gid":1,"gameFullName":"LOL","isHide":0},{"gid":2,"gameFullName":"Hidden","isHide":1}]})");
        if (categories.size() != 1 || categories[0].id != "1") throw std::runtime_error("Huya categories mismatch");
        categories = live::parseCategories(live::Platform::DOUYU,
            R"({"error":0,"data":[{"cate_id":"1","game_name":"LOL"},{"cate_id":"bad","game_name":"Invalid"}]})");
        if (categories.size() != 1 || categories[0].name != "LOL") throw std::runtime_error("Douyu categories mismatch");
        std::cout << "PASS category mapping and filtering\n";
        if (argc > 1) {
            for (auto platform : {live::Platform::HUYA, live::Platform::DOUYU}) {
                std::promise<std::vector<live::LiveCategory>> categoryPromise;
                auto categoryFuture = categoryPromise.get_future();
                live::requestCategories(platform, [&categoryPromise](std::vector<live::LiveCategory> result) { categoryPromise.set_value(std::move(result)); },
                    [&categoryPromise](std::string error) { categoryPromise.set_exception(std::make_exception_ptr(std::runtime_error(error))); });
                if (categoryFuture.get().empty()) throw std::runtime_error("Public categories empty");
                            for (int number : {1, 2}) {
                std::promise<live::RoomPage> promise;
                auto future = promise.get_future();
                                live::requestRoomPage(platform, number, [&promise](live::RoomPage result) { promise.set_value(std::move(result)); },
                    [&promise](std::string error) { promise.set_exception(std::make_exception_ptr(std::runtime_error(error))); }, "1");
                auto result = future.get();
                if (result.rooms.empty()) throw std::runtime_error("Public room list empty");
                                std::cout << "PASS public room list page " << number << ": " << result.rooms.size() << " rooms\n";
                            }
            }
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}