#include "api/live/huya_danmaku.hpp"
#include <regex>
#include <stdexcept>

namespace {
struct Field {
    int tag = 0;
    int type = 0;
    int64_t number = 0;
    std::string text;
    std::vector<Field> children;
};

class Reader {
public:
    explicit Reader(const std::string& data) : data(data) {
        if (data.size() > 1024 * 1024) throw std::runtime_error("Huya packet limit");
    }
    std::vector<Field> fields(int depth = 0, bool structure = false) {
        if (depth > 16) throw std::runtime_error("Huya nesting limit");
        std::vector<Field> result;
        while (offset < data.size()) {
            auto item = field(depth);
            if (item.type == 11) {
                if (!structure) throw std::runtime_error("Unexpected Huya structure end");
                return result;
            }
            if (result.size() >= 4096) throw std::runtime_error("Huya field limit");
            result.push_back(std::move(item));
        }
        if (structure) throw std::runtime_error("Truncated Huya structure");
        return result;
    }
private:
    const std::string& data;
    size_t offset = 0;
    uint64_t integer(size_t size) {
        if (size > data.size() - offset) throw std::runtime_error("Truncated Huya integer");
        uint64_t value = 0;
        for (size_t index = 0; index < size; ++index) value = (value << 8) | static_cast<unsigned char>(data[offset++]);
        return value;
    }
    std::string bytes(size_t size) {
        if (size > data.size() - offset) throw std::runtime_error("Truncated Huya bytes");
        auto value = data.substr(offset, size);
        offset += size;
        return value;
    }
    Field field(int depth) {
        if (depth > 16) throw std::runtime_error("Huya nesting limit");
        auto header = integer(1);
        Field result;
        result.tag = header >> 4;
        result.type = header & 15;
        if (result.tag == 15) result.tag = static_cast<int>(integer(1));
        if (result.type <= 3) {
            const size_t width = size_t{1} << result.type;
            auto value = integer(width);
            if (width < 8 && (value & (uint64_t{1} << (width * 8 - 1)))) value |= (~uint64_t{0}) << (width * 8);
            result.number = static_cast<int64_t>(value);
        } else if (result.type == 4 || result.type == 5) {
            bytes(result.type == 4 ? 4 : 8);
        } else if (result.type == 6 || result.type == 7) {
            result.text = bytes(static_cast<size_t>(integer(result.type == 6 ? 1 : 4)));
        } else if (result.type == 10) {
            result.children = fields(depth + 1, true);
        } else if (result.type == 8 || result.type == 9 || result.type == 13) {
            if (result.type == 13 && integer(1) != 0) throw std::runtime_error("Unsupported Huya byte list");
            auto count = field(depth + 1);
            if (count.type > 3 && count.type != 12) throw std::runtime_error("Invalid Huya list count");
            if (count.number < 0 || count.number > 1024 * 1024) throw std::runtime_error("Huya list limit");
            if (result.type == 13) result.text = bytes(static_cast<size_t>(count.number));
            else {
                if (count.number > 4096) throw std::runtime_error("Huya element limit");
                for (int64_t index = 0; index < count.number * (result.type == 8 ? 2 : 1); ++index)
                    result.children.push_back(field(depth + 1));
            }
        } else if (result.type != 11 && result.type != 12) throw std::runtime_error("Unsupported Huya field");
        return result;
    }
};

const Field* find(const std::vector<Field>& fields, int tag) {
    for (const auto& item : fields) if (item.tag == tag) return &item;
    return nullptr;
}

void number(std::string& output, int tag, int64_t value) {
    int type = value == 0 ? 12 : value >= -128 && value <= 127 ? 0 : value >= -32768 && value <= 32767 ? 1 : value >= INT32_MIN && value <= INT32_MAX ? 2 : 3;
    output.push_back(static_cast<char>((tag << 4) | type));
    if (type == 12) return;
    for (int index = (1 << type) - 1; index >= 0; --index) output.push_back(static_cast<char>((static_cast<uint64_t>(value) >> (index * 8)) & 255));
}
void blob(std::string& output, int tag, const std::string& value) {
    output.push_back(static_cast<char>((tag << 4) | 13));
    output.push_back(0);
    number(output, 0, static_cast<int64_t>(value.size()));
    output += value;
}
}

namespace live {
std::string huyaRegistration(const std::string& page) {
    std::string body;
    const char* keys[] = {"lYyid", "lChannelId", "lSubChannelId"};
    int64_t values[3];
    for (size_t index = 0; index < 3; ++index) {
        std::smatch match;
        if (!std::regex_search(page, match, std::regex(std::string(keys[index]) + R"("?\s*:\s*"?(\d+))")))
            return "";
        values[index] = std::stoll(match[1].str());
    }
    number(body, 0, values[0]);
    number(body, 1, 1);
    body += std::string("\x26\0\x36\0", 4);
    number(body, 4, values[1]);
    number(body, 5, values[2]);
    number(body, 6, 0);
    number(body, 7, 0);
    std::string packet;
    number(packet, 0, 1);
    blob(packet, 1, body);
    return packet;
}

std::vector<TextDanmaku> decodeHuyaMessages(const std::string& bytes) {
    auto command = Reader(bytes).fields();
    const auto* type = find(command, 0);
    const auto* payload = find(command, 1);
    if (!type || !payload || type->number != 7 || payload->type != 13) return {};
    auto push = Reader(payload->text).fields();
    const auto* uri = find(push, 1);
    const auto* data = find(push, 2);
    if (!uri || uri->number != 1400 || !data || data->type != 13) return {};
    auto message = Reader(data->text).fields();
    const auto* content = find(message, 3);
    const auto* user = find(message, 0);
    if (!content || content->text.empty() || content->text.size() > 4096) return {};
    const Field* sender = user ? find(user->children, 2) : nullptr;
    return {{sender && sender->text.size() <= 256 ? sender->text : "", content->text}};
}
}