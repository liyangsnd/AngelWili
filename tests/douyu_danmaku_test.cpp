#include "api/live/douyu_danmaku.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        std::string buffer;
        const auto frame = live::encodeDouyuMessage("type@=chatmsg/nn@=User/txt@=hello@Sworld@A/");
        if (!live::decodeDouyuMessages(buffer, frame.substr(0, 7)).empty()) throw std::runtime_error("Partial frame accepted");
        auto messages = live::decodeDouyuMessages(buffer, frame.substr(7) + frame);
        if (messages.size() != 2 || messages[0].text != "hello/world@" || !buffer.empty())
            throw std::runtime_error("Message decoding mismatch");
        if (!live::decodeDouyuMessages(buffer, live::encodeDouyuMessage("type@=dgb/txt@=gift/")).empty())
            throw std::runtime_error("Gift was displayed");
        auto invalid = frame;
        invalid[4] = 0;
        bool rejected = false;
        try { live::decodeDouyuMessages(buffer, invalid); } catch (const std::exception&) { rejected = true; }
        if (!rejected || !buffer.empty()) throw std::runtime_error("Invalid length accepted");
        rejected = false;
        try { live::decodeDouyuMessages(buffer, std::string(1024 * 1024 + 1, 'x')); } catch (const std::exception&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Buffer limit not enforced");
        std::cout << "PASS fragmented frames, batching, escaping, gift filtering and buffer limit\n";
        if (argc > 1) {
            live::DouyuDanmakuClient client;
            client.start(argv[1]);
            auto received = client.waitForMessages(12000);
            client.stop();
            if (received.empty() || client.state() != live::DouyuDanmakuClient::State::STOPPED)
                throw std::runtime_error("Native danmaku reception failed");
            std::cout << "PASS verified TLS, guest join, text reception and clean stop\n";
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}