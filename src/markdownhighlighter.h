#pragma once

#include <QList>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QVector>

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *document);

    void setDarkMode(bool darkMode);
    void setColors(const QString &background, const QString &foreground, const QString &accent);
    void setSearch(const QString &query, int currentMatchStart);
    // -1 keeps the legacy raw table grid (pipes stay full width). A real caret
    // position renders a table only while the caret is outside that table.
    void setEditingPosition(int position);
    int editingPosition() const { return m_editingPosition; }

    struct Span {
        int start;
        int length;
    };

    enum class InlineKind { Bold, Italic, Strike, Link };

    struct InlineMarkup {
        InlineKind kind;
        Span content;
        Span markers[2];
    };

    struct ImageSpan {
        int start;
        int length;
        QString alt;
        QString destination;
    };

    // Single source of truth for inline markdown spans: the highlighter uses it
    // to style content and hide markers, and the editor uses it (via
    // Backend::hiddenRangesAt) to skip the caret over the hidden markers.
    static QList<InlineMarkup> inlineMarkup(const QString &text);
    static QList<ImageSpan> imageSpans(const QString &text);

    struct TableLine {
        enum class Kind { None, Header, Separator, Body };
        Kind kind = Kind::None;
        QList<Span> hidden;
        QList<Span> cells;
    };

    static bool isTableRow(const QString &text);
    static bool isTableSeparator(const QString &text);
    static bool isHorizontalRule(const QString &text);
    static bool isStandaloneImage(const QString &text);
    static bool isFencedCodeLine(const QTextBlock &block);
    static TableLine parseTableLine(const QString &text);

    // Light muted text on white. #aeb1b5 was about 2.15:1; #767676 clears WCAG AA.
    static QString mutedColor(bool darkMode);

    static constexpr qreal tableSeparatorLineHeight = 3;
    static constexpr qreal imageLineHeight = 128;

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Column {
        qreal width = 0;
        int align = 0;
    };

    void rebuildFormats();
    void highlightMarkers(const QString &text);
    void highlightInline(const QString &text);
    void highlightSearch(const QString &text);
    void highlightTable(const QString &text);
    void highlightFenceBody(const QString &text, const QString &language, bool continuedComment);
    void highlightRenderedTable(const QString &text, const QVector<Column> &columns, bool header);
    void setAdvance(const QString &text, int index, qreal desired, const QTextCharFormat &base);
    bool cursorInside(const QTextBlock &block) const;
    bool cursorInsideFence(const QTextBlock &block) const;
    bool revealLineSyntax() const;
    bool tableIsRaw(const QTextBlock &block) const;
    QTextCharFormat readableMarkerFormat() const;
    QVector<Column> columnsForTable(const QTextBlock &block) const;
    QString fenceLanguage(const QTextBlock &opening) const;

    bool m_darkMode = true;
    int m_editingPosition = -1;
    QString m_customBackground;
    QString m_customForeground;
    QString m_customAccent;
    QTextCharFormat m_markerFormat;
    QTextCharFormat m_hiddenMarkerFormat;
    QTextCharFormat m_headingFormat;
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_strikeFormat;
    QTextCharFormat m_codeFormat;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_linkFormat;
    QTextCharFormat m_footnoteFormat;
    QString m_searchQuery;
    int m_currentMatchStart = -1;
    QTextCharFormat m_searchFormat;
    QTextCharFormat m_currentSearchFormat;
    QTextCharFormat m_tableHeaderFormat;
    QTextCharFormat m_tableBodyFormat;
    QTextCharFormat m_tableRuleFormat;
    QTextCharFormat m_tableHairlineFormat;
    QTextCharFormat m_keywordFormat;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_numberFormat;
    QColor m_background;
};
