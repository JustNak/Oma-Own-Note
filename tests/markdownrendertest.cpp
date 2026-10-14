#include "markdownrendertest.h"

#include <QFile>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest>

#include "backend.h"
#include "htmlsanitizer.h"
#include "markdownrenderer.h"

namespace {

MarkdownRenderOptions optionsFor(const QString &baseDirectory = QString()) {
    MarkdownRenderOptions options;
    options.dark = true;
    options.baseDirectory = baseDirectory;
    options.foreground = QStringLiteral("#eeeeee");
    options.accent = QStringLiteral("#5584aa");
    options.muted = QStringLiteral("#909191");
    options.codeBackground = QStringLiteral("#1c1a1a");
    return options;
}

QVariantList render(const QString &markdown, const QString &baseDirectory = QString()) {
    return MarkdownRenderer::render(markdown, optionsFor(baseDirectory));
}

QVariantMap blockOfType(const QVariantList &blocks, const QString &type) {
    for (const QVariant &value : blocks) {
        const QVariantMap block = value.toMap();
        if (block.value(QStringLiteral("type")).toString() == type)
            return block;
    }
    return {};
}

void collectHtml(const QVariant &value, QStringList *html) {
    if (value.userType() == QMetaType::QVariantList) {
        for (const QVariant &item : value.toList())
            collectHtml(item, html);
        return;
    }
    if (value.userType() != QMetaType::QVariantMap)
        return;
    const QVariantMap map = value.toMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.key() == QLatin1String("html") || it.key() == QLatin1String("src"))
            html->append(it.value().toString());
        else
            collectHtml(it.value(), html);
    }
}

QQuickItem *findVisual(QQuickItem *item, const QString &name) {
    if (!item)
        return nullptr;
    if (item->objectName() == name)
        return item;
    const auto kids = item->childItems();
    for (QQuickItem *child : kids) {
        if (QQuickItem *found = findVisual(child, name))
            return found;
    }
    return nullptr;
}

QString allHtml(const QVariantList &blocks) {
    QStringList html;
    collectHtml(QVariant(blocks), &html);
    return html.join(QLatin1Char('\n'));
}

#define assertNeutral(html) \
    do { \
        const QString neutralHtml = (html); \
        QVERIFY2(!QRegularExpression(QStringLiteral("<\\s*script"), \
                                      QRegularExpression::CaseInsensitiveOption) \
                      .match(neutralHtml) \
                      .hasMatch(), \
                 qPrintable(neutralHtml)); \
        QVERIFY2(!QRegularExpression(QStringLiteral("<\\s*iframe"), \
                                      QRegularExpression::CaseInsensitiveOption) \
                      .match(neutralHtml) \
                      .hasMatch(), \
                 qPrintable(neutralHtml)); \
        QVERIFY2(!QRegularExpression(QStringLiteral("<[^>]*\\son[a-z]+\\s*="), \
                                      QRegularExpression::CaseInsensitiveOption) \
                      .match(neutralHtml) \
                      .hasMatch(), \
                 qPrintable(neutralHtml)); \
        QVERIFY2(!QRegularExpression(QStringLiteral("(href|src)\\s*=\\s*\"[^\"]*javascript\\s*:"), \
                                      QRegularExpression::CaseInsensitiveOption) \
                      .match(neutralHtml) \
                      .hasMatch(), \
                 qPrintable(neutralHtml)); \
        QVERIFY2(!QRegularExpression( \
                       QStringLiteral("src\\s*=\\s*\"data:(?!image/(?:png|jpe?g|gif|webp);base64,)"), \
                       QRegularExpression::CaseInsensitiveOption) \
                      .match(neutralHtml) \
                      .hasMatch(), \
                 qPrintable(neutralHtml)); \
    } while (false)

} // namespace

void MarkdownRenderTest::sanitizerDropsActiveContent() {
    QCOMPARE(HtmlSanitizer::sanitize(QStringLiteral("<script>alert(1)</script>")), QString());
    QCOMPARE(HtmlSanitizer::sanitize(QStringLiteral("<style>body{}</style>")), QString());
    QCOMPARE(HtmlSanitizer::sanitize(QStringLiteral("<iframe src=\"https://evil.test\"></iframe>")),
             QString());
    QCOMPARE(HtmlSanitizer::sanitize(
                 QStringLiteral("<math><mi href=\"javascript:alert(1)\">x</mi></math>")),
             QString());
    QCOMPARE(HtmlSanitizer::sanitize(QStringLiteral("a<script>alert(1)</script>b")),
             QStringLiteral("ab"));
    const QString mermaid = HtmlSanitizer::sanitize(
        QStringLiteral("<div class=\"mermaid\"><script>alert(1)</script><p onclick=\"alert(1)\">hi</p></div>"));
    QCOMPARE(mermaid, QStringLiteral("<p>hi</p>"));
    assertNeutral(mermaid);
}

void MarkdownRenderTest::sanitizerNeutralizesUrlsAndHandlers() {
    QCOMPARE(HtmlSanitizer::sanitize(
                 QStringLiteral("<a href=\"javascript:alert(1)\">x</a>")),
             QStringLiteral("x"));
    QCOMPARE(HtmlSanitizer::sanitize(
                 QStringLiteral("<a href=\"JaVaScRiPt:alert(1)\">x</a>")),
             QStringLiteral("x"));
    QCOMPARE(HtmlSanitizer::sanitize(
                 QStringLiteral("<a href=\"java&#115;cript:alert(1)\">x</a>")),
             QStringLiteral("x"));
    QCOMPARE(HtmlSanitizer::sanitize(
                 QStringLiteral("<a href=\"javascript&#58;alert(1)\">x</a>")),
             QStringLiteral("x"));
    QCOMPARE(HtmlSanitizer::sanitize(
                 QStringLiteral("<a href=\"java\tscript:alert(1)\">x</a>")),
             QStringLiteral("x"));
    QCOMPARE(HtmlSanitizer::sanitize(
                 QStringLiteral("<img src=\"javascript:alert(1)\" onerror=\"alert(1)\" alt=\"pic\">")),
             QStringLiteral("pic"));
    QCOMPARE(HtmlSanitizer::sanitize(
                 QStringLiteral("<img src=\"data:image/svg+xml;base64,PHN2ZyI=\" alt=\"svg\">")),
             QStringLiteral("svg"));
    QCOMPARE(HtmlSanitizer::sanitize(
                 QStringLiteral("<img src=\"data:text/html,<script>alert(1)</script>\" alt=\"html\">")),
             QStringLiteral("html"));
    QCOMPARE(HtmlSanitizer::sanitize(QStringLiteral("<img src=x onerror=alert(1)>")),
             QStringLiteral("<img src=\"x\" alt=\"\">"));
    QCOMPARE(HtmlSanitizer::sanitize(
                 QStringLiteral("<a href=\"https://example.com\" onclick=\"alert(1)\">x</a>")),
             QStringLiteral("<a href=\"https://example.com\" rel=\"noopener noreferrer\" target=\"_blank\">x</a>"));
}

void MarkdownRenderTest::sanitizerKeepsSafeMarkup() {
    QCOMPARE(HtmlSanitizer::sanitize(QStringLiteral("<em>ok</em>")), QStringLiteral("<em>ok</em>"));
    const QString png = QStringLiteral("data:image/png;base64,iVBORw0KGgo=");
    QCOMPARE(HtmlSanitizer::sanitize(QStringLiteral("<img src=\"%1\" alt=\"dot\">").arg(png)),
             QStringLiteral("<img src=\"%1\" alt=\"dot\">").arg(png));
    QVERIFY(HtmlSanitizer::isSafeLinkUrl(QStringLiteral("mailto:writer@example.com")));
    QVERIFY(!HtmlSanitizer::isSafeLinkUrl(QStringLiteral("file:///etc/passwd")));
}

void MarkdownRenderTest::rendersCommonMark() {
    const QVariantList blocks = render(QStringLiteral(
        "# One\n\n"
        "## Two\n\n"
        "### Three\n\n"
        "#### Four\n\n"
        "##### Five\n\n"
        "###### Six\n\n"
        "A *em* **strong** ***both*** and `code`.\n"
        "Break  \n"
        "next line.\n\n"
        "- Oak\n"
        "  - Acorn\n\n"
        "1. First\n"
        "   1. Nested\n\n"
        "> Outer\n"
        ">\n"
        "> > Inner\n\n"
        "---\n\n"
        "[example](https://example.com)\n\n"
        "![Sample swatch](sample.png)\n"));

    int headings = 0;
    for (const QVariant &value : blocks) {
        if (value.toMap().value(QStringLiteral("type")).toString() == QLatin1String("heading"))
            ++headings;
    }
    QCOMPARE(headings, 6);
    QCOMPARE(blocks.at(0).toMap().value(QStringLiteral("level")).toInt(), 1);
    QCOMPARE(blocks.at(5).toMap().value(QStringLiteral("level")).toInt(), 6);

    const QString paragraph = blocks.at(6).toMap().value(QStringLiteral("html")).toString();
    QVERIFY(paragraph.contains(QStringLiteral("<i>")));
    QVERIFY(paragraph.contains(QStringLiteral("<b>")));
    QVERIFY(paragraph.contains(QStringLiteral("<br>")));
    QVERIFY(paragraph.contains(QStringLiteral("code")));

    const QVariantMap list = blockOfType(blocks, QStringLiteral("list"));
    QVERIFY(!list.isEmpty());
    const QVariantList items = list.value(QStringLiteral("items")).toList();
    QVERIFY(items.size() >= 1);
    const QVariantList nested = items.at(0).toMap().value(QStringLiteral("blocks")).toList();
    QVERIFY(blockOfType(nested, QStringLiteral("list")).value(QStringLiteral("items")).toList().size() >= 1);

    const QVariantMap quote = blockOfType(blocks, QStringLiteral("quote"));
    QVERIFY(blockOfType(quote.value(QStringLiteral("blocks")).toList(), QStringLiteral("quote"))
                .value(QStringLiteral("type")).toString() == QLatin1String("quote"));
    QCOMPARE(blockOfType(blocks, QStringLiteral("rule")).value(QStringLiteral("type")).toString(),
             QStringLiteral("rule"));

    const QString linkHtml = allHtml(blocks);
    QVERIFY(linkHtml.contains(QStringLiteral("href=\"https://example.com\"")));
    QVERIFY(linkHtml.contains(QStringLiteral("rel=\"noopener noreferrer\"")));

    const QVariantMap image = blockOfType(blocks, QStringLiteral("image"));
    QCOMPARE(image.value(QStringLiteral("alt")).toString(), QStringLiteral("Sample swatch"));
    QVERIFY(image.value(QStringLiteral("src")).toString().contains(QStringLiteral("sample.png")));
}

void MarkdownRenderTest::rendersGfm() {
    const QVariantList blocks = render(QStringLiteral(
        "See <https://example.com/auto> and www.example.com and writer@example.com.\n\n"
        "~~gone~~\n\n"
        "| Left | Center | Right |\n"
        "| :--- | :----: | ----: |\n"
        "| a | b | c |\n"
        "| d | e | f |\n\n"
        "- [ ] open\n"
        "- [x] done\n\n"
        "Note[^label].\n\n"
        "[^label]: The *body*.\n"));

    const QString html = allHtml(blocks);
    QVERIFY(html.contains(QStringLiteral("https://example.com/auto")));
    QVERIFY(html.contains(QStringLiteral("www.example.com")));
    QVERIFY(html.contains(QStringLiteral("mailto:writer@example.com")));
    QVERIFY(html.contains(QStringLiteral("<s>")));
    QVERIFY(html.contains(QStringLiteral("gone")));

    const QVariantMap table = blockOfType(blocks, QStringLiteral("table"));
    QCOMPARE(table.value(QStringLiteral("align")).toStringList(),
             QStringList({QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}));
    QCOMPARE(table.value(QStringLiteral("header")).toStringList().size(), 3);
    QCOMPARE(table.value(QStringLiteral("rows")).toList().size(), 2);

    const QVariantMap tasks = blockOfType(blocks, QStringLiteral("list"));
    const QVariantList items = tasks.value(QStringLiteral("items")).toList();
    QCOMPARE(items.size(), 2);
    QVERIFY(items.at(0).toMap().value(QStringLiteral("task")).toBool());
    QVERIFY(!items.at(0).toMap().value(QStringLiteral("checked")).toBool());
    QVERIFY(items.at(1).toMap().value(QStringLiteral("checked")).toBool());

    const QVariantMap notes = blockOfType(blocks, QStringLiteral("footnotes"));
    const QVariantList noteItems = notes.value(QStringLiteral("items")).toList();
    QCOMPARE(noteItems.size(), 1);
    QCOMPARE(noteItems.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("1"));
    QVERIFY(html.contains(QStringLiteral("href=\"#fn-1\"")));
    QVERIFY(allHtml(noteItems.at(0).toMap().value(QStringLiteral("blocks")).toList())
                .contains(QStringLiteral("<i>")));

    const QVariantList ordered = render(QStringLiteral(
        "Second[^b] then first[^a].\n\n"
        "[^a]: aaa\n\n"
        "[^b]: bbb\n"));
    const QString orderedHtml = allHtml(ordered);
    QVERIFY(orderedHtml.contains(QStringLiteral("href=\"#fn-1\"")));
    QVERIFY(orderedHtml.contains(QStringLiteral("href=\"#fn-2\"")));
    const QVariantList orderedNotes =
        blockOfType(ordered, QStringLiteral("footnotes")).value(QStringLiteral("items")).toList();
    QCOMPARE(orderedNotes.size(), 2);
    QVERIFY(allHtml(orderedNotes.at(0).toMap().value(QStringLiteral("blocks")).toList())
                .contains(QStringLiteral("bbb")));
    QVERIFY(allHtml(orderedNotes.at(1).toMap().value(QStringLiteral("blocks")).toList())
                .contains(QStringLiteral("aaa")));
}

void MarkdownRenderTest::highlightsFencedCode() {
    const QVariantList blocks = render(QStringLiteral(
        "```cpp\n"
        "int answer = 42;\n"
        "const char *label = \"specimen\";\n"
        "// fence language\n"
        "```\n\n"
        "    indented line\n"));
    const QVariantMap code = blocks.at(0).toMap();
    QCOMPARE(code.value(QStringLiteral("language")).toString(), QStringLiteral("cpp"));
    QCOMPARE(code.value(QStringLiteral("text")).toString(),
             QStringLiteral("int answer = 42;\nconst char *label = \"specimen\";\n// fence language"));
    const QString html = code.value(QStringLiteral("html")).toString();
    QVERIFY(html.contains(QStringLiteral("color:#5584aa")));
    QVERIFY(html.contains(QStringLiteral("color:#c4b39a")));
    QVERIFY(html.contains(QStringLiteral("color:#909191")));
    QVERIFY(html.contains(QStringLiteral("&nbsp;")));
    QVERIFY(!html.contains(QStringLiteral("<script")));

    bool sawIndented = false;
    for (const QVariant &value : blocks) {
        const QVariantMap block = value.toMap();
        if (block.value(QStringLiteral("text")).toString().contains(QStringLiteral("indented"))) {
            sawIndented = true;
            QVERIFY(block.value(QStringLiteral("language")).toString().isEmpty());
        }
    }
    QVERIFY(sawIndented);
}

void MarkdownRenderTest::neutralizesPayloadsInTheDocument() {
    const QVariantList blocks = render(QStringLiteral(
        "Raw <script>alert(1)</script> and <img src=x onerror=alert(1)>.\n\n"
        "[click](javascript:alert(1))\n\n"
        "![pic](javascript:alert(1))\n\n"
        "![svg](data:image/svg+xml,<svg onload=alert(1)>)\n\n"
        "![ok](data:image/png;base64,iVBORw0KGgo=)\n\n"
        "```mermaid\n"
        "<script>alert(1)</script>\n"
        "click A \"javascript:alert(1)\"\n"
        "```\n\n"
        "```math\n"
        "\\href{javascript:alert(1)}{x}\n"
        "<math><mi onload=alert(1)></mi></math>\n"
        "```\n\n"
        "Foot[^<img src=x onerror=alert(1)>].\n\n"
        "[^<img src=x onerror=alert(1)>]: <script>alert(1)</script> [x](javascript:alert(1))\n"));

    const QString html = allHtml(blocks);
    assertNeutral(html);
    QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
    QVERIFY(!html.contains(QStringLiteral("href=\"javascript:")));
    QVERIFY(html.contains(QStringLiteral("data:image/png;base64,iVBORw0KGgo=")));

    const QVariantMap mermaid = [&blocks]() {
        for (const QVariant &value : blocks) {
            const QVariantMap block = value.toMap();
            if (block.value(QStringLiteral("language")).toString() == QLatin1String("mermaid"))
                return block;
        }
        return QVariantMap();
    }();
    QCOMPARE(mermaid.value(QStringLiteral("type")).toString(), QStringLiteral("code"));
    QVERIFY(mermaid.value(QStringLiteral("text")).toString().contains(QStringLiteral("<script>")));
    QVERIFY(mermaid.value(QStringLiteral("html")).toString().contains(QStringLiteral("&lt;script&gt;")));

    bool sawMath = false;
    for (const QVariant &value : blocks) {
        const QVariantMap block = value.toMap();
        if (block.value(QStringLiteral("language")).toString() == QLatin1String("math")) {
            sawMath = true;
            QVERIFY(block.value(QStringLiteral("html")).toString().contains(QStringLiteral("&lt;math&gt;")));
        }
    }
    QVERIFY(sawMath);
}

void MarkdownRenderTest::previewRendersTheSpecimen() {
    const QString specimenPath = QFINDTESTDATA("../docs/markdown-test.md");
    QVERIFY(!specimenPath.isEmpty());
    QFile specimen(specimenPath);
    QVERIFY(specimen.open(QIODevice::ReadOnly));
    const QVariantList blocks = render(QString::fromUtf8(specimen.readAll()),
                                        QFileInfo(specimenPath).absolutePath());
    QStringList types;
    for (const QVariant &value : blocks)
        types.append(value.toMap().value(QStringLiteral("type")).toString());
    for (const QString &type : {QStringLiteral("heading"), QStringLiteral("paragraph"),
                                QStringLiteral("list"), QStringLiteral("quote"),
                                QStringLiteral("rule"), QStringLiteral("image"),
                                QStringLiteral("table"), QStringLiteral("code"),
                                QStringLiteral("footnotes")})
        QVERIFY2(types.contains(type), qPrintable(type));

    const QString image = blockOfType(blocks, QStringLiteral("image")).value(QStringLiteral("src")).toString();
    QVERIFY(image.startsWith(QStringLiteral("file:")));
    QVERIFY(image.contains(QStringLiteral("sample.png")));
    assertNeutral(allHtml(blocks));

    const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
    Backend backend;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));

    QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
    QVERIFY(editor);
    editor->setProperty("text", QStringLiteral("# Hello render\n\n```cpp\nint x = 1;\n```\n"));
    window->setProperty("previewMode", true);

    QObject *preview = window->findChild<QObject *>(QStringLiteral("markdownPreview"));
    QVERIFY(preview);
    QTRY_VERIFY(preview->property("visible").toBool());
    QTRY_VERIFY(preview->property("implicitHeight").toInt() > 20);
    QVERIFY(preview->property("summary").toString().contains(QStringLiteral("heading")));
    QVERIFY(preview->property("summary").toString().contains(QStringLiteral("code")));
    QQuickItem *previewItem = qobject_cast<QQuickItem *>(preview);
    QVERIFY(previewItem);
    QQuickItem *copyButton = nullptr;
    QTRY_VERIFY((copyButton = findVisual(previewItem, QStringLiteral("codeCopyButton"))));
    QCOMPARE(copyButton->property("text").toString(), QStringLiteral("Copy"));
}
