// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdfrecentfilemanager.h"
#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFRecentFileManager::PDFRecentFileManager(QObject* parent) :
    BaseClass(parent),
    m_recentFilesLimit(DEFAULT_RECENT_FILES),
    m_actions()
{
    // Initialize actions
    int index = 0;

    for (auto it = m_actions.begin(); it != m_actions.end(); ++it)
    {
        QAction* recentFileAction = new QAction(this);
        recentFileAction->setObjectName(QString("actionRecentFile%1").arg(++index));
        recentFileAction->setVisible(false);
        connect(recentFileAction, &QAction::triggered, this, &PDFRecentFileManager::onRecentFileActionTriggered);
        *it = recentFileAction;
    }
}

void PDFRecentFileManager::addRecentFile(QString fileName)
{
    m_recentFiles.removeAll(fileName);
    m_recentFiles.push_front(fileName);
    update();
}

void PDFRecentFileManager::setRecentFiles(QStringList recentFiles)
{
    if (m_recentFiles != recentFiles)
    {
        m_recentFiles = qMove(recentFiles);
        update();
    }
}

void PDFRecentFileManager::setRecentFilesLimit(int recentFilesLimit)
{
    recentFilesLimit = qBound(getMinimumRecentFiles(), recentFilesLimit, getMaximumRecentFiles());

    if (m_recentFilesLimit != recentFilesLimit)
    {
        m_recentFilesLimit = recentFilesLimit;
        update();
    }
}

void PDFRecentFileManager::update()
{
    while (m_recentFiles.size() > m_recentFilesLimit)
    {
        m_recentFiles.pop_back();
    }

    for (int i = 0; i < static_cast<int>(m_actions.size()); ++i)
    {
        QAction* recentFileAction = m_actions[i];
        if (i < m_recentFiles.size())
        {
            recentFileAction->setData(m_recentFiles[i]);
            recentFileAction->setText(tr("(&%1) %2").arg(i + 1).arg(m_recentFiles[i]));
            recentFileAction->setVisible(true);
        }
        else
        {
            recentFileAction->setData(QVariant());
            recentFileAction->setText(tr("Recent file dummy &%1").arg(i + 1));
            recentFileAction->setVisible(false);
        }
    }
}

void PDFRecentFileManager::onRecentFileActionTriggered()
{
    QAction* action = qobject_cast<QAction*>(sender());
    Q_ASSERT(action);

    QVariant data = action->data();
    if (data.typeId() == QMetaType::QString)
    {
        Q_EMIT fileOpenRequest(data.toString());
    }
}

}   // namespace pdfviewer
