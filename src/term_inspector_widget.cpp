#include "aiws/gui/term_inspector_widget.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
namespace aiws::gui {

TermInspectorWidget::TermInspectorWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Term Inspector", this));
    auto* input_layout = new QHBoxLayout();
    term_edit_ = new QLineEdit(this);
    term_edit_->setPlaceholderText("Enter a term");
    inspect_button_ = new QPushButton("Inspect", this);
    input_layout->addWidget(term_edit_);
    input_layout->addWidget(inspect_button_);
    layout->addLayout(input_layout);
    auto* statistics_layout = new QFormLayout();
    document_frequency_value_ = new QLabel(this);
    chunk_frequency_value_ = new QLabel(this);
    statistics_layout->addRow("Document frequency (DF):",document_frequency_value_);
    statistics_layout->addRow("Selected-chunk TF:",chunk_frequency_value_);
    layout->addLayout(statistics_layout);
    layout->addStretch();
    connect(inspect_button_, &QPushButton::clicked,this, &TermInspectorWidget::request_inspection);
    reset();
}

void TermInspectorWidget::set_inspection_enabled(bool enabled) {
    inspect_button_->setEnabled(enabled);
}

void TermInspectorWidget::set_selected_chunk(const QString& chunk_id) {
    if(selected_chunk_id_ != chunk_id){
        selected_chunk_id_ = chunk_id;
        chunk_frequency_value_->setText("-");
    }
}

void TermInspectorWidget::set_statistics(std::size_t document_frequency,
                                         bool has_chunk_frequency,
                                         std::size_t chunk_frequency) {
    document_frequency_value_->setText(QString::number(static_cast<qulonglong>(document_frequency)));
    if(has_chunk_frequency){
        chunk_frequency_value_->setText(QString::number(static_cast<qulonglong>(chunk_frequency)));
    }else{
        chunk_frequency_value_->setText("-");
    }
}

void TermInspectorWidget::clear_statistics() {
    document_frequency_value_->setText("-");
    chunk_frequency_value_->setText("-");
}

void TermInspectorWidget::reset() {
    term_edit_->clear();
    selected_chunk_id_.clear();
    clear_statistics();
    set_inspection_enabled(false);
}

void TermInspectorWidget::request_inspection() {
    const QString term = term_edit_->text().trimmed();
    if(term.isEmpty() || !inspect_button_->isEnabled()){
        return;
    }
    emit term_inspection_requested(term, selected_chunk_id_);
}

}  // namespace aiws::gui
