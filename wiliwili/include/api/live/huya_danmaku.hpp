#pragma once
#include "api/live/douyu_danmaku.hpp"

namespace live {
std::string huyaRegistration(const std::string& page);
std::vector<TextDanmaku> decodeHuyaMessages(const std::string& bytes);
}