#include "markdownhighlighter.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QSet>
#include <QTextDocument>
#include <QTextLayout>

#include <limits>

namespace {

constexpr int stateNormal = 0;
constexpr int stateFence = 1;
constexpr int stateFenceComment = 2;

class FenceData : public QTextBlockUserData {
public:
    QString language;
};

QSet<QString> keywordsFor(const QString &language) {
    const QString lang = language.toLower();
    const QSet<QString> cLike = {
        QStringLiteral("auto"), QStringLiteral("bool"), QStringLiteral("break"),
        QStringLiteral("case"), QStringLiteral("char"), QStringLiteral("class"),
        QStringLiteral("const"), QStringLiteral("continue"), QStringLiteral("else"),
        QStringLiteral("enum"), QStringLiteral("false"), QStringLiteral("for"),
        QStringLiteral("if"), QStringLiteral("int"), QStringLiteral("long"),
        QStringLiteral("namespace"), QStringLiteral("new"), QStringLiteral("nullptr"),
        QStringLiteral("private"), QStringLiteral("public"), QStringLiteral("return"),
        QStringLiteral("short"), QStringLiteral("sizeof"), QStringLiteral("static"),
        QStringLiteral("struct"), QStringLiteral("switch"), QStringLiteral("this"),
        QStringLiteral("true"), QStringLiteral("void"), QStringLiteral("while"),
    };
    if (lang == QLatin1String("c") || lang == QLatin1String("h")
            || lang == QLatin1String("cpp") || lang == QLatin1String("cc")
            || lang == QLatin1String("cxx") || lang == QLatin1String("hpp")
            || lang == QLatin1String("hh"))
        return cLike;
    if (lang == QLatin1String("js") || lang == QLatin1String("javascript")
            || lang == QLatin1String("jsx") || lang == QLatin1String("ts")
            || lang == QLatin1String("typescript")) {
        return {QStringLiteral("break"), QStringLiteral("case"), QStringLiteral("catch"),
                QStringLiteral("class"), QStringLiteral("const"), QStringLiteral("continue"),
                QStringLiteral("else"), QStringLiteral("export"), QStringLiteral("false"),
                QStringLiteral("for"), QStringLiteral("function"), QStringLiteral("if"),
                QStringLiteral("import"), QStringLiteral("let"), QStringLiteral("new"),
                QStringLiteral("null"), QStringLiteral("return"), QStringLiteral("this"),
                QStringLiteral("true"), QStringLiteral("undefined"), QStringLiteral("var"),
                QStringLiteral("while")};
    }
    if (lang == QLatin1String("py") || lang == QLatin1String("python")) {
        return {QStringLiteral("and"), QStringLiteral("class"), QStringLiteral("def"),
                QStringLiteral("elif"), QStringLiteral("else"), QStringLiteral("False"),
                QStringLiteral("for"), QStringLiteral("from"), QStringLiteral("if"),
                QStringLiteral("import"), QStringLiteral("in"), QStringLiteral("None"),
                QStringLiteral("not"), QStringLiteral("or"), QStringLiteral("return"),
                QStringLiteral("True"), QStringLiteral("while")};
    }
    if (lang == QLatin1String("sh") || lang == QLatin1String("bash")
            || lang == QLatin1String("zsh") || lang == QLatin1String("shell")) {
        return {QStringLiteral("case"), QStringLiteral("do"), QStringLiteral("done"),
                QStringLiteral("elif"), QStringLiteral("else"), QStringLiteral("esac"),
                QStringLiteral("fi"), QStringLiteral("for"), QStringLiteral("function"),
                QStringLiteral("if"), QStringLiteral("in"), QStringLiteral("then"),
                QStringLiteral("while")};
    }
    if (lang == QLatin1String("java")) {
        return {QStringLiteral("abstract"), QStringLiteral("boolean"), QStringLiteral("break"),
                QStringLiteral("case"), QStringLiteral("catch"), QStringLiteral("class"),
                QStringLiteral("continue"), QStringLiteral("else"), QStringLiteral("enum"),
                QStringLiteral("extends"), QStringLiteral("final"), QStringLiteral("for"),
                QStringLiteral("if"), QStringLiteral("implements"), QStringLiteral("import"),
                QStringLiteral("int"), QStringLiteral("interface"), QStringLiteral("new"),
                QStringLiteral("null"), QStringLiteral("package"), QStringLiteral("private"),
                QStringLiteral("protected"), QStringLiteral("public"), QStringLiteral("return"),
                QStringLiteral("static"), QStringLiteral("this"), QStringLiteral("void"),
                QStringLiteral("while")};
    }
    if (lang == QLatin1String("go")) {
        return {QStringLiteral("break"), QStringLiteral("case"), QStringLiteral("chan"),
                QStringLiteral("const"), QStringLiteral("continue"), QStringLiteral("default"),
                QStringLiteral("defer"), QStringLiteral("else"), QStringLiteral("for"),
                QStringLiteral("func"), QStringLiteral("go"), QStringLiteral("if"),
                QStringLiteral("import"), QStringLiteral("interface"), QStringLiteral("map"),
                QStringLiteral("package"), QStringLiteral("range"), QStringLiteral("return"),
                QStringLiteral("struct"), QStringLiteral("switch"), QStringLiteral("type"),
                QStringLiteral("var")};
    }
    if (lang == QLatin1String("rs") || lang == QLatin1String("rust")) {
        return {QStringLiteral("as"), QStringLiteral("async"), QStringLiteral("await"),
                QStringLiteral("break"), QStringLiteral("const"), QStringLiteral("continue"),
                QStringLiteral("else"), QStringLiteral("enum"), QStringLiteral("fn"),
                QStringLiteral("for"), QStringLiteral("if"), QStringLiteral("impl"),
                QStringLiteral("let"), QStringLiteral("loop"), QStringLiteral("match"),
                QStringLiteral("mut"), QStringLiteral("pub"), QStringLiteral("return"),
                QStringLiteral("self"), QStringLiteral("struct"), QStringLiteral("trait"),
                QStringLiteral("type"), QStringLiteral("use"), QStringLiteral("while")};
    }
    return {};
}

bool hashComments(const QString &language) {
    const QString lang = language.toLower();
    return lang == QLatin1String("py") || lang == QLatin1String("python")
        || lang == QLatin1String("sh") || lang == QLatin1String("bash")
        || lang == QLatin1String("zsh") || lang == QLatin1String("shell")
        || lang == QLatin1String("rb") || lang == QLatin1String("ruby")
        || lang == QLatin1String("yaml") || lang == QLatin1String("yml")
        || lang == QLatin1String("toml") || lang == QLatin1String("r")
        || lang == QLatin1String("mermaid");
}

bool slashComments(const QString &language) {
    return !keywordsFor(language).isEmpty() || language.toLower() == QLatin1String("css")
        || language.toLower() == QLatin1String("java")
        || language.toLower() == QLatin1String("go")
        || language.toLower() == QLatin1String("rs")
        || language.toLower() == QLatin1String("rust");
}

bool mermaidComments(const QString &language) {
    return language.compare(QLatin1String("mermaid"), Qt::CaseInsensitive) == 0;
}

qreal measuredTextWidth(const QString &text, const QFont &font) {
    if (text.isEmpty())
        return 0;
    QTextLayout layout(text, font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid())
        line.setLineWidth(1.0e6);
    layout.endLayout();
    return line.isValid() ? line.naturalTextWidth() : 0;
}

QString languageOfFenceLine(const QString &text) {
    QString language = text.trimmed();
    if (language.startsWith(QLatin1String("```")))
        language = language.mid(3).trimmed();
    const int space = language.indexOf(QRegularExpression(QStringLiteral("\\s")));
    if (space >= 0)
        language = language.left(space);
    return language;
}

QString destinationUrl(const QString &raw) {
    QString destination = raw.trimmed();
    if (destination.startsWith(QLatin1Char('<')) && destination.endsWith(QLatin1Char('>'))
            && destination.size() >= 2)
        destination = destination.mid(1, destination.size() - 2).trimmed();
    if (!destination.startsWith(QLatin1Char('<'))) {
        bool quoted = false;
        for (int index = 0; index < destination.size(); ++index) {
            const QChar character = destination.at(index);
            if (character == QLatin1Char('"') || character == QLatin1Char('\''))
                quoted = !quoted;
            if (!quoted && character.isSpace()) {
                destination = destination.left(index);
                break;
            }
        }
    }
    return destination.trimmed();
}

} // namespace

QString MarkdownHighlighter::mutedColor(bool darkMode) {
    return darkMode ? QStringLiteral("#909191") : QStringLiteral("#767676");
}

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
}

void MarkdownHighlighter::setEditingPosition(int position) {
    setEditingRange(position, position, position);
}

bool MarkdownHighlighter::setEditingRange(int cursor, int selectionStart, int selectionEnd) {
    if (m_editingPosition == cursor && m_selectionStart == selectionStart
            && m_selectionEnd == selectionEnd)
        return false;
    const QSet<int> before = activeBlocks();
    m_editingPosition = cursor;
    m_selectionStart = selectionStart;
    m_selectionEnd = selectionEnd;
    const QSet<int> after = activeBlocks();
    if (before == after || !document())
        return false;
    QSet<int> dirty = before;
    dirty.unite(after);
    if (dirty.contains(-1) || dirty.size() > 200
            || dirty.size() * 2 > document()->blockCount()) {
        rehighlight();
        return true;
    }
    for (int number : dirty) {
        const QTextBlock block = document()->findBlockByNumber(number);
        if (block.isValid())
            rehighlightBlock(block);
    }
    return true;
}

void MarkdownHighlighter::ensureStructure() const {
    if (!document())
        return;
    const int revision = document()->revision();
    if (revision == m_structureRevision)
        return;
    m_structureRevision = revision;
    m_fences.clear();
    m_columnCache.clear();
    bool open = false;
    FenceInterval current;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (!isFenceMarker(block.text()))
            continue;
        if (!open) {
            current.start = block.blockNumber();
            current.end = -1;
            current.language = languageOfFenceLine(block.text());
            open = true;
            continue;
        }
        current.end = block.blockNumber();
        m_fences.append(current);
        open = false;
    }
    if (open)
        m_fences.append(current);
}

bool MarkdownHighlighter::blockInFence(const QTextBlock &block) const {
    if (!block.isValid())
        return false;
    ensureStructure();
    const int number = block.blockNumber();
    for (const FenceInterval &fence : m_fences) {
        const int end = fence.end < 0 ? std::numeric_limits<int>::max() : fence.end;
        if (number >= fence.start && number <= end)
            return true;
    }
    return false;
}

QString MarkdownHighlighter::codeLanguage(const QTextBlock &opening) const {
    if (!opening.isValid())
        return {};
    ensureStructure();
    for (const FenceInterval &fence : m_fences) {
        if (fence.start == opening.blockNumber())
            return fence.language;
    }
    return languageOfFenceLine(opening.text());
}

QSet<int> MarkdownHighlighter::activeBlocks() const {
    QSet<int> blocks;
    if (!document() || m_editingPosition < 0)
        return blocks;
    int from = m_editingPosition;
    int to = m_editingPosition;
    if (m_selectionStart >= 0 && m_selectionEnd >= 0) {
        from = qMin(from, qMin(m_selectionStart, m_selectionEnd));
        to = qMax(to, qMax(m_selectionStart, m_selectionEnd));
    }
    if (to - from > 8000) {
        blocks.insert(-1);
        return blocks;
    }
    for (QTextBlock block = document()->findBlock(qMax(0, from));
         block.isValid() && block.position() <= to; block = block.next())
        addActiveGroup(blocks, block);
    return blocks;
}

void MarkdownHighlighter::addActiveGroup(QSet<int> &blocks, const QTextBlock &block) const {
    if (!block.isValid())
        return;
    if (blockInFence(block)) {
        ensureStructure();
        const int number = block.blockNumber();
        for (const FenceInterval &fence : m_fences) {
            const int end = fence.end < 0 ? std::numeric_limits<int>::max() : fence.end;
            if (number < fence.start || number > end)
                continue;
            for (QTextBlock row = document()->findBlockByNumber(fence.start);
                 row.isValid() && (fence.end < 0 || row.blockNumber() <= fence.end);
                 row = row.next())
                blocks.insert(row.blockNumber());
            return;
        }
    }
    if (isTableRow(block.text())) {
        QTextBlock start = block;
        while (start.previous().isValid() && isTableRow(start.previous().text())
               && !blockInFence(start.previous()))
            start = start.previous();
        for (QTextBlock row = start; row.isValid() && isTableRow(row.text()) && !blockInFence(row);
             row = row.next())
            blocks.insert(row.blockNumber());
        return;
    }
    const QString trimmed = block.text().trimmed();
    if (trimmed.startsWith(QLatin1Char('>'))) {
        QTextBlock start = block;
        while (start.previous().isValid()
               && start.previous().text().trimmed().startsWith(QLatin1Char('>'))
               && !blockInFence(start.previous()))
            start = start.previous();
        for (QTextBlock row = start;
             row.isValid() && row.text().trimmed().startsWith(QLatin1Char('>')) && !blockInFence(row);
             row = row.next())
            blocks.insert(row.blockNumber());
        return;
    }
    blocks.insert(block.blockNumber());
}

void MarkdownHighlighter::rebuildFormats() {
    const QColor marker = m_darkMode ? QColor(QStringLiteral("#4f525a"))
                                     : QColor(QStringLiteral("#767676"));
    m_background = !m_customBackground.isEmpty() ? QColor(m_customBackground)
        : (m_darkMode ? QColor(QStringLiteral("#101010")) : QColor(QStringLiteral("#ffffff")));
    const QColor text = !m_customForeground.isEmpty() ? QColor(m_customForeground)
        : (m_darkMode ? QColor(QStringLiteral("#eeeeee")) : QColor(QStringLiteral("#222324")));
    const QColor link = !m_customAccent.isEmpty() ? QColor(m_customAccent)
        : (m_darkMode ? QColor(QStringLiteral("#5584aa")) : QColor(QStringLiteral("#2077b2")));
    const QColor quote = QColor(mutedColor(m_darkMode));
    const QColor codeBackground = m_darkMode ? QColor(QStringLiteral("#1c1a1a"))
                                             : QColor(QStringLiteral("#f8f8f8"));

    m_markerFormat = QTextCharFormat();
    m_markerFormat.setForeground(marker);

    // A sub-pixel font size combined with a stretch factor used to make these
    // markers occupy (close to) zero space, but that combination deadlocks Qt's
    // font metrics engine on some platforms. Instead, use a normal font size and
    // cancel out its advance width with negative absolute letter-spacing.
    m_hiddenMarkerFormat = QTextCharFormat();
    m_hiddenMarkerFormat.setForeground(m_background);
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

    m_strikeFormat = QTextCharFormat();
    m_strikeFormat.setFontStrikeOut(true);
    m_strikeFormat.setForeground(text);

    m_codeFormat = QTextCharFormat();
    m_codeFormat.setForeground(text);
    m_codeFormat.setBackground(codeBackground);
    m_codeFormat.setFontStyleHint(QFont::Monospace);
    m_codeFormat.setFontFixedPitch(true);

    m_quoteFormat = QTextCharFormat();
    m_quoteFormat.setForeground(quote);
    m_quoteFormat.setFontItalic(true);

    m_linkFormat = QTextCharFormat();
    m_linkFormat.setForeground(link);
    m_linkFormat.setFontUnderline(true);

    m_footnoteFormat = QTextCharFormat();
    m_footnoteFormat.setForeground(link);

    m_searchFormat = QTextCharFormat();
    m_searchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#725b18"))
                                            : QColor(QStringLiteral("#ffe58a")));
    m_currentSearchFormat = QTextCharFormat();
    m_currentSearchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#b36b20"))
                                                   : QColor(QStringLiteral("#ffad42")));

    const auto mixToward = [](const QColor &from, const QColor &to, qreal amount) {
        return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount,
                                from.greenF() + (to.greenF() - from.greenF()) * amount,
                                from.blueF() + (to.blueF() - from.blueF()) * amount,
                                from.alphaF() + (to.alphaF() - from.alphaF()) * amount);
    };

    m_tableHeaderFormat = QTextCharFormat();
    m_tableHeaderFormat.setForeground(text);
    m_tableHeaderFormat.setFontWeight(QFont::Bold);
    m_tableHeaderFormat.setBackground(mixToward(m_background, text, 0.14));

    m_tableBodyFormat = QTextCharFormat();
    m_tableBodyFormat.setForeground(text);
    m_tableBodyFormat.setBackground(mixToward(m_background, text, 0.07));

    m_tableRuleFormat = QTextCharFormat();
    m_tableRuleFormat.setForeground(marker);

    m_tableHairlineFormat = QTextCharFormat();
    m_tableHairlineFormat.setForeground(marker);
    m_tableHairlineFormat.setBackground(marker);

    m_keywordFormat = QTextCharFormat();
    m_keywordFormat.setForeground(link);
    m_keywordFormat.setBackground(codeBackground);
    m_keywordFormat.setFontFixedPitch(true);

    m_stringFormat = QTextCharFormat();
    m_stringFormat.setForeground(m_darkMode ? QColor(QStringLiteral("#c4b39a"))
                                           : QColor(QStringLiteral("#7a5b3a")));
    m_stringFormat.setBackground(codeBackground);
    m_stringFormat.setFontFixedPitch(true);

    m_commentFormat = QTextCharFormat();
    m_commentFormat.setForeground(quote);
    m_commentFormat.setBackground(codeBackground);
    m_commentFormat.setFontFixedPitch(true);

    m_numberFormat = QTextCharFormat();
    m_numberFormat.setForeground(m_darkMode ? QColor(QStringLiteral("#8aa8b7"))
                                           : QColor(QStringLiteral("#3d6d86")));
    m_numberFormat.setBackground(codeBackground);
    m_numberFormat.setFontFixedPitch(true);
}

bool MarkdownHighlighter::rangeOverlaps(const QTextBlock &block) const {
    if (m_editingPosition < 0 || !block.isValid())
        return false;
    int from = m_editingPosition;
    int to = m_editingPosition;
    if (m_selectionStart >= 0 && m_selectionEnd >= 0) {
        from = qMin(from, qMin(m_selectionStart, m_selectionEnd));
        to = qMax(to, qMax(m_selectionStart, m_selectionEnd));
    }
    const int start = block.position();
    const int end = start + qMax(0, block.length() - 1);
    return to >= start && from <= end;
}

bool MarkdownHighlighter::cursorInside(const QTextBlock &block) const {
    return rangeOverlaps(block);
}

bool MarkdownHighlighter::revealLineSyntax() const {
    if (m_editingPosition < 0)
        return true;
    return cursorInside(currentBlock());
}

bool MarkdownHighlighter::cursorInsideFence(const QTextBlock &block) const {
    if (m_editingPosition < 0 || !block.isValid())
        return true;
    if (!blockInFence(block))
        return false;
    ensureStructure();
    const int number = block.blockNumber();
    for (const FenceInterval &fence : m_fences) {
        const int end = fence.end < 0 ? std::numeric_limits<int>::max() : fence.end;
        if (number < fence.start || number > end)
            continue;
        for (QTextBlock row = document()->findBlockByNumber(fence.start);
             row.isValid() && (fence.end < 0 || row.blockNumber() <= fence.end); row = row.next()) {
            if (rangeOverlaps(row))
                return true;
        }
        return false;
    }
    return false;
}

QTextCharFormat MarkdownHighlighter::readableMarkerFormat() const {
    QTextCharFormat format = m_markerFormat;
    const QFont font = document() ? document()->defaultFont() : QFont();
    if (font.pixelSize() > 0)
        format.setProperty(QTextFormat::FontPixelSize, font.pixelSize());
    else if (font.pointSizeF() > 0)
        format.setFontPointSize(font.pointSizeF());
    return format;
}

bool MarkdownHighlighter::tableIsRaw(const QTextBlock &block) const {
    if (m_editingPosition < 0 || !block.isValid())
        return true;

    QTextBlock start = block;
    while (start.previous().isValid() && isTableRow(start.previous().text())
           && !blockInFence(start.previous()))
        start = start.previous();
    for (QTextBlock row = start; row.isValid() && isTableRow(row.text()) && !blockInFence(row);
         row = row.next()) {
        if (rangeOverlaps(row))
            return true;
    }
    return false;
}

QTextCharFormat MarkdownHighlighter::invisibleMarkFormat() const {
    QTextCharFormat format;
    format.setForeground(QColor(0, 0, 0, 0));
    format.setFontPointSize(1.0);
    return format;
}

QString MarkdownHighlighter::fenceLanguage(const QTextBlock &opening) const {
    return languageOfFenceLine(opening.text());
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    const int previous = previousBlockState();
    const bool inFence = previous == stateFence || previous == stateFenceComment;
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith(QLatin1String("```"))) {
        const bool showFence = m_editingPosition < 0 || cursorInsideFence(currentBlock());
        setFormat(0, text.length(), showFence ? readableMarkerFormat() : m_hiddenMarkerFormat);
        auto *data = new FenceData;
        data->language = inFence ? QString() : fenceLanguage(currentBlock());
        setCurrentBlockUserData(data);
        setCurrentBlockState(inFence ? stateNormal : stateFence);
        highlightSearch(text);
        return;
    }
    if (inFence) {
        QString language;
        const QTextBlock previousBlock = currentBlock().previous();
        if (const auto *data = dynamic_cast<const FenceData *>(previousBlock.userData()))
            language = data->language;
        auto *data = new FenceData;
        data->language = language;
        setCurrentBlockUserData(data);
        highlightFenceBody(text, language, previous == stateFenceComment);
        highlightSearch(text);
        return;
    }

    setCurrentBlockState(stateNormal);
    // Raw HTML stays ordinary characters. Sanitizing each HTML fragment on its
    // own used to leave an empty element; the source text is what is drawn.
    if (!text.isEmpty()) {
        highlightMarkers(text);
        highlightTable(text);
        if (text.contains(QLatin1Char('`')) || text.contains(QLatin1Char('*'))
                || text.contains(QLatin1Char('_')) || text.contains(QLatin1Char('['))
                || text.contains(QLatin1Char('~')) || text.contains(QLatin1Char('<'))
                || text.contains(QLatin1Char('!')) || text.contains(QLatin1String("http"), Qt::CaseInsensitive)
                || text.contains(QLatin1String("mailto:"), Qt::CaseInsensitive)
                || text.contains(QLatin1String("www."), Qt::CaseInsensitive)) {
            highlightInline(text);
        }
    }
    highlightSearch(text);
}

void MarkdownHighlighter::highlightFenceBody(const QString &text, const QString &language,
                                            bool continuedComment) {
    setFormat(0, text.length(), m_codeFormat);
    enum class Token { Plain, Keyword, String, Comment, Number };
    QVector<Token> kinds(text.size(), Token::Plain);
    const auto mark = [&kinds](int from, int to, Token kind) {
        for (int index = from; index < to && index < kinds.size(); ++index)
            kinds[index] = kind;
    };

    const QSet<QString> keywords = keywordsFor(language);
    const bool slash = slashComments(language);
    const bool hash = hashComments(language);
    const bool mermaid = mermaidComments(language);
    bool inComment = continuedComment;
    bool commentOpen = continuedComment;

    for (int index = 0; index < text.size();) {
        if (inComment) {
            const int end = text.indexOf(QLatin1String("*/"), index);
            if (end < 0) {
                mark(index, text.size(), Token::Comment);
                commentOpen = true;
                break;
            }
            mark(index, end + 2, Token::Comment);
            index = end + 2;
            inComment = false;
            commentOpen = false;
            continue;
        }
        const QChar character = text.at(index);
        if (slash && character == QLatin1Char('/') && index + 1 < text.size()
                && text.at(index + 1) == QLatin1Char('*')) {
            inComment = true;
            continue;
        }
        if (slash && character == QLatin1Char('/') && index + 1 < text.size()
                && text.at(index + 1) == QLatin1Char('/')) {
            mark(index, text.size(), Token::Comment);
            break;
        }
        if (hash && character == QLatin1Char('#')) {
            mark(index, text.size(), Token::Comment);
            break;
        }
        if (mermaid && character == QLatin1Char('%') && index + 1 < text.size()
                && text.at(index + 1) == QLatin1Char('%')) {
            mark(index, text.size(), Token::Comment);
            break;
        }
        if (character == QLatin1Char('"') || character == QLatin1Char('\'')) {
            const QChar quote = character;
            int end = index + 1;
            while (end < text.size()) {
                if (text.at(end) == QLatin1Char('\\') && end + 1 < text.size()) {
                    end += 2;
                    continue;
                }
                if (text.at(end) == quote) {
                    ++end;
                    break;
                }
                ++end;
            }
            mark(index, end, Token::String);
            index = end;
            continue;
        }
        if (character.isDigit()
                && (index == 0 || !(text.at(index - 1).isLetterOrNumber()
                                    || text.at(index - 1) == QLatin1Char('_')))) {
            int end = index + 1;
            while (end < text.size()) {
                const QChar next = text.at(end);
                if (!next.isLetterOrNumber() && next != QLatin1Char('.') && next != QLatin1Char('_'))
                    break;
                ++end;
            }
            mark(index, end, Token::Number);
            index = end;
            continue;
        }
        if (character.isLetter() || character == QLatin1Char('_')) {
            int end = index + 1;
            while (end < text.size()
                   && (text.at(end).isLetterOrNumber() || text.at(end) == QLatin1Char('_')))
                ++end;
            if (keywords.contains(text.mid(index, end - index)))
                mark(index, end, Token::Keyword);
            index = end;
            continue;
        }
        ++index;
    }

    for (int index = 0; index < text.size();) {
        const Token kind = kinds.at(index);
        int end = index + 1;
        while (end < text.size() && kinds.at(end) == kind)
            ++end;
        const QTextCharFormat *format = &m_codeFormat;
        switch (kind) {
        case Token::Plain:
            format = &m_codeFormat;
            break;
        case Token::Keyword:
            format = &m_keywordFormat;
            break;
        case Token::String:
            format = &m_stringFormat;
            break;
        case Token::Comment:
            format = &m_commentFormat;
            break;
        case Token::Number:
            format = &m_numberFormat;
            break;
        }
        setFormat(index, end - index, *format);
        index = end;
    }
    setCurrentBlockState(commentOpen ? stateFenceComment : stateFence);
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
            const int markerLength = heading.capturedLength(1) + heading.capturedLength(2);
            const QTextCharFormat &marker = revealLineSyntax() ? m_markerFormat : m_hiddenMarkerFormat;
            setFormat(0, markerLength, marker);
            QTextCharFormat headingFormat = m_headingFormat;
            const int level = heading.capturedLength(1);
            qreal pixelSize = document() ? document()->defaultFont().pixelSize() : 0;
            if (pixelSize <= 0 && document())
                pixelSize = document()->defaultFont().pointSizeF() * 1.33;
            if (pixelSize > 0) {
                static const qreal scales[] = {1.0, 1.45, 1.30, 1.18, 1.08, 1.0, 1.0};
                const int index = qBound(1, level, 6);
                headingFormat.setProperty(QTextFormat::FontPixelSize, pixelSize * scales[index]);
            }
            setFormat(heading.capturedStart(3), heading.capturedLength(3), headingFormat);
            return;
        }
    }

    if (firstChar == QLatin1Char('>')) {
        static const QRegularExpression quoteRe(QStringLiteral("^(\\s*(?:>\\s*)+)(.*)$"));
        const QRegularExpressionMatch quote = quoteRe.match(text);
        if (quote.hasMatch()) {
            bool reveal = m_editingPosition < 0;
            if (!reveal) {
                QTextBlock row = currentBlock();
                while (row.previous().isValid() && row.previous().text().trimmed().startsWith(QLatin1Char('>')))
                    row = row.previous();
                for (; row.isValid() && row.text().trimmed().startsWith(QLatin1Char('>')); row = row.next()) {
                    if (cursorInside(row)) {
                        reveal = true;
                        break;
                    }
                }
            }
            if (reveal) {
                setFormat(0, quote.capturedLength(1), m_markerFormat);
            } else {
                const QString marker = quote.captured(1);
                const int markerStart = int(quote.capturedStart(1));
                const QTextCharFormat hidden = invisibleMarkFormat();
                for (int index = 0; index < marker.size(); ++index) {
                    const qreal advance = marker.at(index) == QLatin1Char('>') ? quoteBarSlot : 0;
                    setAdvance(text, markerStart + index, advance, hidden);
                }
            }
            setFormat(quote.capturedStart(2), quote.capturedLength(2), m_quoteFormat);
        }
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('+')
            || firstChar == QLatin1Char('*') || firstChar.isDigit()) {
        static const QRegularExpression listRe(
            QStringLiteral("^(\\s*(?:[-+*]|\\d+[.)])\\s+)(.*)$"));
        const QRegularExpressionMatch list = listRe.match(text);
        if (list.hasMatch()) {
            // List and task markers stay at the text size so they share the
            // baseline. Collapsing them with a 1pt font lifts the glyph.
            setFormat(0, list.capturedLength(1), m_markerFormat);
            static const QRegularExpression taskMark(QStringLiteral("^\\[[ xX]\\]"));
            const QRegularExpressionMatch task = taskMark.match(list.captured(2));
            if (task.hasMatch())
                setFormat(list.capturedStart(2), task.capturedLength(), m_markerFormat);
        }
    }

    if (isHorizontalRule(text)) {
        if (revealLineSyntax())
            setFormat(0, text.length(), m_markerFormat);
        else
            setFormat(0, text.length(), m_hiddenMarkerFormat);
    }
}

bool MarkdownHighlighter::isHorizontalRule(const QString &text) {
    static const QRegularExpression ruleRe(QStringLiteral("^\\s{0,3}([-*_])(?:\\s*\\1){2,}\\s*$"));
    return ruleRe.match(text).hasMatch();
}

bool MarkdownHighlighter::isTableRow(const QString &text) {
    const QString trimmed = text.trimmed();
    return trimmed.startsWith(QLatin1Char('|')) && trimmed.indexOf(QLatin1Char('|'), 1) >= 0;
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

bool MarkdownHighlighter::isStandaloneImage(const QString &text) {
    const QString trimmed = text.trimmed();
    const QList<ImageSpan> spans = imageSpans(trimmed);
    return spans.size() == 1 && spans.first().start == 0
        && spans.first().length == trimmed.size();
}

bool MarkdownHighlighter::isFenceMarker(const QString &text) {
    return text.trimmed().startsWith(QLatin1String("```"));
}

bool MarkdownHighlighter::isFencedCodeLine(const QTextBlock &block) {
    if (!block.isValid())
        return false;
    bool inFence = false;
    for (QTextBlock cursor = block.document()->begin(); cursor.isValid() && cursor != block;
         cursor = cursor.next()) {
        if (cursor.text().trimmed().startsWith(QLatin1String("```")))
            inFence = !inFence;
    }
    return inFence || block.text().trimmed().startsWith(QLatin1String("```"));
}

MarkdownHighlighter::TableLine MarkdownHighlighter::parseTableLine(const QString &text) {
    TableLine line;
    if (!isTableRow(text))
        return line;

    line.kind = isTableSeparator(text) ? TableLine::Kind::Separator : TableLine::Kind::Body;
    bool inCode = false;
    int cellStart = 0;
    for (int index = 0; index < text.length(); ++index) {
        const QChar character = text.at(index);
        if (!inCode && character == QLatin1Char('\\') && index + 1 < text.length()) {
            ++index;
            continue;
        }
        if (character == QLatin1Char('`')) {
            inCode = !inCode;
            continue;
        }
        if (!inCode && character == QLatin1Char('|')) {
            if (index > cellStart)
                line.cells.append(Span{cellStart, index - cellStart});
            line.hidden.append(Span{index, 1});
            cellStart = index + 1;
        }
    }
    if (cellStart < text.length())
        line.cells.append(Span{cellStart, int(text.length()) - cellStart});
    if (line.kind == TableLine::Kind::Separator)
        line.hidden = {Span{0, int(text.length())}};
    return line;
}

QVector<MarkdownHighlighter::Column> MarkdownHighlighter::columnsForTable(const QTextBlock &block) const {
    QTextBlock start = block;
    while (start.previous().isValid() && isTableRow(start.previous().text())
           && !blockInFence(start.previous()))
        start = start.previous();
    ensureStructure();
    const auto cached = m_columnCache.constFind(start.blockNumber());
    if (cached != m_columnCache.constEnd())
        return cached.value();

    QVector<Column> columns;
    for (QTextBlock row = start; row.isValid() && isTableRow(row.text()) && !blockInFence(row);
         row = row.next()) {
        const TableLine line = parseTableLine(row.text());
        if (columns.size() < line.cells.size())
            columns.resize(line.cells.size());
        if (line.kind == TableLine::Kind::Separator) {
            for (int index = 0; index < line.cells.size(); ++index) {
                const QString token = row.text().mid(line.cells.at(index).start,
                                                     line.cells.at(index).length).trimmed();
                const bool leftColon = token.startsWith(QLatin1Char(':'));
                const bool rightColon = token.endsWith(QLatin1Char(':'));
                if (leftColon && rightColon)
                    columns[index].align = 1;
                else if (rightColon)
                    columns[index].align = 2;
                else
                    columns[index].align = 0;
            }
            continue;
        }
        const bool header = row.next().isValid() && isTableSeparator(row.next().text());
        QFont font = document() ? document()->defaultFont() : QFont();
        if (header)
            font.setWeight(QFont::Bold);
        for (int index = 0; index < line.cells.size(); ++index) {
            QString cell = row.text().mid(line.cells.at(index).start,
                                          line.cells.at(index).length).trimmed();
            cell.replace(QStringLiteral("\\|"), QStringLiteral("|"));
            columns[index].width = qMax(columns[index].width, measuredTextWidth(cell, font));
        }
    }
    ensureStructure();
    m_columnCache.insert(start.blockNumber(), columns);
    return columns;
}

void MarkdownHighlighter::setAdvance(const QString &text, int index, qreal desired,
                                    const QTextCharFormat &base) {
    if (index < 0 || index >= text.size())
        return;
    QFont font = document() ? document()->defaultFont() : QFont();
    if (base.fontPointSize() > 0)
        font.setPointSizeF(base.fontPointSize());
    else if (base.hasProperty(QTextFormat::FontPixelSize))
        font.setPixelSize(base.intProperty(QTextFormat::FontPixelSize));
    if (base.fontWeight() != QFont::Normal)
        font.setWeight(static_cast<QFont::Weight>(base.fontWeight()));
    if (base.fontItalic())
        font.setItalic(true);
    const qreal natural = measuredTextWidth(QString(text.at(index)), font);
    QTextCharFormat format = base;
    format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    format.setFontLetterSpacing(desired - natural);
    setFormat(index, 1, format);
}

void MarkdownHighlighter::highlightRenderedTable(const QString &text, const QVector<Column> &columns,
                                                bool header) {
    const TableLine line = parseTableLine(text);
    if (line.kind == TableLine::Kind::Separator) {
        for (int index = 0; index < text.size(); ++index)
            setAdvance(text, index, 0, m_hiddenMarkerFormat);
        return;
    }

    // Column fill is painted behind the text. A per-glyph background would
    // leave the padding (carried as letter-spacing) unfilled.
    QTextCharFormat cellFormat = header ? m_tableHeaderFormat : m_tableBodyFormat;
    cellFormat.clearBackground();
    QFont font = document() ? document()->defaultFont() : QFont();
    if (header)
        font.setWeight(QFont::Bold);
    const qreal gutter = qMax(1.0, QFontMetricsF(document() ? document()->defaultFont() : QFont())
                                       .horizontalAdvance(QLatin1Char(' ')));
    constexpr qreal border = 1.0;

    for (int index = 0; index < line.cells.size() && index < columns.size(); ++index) {
        const Span cell = line.cells.at(index);
        const int cellEnd = cell.start + cell.length;
        int contentStart = cell.start;
        while (contentStart < cellEnd && text.at(contentStart).isSpace())
            ++contentStart;
        int contentEnd = cellEnd;
        while (contentEnd > contentStart && text.at(contentEnd - 1).isSpace())
            --contentEnd;
        const int contentLength = contentEnd - contentStart;
        QString visible = text.mid(contentStart, contentLength);
        visible.replace(QStringLiteral("\\|"), QStringLiteral("|"));
        const qreal contentWidth = measuredTextWidth(visible, font);
        const qreal pad = qMax(0.0, columns.at(index).width - contentWidth);
        qreal leftExtra = 0;
        qreal rightExtra = pad;
        if (columns.at(index).align == 2) {
            leftExtra = pad;
            rightExtra = 0;
        } else if (columns.at(index).align == 1) {
            leftExtra = pad / 2;
            rightExtra = pad - leftExtra;
        }
        const qreal leftPad = gutter + leftExtra;
        const qreal rightPad = gutter + rightExtra;
        const int lead = contentStart - cell.start;
        const int trail = cellEnd - contentEnd;
        const bool lastCell = index + 1 == line.cells.size();
        const bool hasTrailingPipe = line.hidden.size() > line.cells.size();
        qreal missing = 0;
        if (lastCell && !hasTrailingPipe) {
            for (int extra = index + 1; extra < columns.size(); ++extra)
                missing += border + gutter + columns.at(extra).width + gutter;
        }

        qreal pipeAdvance = border;
        if (lead == 0)
            pipeAdvance += leftPad + (contentLength == 0 && trail == 0 ? rightPad : 0);
        if (index < line.hidden.size())
            setAdvance(text, line.hidden.at(index).start, pipeAdvance, invisibleMarkFormat());

        if (lead > 0) {
            const qreal leadingAdvance = leftPad
                + (contentLength == 0 && trail == 0 ? rightPad : 0);
            setAdvance(text, cell.start, leadingAdvance, cellFormat);
            for (int space = 1; space < lead; ++space)
                setAdvance(text, cell.start + space, 0, cellFormat);
        }
        for (int character = contentStart; character < contentEnd; ++character) {
            const bool escapedPipe = text.at(character) == QLatin1Char('\\')
                && character + 1 < contentEnd && text.at(character + 1) == QLatin1Char('|');
            if (escapedPipe) {
                setAdvance(text, character, 0, invisibleMarkFormat());
                continue;
            }
            qreal desired = measuredTextWidth(QString(text.at(character)), font);
            if (trail == 0 && character == contentEnd - 1)
                desired += rightPad + missing;
            setAdvance(text, character, desired, cellFormat);
        }
        if (trail > 0) {
            setAdvance(text, contentEnd, rightPad + (lastCell ? missing : 0), cellFormat);
            for (int space = 1; space < trail; ++space)
                setAdvance(text, contentEnd + space, 0, cellFormat);
        }
    }

    if (line.hidden.size() > line.cells.size()) {
        qreal tail = border;
        if (line.cells.size() < columns.size()) {
            for (int extra = line.cells.size(); extra < columns.size(); ++extra)
                tail += gutter + columns.at(extra).width + gutter + border;
        }
        setAdvance(text, line.hidden.constLast().start, tail, invisibleMarkFormat());
    }
}

void MarkdownHighlighter::highlightTable(const QString &text) {
    TableLine line = parseTableLine(text);
    if (line.kind == TableLine::Kind::None)
        return;

    if (isHorizontalRule(text))
        return;

    bool hasSeparator = line.kind == TableLine::Kind::Separator;
    if (!hasSeparator) {
        QTextBlock start = currentBlock();
        while (start.previous().isValid() && isTableRow(start.previous().text())
               && !blockInFence(start.previous()))
            start = start.previous();
        for (QTextBlock row = start; row.isValid() && isTableRow(row.text()) && !blockInFence(row);
             row = row.next()) {
            if (isTableSeparator(row.text())) {
                hasSeparator = true;
                break;
            }
        }
    }

    if (tableIsRaw(currentBlock()) || !hasSeparator) {
        if (line.kind == TableLine::Kind::Separator) {
            // Legacy tests (no caret) keep the hairline chip. A real caret
            // inside the table shows the separator at the document size.
            setFormat(0, text.length(),
                      m_editingPosition < 0 ? m_tableHairlineFormat : readableMarkerFormat());
            return;
        }

        const QTextBlock next = currentBlock().next();
        if (next.isValid() && isTableSeparator(next.text()))
            line.kind = TableLine::Kind::Header;

        const QTextCharFormat &cellFormat = line.kind == TableLine::Kind::Header
            ? m_tableHeaderFormat
            : m_tableBodyFormat;
        for (const Span &cell : line.cells)
            setFormat(cell.start, cell.length, cellFormat);
        for (const Span &hidden : line.hidden)
            setFormat(hidden.start, hidden.length, m_tableRuleFormat);
        return;
    }

    const QTextBlock next = currentBlock().next();
    const bool header = next.isValid() && isTableSeparator(next.text());
    highlightRenderedTable(text, columnsForTable(currentBlock()), header);
}

void MarkdownHighlighter::highlightInline(const QString &text) {
    const bool hideMarkers = m_editingPosition < 0 || !cursorInside(currentBlock());
    const QTextCharFormat &markerFormat = hideMarkers ? m_hiddenMarkerFormat : m_markerFormat;

    if (text.contains(QLatin1Char('`'))) {
        static const QRegularExpression codeRe(QStringLiteral("`([^`]+)`"));
        QRegularExpressionMatchIterator codeMatches = codeRe.globalMatch(text);
        while (codeMatches.hasNext()) {
            const QRegularExpressionMatch match = codeMatches.next();
            setFormat(match.capturedStart(0), match.capturedLength(0), m_codeFormat);
        }
    }

    const QList<ImageSpan> images = imageSpans(text);
    for (const ImageSpan &image : images) {
        if (hideMarkers)
            setFormat(image.start, image.length, m_hiddenMarkerFormat);
    }

    const QList<InlineMarkup> markup = inlineMarkup(text);
    for (const InlineMarkup &item : markup) {
        QTextCharFormat contentFormat;
        switch (item.kind) {
        case InlineKind::Bold:
            contentFormat = m_boldFormat;
            break;
        case InlineKind::Italic:
            contentFormat = m_italicFormat;
            break;
        case InlineKind::Strike:
            contentFormat = m_strikeFormat;
            break;
        case InlineKind::Link:
            contentFormat = m_linkFormat;
            break;
        }
        setFormat(item.content.start, item.content.length, contentFormat);
        for (const Span &marker : item.markers)
            setFormat(marker.start, marker.length, markerFormat);
    }

    static const QRegularExpression footnoteRe(QStringLiteral("\\[\\^[^\\]\\s]+\\]"));
    QRegularExpressionMatchIterator footnotes = footnoteRe.globalMatch(text);
    while (footnotes.hasNext()) {
        const QRegularExpressionMatch match = footnotes.next();
        setFormat(match.capturedStart(0), match.capturedLength(0), m_footnoteFormat);
    }

    static const QRegularExpression autolinkRe(
        QStringLiteral("<((?:https?://|mailto:)[^\\s>]+)>|(?<![<\\w/])((?:https?://|mailto:)[^\\s<]+)|(?<![\\w@])www\\.[^\\s<]+"));
    QRegularExpressionMatchIterator autolinks = autolinkRe.globalMatch(text);
    while (autolinks.hasNext()) {
        const QRegularExpressionMatch match = autolinks.next();
        const int start = int(match.capturedStart(0));
        const int length = int(match.capturedLength(0));
        bool covered = false;
        for (const InlineMarkup &item : markup) {
            if (item.kind != InlineKind::Link)
                continue;
            for (const Span &marker : item.markers) {
                if (start < marker.start + marker.length && start + length > marker.start)
                    covered = true;
            }
        }
        for (const ImageSpan &image : images) {
            if (start < image.start + image.length && start + length > image.start)
                covered = true;
        }
        // A URL inside <em> or <a href> stays source text. The <https://…>
        // autolink is the whole match, so it is not "inside" another tag.
        if (!covered && !match.captured(0).startsWith(QLatin1Char('<'))) {
            const int open = text.lastIndexOf(QLatin1Char('<'), start - 1);
            if (open >= 0) {
                const int close = text.indexOf(QLatin1Char('>'), open + 1);
                if (close >= start)
                    covered = true;
            }
        }
        if (!covered) {
            const bool bracketed = match.captured(0).startsWith(QLatin1Char('<'))
                && match.captured(0).endsWith(QLatin1Char('>')) && length >= 2;
            if (bracketed && hideMarkers) {
                setFormat(start, 1, m_hiddenMarkerFormat);
                setFormat(start + 1, length - 2, m_linkFormat);
                setFormat(start + length - 1, 1, m_hiddenMarkerFormat);
            } else {
                setFormat(start, length, m_linkFormat);
            }
        }
    }
}

QList<MarkdownHighlighter::ImageSpan> MarkdownHighlighter::imageSpans(const QString &text) {
    QList<ImageSpan> spans;
    static const QRegularExpression imageRe(
        QStringLiteral("!\\[([^\\]]*)\\]\\(((?:\\\\.|[^)])*)\\)"));
    QRegularExpressionMatchIterator matches = imageRe.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        ImageSpan span;
        span.start = int(match.capturedStart(0));
        span.length = int(match.capturedLength(0));
        span.alt = match.captured(1);
        span.destination = destinationUrl(match.captured(2));
        spans.append(span);
    }
    return spans;
}

QList<MarkdownHighlighter::InlineMarkup> MarkdownHighlighter::inlineMarkup(const QString &text) {
    QList<InlineMarkup> markup;
    if (!text.contains(QLatin1Char('*')) && !text.contains(QLatin1Char('_'))
            && !text.contains(QLatin1Char('[')) && !text.contains(QLatin1Char('~'))) {
        return markup;
    }

    const auto span = [](const QRegularExpressionMatch &match, int group) {
        return Span{int(match.capturedStart(group)), int(match.capturedLength(group))};
    };
    const QList<ImageSpan> images = imageSpans(text);
    const auto insideImage = [&images](int start) {
        for (const ImageSpan &image : images) {
            if (start >= image.start && start < image.start + image.length)
                return true;
        }
        return false;
    };

    static const QRegularExpression boldRe(QStringLiteral("(\\*\\*|__)(.+?)(\\1)"));
    QRegularExpressionMatchIterator boldMatches = boldRe.globalMatch(text);
    while (boldMatches.hasNext()) {
        const QRegularExpressionMatch match = boldMatches.next();
        if (insideImage(int(match.capturedStart(0))))
            continue;
        markup.append({InlineKind::Bold, span(match, 2),
                       {span(match, 1), span(match, 3)}});
    }

    static const QRegularExpression strikeRe(QStringLiteral("(~~)(.+?)(~~)"));
    QRegularExpressionMatchIterator strikeMatches = strikeRe.globalMatch(text);
    while (strikeMatches.hasNext()) {
        const QRegularExpressionMatch match = strikeMatches.next();
        if (insideImage(int(match.capturedStart(0))))
            continue;
        markup.append({InlineKind::Strike, span(match, 2),
                       {span(match, 1), span(match, 3)}});
    }

    static const QRegularExpression italicRe(
        QStringLiteral("(?<!\\*)\\*([^*\\n]+)\\*(?!\\*)|(?<!_)_([^_\\n]+)_(?!_)"));
    QRegularExpressionMatchIterator italicMatches = italicRe.globalMatch(text);
    while (italicMatches.hasNext()) {
        const QRegularExpressionMatch match = italicMatches.next();
        if (insideImage(int(match.capturedStart(0))))
            continue;
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
        if (whole.start > 0 && text.at(whole.start - 1) == QLatin1Char('!'))
            continue;
        if (insideImage(whole.start))
            continue;
        const Span content = span(match, 1);
        const int contentEnd = content.start + content.length;
        markup.append({InlineKind::Link, content,
                       {{whole.start, 1},
                        {contentEnd, whole.start + whole.length - contentEnd}}});
    }

    return markup;
}
