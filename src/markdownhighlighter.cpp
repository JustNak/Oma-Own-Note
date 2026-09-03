#include "markdownhighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {
    rebuildFormats();
}

void MarkdownHighlighter::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setColors(const QString &background, const QString &foreground,
                                    const QString &accent) {
    if (m_customBackground == background && m_customForeground == foreground
            && m_customAccent == accent)
        return;

    m_customBackground = background;
    m_customForeground = foreground;
    m_customAccent = accent;
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setSearch(const QString &query, int currentMatchStart) {
    if (m_searchQuery == query && m_currentMatchStart == currentMatchStart)
        return;
    m_searchQuery = query;
    m_currentMatchStart = currentMatchStart;
    rehighlight();
    // Table rows stay transparent, so rehighlight may not dirty the layout.
    // TableChrome paints find matches and must repaint when the query changes.
    if (QTextDocument *doc = document()) {
        if (QAbstractTextDocumentLayout *layout = doc->documentLayout())
            emit layout->update();
    }
}

void MarkdownHighlighter::rebuildFormats() {
    const QColor marker = m_darkMode ? QColor(QStringLiteral("#4f525a"))
                                     : QColor(QStringLiteral("#aeb1b5"));
    const QColor background = !m_customBackground.isEmpty() ? QColor(m_customBackground)
        : (m_darkMode ? QColor(QStringLiteral("#101010")) : QColor(QStringLiteral("#ffffff")));
    const QColor text = !m_customForeground.isEmpty() ? QColor(m_customForeground)
        : (m_darkMode ? QColor(QStringLiteral("#eeeeee")) : QColor(QStringLiteral("#222324")));
    const QColor link = !m_customAccent.isEmpty() ? QColor(m_customAccent)
        : (m_darkMode ? QColor(QStringLiteral("#5584aa")) : QColor(QStringLiteral("#2077b2")));
    const QColor quote = marker;
    const QColor codeBackground = m_darkMode ? QColor(QStringLiteral("#1c1a1a"))
                                             : QColor(QStringLiteral("#f8f8f8"));

    m_markerFormat = QTextCharFormat();
    m_markerFormat.setForeground(marker);

    // A sub-pixel font size combined with a stretch factor used to make these
    // markers occupy (close to) zero space, but that combination deadlocks Qt's
    // font metrics engine on some platforms. Instead, use a normal font size and
    // cancel out its advance width with negative absolute letter-spacing.
    m_hiddenMarkerFormat = QTextCharFormat();
    m_hiddenMarkerFormat.setForeground(background);
    m_hiddenMarkerFormat.setFontPointSize(1.0);

    QFont hiddenFont = document() ? document()->defaultFont() : QFont();
    hiddenFont.setPointSizeF(1.0);
    const qreal charWidth = QFontMetricsF(hiddenFont).horizontalAdvance(QLatin1Char('['));

    m_hiddenMarkerFormat.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    m_hiddenMarkerFormat.setFontLetterSpacing(-charWidth);

    m_headingFormat = QTextCharFormat();
    m_headingFormat.setForeground(text);
    m_headingFormat.setFontWeight(QFont::Bold);

    m_boldFormat = QTextCharFormat();
    m_boldFormat.setFontWeight(QFont::Bold);
    m_boldFormat.setForeground(text);

    m_italicFormat = QTextCharFormat();
    m_italicFormat.setFontItalic(true);
    m_italicFormat.setForeground(text);

    m_codeFormat = QTextCharFormat();
    m_codeFormat.setForeground(text);
    m_codeFormat.setBackground(codeBackground);

    m_quoteFormat = QTextCharFormat();
    m_quoteFormat.setForeground(quote);
    m_quoteFormat.setFontItalic(true);

    m_linkFormat = QTextCharFormat();
    m_linkFormat.setForeground(link);
    m_linkFormat.setFontUnderline(true);

    m_searchFormat = QTextCharFormat();
    m_searchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#725b18"))
                                            : QColor(QStringLiteral("#ffe58a")));
    m_currentSearchFormat = QTextCharFormat();
    m_currentSearchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#b36b20"))
                                                   : QColor(QStringLiteral("#ffad42")));

    // Entire table-row source stays invisible so TableChrome can paint
    // wrapped cell text and hairlines without doubled glyphs. A transparent
    // foreground alone is not enough: TextEdit repaints selected glyphs in
    // selectedTextColor and fills their advance with the selection colour, so
    // the raw pipes would reappear over the overlay whenever a table is
    // selected. Collapse the glyphs to zero advance as well, like the inline
    // markers, so there is nothing for the selection to paint.
    m_tableHiddenSyntaxFormat = QTextCharFormat();
    m_tableHiddenSyntaxFormat.setForeground(Qt::transparent);
    m_tableHiddenSyntaxFormat.setFontPointSize(1.0);
    m_tableHiddenSyntaxFormat.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    m_tableHiddenSyntaxFormat.setFontLetterSpacing(-charWidth);
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    constexpr int fenceState = 1;
    const bool inFence = previousBlockState() == fenceState;
    const QString trimmed = text.trimmed();
    if (isFenceLine(text)) {
        setFormat(0, text.length(), m_markerFormat);
        setCurrentBlockState(inFence ? 0 : fenceState);
        highlightSearch(text);
        return;
    }
    if (inFence) {
        setFormat(0, text.length(), m_codeFormat);
        setCurrentBlockState(fenceState);
        highlightSearch(text);
        return;
    }

    setCurrentBlockState(0);
    if (!text.isEmpty()) {
        highlightMarkers(text);
        highlightTable(text);
        if (!isTableRow(text)
                && (text.contains(QLatin1Char('`')) || text.contains(QLatin1Char('*'))
                    || text.contains(QLatin1Char('_')) || text.contains(QLatin1Char('[')))) {
            highlightInline(text);
        }
    }
    if (!isTableRow(text))
        highlightSearch(text);
}

void MarkdownHighlighter::highlightSearch(const QString &text) {
    if (m_searchQuery.isEmpty())
        return;

    int from = 0;
    while ((from = text.indexOf(m_searchQuery, from, Qt::CaseInsensitive)) >= 0) {
        const int documentStart = currentBlock().position() + from;
        QTextCharFormat format = this->format(from);
        format.setBackground(documentStart == m_currentMatchStart
                                 ? m_currentSearchFormat.background()
                                 : m_searchFormat.background());
        setFormat(from, m_searchQuery.length(), format);
        from += qMax(1, m_searchQuery.length());
    }
}

void MarkdownHighlighter::highlightMarkers(const QString &text) {
    int first = 0;
    while (first < text.length() && text.at(first).isSpace())
        ++first;
    if (first >= text.length())
        return;

    const QChar firstChar = text.at(first);
    if (first == 0 && firstChar == QLatin1Char('#')) {
        static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s+)(.*)$"));
        const QRegularExpressionMatch heading = headingRe.match(text);
        if (heading.hasMatch()) {
            setFormat(0, heading.capturedLength(1) + heading.capturedLength(2),
                      m_markerFormat);
            setFormat(heading.capturedStart(3), heading.capturedLength(3),
                      m_headingFormat);
            return;
        }
    }

    if (firstChar == QLatin1Char('>')) {
        static const QRegularExpression quoteRe(QStringLiteral("^(\\s*>+\\s?)(.*)$"));
        const QRegularExpressionMatch quote = quoteRe.match(text);
        if (quote.hasMatch()) {
            setFormat(0, quote.capturedLength(1), m_markerFormat);
            setFormat(quote.capturedStart(2), quote.capturedLength(2), m_quoteFormat);
        }
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('+')
            || firstChar == QLatin1Char('*') || firstChar.isDigit()) {
        static const QRegularExpression listRe(
            QStringLiteral("^(\\s*(?:[-+*]|\\d+[.)])\\s+)(.*)$"));
        const QRegularExpressionMatch list = listRe.match(text);
        if (list.hasMatch()) {
            setFormat(0, list.capturedLength(1), m_markerFormat);
            static const QRegularExpression taskMark(QStringLiteral("^\\[[ xX]\\]"));
            const QRegularExpressionMatch task = taskMark.match(list.captured(2));
            if (task.hasMatch())
                setFormat(list.capturedStart(2), task.capturedLength(), m_markerFormat);
        }
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('*')
            || firstChar == QLatin1Char('_')) {
        static const QRegularExpression ruleRe(QStringLiteral("^\\s{0,3}([-*_])(?:\\s*\\1){2,}\\s*$"));
        const QRegularExpressionMatch rule = ruleRe.match(text);
        if (rule.hasMatch())
            setFormat(0, text.length(), m_markerFormat);
    }
}

bool MarkdownHighlighter::isFenceLine(const QString &text) {
    return text.trimmed().startsWith(QLatin1String("```"));
}

bool MarkdownHighlighter::isTableRow(const QString &text) {
    const QString trimmed = text.trimmed();
    return trimmed.startsWith(QLatin1Char('|')) && trimmed.indexOf(QLatin1Char('|'), 1) >= 0;
}

qreal MarkdownHighlighter::tableDataRowLineHeight(const QFont &font) {
    return qMax(qreal(1), QFontMetricsF(font).lineSpacing() * qreal(1.4));
}

bool MarkdownHighlighter::isTableSeparator(const QString &text) {
    const QString trimmed = text.trimmed();
    if (!isTableRow(trimmed))
        return false;

    bool sawDash = false;
    for (const QChar character : trimmed) {
        if (character == QLatin1Char('-')) {
            sawDash = true;
            continue;
        }
        if (character != QLatin1Char('|') && character != QLatin1Char(':')
                && character != QLatin1Char(' ') && character != QLatin1Char('\t'))
            return false;
    }
    return sawDash;
}

MarkdownHighlighter::TableLine MarkdownHighlighter::parseTableLine(const QString &text) {
    TableLine line;
    if (!isTableRow(text))
        return line;

    line.kind = isTableSeparator(text) ? TableLine::Kind::Separator : TableLine::Kind::Body;
    int cellStart = 0;
    for (int i = 0; i <= text.length(); ++i) {
        if (i == text.length() || text.at(i) == QLatin1Char('|')) {
            const bool atPipe = i < text.length();
            // Keep zero-width cells (`||`) so overlay columns match the
            // source grid. Skip only the implicit span before a leading pipe.
            if (i > cellStart || (atPipe && cellStart > 0))
                line.cells.append(Span{cellStart, i - cellStart});
            if (atPipe)
                line.hidden.append(Span{i, 1});
            cellStart = i + 1;
        }
    }
    if (line.kind == TableLine::Kind::Separator)
        line.hidden = {Span{0, int(text.length())}};
    return line;
}

void MarkdownHighlighter::highlightTable(const QString &text) {
    if (!isTableRow(text))
        return;

    // Overlay paints cell text and rules. Keep source glyphs invisible so they
    // cannot sit on top of wrapped cells or hidden pipes.
    setFormat(0, text.length(), m_tableHiddenSyntaxFormat);
}

void MarkdownHighlighter::highlightInline(const QString &text) {
    if (text.contains(QLatin1Char('`'))) {
        static const QRegularExpression codeRe(QStringLiteral("`([^`]+)`"));
        QRegularExpressionMatchIterator codeMatches = codeRe.globalMatch(text);
        while (codeMatches.hasNext()) {
            const QRegularExpressionMatch match = codeMatches.next();
            setFormat(match.capturedStart(0), match.capturedLength(0), m_codeFormat);
        }
    }

    const QList<InlineMarkup> markup = inlineMarkup(text);
    for (const InlineMarkup &item : markup) {
        const QTextCharFormat &contentFormat =
            item.kind == InlineKind::Bold ? m_boldFormat
            : item.kind == InlineKind::Italic ? m_italicFormat
                                              : m_linkFormat;
        setFormat(item.content.start, item.content.length, contentFormat);
        for (const Span &marker : item.markers)
            setFormat(marker.start, marker.length, m_hiddenMarkerFormat);
    }
}

QList<MarkdownHighlighter::InlineMarkup> MarkdownHighlighter::inlineMarkup(const QString &text) {
    QList<InlineMarkup> markup;
    if (!text.contains(QLatin1Char('*')) && !text.contains(QLatin1Char('_'))
            && !text.contains(QLatin1Char('['))) {
        return markup;
    }

    const auto span = [](const QRegularExpressionMatch &match, int group) {
        return Span{int(match.capturedStart(group)), int(match.capturedLength(group))};
    };

    static const QRegularExpression boldRe(QStringLiteral("(\\*\\*|__)(.+?)(\\1)"));
    QRegularExpressionMatchIterator boldMatches = boldRe.globalMatch(text);
    while (boldMatches.hasNext()) {
        const QRegularExpressionMatch match = boldMatches.next();
        markup.append({InlineKind::Bold, span(match, 2),
                       {span(match, 1), span(match, 3)}});
    }

    static const QRegularExpression italicRe(
        QStringLiteral("(?<!\\*)\\*([^*\\n]+)\\*(?!\\*)|(?<!_)_([^_\\n]+)_(?!_)"));
    QRegularExpressionMatchIterator italicMatches = italicRe.globalMatch(text);
    while (italicMatches.hasNext()) {
        const QRegularExpressionMatch match = italicMatches.next();
        const Span whole = span(match, 0);
        const int contentIndex = match.capturedStart(1) >= 0 ? 1 : 2;
        markup.append({InlineKind::Italic, span(match, contentIndex),
                       {{whole.start, 1}, {whole.start + whole.length - 1, 1}}});
    }

    static const QRegularExpression linkRe(
        QStringLiteral("\\[([^\\]]+)\\]\\(((?:\\\\.|[^)])+)\\)"));
    QRegularExpressionMatchIterator linkMatches = linkRe.globalMatch(text);
    while (linkMatches.hasNext()) {
        const QRegularExpressionMatch match = linkMatches.next();
        const Span whole = span(match, 0);
        const Span content = span(match, 1);
        const int contentEnd = content.start + content.length;
        markup.append({InlineKind::Link, content,
                       {{whole.start, 1},
                        {contentEnd, whole.start + whole.length - contentEnd}}});
    }

    return markup;
}
