#-------------------------------------------------
#
# Project created by QtCreator 2018-11-18T16:50:12
#
#-------------------------------------------------

QT       += core gui winextras

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = PdfForQtViewer
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

QMAKE_CXXFLAGS += /std:c++latest /utf-8

INCLUDEPATH += $$PWD/../PDFForQtLib/Sources

DESTDIR = $$OUT_PWD/..

LIBS += -L$$OUT_PWD/..

LIBS += -lPDFForQtLib

SOURCES += \
        main.cpp \
        pdfviewermainwindow.cpp \
        pdfviewersettings.cpp \
        pdfviewersettingsdialog.cpp

HEADERS += \
        pdfviewermainwindow.h \
        pdfviewersettings.h \
        pdfviewersettingsdialog.h

FORMS += \
        pdfviewermainwindow.ui \
        pdfviewersettingsdialog.ui

CONFIG += force_debug_info

application.files = $$DESTDIR/PdfForQtViewer.exe
application.path = $$DESTDIR/install
INSTALLS += application

RESOURCES += \
    pdfforqtviewer.qrc

