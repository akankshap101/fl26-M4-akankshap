#include "aiws/gui/search_result_model.hpp"

namespace aiws::gui {

SearchResultModel::SearchResultModel(QObject* parent)
    : QAbstractTableModel(parent) {
}

int SearchResultModel::rowCount(const QModelIndex& parent) const {
    if(parent.isValid()){
        return 0;
    }
    return static_cast<int>(results_.size());
}

int SearchResultModel::columnCount(const QModelIndex& parent) const {
    if(parent.isValid()){  // To suppress unused-parameter warning.
        return 0;
    }
    return 5;
}

QVariant SearchResultModel::data(const QModelIndex& index, int role) const {
    if(!index.isValid() || role != Qt::DisplayRole){
        return {};
    }

    const SearchResult* result = result_at(index.row());

    if(result == nullptr){
        return {};
    }
    switch(index.column()){
    case 0:
        return index.row() + 1;
    case 1:
        return QString::fromStdString(result->document_id);
    case 2:
        return QVariant::fromValue(static_cast<qulonglong>(result->chunk_sequence));
    case 3:
        return result->score;
    case 4:
        return QString::fromStdString(result->text);
    default:
        return {};
    }
}


QVariant SearchResultModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const {
    if(role != Qt::DisplayRole || section < 0){
        return {};
    }

    if(orientation == Qt::Horizontal){
        switch(section){
        case 0:
            return QString("Rank");
        case 1:
            return QString("Document");
        case 2:
            return QString("Chunk");
        case 3:
            return QString("Score");
        case 4:
            return QString("Text");
        default:
            return{};
        }
    }

    if(orientation == Qt::Vertical && section < rowCount()){
        return section + 1;
    }

    return {};
}

void SearchResultModel::set_results(std::vector<SearchResult> results) {
    beginResetModel();
    results_= std::move(results);
    endResetModel();
}

void SearchResultModel::clear() {
    beginResetModel();
    results_.clear();
    endResetModel();
}

const SearchResult* SearchResultModel::result_at(int row) const noexcept {
    if(row < 0 || static_cast<std::size_t>(row) >= results_.size()){
        return nullptr;
    }

    return &results_[static_cast<std::size_t>(row)];
}

}  // namespace aiws::gui
