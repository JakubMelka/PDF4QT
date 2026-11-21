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

// A probe of nested_event_loops.py --self-test (never compiled): every form of an entry into a nested event loop

#include "probe_exec.h"

#include <QEventLoop>                                       // EXPECT NONE

int ProbeDialog::exec()                                     // EXPECT NONE
{
    return QDialog::exec();                                 // EXPECT ENTRY
}

void ProbeDialog::runBase()
{
    BaseClass::exec();                                      // EXPECT ENTRY
    exec();                                                 // EXPECT ENTRY
    const int code = exec();                                // EXPECT ENTRY
    // dialog->exec(); a comment is no call                 // EXPECT NONE
}

void ProbeUser::run(QObject* object, QWidget* window)
{
    QMenu::exec(m_actions, QCursor::pos());                 // EXPECT ENTRY
    qobject_cast<QMenu*>(object)->exec(QCursor::pos());     // EXPECT ENTRY
    window->findChild<QDialog*>("dialog")->exec();          // EXPECT ENTRY
    m_dialogs[0]->exec();                                   // EXPECT ENTRY
    for (auto it = m_dialogs.begin(); it != m_dialogs.end(); ++it)
    {
        (*it)->exec();                                      // EXPECT ENTRY
    }

    QDialog dialog(window);
    dialog.exec();                                          // EXPECT ENTRY
    m_dialog->exec();                                       // EXPECT ENTRY
    const int code = m_dialog
        ->exec();                                           // EXPECT ENTRY
    const QString text = QStringLiteral("menu.exec()");     // EXPECT NONE

    QEventLoop loop;                                        // EXPECT ENTRY
    loop.exec();                                            // EXPECT ENTRY
    QCoreApplication::processEvents();                      // EXPECT ENTRY

    QDrag* drag = new QDrag(window);                        // EXPECT NONE
    drag->exec(Qt::MoveAction);                             // EXPECT EXCEPTION

    QFileDialog::getOpenFileName(window);                   // EXPECT ENTRY
    QMessageBox::warning(window, text, text);               // EXPECT ENTRY
    QInputDialog::getText(window, text, text);              // EXPECT ENTRY
}
