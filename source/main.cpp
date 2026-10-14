#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include "config.hpp"
#include "dmnt_cht.hpp"
#include "ff12_rng.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>

namespace {

constexpr uint64_t TARGET_TITLE_ID = 0x0100EB100AB42000ULL;
constexpr const char* CONFIG_PATH = "sdmc:/config/ff12rng/config.ini";
constexpr const char* APP_VERSION_TEXT = "0.3.0";
constexpr uint64_t TICKS_PER_MS_MAX = 1000;
constexpr s64 LOCK_POLL_NS = 50'000'000LL;
bool g_sd_mounted = false;

struct LockWorker {
    ff12::RngTracker* tracker = nullptr;
    std::atomic<bool> running{false};
    std::atomic<bool> enabled{false};
    Thread thread{};

    static void threadFunc(void* arg) {
        auto* worker = static_cast<LockWorker*>(arg);
        while (worker->running.load(std::memory_order_acquire)) {
            if (worker->enabled.load(std::memory_order_acquire))
                (void)worker->tracker->MaintainLock();
            svcSleepThread(LOCK_POLL_NS);
        }
    }

    bool start(ff12::RngTracker* target) {
        if (running.load(std::memory_order_acquire))
            return true;
        tracker = target;
        running.store(true, std::memory_order_release);
        if (R_FAILED(threadCreate(&thread, threadFunc, this, nullptr,
                                  0x2000, 0x2C, -2))) {
            running.store(false, std::memory_order_release);
            return false;
        }
        if (R_FAILED(threadStart(&thread))) {
            running.store(false, std::memory_order_release);
            threadClose(&thread);
            return false;
        }
        return true;
    }

    void stop() {
        if (!running.exchange(false, std::memory_order_acq_rel))
            return;
        threadWaitForExit(&thread);
        threadClose(&thread);
    }
};

class MainGui final : public tsl::Gui {
public:
    MainGui() {
        m_cfg.title_id = TARGET_TITLE_ID;
        std::string error;
        m_cfg_loaded = ff12::LoadConfig(CONFIG_PATH, m_cfg, error);
        if (!m_cfg_loaded) m_config_error = error;
        m_tracker.Configure(m_cfg);
        m_worker_started = m_worker.start(&m_tracker);
    }

    ~MainGui() override {
        m_tracker.Unlock();
        m_worker.stop();
    }

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("FF12 TZA RNG Tracker", APP_VERSION_TEXT);
        auto* list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("Live RNG", true));
        list->addItem(new tsl::elm::CustomDrawer(
            [this](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
                render(renderer, x, y, w, h);
            }), 450);

        list->addItem(new tsl::elm::CategoryHeader("RNG Lock", true));

        auto* lock_value_item = new tsl::elm::ListItem(
            "Lock value", std::to_string(m_lock_value));
        lock_value_item->setClickListener([this, lock_value_item](u64 keys) {
            if (!(keys & HidNpadButton_A))
                return false;
            m_lock_value = m_lock_value == 99 ? 0 : m_lock_value + 1;
            lock_value_item->setValue(std::to_string(m_lock_value));
            return true;
        });
        list->addItem(lock_value_item);

        auto* lock_item = new tsl::elm::ListItem(
            "Lock", m_tracker.LockActive() ? "ON" : "OFF");
        lock_item->setClickListener([this, lock_item](u64 keys) {
            if (!(keys & HidNpadButton_A))
                return false;
            if (m_tracker.LockActive()) {
                m_tracker.Unlock();
                m_worker.enabled.store(false, std::memory_order_release);
                lock_item->setValue("OFF");
                m_status = "ONLINE";
            } else {
                const Result rc = m_tracker.LockPercentage(m_lock_value);
                if (m_tracker.LockActive() && m_worker_started)
                    m_worker.enabled.store(true, std::memory_order_release);
                lock_item->setValue(m_tracker.LockActive() ? "ON" : "ERROR");
                m_status = m_tracker.LockActive()
                    ? (m_worker_started ? "ONLINE / LOCKED" : "LOCK THREAD ERROR")
                    : "LOCK ERROR: " + m_tracker.LockError();
                m_last_rc = rc;
            }
            return true;
        });
        list->addItem(lock_item);

        frame->setContent(list);
        return frame;
    }

    void update() override {
        const u64 now = armGetSystemTick();
        const u64 freq = armGetSystemTickFreq();
        if (m_last_tick != 0) {
            const u64 elapsed_ms = (now - m_last_tick) * TICKS_PER_MS_MAX / freq;
            if (elapsed_ms < m_cfg.poll_ms)
                return;
        }
        m_last_tick = now;
        refreshGame();
    }

private:
    void refreshGame() {
        if (!m_cfg_loaded) {
            m_status = "CONFIG ERROR";
            return;
        }

        if (!ff12::dmnt::IsInitialized()) {
            m_status = "dmnt:cht unavailable";
            return;
        }

        bool has_process = false;
        Result rc = ff12::dmnt::HasCheatProcess(has_process);
        if (R_FAILED(rc)) {
            m_status = "dmnt process query failed";
            m_last_rc = rc;
            return;
        }
        if (!has_process) {
            rc = ff12::dmnt::ForceOpenCheatProcess();
            if (R_FAILED(rc)) {
                m_status = "cannot attach dmnt";
                m_last_rc = rc;
                return;
            }
        }

        ff12::DmntCheatProcessMetadata meta{};
        rc = ff12::dmnt::GetMetadata(meta);
        if (R_FAILED(rc)) {
            m_status = "metadata read failed";
            m_last_rc = rc;
            return;
        }

        m_meta = meta;
        if (meta.title_id != m_cfg.title_id) {
            m_status = "waiting for FFXII TZA";
            return;
        }

        rc = m_tracker.Refresh(meta);
        if (R_FAILED(rc)) {
            m_status = "RNG read error";
            m_last_rc = rc;
            return;
        }

        if (m_tracker.LockActive()) {
            rc = m_tracker.MaintainLock();
            if (R_FAILED(rc)) {
                m_status = "LOCK ERROR: " + m_tracker.LockError();
                m_last_rc = rc;
                return;
            }
        }

        if (m_tracker.Snapshot().valid)
            m_status = m_tracker.LockActive()
                ? "ONLINE / LOCKED"
                : (m_tracker.Snapshot().coherent ? "ONLINE" : "ONLINE / coherence warning");
        else
            m_status = m_tracker.Error().empty() ? "offline" : m_tracker.Error();
    }

    void drawLine(tsl::gfx::Renderer* renderer, const std::string& text, s32 x, s32 y, s32 size = 16) {
        renderer->drawString(text.c_str(), false, x, y, size, renderer->a(0xFFFF));
    }

    tsl::Color percentColor(uint32_t percent) const {
        if (percent == 99)
            return {0xF, 0x2, 0x2, 0xF};
        if (percent == 0)
            return {0x0, 0xF, 0x4, 0xF};
        return {0xF, 0xF, 0xF, 0xF};
    }

    void render(tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 /*h*/) {
        const auto& s = m_tracker.Snapshot();
        char line[192];
        s32 yy = y + 22;

        drawLine(renderer, "Status: " + m_status, x + 10, yy); yy += 25;

        if (!m_cfg_loaded) {
            drawLine(renderer, "Path: /config/ff12rng/config.ini", x + 10, yy); yy += 25;
            drawLine(renderer, "Config: " + m_config_error, x + 10, yy); yy += 25;
            return;
        }

        if (!s.valid) {
            drawLine(renderer, "Target: 0100EB100AB42000", x + 10, yy); yy += 25;
            drawLine(renderer, "Mode: " + m_cfg.rng_base, x + 10, yy); yy += 25;
            drawLine(renderer, "Error: " + m_tracker.Error(), x + 10, yy); yy += 25;
            return;
        }

        std::snprintf(line, sizeof(line), "PID: %llu  Main: %016llX",
                      static_cast<unsigned long long>(s.process_id),
                      static_cast<unsigned long long>(s.main_base));
        drawLine(renderer, line, x + 10, yy); yy += 25;

        std::snprintf(line, sizeof(line), "BID: %.32s",
                      ff12::FormatBuildId(m_meta.main_nso_build_id).c_str());
        drawLine(renderer, line, x + 10, yy, 14); yy += 20;

        std::snprintf(line, sizeof(line), "Slot: %016llX",
                      static_cast<unsigned long long>(s.pointer_slot));
        drawLine(renderer, line, x + 10, yy); yy += 25;

        std::snprintf(line, sizeof(line), "Obj:  %016llX",
                      static_cast<unsigned long long>(s.pointer_value));
        drawLine(renderer, line, x + 10, yy); yy += 25;

        std::snprintf(line, sizeof(line), "Rng: %016llX  Mti: %u/624",
                      static_cast<unsigned long long>(s.object_address), s.index);
        drawLine(renderer, line, x + 10, yy); yy += 25;

        std::snprintf(line, sizeof(line), "State: %016llX",
                      static_cast<unsigned long long>(s.state_address));
        drawLine(renderer, line, x + 10, yy); yy += 25;

        std::snprintf(line, sizeof(line), "RAW: 0x%08X  RNG%%: ", s.next_raw);
        const auto prefixSize = renderer->drawString(
            line, false, x + 10, yy, 18,
            renderer->a(tsl::style::color::ColorText));
        std::snprintf(line, sizeof(line), "%02u", s.current_percent);
        renderer->drawString(line, false, x + 10 + static_cast<s32>(prefixSize.first),
                             yy, 18, renderer->a(percentColor(s.current_percent)));
        yy += 29;

        std::snprintf(line, sizeof(line), "Success [%02u..%02u] = %.0f%%",
                      m_cfg.success_min, m_cfg.success_max, m_tracker.SuccessProbability());
        drawLine(renderer, line, x + 10, yy); yy += 25;

        drawLine(renderer,
                 std::string("Current result: ") +
                     (m_tracker.IsSuccessPercent(s.current_percent) ? "SUCCESS" : "FAIL"),
                 x + 10, yy);
        yy += 25;

        std::snprintf(line, sizeof(line), "Observed RNG advances: %llu",
                      static_cast<unsigned long long>(s.observed_consumed));
        drawLine(renderer, line, x + 10, yy); yy += 25;

        drawLine(renderer, "Next RNG%%:", x + 10, yy); yy += 23;
        const uint32_t count = std::min<uint32_t>(m_cfg.preview_count, 32);
        for (uint32_t base = 0; base < count; base += 8) {
            s32 tokenX = x + 30;
            for (uint32_t j = 0; j < 8 && base + j < count; ++j) {
                const auto p = ff12::Mt19937::Peek(s, base + j);
                std::snprintf(line, sizeof(line), "%02u%s", p.percent,
                              m_tracker.IsSuccessPercent(p.percent) ? "*" : "");
                const auto tokenSize = renderer->drawString(
                    line, false, tokenX, yy, 16,
                    renderer->a(percentColor(p.percent)));
                tokenX += static_cast<s32>(tokenSize.first) + 7;
            }
            yy += 22;
        }

        const uint64_t match = m_tracker.GetNextMatch(count);
        if (match == ff12::INVALID_MATCH)
            drawLine(renderer, "Next configured success: --", x + 10, yy);
        else {
            std::snprintf(line, sizeof(line), "Next configured success: +%llu",
                          static_cast<unsigned long long>(match));
            drawLine(renderer, line, x + 10, yy);
        }
        yy += 25;

        std::snprintf(line, sizeof(line), "Base: %s  Slot:+0x%llX St:+0x%X Mti:+0x%X",
                      m_cfg.rng_base.c_str(),
                      static_cast<unsigned long long>(m_cfg.rng_offset),
                      m_cfg.state_offset, m_cfg.index_offset);
        drawLine(renderer, line, x + 10, yy, 14); yy += 20;

        drawLine(renderer, "RNG lock writes MT19937 state while enabled", x + 10, yy, 14);
    }

private:
    ff12::Config m_cfg{};
    ff12::RngTracker m_tracker{};
    ff12::DmntCheatProcessMetadata m_meta{};
    bool m_cfg_loaded = false;
    std::string m_config_error;
    std::string m_status = "starting";
    uint32_t m_lock_value = 99;
    LockWorker m_worker;
    bool m_worker_started = false;
    Result m_last_rc = 0;
    u64 m_last_tick = 0;
};

class Overlay final : public tsl::Overlay {
public:
    void initServices() override {
        g_sd_mounted = R_SUCCEEDED(fsdevMountSdmc());
        (void)ff12::dmnt::Initialize();
    }

    void exitServices() override {
        if (g_sd_mounted)
            fsdevUnmountDevice("sdmc");
        ff12::dmnt::Exit();
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainGui>();
    }
};

} // namespace

int main(int argc, char** argv) {
    return tsl::loop<Overlay>(argc, argv);
}
