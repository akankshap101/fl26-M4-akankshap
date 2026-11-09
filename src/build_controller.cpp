#include "aiws/gui/build_controller.hpp"
#include "aiws/chunker.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/retrieval_engine.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace aiws::gui{
namespace{
// Add delay and cancellation without changing ProcessingCore.
class BuildChunker final : public ChunkingStrategy{
public:
    BuildChunker(int delay_ms, const std::atomic_bool& cancelled)
        : delay_ms_(std::max(0, delay_ms)), cancelled_(cancelled),
          chunker_(ChunkingPolicy{ProcessingCore::kMaxChunkTokens, ProcessingCore::kChunkOverlap,
                                  ProcessingCore::kParagraphPreferenceWindow}){
    }

    std::vector<Chunk> chunk(const Document& document, std::size_t order) const override{
        check_cancelled();
        auto chunks = chunker_.chunk(document, order);
        for(int remaining = delay_ms_; remaining > 0;){
            check_cancelled();
            const int slice = std::min(remaining, 10);
            std::this_thread::sleep_for(std::chrono::milliseconds(slice));
            remaining -= slice;
        }
        check_cancelled();
        return chunks;
    }

private:
    void check_cancelled() const{
        if(cancelled_.load()){
            throw std::runtime_error("Build cancelled");
        }
    }
    int delay_ms_;
    const std::atomic_bool& cancelled_;
    Chunker chunker_;
};
}

BuildController::~BuildController(){
    cancel();
    if(worker_.joinable()){
        worker_.join();
    }
}

void BuildController::start(Workspace snapshot, std::size_t version, int delay_ms){
    if(active()){
        throw std::logic_error("A build is already running");
    }
    // Join the previous worker before starting another build.
    if(worker_.joinable()){
        worker_.join();
    }
    cancellation_.store(false);
    std::promise<Result> promise;
    future_ = promise.get_future();
    try{
        worker_ = std::thread([this, snapshot = std::move(snapshot), version, delay_ms,
                               promise = std::move(promise)]() mutable {
            Result result;
            result.version = version;
            try{
                if(cancellation_.load()){
                    result.cancelled = true;
                }else{
                    auto core = std::make_unique<ProcessingCore>(std::make_unique<BuildChunker>(delay_ms, cancellation_),std::make_unique<RetrievalEngine>(), std::make_unique<ContextBuilder>());
                    core->rebuild(snapshot);
                    result.cancelled = cancellation_.load();
                    if(!result.cancelled){
                        result.core = std::move(core);
                    }
                }
            }catch(const std::exception& error){
                result.cancelled = cancellation_.load();
                if(!result.cancelled){
                    result.error = error.what();
                }
            }catch(...){
                result.cancelled = cancellation_.load();
                if(!result.cancelled){
                    result.error = "Unknown build error";
                }
            }
            // Send the result after the worker finishes.
            promise.set_value_at_thread_exit(std::move(result));
        });
    }catch(...){
        future_ = {};
        throw;
    }
}

void BuildController::cancel() noexcept{
    cancellation_.store(true);
}

bool BuildController::active() const noexcept{
    return future_.valid();
}

std::optional<BuildController::Result> BuildController::poll(){
    if(!future_.valid() || future_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready){
        return std::nullopt;
    }
    auto result = future_.get();
    if(worker_.joinable()){
        worker_.join();
    }
    return result;
}

} // namespace aiws::gui
