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
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include "backend.h"
#include "markdownhighlighter.h"
#include "tablechrome.h"
#include "viewzoom.h"

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
        QVERIFY(qFuzzyIsNull(formatAt(0).fontPointSize()) || formatAt(0).fontPointSize() > 8);
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

        const auto tables = TableChrome::collectTables(&document);
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
        QCOMPARE(TableChrome::collectTables(&fencedOnly).size(), 0);
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
        QVERIFY(originX > 0);

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
        QVERIFY(originX > 0);

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

        const auto tables = TableChrome::collectTables(document);
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
        const auto tables = TableChrome::collectTables(
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

        const auto tables = TableChrome::collectTables(&document, 220);
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
        const auto before = TableChrome::collectTables(&document, 2000);
        QCOMPARE(before.size(), 1);
        const qreal shortCol = before.first().columns.at(1) - before.first().columns.at(0);

        document.setPlainText(QStringLiteral(
            "| hellohello | a |\n"
            "| ---------- | - |\n"
            "| hi         | b |\n"));
        const auto after = TableChrome::collectTables(&document, 2000);
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
        const auto tables = TableChrome::collectTables(document, columnWidth);
        QCOMPARE(tables.size(), 1);
        QVERIFY(tables.first().rowEdges.size() >= 3);
        const qreal headerH = tables.first().rowEdges.at(1) - tables.first().rowEdges.at(0);
        const qreal bodyH = tables.first().rowEdges.at(2) - tables.first().rowEdges.at(1);
        QVERIFY2(headerH > bodyH + 4,
                 qPrintable(QStringLiteral("header %1 body %2")
                            .arg(headerH).arg(bodyH)));
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
        const QTextLine headerLine = header.layout()->lineAt(0);
        const qreal pipeWidth = headerLine.cursorToX(1) - headerLine.cursorToX(0);
        const qreal em = QFontMetricsF(font).horizontalAdvance(QLatin1Char('|'));
        if (pipeWidth <= em * 0.6) {
            qWarning("pipe width %f vs em %f", pipeWidth, em);
            return false;
        }
        if (!(qFuzzyIsNull(formatAt(0).fontPointSize()) || formatAt(0).fontPointSize() > 8)) {
            qWarning("pipe font was collapsed");
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

        const auto tables = TableChrome::collectTables(&document);
        if (tables.size() != 1) {
            qWarning("expected 1 table, got %lld", static_cast<long long>(tables.size()));
            return false;
        }
        const TableChrome::TableBox box = tables.first();
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
