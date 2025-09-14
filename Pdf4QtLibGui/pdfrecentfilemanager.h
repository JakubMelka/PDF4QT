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

    /// Removes all recent files
    void clearRecentFiles();

    /// Returns list of recent files actions
    const std::array<QAction*, MAXIMUM_RECENT_FILES>& getActions() const;

    static constexpr int getMinimumRecentFiles() { return 1; }
    static constexpr int getDefaultRecentFiles() { return DEFAULT_RECENT_FILES; }
    static constexpr int getMaximumRecentFiles() { return MAXIMUM_RECENT_FILES; }

    QAction* getClearRecentFileHistoryAction() const;
    void setClearRecentFileHistoryAction(QAction* newClearRecentFileHistoryAction);

signals:
    void fileOpenRequest(QString fileName);

private:
    /// Updates recent files actions / recent file list
    void update();

    /// Updates clear recent file action
    void updateClearRecentFileAction();

    /// Reaction on recent file action triggered
    void onRecentFileActionTriggered();

    int m_recentFilesLimit;
    std::array<QAction*, MAXIMUM_RECENT_FILES> m_actions;
    QAction* m_clearRecentFileHistoryAction;
    QStringList m_recentFiles;
};

}   // namespace pdfviewer

#endif // PDFRECENTFILEMANAGER_H
