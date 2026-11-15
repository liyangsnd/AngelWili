#include "api/live/douyu_danmaku.hpp"
#include "api/live/huya_danmaku.hpp"
#include <map>
#include <stdexcept>
#include <chrono>
#include <mongoose.h>
#include "danmaku_ca.hpp"

namespace {
uint32_t readLength(const std::string& data, size_t offset) {
    uint32_t result = 0;
    for (size_t index = 0; index < 4; ++index)
        result |= static_cast<uint32_t>(static_cast<unsigned char>(data[offset + index])) << (index * 8);
    return result;
}
std::string unescape(const std::string& data) {
    std::string result;
    for (size_t index = 0; index < data.size(); ++index) {
        if (data[index] == '@' && index + 1 < data.size() && (data[index + 1] == 'A' || data[index + 1] == 'S'))
            result += data[++index] == 'A' ? '@' : '/';
        else result += data[index];
    }
    return result;
}
}

namespace live {
std::string encodeDouyuMessage(const std::string& message) {
    if (message.size() > 4096) throw std::runtime_error("Douyu command too large");
    const uint32_t length = static_cast<uint32_t>(message.size() + 9);
    std::string result(12, '\0');
    for (size_t index = 0; index < 4; ++index) {
        result[index] = static_cast<char>((length >> (index * 8)) & 255);
        result[index + 4] = result[index];
    }
    result[8] = static_cast<char>(689 & 255);
    result[9] = static_cast<char>(689 >> 8);
    result += message;
    result.push_back('\0');
    return result;
}

std::vector<TextDanmaku> decodeDouyuMessages(std::string& buffer, const std::string& bytes) {
    constexpr size_t limit = 1024 * 1024;
    if (bytes.size() > limit || buffer.size() > limit - bytes.size()) {
        buffer.clear();
        throw std::runtime_error("Douyu receive buffer limit");
    }
    buffer += bytes;
    std::vector<TextDanmaku> messages;
    size_t offset = 0;
    while (buffer.size() - offset >= 12) {
        const uint32_t length = readLength(buffer, offset);
        if (length < 9 || length > limit - 4 || length != readLength(buffer, offset + 4)) {
            buffer.clear();
            throw std::runtime_error("Invalid Douyu frame length");
        }
        if (buffer.size() - offset < length + 4) break;
        if (buffer[offset + length + 3] != '\0') {
            buffer.clear();
            throw std::runtime_error("Invalid Douyu frame terminator");
        }
        std::map<std::string, std::string> fields;
        const std::string payload = buffer.substr(offset + 12, length - 9);
        size_t start = 0;
        while (start < payload.size()) {
            auto end = payload.find('/', start);
            if (end == std::string::npos) end = payload.size();
            const auto item = payload.substr(start, end - start);
            const auto separator = item.find("@=");
            if (separator != std::string::npos)
                fields[unescape(item.substr(0, separator))] = unescape(item.substr(separator + 2));
            start = end + 1;
        }
        if (fields["type"] == "chatmsg" && !fields["txt"].empty() && fields["txt"].size() <= 4096 && messages.size() < 100)
            messages.push_back({fields["nn"].size() <= 256 ? fields["nn"] : "", fields["txt"]});
        offset += length + 4;
    }
    buffer.erase(0, offset);
    return messages;
}

DouyuDanmakuClient::~DouyuDanmakuClient() { stop(); }

void DouyuDanmakuClient::start(const std::string& requestedRoom, bool isHuya, const std::string& registerPacket) {
    stop();
    if (requestedRoom.empty() || requestedRoom.size() > 16 || requestedRoom.find_first_not_of("0123456789") != std::string::npos) {
        currentState = State::FAILED;
        return;
    }
    room = requestedRoom;
    huya = isHuya;
    registration = registerPacket;
    packets = 0;
    if (huya && registration.empty()) { currentState = State::FAILED; return; }
    running = true;
    currentState = State::CONNECTING;
    worker = std::thread([this]() { run(); });
}

void DouyuDanmakuClient::stop() {
    running = false;
    ready.notify_all();
    if (worker.joinable()) worker.join();
    std::lock_guard<std::mutex> lock(mutex);
    queue.clear();
    buffer.clear();
    currentState = State::STOPPED;
}

std::vector<TextDanmaku> DouyuDanmakuClient::drain() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<TextDanmaku> result;
    while (!queue.empty() && result.size() < 20) {
        result.push_back(std::move(queue.front()));
        queue.pop_front();
    }
    return result;
}

std::vector<TextDanmaku> DouyuDanmakuClient::waitForMessages(int timeoutMilliseconds) {
    std::unique_lock<std::mutex> lock(mutex);
    ready.wait_for(lock, std::chrono::milliseconds(timeoutMilliseconds), [this]() { return !queue.empty() || !running; });
    lock.unlock();
    return drain();
}

void DouyuDanmakuClient::eventHandler(mg_connection* socket, int event, void* data) {
    auto* client = static_cast<DouyuDanmakuClient*>(socket->fn_data);
    if (event == MG_EV_CONNECT) {
        mg_tls_opts options{};
        options.ca = mg_str_n(DANMAKU_CA, sizeof(DANMAKU_CA));
        options.name = mg_str(client->huya ? "cdnws.api.huya.com" : "danmuproxy.douyu.com");
        mg_tls_init(socket, &options);
    } else if (event == MG_EV_WS_OPEN) {
        client->currentState = State::CONNECTED;
        if (client->huya) {
            mg_ws_send(socket, client->registration.data(), client->registration.size(), WEBSOCKET_OP_BINARY);
            return;
        }
        for (const auto& message : {"type@=loginreq/roomid@=" + client->room + "/",
                                   "type@=joingroup/rid@=" + client->room + "/gid@=-9999/"}) {
            auto packet = encodeDouyuMessage(message);
            mg_ws_send(socket, packet.data(), packet.size(), WEBSOCKET_OP_BINARY);
        }
    } else if (event == MG_EV_WS_MSG) {
        ++client->packets;
        auto* message = static_cast<mg_ws_message*>(data);
        try {
            const std::string bytes(message->data.buf, message->data.len);
            auto messages = client->huya ? decodeHuyaMessages(bytes) : decodeDouyuMessages(client->buffer, bytes);
            std::lock_guard<std::mutex> lock(client->mutex);
            for (auto& item : messages) {
                if (client->queue.size() >= 200) client->queue.pop_front();
                client->queue.push_back(std::move(item));
            }
            client->ready.notify_all();
        } catch (const std::exception&) { socket->is_closing = 1; }
    } else if (event == MG_EV_READ && socket->recv.len > 1024 * 1024) {
        socket->is_closing = 1;
    } else if (event == MG_EV_ERROR) {
        socket->is_closing = 1;
    } else if (event == MG_EV_CLOSE) {
        client->connection = nullptr;
        client->currentState = State::RETRYING;
    }
}

void DouyuDanmakuClient::run() {
    mg_mgr manager{};
    mg_mgr_init(&manager);
    uint64_t nextConnect = 0;
    uint64_t heartbeat = 0;
    uint64_t connectStarted = 0;
    int attempts = 0;
    while (running) {
        const auto now = mg_millis();
        if (!connection && now >= nextConnect) {
            if (attempts >= 4) { currentState = State::FAILED; break; }
            buffer.clear();
            connection = mg_ws_connect(&manager, huya ? "wss://cdnws.api.huya.com/" : "wss://danmuproxy.douyu.com:8506/", eventHandler, this,
                                       "%s", huya ? "Origin: https://www.huya.com\r\n" : "");
            ++attempts;
            connectStarted = now;
            nextConnect = now + 2000 * attempts;
            heartbeat = now;
        }
        if (connection && currentState != State::CONNECTED && now - connectStarted >= 10000)
            connection->is_closing = 1;
        if (connection && currentState == State::CONNECTED && now - heartbeat >= (huya ? 30000 : 45000)) {
            if (huya) mg_ws_send(connection, "", 0, WEBSOCKET_OP_PING);
            else {
                auto packet = encodeDouyuMessage("type@=mrkl/");
                mg_ws_send(connection, packet.data(), packet.size(), WEBSOCKET_OP_BINARY);
            }
            heartbeat = now;
        }
        mg_mgr_poll(&manager, 100);
    }
    mg_mgr_free(&manager);
    connection = nullptr;
    if (running) currentState = State::FAILED;
    ready.notify_all();
}
}