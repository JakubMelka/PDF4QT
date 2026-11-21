// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "appasyncdialogs.h"
#include "appdocumentfixture.h"

#include "pdfappfiledrop.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDrag>
#include <QEvent>
#include <QThread>
#include <QtTest>

namespace pdftest
{

void AppAsyncDialogs::settle()
{
    AppDocumentFixture::settle();
}

QList<QDialog*> AppAsyncDialogs::getOpenDialogs()
{
    QList<QDialog*> dialogs;
    QDialog* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    if (modal && modal->isVisible())
    {
        dialogs << modal;
    }

    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        QDialog* dialog = qobject_cast<QDialog*>(widget);
        if (dialog && dialog != modal && dialog->isVisible())
        {
            dialogs << dialog;
        }
    }
    return dialogs;
}

QDialog* AppAsyncDialogs::findOpenDialog(const QString& objectName)
{
    for (QDialog* dialog : getOpenDialogs())
    {
        if (objectName.isEmpty() || dialog->objectName() == objectName)
        {
            return dialog;
        }
    }
    return nullptr;
}

bool AppAsyncDialogs::clickButton(QDialog* dialog, const QString& buttonName)
{
    QAbstractButton* button = dialog ? dialog->findChild<QAbstractButton*>(buttonName) : nullptr;
    if (!button || !button->isVisible() || !button->isEnabled())
    {
        return false;
    }

    // The continuation runs in the click: the dialog may be gone afterwards
    button->click();
    return true;
}

bool AppAsyncDialogs::answer(const QString& dialogName, const QString& buttonName)
{
    QDialog* dialog = findOpenDialog(dialogName);
    const bool isClicked = dialog && clickButton(dialog, buttonName);
    settle();
    return isClicked;
}

bool AppAsyncDialogs::reject(const QString& dialogName)
{
    QDialog* dialog = findOpenDialog(dialogName);
    if (dialog)
    {
        dialog->reject();
    }
    settle();
    return dialog != nullptr;
}

pdf::PDFAppFileDialogRequest* AppAsyncDialogs::findFileRequest()
{
    const std::vector<pdf::PDFAppFileDialogRequest*> requests = pdf::PDFAppFileDialogRequest::getPendingRequests();
    return requests.empty() ? nullptr : requests.front();
}

bool AppAsyncDialogs::answerFiles(const QStringList& files)
{
    pdf::PDFAppFileDialogRequest* request = findFileRequest();
    if (request)
    {
        request->answer(files);
    }
    settle();
    return request != nullptr;
}

bool AppAsyncDialogs::answerFile(const QString& file)
{
    return answerFiles(file.isEmpty() ? QStringList() : QStringList(file));
}

bool AppAsyncDialogs::cancelFile()
{
    return answerFiles(QStringList());
}

QStringList AppAsyncDialogs::describeOpen()
{
    QStringList open;
    for (const QDialog* dialog : getOpenDialogs())
    {
        open << QStringLiteral("dialog:") + (dialog->objectName().isEmpty() ? QString::fromLatin1(dialog->metaObject()->className()) : dialog->objectName());
    }
    if (const QWidget* popup = QApplication::activePopupWidget())
    {
        open << QStringLiteral("popup:") + (popup->objectName().isEmpty() ? QString::fromLatin1(popup->metaObject()->className()) : popup->objectName());
    }
    for (const pdf::PDFAppFileDialogRequest* request : pdf::PDFAppFileDialogRequest::getPendingRequests())
    {
        open << QStringLiteral("fileRequest:") + request->getSettings().title;
    }
    return open;
}

NestedEventLoopGuard::NestedEventLoopGuard() :
    BaseClass(nullptr)
{
    m_baseLevel = QThread::currentThread()->loopLevel();
    m_reportedLevel = m_baseLevel;
    QCoreApplication::instance()->installEventFilter(this);
}

NestedEventLoopGuard::~NestedEventLoopGuard()
{
    QCoreApplication::instance()->removeEventFilter(this);
    if (!m_violations.isEmpty())
    {
        const QString message = QStringLiteral("A nested event loop was entered (A0): ") + m_violations.join(QStringLiteral("; "));
        QTest::qFail(qPrintable(message), __FILE__, __LINE__);
    }
}

QStringList NestedEventLoopGuard::takeViolations()
{
    QStringList violations = m_violations;
    m_violations.clear();
    return violations;
}

bool NestedEventLoopGuard::eventFilter(QObject* watched, QEvent* event)
{
    // The filter of the application sees only the events of the GUI thread
    const int level = QThread::currentThread()->loopLevel();
    if (level <= m_baseLevel)
    {
        m_reportedLevel = m_baseLevel;
        return BaseClass::eventFilter(watched, event);
    }

    if (level > m_reportedLevel)
    {
        // A new level of event loops: the first event delivered inside of it
        m_reportedLevel = level;
        if (isDragActive())
        {
            ++m_exceptionCount;
        }
        else
        {
            const QString violation = QStringLiteral("level %1, %2 to %3").arg(level).arg(int(event->type())).arg(QString::fromLatin1(watched ? watched->metaObject()->className() : "nullptr"));
            m_violations << violation;
            qWarning("NestedEventLoopGuard: a nested event loop was entered (%s)", qPrintable(violation));
            breakNestedLoop();
        }
    }
    return BaseClass::eventFilter(watched, event);
}

bool NestedEventLoopGuard::isDragActive()
{
    // The named exception: QDrag::exec (own drags, QAbstractItemView::startDrag, text drags) - the drag object is a
    // child of its source while it runs
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (widget->findChild<QDrag*>())
        {
            return true;
        }
    }
    return false;
}

void NestedEventLoopGuard::breakNestedLoop()
{
    // The loop of a popup or of a modal dialog ends by its close (from the event loop: the filter runs inside of the
    // delivery of an event)
    if (QWidget* popup = QApplication::activePopupWidget())
    {
        QMetaObject::invokeMethod(popup, "close", Qt::QueuedConnection);
    }
    if (QWidget* modal = QApplication::activeModalWidget())
    {
        QMetaObject::invokeMethod(modal, "close", Qt::QueuedConnection);
    }
}

}   // namespace pdftest
