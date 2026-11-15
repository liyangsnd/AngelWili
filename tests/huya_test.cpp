#include "api/live/huya.hpp"
#include <cpr/cpr.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        bool rejected = false;
        try {
            live::parseHuyaRoom("<html>No stream</html>");
        } catch (const std::exception&) {
            rejected = true;
        }
        if (!rejected) throw std::runtime_error("Missing stream was accepted");
        bool invalidRoom = false;
        live::requestHuyaRoom("../invalid", [](live::HuyaRoom) {
            throw std::runtime_error("Invalid room was accepted");
        }, [&invalidRoom](std::string) { invalidRoom = true; });
        if (!invalidRoom) throw std::runtime_error("Invalid room was not rejected");
        std::cout << "PASS invalid room and missing stream\n";
        std::vector<live::StreamQuality> qualities = {{0, "Source", 0}, {4, "High", 4000}, {3, "Medium", 2000}, {2, "Low", 900}};
        if (live::lowerQuality(qualities, 0) != 4 || live::lowerQuality(qualities, 4) != 3 ||
            live::lowerQuality(qualities, 2) != -1 || live::lowerQuality(qualities, -1) != -1)
            throw std::runtime_error("Quality fallback mismatch");
        std::cout << "PASS quality fallback\n";
        if (live::preferredQuality({{1, "1080p", 4000}, {2, "4K", 20000}}, 3) != 1 ||
            live::preferredQuality({{1, "High", 4000}}, 3) != 3)
            throw std::runtime_error("1080 quality preference mismatch");
        std::cout << "PASS explicit 1080 preference without bitrate inference\n";
        live::RecoveryBudget budget;
        if (budget.nextDelay() != 2000 || budget.nextDelay() != 4000 || budget.nextDelay() != 8000 || budget.nextDelay() != 0)
            throw std::runtime_error("Recovery retry limit mismatch");
        budget.reset();
        if (budget.nextDelay() != 2000) throw std::runtime_error("Recovery reset mismatch");
        std::cout << "PASS bounded recovery delays\n";
        if (argc > 1) {
            auto response = cpr::Get(cpr::Url{"https://www.huya.com/" + std::string(argv[1])},
                                     cpr::Timeout{10000}, cpr::Header{{"User-Agent", "Mozilla/5.0"}});
            if (response.status_code != 200) throw std::runtime_error("Room request failed");
            auto room = live::parseHuyaRoom(response.text);
            if (room.url.find("wsSecret=") == std::string::npos || room.title.empty())
                throw std::runtime_error("Invalid resolved room");
            std::cout << "PASS public room parser\n";
            if (room.qualities.empty()) throw std::runtime_error("No Huya quality list");
            auto selected = live::parseHuyaRoom(response.text, room.qualities.back().id);
            if (selected.selectedQuality != room.qualities.back().id) throw std::runtime_error("Huya quality selection mismatch");
            std::cout << "PASS actual Huya quality selection\n";
            if (argc > 2 && std::string(argv[2]) == "--stream") {
                size_t bytes = 0;
                auto start = std::chrono::steady_clock::now();
                auto media = cpr::Get(cpr::Url{room.url}, cpr::Timeout{12000},
                    cpr::Header{{"User-Agent", "curl/8.7.1"}, {"Referer", "https://www.huya.com/"},
                                {"Origin", "https://www.huya.com"}},
                    cpr::WriteCallback{[&bytes](std::string chunk, intptr_t) {
                        bytes += chunk.size();
                        return true;
                    }});
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (media.status_code != 200 || bytes < 200000 || elapsed < std::chrono::seconds(10))
                    throw std::runtime_error("Stream did not sustain reception");
                std::cout << "PASS >=10 seconds stream reception\n";
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}