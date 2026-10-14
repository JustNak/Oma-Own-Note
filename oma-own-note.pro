QT += core gui widgets printsupport qml quick quickcontrols2 quickdialogs2 dbus

CONFIG += c++17 release
TARGET = oma-own-note
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/htmlsanitizer.h \
    src/markdownhighlighter.h \
    src/markdownrenderer.h \
    src/systemtheme.h \
    src/viewzoom.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/htmlsanitizer.cpp \
    src/markdownhighlighter.cpp \
    src/markdownrenderer.cpp \
    src/systemtheme.cpp

include(third_party/cmark-gfm/cmark.pri)

RESOURCES += src/resources.qrc
