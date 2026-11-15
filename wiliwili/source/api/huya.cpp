#include "api/live/huya.hpp"
#include "api/live/huya_danmaku.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "bilibili/util/md5.hpp"
#include "utils/string_helper.hpp"

namespace live {

HuyaRoom parseHuyaRoom(const std::string& page, int requestedQuality) {
    if (requestedQuality < -1) throw std::runtime_error("Invalid Huya quality");
    std::smatch match;
    const std::regex marker(R"("?stream"?\s*:\s*)");
    if (!std::regex_search(page, match, marker)) throw std::runtime_error("Huya stream data not found");
    std::istringstream input(page.substr(match.position() + match.length()));
    nlohmann::json data;
    input >> data;
    if (data.is_string()) {
        std::string decoded;
        if (wiliwili::base64Decode(data.get<std::string>(), decoded) != 0)
            throw std::runtime_error("Invalid Huya stream data");
        data = nlohmann::json::parse(decoded);
    }
    const auto& room = data.at("data").at(0);
    auto streams = room.at("gameStreamInfoList");
    if (streams.empty()) throw std::runtime_error("Huya room is offline");
    std::stable_sort(streams.begin(), streams.end(), [](const auto& left, const auto& right) {
        return left.value("sCdnType", "") == "HS" && right.value("sCdnType", "") != "HS";
    });
    const auto& stream = streams.at(0);
    std::string anticode = stream.at("sFlvAntiCode").get<std::string>();
    for (size_t position = 0; (position = anticode.find("&amp;", position)) != std::string::npos;)
        anticode.replace(position, 5, "&");
    std::map<std::string, std::string> params;
    std::istringstream query(anticode);
    std::string field;
    while (std::getline(query, field, '&')) {
        auto separator = field.find('=');
        if (separator != std::string::npos)
            params[field.substr(0, separator)] = cpr::util::urlDecode(field.substr(separator + 1));
    }
    std::string prefix;
    if (wiliwili::base64Decode(params.at("fm"), prefix) != 0)
        throw std::runtime_error("Invalid Huya stream parameters");
    prefix = prefix.substr(0, prefix.find('_'));
    std::random_device random;
    const uint32_t uid = 12340000 + random() % 10000;
    const uint32_t rotated = (uid << 8) | (uid >> 24);
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string sequence = std::to_string(timestamp + uid);
    const std::string name = stream.at("sStreamName").get<std::string>();
    const std::string ctype = params.at("ctype");
    const auto hash = websocketpp::md5::md5_hash_hex(sequence + "|" + ctype + "|100");
    int bitrate = 0;
    std::vector<StreamQuality> qualities;
    bool available = requestedQuality < 0;
    for (const auto& quality : data.at("vMultiStreamInfo")) {
        int candidate = quality.at("iBitRate").get<int>();
        if (candidate < 0) continue;
        qualities.push_back({candidate, quality.value("sDisplayName", std::to_string(candidate) + " kbps"), candidate});
        if (candidate == requestedQuality) available = true;
        if (candidate > bitrate && candidate <= 4000) bitrate = candidate;
    }
    if (!available) throw std::runtime_error("Requested Huya quality unavailable");
    if (requestedQuality < 0) bitrate = preferredQuality(qualities, bitrate);
    if (requestedQuality >= 0) bitrate = requestedQuality;
    std::map<std::string, std::string> playback = {
        {"wsSecret", websocketpp::md5::md5_hash_hex(prefix + "_" + std::to_string(rotated) + "_" + name + "_" + hash + "_" + params.at("wsTime"))},
        {"wsTime", params.at("wsTime")}, {"ctype", ctype}, {"fs", params.at("fs")},
        {"seqid", sequence}, {"u", std::to_string(rotated)}, {"sdk_sid", std::to_string(timestamp)},
        {"ratio", std::to_string(bitrate)}, {"t", "100"}, {"ver", "1"}, {"sv", "2401090219"}, {"codec", "264"},
    };
    std::string url = stream.at("sFlvUrl").get<std::string>();
    if (url.compare(0, 7, "http://") == 0) url.replace(0, 7, "https://");
    if (url.compare(0, 8, "https://") != 0) throw std::runtime_error("Unsupported Huya stream URL");
    url += "/" + name + "." + stream.at("sFlvUrlSuffix").get<std::string>() + "?";
    for (const auto& parameter : playback)
        url += cpr::util::urlEncode(parameter.first) + "=" + cpr::util::urlEncode(parameter.second) + "&";
    url.pop_back();
    auto title = room.at("gameLiveInfo").value("roomName", "");
    if (title.empty()) title = room.at("gameLiveInfo").value("introduction", "");
    if (title.empty()) title = "Huya live";
    return {title, url, qualities, bitrate, huyaRegistration(page)};
}

void requestHuyaRoom(const std::string& room, const std::function<void(HuyaRoom)>& success,
                     const std::function<void(std::string)>& error, int quality) {
    if (room.empty() || room.find_first_not_of("0123456789") != std::string::npos) {
        error("Invalid Huya room number");
        return;
    }
    cpr::GetCallback([room, success, error, quality](const cpr::Response& response) {
        if (response.error || response.status_code != 200) {
            error("Huya room request failed (HTTP " + std::to_string(response.status_code) + ")");
            return;
        }
        try {
            auto result = parseHuyaRoom(response.text, quality);
            if (result.danmakuRegistration.empty()) {
                auto mobile = cpr::Get(cpr::Url{"https://m.huya.com/" + room}, cpr::Timeout{10000},
                    cpr::Header{{"User-Agent", "Mozilla/5.0 (Linux; Android 6.0) Mobile Safari/537.36"}});
                if (!mobile.error && mobile.status_code == 200) result.danmakuRegistration = huyaRegistration(mobile.text);
            }
            success(std::move(result));
        } catch (const std::exception&) {
            error("Huya room is offline or its stream data has changed");
        }
    }, cpr::Url{"https://www.huya.com/" + room}, cpr::Timeout{10000},
       cpr::Header{{"User-Agent", "Mozilla/5.0"}, {"Referer", "https://www.huya.com/"}, {"Origin", "https://www.huya.com"}});
}

}