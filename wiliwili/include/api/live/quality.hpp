#pragma once
#include <string>
#include <vector>

namespace live {
struct RecoveryBudget {
    int attempts = 0;
    int nextDelay() {
        if (attempts >= 3) return 0;
        return 2000 << attempts++;
    }
    void reset() { attempts = 0; }
};

struct StreamQuality {
    int id;
    std::string name;
    int bitrate;
};

struct StreamLine {
    std::string id;
    std::string name;
};

inline int preferredQuality(const std::vector<StreamQuality>& qualities, int fallback) {
    int selected = fallback;
    int bitrate = -1;
    for (const auto& quality : qualities) {
        if (quality.name.find("1080") != std::string::npos && quality.bitrate > 0 &&
            quality.bitrate <= 8000 && quality.bitrate > bitrate) {
            selected = quality.id;
            bitrate = quality.bitrate;
        }
    }
    return selected;
}

inline int lowerQuality(const std::vector<StreamQuality>& qualities, int current) {
    int currentBitrate = -1;
    for (const auto& quality : qualities) if (quality.id == current) currentBitrate = quality.bitrate;
    int selected = -1;
    int selectedBitrate = -1;
    for (const auto& quality : qualities) {
        if (quality.id != current && quality.bitrate > 0 &&
            (currentBitrate == 0 || quality.bitrate < currentBitrate) && quality.bitrate > selectedBitrate) {
            selected = quality.id;
            selectedBitrate = quality.bitrate;
        }
    }
    return selected;
}
}