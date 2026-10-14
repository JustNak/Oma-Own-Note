CMARK_ROOT = $$PWD
INCLUDEPATH += $$CMARK_ROOT/src $$CMARK_ROOT/extensions $$CMARK_ROOT/generated
DEFINES += CMARK_GFM_STATIC_DEFINE

# Third-party C. Warnings stay on for the application sources, which are C++.
QMAKE_CFLAGS += -w

CMARK_SOURCES = $$files($$CMARK_ROOT/src/*.c) $$files($$CMARK_ROOT/extensions/*.c)
SOURCES += $$CMARK_SOURCES
