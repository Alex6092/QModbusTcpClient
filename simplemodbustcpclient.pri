# qmake integration: add "include(path/to/simplemodbustcpclient.pri)" to your .pro file.

QT += network

INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD

HEADERS += $$PWD/simplemodbustcpclient.h
SOURCES += $$PWD/simplemodbustcpclient.cpp
