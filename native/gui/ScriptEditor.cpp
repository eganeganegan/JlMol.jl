#include "gui/ScriptEditor.h"

#include <algorithm>
#include <QAbstractTextDocumentLayout>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include <vector>

namespace jlmol {
namespace {

class JuliaHighlighter final : public QSyntaxHighlighter {
public:
    explicit JuliaHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {
        QTextCharFormat keyword;
        keyword.setForeground(QColor(QStringLiteral("#c678dd")));
        keyword.setFontWeight(QFont::DemiBold);
        const QStringList keywords = {
            QStringLiteral("baremodule"), QStringLiteral("begin"), QStringLiteral("break"), QStringLiteral("catch"),
            QStringLiteral("const"), QStringLiteral("continue"), QStringLiteral("do"), QStringLiteral("else"),
            QStringLiteral("elseif"), QStringLiteral("end"), QStringLiteral("export"), QStringLiteral("false"),
            QStringLiteral("finally"), QStringLiteral("for"), QStringLiteral("function"), QStringLiteral("global"),
            QStringLiteral("if"), QStringLiteral("import"), QStringLiteral("let"), QStringLiteral("local"),
            QStringLiteral("macro"), QStringLiteral("module"), QStringLiteral("mutable"), QStringLiteral("primitive"),
            QStringLiteral("quote"), QStringLiteral("return"), QStringLiteral("struct"), QStringLiteral("true"),
            QStringLiteral("try"), QStringLiteral("using"), QStringLiteral("where"), QStringLiteral("while")};
        rules_.reserve(keywords.size() + 5);
        for (const auto& word : keywords) rules_.push_back({QRegularExpression(QStringLiteral("\\b%1\\b").arg(word)), keyword});

        QTextCharFormat number;
        number.setForeground(QColor(QStringLiteral("#d19a66")));
        rules_.push_back({QRegularExpression(QStringLiteral(R"(\b(?:0x[0-9a-fA-F]+|\d+(?:\.\d+)?(?:[eEfF][+-]?\d+)?)\b)")), number});
        QTextCharFormat string;
        string.setForeground(QColor(QStringLiteral("#98c379")));
        rules_.push_back({QRegularExpression(QStringLiteral(R"("(?:\\.|[^"\\])*")")), string});
        rules_.push_back({QRegularExpression(QStringLiteral(R"('(?:\\.|[^'\\])*')")), string});
        QTextCharFormat macro;
        macro.setForeground(QColor(QStringLiteral("#56b6c2")));
        rules_.push_back({QRegularExpression(QStringLiteral(R"(@[A-Za-z_][A-Za-z0-9_!]*)")), macro});
        comment_.setForeground(QColor(QStringLiteral("#6f7b8a")));
        comment_.setFontItalic(true);
    }

protected:
    void highlightBlock(const QString& text) override {
        for (const auto& rule : rules_) {
            auto matches = rule.pattern.globalMatch(text);
            while (matches.hasNext()) {
                const auto match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
        const auto commentStart = text.indexOf(QLatin1Char('#'));
        if (commentStart >= 0) setFormat(commentStart, text.size() - commentStart, comment_);
    }

private:
    struct Rule { QRegularExpression pattern; QTextCharFormat format; };
    std::vector<Rule> rules_;
    QTextCharFormat comment_;
};

} // namespace

class LineNumberArea final : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), editor_(editor) {}
    [[nodiscard]] QSize sizeHint() const override { return QSize(editor_->lineNumberAreaWidth(), 0); }
protected:
    void paintEvent(QPaintEvent* event) override { editor_->paintLineNumbers(event); }
private:
    CodeEditor* editor_;
};

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent), lineNumberArea_(new LineNumberArea(this)) {
    QFont font(QStringLiteral("Menlo"));
    font.setStyleHint(QFont::Monospace);
    setFont(font);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    new JuliaHighlighter(document());
    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth() const {
    int digits = 1;
    for (int blocks = std::max(1, blockCount()); blocks >= 10; blocks /= 10) ++digits;
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth() { setViewportMargins(lineNumberAreaWidth(), 0, 0, 0); }

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy) lineNumberArea_->scroll(0, dy);
    else lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const auto contents = contentsRect();
    lineNumberArea_->setGeometry(QRect(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()));
}

void CodeEditor::paintLineNumbers(QPaintEvent* event) {
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), palette().alternateBase());
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(palette().color(QPalette::PlaceholderText));
            painter.drawText(0, top, lineNumberArea_->width() - 5, fontMetrics().height(), Qt::AlignRight,
                             QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void CodeEditor::highlightCurrentLine() {
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(palette().alternateBase().color().lighter(108));
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    setExtraSelections({selection});
}

void CodeEditor::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
        insertPlainText(QStringLiteral("    "));
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

ScriptEditor::ScriptEditor(JuliaRuntime* runtime, QWidget* parent) : QWidget(parent), runtime_(runtime) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* controls = new QHBoxLayout;
    const auto addButton = [this, controls](const QString& text, auto slot) {
        auto* button = new QToolButton(this);
        button->setText(text);
        connect(button, &QToolButton::clicked, this, slot);
        controls->addWidget(button);
    };
    addButton(QStringLiteral("New"), &ScriptEditor::newDocument);
    addButton(QStringLiteral("Open"), &ScriptEditor::openDocument);
    addButton(QStringLiteral("Save"), &ScriptEditor::saveDocument);
    controls->addSpacing(10);
    addButton(QStringLiteral("Run Selection"), &ScriptEditor::runSelection);
    addButton(QStringLiteral("Run File"), &ScriptEditor::runScript);
    controls->addStretch();
    layout->addLayout(controls);

    tabs_ = new QTabWidget(this);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &ScriptEditor::closeTab);
    layout->addWidget(tabs_, 1);

    auto* search = new QHBoxLayout;
    findEdit_ = new QLineEdit(this);
    replaceEdit_ = new QLineEdit(this);
    findEdit_->setPlaceholderText(QStringLiteral("Find"));
    replaceEdit_->setPlaceholderText(QStringLiteral("Replace"));
    auto* next = new QPushButton(QStringLiteral("Next"), this);
    auto* replace = new QPushButton(QStringLiteral("Replace"), this);
    auto* replaceAllButton = new QPushButton(QStringLiteral("All"), this);
    search->addWidget(findEdit_, 2);
    search->addWidget(replaceEdit_, 2);
    search->addWidget(next);
    search->addWidget(replace);
    search->addWidget(replaceAllButton);
    layout->addLayout(search);
    connect(findEdit_, &QLineEdit::returnPressed, this, &ScriptEditor::findNext);
    connect(next, &QPushButton::clicked, this, &ScriptEditor::findNext);
    connect(replace, &QPushButton::clicked, this, &ScriptEditor::replaceOne);
    connect(replaceAllButton, &QPushButton::clicked, this, &ScriptEditor::replaceAll);

    output_ = new QTextBrowser(this);
    output_->setMaximumHeight(130);
    output_->setFontFamily(QStringLiteral("Menlo"));
    output_->setPlaceholderText(QStringLiteral("Script output and errors"));
    layout->addWidget(output_);
    newDocument();
}

CodeEditor* ScriptEditor::currentEditor() const { return qobject_cast<CodeEditor*>(tabs_->currentWidget()); }
QString ScriptEditor::currentText() const { return currentEditor() ? currentEditor()->toPlainText() : QString{}; }
QString ScriptEditor::lastOutput() const { return lastOutput_; }
void ScriptEditor::setCurrentText(const QString& source) { if (currentEditor()) currentEditor()->setPlainText(source); }

void ScriptEditor::addEditor(const QString& title, const QString& source, const QString& path) {
    auto* editor = new CodeEditor(tabs_);
    editor->setPlainText(source);
    editor->document()->setModified(false);
    editor->setProperty("filePath", path);
    editor->setProperty("displayTitle", title);
    const int index = tabs_->addTab(editor, title);
    tabs_->setCurrentIndex(index);
    connect(editor->document(), &QTextDocument::modificationChanged, this, [this, editor] { updateTabTitle(editor); });
}

void ScriptEditor::newDocument() { addEditor(QStringLiteral("Untitled %1").arg(untitledCounter_++)); }

bool ScriptEditor::openFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QString source = QString::fromUtf8(file.readAll());
    addEditor(QFileInfo(path).fileName(), source, QFileInfo(path).absoluteFilePath());
    return true;
}

void ScriptEditor::openDocument() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Julia script"), {}, QStringLiteral("Julia scripts (*.jl);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!openFile(path, &error)) QMessageBox::critical(this, QStringLiteral("Open Script"), error);
}

bool ScriptEditor::saveCurrentTo(const QString& path, QString* error) {
    auto* editor = currentEditor();
    if (!editor) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(editor->toPlainText().toUtf8());
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    const auto absolutePath = QFileInfo(path).absoluteFilePath();
    editor->setProperty("filePath", absolutePath);
    editor->setProperty("displayTitle", QFileInfo(path).fileName());
    editor->document()->setModified(false);
    updateTabTitle(editor);
    return true;
}

bool ScriptEditor::saveDocument() {
    auto* editor = currentEditor();
    if (!editor) return false;
    const QString path = editor->property("filePath").toString();
    if (path.isEmpty()) return saveDocumentAs();
    QString error;
    if (!saveCurrentTo(path, &error)) {
        QMessageBox::critical(this, QStringLiteral("Save Script"), error);
        return false;
    }
    return true;
}

bool ScriptEditor::saveDocumentAs() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Julia script"), {}, QStringLiteral("Julia scripts (*.jl);;All files (*)"));
    if (path.isEmpty()) return false;
    QString error;
    if (!saveCurrentTo(path, &error)) {
        QMessageBox::critical(this, QStringLiteral("Save Script"), error);
        return false;
    }
    return true;
}

QString ScriptEditor::evaluate(const QString& source, const QString& label) {
    if (source.trimmed().isEmpty()) return {};
    lastOutput_ = runtime_ ? runtime_->evaluate(source) : QStringLiteral("Julia is not available.");
    output_->append(QStringLiteral("<b>%1</b><pre>%2</pre>").arg(label.toHtmlEscaped(), lastOutput_.toHtmlEscaped()));
    return lastOutput_;
}

QString ScriptEditor::runScript() { return evaluate(currentText(), QStringLiteral("Run file")); }

QString ScriptEditor::runSelection() {
    const auto* editor = currentEditor();
    if (!editor) return {};
    const QString selected = editor->textCursor().selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return evaluate(selected.isEmpty() ? editor->toPlainText() : selected, selected.isEmpty() ? QStringLiteral("Run file") : QStringLiteral("Run selection"));
}

void ScriptEditor::findNext() {
    auto* editor = currentEditor();
    const QString text = findEdit_->text();
    if (!editor || text.isEmpty()) return;
    if (!editor->find(text)) {
        auto cursor = editor->textCursor();
        cursor.movePosition(QTextCursor::Start);
        editor->setTextCursor(cursor);
        editor->find(text);
    }
}

void ScriptEditor::replaceOne() {
    auto* editor = currentEditor();
    if (!editor) return;
    auto cursor = editor->textCursor();
    if (cursor.hasSelection() && cursor.selectedText() == findEdit_->text()) cursor.insertText(replaceEdit_->text());
    findNext();
}

void ScriptEditor::replaceAll() {
    (void)replaceAllText(findEdit_->text(), replaceEdit_->text());
}

int ScriptEditor::replaceAllText(const QString& find, const QString& replacement) {
    auto* editor = currentEditor();
    if (!editor || find.isEmpty()) return 0;
    auto cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::Start);
    editor->setTextCursor(cursor);
    int count = 0;
    while (editor->find(find)) {
        editor->textCursor().insertText(replacement);
        ++count;
    }
    cursor.endEditBlock();
    return count;
}

void ScriptEditor::closeTab(int index) {
    auto* editor = qobject_cast<CodeEditor*>(tabs_->widget(index));
    if (editor && editor->document()->isModified()) {
        const auto choice = QMessageBox::question(this, QStringLiteral("Unsaved Script"), QStringLiteral("Save changes before closing?"),
                                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (choice == QMessageBox::Cancel) return;
        if (choice == QMessageBox::Save) {
            tabs_->setCurrentIndex(index);
            if (!saveDocument()) return;
        }
    }
    tabs_->removeTab(index);
    if (editor) editor->deleteLater();
    if (tabs_->count() == 0) newDocument();
}

void ScriptEditor::updateTabTitle(CodeEditor* editor) {
    const int index = tabs_->indexOf(editor);
    if (index < 0) return;
    QString title = editor->property("displayTitle").toString();
    if (editor->document()->isModified()) title += QLatin1Char('*');
    tabs_->setTabText(index, title);
    tabs_->setTabToolTip(index, editor->property("filePath").toString());
}

bool ScriptEditor::maybeSaveAll() {
    for (int index = 0; index < tabs_->count(); ++index) {
        auto* editor = qobject_cast<CodeEditor*>(tabs_->widget(index));
        if (!editor || !editor->document()->isModified()) continue;
        tabs_->setCurrentIndex(index);
        QString title = tabs_->tabText(index);
        title.remove(QLatin1Char('*'));
        const auto choice = QMessageBox::question(this, QStringLiteral("Unsaved Script"),
                                                  QStringLiteral("Save changes to %1?").arg(title),
                                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (choice == QMessageBox::Cancel) return false;
        if (choice == QMessageBox::Save && !saveDocument()) return false;
    }
    return true;
}

} // namespace jlmol
