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

#ifndef PDFWORKSPACEREPOSITORY_H
#define PDFWORKSPACEREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QPointer>
#include <QString>

#include <optional>
#include <vector>

namespace pdf
{

class PDFDatabaseConnection;

/// Document of a workspace: the file and its page (from 1, 0 = unknown)
class PDF4QTLIBAPPSHARED_EXPORT PDFWorkspaceDocument
{
public:
    QString path;
    int page = 0;

    bool operator==(const PDFWorkspaceDocument&) const = default;
};

/// Workspace (tables workspace, workspace_document). Times are milliseconds
/// since the epoch, 0 = none.
class PDF4QTLIBAPPSHARED_EXPORT PDFWorkspace
{
public:
    qint64 id = 0;
    QString name;           ///< Empty for the workspace of the last session
    qint64 created = 0;
    qint64 opened = 0;      ///< Last opening ("Opened Yesterday 16:15"); 0 = not in Recent
    qint64 saved = 0;       ///< Last save ("Saved Sep 10, 2026"); last session: the time it was closed
    bool isLastSession = false;
    int documentCount = 0;  ///< Number of the documents (also when documents are not read)
    std::vector<PDFWorkspaceDocument> documents;
};

/// Saved workspaces and the workspace of the last session (newapp_storage.md 4,
/// Figma Open 61:1201 Workspaces, Save as workspace 941:136915, duplicate name
/// 955:147918, delete 955:149658, rename 955:150171, Restore last session).
/// Workspaces exist only in the database, without a limit (the user manages
/// them). A value object: every read goes to the database, every write is a
/// short BEGIN IMMEDIATE transaction and is notified (topics getWorkspaceTopic,
/// getDocumentTopic, key = id of the workspace). Rows are untrusted: invalid
/// documents are skipped, an invalid name is replaced. Usable in any thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFWorkspaceRepository
{
public:
    /// Most documents of one workspace, which are stored or read
    static constexpr int MAXIMUM_DOCUMENTS = 1000;

    /// Most workspaces, which are read
    static constexpr int MAXIMUM_WORKSPACES = 10000;

    static QString getWorkspaceTopic() { return QStringLiteral("workspace"); }
    static QString getDocumentTopic() { return QStringLiteral("workspace_document"); }

    explicit PDFWorkspaceRepository(PDFDatabase* database);

    /// Fixed time of the writes (tests); 0 = the current time
    void setCurrentTime(qint64 time) { m_currentTime = time; }

    /// Saved workspaces (without the last session), the most recently opened or
    /// saved first. \p withDocuments = read also the documents (else only documentCount).
    std::vector<PDFWorkspace> getWorkspaces(bool withDocuments = false) const;

    /// Workspaces in Recent: opened ones (opened > 0), the most recent first
    std::vector<PDFWorkspace> getRecentWorkspaces() const;

    /// Workspace with its documents (also the last session)
    std::optional<PDFWorkspace> getWorkspace(qint64 id) const;

    /// Saved workspace of the name (case insensitive), std::nullopt = the name is free.
    /// Figma 955:147918: "A workspace named “X” already exists (12 documents)".
    std::optional<PDFWorkspace> findWorkspace(const QString& name) const;

    /// Normalized name (PDFRepositoryHelper::normalizeName), empty = invalid
    static QString normalizeName(const QString& name);

    /// Saves the documents as a workspace. A name, which exists, is refused with
    /// \p isDuplicate = true, unless \p replaceExisting (Replace): then the existing
    /// workspace gets the documents (its id and creation time are kept). Invalid
    /// paths are refused, at most MAXIMUM_DOCUMENTS documents, at least one.
    PDFDatabaseError saveWorkspace(const QString& name, const std::vector<PDFWorkspaceDocument>& documents, bool replaceExisting,
                                   qint64* id = nullptr, bool* isDuplicate = nullptr);

    /// Renames a saved workspace; a name of another workspace is refused (\p isDuplicate),
    /// a workspace, which does not exist (deleted by another instance), is an error
    PDFDatabaseError renameWorkspace(qint64 id, const QString& name, bool* isDuplicate = nullptr);

    /// Deletes a workspace (its documents with it); the files are not touched
    PDFDatabaseError deleteWorkspace(qint64 id);

    /// The workspace was opened now (it goes to Recent, "Opened ...")
    PDFDatabaseError markOpened(qint64 id);

    /// Stores the documents of the window, which is closing, as the last session
    /// (replaces the previous one); empty \p documents deletes the last session
    PDFDatabaseError saveLastSession(const std::vector<PDFWorkspaceDocument>& documents);

    /// Workspace of the last session (Restore last session), with its documents
    std::optional<PDFWorkspace> getLastSession() const;

    /// Deletes the last session (after it was restored)
    PDFDatabaseError deleteLastSession();

private:
    qint64 getCurrentTime() const;
    PDFDatabaseConnection* getConnection() const;
    std::vector<PDFWorkspace> readWorkspaces(const char* sql, bool withDocuments) const;
    std::vector<PDFWorkspaceDocument> readDocuments(PDFDatabaseConnection* connection, qint64 id) const;
    static PDFDatabaseError writeDocuments(PDFDatabaseConnection* connection, qint64 id, const std::vector<PDFWorkspaceDocument>& documents);
    static PDFDatabaseError validateDocuments(const std::vector<PDFWorkspaceDocument>& documents, bool allowEmpty);
    std::optional<PDFWorkspace> findWorkspaceImpl(const QString& name, qint64 excludedId) const;

    QPointer<PDFDatabase> m_database;
    qint64 m_currentTime = 0;
};

}   // namespace pdf

#endif // PDFWORKSPACEREPOSITORY_H
