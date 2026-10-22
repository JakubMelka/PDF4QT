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

#ifndef PDFAPPDOCUMENTCOMMANDS_H
#define PDFAPPDOCUMENTCOMMANDS_H

#include "pdfappglobal.h"

#include <QObject>
#include <QPointer>
#include <QStringList>

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentTab;
class PDFAppDocumentView;

/// Package of step 3, which implements a command of the document tab (step 3 plan,
/// chapter 2): one command handler per package, created by the document tab
enum class PDFAppDocumentPackage
{
    Model,          ///< A - model, lifecycle, undo / redo, lock, Edit a Copy
    View,           ///< B - view, toolbar, panels File / View / Go to, Select menu, status bar
    Panels,         ///< C - sidebar and the navigation panels (thumbnails, bookmarks, ...)
    Search,         ///< D - search, text index, signatures, speech
    File,           ///< E - save, print, e-mail, automatic refresh
    Properties      ///< F - properties, links and actions, rendering issues, developer view
};

/// Table "command id -> package" of the commands of the document tab. The ids are
/// the ids of PDFAppCommands (registered in the command registry with the scope
/// Document or Canvas); every such command belongs to exactly one package.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentCommandTable
{
public:
    PDFAppDocumentCommandTable() = delete;

    /// Commands of the package
    static QStringList getCommands(PDFAppDocumentPackage package);

    /// All commands of the document tab
    static QStringList getAllCommands();

    /// Package of the command. \returns false, if the command is not a command of the document tab
    static bool findPackage(const QString& commandId, PDFAppDocumentPackage* package);
};

/// Base of the command handler of one package (step 3 plan 1.1). The window keeps
/// one QAction per command and dispatches it to the current tab
/// (PDFAppTab::executeCommand); the document tab asks its handlers. A handler
/// reads its state at each call (isEnabled, isChecked) and emits stateChanged,
/// when the state of its commands may have changed (the window updates the
/// actions then).
///
/// The handler is a child of the tab and lives as long as the tab; the document
/// and the view of the tab live longer than the handlers (the tab deletes the
/// handlers first).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentCommandHandler : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppDocumentCommandHandler(PDFAppDocumentPackage package, PDFAppDocumentTab* tab);
    virtual ~PDFAppDocumentCommandHandler() override;

    PDFAppDocumentPackage getPackage() const { return m_package; }

    /// The tab, its document and its view (nullptr, when the tab is being destroyed)
    PDFAppDocumentTab* getTab() const;
    PDFAppDocument* getDocument() const;
    PDFAppDocumentView* getView() const;

    /// Commands of the handler (the commands of its package in the table)
    QStringList getCommands() const;

    /// The handler handles the command
    bool handles(const QString& commandId) const;

    /// Runs the command (only a command of the handler, which is enabled). The
    /// code of a command may run dialogs and delete the tab: nothing of the handler
    /// is used after the call.
    virtual void execute(const QString& commandId) = 0;

    /// The command is enabled (default: the document is shown - state Ready)
    virtual bool isEnabled(const QString& commandId) const;

    /// The command is checked (default: false)
    virtual bool isChecked(const QString& commandId) const;

Q_SIGNALS:
    /// The enabled or checked state of the commands may have changed
    void stateChanged();

private:
    PDFAppDocumentPackage m_package;
    QPointer<PDFAppDocumentTab> m_tab;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTCOMMANDS_H
