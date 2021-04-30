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

#ifndef PDFRECENTFILEMANAGER_H
#define PDFRECENTFILEMANAGER_H

#include <QObject>
#include <QAction>

#include <array>

namespace pdfviewer
{

/// Recent file manager, manages list of recent files.
class PDFRecentFileManager : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

    static constexpr const int MAXIMUM_RECENT_FILES = 9;
    static constexpr const int DEFAULT_RECENT_FILES = 5;

public:
    explicit PDFRecentFileManager(QObject* parent);

    /// Adds recent file to recent file list and updates the actions.
    /// New recent file is added as first file.
    /// \param fileName New recent file
    void addRecentFile(QString fileName);

    /// Returns a list of recent files
    const QStringList& getRecentFiles() const { return m_recentFiles; }

    /// Sets a list of recent files, and udpates manager
    void setRecentFiles(QStringList recentFiles);

    /// Returns a limit of recent files count
    int getRecentFilesLimit() const { return m_recentFilesLimit; }

    /// Sets a new limit of recent files count
    void setRecentFilesLimit(int recentFilesLimit);

    /// Returns list of recent files actions
    const std::array<QAction*, MAXIMUM_RECENT_FILES>& getActions() const { return m_actions; }

    static constexpr int getMinimumRecentFiles() { return 1; }
    static constexpr int getDefaultRecentFiles() { return DEFAULT_RECENT_FILES; }
    static constexpr int getMaximumRecentFiles() { return MAXIMUM_RECENT_FILES; }

signals:
    void fileOpenRequest(QString fileName);

private:
    /// Updates recent files actions / recent file list
    void update();

    /// Reaction on recent file action triggered
    void onRecentFileActionTriggered();

    int m_recentFilesLimit;
    std::array<QAction*, MAXIMUM_RECENT_FILES> m_actions;
    QStringList m_recentFiles;
};

}   // namespace pdfviewer

#endif // PDFRECENTFILEMANAGER_H
