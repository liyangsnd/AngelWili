#include "api/live/douyu.hpp"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <random>
#include <stdexcept>
#include <cpr/cpr.h>
#include <duktape.h>
#include <nlohmann/json.hpp>
#include "bilibili/util/md5.hpp"

namespace {
struct JsBudget {
    size_t bytes = 0;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
};
struct alignas(std::max_align_t) Allocation {
    size_t size;
};
constexpr size_t JS_MEMORY_LIMIT = 16 * 1024 * 1024;

void* jsAllocate(void* userdata, duk_size_t size) {
    auto& budget = *static_cast<JsBudget*>(userdata);
    if (!size || size > JS_MEMORY_LIMIT - budget.bytes) return nullptr;
    auto* allocation = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
    if (!allocation) return nullptr;
    allocation->size = size;
    budget.bytes += size;
    return allocation + 1;
}

void jsFree(void* userdata, void* pointer) {
    if (!pointer) return;
    auto* allocation = static_cast<Allocation*>(pointer) - 1;
    static_cast<JsBudget*>(userdata)->bytes -= allocation->size;
    std::free(allocation);
}

void* jsReallocate(void* userdata, void* pointer, duk_size_t size) {
    if (!pointer) return jsAllocate(userdata, size);
    if (!size) {
        jsFree(userdata, pointer);
        return nullptr;
    }
    auto& budget = *static_cast<JsBudget*>(userdata);
    auto* allocation = static_cast<Allocation*>(pointer) - 1;
    const size_t oldSize = allocation->size;
    if (size > JS_MEMORY_LIMIT - (budget.bytes - oldSize)) return nullptr;
    auto* resized = static_cast<Allocation*>(std::realloc(allocation, sizeof(Allocation) + size));
    if (!resized) return nullptr;
    resized->size = size;
    budget.bytes = budget.bytes - oldSize + size;
    return resized + 1;
}

duk_ret_t jsMd5(duk_context* context) {
    duk_size_t length = 0;
    const char* value = duk_require_lstring(context, 0, &length);
    char output[32];
    {
        auto digest = websocketpp::md5::md5_hash_hex(std::string(value, length));
        std::memcpy(output, digest.data(), sizeof(output));
    }
    duk_push_lstring(context, output, sizeof(output));
    return 1;
}
}

extern "C" int angelwili_js_timeout(void* userdata) {
    return std::chrono::steady_clock::now() >= static_cast<JsBudget*>(userdata)->deadline;
}

namespace live {
std::string signDouyu(const std::string& script, const std::string& room,
                     const std::string& device, const std::string& timestamp) {
    if (script.empty() || script.size() > 500000) throw std::runtime_error("Invalid Douyu signing script");
    JsBudget budget;
    std::unique_ptr<duk_context, decltype(&duk_destroy_heap)> context(
        duk_create_heap(jsAllocate, jsReallocate, jsFree, &budget, nullptr), duk_destroy_heap);
    if (!context) throw std::runtime_error("Cannot create Douyu signing runtime");
    duk_push_c_function(context.get(), jsMd5, 1);
    duk_put_global_string(context.get(), "angelwiliMd5");
    const std::string bootstrap =
        "var CryptoJS={MD5:function(value){var hash=angelwiliMd5(String(value));"
        "return {toString:function(){return hash;}};}};";
    const std::string program = bootstrap + script;
    if (duk_peval_lstring(context.get(), program.data(), program.size()) != 0)
        throw std::runtime_error("Douyu signing script failed or exceeded limits");
    duk_pop(context.get());
    if (!duk_get_global_string(context.get(), "ub98484234")) throw std::runtime_error("Douyu signing function missing");
    duk_push_lstring(context.get(), room.data(), room.size());
    duk_push_lstring(context.get(), device.data(), device.size());
    duk_push_lstring(context.get(), timestamp.data(), timestamp.size());
    if (duk_pcall(context.get(), 3) != 0 || !duk_is_string(context.get(), -1))
        throw std::runtime_error("Douyu signing failed or exceeded limits");
    duk_size_t size = 0;
    const char* output = duk_get_lstring(context.get(), -1, &size);
    if (size > 4096) throw std::runtime_error("Invalid Douyu signing result");
    return std::string(output, size);
}

void requestDouyuRoom(const std::string& room, const std::function<void(DouyuRoom)>& success,
                      const std::function<void(std::string)>& error, int quality) {
    if (room.empty() || room.size() > 16 || room.find_first_not_of("0123456789") != std::string::npos) {
        error("Invalid Douyu room number");
        return;
    }
    if (quality < -1 || quality > 10000) { error("Invalid Douyu quality"); return; }
    cpr::GetCallback([room, success, error, quality](const cpr::Response& response) {
        try {
            if (response.error || response.status_code != 200) throw std::runtime_error("Douyu signing request failed");
            auto document = nlohmann::json::parse(response.text);
            if (document.at("error").get<int>() != 0) throw std::runtime_error("Douyu room signing unavailable");
            auto script = document.at("data").at("room" + room).get<std::string>();
            std::random_device random;
            std::string entropy;
            for (int count = 0; count < 4; ++count) entropy += std::to_string(random());
            auto device = websocketpp::md5::md5_hash_hex(entropy);
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            auto signature = signDouyu(script, room, device, std::to_string(timestamp));
            auto fetchQuality = [&signature, &room](int rate) {
                return cpr::Post(cpr::Url{"https://www.douyu.com/lapi/live/getH5Play/" + room},
                cpr::Timeout{10000}, cpr::Body{signature + "&rate=" + std::to_string(rate)},
                cpr::Header{{"Content-Type", "application/x-www-form-urlencoded"},
                            {"Referer", "https://www.douyu.com/" + room}, {"Origin", "https://www.douyu.com"}});
            };
            auto playback = fetchQuality(quality < 0 ? 3 : quality);
            if (playback.error || playback.status_code != 200) throw std::runtime_error("Douyu playback request failed");
            auto info = nlohmann::json::parse(playback.text);
            if (info.at("error").get<int>() != 0) throw std::runtime_error("Douyu room is offline or playback unavailable");
            if (quality < 0) {
                std::vector<StreamQuality> offered;
                for (const auto& variant : info.at("data").at("multirates"))
                    offered.push_back({variant.at("rate").get<int>(), variant.at("name").get<std::string>(), variant.value("bit", 0)});
                const int preferred = preferredQuality(offered, info.at("data").value("rate", 3));
                if (preferred != info.at("data").value("rate", 3)) {
                    auto upgraded = fetchQuality(preferred);
                    if (!upgraded.error && upgraded.status_code == 200) {
                        auto candidate = nlohmann::json::parse(upgraded.text);
                        if (candidate.value("error", -1) == 0) info = std::move(candidate);
                    }
                }
            }
            const auto& data = info.at("data");
            auto base = data.at("rtmp_url").get<std::string>();
            auto path = data.at("rtmp_live").get<std::string>();
            if (base.empty() || path.empty()) throw std::runtime_error("Douyu stream unavailable");
            std::string url = path;
            if (path.compare(0, 7, "http://") != 0 && path.compare(0, 8, "https://") != 0)
                url = base + (base.back() == '/' ? "" : "/") + path;
            if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0)
                throw std::runtime_error("Unsupported Douyu stream protocol");
            std::vector<StreamQuality> qualities;
            for (const auto& variant : data.at("multirates")) {
                qualities.push_back({variant.at("rate").get<int>(), variant.at("name").get<std::string>(), variant.value("bit", 0)});
            }
            success({"Douyu " + room, url, qualities, data.value("rate", quality < 0 ? 3 : quality)});
        } catch (const std::exception& failure) {
            error(failure.what());
        }
    }, cpr::Url{"https://www.douyu.com/swf_api/homeH5Enc?rids=" + room}, cpr::Timeout{10000});
}
}