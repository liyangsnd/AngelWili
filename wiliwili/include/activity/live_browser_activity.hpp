#pragma once
#include <memory>
#include <map>
#include <borealis/core/activity.hpp>
#include <borealis/core/bind.hpp>
#include "api/live/room_list.hpp"
#include "presenter/presenter.h"

class RecyclingGrid;
namespace brls { class Label; }

class LiveBrowserActivity : public brls::Activity, public Presenter {
public:
    CONTENT_FROM_XML_RES("activity/live_browser.xml");
    void onContentAvailable() override;
    ~LiveBrowserActivity() override;

private:
    void loadPage(live::Platform platform, int page, const live::LiveCategory* category = nullptr);
    void selectCategory();
    void showCategories(const std::string& search = "");
    void showSavedRooms();
    void toggleFavorite();
    std::map<live::Platform, std::vector<live::LiveCategory>> categories;
    std::map<live::Platform, live::LiveCategory> selectedCategories;
    live::Platform platform = live::Platform::HUYA;
    int page = 1;
    bool hasMore = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    BRLS_BIND(RecyclingGrid, grid, "live/rooms");
    BRLS_BIND(brls::Label, status, "live/status");
};