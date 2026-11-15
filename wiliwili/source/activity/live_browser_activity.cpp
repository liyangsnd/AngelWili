#include "activity/live_browser_activity.hpp"
#include <borealis/core/ime.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/label.hpp>
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"
#include "view/grid_dropdown.hpp"
#include "utils/activity_helper.hpp"
#include "utils/config_helper.hpp"
#include "api/live/room_library.hpp"
#include <unordered_set>

using namespace brls::literals;

namespace {
class LiveRoomDataSource : public RecyclingGridDataSource {
public:
    LiveRoomDataSource(live::Platform platform, std::vector<live::RoomSummary> rooms)
        : platform(platform), rooms(std::move(rooms)) {
        live::RoomLibrary library(ProgramConfig::instance().getConfigDir() + "/live_rooms.json");
        for (const auto& saved : library.favorites())
            if (saved.platform == platform) favoriteIds.insert(saved.room.id);
    }
    RecyclingGridItem* cellForRow(RecyclingGrid* grid, size_t index) override {
        auto* cell = static_cast<RecyclingGridItemLiveVideoCard*>(grid->dequeueReusableCell("LiveRoom"));
        const auto& room = rooms.at(index);
        cell->setCard(room.cover, room.title, room.owner, platform == live::Platform::HUYA ? "Huya" : "Douyu", room.viewers, false);
        cell->setFavorite(favoriteIds.count(room.id) != 0);
        return cell;
    }
    size_t getItemCount() override { return rooms.size(); }
    const live::RoomSummary* roomAt(size_t index) const { return index < rooms.size() ? &rooms[index] : nullptr; }
    size_t append(const std::vector<live::RoomSummary>& more) { return live::appendUniqueRooms(rooms, more); }
    void setFavorite(const std::string& id, bool favorite) {
        if (favorite) favoriteIds.insert(id);
        else favoriteIds.erase(id);
    }
    void onItemSelected(RecyclingGrid*, size_t index) override {
        Intent::openPlatformLive(platform == live::Platform::DOUYU, rooms.at(index).id);
    }
    void clearData() override { rooms.clear(); }
private:
    live::Platform platform;
    std::vector<live::RoomSummary> rooms;
    std::unordered_set<std::string> favoriteIds;
};
}

void LiveBrowserActivity::onContentAvailable() {
    grid->registerCell("LiveRoom", []() { return new RecyclingGridItemLiveVideoCard(); });
    auto selectPlatform = [this](...) {
        if (requesting) return true;
        auto guard = alive;
        BaseDropdown::text("wiliwili/live_browser/platform"_i18n, {"Huya", "Douyu"}, [this, guard](int selected) {
            if (!*guard || requesting) return;
            loadPage(selected == 0 ? live::Platform::HUYA : live::Platform::DOUYU, 1);
        }, platform == live::Platform::HUYA ? 0 : 1);
        return true;
    };
    this->registerAction("wiliwili/live_browser/platform"_i18n, brls::ControllerButton::BUTTON_X, selectPlatform);
    this->registerAction("wiliwili/live_browser/category"_i18n, brls::ControllerButton::BUTTON_LB, [this](...) {
        selectCategory();
        return true;
    });
    this->registerAction("wiliwili/live_browser/room"_i18n, brls::ControllerButton::BUTTON_Y, [this](...) {
        auto guard = alive;
        const auto selected = platform;
        return brls::Application::getImeManager()->openForText([this, guard, selected](const std::string& room) {
            if (!*guard || room.empty() || room.find_first_not_of("0123456789") != std::string::npos) return;
            Intent::openPlatformLive(selected == live::Platform::DOUYU, room);
        }, "wiliwili/live_browser/room"_i18n, "", 16, "", 0);
    });
    this->registerAction("wiliwili/live_browser/refresh"_i18n, brls::ControllerButton::BUTTON_START, [this](...) { loadPage(platform, 1); return true; });
    this->registerAction("wiliwili/live_browser/favorite"_i18n, brls::ControllerButton::BUTTON_RB, [this](...) { toggleFavorite(); return true; });
    this->registerAction("wiliwili/live_browser/library"_i18n, brls::ControllerButton::BUTTON_BACK, [this](...) { showSavedRooms(); return true; });
    this->registerAction("wiliwili/live_browser/back"_i18n, brls::ControllerButton::BUTTON_B, [](auto*) {
        brls::Application::popActivity();
        return true;
    });
    grid->setRefreshAction([this]() { loadPage(platform, 1); });
    grid->onNextPage([this]() {
        if (hasMore && !requesting && page < 1000) loadPage(platform, page + 1);
    });
    loadPage(platform, 1);
}

void LiveBrowserActivity::selectCategory() {
    if (requesting) return;
    if (categories.count(platform)) { showCategories(); return; }
    requesting = true;
    status->setText("wiliwili/live_browser/loading_categories"_i18n);
    const auto selected = platform;
    ASYNC_RETAIN
    live::requestCategories(selected, [ASYNC_TOKEN, selected](std::vector<live::LiveCategory> result) {
        brls::sync([ASYNC_TOKEN, selected, result]() {
            ASYNC_RELEASE
            requesting = false;
            categories[selected] = result;
            status->setText(std::string(selected == live::Platform::HUYA ? "Huya" : "Douyu") + " - " +
                            (selectedCategories[selected].name.empty() ? "wiliwili/live_browser/all"_i18n : selectedCategories[selected].name));
            showCategories();
        });
    }, [ASYNC_TOKEN](std::string error) {
        brls::sync([ASYNC_TOKEN, error]() {
            ASYNC_RELEASE
            requesting = false;
            status->setText(error);
        });
    });
}

void LiveBrowserActivity::showCategories(const std::string& search) {
    std::vector<live::LiveCategory> choices = {{"", "wiliwili/live_browser/all"_i18n}, {"", "wiliwili/live_browser/search_categories"_i18n}};
    for (const auto& category : categories[platform]) {
        if (search.empty() || category.name.find(search) != std::string::npos) choices.push_back(category);
    }
    std::vector<std::string> labels;
    int current = 0;
    for (size_t index = 0; index < choices.size(); ++index) {
        labels.push_back(choices[index].name);
        if (index != 1 && choices[index].id == selectedCategories[platform].id) current = static_cast<int>(index);
    }
    auto guard = alive;
    const auto selected = platform;
    BaseDropdown::text("wiliwili/live_browser/category"_i18n, labels, [this, guard, choices, selected](int index) {
        if (!*guard || requesting || selected != platform || index < 0 || static_cast<size_t>(index) >= choices.size()) return;
        if (index == 1) {
            brls::Application::getImeManager()->openForText([this, guard, selected](const std::string& text) {
                if (!*guard || requesting || selected != platform) return;
                showCategories(text);
            }, "wiliwili/live_browser/search_categories"_i18n, "", 32, "", 0);
        } else {
            loadPage(selected, 1, &choices[index]);
        }
    }, current);
}

void LiveBrowserActivity::loadPage(live::Platform selected, int requestedPage, const live::LiveCategory* category) {
    if (requesting || requestedPage < 1 || requestedPage > 1000) return;
    requesting = true;
    const auto requestedCategory = category ? *category : selectedCategories[selected];
    const std::string heading = std::string(selected == live::Platform::HUYA ? "Huya" : "Douyu") + " - " +
        (requestedCategory.name.empty() ? "wiliwili/live_browser/all"_i18n : requestedCategory.name);
    status->setText(heading + " - loading");
    ASYNC_RETAIN
    live::requestRoomPage(selected, requestedPage, [ASYNC_TOKEN, selected, requestedPage, requestedCategory, heading](live::RoomPage result) {
        brls::sync([ASYNC_TOKEN, selected, requestedPage, requestedCategory, heading, result]() {
            ASYNC_RELEASE
            requesting = false;
            platform = selected;
            selectedCategories[selected] = requestedCategory;
            page = requestedPage;
            hasMore = result.hasMore;
            auto* source = dynamic_cast<LiveRoomDataSource*>(grid->getDataSource());
            if (requestedPage > 1 && source) {
                if (source->append(result.rooms) == 0) hasMore = false;
                grid->notifyDataChanged();
            } else {
                grid->setDefaultCellFocus(0);
                grid->setDataSource(new LiveRoomDataSource(platform, result.rooms));
                if (result.rooms.empty()) grid->setEmpty("wiliwili/live_browser/empty"_i18n);
                else brls::Application::giveFocus(grid);
            }
            status->setText(heading + " - " + std::to_string(grid->getItemCount()) + " rooms" +
                            (hasMore ? "" : " - " + "wiliwili/live_browser/end"_i18n));
        });
    }, [ASYNC_TOKEN](std::string error) {
        brls::sync([ASYNC_TOKEN, error]() {
            ASYNC_RELEASE
            requesting = false;
            status->setText(error);
            if (grid->getItemCount() == 0) grid->setError(error);
        });
    }, requestedCategory.id);
}

void LiveBrowserActivity::toggleFavorite() {
    if (requesting) return;
    auto* source = dynamic_cast<LiveRoomDataSource*>(grid->getDataSource());
    auto* focus = brls::Application::getCurrentFocus();
    RecyclingGridItem* item = nullptr;
    while (focus && !(item = dynamic_cast<RecyclingGridItem*>(focus))) focus = focus->getParent();
    const auto* room = source && item ? source->roomAt(item->getIndex()) : nullptr;
    if (!room) return;
    live::RoomLibrary library(ProgramConfig::instance().getConfigDir() + "/live_rooms.json");
    const bool success = library.toggleFavorite(platform, *room);
    if (success) {
        bool favorite = false;
        for (const auto& saved : library.favorites())
            if (saved.platform == platform && saved.room.id == room->id) favorite = true;
        source->setFavorite(room->id, favorite);
        if (auto* card = dynamic_cast<RecyclingGridItemLiveVideoCard*>(item)) card->setFavorite(favorite);
    }
    brls::Application::notify(success ? "wiliwili/live_browser/favorite_updated"_i18n : "wiliwili/live_browser/save_failed"_i18n);
}

void LiveBrowserActivity::showSavedRooms() {
    auto guard = alive;
    BaseDropdown::text("wiliwili/live_browser/library"_i18n,
        {"wiliwili/live_browser/favorites"_i18n, "wiliwili/live_browser/recent"_i18n}, [this, guard](int selected) {
        if (!*guard) return;
        live::RoomLibrary library(ProgramConfig::instance().getConfigDir() + "/live_rooms.json");
        const auto rooms = selected == 0 ? library.favorites() : library.recent();
        if (rooms.empty()) { brls::Application::notify("wiliwili/live_browser/library_empty"_i18n); return; }
        std::vector<std::string> names;
        for (const auto& item : rooms) names.push_back(std::string(item.platform == live::Platform::HUYA ? "Huya - " : "Douyu - ") +
            (item.room.title.empty() ? item.room.id : item.room.title));
        BaseDropdown::text(selected == 0 ? "wiliwili/live_browser/favorites"_i18n : "wiliwili/live_browser/recent"_i18n,
            names, [guard, rooms](int index) {
                if (!*guard || index < 0 || static_cast<size_t>(index) >= rooms.size()) return;
                Intent::openPlatformLive(rooms[index].platform == live::Platform::DOUYU, rooms[index].room.id);
            }, 0);
    }, 0);
}

LiveBrowserActivity::~LiveBrowserActivity() { *alive = false; }