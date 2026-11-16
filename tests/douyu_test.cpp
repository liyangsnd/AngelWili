#include "api/live/douyu.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <cpr/cpr.h>

int main(int argc, char** argv) {
    try {
        const std::string script = "function ub98484234(room,device,time){return CryptoJS.MD5('abc').toString();}";
        if (live::signDouyu(script, "9999", "device", "123") != "900150983cd24fb0d6963f7d28e17f72")
            throw std::runtime_error("MD5 signature mismatch");
        std::cout << "PASS signature MD5\n";
        auto start = std::chrono::steady_clock::now();
        bool stopped = false;
        try { live::signDouyu("while(true){}", "9999", "device", "123"); }
        catch (const std::exception&) { stopped = true; }
        if (!stopped || std::chrono::steady_clock::now() - start > std::chrono::seconds(5))
            throw std::runtime_error("Execution timeout not enforced");
        std::cout << "PASS execution timeout\n";
        stopped = false;
        try { live::signDouyu("var text='x';while(text.length<25000000){text+=text;}", "9999", "device", "123"); }
        catch (const std::exception&) { stopped = true; }
        if (!stopped) throw std::runtime_error("Resource limit not enforced");
        std::cout << "PASS resource limit\n";
        if (argc > 1) {
            const bool lineOnly = argc > 2 && std::string(argv[2]) == "--line-only";
            const int requestedQuality = argc > 2 && !lineOnly ? std::stoi(argv[2]) : -1;
            std::promise<live::DouyuRoom> promise;
            auto result = promise.get_future();
            live::requestDouyuRoom(argv[1], [&promise](live::DouyuRoom room) {
                promise.set_value(std::move(room));
            }, [&promise](std::string error) {
                promise.set_exception(std::make_exception_ptr(std::runtime_error(error)));
            }, requestedQuality);
            auto room = result.get();
            if (room.url.empty()) throw std::runtime_error("No Douyu stream returned");
            if (room.qualities.empty() || (requestedQuality >= 0 && room.selectedQuality != requestedQuality))
                throw std::runtime_error("Douyu quality selection mismatch");
            std::cout << "PASS native public room resolution\n";
            if (lineOnly) {
                if (room.lines.size() < 2 || room.selectedLine.empty())
                    throw std::runtime_error("Douyu alternate lines unavailable");
                std::promise<live::DouyuRoom> alternatePromise;
                auto alternateResult = alternatePromise.get_future();
                const auto alternateLine = room.lines.back().id;
                live::requestDouyuRoom(argv[1], [&alternatePromise](live::DouyuRoom alternate) {
                    alternatePromise.set_value(std::move(alternate));
                }, [&alternatePromise](std::string error) {
                    alternatePromise.set_exception(std::make_exception_ptr(std::runtime_error(error)));
                }, room.selectedQuality, alternateLine);
                const auto alternate = alternateResult.get();
                if (alternate.selectedLine != alternateLine || alternate.url == room.url)
                    throw std::runtime_error("Douyu line selection mismatch");
                std::cout << "PASS native Douyu line selection\n";
                return 0;
            }
            size_t bytes = 0;
            start = std::chrono::steady_clock::now();
            auto media = cpr::Get(cpr::Url{room.url}, cpr::Timeout{12000},
                cpr::Header{{"Referer", "https://www.douyu.com/"}, {"Origin", "https://www.douyu.com"}},
                cpr::WriteCallback{[&bytes](std::string chunk, intptr_t) { bytes += chunk.size(); return true; }});
            if (media.status_code != 200 || bytes < 200000 || std::chrono::steady_clock::now() - start < std::chrono::seconds(10))
                throw std::runtime_error("Douyu stream ended early");
            std::cout << "PASS native stream reception >=10 seconds\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}