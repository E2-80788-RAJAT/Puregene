QT       += core gui charts widgets
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
TARGET = PureGeneGUI
TEMPLATE = app
SOURCES += main.cpp drv8825.c
HEADERS += drv8825.h
LIBS += -llgpio -lcurl -lpthread