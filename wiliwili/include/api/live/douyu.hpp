#pragma once

#include <functional>
#include <string>
#include "api/live/quality.hpp"

namespace live {

struct DouyuRoom {
    std::string title;
    std::string url;
    std::vector<StreamQuality> qualities;
    int selectedQuality = -1;
    std::string danmakuRegistration;
};

std::string signDouyu(const std::string& script, const std::string& room,
                     const std::string& device, const std::string& timestamp);
void requestDouyuRoom(const std::string& room,
                      const std::function<void(DouyuRoom)>& success,
                      const std::function<void(std::string)>& error, int quality = -1);

}