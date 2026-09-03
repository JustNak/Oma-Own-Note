QT += core gui gui-private widgets printsupport qml quick quickcontrols2 quickdialogs2 dbus

CONFIG += c++17 release
TARGET = oma-own-note
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/markdownhighlighter.h \
    src/systemtheme.h \
    src/tablechrome.h \
    src/tabletypography.h \
    src/viewzoom.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/markdownhighlighter.cpp \
    src/systemtheme.cpp \
    src/tablechrome.cpp \
    src/tabletypography.cpp

RESOURCES += src/resources.qrc
