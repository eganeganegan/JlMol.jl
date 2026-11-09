#pragma once

#include "scripting/JuliaRuntime.h"

#include <QPlainTextEdit>
#include <QWidget>

class QLineEdit;
class QTabWidget;
class QTextBrowser;

namespace jlmol {

class LineNumberArea;

class CodeEditor final : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);
    [[nodiscard]] int lineNumberAreaWidth() const;
    void paintLineNumbers(QPaintEvent* event);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();

private:
    LineNumberArea* lineNumberArea_{};
};

class ScriptEditor final : public QWidget {
    Q_OBJECT
public:
    explicit ScriptEditor(JuliaRuntime* runtime, QWidget* parent = nullptr);

    [[nodiscard]] CodeEditor* currentEditor() const;
    [[nodiscard]] QString currentText() const;
    [[nodiscard]] QString lastOutput() const;
    void setCurrentText(const QString& source);
    bool openFile(const QString& path, QString* error = nullptr);
    bool saveCurrentTo(const QString& path, QString* error = nullptr);
    int replaceAllText(const QString& find, const QString& replacement);
    bool maybeSaveAll();

public slots:
    void newDocument();
    void openDocument();
    bool saveDocument();
    bool saveDocumentAs();
    QString runScript();
    QString runSelection();
    void findNext();
    void replaceOne();
    void replaceAll();

private:
    void addEditor(const QString& title, const QString& source = {}, const QString& path = {});
    void closeTab(int index);
    void updateTabTitle(CodeEditor* editor);
    [[nodiscard]] QString evaluate(const QString& source, const QString& label);

    JuliaRuntime* runtime_{};
    QTabWidget* tabs_{};
    QLineEdit* findEdit_{};
    QLineEdit* replaceEdit_{};
    QTextBrowser* output_{};
    QString lastOutput_;
    int untitledCounter_{1};
};

} // namespace jlmol
