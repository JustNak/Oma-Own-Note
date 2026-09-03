QT += core gui gui-private quick testlib
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_oma_own_note

INCLUDEPATH += ../src
SOURCES += \
    tst_oma_own_note.cpp \
    ../src/backend.cpp \
    ../src/markdownhighlighter.cpp \
    ../src/tablechrome.cpp \
    ../src/tablegeometry.cpp \
    ../src/tabletypography.cpp
HEADERS += \
    ../src/backend.h \
    ../src/markdownhighlighter.h \
    ../src/tablechrome.h \
    ../src/tablegeometry.h \
    ../src/tabletypography.h \
    ../src/viewzoom.h

QT += widgets printsupport quickcontrols2 quickdialogs2 dbus
