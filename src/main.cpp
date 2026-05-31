#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    loadCacheThenFetch();
}

static std::unordered_map<int, float> g_blockList;
static async::TaskHolder<web::WebResponse> g_fetchTask;

static constexpr const char* LIST_URL =
    "https://raw.githubusercontent.com/MalikHw/fuck/main/list.json";
static constexpr const char* SAVE_KEY = "cached_block_list";

static void parseAndStore(std::string_view jsonStr) {
    auto res = matjson::parse(jsonStr);
    if (!res) {
        log::error("InputBlocker: failed to parse list.json: {}", res.error()); return;
    }
    auto& root = res.value();
    if (!root.isObject()) {
        log::error("InputBlocker: list.json root is not an object"); return;
    }
    g_blockList.clear();
    for (auto& [key, val] : root) {
        auto idResult = std::stoi(key);  // key = level ID as string
        float pct = val.isNumber() ? static_cast<float>(val.asDouble().unwrapOr(0.0)) : 0.f;
        g_blockList[idResult] = pct;
    }
    log::info("InputBlocker: loaded {} entries from list.json", g_blockList.size());
}

// the fetcher ultra
static void fetchList() {
    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(10));
    g_fetchTask.spawn(
        req.get(LIST_URL),
        [](web::WebResponse res) {
            if (!res.ok()) {
                log::warn("InputBlocker: fetch failed ({}), using cache", res.code());
                // fall back to cache
                auto cached = Mod::get()->getSavedValue<std::string>(SAVE_KEY, ""); if (!cached.empty()) parseAndStore(cached); return;
            }
            auto body = res.string().unwrapOr("");
            if (body.empty()) {
                log::warn("InputBlocker: empty response, using cache");
                auto cached = Mod::get()->getSavedValue<std::string>(SAVE_KEY, ""); if (!cached.empty()) parseAndStore(cached); return;
            }
            Mod::get()->setSavedValue<std::string>(SAVE_KEY, body); parseAndStore(body);
        }
    );
}

static void loadCacheThenFetch() {
    auto cached = Mod::get()->getSavedValue<std::string>(SAVE_KEY, "");
    if (!cached.empty()) parseAndStore(cached);
    // fresh fetch ALWAYS
    fetchList();
}

// PlayLayer bs
class $modify(InputBlockerPlayLayer, PlayLayer) {
    struct Fields {
        int m_trackedLevelID = -1; float m_blockAtPercent = -1.f; bool m_inputBlocked = false;
    };
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        int lvlID = level->m_levelID.value();
        auto it = g_blockList.find(lvlID);
        if (it != g_blockList.end()) {
            m_fields->m_trackedLevelID = lvlID;
            m_fields->m_blockAtPercent = it->second;
            log::info("InputBlocker: tracking level {} — will block at {:.1f}%",
                lvlID, m_fields->m_blockAtPercent);
        }
        return true;
    }
    // Called every frame
    // EDIT not anymore
    void updateProgressbar() {
        PlayLayer::updateProgressbar();
        if (m_fields->m_blockAtPercent < 0.f) return;
        if (m_fields->m_inputBlocked) return; // already blocked, skip every future call too

        if (this->getCurrentPercent() >= m_fields->m_blockAtPercent) {
            m_fields->m_inputBlocked = true;
            log::info("InputBlocker: reached {:.2f}% on level {} — blocking input",
                this->getCurrentPercent(), m_fields->m_trackedLevelID);
        }
    }
    // Intercept pushButton
    bool pushButton(PlayerButton btn) {
        if (m_fields->m_inputBlocked) return false; // take the input
        return PlayLayer::pushButton(btn);
    }
    // intercept releaseButton too
    bool releaseButton(PlayerButton btn) {
        if (m_fields->m_inputBlocked) return false;
        return PlayLayer::releaseButton(btn);
    }
    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->m_inputBlocked = false;
    }
};