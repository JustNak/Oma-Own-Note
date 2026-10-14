QT += core gui quick testlib
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_oma_own_note

INCLUDEPATH += ../src
SOURCES += \
    tst_oma_own_note.cpp \
    markdownrendertest.cpp \
    ../src/backend.cpp \
    ../src/htmlsanitizer.cpp \
    ../src/markdownhighlighter.cpp \
    ../src/markdownrenderer.cpp
HEADERS += \
    markdownrendertest.h \
    ../src/backend.h \
    ../src/htmlsanitizer.h \
    ../src/markdownhighlighter.h \
    ../src/markdownrenderer.h \
    ../src/viewzoom.h

include(../third_party/cmark-gfm/cmark.pri)

QT += widgets printsupport quickcontrols2 quickdialogs2 dbus
