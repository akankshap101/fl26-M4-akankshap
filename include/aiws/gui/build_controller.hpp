#pragma once

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <atomic>
#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace aiws::gui{

// The GUI owns this controller and is the only caller of start/poll/cancel.
class BuildController{
public:
    struct Result{
        std::size_t version{0};
        std::unique_ptr<ProcessingCore> core;
        std::string error;
        bool cancelled{false};
    };

    BuildController() = default;
    ~BuildController();
    BuildController(const BuildController&) = delete;
    BuildController& operator=(const BuildController&) = delete;

    void start(Workspace snapshot, std::size_t version, int delay_ms);
    void cancel() noexcept;
    bool active() const noexcept;
    std::optional<Result> poll();

private:
    std::atomic_bool cancellation_{false};
    std::thread worker_;
    std::future<Result> future_;
};

} // namespace aiws::gui
