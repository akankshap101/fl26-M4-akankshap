#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/search_result_model.hpp"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QTableView>
#include <QVBoxLayout>
#include <utility>

namespace aiws::gui {

SearchWidget::SearchWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Query / Search", this));
    auto* controls = new QHBoxLayout();
    query_edit_ = new QLineEdit(this);
    query_edit_->setPlaceholderText("Enter a search query");
    result_count_spin_ = new QSpinBox(this);
    result_count_spin_->setRange(1, 100);
    search_button_ = new QPushButton("Search", this);
    controls->addWidget(new QLabel("Query:", this));
    controls->addWidget(query_edit_, 1);
    controls->addWidget(new QLabel("Results (k):", this));
    controls->addWidget(result_count_spin_);
    controls->addWidget(search_button_);
    layout->addLayout(controls);
    result_model_ = new SearchResultModel(this);
    results_view_ = new QTableView(this);
    results_view_->setModel(result_model_);
    results_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(results_view_, 2);
    layout->addWidget(new QLabel("Selected Result", this));
    selected_result_text_ = new QPlainTextEdit(this);
    selected_result_text_->setReadOnly(true);
    layout->addWidget(selected_result_text_, 1);
    view_source_button_ = new QPushButton("View Source", this);
    layout->addWidget(view_source_button_);
    connect(search_button_, &QPushButton::clicked,this, &SearchWidget::request_search);
    connect(query_edit_, &QLineEdit::textChanged,this, &SearchWidget::query_changed);
    connect(results_view_->selectionModel(),&QItemSelectionModel::selectionChanged,this, &SearchWidget::show_selected_result);
    connect(view_source_button_, &QPushButton::clicked,this, &SearchWidget::request_source_document);
    reset();
}

QString SearchWidget::query() const {
    return query_edit_->text();
}

int SearchWidget::result_count() const {
    return result_count_spin_->value();
}

bool SearchWidget::has_query() const {
    return !query().trimmed().isEmpty();
}

void SearchWidget::set_search_enabled(bool enabled) {
    search_button_->setEnabled(enabled);
}

void SearchWidget::set_results(std::vector<SearchResult> results) {
    result_model_->set_results(std::move(results));
    results_view_->selectionModel()->clear();
    show_selected_result();
}

void SearchWidget::clear_results() {
    result_model_->clear();
    results_view_->selectionModel()->clear();
    show_selected_result();
}

void SearchWidget::reset() {
    query_edit_->clear();
    result_count_spin_->setValue(3);
    clear_results();
    set_search_enabled(false);
}

void SearchWidget::request_search() {
    if(!search_button_->isEnabled() || !has_query()){
        return;
}
    emit search_requested(query(), result_count());
}

void SearchWidget::show_selected_result() {
    const auto rows =results_view_->selectionModel()->selectedRows();

    if(rows.isEmpty()){
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }
    const SearchResult* result =result_model_->result_at(rows.first().row());
    if (result == nullptr) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
}
    QString details = "Document: ";
    details += QString::fromStdString(result->document_id);
    details += "\nChunk ID: ";
    details += QString::fromStdString(result->chunk_id);
    details += "\nChunk: ";
    details += QString::number(static_cast<qulonglong>(result->chunk_sequence));
    details += "\nScore: ";
    details += QString::number(result->score, 'g', 12);
    details += "\nMatched terms: ";
    details += QString::number(static_cast<qulonglong>(result->matched_terms));
    details += "\nText: ";
    details += QString::fromStdString(result->text);
    selected_result_text_->setPlainText(details);
    view_source_button_->setEnabled(true);
    emit selected_chunk_changed(QString::fromStdString(result->chunk_id));

}

void SearchWidget::request_source_document() {
    const auto rows =results_view_->selectionModel()->selectedRows();
    if(rows.isEmpty()){
        return;
    }
    const SearchResult* result = result_model_->result_at(rows.first().row());
    if(result != nullptr){
        emit source_document_requested(QString::fromStdString(result->document_id));
    }
}

}  // namespace aiws::gui
