//
// Created by fang on 2023/7/18.
//

#pragma once

#include <borealis/core/activity.hpp>
#include <libpdr.h>
#include <chrono>
#include "api/live/quality.hpp"
#include "api/live/douyu_danmaku.hpp"

#include "utils/event_helper.hpp"
#include "presenter/presenter.h"

class VideoView;
class DLNAActivity : public brls::Activity, public Presenter {
public:
    CONTENT_FROM_XML_RES("activity/video_activity.xml");

    DLNAActivity(const std::string& room = "", bool douyu = false);

    void onContentAvailable() override;

    ~DLNAActivity() override;

    void dismiss();

private:
    void openHuyaRoom(const std::string& room);
    void openDouyuRoom(const std::string& room);
    void loadLiveRoom(const std::string& room, bool douyu, int quality = -1, bool recovery = false,
                      const std::string& line = "");
    void showLiveQuality();
    void showLiveLine();
    void scheduleLiveRetry(bool lowerQuality);
    void cancelLiveRetry();
    void stopLiveDanmaku();
    void pollLiveDanmaku();
    std::unique_ptr<live::DouyuDanmakuClient> douyuDanmaku;
    size_t danmakuDelay = 0;
    std::string activeRoom;
    bool activeDouyu = false;
    int selectedQuality = -1;
    std::vector<live::StreamQuality> liveQualities;
    std::string selectedLine;
    std::vector<live::StreamLine> liveLines;
    live::RecoveryBudget recoveryBudget;
    size_t retryDelay = 0;
    bool retryPending = false;
    bool ignoreNextStop = false;
    bool playbackStarted = false;
    bool historyRecorded = false;
    std::string liveTitle;
    std::chrono::steady_clock::time_point stableSince;
    CustomEvent::Subscription qualityEvent;
    std::string initialRoom;
    bool initialDouyu = false;
    std::shared_ptr<bool> inputAlive = std::make_shared<bool>(true);

    BRLS_BIND(VideoView, video, "video");

    // 监控mpv事件
    MPVEvent::Subscription mpvEventSubscribeID;
    // 监控DLNA事件
    pdr::Subscription dlnaEventSubscribeID;
    std::shared_ptr<pdr::DLNA> dlna;

    std::string ip, uuid;
    size_t port;
};