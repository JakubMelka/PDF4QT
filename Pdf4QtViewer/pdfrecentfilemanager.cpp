//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfrecentfilemanager.h"

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

    for (int i = 0; i < m_actions.size(); ++i)
    {
        QAction* recentFileAction = m_actions[i];
        if (i < m_recentFiles.size())
        {
            recentFileAction->setData(m_recentFiles[i]);
            recentFileAction->setText(m_recentFiles[i]);
            recentFileAction->setVisible(true);
        }
        else
        {
            recentFileAction->setData(QVariant());
            recentFileAction->setText(tr("Recent file dummy %1").arg(i + 1));
            recentFileAction->setVisible(false);
        }
    }
}

void PDFRecentFileManager::onRecentFileActionTriggered()
{
    QAction* action = qobject_cast<QAction*>(sender());
    Q_ASSERT(action);

    QVariant data = action->data();
    if (data.type() == QVariant::String)
    {
        emit fileOpenRequest(data.toString());
    }
}

}   // namespace pdfviewer
