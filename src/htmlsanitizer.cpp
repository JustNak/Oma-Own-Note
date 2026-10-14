#include "htmlsanitizer.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QUrl>

namespace {

constexpr int kMaxDepth = 32;
constexpr int kMaxEntityPasses = 5;

QString namedEntity(const QString &name) {
    static const QHash<QString, QChar> entities = {
        {QStringLiteral("amp"), QLatin1Char('&')},
        {QStringLiteral("lt"), QLatin1Char('<')},
        {QStringLiteral("gt"), QLatin1Char('>')},
        {QStringLiteral("quot"), QLatin1Char('"')},
        {QStringLiteral("apos"), QLatin1Char('\'')},
        {QStringLiteral("nbsp"), QChar(0x00a0)},
        {QStringLiteral("colon"), QLatin1Char(':')},
        {QStringLiteral("semi"), QLatin1Char(';')},
        {QStringLiteral("equals"), QLatin1Char('=')},
        {QStringLiteral("plus"), QLatin1Char('+')},
        {QStringLiteral("sol"), QLatin1Char('/')},
        {QStringLiteral("num"), QLatin1Char('#')},
        {QStringLiteral("quest"), QLatin1Char('?')},
        {QStringLiteral("commat"), QLatin1Char('@')},
        {QStringLiteral("excl"), QLatin1Char('!')},
        {QStringLiteral("Tab"), QLatin1Char('\t')},
        {QStringLiteral("NewLine"), QLatin1Char('\n')},
    };
    return entities.contains(name) ? QString(entities.value(name)) : QString();
}

bool decodeOneEntity(const QString &body, QString *decoded) {
    if (body.isEmpty())
        return false;

    if (body.startsWith(QLatin1Char('#'))) {
        bool ok = false;
        uint code = 0;
        if (body.size() > 1 && (body.at(1) == QLatin1Char('x') || body.at(1) == QLatin1Char('X')))
            code = body.mid(2).toUInt(&ok, 16);
        else
            code = body.mid(1).toUInt(&ok, 10);
        if (!ok || code == 0 || code > 0x10FFFF)
            return false;
        const char32_t codepoint = code;
        *decoded = QString::fromUcs4(&codepoint, 1);
        return true;
    }

    const QString named = namedEntity(body);
    if (named.isEmpty())
        return false;
    *decoded = named;
    return true;
}

QString decodeEntities(QString text) {
    for (int pass = 0; pass < kMaxEntityPasses; ++pass) {
        QString decoded;
        decoded.reserve(text.size());
        bool changed = false;
        for (int i = 0; i < text.size();) {
            if (text.at(i) == QLatin1Char('&')) {
                const int semi = text.indexOf(QLatin1Char(';'), i + 1);
                if (semi > i && semi - i <= 16) {
                    QString entity;
                    if (decodeOneEntity(text.mid(i + 1, semi - i - 1), &entity)) {
                        decoded += entity;
                        i = semi + 1;
                        changed = true;
                        continue;
                    }
                }
            }
            decoded += text.at(i);
            ++i;
        }
        text = decoded;
        if (!changed)
            break;
    }
    return text;
}

bool isNoise(QChar character) {
    const ushort code = character.unicode();
    if (code <= 0x20 || code == 0x7f)
        return true;
    if (code == 0x00ad || code == 0xfeff)
        return true;
    if (code >= 0x200b && code <= 0x200f)
        return true;
    return code == 0x2028 || code == 0x2029 || code == 0x2060;
}

QString stripNoise(const QString &text) {
    QString cleaned;
    cleaned.reserve(text.size());
    for (const QChar character : text) {
        if (!isNoise(character))
            cleaned += character;
    }
    return cleaned;
}

bool isSchemeName(const QString &scheme) {
    if (scheme.isEmpty() || !scheme.at(0).isLetter())
        return false;
    for (const QChar character : scheme) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('+')
                && character != QLatin1Char('-') && character != QLatin1Char('.'))
            return false;
    }
    return true;
}

QString schemeOf(const QString &url) {
    const int colon = url.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return {};
    const QString scheme = url.left(colon);
    if (!isSchemeName(scheme))
        return {};
    return scheme.toLower();
}

bool isSafeDataImage(const QString &url) {
    const QString lower = url.toLower();
    static const QStringList prefixes = {
        QStringLiteral("data:image/png;base64,"),
        QStringLiteral("data:image/jpeg;base64,"),
        QStringLiteral("data:image/jpg;base64,"),
        QStringLiteral("data:image/gif;base64,"),
        QStringLiteral("data:image/webp;base64,"),
    };
    QString payload;
    for (const QString &prefix : prefixes) {
        if (lower.startsWith(prefix)) {
            payload = url.mid(prefix.size());
            break;
        }
    }
    if (payload.isEmpty())
        return false;
    for (const QChar character : payload) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('+')
                && character != QLatin1Char('/') && character != QLatin1Char('='))
            return false;
    }
    return true;
}

QString cleanUrl(const QString &url) {
    QString cleaned = stripNoise(decodeEntities(url.trimmed()));
    if (cleaned.startsWith(QLatin1String("//")))
        cleaned.prepend(QStringLiteral("https:"));
    return cleaned;
}

const QSet<QString> &allowedTags() {
    static const QSet<QString> tags = {
        QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("blockquote"),
        QStringLiteral("br"), QStringLiteral("code"), QStringLiteral("del"),
        QStringLiteral("em"), QStringLiteral("h1"), QStringLiteral("h2"),
        QStringLiteral("h3"), QStringLiteral("h4"), QStringLiteral("h5"),
        QStringLiteral("h6"), QStringLiteral("hr"), QStringLiteral("i"),
        QStringLiteral("img"), QStringLiteral("li"), QStringLiteral("ol"),
        QStringLiteral("p"), QStringLiteral("pre"), QStringLiteral("s"),
        QStringLiteral("span"), QStringLiteral("strong"), QStringLiteral("sub"),
        QStringLiteral("sup"), QStringLiteral("table"), QStringLiteral("tbody"),
        QStringLiteral("td"), QStringLiteral("th"), QStringLiteral("thead"),
        QStringLiteral("tr"), QStringLiteral("u"), QStringLiteral("ul"),
    };
    return tags;
}

const QSet<QString> &voidTags() {
    static const QSet<QString> tags = {
        QStringLiteral("br"), QStringLiteral("hr"), QStringLiteral("img"),
    };
    return tags;
}

const QSet<QString> &droppedTags() {
    static const QSet<QString> tags = {
        QStringLiteral("script"), QStringLiteral("style"), QStringLiteral("iframe"),
        QStringLiteral("frame"), QStringLiteral("frameset"), QStringLiteral("object"),
        QStringLiteral("embed"), QStringLiteral("applet"), QStringLiteral("link"),
        QStringLiteral("meta"), QStringLiteral("base"), QStringLiteral("form"),
        QStringLiteral("svg"), QStringLiteral("math"), QStringLiteral("noscript"),
        QStringLiteral("noembed"), QStringLiteral("noframes"), QStringLiteral("template"),
        QStringLiteral("textarea"), QStringLiteral("title"), QStringLiteral("xmp"),
        QStringLiteral("plaintext"), QStringLiteral("foreignobject"),
    };
    return tags;
}

struct Attribute {
    QString name;
    QString value;
};

struct Tag {
    QString name;
    QList<Attribute> attributes;
    bool closing = false;
    bool selfClosing = false;
    int end = 0;
    bool wellFormed = false;
};

QString readName(const QString &html, int *index) {
    const int start = *index;
    while (*index < html.size()) {
        const QChar character = html.at(*index);
        if (character.isLetterOrNumber() || character == QLatin1Char('-')
                || character == QLatin1Char(':') || character == QLatin1Char('_'))
            ++*index;
        else
            break;
    }
    return html.mid(start, *index - start).toLower();
}

void skipSpace(const QString &html, int *index) {
    while (*index < html.size() && html.at(*index).isSpace())
        ++*index;
}

QString readValue(const QString &html, int *index) {
    if (*index >= html.size())
        return {};
    const QChar quote = html.at(*index);
    if (quote == QLatin1Char('"') || quote == QLatin1Char('\'')) {
        ++*index;
        const int start = *index;
        while (*index < html.size() && html.at(*index) != quote)
            ++*index;
        const QString value = html.mid(start, *index - start);
        if (*index < html.size())
            ++*index;
        return value;
    }
    const int start = *index;
    while (*index < html.size()) {
        const QChar character = html.at(*index);
        if (character.isSpace() || character == QLatin1Char('>') || character == QLatin1Char('/'))
            break;
        ++*index;
    }
    return html.mid(start, *index - start);
}

Tag readTag(const QString &html, int index) {
    Tag tag;
    tag.end = index;
    if (index >= html.size() || html.at(index) != QLatin1Char('<'))
        return tag;
    ++index;
    skipSpace(html, &index);
    if (index < html.size() && html.at(index) == QLatin1Char('/')) {
        tag.closing = true;
        ++index;
        skipSpace(html, &index);
    }
    if (index >= html.size() || !html.at(index).isLetter())
        return tag;
    tag.name = readName(html, &index);
    if (tag.name.isEmpty())
        return tag;

    while (index < html.size()) {
        skipSpace(html, &index);
        if (index >= html.size())
            break;
        if (html.at(index) == QLatin1Char('>')) {
            ++index;
            tag.wellFormed = true;
            break;
        }
        if (html.at(index) == QLatin1Char('/')) {
            tag.selfClosing = true;
            ++index;
            skipSpace(html, &index);
            if (index < html.size() && html.at(index) == QLatin1Char('>')) {
                ++index;
                tag.wellFormed = true;
            }
            break;
        }
        if (!html.at(index).isLetter())
            break;
        Attribute attribute;
        attribute.name = readName(html, &index);
        skipSpace(html, &index);
        if (index < html.size() && html.at(index) == QLatin1Char('=')) {
            ++index;
            skipSpace(html, &index);
            attribute.value = readValue(html, &index);
        }
        if (!attribute.name.isEmpty())
            tag.attributes.append(attribute);
    }
    tag.end = index;
    return tag;
}

int skipRawElement(const QString &html, int index, const QString &name) {
    int depth = 1;
    while (index < html.size() && depth > 0) {
        const int next = html.indexOf(QLatin1Char('<'), index);
        if (next < 0)
            return html.size();
        const Tag tag = readTag(html, next);
        if (!tag.wellFormed || tag.name != name) {
            index = next + 1;
            continue;
        }
        if (tag.closing)
            --depth;
        else if (!tag.selfClosing)
            ++depth;
        index = tag.end;
    }
    return index;
}

QString resolveImageSource(const QString &url, const QString &imageBaseDirectory) {
    const QString scheme = schemeOf(url);
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
            || scheme == QStringLiteral("data") || scheme == QStringLiteral("file"))
        return url;
    if (imageBaseDirectory.isEmpty())
        return url;
    const QString path = QFileInfo(QDir(imageBaseDirectory).filePath(url)).absoluteFilePath();
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

QString safeAttributes(const Tag &tag, const QString &imageBaseDirectory) {
    if (tag.name == QStringLiteral("a")) {
        for (const Attribute &attribute : tag.attributes) {
            if (attribute.name != QStringLiteral("href"))
                continue;
            QString href;
            if (!HtmlSanitizer::isSafeLinkUrl(attribute.value, &href))
                return {};
            return QStringLiteral(" href=\"") + HtmlSanitizer::escape(href)
                + QStringLiteral("\" rel=\"noopener noreferrer\" target=\"_blank\"");
        }
        return {};
    }
    if (tag.name == QStringLiteral("img")) {
        QString source;
        QString alt;
        bool sourceOk = false;
        for (const Attribute &attribute : tag.attributes) {
            if (attribute.name == QStringLiteral("src")) {
                QString normalized;
                if (HtmlSanitizer::isSafeImageUrl(attribute.value, &normalized)) {
                    source = resolveImageSource(normalized, imageBaseDirectory);
                    sourceOk = true;
                }
            } else if (attribute.name == QStringLiteral("alt")) {
                alt = attribute.value;
            }
        }
        if (!sourceOk)
            return QString(QChar(1)) + HtmlSanitizer::escape(alt);
        return QStringLiteral(" src=\"") + HtmlSanitizer::escape(source)
            + QStringLiteral("\" alt=\"") + HtmlSanitizer::escape(alt) + QStringLiteral("\"");
    }
    return {};
}

int parseNodes(const QString &html, int index, const QString &stopTag, const QString &imageBase,
               QString *out, int depth);

int emitTag(const QString &html, const Tag &tag, const QString &imageBase, QString *out, int depth) {
    if (tag.closing)
        return tag.end;
    if (droppedTags().contains(tag.name))
        return tag.selfClosing ? tag.end : skipRawElement(html, tag.end, tag.name);
    if (!allowedTags().contains(tag.name)) {
        if (tag.selfClosing)
            return tag.end;
        return parseNodes(html, tag.end, tag.name, imageBase, out, depth + 1);
    }

    const QString attributes = safeAttributes(tag, imageBase);
    if (attributes.startsWith(QChar(1))) {
        *out += attributes.mid(1);
        return tag.selfClosing || voidTags().contains(tag.name)
            ? tag.end
            : parseNodes(html, tag.end, tag.name, imageBase, out, depth + 1);
    }

    if (tag.name == QStringLiteral("a") && attributes.isEmpty())
        return tag.selfClosing ? tag.end : parseNodes(html, tag.end, tag.name, imageBase, out, depth + 1);

    if (voidTags().contains(tag.name) || tag.selfClosing) {
        *out += QLatin1Char('<') + tag.name + attributes + QStringLiteral(">");
        return tag.end;
    }

    *out += QLatin1Char('<') + tag.name + attributes + QStringLiteral(">");
    const int after = parseNodes(html, tag.end, tag.name, imageBase, out, depth + 1);
    *out += QStringLiteral("</") + tag.name + QStringLiteral(">");
    return after;
}

int parseNodes(const QString &html, int index, const QString &stopTag, const QString &imageBase,
               QString *out, int depth) {
    if (depth > kMaxDepth) {
        *out += HtmlSanitizer::escape(html.mid(index));
        return html.size();
    }

    while (index < html.size()) {
        if (html.at(index) != QLatin1Char('<')) {
            const int next = html.indexOf(QLatin1Char('<'), index);
            const int end = next < 0 ? html.size() : next;
            *out += HtmlSanitizer::escape(html.mid(index, end - index));
            index = end;
            continue;
        }

        if (html.mid(index, 4) == QLatin1String("<!--")) {
            const int close = html.indexOf(QLatin1String("-->"), index + 4);
            index = close < 0 ? html.size() : close + 3;
            continue;
        }
        if (html.mid(index, 2) == QLatin1String("<!") || html.mid(index, 2) == QLatin1String("<?")) {
            const int close = html.indexOf(QLatin1Char('>'), index + 2);
            index = close < 0 ? html.size() : close + 1;
            continue;
        }

        const Tag tag = readTag(html, index);
        if (!tag.wellFormed) {
            *out += HtmlSanitizer::escape(html.mid(index, 1));
            ++index;
            continue;
        }
        if (tag.closing) {
            if (!stopTag.isEmpty() && tag.name == stopTag)
                return tag.end;
            index = tag.end;
            continue;
        }
        index = emitTag(html, tag, imageBase, out, depth);
    }
    return index;
}

} // namespace

QString HtmlSanitizer::escape(const QString &text) {
    QString escaped;
    escaped.reserve(text.size());
    for (const QChar character : text) {
        switch (character.unicode()) {
        case '&':
            escaped += QStringLiteral("&amp;");
            break;
        case '<':
            escaped += QStringLiteral("&lt;");
            break;
        case '>':
            escaped += QStringLiteral("&gt;");
            break;
        case '"':
            escaped += QStringLiteral("&quot;");
            break;
        case '\'':
            escaped += QStringLiteral("&#39;");
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

bool HtmlSanitizer::isSafeLinkUrl(const QString &url, QString *normalized) {
    const QString cleaned = cleanUrl(url);
    if (cleaned.isEmpty() || cleaned.contains(QLatin1Char('<')) || cleaned.contains(QLatin1Char('"')))
        return false;

    if (cleaned.startsWith(QLatin1Char('#'))) {
        if (cleaned.contains(QLatin1Char(':')) || cleaned.contains(QLatin1Char('\'')))
            return false;
        if (normalized)
            *normalized = cleaned;
        return true;
    }

    const QString scheme = schemeOf(cleaned);
    if (!scheme.isEmpty()) {
        const bool web = scheme == QStringLiteral("http") || scheme == QStringLiteral("https");
        if (scheme != QStringLiteral("mailto") && !web)
            return false;
        if (web && !cleaned.mid(scheme.size() + 1).startsWith(QLatin1String("//")))
            return false;
        if (normalized)
            *normalized = cleaned;
        return true;
    }

    if (normalized)
        *normalized = cleaned;
    return true;
}

bool HtmlSanitizer::isSafeImageUrl(const QString &url, QString *normalized) {
    const QString cleaned = cleanUrl(url);
    if (cleaned.isEmpty() || cleaned.contains(QLatin1Char('<')) || cleaned.contains(QLatin1Char('"')))
        return false;

    const QString scheme = schemeOf(cleaned);
    if (scheme == QStringLiteral("data")) {
        if (!isSafeDataImage(cleaned))
            return false;
        if (normalized)
            *normalized = cleaned;
        return true;
    }
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
        if (!cleaned.mid(scheme.size() + 1).startsWith(QLatin1String("//")))
            return false;
        if (normalized)
            *normalized = cleaned;
        return true;
    }
    if (scheme == QStringLiteral("file")) {
        const QUrl fileUrl(cleaned);
        if (!fileUrl.isValid() || !fileUrl.isLocalFile())
            return false;
        if (normalized)
            *normalized = fileUrl.toString(QUrl::FullyEncoded);
        return true;
    }
    if (!scheme.isEmpty())
        return false;
    if (normalized)
        *normalized = cleaned;
    return true;
}

QString HtmlSanitizer::sanitize(const QString &html, const QString &imageBaseDirectory) {
    QString safe;
    safe.reserve(html.size());
    parseNodes(html, 0, QString(), imageBaseDirectory, &safe, 0);
    return safe;
}
