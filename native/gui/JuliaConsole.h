#pragma once

#include "scripting/JuliaRuntime.h"

#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QWidget>
#include <QStringList>

namespace jlmol {

class ConsoleInput final : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit ConsoleInput(QWidget* parent = nullptr);
signals:
    void executeRequested();
    void previousHistoryRequested();
    void nextHistoryRequested();
protected:
    void keyPressEvent(QKeyEvent* event) override;
};

class JuliaConsole final : public QWidget {
    Q_OBJECT
public:
    explicit JuliaConsole(JuliaRuntime* runtime, QWidget* parent = nullptr);

public slots:
    void execute();
    void clearConsole();

private:
    JuliaRuntime* runtime_{};
    QTextBrowser* output_{};
    ConsoleInput* input_{};
    QStringList history_;
    int historyIndex_{};
};

} // namespace jlmol

