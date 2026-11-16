#pragma once

#include <functional>
#include <string>
#include "api/live/quality.hpp"

namespace live {

struct HuyaRoom {
    std::string title;
    std::string url;
    std::vector<StreamQuality> qualities;
    int selectedQuality = -1;
    std::string danmakuRegistration;
    std::vector<StreamLine> lines;
    std::string selectedLine;
};

HuyaRoom parseHuyaRoom(const std::string& page, int quality = -1, const std::string& line = "");
void requestHuyaRoom(const std::string& room,
                     const std::function<void(HuyaRoom)>& success,
                     const std::function<void(std::string)>& error, int quality = -1,
                     const std::string& line = "");

}