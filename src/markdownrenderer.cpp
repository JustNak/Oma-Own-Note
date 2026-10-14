#include "markdownrenderer.h"

#include "htmlsanitizer.h"

#include "cmark-gfm-core-extensions.h"
#include "cmark-gfm-extension_api.h"
#include "cmark-gfm.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QVariantMap>

namespace {

enum class TokenKind { Plain, Keyword, String, Comment, Number };

QString cssColor(const QString &color, const QString &fallback) {
    static const QRegularExpression hex(
        QStringLiteral("^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    return hex.match(color).hasMatch() ? color : fallback;
}

QString typeOf(cmark_node *node) {
    const char *type = node ? cmark_node_get_type_string(node) : nullptr;
    return type ? QString::fromUtf8(type) : QString();
}

QString literalOf(cmark_node *node) {
    const char *literal = node ? cmark_node_get_literal(node) : nullptr;
    return literal ? QString::fromUtf8(literal) : QString();
}

QString urlOf(cmark_node *node) {
    const char *url = node ? cmark_node_get_url(node) : nullptr;
    return url ? QString::fromUtf8(url) : QString();
}

QString preserveCode(const QString &text) {
    QString preserved;
    preserved.reserve(text.size());
    for (const QChar character : text) {
        if (character == QLatin1Char(' '))
            preserved += QStringLiteral("&nbsp;");
        else if (character == QLatin1Char('\n'))
            preserved += QStringLiteral("<br>");
        else if (character == QLatin1Char('\t'))
            preserved += QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;");
        else
            preserved += HtmlSanitizer::escape(QString(character));
    }
    return preserved;
}

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
        || language.toLower() == QLatin1String("java") || language.toLower() == QLatin1String("go")
        || language.toLower() == QLatin1String("rs") || language.toLower() == QLatin1String("rust");
}

QString tokenColor(TokenKind kind, const MarkdownRenderOptions &options) {
    switch (kind) {
    case TokenKind::Keyword:
        return cssColor(options.accent, options.dark ? QStringLiteral("#5584aa")
                                                      : QStringLiteral("#2077b2"));
    case TokenKind::String:
        return options.dark ? QStringLiteral("#c4b39a") : QStringLiteral("#7a5b3a");
    case TokenKind::Comment:
        return cssColor(options.muted, options.dark ? QStringLiteral("#909191")
                                                     : QStringLiteral("#8b8e93"));
    case TokenKind::Number:
        return options.dark ? QStringLiteral("#8aa8b7") : QStringLiteral("#3d6d86");
    case TokenKind::Plain:
        return cssColor(options.foreground, options.dark ? QStringLiteral("#eeeeee")
                                                          : QStringLiteral("#222324"));
    }
    return cssColor(options.foreground, QStringLiteral("#eeeeee"));
}

QString highlightCode(const QString &code, const QString &language,
                      const MarkdownRenderOptions &options) {
    const QSet<QString> keywords = keywordsFor(language);
    const bool slash = slashComments(language);
    const bool hash = hashComments(language);
    QVector<TokenKind> kinds(code.size(), TokenKind::Plain);
    const auto mark = [&kinds](int from, int to, TokenKind kind) {
        for (int index = from; index < to && index < kinds.size(); ++index)
            kinds[index] = kind;
    };

    for (int index = 0; index < code.size();) {
        const QChar character = code.at(index);
        if (slash && character == QLatin1Char('/') && index + 1 < code.size()
                && code.at(index + 1) == QLatin1Char('*')) {
            int end = code.indexOf(QLatin1String("*/"), index + 2);
            end = end < 0 ? code.size() : end + 2;
            mark(index, end, TokenKind::Comment);
            index = end;
            continue;
        }
        if (slash && character == QLatin1Char('/') && index + 1 < code.size()
                && code.at(index + 1) == QLatin1Char('/')) {
            int end = code.indexOf(QLatin1Char('\n'), index);
            if (end < 0)
                end = code.size();
            mark(index, end, TokenKind::Comment);
            index = end;
            continue;
        }
        if (hash && character == QLatin1Char('#')) {
            int end = code.indexOf(QLatin1Char('\n'), index);
            if (end < 0)
                end = code.size();
            mark(index, end, TokenKind::Comment);
            index = end;
            continue;
        }
        if (character == QLatin1Char('"') || character == QLatin1Char('\'')) {
            const QChar quote = character;
            int end = index + 1;
            while (end < code.size()) {
                if (code.at(end) == QLatin1Char('\\') && end + 1 < code.size()) {
                    end += 2;
                    continue;
                }
                if (code.at(end) == quote) {
                    ++end;
                    break;
                }
                if (code.at(end) == QLatin1Char('\n'))
                    break;
                ++end;
            }
            mark(index, end, TokenKind::String);
            index = end;
            continue;
        }
        if (character.isDigit()
                && (index == 0 || !(code.at(index - 1).isLetterOrNumber()
                                    || code.at(index - 1) == QLatin1Char('_')))) {
            int end = index + 1;
            while (end < code.size()) {
                const QChar next = code.at(end);
                if (!next.isLetterOrNumber() && next != QLatin1Char('.') && next != QLatin1Char('_'))
                    break;
                ++end;
            }
            mark(index, end, TokenKind::Number);
            index = end;
            continue;
        }
        if (character.isLetter() || character == QLatin1Char('_')) {
            int end = index + 1;
            while (end < code.size()
                   && (code.at(end).isLetterOrNumber() || code.at(end) == QLatin1Char('_')))
                ++end;
            if (keywords.contains(code.mid(index, end - index)))
                mark(index, end, TokenKind::Keyword);
            index = end;
            continue;
        }
        ++index;
    }

    QString html;
    for (int index = 0; index < code.size();) {
        const TokenKind kind = kinds.at(index);
        int end = index + 1;
        while (end < code.size() && kinds.at(end) == kind)
            ++end;
        const QString piece = preserveCode(code.mid(index, end - index));
        if (kind == TokenKind::Plain || piece.isEmpty())
            html += piece;
        else
            html += QStringLiteral("<span style=\"color:%1\">").arg(tokenColor(kind, options))
                + piece + QStringLiteral("</span>");
        index = end;
    }
    return html;
}

class DocumentRenderer {
public:
    explicit DocumentRenderer(const MarkdownRenderOptions &options) : m_options(options) {
        m_accent = cssColor(options.accent, options.dark ? QStringLiteral("#5584aa")
                                                          : QStringLiteral("#2077b2"));
        m_codeBackground = cssColor(options.codeBackground,
                                    options.dark ? QStringLiteral("#1c1a1a")
                                                 : QStringLiteral("#f8f8f8"));
        m_linkStyle = QStringLiteral("color:%1;text-decoration:underline").arg(m_accent);
        m_codeStyle = QStringLiteral("background-color:%1").arg(m_codeBackground);
    }

    QVariantList renderDocument(cmark_node *document) {
        collectFootnotes(document);
        QVariantList blocks;
        QVariantList notes;
        for (cmark_node *child = cmark_node_first_child(document); child;
             child = cmark_node_next(child)) {
            if (cmark_node_get_type(child) == CMARK_NODE_FOOTNOTE_DEFINITION) {
                QVariantMap note;
                note.insert(QStringLiteral("id"),
                            QString::number(m_footnotes.value(literalOf(child))));
                note.insert(QStringLiteral("blocks"), renderChildren(child));
                notes.append(note);
                continue;
            }
            const QVariant block = renderBlock(child);
            if (!block.isNull())
                blocks.append(block);
        }
        if (!notes.isEmpty()) {
            QVariantMap footnotes;
            footnotes.insert(QStringLiteral("type"), QStringLiteral("footnotes"));
            footnotes.insert(QStringLiteral("items"), notes);
            blocks.append(footnotes);
        }
        return blocks;
    }

private:
    void collectFootnotes(cmark_node *node) {
        if (!node)
            return;
        if (cmark_node_get_type(node) == CMARK_NODE_FOOTNOTE_DEFINITION) {
            const QString label = literalOf(node);
            if (!m_footnotes.contains(label))
                m_footnotes.insert(label, m_footnotes.size() + 1);
        }
        for (cmark_node *child = cmark_node_first_child(node); child; child = cmark_node_next(child))
            collectFootnotes(child);
    }

    QVariantList renderChildren(cmark_node *node) {
        QVariantList blocks;
        for (cmark_node *child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
            const QVariant block = renderBlock(child);
            if (!block.isNull())
                blocks.append(block);
        }
        return blocks;
    }

    QVariant renderBlock(cmark_node *node) {
        const QString type = typeOf(node);
        if (type == QLatin1String("table"))
            return renderTable(node);

        switch (cmark_node_get_type(node)) {
        case CMARK_NODE_HEADING:
            return QVariantMap{{QStringLiteral("type"), QStringLiteral("heading")},
                               {QStringLiteral("level"), cmark_node_get_heading_level(node)},
                               {QStringLiteral("html"), renderInlines(node)}};
        case CMARK_NODE_PARAGRAPH:
            return renderParagraph(node);
        case CMARK_NODE_BLOCK_QUOTE:
            return QVariantMap{{QStringLiteral("type"), QStringLiteral("quote")},
                               {QStringLiteral("blocks"), renderChildren(node)}};
        case CMARK_NODE_LIST:
            return renderList(node);
        case CMARK_NODE_CODE_BLOCK:
            return renderCode(node);
        case CMARK_NODE_THEMATIC_BREAK:
            return QVariantMap{{QStringLiteral("type"), QStringLiteral("rule")}};
        case CMARK_NODE_HTML_BLOCK: {
            const QString html = HtmlSanitizer::sanitize(literalOf(node), m_options.baseDirectory);
            if (html.isEmpty())
                return {};
            return QVariantMap{{QStringLiteral("type"), QStringLiteral("html")},
                               {QStringLiteral("html"), html}};
        }
        case CMARK_NODE_ITEM:
            return QVariantMap{{QStringLiteral("type"), QStringLiteral("paragraph")},
                               {QStringLiteral("html"), renderInlines(node)}};
        case CMARK_NODE_DOCUMENT:
        case CMARK_NODE_FOOTNOTE_DEFINITION:
            return {};
        default:
            if (cmark_node_first_child(node))
                return QVariantMap{{QStringLiteral("type"), QStringLiteral("quote")},
                                   {QStringLiteral("blocks"), renderChildren(node)}};
            return {};
        }
    }

    QVariant renderParagraph(cmark_node *node) {
        cmark_node *image = nullptr;
        if (paragraphIsImage(node, &image)) {
            QString source;
            if (!HtmlSanitizer::isSafeImageUrl(urlOf(image), &source))
                return QVariantMap{{QStringLiteral("type"), QStringLiteral("paragraph")},
                                   {QStringLiteral("html"), HtmlSanitizer::escape(plainText(image))}};
            source = resolveImage(source);
            if (source.isEmpty())
                return QVariantMap{{QStringLiteral("type"), QStringLiteral("paragraph")},
                                   {QStringLiteral("html"), HtmlSanitizer::escape(plainText(image))}};
            return QVariantMap{{QStringLiteral("type"), QStringLiteral("image")},
                               {QStringLiteral("src"), source},
                               {QStringLiteral("alt"), plainText(image)}};
        }
        const QString html = renderInlines(node);
        if (html.isEmpty())
            return {};
        return QVariantMap{{QStringLiteral("type"), QStringLiteral("paragraph")},
                           {QStringLiteral("html"), html}};
    }

    QVariantMap renderList(cmark_node *node) {
        const bool ordered = cmark_node_get_list_type(node) == CMARK_ORDERED_LIST;
        QVariantList items;
        for (cmark_node *child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
            const bool task = typeOf(child) == QLatin1String("tasklist");
            items.append(QVariantMap{
                {QStringLiteral("task"), task},
                {QStringLiteral("checked"),
                 task && cmark_gfm_extensions_get_tasklist_item_checked(child)},
                {QStringLiteral("blocks"), renderChildren(child)},
            });
        }
        return {{QStringLiteral("type"), QStringLiteral("list")},
                {QStringLiteral("ordered"), ordered},
                {QStringLiteral("start"), ordered ? cmark_node_get_list_start(node) : 1},
                {QStringLiteral("tight"), cmark_node_get_list_tight(node) != 0},
                {QStringLiteral("items"), items}};
    }

    QVariantMap renderCode(cmark_node *node) {
        QString text = literalOf(node);
        if (text.endsWith(QLatin1Char('\n')))
            text.chop(1);
        const char *infoRaw = cmark_node_get_fence_info(node);
        const QString info = QString::fromUtf8(infoRaw ? infoRaw : "").trimmed();
        const QString language = info.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).left(40);
        return {{QStringLiteral("type"), QStringLiteral("code")},
                {QStringLiteral("language"), language},
                {QStringLiteral("text"), text},
                {QStringLiteral("html"), highlightCode(text, language, m_options)}};
    }

    QVariantMap renderTable(cmark_node *node) {
        const int columns = cmark_gfm_extensions_get_table_columns(node);
        const uint8_t *alignments = cmark_gfm_extensions_get_table_alignments(node);
        QVariantList align;
        for (int column = 0; column < columns; ++column) {
            const uint8_t mark = alignments ? alignments[column] : 0;
            align.append(mark == 'c' ? QStringLiteral("center")
                         : mark == 'r' ? QStringLiteral("right")
                                       : QStringLiteral("left"));
        }
        QVariantList header;
        QVariantList rows;
        for (cmark_node *row = cmark_node_first_child(node); row; row = cmark_node_next(row)) {
            QVariantList cells;
            for (cmark_node *cell = cmark_node_first_child(row); cell; cell = cmark_node_next(cell))
                cells.append(renderInlines(cell));
            if (typeOf(row) == QLatin1String("table_header"))
                header = cells;
            else
                rows.append(QVariant(cells));
        }
        return {{QStringLiteral("type"), QStringLiteral("table")},
                {QStringLiteral("align"), align},
                {QStringLiteral("header"), header},
                {QStringLiteral("rows"), rows}};
    }

    bool paragraphIsImage(cmark_node *node, cmark_node **image) const {
        cmark_node *found = nullptr;
        for (cmark_node *child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
            const cmark_node_type type = cmark_node_get_type(child);
            if (type == CMARK_NODE_IMAGE && !found) {
                found = child;
                continue;
            }
            if (type == CMARK_NODE_SOFTBREAK)
                continue;
            if (type == CMARK_NODE_TEXT && literalOf(child).trimmed().isEmpty())
                continue;
            return false;
        }
        if (!found)
            return false;
        *image = found;
        return true;
    }

    QString plainText(cmark_node *node) const {
        QString text;
        for (cmark_node *child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
            const cmark_node_type type = cmark_node_get_type(child);
            if (type == CMARK_NODE_TEXT)
                text += literalOf(child);
            else if (type == CMARK_NODE_SOFTBREAK || type == CMARK_NODE_LINEBREAK)
                text += QLatin1Char(' ');
            else
                text += plainText(child);
        }
        if (text.isEmpty() && cmark_node_get_type(node) == CMARK_NODE_TEXT)
            text = literalOf(node);
        return text;
    }

    QString resolveImage(const QString &url) const {
        const QString scheme = url.section(QLatin1Char(':'), 0, 0).toLower();
        if (scheme == QLatin1String("http") || scheme == QLatin1String("https")
                || scheme == QLatin1String("data") || scheme == QLatin1String("file"))
            return url;
        if (m_options.baseDirectory.isEmpty())
            return url;
        const QString path = QFileInfo(QDir(m_options.baseDirectory).filePath(url)).absoluteFilePath();
        return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
    }

    QString renderInlines(cmark_node *node) {
        QString html;
        for (cmark_node *child = cmark_node_first_child(node); child; child = cmark_node_next(child))
            html += renderInline(child);
        return html;
    }

    QString renderInline(cmark_node *node) {
        const QString type = typeOf(node);
        if (type == QLatin1String("strikethrough"))
            return QStringLiteral("<s>") + renderInlines(node) + QStringLiteral("</s>");

        switch (cmark_node_get_type(node)) {
        case CMARK_NODE_TEXT:
            return HtmlSanitizer::escape(literalOf(node));
        case CMARK_NODE_SOFTBREAK:
            return QStringLiteral(" ");
        case CMARK_NODE_LINEBREAK:
            return QStringLiteral("<br>");
        case CMARK_NODE_CODE:
            return QStringLiteral("<span style=\"%1\">").arg(m_codeStyle)
                + preserveCode(literalOf(node)) + QStringLiteral("</span>");
        case CMARK_NODE_EMPH:
            return QStringLiteral("<i>") + renderInlines(node) + QStringLiteral("</i>");
        case CMARK_NODE_STRONG:
            return QStringLiteral("<b>") + renderInlines(node) + QStringLiteral("</b>");
        case CMARK_NODE_LINK:
            return renderLink(node);
        case CMARK_NODE_IMAGE:
            return renderImage(node);
        case CMARK_NODE_HTML_INLINE:
            return HtmlSanitizer::sanitize(literalOf(node), m_options.baseDirectory);
        case CMARK_NODE_FOOTNOTE_REFERENCE:
            return renderFootnoteRef(node);
        default:
            return renderInlines(node);
        }
    }

    QString renderLink(cmark_node *node) {
        const QString inner = renderInlines(node);
        QString href;
        if (!HtmlSanitizer::isSafeLinkUrl(urlOf(node), &href))
            return inner;
        return QStringLiteral("<a href=\"%1\" rel=\"noopener noreferrer\" target=\"_blank\" style=\"%2\">")
                   .arg(HtmlSanitizer::escape(href), m_linkStyle)
            + inner + QStringLiteral("</a>");
    }

    QString renderImage(cmark_node *node) {
        const QString alt = HtmlSanitizer::escape(plainText(node));
        QString source;
        if (!HtmlSanitizer::isSafeImageUrl(urlOf(node), &source))
            return alt;
        source = resolveImage(source);
        if (source.isEmpty())
            return alt;
        return QStringLiteral("<img src=\"%1\" alt=\"%2\">").arg(HtmlSanitizer::escape(source), alt);
    }

    QString renderFootnoteRef(cmark_node *node) {
        // cmark rewrites a resolved reference's literal to its 1-based index.
        // The definition keeps the author label, which is never copied into HTML.
        const QString id = literalOf(node).trimmed();
        static const QRegularExpression digits(QStringLiteral("^[1-9][0-9]*$"));
        if (!digits.match(id).hasMatch())
            return HtmlSanitizer::escape(id);
        return QStringLiteral("<sup><a href=\"#fn-%1\" rel=\"noopener noreferrer\" style=\"%2\">%3</a></sup>")
            .arg(id, m_linkStyle, id);
    }

    MarkdownRenderOptions m_options;
    QString m_accent;
    QString m_codeBackground;
    QString m_linkStyle;
    QString m_codeStyle;
    QHash<QString, int> m_footnotes;
};

void attachExtensions(cmark_parser *parser) {
    const char *names[] = {"table", "strikethrough", "autolink", "tasklist", "tagfilter"};
    for (const char *name : names) {
        cmark_syntax_extension *extension = cmark_find_syntax_extension(name);
        if (extension)
            cmark_parser_attach_syntax_extension(parser, extension);
    }
}

} // namespace

QVariantList MarkdownRenderer::render(const QString &markdown, const MarkdownRenderOptions &options) {
    cmark_gfm_core_extensions_ensure_registered();
    const int parseOptions = CMARK_OPT_UNSAFE | CMARK_OPT_FOOTNOTES | CMARK_OPT_VALIDATE_UTF8;
    cmark_parser *parser = cmark_parser_new(parseOptions);
    if (!parser)
        return {};
    attachExtensions(parser);
    const QByteArray utf8 = markdown.toUtf8();
    cmark_parser_feed(parser, utf8.constData(), size_t(utf8.size()));
    cmark_node *document = cmark_parser_finish(parser);
    cmark_parser_free(parser);
    if (!document)
        return {};
    const QVariantList blocks = DocumentRenderer(options).renderDocument(document);
    cmark_node_free(document);
    return blocks;
}
