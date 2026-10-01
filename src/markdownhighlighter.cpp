#include "markdownhighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>

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

namespace {

struct Delimiter {
    int start = 0;
    int length = 0;
    int remaining = 0;
    QChar marker;
    bool canOpen = false;
    bool canClose = false;
};

struct Neighbor {
    bool edge = false;
    QChar character;
};

bool covered(int index, const QList<MarkdownHighlighter::Span> &spans)
{
    for (const MarkdownHighlighter::Span &span : spans) {
        if (index >= span.start && index < span.start + span.length)
            return true;
    }
    return false;
}

bool rangeCovered(int start, int end, const QList<MarkdownHighlighter::Span> &spans)
{
    for (int index = start; index < end; ++index) {
        if (covered(index, spans))
            return true;
    }
    return false;
}

bool escaped(const QString &text, int index)
{
    int slashes = 0;
    for (int cursor = index - 1; cursor >= 0 && text.at(cursor) == QLatin1Char('\\'); --cursor)
        ++slashes;
    return slashes % 2 == 1;
}

Neighbor neighborAt(const QString &text, int index)
{
    if (index < 0 || index >= text.size())
        return {true, {}};
    return {false, text.at(index)};
}

bool isSpace(const Neighbor &neighbor)
{
    return neighbor.edge || neighbor.character.isSpace();
}

bool isPunct(const Neighbor &neighbor)
{
    return !neighbor.edge && neighbor.character.isPunct();
}

bool leftFlanking(const Neighbor &before, const Neighbor &after)
{
    if (isSpace(after))
        return false;
    if (!isPunct(after))
        return true;
    return isSpace(before) || isPunct(before);
}

bool rightFlanking(const Neighbor &before, const Neighbor &after)
{
    if (isSpace(before))
        return false;
    if (!isPunct(before))
        return true;
    return isSpace(after) || isPunct(after);
}

QList<MarkdownHighlighter::Span> codeSpans(const QString &text)
{
    QList<MarkdownHighlighter::Span> spans;
    const int length = int(text.size());
    for (int index = 0; index < length;) {
        if (text.at(index) != QLatin1Char('`') || escaped(text, index)) {
            ++index;
            continue;
        }
        const int open = index;
        while (index < length && text.at(index) == QLatin1Char('`'))
            ++index;
        const int ticks = index - open;
        int close = -1;
        for (int cursor = index; cursor < length;) {
            if (text.at(cursor) != QLatin1Char('`') || escaped(text, cursor)) {
                ++cursor;
                continue;
            }
            const int closeStart = cursor;
            while (cursor < length && text.at(cursor) == QLatin1Char('`'))
                ++cursor;
            if (cursor - closeStart == ticks) {
                close = closeStart;
                index = cursor;
                break;
            }
        }
        if (close < 0)
            continue;
        spans.append({open, index - open});
    }
    return spans;
}

void closeEmphasis(QList<Delimiter> &stack, int closerIndex,
                   QList<MarkdownHighlighter::InlineMarkup> &markup)
{
    Delimiter &closer = stack[closerIndex];
    while (closer.remaining > 0) {
        int openerIndex = -1;
        for (int index = closerIndex - 1; index >= 0; --index) {
            const Delimiter &opener = stack.at(index);
            if (opener.marker != closer.marker || opener.remaining <= 0 || !opener.canOpen)
                continue;
            // A run that can both open and close cannot pair when the sum of
            // the run lengths is a multiple of 3 unless both lengths are too.
            const bool interior = (opener.canOpen && opener.canClose)
                    || (closer.canOpen && closer.canClose);
            const int sum = opener.length + closer.length;
            if (interior && sum % 3 == 0 && opener.length % 3 != 0 && closer.length % 3 != 0)
                continue;
            openerIndex = index;
            break;
        }
        if (openerIndex < 0)
            break;

        Delimiter &opener = stack[openerIndex];
        const int used = opener.remaining >= 2 && closer.remaining >= 2 ? 2 : 1;
        const int openerMarker = opener.start + opener.remaining - used;
        const int closerMarker = closer.start;
        const int contentStart = openerMarker + used;
        const int contentLength = closerMarker - contentStart;
        if (contentLength > 0) {
            const auto kind = used == 2 ? MarkdownHighlighter::InlineKind::Bold
                                        : MarkdownHighlighter::InlineKind::Italic;
            markup.append({kind,
                           {contentStart, contentLength},
                           {{openerMarker, used}, {closerMarker, used}}});
        }
        opener.remaining -= used;
        closer.remaining -= used;
        closer.start += used;
    }
}

} // namespace

void MarkdownHighlighter::highlightInline(const QString &text) {
    const QList<InlineMarkup> markup = inlineMarkup(text);
    QList<InlineMarkup> byLength = markup;
    std::sort(byLength.begin(), byLength.end(), [](const InlineMarkup &left, const InlineMarkup &right) {
        if (left.content.length != right.content.length)
            return left.content.length > right.content.length;
        return left.content.start < right.content.start;
    });

    for (const InlineMarkup &item : byLength) {
        if (item.kind == InlineKind::Code || item.content.length <= 0)
            continue;
        QTextCharFormat format = this->format(item.content.start);
        if (item.kind == InlineKind::Bold)
            format.setFontWeight(QFont::Bold);
        else if (item.kind == InlineKind::Italic)
            format.setFontItalic(true);
        else if (item.kind == InlineKind::Link) {
            format.setForeground(m_linkFormat.foreground());
            format.setFontUnderline(true);
        }
        setFormat(item.content.start, item.content.length, format);
    }

    for (const InlineMarkup &item : markup) {
        if (item.kind == InlineKind::Code) {
            const int from = item.markers[0].start;
            const int to = item.markers[1].start + item.markers[1].length;
            if (to > from)
                setFormat(from, to - from, m_codeFormat);
            continue;
        }
        for (const Span &marker : item.markers) {
            if (marker.length > 0)
                setFormat(marker.start, marker.length, m_hiddenMarkerFormat);
        }
    }
}

QList<MarkdownHighlighter::InlineMarkup> MarkdownHighlighter::inlineMarkup(const QString &text) {
    QList<InlineMarkup> markup;
    const int length = int(text.size());
    if (length == 0)
        return markup;

    const QList<Span> codes = codeSpans(text);
    QList<Delimiter> stack;
    for (int index = 0; index < length;) {
        if (covered(index, codes)) {
            ++index;
            continue;
        }
        if (text.at(index) == QLatin1Char('\\') && index + 1 < length
                && text.at(index + 1).isPunct()) {
            index += 2;
            continue;
        }

        const QChar marker = text.at(index);
        if (marker != QLatin1Char('*') && marker != QLatin1Char('_')) {
            ++index;
            continue;
        }
        const int start = index;
        while (index < length && text.at(index) == marker && !covered(index, codes))
            ++index;
        const int run = index - start;
        const Neighbor before = neighborAt(text, start - 1);
        const Neighbor after = neighborAt(text, index);
        const bool opens = leftFlanking(before, after);
        const bool closes = rightFlanking(before, after);
        bool canOpen = opens;
        bool canClose = closes;
        if (marker == QLatin1Char('_')) {
            if (closes && !isPunct(before))
                canOpen = false;
            if (opens && !isPunct(after))
                canClose = false;
        }
        if (!canOpen && !canClose)
            continue;

        stack.append({start, run, run, marker, canOpen, canClose});
        if (canClose)
            closeEmphasis(stack, int(stack.size()) - 1, markup);
    }

    for (int index = 0; index < length; ++index) {
        if (text.at(index) != QLatin1Char('[') || escaped(text, index) || covered(index, codes))
            continue;
        int cursor = index + 1;
        while (cursor < length && !(text.at(cursor) == QLatin1Char(']') && !escaped(text, cursor)))
            ++cursor;
        if (cursor >= length || cursor == index + 1)
            continue;
        if (cursor + 1 >= length || text.at(cursor + 1) != QLatin1Char('('))
            continue;
        int end = cursor + 2;
        while (end < length && !(text.at(end) == QLatin1Char(')') && !escaped(text, end)))
            ++end;
        if (end >= length || end == cursor + 2)
            continue;
        if (rangeCovered(index, end + 1, codes))
            continue;
        const int contentStart = index + 1;
        const int contentLength = cursor - contentStart;
        markup.append({InlineKind::Link,
                       {contentStart, contentLength},
                       {{index, 1}, {cursor, end - cursor + 1}}});
        index = end;
    }

    for (const Span &code : codes) {
        int ticks = 0;
        while (ticks < code.length && text.at(code.start + ticks) == QLatin1Char('`'))
            ++ticks;
        const int close = code.start + code.length - ticks;
        markup.append({InlineKind::Code,
                       {code.start + ticks, close - (code.start + ticks)},
                       {{code.start, ticks}, {close, ticks}}});
    }

    std::sort(markup.begin(), markup.end(), [](const InlineMarkup &left, const InlineMarkup &right) {
        if (left.content.start != right.content.start)
            return left.content.start < right.content.start;
        return left.markers[0].start < right.markers[0].start;
    });
    return markup;
}
