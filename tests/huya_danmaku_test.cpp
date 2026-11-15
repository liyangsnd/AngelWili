#include "api/live/huya_danmaku.hpp"
#include <iostream>
#include <stdexcept>
#include <cpr/cpr.h>

int main(int argc, char** argv) {
    try {
        auto registration = live::huyaRegistration(R"({"lYyid":123,"lChannelId":456,"lSubChannelId":789})");
        if (registration.empty() || !live::huyaRegistration("empty").empty()) throw std::runtime_error("Registration mismatch");
        if (!live::decodeHuyaMessages(registration).empty()) throw std::runtime_error("Registration displayed as chat");
        bool rejected = false;
        try { live::decodeHuyaMessages(std::string("\x17\xff\xff\xff\xff", 5)); } catch (const std::exception&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Invalid length accepted");
        std::cout << "PASS Huya registration, non-chat filtering and bounds\n";
        if (argc > 1) {
            auto page = cpr::Get(cpr::Url{"https://m.huya.com/" + std::string(argv[1])}, cpr::Timeout{10000},
                                 cpr::Header{{"User-Agent", "Mozilla/5.0 (Linux; Android 6.0) Mobile Safari/537.36"}});
            if (page.status_code != 200) throw std::runtime_error("Huya room request failed");
            live::DouyuDanmakuClient client;
            auto packet = live::huyaRegistration(page.text);
            if (packet.empty()) throw std::runtime_error("Huya registration metadata missing");
            client.start(argv[1], true, packet);
            auto messages = client.waitForMessages(12000);
            std::cout << "Huya state=" << static_cast<int>(client.state()) << " packets=" << client.packetCount() << '\n';
            client.stop();
            if (messages.empty()) throw std::runtime_error("Huya text reception failed");
            std::cout << "PASS Huya verified TLS, registration and text reception\n";
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}