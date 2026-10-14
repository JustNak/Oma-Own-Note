#pragma once

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QSize>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QVector>

#include "codeblocks.h"

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *document);

    void setDarkMode(bool darkMode);
    void setColors(const QString &background, const QString &foreground, const QString &accent);
    void setSearch(const QString &query, int currentMatchStart);
    // -1 keeps the legacy raw table grid (pipes stay full width). A real caret
    // position renders a table only while the caret and selection are outside it.
    void setEditingPosition(int position);
    bool setEditingRange(int cursor, int selectionStart, int selectionEnd);
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
    bool blockInCode(const QTextBlock &block) const;
    CodeLine codeLine(const QTextBlock &block) const;
    QString codeLanguage(const QTextBlock &opening) const;
    void noteEdit(int position);
    void rehighlightChange(int position, int charsRemoved, int charsAdded);
    bool painting() const { return m_highlightDepth > 0; }
    bool hasPaintedRange() const { return m_paintedAny; }
    int paintedFrom() const { return m_paintedFrom; }
    int paintedTo() const { return m_paintedTo; }
    static QSize fittedImageSize(const QSize &intrinsic);
    static TableLine parseTableLine(const QString &text);

    // Light muted text on white. #aeb1b5 was about 2.15:1; #767676 clears WCAG AA.
    static QString mutedColor(bool darkMode);

    static constexpr qreal tableSeparatorLineHeight = 3;
    static constexpr int imageMaxWidth = 320;
    static constexpr int imageMaxHeight = 480;
    // Remote images have no intrinsic size until they load.
    static constexpr int imageLineHeight = 160;
    static constexpr int imagePlaceholderHeight = 36;
    static constexpr qreal codeBlockInset = 16;
    static constexpr qreal quoteBarSlot = 10;

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
    bool highlightFenceBody(const QString &text, const QString &language, bool continuedComment);
    void highlightRenderedTable(const QString &text, const QVector<Column> &columns, bool header);
    void setAdvance(const QString &text, int index, qreal desired, const QTextCharFormat &base);
    bool cursorInside(const QTextBlock &block) const;
    bool rangeOverlaps(const QTextBlock &block) const;
    bool cursorInsideCode(const QTextBlock &block) const;
    bool revealLineSyntax() const;
    bool tableIsRaw(const QTextBlock &block) const;
    QTextCharFormat readableMarkerFormat() const;
    QTextCharFormat invisibleMarkFormat() const;
    QVector<Column> columnsForTable(const QTextBlock &block) const;
    void notePainted(int blockNumber);
    void refreshBlankCode(const CodeLine &line, const QString &text);
    void flushBlankCode();
    QSet<int> activeBlocks() const;
    void addActiveGroup(QSet<int> &blocks, const QTextBlock &block) const;

    bool m_darkMode = true;
    int m_editingPosition = -1;
    int m_selectionStart = -1;
    int m_selectionEnd = -1;
    mutable QHash<int, QVector<Column>> m_columnCache;
    int m_paintedFrom = 0;
    int m_paintedTo = 0;
    bool m_paintedAny = false;
    bool m_refreshingBlanks = false;
    int m_highlightDepth = 0;
    QList<int> m_pendingBlankBlocks;
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
