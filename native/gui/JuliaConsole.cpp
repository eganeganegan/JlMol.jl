#include "gui/JuliaConsole.h"

#include <algorithm>
#include <QHBoxLayout>
#include <QFont>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace jlmol {

ConsoleInput::ConsoleInput(QWidget* parent) : QPlainTextEdit(parent) {
    setPlaceholderText(QStringLiteral("Enter Julia code…  (Ctrl+Return to run)"));
    setMaximumBlockCount(1000);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
}

void ConsoleInput::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        event->modifiers().testFlag(Qt::ControlModifier)) {
        emit executeRequested();
        return;
    }
    if (blockCount() == 1 && event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_Up) {
        emit previousHistoryRequested();
        return;
    }
    if (blockCount() == 1 && event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_Down) {
        emit nextHistoryRequested();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

JuliaConsole::JuliaConsole(JuliaRuntime* runtime, QWidget* parent) : QWidget(parent), runtime_(runtime) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(5);
    output_ = new QTextBrowser(this);
    output_->setOpenExternalLinks(false);
    output_->setFontFamily(QStringLiteral("Menlo"));
    input_ = new ConsoleInput(this);
    QFont inputFont(QStringLiteral("Menlo"));
    inputFont.setStyleHint(QFont::Monospace);
    input_->setFont(inputFont);
    input_->setFixedHeight(82);
    auto* controls = new QHBoxLayout;
    auto* prompt = new QLabel(QStringLiteral("julia>"), this);
    auto* run = new QPushButton(QStringLiteral("Run"), this);
    auto* clear = new QPushButton(QStringLiteral("Clear"), this);
    controls->addWidget(prompt);
    controls->addStretch();
    controls->addWidget(clear);
    controls->addWidget(run);
    layout->addWidget(output_, 1);
    layout->addLayout(controls);
    layout->addWidget(input_);
    connect(run, &QPushButton::clicked, this, &JuliaConsole::execute);
    connect(clear, &QPushButton::clicked, this, &JuliaConsole::clearConsole);
    connect(input_, &ConsoleInput::executeRequested, this, &JuliaConsole::execute);
    connect(input_, &ConsoleInput::previousHistoryRequested, this, [this] {
        if (history_.isEmpty()) return;
        historyIndex_ = std::max(0, historyIndex_ - 1);
        input_->setPlainText(history_.at(historyIndex_));
        input_->moveCursor(QTextCursor::End);
    });
    connect(input_, &ConsoleInput::nextHistoryRequested, this, [this] {
        if (history_.isEmpty()) return;
        historyIndex_ = std::min(static_cast<int>(history_.size()), historyIndex_ + 1);
        input_->setPlainText(historyIndex_ == history_.size() ? QString{} : history_.at(historyIndex_));
        input_->moveCursor(QTextCursor::End);
    });

    if (runtime_ && runtime_->isAvailable()) {
        output_->append(QStringLiteral("<span style='color:#65d18a'>Julia %1 ready.</span>").arg(runtime_->version()));
        output_->append(QStringLiteral("<span style='color:#9ba7bb'>Use <b>using JlMol</b> to control the active scene.</span>"));
    } else {
        output_->append(QStringLiteral("<span style='color:#e58282'>Embedded Julia is unavailable in this build.</span>"));
        input_->setEnabled(false);
        run->setEnabled(false);
    }
}

void JuliaConsole::execute() {
    const QString source = input_->toPlainText();
    if (source.trimmed().isEmpty() || !runtime_) return;
    history_.append(source);
    historyIndex_ = history_.size();
    output_->append(QStringLiteral("<span style='color:#90b8f8'>julia&gt;</span> <pre style='margin:2px'>%1</pre>")
                        .arg(source.toHtmlEscaped()));
    const QString result = runtime_->evaluate(source);
    if (!result.isEmpty()) {
        output_->append(QStringLiteral("<pre style='margin:2px;color:#d8dee9'>%1</pre>").arg(result.toHtmlEscaped()));
    }
    input_->clear();
    input_->setFocus();
}

void JuliaConsole::clearConsole() { output_->clear(); }

} // namespace jlmol
