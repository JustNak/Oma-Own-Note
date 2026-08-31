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
#include <QQuickWindow>
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
        QCOMPARE(editor->property("nextCellStart").toInt(), 12);
        QCOMPARE(editor->property("dividerText").toString(), QStringLiteral("---\n"));
        QCOMPARE(editor->property("dividerCursor").toInt(), 4);
        QCOMPARE(editor->property("dividerMidText").toString(),
                 QStringLiteral("hello\n\n---\n\nworld"));
        QCOMPARE(editor->property("dividerMidCursor").toInt(), 11);
        QCOMPARE(editor->property("escapedImage").toString(),
                 QStringLiteral("![Cat \\[1\\]](<photo (2).png>)"));
        QVERIFY(editor->property("oneColumnMoved").toBool());
        QCOMPARE(editor->property("oneColumnCursor").toInt(), 22);
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

        QVERIFY(formatAt(2).fontWeight() >= QFont::Bold);
        QCOMPARE(formatAt(2).background().style(), Qt::NoBrush);
        QVERIFY(qFuzzyIsNull(formatAt(0).fontPointSize()) || formatAt(0).fontPointSize() > 8);
        QCOMPARE(formatAt(0).foreground().color(), QColor(QStringLiteral("#101010")));
        QCOMPARE(formatAt(0).background().style(), Qt::NoBrush);

        const int bodyDigit = document.toPlainText().indexOf(QLatin1Char('1'));
        QVERIFY(bodyDigit >= 0);
        QCOMPARE(formatAt(bodyDigit).background().style(), Qt::NoBrush);

        const int separatorDash = document.toPlainText().indexOf(QStringLiteral("---"));
        QVERIFY(separatorDash >= 0);
        QCOMPARE(formatAt(separatorDash).background().style(), Qt::NoBrush);
        QCOMPARE(formatAt(separatorDash).foreground().color(),
                 QColor(QStringLiteral("#101010")));

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
        QCOMPARE(editor->property("cursorPosition").toInt(), 12);
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

    bool assertTableBoxPixels(const QString &markdown, int expectedPipes) {
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
        const QColor rule(QStringLiteral("#4f525a"));
        highlighter.setColors(paper.name(), text.name(), QStringLiteral("#5584aa"));
        highlighter.rehighlight();

        QTextCursor cursor(&document);
        for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
            QTextBlockFormat format = block.blockFormat();
            if (MarkdownHighlighter::isTableSeparator(block.text())) {
                format.setLineHeight(MarkdownHighlighter::tableSeparatorLineHeight,
                                     QTextBlockFormat::FixedHeight);
            } else {
                format.setLineHeight(140, QTextBlockFormat::ProportionalHeight);
            }
            cursor.setPosition(block.position());
            cursor.setBlockFormat(format);
        }

        QImage surface(1400, 240, QImage::Format_ARGB32_Premultiplied);
        surface.fill(paper);
        QPainter painter(&surface);
        TableChrome::paintTables(&painter, &document, paper, text, rule);
        document.drawContents(&painter);
        painter.end();

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

        const QColor headerFill = TableChrome::headerFill(paper, text);
        const QColor bodyFill = TableChrome::bodyFill(paper, text);
        const int midX = qRound((box.columns.at(0) + box.columns.at(1)) * 0.5);
        const int headerY = qRound(box.header.top() + 2);
        const int pipeX = qRound(box.columns.at(1));
        const QColor headerPixel = surface.pixelColor(midX, headerY);
        const QColor pipePixel = surface.pixelColor(pipeX, qRound(box.header.center().y()));
        if (!nearColor(headerPixel, headerFill)) {
            qWarning("header cell %s is not fill %s",
                     qPrintable(headerPixel.name()), qPrintable(headerFill.name()));
            return false;
        }
        if (nearColor(headerPixel, rule)) {
            qWarning("header cell was painted as the separator slab");
            return false;
        }
        if (!nearColor(pipePixel, rule)) {
            qWarning("gutter %s is not a rule %s",
                     qPrintable(pipePixel.name()), qPrintable(rule.name()));
            return false;
        }

        int paperGaps = 0;
        int longestPaper = 0;
        int currentPaper = 0;
        const int scanY = qRound(box.header.top() + 1);
        const int left = qRound(box.columns.first()) + 1;
        const int right = qRound(box.columns.last()) - 1;
        for (int x = left; x <= right; ++x) {
            const QColor pixel = surface.pixelColor(x, scanY);
            if (nearColor(pixel, paper, 24) && !nearColor(pixel, headerFill, 24)
                    && !nearColor(pixel, rule, 24)) {
                ++paperGaps;
                ++currentPaper;
                longestPaper = qMax(longestPaper, currentPaper);
            } else {
                currentPaper = 0;
            }
        }
        if (longestPaper > 3) {
            qWarning("disconnected cells: paper gap %d (samples %d)",
                     longestPaper, paperGaps);
            return false;
        }

        const int cellX = midX;
        const int fromY = qRound(box.header.top());
        const int toY = qRound(box.bounds.bottom());
        int longestRuleRun = 0;
        int currentRule = 0;
        for (int y = fromY; y <= toY; ++y) {
            if (nearColor(surface.pixelColor(cellX, y), rule, 18)) {
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
            const int bodyY = qRound((box.rowEdges.at(1) + box.rowEdges.at(2)) * 0.5);
            const QColor bodyPixel = surface.pixelColor(midX, bodyY);
            if (!nearColor(bodyPixel, bodyFill) && !nearColor(bodyPixel, paper)) {
                qWarning("body cell %s is not a table fill", qPrintable(bodyPixel.name()));
                return false;
            }
            if (nearColor(bodyPixel, rule)) {
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
