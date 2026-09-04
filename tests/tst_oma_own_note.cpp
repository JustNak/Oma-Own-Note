#include <QtTest>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QSize>
#include <QStringList>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <QTextLayout>

#include "backend.h"
#include "markdownhighlighter.h"
#include "tablechrome.h"
#include "tablegeometry.h"
#include "viewzoom.h"

static QString firstTableCellRaw(const QString &line)
{
    const auto parsed = MarkdownHighlighter::parseTableLine(line);
    if (parsed.cells.isEmpty())
        return {};
    const MarkdownHighlighter::Span cell = parsed.cells.first();
    return line.mid(cell.start, cell.length);
}

static QString firstTableCellText(const QString &line)
{
    return firstTableCellRaw(line).trimmed();
}

static int spacesAfterWord(const QString &raw, const QString &word)
{
    const int at = raw.indexOf(word);
    if (at < 0)
        return -1;
    int n = 0;
    for (int i = at + word.size(); i < raw.size() && raw.at(i).isSpace(); ++i)
        ++n;
    return n;
}

static void typeIntoWindow(QQuickWindow *window, const QString &text)
{
    for (const QChar ch : text) {
        if (ch == QLatin1Char(' '))
            QTest::keyClick(window, Qt::Key_Space);
        else
            QTest::keyClick(window, static_cast<Qt::Key>(ch.toUpper().unicode()));
    }
}

class OmaOwnNoteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
        TableChrome::registerQmlType();
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!backend.darkMode());
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void insertsMarkdownBlocksAndTables() {
        const QString harnessPath = QFINDTESTDATA("InsertHarness.qml");
        QVERIFY(!harnessPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(harnessPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("headingText").toString(), QStringLiteral("# "));
        QCOMPARE(editor->property("headingCursor").toInt(), 2);
        QCOMPARE(editor->property("selectedHeading").toString(), QStringLiteral("# hello"));
        QCOMPARE(editor->property("midLineHeading").toString(), QStringLiteral("hello\n\n# "));
        QCOMPARE(editor->property("pipeTableSample").toString(),
                 QStringLiteral("|     |     |     |\n| --- | --- | --- |\n|     |     |     |"));
        QCOMPARE(editor->property("tableText").toString(),
                 QStringLiteral("|     |     |     |\n| --- | --- | --- |\n|     |     |     |"));
        QCOMPARE(editor->property("tableCursor").toInt(), 2);
        QCOMPARE(editor->property("convertedText").toString(),
                 QStringLiteral("| a   | b   |\n| --- | --- |\n| 1   | 2   |"));
        QCOMPARE(editor->property("fenceText").toString(), QStringLiteral("```\n\n```"));
        QCOMPARE(editor->property("fenceCursor").toInt(), 3);
        QCOMPARE(editor->property("imageText").toString(), QStringLiteral("![Cat](cat.png)"));
        QCOMPARE(editor->property("imageSelStart").toInt(), 2);
        QCOMPARE(editor->property("imageSelEnd").toInt(), 5);
        QCOMPARE(editor->property("dateText").toString(), QStringLiteral("2026-08-31"));
        QCOMPARE(editor->property("relativeImage").toString(), QStringLiteral("img/cat.png"));
        QVERIFY(editor->property("movedInTable").toBool());
        QCOMPARE(editor->property("nextCellStart").toInt(), 8);
        QCOMPARE(editor->property("emptyCellTypedLine").toString(),
                 QStringLiteral("|     | x    |"));
        QCOMPARE(editor->property("dividerText").toString(), QStringLiteral("---\n"));
        QCOMPARE(editor->property("dividerCursor").toInt(), 4);
        QCOMPARE(editor->property("dividerMidText").toString(),
                 QStringLiteral("hello\n\n---\n\nworld"));
        QCOMPARE(editor->property("dividerMidCursor").toInt(), 11);
        QCOMPARE(editor->property("escapedImage").toString(),
                 QStringLiteral("![Cat \\[1\\]](<photo (2).png>)"));
        QVERIFY(editor->property("oneColumnMoved").toBool());
        QCOMPARE(editor->property("oneColumnCursor").toInt(), 18);
        QVERIFY(editor->property("confinedOutsideResult").toBool());
        QCOMPARE(editor->property("confinedOutsideLine").toString().count(QLatin1Char('|')), 3);
        QVERIFY(editor->property("confinedOutsideLine").toString().contains(QLatin1Char('x')));
        QVERIFY(editor->property("tableReturnKeptRows").toBool());
        QVERIFY(editor->property("arrowKeptRagged").toBool());
        const int skipPipe = editor->property("skipPipePosition").toInt();
        QVERIFY(skipPipe > 4);
        QCOMPARE(editor->property("text").toString().at(skipPipe), QLatin1Char('b'));
    }

    void insertPaletteOpensOnCtrlTab() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_Tab, Qt::ControlModifier);
        QTRY_COMPARE(window->property("insertPaletteOpened").toBool(), true);

        QObject *palette = window->findChild<QObject *>(QStringLiteral("insertPalette"));
        QVERIFY(palette);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "applyInsertKind",
                                          Q_ARG(QVariant, QVariant(QStringLiteral("heading1"))),
                                          Q_ARG(QVariant, QVariantMap())));
        QTRY_COMPARE(editor->property("text").toString(), QStringLiteral("# "));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "closeInsertPalette"));

        QVERIFY(QMetaObject::invokeMethod(window.data(), "openInsertPalette"));
        QTRY_COMPARE(window->property("insertPaletteOpened").toBool(), true);
        QCOMPARE(palette->property("mode").toString(), QStringLiteral("root"));
        QCOMPARE(palette->property("choiceCount").toInt(), 5);
        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QTRY_COMPARE(window->property("insertPaletteOpened").toBool(), false);
        QTRY_VERIFY(editor->property("activeFocus").toBool());
    }

    void insertPaletteDrillsIntoCategories() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(QMetaObject::invokeMethod(window.data(), "openInsertPalette"));
        QTRY_COMPARE(window->property("insertPaletteOpened").toBool(), true);

        QObject *palette = window->findChild<QObject *>(QStringLiteral("insertPalette"));
        QObject *filter = window->findChild<QObject *>(QStringLiteral("insertPaletteFilter"));
        QVERIFY(palette);
        QVERIFY(filter);
        QCOMPARE(palette->property("mode").toString(), QStringLiteral("root"));
        QCOMPARE(palette->property("choiceCount").toInt(), 5);

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_Return);
        QTRY_COMPARE(palette->property("mode").toString(), QStringLiteral("category"));
        QCOMPARE(palette->property("activeSection").toString(), QStringLiteral("Structure"));
        QCOMPARE(palette->property("choiceCount").toInt(), 5);

        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QTRY_COMPARE(palette->property("mode").toString(), QStringLiteral("root"));

        filter->setProperty("text", QStringLiteral("table"));
        QTRY_COMPARE(palette->property("mode").toString(), QStringLiteral("search"));
        QCOMPARE(palette->property("choiceCount").toInt(), 1);
        QCOMPARE(palette->property("previewText").toString(), QStringLiteral("3 × 3"));

        QTest::keyClick(quickWindow, Qt::Key_Return);
        QTRY_COMPARE(palette->property("mode").toString(), QStringLiteral("table"));
        QCOMPARE(palette->property("previewText").toString(), QStringLiteral("3 × 3"));

        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QTRY_COMPARE(palette->property("mode").toString(), QStringLiteral("search"));

        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QTRY_COMPARE(palette->property("mode").toString(), QStringLiteral("root"));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "closeInsertPalette"));
    }

    void highlightsTablesTasksAndFences() {
        QTextDocument document;
        document.setPlainText(QStringLiteral(
            "| A | B |\n"
            "| --- | --- |\n"
            "| 1 | 2 |\n"
            "\n"
            "- [ ] task\n"
            "\n"
            "```\n"
            "code\n"
            "```\n"));
        MarkdownHighlighter highlighter(&document);
        highlighter.rehighlight();

        const auto formatAt = [&document](int position) {
            const QTextBlock block = document.findBlock(position);
            const int local = position - block.position();
            QTextCharFormat format;
            const auto ranges = block.layout()->formats();
            for (const QTextLayout::FormatRange &range : ranges) {
                if (local >= range.start && local < range.start + range.length)
                    format = range.format;
            }
            return format;
        };

        QCOMPARE(formatAt(2).foreground().color().alpha(), 0);
        QCOMPARE(formatAt(2).background().style(), Qt::NoBrush);
        // Table source is collapsed like inline markers so a selection cannot
        // repaint it over the overlay.
        QCOMPARE(formatAt(0).fontPointSize(), 1.0);
        QVERIFY(formatAt(0).fontLetterSpacing() < 0);
        QCOMPARE(formatAt(0).foreground().color().alpha(), 0);
        QCOMPARE(formatAt(0).background().style(), Qt::NoBrush);

        const int bodyDigit = document.toPlainText().indexOf(QLatin1Char('1'));
        QVERIFY(bodyDigit >= 0);
        QCOMPARE(formatAt(bodyDigit).background().style(), Qt::NoBrush);

        const int separatorDash = document.toPlainText().indexOf(QStringLiteral("---"));
        QVERIFY(separatorDash >= 0);
        QCOMPARE(formatAt(separatorDash).background().style(), Qt::NoBrush);
        QCOMPARE(formatAt(separatorDash).foreground().color().alpha(), 0);

        const int taskBox = document.toPlainText().indexOf(QStringLiteral("[ ]"));
        QVERIFY(taskBox >= 0);
        QCOMPARE(formatAt(taskBox).foreground().color(),
                 QColor(QStringLiteral("#4f525a")));

        const int codePos = document.toPlainText().indexOf(QStringLiteral("code"));
        QVERIFY(codePos >= 0);
        QVERIFY(formatAt(codePos).background().style() != Qt::NoBrush);
    }

    void tableGridKeepsColumnGutters() {
        QVERIFY(assertTableBoxPixels(QStringLiteral(
            "| asd | asd | asd |\n"
            "| --- | --- | --- |\n"
            "|     |     |     |\n"), 4));
    }

    void tableBoxConnectsEmptyColumns() {
        QVERIFY(assertTableBoxPixels(QStringLiteral(
            "|     |     |     |     |     |     |     |     |\n"
            "| --- | --- | --- | --- | --- | --- | --- | --- |\n"
            "|     |     |     |     |     |     |     |     |\n"), 9));
    }

    void tableRulesStayHairlineUnderPainterScale() {
        QVERIFY(assertTableBoxPixels(QStringLiteral(
            "| asd | asd | asd |\n"
            "| --- | --- | --- |\n"
            "|     |     |     |\n"), 4, 0.5));
        QVERIFY(assertTableBoxPixels(QStringLiteral(
            "| asd | asd | asd |\n"
            "| --- | --- | --- |\n"
            "|     |     |     |\n"), 4, 0.7));
        QVERIFY(assertTableBoxPixels(QStringLiteral(
            "| asd | asd | asd |\n"
            "| --- | --- | --- |\n"
            "|     |     |     |\n"), 4, 0.9));
    }

    void tableChromeSkipsFencedPipeRows() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral(
            "```\n"
            "| A | B |\n"
            "| --- | --- |\n"
            "| 1 | 2 |\n"
            "```\n"
            "\n"
            "| C | D |\n"
            "| --- | --- |\n"
            "| 3 | 4 |\n"));
        document.setTextWidth(2000);

        QImage surface(800, 300, QImage::Format_ARGB32_Premultiplied);
        surface.fill(Qt::white);
        QPainter painter(&surface);
        document.drawContents(&painter);
        painter.end();

        const auto tables = TableGeometry::collectTables(&document);
        QCOMPARE(tables.size(), 1);

        QTextDocument fencedOnly;
        fencedOnly.setDefaultFont(font);
        fencedOnly.setPlainText(QStringLiteral(
            "```markdown\n"
            "| A | B |\n"
            "| --- | --- |\n"
            "| 1 | 2 |\n"
            "```\n"));
        fencedOnly.setTextWidth(2000);
        QImage fenceSurface(800, 200, QImage::Format_ARGB32_Premultiplied);
        fenceSurface.fill(Qt::white);
        QPainter fencePainter(&fenceSurface);
        fencedOnly.drawContents(&fencePainter);
        fencePainter.end();
        QCOMPARE(TableGeometry::collectTables(&fencedOnly).size(), 0);
    }

    void tabMovesBetweenTableCells() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        editor->setProperty("text", QStringLiteral("|  |  |\n| --- | --- |\n|  |  |"));
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_Tab);
        QCOMPARE(editor->property("text").toString(),
                 QStringLiteral("|     |     |\n| --- | --- |\n|     |     |"));
        QCOMPARE(editor->property("cursorPosition").toInt(), 8);
    }

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QVERIFY(window->property("title").toString().endsWith(QStringLiteral("Oma Own Note")));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void zoomSnapsAndClamps() {
        QCOMPARE(ViewZoom::fromPercent(100).factor(), 1.0);
        QCOMPARE(ViewZoom::fromPercent(137).percent(), 140);
        QCOMPARE(ViewZoom::fromPercent(0).percent(), 50);
        QCOMPARE(ViewZoom::fromPercent(900).percent(), 300);
        QCOMPARE(ViewZoom::fromPercent(100).stepped(1).percent(), 110);
        QCOMPARE(ViewZoom::fromPercent(300).stepped(1).percent(), 300);
        QCOMPARE(ViewZoom::fromPercent(50).stepped(-1).percent(), 50);
        QCOMPARE(ViewZoom::fromPercent(150).stepped(-20).percent(), 50);
        QCOMPARE(ViewZoom{}.percent(), 100);
    }

    void zoomPersistsAcrossBackendInstances() {
        Backend first;
        first.zoomIn();
        QCOMPARE(first.zoomFactor(), 1.1);
        Backend second;
        QCOMPARE(second.zoomFactor(), 1.1);
        second.resetZoom();
        QCOMPARE(second.zoomFactor(), 1.0);
    }

    void zoomToPercentSnapsAndClamps() {
        Backend first;
        first.zoomToPercent(137);
        QCOMPARE(first.zoomFactor(), 1.4);
        first.zoomToPercent(0);
        QCOMPARE(first.zoomFactor(), 0.5);
        first.zoomToPercent(900);
        QCOMPARE(first.zoomFactor(), 3.0);
        first.zoomToPercent(100);
        QCOMPARE(first.zoomFactor(), 1.0);

        first.zoomToPercent(137);
        QCOMPARE(first.zoomFactor(), 1.4);
        Backend second;
        QCOMPARE(second.zoomFactor(), 1.4);
        second.resetZoom();
        QCOMPARE(second.zoomFactor(), 1.0);
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // `omarchy display text size 16` sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);
    }

    void canvasZoomDoesNotChangeEditorPixelSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *canvas = window->findChild<QObject *>(QStringLiteral("editorCanvas"));
        QVERIFY(editor);
        QVERIFY(canvas);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);
        QCOMPARE(canvas->property("scale").toReal(), 1.0);

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_Equal, Qt::ControlModifier);
        QTRY_COMPARE(backend.zoomFactor(), 1.1);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);
        QCOMPARE(canvas->property("scale").toReal(), 1.1);

        QTest::keyClick(quickWindow, Qt::Key_Minus, Qt::ControlModifier);
        QTRY_COMPARE(backend.zoomFactor(), 1.0);
        QCOMPARE(canvas->property("scale").toReal(), 1.0);

        QTest::keyClick(quickWindow, Qt::Key_Equal, Qt::ControlModifier);
        QTRY_COMPARE(backend.zoomFactor(), 1.1);
        QTest::keyClick(quickWindow, Qt::Key_0, Qt::ControlModifier);
        QTRY_COMPARE(backend.zoomFactor(), 1.0);
        QCOMPARE(canvas->property("scale").toReal(), 1.0);

        QVERIFY(QMetaObject::invokeMethod(window.data(), "zoomIn"));
        QCOMPARE(backend.zoomFactor(), 1.1);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);
        QCOMPARE(canvas->property("scale").toReal(), 1.1);

        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);
        QCOMPARE(canvas->property("scale").toReal(), 1.1);

        QObject *wordCount = window->findChild<QObject *>(QStringLiteral("wordCountLabel"));
        QVERIFY(wordCount);
        QCOMPARE(wordCount->property("font").value<QFont>().pixelSize(),
                 qRound(11 * 16.0 / 12.0));

        backend.resetZoom();
        QCOMPARE(backend.zoomFactor(), 1.0);
    }

    void canvasZoomKeepsColumnOrigin() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *canvas = window->findChild<QObject *>(QStringLiteral("editorCanvas"));
        QObject *flick = window->findChild<QObject *>(QStringLiteral("editorFlick"));
        QVERIFY(canvas);
        QVERIFY(flick);

        const qreal originX = canvas->property("x").toReal();
        QVERIFY(originX >= 0);

        for (int i = 0; i < 5; ++i)
            backend.zoomOut();
        QCOMPARE(backend.zoomFactor(), 0.5);
        QCOMPARE(canvas->property("x").toReal(), originX);

        backend.resetZoom();
        for (int i = 0; i < 20; ++i)
            backend.zoomIn();
        QCOMPARE(backend.zoomFactor(), 3.0);
        QCOMPARE(canvas->property("x").toReal(), originX);
        const qreal visualRight = originX
            + canvas->property("width").toReal() * canvas->property("scale").toReal();
        QVERIFY(flick->property("contentWidth").toReal() >= visualRight);

        backend.resetZoom();
    }

    void canvasZoomKeepsVisualColumn() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *canvas = window->findChild<QObject *>(QStringLiteral("editorCanvas"));
        QObject *flick = window->findChild<QObject *>(QStringLiteral("editorFlick"));
        QVERIFY(editor);
        QVERIFY(canvas);
        QVERIFY(flick);

        const qreal columnWidth = editor->property("width").toReal();
        const qreal originX = canvas->property("x").toReal();
        QVERIFY(columnWidth > 0);
        QVERIFY(originX >= 0);

        backend.zoomToPercent(70);
        QCOMPARE(backend.zoomFactor(), 0.7);
        QCOMPARE(canvas->property("x").toReal(), originX);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);
        QVERIFY(editor->property("width").toReal() > columnWidth);
        const qreal visualAt70 = canvas->property("width").toReal()
            * canvas->property("scale").toReal();
        QVERIFY2(qAbs(visualAt70 - columnWidth) < 2.0,
                 qPrintable(QStringLiteral("70% visual %1 vs column %2")
                            .arg(visualAt70)
                            .arg(columnWidth)));

        backend.zoomToPercent(300);
        QCOMPARE(backend.zoomFactor(), 3.0);
        QCOMPARE(canvas->property("x").toReal(), originX);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);
        QVERIFY(editor->property("width").toReal() < columnWidth);
        const qreal visualAt300 = canvas->property("width").toReal()
            * canvas->property("scale").toReal();
        QVERIFY2(qAbs(visualAt300 - columnWidth) < 2.0,
                 qPrintable(QStringLiteral("300% visual %1 vs column %2")
                            .arg(visualAt300)
                            .arg(columnWidth)));
        QVERIFY(flick->property("contentWidth").toReal()
                <= flick->property("width").toReal() + 1);

        backend.resetZoom();
        QCOMPARE(editor->property("width").toReal(), columnWidth);
    }

    void editorColumnFollowsWindowNotZoom() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *canvas = window->findChild<QObject *>(QStringLiteral("editorCanvas"));
        QObject *flick = window->findChild<QObject *>(QStringLiteral("editorFlick"));
        QObject *chrome = window->findChild<QObject *>(QStringLiteral("tableChrome"));
        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(editor);
        QVERIFY(canvas);
        QVERIFY(flick);
        QVERIFY(chrome);
        QVERIFY(quickWindow);

        const auto visualColumn = [canvas]() {
            return canvas->property("width").toReal() * canvas->property("scale").toReal();
        };

        QTRY_VERIFY(flick->property("width").toReal() > 360);
        QVERIFY2(qAbs(visualColumn() - flick->property("width").toReal()) < 2.0,
                 qPrintable(QStringLiteral("visual %1 vs flick %2")
                            .arg(visualColumn())
                            .arg(flick->property("width").toReal())));
        QCOMPARE(chrome->property("wrapWidth").toReal(), editor->property("width").toReal());
        const qreal visualAtDefault = visualColumn();
        QVERIFY(visualAtDefault > 360);

        backend.zoomToPercent(70);
        QCOMPARE(backend.zoomFactor(), 0.7);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);
        QVERIFY2(qAbs(visualColumn() - visualAtDefault) < 2.0,
                 qPrintable(QStringLiteral("70% visual %1 vs window column %2")
                            .arg(visualColumn())
                            .arg(visualAtDefault)));
        QCOMPARE(chrome->property("wrapWidth").toReal(), editor->property("width").toReal());

        backend.zoomToPercent(300);
        QCOMPARE(backend.zoomFactor(), 3.0);
        QVERIFY2(qAbs(visualColumn() - visualAtDefault) < 2.0,
                 qPrintable(QStringLiteral("300% visual %1 vs window column %2")
                            .arg(visualColumn())
                            .arg(visualAtDefault)));
        QCOMPARE(chrome->property("wrapWidth").toReal(), editor->property("width").toReal());
        QVERIFY(flick->property("contentWidth").toReal()
                <= flick->property("width").toReal() + 1);

        backend.resetZoom();
        QCOMPARE(backend.zoomFactor(), 1.0);
        QVERIFY2(qAbs(visualColumn() - visualAtDefault) < 2.0,
                 qPrintable(QStringLiteral("reset visual %1 vs window column %2")
                            .arg(visualColumn())
                            .arg(visualAtDefault)));

        quickWindow->resize(1600, quickWindow->height());
        QTRY_VERIFY(visualColumn() > visualAtDefault + 50);
        QVERIFY2(qAbs(visualColumn() - flick->property("width").toReal()) < 2.0,
                 qPrintable(QStringLiteral("wide visual %1 vs flick %2")
                            .arg(visualColumn())
                            .arg(flick->property("width").toReal())));
        QCOMPARE(chrome->property("wrapWidth").toReal(), editor->property("width").toReal());

        quickWindow->resize(800, quickWindow->height());
        QTRY_VERIFY(visualColumn() < visualAtDefault - 50);
        QVERIFY2(qAbs(visualColumn() - flick->property("width").toReal()) < 2.0,
                 qPrintable(QStringLiteral("narrow visual %1 vs flick %2")
                            .arg(visualColumn())
                            .arg(flick->property("width").toReal())));
        QCOMPARE(chrome->property("wrapWidth").toReal(), editor->property("width").toReal());

        const qreal visualAtNarrow = visualColumn();
        backend.zoomToPercent(70);
        QCOMPARE(backend.zoomFactor(), 0.7);
        QVERIFY2(qAbs(visualColumn() - visualAtNarrow) < 2.0,
                 qPrintable(QStringLiteral("narrow 70% visual %1 vs column %2")
                            .arg(visualColumn())
                            .arg(visualAtNarrow)));
        QCOMPARE(chrome->property("wrapWidth").toReal(), editor->property("width").toReal());

        backend.resetZoom();
        quickWindow->resize(1280, 820);
        QTRY_COMPARE(quickWindow->width(), 1280);
    }

    void tableInsertedRowsHaveEqualHeight() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *chrome = window->findChild<QObject *>(QStringLiteral("tableChrome"));
        QVERIFY(editor);
        QVERIFY(chrome);

        editor->setProperty("text", QStringLiteral(
            "|     |     |     |     |     |     |\n"
            "| --- | --- | --- | --- | --- | --- |\n"
            "|     |     |     |     |     |     |\n"
            "|     |     |     |     |     |     |\n"));
        QTRY_VERIFY(chrome->property("naturalWidth").toReal() > 1);

        auto *quickDocument = qobject_cast<QQuickTextDocument *>(
            qvariant_cast<QObject *>(editor->property("textDocument")));
        QVERIFY(quickDocument);
        QTextDocument *document = quickDocument->textDocument();
        QVERIFY(document);

        QTextBlock header = document->begin();
        QVERIFY(header.isValid());
        QCOMPARE(header.blockFormat().lineHeightType(), QTextBlockFormat::MinimumHeight);
        QCOMPARE(header.blockFormat().lineHeight(),
                 MarkdownHighlighter::tableDataRowLineHeight(document->defaultFont()));
        QVERIFY(header.blockFormat().nonBreakableLines());

        const QTextBlock separator = header.next();
        QVERIFY(separator.isValid());
        QCOMPARE(separator.blockFormat().lineHeightType(), QTextBlockFormat::FixedHeight);
        QCOMPARE(separator.blockFormat().lineHeight(),
                 MarkdownHighlighter::tableSeparatorLineHeight);

        const auto tables = TableGeometry::collectTables(document);
        QCOMPARE(tables.size(), 1);
        const QVector<qreal> edges = tables.first().rowEdges;
        QVERIFY(edges.size() >= 4);
        const qreal headerGap = edges.at(1) - edges.at(0);
        const qreal bodyGap = edges.at(2) - edges.at(1);
        const qreal nextBodyGap = edges.at(3) - edges.at(2);
        QVERIFY2(qAbs(headerGap - bodyGap) <= 1.0,
                 qPrintable(QStringLiteral("header %1 vs body %2")
                            .arg(headerGap).arg(bodyGap)));
        QVERIFY2(qAbs(bodyGap - nextBodyGap) <= 1.0,
                 qPrintable(QStringLiteral("body %1 vs next %2")
                            .arg(bodyGap).arg(nextBodyGap)));

        // The overlay's body rows must sit on the body blocks, including the
        // collapsed separator block laid out between header and body.
        const QTextBlock firstBody = separator.next();
        QVERIFY(firstBody.isValid());
        QVERIFY(firstBody.next().isValid());
        QAbstractTextDocumentLayout *layout = document->documentLayout();
        const qreal bodyTop = layout->blockBoundingRect(firstBody).top();
        const qreal nextBodyTop = layout->blockBoundingRect(firstBody.next()).top();
        QVERIFY2(qAbs(edges.at(1) - bodyTop) <= 0.5,
                 qPrintable(QStringLiteral("overlay body top %1 vs block top %2")
                            .arg(edges.at(1)).arg(bodyTop)));
        QVERIFY2(qAbs(edges.at(2) - nextBodyTop) <= 0.5,
                 qPrintable(QStringLiteral("overlay body bottom %1 vs next block top %2")
                            .arg(edges.at(2)).arg(nextBodyTop)));
    }

    void tableZoomInKeepsWritingColumn() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *chrome = window->findChild<QObject *>(QStringLiteral("tableChrome"));
        QVERIFY(editor);
        QVERIFY(chrome);

        const qreal columnWidth = editor->property("width").toReal();
        editor->setProperty("text", QStringLiteral(
            "| alpha | bravo | charlie | delta | echo | foxtrot | golf | hotel |\n"
            "| ----- | ----- | ------- | ----- | ---- | ------- | ---- | ----- |\n"
            "|       |       |         |       |      |         |      |       |\n"));
        QTRY_VERIFY(chrome->property("naturalWidth").toReal() > 1);

        backend.zoomToPercent(300);
        QCOMPARE(backend.zoomFactor(), 3.0);
        QTRY_VERIFY(qAbs(editor->property("width").toReal() - columnWidth / 3.0) < 2.0);
        QVERIFY(editor->property("width").toReal()
                < chrome->property("naturalWidth").toReal());

        auto *quickDocument = qobject_cast<QQuickTextDocument *>(
            qvariant_cast<QObject *>(editor->property("textDocument")));
        QVERIFY(quickDocument);
        QTextDocument *document = quickDocument->textDocument();
        QVERIFY(document);
        for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
            if (!MarkdownHighlighter::isTableRow(block.text())
                    || MarkdownHighlighter::isTableSeparator(block.text()))
                continue;
            QVERIFY(block.layout());
            QCOMPARE(block.layout()->lineCount(), 1);
        }
        const auto tables = TableGeometry::collectTables(
            document, editor->property("width").toReal());
        QCOMPARE(tables.size(), 1);
        QCOMPARE(tables.first().columns.size(), 9);

        backend.resetZoom();
    }

    void tableCellsWrapInsideColumnCap() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        const QString longCell = QString(80, QLatin1Char('w'));
        document.setPlainText(
            QStringLiteral("| short | x |\n| ----- | - |\n| ")
            + longCell + QStringLiteral(" | y |\n"));
        document.setTextWidth(220);

        const auto tables = TableGeometry::collectTables(&document, 220);
        QCOMPARE(tables.size(), 1);
        QVERIFY2(tables.first().bounds.width() <= 221,
                 qPrintable(QStringLiteral("table width %1")
                            .arg(tables.first().bounds.width())));
        QVERIFY(tables.first().rowEdges.size() >= 3);
        const qreal headerH = tables.first().rowEdges.at(1) - tables.first().rowEdges.at(0);
        const qreal bodyH = tables.first().rowEdges.at(2) - tables.first().rowEdges.at(1);
        QVERIFY2(bodyH > headerH + 4,
                 qPrintable(QStringLiteral("header %1 body %2")
                            .arg(headerH).arg(bodyH)));
        const qreal minRow = MarkdownHighlighter::tableDataRowLineHeight(font);
        QVERIFY2(qAbs(headerH - minRow) <= 2,
                 qPrintable(QStringLiteral("header %1 min %2").arg(headerH).arg(minRow)));
        QCOMPARE(tables.first().columns.size(), 3);
    }

    void tableLongColumnsShareWrapWidth() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);

        const QFontMetricsF metrics(font);
        const qreal wrapWidth = metrics.averageCharWidth() * 65;
        const qreal spaceAdvance = qMax(qreal(1), metrics.horizontalAdvance(QLatin1Char(' ')));
        const qreal pipeAdvance = qMax(qreal(1), metrics.horizontalAdvance(QLatin1Char('|')));
        const qreal minInner = qMax(spaceAdvance * 3, metrics.averageCharWidth() * 3);
        const qreal gutter = 2 * spaceAdvance + pipeAdvance;
        const qreal minSpan = minInner + gutter;

        document.setPlainText(QStringLiteral(
            "| This is a test to see if the table itself is fixed | How else would this work though? I want to | |\n"
            "| --- | --- | --- |\n"
            "|  |  |  |\n"
            "|  |  |  |\n"));
        document.setTextWidth(wrapWidth);

        const auto tables = TableGeometry::collectTables(&document, wrapWidth);
        QCOMPARE(tables.size(), 1);
        const auto &box = tables.first();
        QVERIFY2(box.bounds.width() <= wrapWidth + 1,
                 qPrintable(QStringLiteral("table width %1 cap %2")
                            .arg(box.bounds.width()).arg(wrapWidth)));
        QCOMPARE(box.columns.size(), 4);
        QVERIFY(box.rowEdges.size() >= 4);

        const qreal col0 = box.columns.at(1) - box.columns.at(0);
        const qreal col1 = box.columns.at(2) - box.columns.at(1);
        const qreal col2 = box.columns.at(3) - box.columns.at(2);
        QVERIFY2(qAbs(col0 - col1) < 4,
                 qPrintable(QStringLiteral("long columns %1 vs %2").arg(col0).arg(col1)));
        QVERIFY2(col0 > minSpan + spaceAdvance * 8,
                 qPrintable(QStringLiteral("long column crushed to %1 min %2")
                            .arg(col0).arg(minSpan)));
        QVERIFY2(col1 > minSpan + spaceAdvance * 8,
                 qPrintable(QStringLiteral("second long column crushed to %1 min %2")
                            .arg(col1).arg(minSpan)));
        QVERIFY2(qAbs(col2 - minSpan) < 4,
                 qPrintable(QStringLiteral("empty column %1 min %2").arg(col2).arg(minSpan)));

        const qreal headerH = box.rowEdges.at(1) - box.rowEdges.at(0);
        const qreal bodyH = box.rowEdges.at(2) - box.rowEdges.at(1);
        const qreal minRow = MarkdownHighlighter::tableDataRowLineHeight(font);
        QVERIFY2(headerH > bodyH + 4,
                 qPrintable(QStringLiteral("header %1 body %2").arg(headerH).arg(bodyH)));
        QVERIFY2(qAbs(bodyH - minRow) <= 2,
                 qPrintable(QStringLiteral("body %1 min %2").arg(bodyH).arg(minRow)));
        QVERIFY2(headerH < minRow * 8,
                 qPrintable(QStringLiteral("header tower %1 min %2").arg(headerH).arg(minRow)));
    }

    void tableCaretMoveRestretchesWrappedRow() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString text = QStringLiteral("| ") + QString(80, QLatin1Char('w'))
            + QStringLiteral(" | ") + QString(40, QLatin1Char(' '))
            + QStringLiteral(" |\n| --- | --- |\n| y | z |\n");
        editor->setProperty("text", text);
        editor->setProperty("cursorPosition", 0);

        auto *quickDocument = qobject_cast<QQuickTextDocument *>(
            qvariant_cast<QObject *>(editor->property("textDocument")));
        QVERIFY(quickDocument);
        QTextDocument *document = quickDocument->textDocument();
        QVERIFY(document);

        const qreal wrapWidth = 220;
        backend.setTableWrapWidth(wrapWidth);
        const int headerPos = document->begin().position();
        const int paddingCaret = 2 + 80 + 3 + 20;
        const QHash<int, qreal> outside = TableGeometry::dataRowHeights(document, wrapWidth, -1);
        const QHash<int, qreal> inside = TableGeometry::dataRowHeights(
            document, wrapWidth, paddingCaret);
        QVERIFY(inside.contains(headerPos));
        QVERIFY2(inside.value(headerPos) > outside.value(headerPos) + 4,
                 qPrintable(QStringLiteral("caret wrap %1 vs outside %2")
                            .arg(inside.value(headerPos)).arg(outside.value(headerPos))));

        QCOMPARE(document->begin().blockFormat().lineHeight(),
                 outside.value(headerPos));

        backend.setTableCaretPosition(paddingCaret);
        QCOMPARE(document->begin().blockFormat().lineHeight(),
                 inside.value(headerPos));
    }

    void tableCaretRestretchKeepsTypingUndo() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString text = QStringLiteral("| ") + QString(80, QLatin1Char('w'))
            + QStringLiteral(" | ") + QString(40, QLatin1Char(' '))
            + QStringLiteral(" |\n| --- | --- |\n| y | z |\n");
        editor->setProperty("text", text);
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));
        backend.setTableWrapWidth(220);

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_A);
        QVERIFY(editor->property("text").toString().contains(QLatin1Char('a')));

        const int paddingCaret = 2 + 80 + 3 + 20;
        backend.setTableCaretPosition(paddingCaret);
        QVERIFY(editor->property("text").toString().contains(QLatin1Char('a')));

        QTest::keyClick(quickWindow, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY2(!editor->property("text").toString().contains(QLatin1Char('a')),
                 "caret restretch must not intercept the typing undo command");
    }

    void tableContentCursorSharesRowHeights() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral(
            "| hello | world |\n"
            "| --- | --- |\n"
            "| x | y |\n"));
        document.setTextWidth(2000);

        const int hello = document.toPlainText().indexOf(QLatin1String("hello"));
        QVERIFY(hello >= 0);
        const QHash<int, qreal> atHello =
            TableGeometry::dataRowHeights(&document, 2000, hello);
        const QHash<int, qreal> atNext =
            TableGeometry::dataRowHeights(&document, 2000, hello + 3);
        const QHash<int, qreal> outside =
            TableGeometry::dataRowHeights(&document, 2000, -1);
        QCOMPARE(atHello, atNext);
        QCOMPARE(atHello, outside);
        QCOMPARE(TableGeometry::layoutRelevantCursor(&document, hello), -1);
        QCOMPARE(TableGeometry::layoutRelevantCursor(&document, hello + 3), -1);
    }

    void tableContentCaretDoesNotRestretch() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString text = QStringLiteral(
            "| hello | world |\n"
            "| --- | --- |\n"
            "| x | y |\n");
        editor->setProperty("text", text);

        auto *quickDocument = qobject_cast<QQuickTextDocument *>(
            qvariant_cast<QObject *>(editor->property("textDocument")));
        QVERIFY(quickDocument);
        QTextDocument *document = quickDocument->textDocument();
        QVERIFY(document);

        backend.setTableWrapWidth(2000);
        const int hello = text.indexOf(QLatin1String("hello"));
        backend.setTableCaretPosition(hello);
        const int revision = document->revision();
        const qreal headerHeight = document->begin().blockFormat().lineHeight();

        backend.setTableCaretPosition(hello + 1);
        QCOMPARE(document->revision(), revision);
        QCOMPARE(document->begin().blockFormat().lineHeight(), headerHeight);
    }

    void tableChromeContentCaretKeepsLayoutRevision() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral(
            "| hello | world |\n"
            "| --- | --- |\n"
            "| x | y |\n"));
        document.setTextWidth(2000);

        TableChrome chrome;
        chrome.setWrapWidth(2000);
        chrome.setTextDocument(&document);
        const int hello = document.toPlainText().indexOf(QLatin1String("hello"));
        chrome.setCursorPosition(hello);
        QVERIFY(chrome.caretRect(hello).height() > 0);
        const int revision = chrome.layoutRevision();
        chrome.setCursorPosition(hello + 2);
        QCOMPARE(chrome.layoutRevision(), revision);
    }

    void tableChromePaddingCaretRevealsSpaces() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral(
            "| hello     | world |\n"
            "| --- | --- |\n"
            "| x | y |\n"));
        document.setTextWidth(2000);

        TableChrome chrome;
        chrome.setWrapWidth(2000);
        chrome.setTextDocument(&document);
        const int hello = document.toPlainText().indexOf(QLatin1String("hello"));
        chrome.setCursorPosition(hello);
        QVERIFY(chrome.caretRect(hello).height() > 0);
        const int revision = chrome.layoutRevision();
        const QRectF atWord = chrome.caretRect(hello);
        QCOMPARE(TableGeometry::layoutRelevantCursor(&document, hello + 7), hello + 7);
        chrome.setCursorPosition(hello + 7);
        const QRectF atPad = chrome.caretRect(hello + 7);
        QVERIFY2(atPad.height() > 0 && atPad.x() > atWord.x() + 8,
                 "caret in trailing cell padding must sit in the revealed spaces");
        QVERIFY2(chrome.layoutRevision() > revision,
                 "revealed padding that widens a column must move overlay wrap");
    }

    void tableChromeTypingDoesNotDirtyUnwrappedRow() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral(
            "| hello | world |\n"
            "| --- | --- |\n"
            "| x | y |\n"));
        document.setTextWidth(2000);

        TableChrome chrome;
        chrome.setWrapWidth(2000);
        chrome.setTextDocument(&document);
        const int bodyX = document.toPlainText().indexOf(QLatin1String("| x |"));
        QVERIFY(bodyX >= 0);
        const int xAt = bodyX + 2;
        chrome.setCursorPosition(xAt + 1);
        QVERIFY(chrome.caretRect(xAt + 1).height() > 0);
        const int revision = chrome.layoutRevision();

        QTextCursor cursor(&document);
        cursor.setPosition(xAt + 1);
        cursor.insertText(QStringLiteral("z"));
        chrome.setCursorPosition(xAt + 2);

        QCOMPARE(chrome.layoutRevision(), revision);
        QVERIFY(chrome.caretRect(xAt + 2).height() > 0);
    }

    void tableChromeIgnoresFormatOnlyContentsChange() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral(
            "| hello | world |\n"
            "| --- | --- |\n"
            "| x | y |\n"));
        document.setTextWidth(2000);

        TableChrome chrome;
        chrome.setWrapWidth(2000);
        chrome.setTextDocument(&document);
        const int hello = document.toPlainText().indexOf(QLatin1String("hello"));
        chrome.setCursorPosition(hello);
        QVERIFY(chrome.caretRect(hello).height() > 0);
        const int revision = chrome.layoutRevision();

        QTextCursor cursor(&document);
        cursor.setPosition(hello);
        QTextCharFormat format;
        format.setForeground(QColor(Qt::red));
        cursor.mergeCharFormat(format);

        QCOMPARE(chrome.layoutRevision(), revision);
    }

    void tableSiblingColumnsGrowTogether() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral(
            "| hi | a |\n"
            "| -- | - |\n"
            "| hi | b |\n"));
        document.setTextWidth(2000);
        const auto before = TableGeometry::collectTables(&document, 2000);
        QCOMPARE(before.size(), 1);
        const qreal shortCol = before.first().columns.at(1) - before.first().columns.at(0);

        document.setPlainText(QStringLiteral(
            "| hellohello | a |\n"
            "| ---------- | - |\n"
            "| hi         | b |\n"));
        const auto after = TableGeometry::collectTables(&document, 2000);
        QCOMPARE(after.size(), 1);
        const qreal longCol = after.first().columns.at(1) - after.first().columns.at(0);
        QVERIFY2(longCol > shortCol + 8,
                 qPrintable(QStringLiteral("short %1 long %2")
                            .arg(shortCol).arg(longCol)));
        QCOMPARE(after.first().columns.size(), before.first().columns.size());
        const qreal otherBefore = before.first().columns.at(2) - before.first().columns.at(1);
        const qreal otherAfter = after.first().columns.at(2) - after.first().columns.at(1);
        QVERIFY2(qAbs(otherAfter - otherBefore) < 4,
                 qPrintable(QStringLiteral("other before %1 after %2")
                            .arg(otherBefore).arg(otherAfter)));
    }

    void tableTypingStaysInsideCells() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString original = QStringLiteral("| a | b |\n| --- | --- |\n|     |     |");
        editor->setProperty("text", original);
        const int lastPipe = original.indexOf(QLatin1Char('\n')) - 1;
        editor->setProperty("cursorPosition", lastPipe);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_X);

        const QString text = editor->property("text").toString();
        const QString header = text.section(QLatin1Char('\n'), 0, 0);
        QCOMPARE(header.count(QLatin1Char('|')), 3);
        QVERIFY(header.contains(QLatin1Char('x')));
        QVERIFY(MarkdownHighlighter::isTableRow(header));
    }

    void tableTypingKeepsLetterOrder() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString original = QStringLiteral("|     |     |\n| --- | --- |\n|     |     |");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        // QTest::keyClick on QQuickWindow does not put Shift into event.text,
        // so this uses the same four-letter sequence as the "This" / "hisT"
        // report (first letter ending up at the right of the cell).
        typeIntoWindow(quickWindow, QStringLiteral("this"));

        const QString text = editor->property("text").toString();
        const QString header = text.section(QLatin1Char('\n'), 0, 0);
        QCOMPARE(firstTableCellText(header), QStringLiteral("this"));
        QCOMPARE(header.count(QLatin1Char('|')), 3);
        QVERIFY(MarkdownHighlighter::isTableRow(header));

        const int typed = text.indexOf(QStringLiteral("this"));
        QVERIFY(typed >= 0);
        QCOMPARE(editor->property("cursorPosition").toInt(), typed + 4);
    }

    void tableTypingDoesNotRestretchPaddedCell() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *chrome = window->findChild<QObject *>(QStringLiteral("tableChrome"));
        QVERIFY(editor);
        QVERIFY(chrome);

        const QString original = QStringLiteral(
            "|     |     |     |\n"
            "| --- | --- | --- |\n"
            "|     |     |     |\n"
            "|     |     |     |\n");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));
        QTRY_VERIFY(chrome->property("layoutRevision").toInt() >= 0);

        auto *quickDocument = qobject_cast<QQuickTextDocument *>(
            qvariant_cast<QObject *>(editor->property("textDocument")));
        QVERIFY(quickDocument);
        QTextDocument *document = quickDocument->textDocument();
        QVERIFY(document);

        const qreal wrap = editor->property("width").toReal();
        const auto before = TableGeometry::collectTables(document, wrap);
        QCOMPARE(before.size(), 1);
        const QVector<qreal> columns = before.first().columns;
        const int revision = chrome->property("layoutRevision").toInt();
        const qreal boxWidth = before.first().bounds.width();
        QVERIFY2(boxWidth + 40 < wrap,
                 qPrintable(QStringLiteral("empty table filled wrap: box %1 wrap %2")
                            .arg(boxWidth).arg(wrap)));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        for (const QChar ch : QStringLiteral("ab")) {
            QTest::keyClick(quickWindow, static_cast<Qt::Key>(ch.toUpper().unicode()));
            const auto now = TableGeometry::collectTables(document, wrap);
            QCOMPARE(now.size(), 1);
            QCOMPARE(now.first().columns.size(), columns.size());
            for (int i = 0; i < columns.size(); ++i) {
                QVERIFY2(qAbs(now.first().columns.at(i) - columns.at(i)) < 0.5,
                         qPrintable(QStringLiteral("col %1 %2 vs %3")
                                    .arg(i)
                                    .arg(now.first().columns.at(i))
                                    .arg(columns.at(i))));
            }
            QCOMPARE(chrome->property("layoutRevision").toInt(), revision);
        }
    }

    void tableTypingWidestCellRestretchesAtMostOnce() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral(
            "| a | b |\n"
            "| --- | --- |\n"
            "| c | d |\n"));
        document.setTextWidth(800);

        TableChrome chrome;
        chrome.setWrapWidth(800);
        chrome.setTextDocument(&document);
        const int aAt = document.toPlainText().indexOf(QLatin1Char('a'));
        chrome.setCursorPosition(aAt + 1);
        QVERIFY(chrome.caretRect(aAt + 1).height() > 0);
        const auto before = TableGeometry::collectTables(&document, 800);
        QCOMPARE(before.size(), 1);
        const QVector<qreal> columns = before.first().columns;
        const int revision = chrome.layoutRevision();

        QString log = QStringLiteral("rev %1 col0 %2")
                          .arg(revision)
                          .arg(columns.at(1) - columns.at(0));
        QTextCursor cursor(&document);
        int bumps = 0;
        int lastRevision = revision;
        for (const QChar ch : QStringLiteral("bcdef")) {
            cursor.setPosition(document.toPlainText().indexOf(QLatin1Char('a')) + 1);
            cursor.insertText(QString(ch));
            chrome.setCursorPosition(cursor.position());
            const auto now = TableGeometry::collectTables(&document, 800);
            QCOMPARE(now.size(), 1);
            const int nextRevision = chrome.layoutRevision();
            log += QStringLiteral(" | %1 col0 %2 rev %3")
                       .arg(ch)
                       .arg(now.first().columns.at(1) - now.first().columns.at(0))
                       .arg(nextRevision);
            QCOMPARE(now.first().columns.size(), columns.size());
            if (nextRevision != lastRevision) {
                QVERIFY2(nextRevision == lastRevision + 1, qPrintable(log));
                ++bumps;
                lastRevision = nextRevision;
            }
        }
        QVERIFY2(bumps <= 1, qPrintable(log));
    }

    void tableHitTestIgnoresSpacePastLastPipe() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral(
            "| asd | asd | asd |\n"
            "| --- | --- | --- |\n"
            "|     |     |     |\n"));
        document.setTextWidth(1200);

        TableChrome chrome;
        chrome.setWidth(1200);
        chrome.setHeight(200);
        chrome.setWrapWidth(1200);
        chrome.setTextDocument(&document);
        const auto tables = TableGeometry::collectTables(&document, 1200);
        QCOMPARE(tables.size(), 1);
        const qreal lastPipe = tables.first().columns.last();
        const qreal midY = (tables.first().rowEdges.at(0) + tables.first().rowEdges.at(1)) * 0.5;
        const int inside = chrome.hitTest(tables.first().columns.at(0) + 4, midY);
        QVERIFY(inside >= 0);
        const int onLastPipe = chrome.hitTest(lastPipe, midY);
        QVERIFY(onLastPipe >= 0);
        const int past = chrome.hitTest(lastPipe + 48, midY);
        QVERIFY2(past < 0,
                 qPrintable(QStringLiteral("hit %1 past last pipe %2 in wrap 1200")
                            .arg(past).arg(lastPipe)));
    }

    void tableReplaceDoesNotKeepPriorColumnWidths() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral(
            "| wwwwwwwwwwwwwwww | x |\n"
            "| --- | --- |\n"
            "| y | z |\n"));
        document.setTextWidth(800);
        const auto wide = TableGeometry::collectTables(&document, 800);
        QCOMPARE(wide.size(), 1);
        const qreal wideCol = wide.first().columns.at(1) - wide.first().columns.at(0);

        document.setPlainText(QStringLiteral(
            "| a | b |\n"
            "| --- | --- |\n"
            "| c | d |\n"));
        const auto next = TableGeometry::collectTables(&document, 800);
        QCOMPARE(next.size(), 1);
        const qreal nextCol = next.first().columns.at(1) - next.first().columns.at(0);
        QVERIFY2(nextCol + 20 < wideCol,
                 qPrintable(QStringLiteral("replaced table kept prior inner: %1 vs wide %2")
                            .arg(nextCol).arg(wideCol)));
    }

    void tableAppendedRowKeepsAllocatedWidths() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral(
            "| wwwwwwwwwwwwwwww | x |\n"
            "| --- | --- |\n"
            "| y | z |\n"));
        document.setTextWidth(800);
        const auto wide = TableGeometry::collectTables(&document, 800);
        QCOMPARE(wide.size(), 1);
        const qreal wideCol = wide.first().columns.at(1) - wide.first().columns.at(0);

        QTextCursor cursor(&document);
        const int wAt = document.toPlainText().indexOf(QLatin1Char('w'));
        cursor.setPosition(wAt + 1);
        cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 15);
        cursor.removeSelectedText();
        const auto shrunk = TableGeometry::collectTables(&document, 800);
        QCOMPARE(shrunk.size(), 1);
        QVERIFY2(qAbs((shrunk.first().columns.at(1) - shrunk.first().columns.at(0)) - wideCol) < 0.5,
                 QByteArray("delete already dropped allocated inner"));

        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QStringLiteral("|     |     |\n"));
        const auto next = TableGeometry::collectTables(&document, 800);
        QCOMPARE(next.size(), 1);
        QCOMPARE(next.first().rowEdges.size(), wide.first().rowEdges.size() + 1);
        const qreal nextCol = next.first().columns.at(1) - next.first().columns.at(0);
        QVERIFY2(qAbs(nextCol - wideCol) < 0.5,
                 qPrintable(QStringLiteral("appended row snapped inner %1 vs allocated %2")
                            .arg(nextCol).arg(wideCol)));
    }

    void tableGrowRecomputesSiblingRowHeights() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        QString longCell;
        for (int i = 0; i < 24; ++i)
            longCell += QStringLiteral("word ");
        document.setPlainText(QStringLiteral("| a | b |\n| --- | --- |\n| x | ")
                              + longCell.trimmed()
                              + QStringLiteral(" |\n"));
        const QFontMetricsF metrics(font);
        const auto hugged = TableGeometry::collectTables(&document, 2000);
        QCOMPARE(hugged.size(), 1);
        const qreal cap = hugged.first().bounds.width() + document.documentMargin() + 2;
        document.setTextWidth(cap);

        TableChrome chrome;
        chrome.setWrapWidth(cap);
        chrome.setTextDocument(&document);
        const int aAt = document.toPlainText().indexOf(QLatin1Char('a'));
        chrome.setCursorPosition(aAt + 1);
        QVERIFY(chrome.caretRect(aAt + 1).height() > 0);

        QTextCursor cursor(&document);
        for (const QChar ch : QStringLiteral("bcdefghij")) {
            cursor.setPosition(document.toPlainText().indexOf(QLatin1Char('a')) + 1);
            cursor.insertText(QString(ch));
            chrome.setCursorPosition(cursor.position());
        }

        const auto geoms = TableGeometry::geometriesFor(&document, cap, cursor.position());
        QCOMPARE(geoms.size(), 1);
        const TableGeometry::Table &table = geoms.first();
        const TableGeometry::Cell *body = nullptr;
        for (const TableGeometry::Cell &cell : table.cells) {
            if (cell.row == 1 && cell.column == 1)
                body = &cell;
        }
        QVERIFY(body);
        QTextLayout needed;
        TableGeometry::prepareCellLayout(&needed, body->text, font,
                                         qMax(qreal(1), body->textRect.width()));
        QVERIFY2(table.rowHeights.at(1) + 0.5 >= needed.boundingRect().height(),
                 qPrintable(QStringLiteral("stale body height %1 needed %2 inner %3")
                            .arg(table.rowHeights.at(1))
                            .arg(needed.boundingRect().height())
                            .arg(body->textRect.width())));
    }

    void tableTypingKeepsSpaces() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString original = QStringLiteral("|     |     |\n| --- | --- |\n|     |     |");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        typeIntoWindow(quickWindow, QStringLiteral("hello world"));

        const QString header = editor->property("text").toString()
                                   .section(QLatin1Char('\n'), 0, 0);
        QCOMPARE(firstTableCellText(header), QStringLiteral("hello world"));
        QVERIFY2(!header.contains(QStringLiteral("helloworld")), qPrintable(header));
    }

    void tableBackspaceDeletesInsideCell() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString original = QStringLiteral("|     |     |\n| --- | --- |\n|     |     |");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        typeIntoWindow(quickWindow, QStringLiteral("ab"));
        QTest::keyClick(quickWindow, Qt::Key_Backspace);

        const QString header = editor->property("text").toString()
                                   .section(QLatin1Char('\n'), 0, 0);
        QCOMPARE(firstTableCellText(header), QStringLiteral("a"));
        QVERIFY2(!header.contains(QChar(8)), qPrintable(header.toUtf8().toHex()));
    }

    void tableBackspaceDeletesTrailingSpace() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString original = QStringLiteral("|     |     |\n| --- | --- |\n|     |     |");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        typeIntoWindow(quickWindow, QStringLiteral("hello "));

        const QString before = editor->property("text").toString()
                                   .section(QLatin1Char('\n'), 0, 0);
        const QString rawBefore = firstTableCellRaw(before);
        QCOMPARE(firstTableCellText(before), QStringLiteral("hello"));
        const int spacesBefore = spacesAfterWord(rawBefore, QStringLiteral("hello"));
        QVERIFY(spacesBefore >= 1);

        QTest::keyClick(quickWindow, Qt::Key_Backspace);

        const QString header = editor->property("text").toString()
                                   .section(QLatin1Char('\n'), 0, 0);
        const QString raw = firstTableCellRaw(header);
        // The bug deleted the last letter and left the space ("hell ").
        QCOMPARE(firstTableCellText(header), QStringLiteral("hello"));
        QCOMPARE(spacesAfterWord(raw, QStringLiteral("hello")), spacesBefore - 1);
    }

    void tableDeleteRemovesTrailingSpace() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString original = QStringLiteral("|     |     |\n| --- | --- |\n|     |     |");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        typeIntoWindow(quickWindow, QStringLiteral("hello "));

        const QString typed = editor->property("text").toString();
        const QString before = typed.section(QLatin1Char('\n'), 0, 0);
        const QString rawBefore = firstTableCellRaw(before);
        const int spacesBefore = spacesAfterWord(rawBefore, QStringLiteral("hello"));
        QVERIFY(spacesBefore >= 1);
        const int hello = typed.indexOf(QStringLiteral("hello"));
        QVERIFY(hello >= 0);
        editor->setProperty("cursorPosition", hello + 5);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));
        QTest::keyClick(quickWindow, Qt::Key_Delete);

        const QString header = editor->property("text").toString()
                                   .section(QLatin1Char('\n'), 0, 0);
        const QString raw = firstTableCellRaw(header);
        QCOMPARE(firstTableCellText(header), QStringLiteral("hello"));
        QCOMPARE(spacesAfterWord(raw, QStringLiteral("hello")), spacesBefore - 1);
    }

    void tableTypingDoesNotInsertPhantomSpace() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString original = QStringLiteral("|     |     |\n| --- | --- |\n|     |     |");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        typeIntoWindow(quickWindow, QStringLiteral("This is a test ofhow"));

        const QString header = editor->property("text").toString()
                                   .section(QLatin1Char('\n'), 0, 0);
        QCOMPARE(firstTableCellText(header), QStringLiteral("this is a test ofhow"));
        QVERIFY2(!header.contains(QStringLiteral("of how")), qPrintable(header));
    }

    void tableTypingKeepsSingleSpaceBeforeHow() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString original = QStringLiteral("|     |     |\n| --- | --- |\n|     |     |");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        typeIntoWindow(quickWindow, QStringLiteral("This is a test of how"));

        const QString header = editor->property("text").toString()
                                   .section(QLatin1Char('\n'), 0, 0);
        const QString raw = firstTableCellRaw(header);
        QCOMPARE(firstTableCellText(header), QStringLiteral("this is a test of how"));
        QVERIFY2(raw.contains(QStringLiteral("of how")), qPrintable(raw));
        QVERIFY2(!raw.contains(QStringLiteral("of  how")), qPrintable(raw));
    }

    void tableTypingKeepsDoubleSpaceBeforeHow() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString original = QStringLiteral("|     |     |\n| --- | --- |\n|     |     |");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        typeIntoWindow(quickWindow, QStringLiteral("This is a test of  how"));

        const QString header = editor->property("text").toString()
                                   .section(QLatin1Char('\n'), 0, 0);
        const QString raw = firstTableCellRaw(header);
        QCOMPARE(firstTableCellText(header), QStringLiteral("this is a test of  how"));
        QVERIFY2(raw.contains(QStringLiteral("of  how")), qPrintable(raw));
        QVERIFY2(!raw.contains(QStringLiteral("of   how")), qPrintable(raw));
    }

    void tableCaretFollowsTrailingSpaces() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        auto *chrome = window->findChild<TableChrome *>(QStringLiteral("tableChrome"));
        QVERIFY(chrome);
        const QString original = QStringLiteral("|     |     |\n| --- | --- |\n|     |     |");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        typeIntoWindow(quickWindow, QStringLiteral("hello"));
        QTest::keyClick(quickWindow, Qt::Key_Space);
        const QString afterOne = editor->property("text").toString();
        const int hello = afterOne.indexOf(QStringLiteral("hello"));
        QVERIFY(hello >= 0);
        const QRectF atWord = chrome->caretRect(hello + 5);
        const QRectF atSpace = chrome->caretRect(editor->property("cursorPosition").toInt());
        QVERIFY(atWord.height() > 0);
        QVERIFY2(atSpace.height() > 0, "caret vanished after one trailing space");
        QVERIFY2(atSpace.x() > atWord.x() + 0.5,
                 qPrintable(QStringLiteral("space caret %1 vs word caret %2")
                            .arg(atSpace.x()).arg(atWord.x())));

        QTest::keyClick(quickWindow, Qt::Key_Space);
        const QRectF atTwo = chrome->caretRect(editor->property("cursorPosition").toInt());
        const QRectF atOneAfter = chrome->caretRect(hello + 6);
        QVERIFY2(atTwo.height() > 0, "caret vanished after two trailing spaces");
        QVERIFY2(atTwo.x() > atOneAfter.x() + 0.5,
                 qPrintable(QStringLiteral("two-space caret %1 vs one-space caret %2")
                            .arg(atTwo.x()).arg(atOneAfter.x())));
    }

    void tableEnterDoesNotSplitRow() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        editor->setProperty("text", QStringLiteral("| a | b |\n| --- | --- |\n|     |     |"));
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_Return);

        const QStringList lines = editor->property("text").toString().split(QLatin1Char('\n'));
        QVERIFY(lines.size() >= 3);
        for (const QString &line : lines) {
            if (line.isEmpty())
                continue;
            QVERIFY2(MarkdownHighlighter::isTableRow(line), qPrintable(line));
        }
    }

    void tableUndoDoesNotDropTable() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString original = QStringLiteral("| a | b |\n| --- | --- |\n|     |     |");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_Z);
        const QString typed = editor->property("text").toString();
        QVERIFY(typed.contains(QLatin1Char('z')));
        QTest::keyClick(quickWindow, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->property("text").toString(), original);
    }

    void tableOpenDoesNotRewriteRaggedSource() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString ragged = QStringLiteral(
            "| a | b |\n| - | --- |\n| longcell | x |");
        editor->setProperty("text", ragged);
        QTRY_COMPARE(editor->property("text").toString(), ragged);
    }

    void tableLongCellKeepsEditorWidth() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *chrome = window->findChild<QObject *>(QStringLiteral("tableChrome"));
        QVERIFY(editor);
        QVERIFY(chrome);

        const qreal columnWidth = editor->property("width").toReal();
        QVERIFY(columnWidth > 1);
        const QString longCell(90, QLatin1Char('w'));
        editor->setProperty("text",
                            QStringLiteral("| ") + longCell
                            + QStringLiteral(" | x |\n| --- | --- |\n| y | z |\n"));
        QTRY_VERIFY(chrome->property("naturalWidth").toReal() > columnWidth + 8);
        QVERIFY2(qAbs(editor->property("width").toReal() - columnWidth) < 1.0,
                 qPrintable(QStringLiteral("editor width %1 column %2")
                            .arg(editor->property("width").toReal()).arg(columnWidth)));

        auto *quickDocument = qobject_cast<QQuickTextDocument *>(
            qvariant_cast<QObject *>(editor->property("textDocument")));
        QVERIFY(quickDocument);
        QTextDocument *document = quickDocument->textDocument();
        QVERIFY(document);
        const auto tables = TableGeometry::collectTables(document, columnWidth);
        QCOMPARE(tables.size(), 1);
        QVERIFY(tables.first().rowEdges.size() >= 3);
        const qreal headerH = tables.first().rowEdges.at(1) - tables.first().rowEdges.at(0);
        const qreal bodyH = tables.first().rowEdges.at(2) - tables.first().rowEdges.at(1);
        QVERIFY2(headerH > bodyH + 4,
                 qPrintable(QStringLiteral("header %1 body %2")
                            .arg(headerH).arg(bodyH)));
    }

    void parseTableLineKeepsEmptyCells() {
        const auto parsed = MarkdownHighlighter::parseTableLine(
            QStringLiteral("| a || b |"));
        QCOMPARE(parsed.cells.size(), 3);
        QCOMPARE(parsed.cells.at(1).length, 0);

        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral("| a || b |\n| --- | --- |\n| c || d |\n"));
        document.setTextWidth(2000);
        const auto tables = TableGeometry::collectTables(&document, 2000);
        QCOMPARE(tables.size(), 1);
        QCOMPARE(tables.first().columns.size(), 4);
    }

    void tableCaretStaysInsideEmptyCell() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        const QString text = QStringLiteral("| a | b |\n| --- | --- |\n|     |     |\n");
        document.setPlainText(text);
        document.setTextWidth(400);
        TableChrome chrome;
        chrome.setTextDocument(&document);
        chrome.setWrapWidth(400);

        const auto tables = TableGeometry::collectTables(&document, 400);
        QCOMPARE(tables.size(), 1);
        const QVector<qreal> edges = tables.first().rowEdges;
        QVERIFY(edges.size() >= 3);
        const qreal rowTop = edges.at(1);
        const qreal rowBottom = edges.at(2);

        const int emptyCell = text.indexOf(QStringLiteral("|     |")) + 2;
        QVERIFY(chrome.positionInTable(emptyCell));
        const QRectF caret = chrome.caretRect(emptyCell);
        QVERIFY(caret.height() > 0);
        QVERIFY2(caret.top() >= rowTop - 0.5 && caret.bottom() <= rowBottom + 0.5,
                 qPrintable(QStringLiteral("caret %1..%2 outside row %3..%4")
                            .arg(caret.top()).arg(caret.bottom())
                            .arg(rowTop).arg(rowBottom)));
        QVERIFY2(caret.height() < rowBottom - rowTop,
                 qPrintable(QStringLiteral("caret %1 vs row %2")
                            .arg(caret.height()).arg(rowBottom - rowTop)));

        // Filled cells centre a single text line; the empty cell's caret must
        // sit on the same line as its neighbours, not on the row midpoint.
        const QRectF filled = chrome.caretRect(text.indexOf(QLatin1Char('a')));
        const qreal headerTop = edges.at(0);
        QVERIFY2(qAbs((caret.top() - rowTop) - (filled.top() - headerTop)) <= 1.0,
                 qPrintable(QStringLiteral("empty offset %1 vs filled offset %2")
                            .arg(caret.top() - rowTop).arg(filled.top() - headerTop)));
    }

    void tableCaretAtPipeHidesPadding() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        const QString text = QStringLiteral("| hello     |\n| --- | --- |\n|     |     |\n");
        document.setPlainText(text);
        document.setTextWidth(400);
        TableChrome chrome;
        chrome.setTextDocument(&document);
        chrome.setWrapWidth(400);

        const int hello = text.indexOf(QStringLiteral("hello"));
        QVERIFY(hello >= 0);
        const int pipe = text.indexOf(QLatin1Char('|'), hello);
        QVERIFY(pipe > hello);

        chrome.setCursorPosition(hello + 5);
        const QRectF atWord = chrome.caretRect(hello + 5);
        chrome.setCursorPosition(hello + 6);
        const QRectF atPad = chrome.caretRect(hello + 6);
        chrome.setCursorPosition(pipe);
        const QRectF atPipe = chrome.caretRect(pipe);

        QVERIFY(atWord.height() > 0);
        QVERIFY(atPad.height() > 0);
        QVERIFY(atPipe.height() > 0);
        QVERIFY2(atPad.x() > atWord.x() + 0.5,
                 qPrintable(QStringLiteral("pad caret %1 vs word caret %2")
                            .arg(atPad.x()).arg(atWord.x())));
        QVERIFY2(qAbs(atPipe.x() - atWord.x()) <= 1.0,
                 qPrintable(QStringLiteral("pipe caret %1 vs word caret %2")
                            .arg(atPipe.x()).arg(atWord.x())));
    }

    void tableCaretAtUnclosedCellShowsTrailingSpace() {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        const QString text = QStringLiteral("| a | hello  \n| --- | --- |\n|     |     |\n");
        document.setPlainText(text);
        document.setTextWidth(400);
        TableChrome chrome;
        chrome.setTextDocument(&document);
        chrome.setWrapWidth(400);

        const int hello = text.indexOf(QStringLiteral("hello"));
        QVERIFY(hello >= 0);
        const int lineEnd = text.indexOf(QLatin1Char('\n'));
        QVERIFY(lineEnd > hello);
        QVERIFY(text.at(lineEnd - 1) != QLatin1Char('|'));

        chrome.setCursorPosition(hello + 5);
        const QRectF atWord = chrome.caretRect(hello + 5);
        chrome.setCursorPosition(lineEnd);
        const QRectF atEnd = chrome.caretRect(lineEnd);

        QVERIFY(atWord.height() > 0);
        QVERIFY2(atEnd.height() > 0, "caret vanished at unclosed cell end");
        QVERIFY2(atEnd.x() > atWord.x() + 0.5,
                 qPrintable(QStringLiteral("end caret %1 vs word caret %2")
                            .arg(atEnd.x()).arg(atWord.x())));
    }

    void tablePipeAppendsCell() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString original = QStringLiteral("| a | b |\n| --- | --- |\n| c | d |");
        editor->setProperty("text", original);
        editor->setProperty("cursorPosition", original.indexOf(QLatin1Char('b')) + 1);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);

        // A pipe in the middle of a cell is dropped rather than splitting it.
        editor->setProperty("cursorPosition", original.indexOf(QLatin1Char('b')));
        QTest::keyClick(quickWindow, Qt::Key_Bar);
        QCOMPARE(editor->property("text").toString(), original);

        // At the end of the last cell it grows the table by one column.
        editor->setProperty("cursorPosition", original.indexOf(QLatin1Char('b')) + 1);
        QTest::keyClick(quickWindow, Qt::Key_Bar);
        QStringList lines = editor->property("text").toString().split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines.at(0).count(QLatin1Char('|')), 4);
        QCOMPARE(lines.at(1).count(QLatin1Char('|')), 4);
        QCOMPARE(lines.at(1).count(QStringLiteral("---")), 3);
        QVERIFY(MarkdownHighlighter::isTableSeparator(lines.at(1)));
        QCOMPARE(lines.at(2), QStringLiteral("| c | d |"));

        // The caret lands in the new cell, so typing fills it.
        QTest::keyClick(quickWindow, Qt::Key_X);
        lines = editor->property("text").toString().split(QLatin1Char('\n'));
        QCOMPARE(MarkdownHighlighter::parseTableLine(lines.at(0)).cells.size(), 3);
        QVERIFY2(lines.at(0).contains(QStringLiteral("| x")), qPrintable(lines.at(0)));
        QVERIFY(lines.at(0).startsWith(QStringLiteral("| a")));

        // At the end of an inner cell the pipe hops to the next cell.
        const QString grown = editor->property("text").toString();
        editor->setProperty("cursorPosition", grown.indexOf(QLatin1Char('a')) + 1);
        QTest::keyClick(quickWindow, Qt::Key_Bar);
        QCOMPARE(editor->property("text").toString(), grown);
        QCOMPARE(editor->property("cursorPosition").toInt(), grown.indexOf(QLatin1Char('b')));
    }

    void tableArrowKeysLeaveTable() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString text = QStringLiteral(
            "above\n| a | b |\n| --- | --- |\n| c | d |\nbelow");
        editor->setProperty("text", text);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));
        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);

        editor->setProperty("cursorPosition", text.indexOf(QLatin1Char('c')));
        QTest::keyClick(quickWindow, Qt::Key_Down);
        QVERIFY(editor->property("cursorPosition").toInt() >= text.indexOf(QStringLiteral("below")));

        editor->setProperty("cursorPosition", text.indexOf(QLatin1Char('a')));
        QTest::keyClick(quickWindow, Qt::Key_Up);
        QVERIFY(editor->property("cursorPosition").toInt() < text.indexOf(QLatin1Char('|')));
    }

    void tableChromeIgnoresCaretOutsideTable() {
        QTextDocument document;
        document.setPlainText(QStringLiteral(
            "hello\n\n| a | b |\n| --- | --- |\n| c | d |\n\nmore\n"));
        document.setTextWidth(400);
        TableChrome chrome;
        chrome.setTextDocument(&document);
        chrome.setWrapWidth(400);

        QVERIFY(!chrome.positionInTable(0));
        QCOMPARE(chrome.movePositionVertically(0, 1), -1);
        const int more = document.toPlainText().indexOf(QStringLiteral("more"));
        QVERIFY(more >= 0);
        QVERIFY(!chrome.positionInTable(more));
        QVERIFY(chrome.caretRect(more).isEmpty());
        QCOMPARE(chrome.movePositionVertically(more, -1), -1);
        QVERIFY(chrome.positionInTable(document.toPlainText().indexOf(QLatin1Char('a'))));
    }

    void tableWrapWidthChangeKeepsTypingUndo() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString longCell(80, QLatin1Char('w'));
        editor->setProperty("text",
                            QStringLiteral("| ") + longCell
                            + QStringLiteral(" | x |\n| --- | --- |\n| y | z |\n"));
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTest::keyClick(quickWindow, Qt::Key_A);
        QVERIFY(editor->property("text").toString().contains(QLatin1Char('a')));

        backend.setTableWrapWidth(160);
        QTest::keyClick(quickWindow, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY2(!editor->property("text").toString().contains(QLatin1Char('a')),
                 "wrap-width restretch must not intercept the typing undo command");
    }

    void windowResizeDoesNotInterceptTypingUndo() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString longCell(80, QLatin1Char('w'));
        editor->setProperty("text",
                            QStringLiteral("| ") + longCell
                            + QStringLiteral(" | x |\n| --- | --- |\n| y | z |\n"));
        editor->setProperty("cursorPosition", 2);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QTRY_VERIFY(quickWindow->width() > 1000);

        auto *quickDocument = qobject_cast<QQuickTextDocument *>(
            qvariant_cast<QObject *>(editor->property("textDocument")));
        QVERIFY(quickDocument);
        QTextDocument *document = quickDocument->textDocument();
        QVERIFY(document);

        QTest::keyClick(quickWindow, Qt::Key_A);
        QVERIFY(editor->property("text").toString().contains(QLatin1Char('a')));
        const qreal heightBefore = document->begin().blockFormat().lineHeight();

        quickWindow->resize(720, quickWindow->height());
        QTRY_VERIFY(editor->property("width").toReal() < 800);
        QTRY_VERIFY(document->begin().blockFormat().lineHeight() > heightBefore + 4);

        QTest::keyClick(quickWindow, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY2(!editor->property("text").toString().contains(QLatin1Char('a')),
                 "window-resize wrap restretch must not intercept the typing undo command");
        QTest::keyClick(quickWindow, Qt::Key_Y, Qt::ControlModifier);
        QVERIFY2(editor->property("text").toString().contains(QLatin1Char('a')),
                 "wrap restretch after undo must not discard redo");
        QTest::keyClick(quickWindow, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY2(!editor->property("text").toString().contains(QLatin1Char('a')),
                 "wrap restretch after redo must not become its own undo step");

        quickWindow->resize(1280, 820);
        QTRY_COMPARE(quickWindow->width(), 1280);
    }

    void tableChromeTextureFollowsCanvasZoom() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *chrome = window->findChild<QObject *>(QStringLiteral("tableChrome"));
        QObject *canvas = window->findChild<QObject *>(QStringLiteral("editorCanvas"));
        QVERIFY(chrome);
        QVERIFY(canvas);
        QCOMPARE(chrome->property("viewScale").toReal(), 1.0);

        backend.zoomToPercent(70);
        QCOMPARE(backend.zoomFactor(), 0.7);
        QTRY_COMPARE(canvas->property("scale").toReal(), 0.7);
        QTRY_COMPARE(chrome->property("viewScale").toReal(), 0.7);
        QTRY_VERIFY(chrome->property("width").toReal() > 1);
        QTRY_COMPARE(chrome->property("textureSize").toSize().width(),
                     qMax(1, qRound(chrome->property("width").toReal() * 0.7)));
        QTRY_COMPARE(chrome->property("textureSize").toSize().height(),
                     qMax(1, qRound(chrome->property("height").toReal() * 0.7)));

        backend.zoomToPercent(90);
        QCOMPARE(backend.zoomFactor(), 0.9);
        QTRY_COMPARE(chrome->property("viewScale").toReal(), 0.9);
        QTRY_COMPARE(chrome->property("textureSize").toSize().width(),
                     qMax(1, qRound(chrome->property("width").toReal() * 0.9)));

        backend.zoomToPercent(300);
        QCOMPARE(backend.zoomFactor(), 3.0);
        QTRY_COMPARE(chrome->property("viewScale").toReal(), 3.0);
        QTRY_COMPARE(chrome->property("textureSize").toSize().width(),
                     qMax(1, qRound(chrome->property("width").toReal() * 3.0)));

        backend.resetZoom();
        QTRY_COMPARE(chrome->property("viewScale").toReal(), 1.0);
        QTRY_COMPARE(chrome->property("textureSize").toSize().width(),
                     qMax(1, qRound(chrome->property("width").toReal())));
    }

    void shortDocumentDoesNotPhantomScroll() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *flick = window->findChild<QObject *>(QStringLiteral("editorFlick"));
        QVERIFY(flick);
        QVERIFY(flick->property("contentHeight").toReal()
                <= flick->property("height").toReal() + 1);

        backend.resetZoom();
    }

    void zoomPercentLabelTracksCanvasZoom() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *zoomLabel = window->findChild<QObject *>(QStringLiteral("zoomPercentLabel"));
        QVERIFY(zoomLabel);
        QCOMPARE(zoomLabel->property("visible").toBool(), false);

        backend.zoomIn();
        QCOMPARE(zoomLabel->property("visible").toBool(), true);
        QCOMPARE(zoomLabel->property("text").toString(), QStringLiteral("110%"));

        backend.resetZoom();
        QCOMPARE(zoomLabel->property("visible").toBool(), false);

        backend.zoomOut();
        QCOMPARE(zoomLabel->property("visible").toBool(), true);
        QCOMPARE(zoomLabel->property("text").toString(), QStringLiteral("90%"));

        backend.resetZoom();
    }

    void zoomChordsKeepCaretInViewAfterWrapReflow() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *flick = window->findChild<QObject *>(QStringLiteral("editorFlick"));
        QVERIFY(editor);
        QVERIFY(flick);

        QString paragraph;
        for (int i = 0; i < 400; ++i) {
            if (i)
                paragraph += QLatin1Char(' ');
            paragraph += QStringLiteral("word");
        }
        editor->setProperty("text", paragraph.repeated(8));
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));
        editor->setProperty("cursorPosition", editor->property("text").toString().size() / 2);

        QTRY_VERIFY(editor->property("cursorRectangle").toRectF().y() > 0);

        const qreal beforeY = editor->property("cursorRectangle").toRectF().y();
        for (int i = 0; i < 20; ++i)
            QVERIFY(QMetaObject::invokeMethod(window.data(), "zoomIn"));
        QCOMPARE(backend.zoomFactor(), 3.0);

        const QRectF caret = editor->property("cursorRectangle").toRectF();
        QVERIFY2(caret.y() > beforeY + 20,
                 qPrintable(QStringLiteral("wrap did not reflow caret y %1 -> %2")
                            .arg(beforeY)
                            .arg(caret.y())));

        const qreal zoom = backend.zoomFactor();
        const qreal canvasY = window->findChild<QObject *>(QStringLiteral("editorCanvas"))
                                  ->property("y").toReal();
        const qreal caretTop = canvasY + caret.y() * zoom;
        const qreal caretBottom = canvasY + (caret.y() + caret.height()) * zoom;
        const qreal contentY = flick->property("contentY").toReal();
        const qreal viewHeight = flick->property("height").toReal();
        QVERIFY2(caretBottom + 1 >= contentY && caretTop <= contentY + viewHeight + 1,
                 qPrintable(QStringLiteral(
                                "caret [%1, %2] outside view contentY=%3 height=%4")
                                .arg(caretTop)
                                .arg(caretBottom)
                                .arg(contentY)
                                .arg(viewHeight)));

        backend.resetZoom();
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

private:
    static int colorDistance(const QColor &left, const QColor &right) {
        return qAbs(left.red() - right.red())
            + qAbs(left.green() - right.green())
            + qAbs(left.blue() - right.blue());
    }

    static bool nearColor(const QColor &pixel, const QColor &expected, int slack = 36) {
        return colorDistance(pixel, expected) <= slack;
    }

    bool assertTableBoxPixels(const QString &markdown, int expectedPipes,
                              qreal scale = 1) {
        QTextDocument document;
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPixelSize(16);
        document.setDefaultFont(font);
        document.setPlainText(markdown);
        document.setTextWidth(2000);

        MarkdownHighlighter highlighter(&document);
        const QColor paper(QStringLiteral("#1a2744"));
        const QColor text(QStringLiteral("#eeeeee"));
        const QColor rule = text;
        highlighter.setColors(paper.name(), text.name(), QStringLiteral("#5584aa"));
        highlighter.rehighlight();

        QTextCursor cursor(&document);
        for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
            QTextBlockFormat format = block.blockFormat();
            format.setTopMargin(0);
            format.setBottomMargin(0);
            if (MarkdownHighlighter::isTableSeparator(block.text())) {
                format.setLineHeight(MarkdownHighlighter::tableSeparatorLineHeight,
                                     QTextBlockFormat::FixedHeight);
                format.setNonBreakableLines(true);
            } else if (MarkdownHighlighter::isTableRow(block.text())) {
                format.setLineHeight(MarkdownHighlighter::tableDataRowLineHeight(font),
                                     QTextBlockFormat::MinimumHeight);
                format.setNonBreakableLines(true);
            } else {
                format.setLineHeight(140, QTextBlockFormat::ProportionalHeight);
                format.setNonBreakableLines(false);
            }
            cursor.setPosition(block.position());
            cursor.setBlockFormat(format);
        }
        document.setTextWidth(2000);

        QImage surface(1400, 240, QImage::Format_ARGB32_Premultiplied);
        surface.fill(paper);
        QPainter painter(&surface);
        if (!qFuzzyCompare(scale, qreal(1)))
            painter.scale(scale, scale);
        TableChrome::paintTables(&painter, &document, paper, text, rule);
        if (qFuzzyCompare(scale, qreal(1)))
            document.drawContents(&painter);
        painter.end();

        const auto device = [scale](qreal itemPos) {
            return qRound(itemPos * scale);
        };

        const auto formatAt = [&document](int position) {
            const QTextBlock block = document.findBlock(position);
            const int local = position - block.position();
            QTextCharFormat format;
            const auto ranges = block.layout()->formats();
            for (const QTextLayout::FormatRange &range : ranges) {
                if (local >= range.start && local < range.start + range.length)
                    format = range.format;
            }
            return format;
        };

        const QTextBlock header = document.begin();
        if (!header.isValid() || !header.layout() || header.layout()->lineCount() <= 0) {
            qWarning("table header layout is missing");
            return false;
        }
        // Source glyphs of table rows must take no space: TextEdit repaints
        // selected glyphs in selectedTextColor, so any advance would let the
        // raw pipes show through the overlay when a table is selected.
        const QTextLine headerLine = header.layout()->lineAt(0);
        const qreal sourceWidth = headerLine.naturalTextWidth();
        if (sourceWidth > 1 || formatAt(0).foreground().color().alpha() != 0) {
            qWarning("table source still has width %f", sourceWidth);
            return false;
        }
        if (formatAt(0).background().style() != Qt::NoBrush
                || formatAt(2).background().style() != Qt::NoBrush) {
            qWarning("highlighter still paints table cell fills");
            return false;
        }

        const QTextBlock separator = header.next();
        if (!separator.isValid()
                || separator.blockFormat().lineHeightType() != QTextBlockFormat::FixedHeight
                || separator.blockFormat().lineHeight()
                    != MarkdownHighlighter::tableSeparatorLineHeight) {
            qWarning("separator was not collapsed to a hairline");
            return false;
        }

        const auto tables = TableGeometry::collectTables(&document);
        if (tables.size() != 1) {
            qWarning("expected 1 table, got %lld", static_cast<long long>(tables.size()));
            return false;
        }
        const TableGeometry::Box box = tables.first();
        if (box.columns.size() != expectedPipes || !box.header.isValid()) {
            qWarning("columns %lld (want %d) header valid %d",
                     static_cast<long long>(box.columns.size()), expectedPipes,
                     int(box.header.isValid()));
            return false;
        }

        const int midX = device((box.columns.at(0) + box.columns.at(1)) * 0.5);
        const int headerY = device(box.header.top()) + 2;
        const int ruleY = device(box.header.center().y());
        if (midX < 0 || headerY < 0 || midX >= surface.width() || headerY >= surface.height()) {
            qWarning("header sample %d,%d is outside the surface", midX, headerY);
            return false;
        }
        const QColor headerPixel = surface.pixelColor(midX, headerY);
        if (!nearColor(headerPixel, paper) && !nearColor(headerPixel, text)) {
            qWarning("header cell %s is not paper or text",
                     qPrintable(headerPixel.name()));
            return false;
        }

        for (qreal column : box.columns) {
            const int pipeX = device(column);
            const QColor pipePixel = surface.pixelColor(pipeX, ruleY);
            if (!nearColor(pipePixel, rule)) {
                qWarning("gutter %s is not a rule %s at scale %f col %f pipeX %d ruleY %d",
                         qPrintable(pipePixel.name()), qPrintable(rule.name()), scale,
                         column, pipeX, ruleY);
                return false;
            }
        }

        const int left = device(box.bounds.left());
        const int right = device(box.bounds.right());
        const int top = device(box.bounds.top());
        const int bottom = device(box.bounds.bottom());
        const QColor leftEdge = surface.pixelColor(left, ruleY);
        const QColor rightEdge = surface.pixelColor(right, ruleY);
        const QColor topEdge = surface.pixelColor(midX, top);
        const QColor bottomEdge = surface.pixelColor(midX, bottom);
        if (!nearColor(leftEdge, rule) || !nearColor(rightEdge, rule)
                || !nearColor(topEdge, rule) || !nearColor(bottomEdge, rule)) {
            qWarning("box edges missing: L %s R %s T %s B %s vs rule %s",
                     qPrintable(leftEdge.name()), qPrintable(rightEdge.name()),
                     qPrintable(topEdge.name()), qPrintable(bottomEdge.name()),
                     qPrintable(rule.name()));
            return false;
        }

        const int fromY = device(box.header.top());
        const int toY = device(box.bounds.bottom());
        int longestRuleRun = 0;
        int currentRule = 0;
        for (int y = fromY; y <= toY; ++y) {
            if (nearColor(surface.pixelColor(midX, y), rule, 18)) {
                ++currentRule;
                longestRuleRun = qMax(longestRuleRun, currentRule);
            } else {
                currentRule = 0;
            }
        }
        if (longestRuleRun > 4) {
            qWarning("separator band is %dpx, not a hairline", longestRuleRun);
            return false;
        }

        if (box.rowEdges.size() >= 3) {
            const qreal headerGap = box.rowEdges.at(1) - box.rowEdges.at(0);
            const qreal bodyGap = box.rowEdges.at(2) - box.rowEdges.at(1);
            if (headerGap < 1 || qAbs(headerGap - bodyGap) > 1.0) {
                qWarning("row gaps unequal: header %f body %f at scale %f",
                         headerGap, bodyGap, scale);
                return false;
            }
            const int deviceHeaderGap = device(box.rowEdges.at(1)) - device(box.rowEdges.at(0));
            const int deviceBodyGap = device(box.rowEdges.at(2)) - device(box.rowEdges.at(1));
            if (qAbs(deviceHeaderGap - deviceBodyGap) > 1) {
                qWarning("device row gaps unequal: header %d body %d at scale %f",
                         deviceHeaderGap, deviceBodyGap, scale);
                return false;
            }

            const int bodyY = device((box.rowEdges.at(1) + box.rowEdges.at(2)) * 0.5);
            const QColor bodyPixel = surface.pixelColor(midX, bodyY);
            if (!nearColor(bodyPixel, paper)) {
                qWarning("body cell %s is not paper", qPrintable(bodyPixel.name()));
                return false;
            }
            if (nearColor(bodyPixel, rule) && !nearColor(paper, rule)) {
                qWarning("body cell was swallowed by a slab");
                return false;
            }
        }

        return MarkdownHighlighter::parseTableLine(header.text()).hidden.size()
            == expectedPipes;
    }

    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmaOwnNoteTest)
#include "tst_oma_own_note.moc"
