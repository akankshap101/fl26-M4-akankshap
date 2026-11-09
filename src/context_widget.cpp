#include "aiws/gui/context_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QString>
#include <QVBoxLayout>

namespace aiws::gui {

ContextWidget::ContextWidget(QWidget* parent) : QWidget(parent) {

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Context", this));
    auto* budget_layout = new QHBoxLayout();
    budget_layout->addWidget(new QLabel("Token budget:", this));
    context_budget_slider_ = new QSlider(Qt::Horizontal, this);
    context_budget_slider_->setRange(50,1000);
    context_budget_slider_->setSingleStep(50);
    context_budget_slider_->setPageStep(50);
    context_budget_slider_->setTickInterval(50);
    budget_value_label_ = new QLabel(this);
    budget_layout->addWidget(context_budget_slider_, 1);
    budget_layout->addWidget(budget_value_label_);
    layout->addLayout(budget_layout);
    build_context_button_ = new QPushButton("Build Context", this);
    layout->addWidget(build_context_button_);
    context_text_ = new QPlainTextEdit(this);
    context_text_->setReadOnly(true);
    layout->addWidget(context_text_, 1);
    connect(context_budget_slider_, &QSlider::valueChanged,this, &ContextWidget::budget_changed);
    connect(build_context_button_, &QPushButton::clicked,this, &ContextWidget::request_context);

    reset();

}

int ContextWidget::token_budget() const{
     return context_budget_slider_->value();
}

void ContextWidget::set_build_context_enabled(bool enabled){
    build_context_button_->setEnabled(enabled);  // To suppress unused-parameter warning.
}

void ContextWidget::set_context(const std::vector<ContextItem>& items){
    QString output;

    for (const ContextItem& item : items){
        output += "Document: ";
        output += QString::fromStdString(item.document_id);
        output += "\nChunk: ";
        output += QString::number(static_cast<qulonglong>(item.chunk_sequence));
        output += "\nTokens: ";
        output += QString::number(static_cast<qulonglong>(item.token_count));
        output += "\nScore: ";
        output += QString::number(item.score, 'g', 12);
        output += "\nTruncated: ";
        output += (item.truncated ? "Yes" : "No");
        output += "\nText: ";
        output += QString::fromStdString(item.text);
        output += "\n\n";
    }
    context_text_->setPlainText(output);
}

void ContextWidget::clear_context(){
    context_text_->clear();
}

void ContextWidget::reset(){
    context_budget_slider_->setValue(300);
    budget_changed(300);
    clear_context();
    set_build_context_enabled(false);
}

void ContextWidget::request_context(){
    emit build_context_requested(token_budget());
}

void ContextWidget::budget_changed(int value){
    budget_value_label_->setText(QString::number(value));
}

}  // namespace aiws::gui
