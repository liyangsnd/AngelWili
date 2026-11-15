#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>

struct mg_connection;

namespace live {
struct TextDanmaku {
    std::string sender;
    std::string text;
};
std::string encodeDouyuMessage(const std::string& message);
std::vector<TextDanmaku> decodeDouyuMessages(std::string& buffer, const std::string& bytes);

class DouyuDanmakuClient {
public:
    enum class State { CONNECTING, CONNECTED, RETRYING, FAILED, STOPPED };
    ~DouyuDanmakuClient();
    void start(const std::string& room, bool huya = false, const std::string& registration = "");
    void stop();
    std::vector<TextDanmaku> drain();
    std::vector<TextDanmaku> waitForMessages(int timeoutMilliseconds);
    State state() const { return currentState.load(); }
    size_t packetCount() const { return packets.load(); }

private:
    static void eventHandler(mg_connection* connection, int event, void* data);
    void run();
    std::string room;
    bool huya = false;
    std::string registration;
    std::string buffer;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<State> currentState{State::STOPPED};
    std::atomic<size_t> packets{0};
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<TextDanmaku> queue;
    mg_connection* connection = nullptr;
};
}