#include "aiws/processing_core.hpp"

#include "aiws/chunker.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/corpus_index.hpp"
#include "aiws/retrieval_engine.hpp"
#include "aiws/text_processor.hpp"

#include <stdexcept>
#include <iterator>
#include <unordered_set>
#include <utility>
using namespace std;

namespace aiws {

struct ProcessingCore::Impl {
    std::vector<Chunk> chunks;
    CorpusIndex index;

    unique_ptr<ChunkingStrategy> chunking;
    unique_ptr<RetrievalStrategy> retrieval;
    unique_ptr<ContextStrategy> context;
};

ProcessingCore::ProcessingCore() : impl_(std::make_unique<Impl>()) {

    impl_->chunking = make_unique<Chunker>(
        ChunkingPolicy{kMaxChunkTokens, kChunkOverlap, kParagraphPreferenceWindow});

    impl_->retrieval = make_unique<RetrievalEngine>();
    impl_->context = make_unique<ContextBuilder>();
}

ProcessingCore::ProcessingCore(std::unique_ptr<ChunkingStrategy> chunking,
                               std::unique_ptr<RetrievalStrategy> retrieval,
                               std::unique_ptr<ContextStrategy> context) : impl_(make_unique<Impl>()) {
    if(!chunking || !retrieval || !context){
        throw invalid_argument("cannot be null");
    }
    impl_->chunking = move(chunking);
    impl_->retrieval = move(retrieval);
    impl_->context = move(context);
}

ProcessingCore::~ProcessingCore() = default;
ProcessingCore::ProcessingCore(ProcessingCore&&) noexcept = default;
ProcessingCore& ProcessingCore::operator=(ProcessingCore&&) noexcept = default;

std::string ProcessingCore::normalize(const std::string& text) {
    return TextProcessor::normalize(text);
}

void ProcessingCore::rebuild(const Workspace& workspace) {
    std::unordered_set<std::string> document_ids;
    std::vector<Chunk> next_chunks;
    for (std::size_t order = 0; order < workspace.documents().size(); ++order) {
        const auto& doc = workspace.documents()[order];
        if (!document_ids.insert(doc.id()).second) {
            throw std::invalid_argument("duplicate document id: " + doc.id());
        }
        auto produced = impl_->chunking->chunk(doc, order);
        next_chunks.insert(next_chunks.end(),
                           std::make_move_iterator(produced.begin()),
                           std::make_move_iterator(produced.end()));
    }
    CorpusIndex next_index(next_chunks);
    impl_->chunks = std::move(next_chunks);
    impl_->index = std::move(next_index);
}

const std::vector<Chunk>& ProcessingCore::chunks() const noexcept { return impl_->chunks; }
std::size_t ProcessingCore::chunk_count() const noexcept { return impl_->chunks.size(); }

std::size_t ProcessingCore::document_frequency(const std::string& term) const {
    const auto terms = TextProcessor::terms(term);
    if (terms.empty()) return 0;
    if (terms.size() != 1) throw std::invalid_argument("term must normalize to one token");
    return impl_->index.document_frequency(terms.front());
}

std::size_t ProcessingCore::term_frequency(const std::string& term,
                                           const std::string& chunk_id) const {
    const auto terms = TextProcessor::terms(term);
    if (terms.empty()) return 0;
    if (terms.size() != 1) throw std::invalid_argument("term must normalize to one token");
    return impl_->index.term_frequency(terms.front(), chunk_id);
}

std::vector<SearchResult> ProcessingCore::search(const std::string& query, int k) const {
    return impl_->retrieval->search(query, k, impl_->chunks, impl_->index);
}

std::vector<ContextItem> ProcessingCore::build_context(const std::string& query,
                                                       int k,
                                                       std::size_t token_budget) const {
    if (k < 0) throw std::invalid_argument("k must be non-negative");
    if (token_budget == 0) return {};
    return impl_->context->build(search(query, k), token_budget);
}

}  // namespace aiws
