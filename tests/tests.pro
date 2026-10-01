QT += core gui testlib dbus
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_omacalc

INCLUDEPATH += ../src
SOURCES += \
    tst_omacalc.cpp \
    ../src/backend.cpp \
    ../src/engine.cpp
HEADERS += \
    ../src/backend.h \
    ../src/engine.h
