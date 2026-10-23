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

#ifndef PDFAPPSENDMAIL_H
#define PDFAPPSENDMAIL_H

#include "pdfappglobal.h"

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentSaver;
class PDFAppDocumentTab;
class PDFMessageBox;

/// Result of a handover of a file to the e-mail program
enum class PDFAppMailResult
{
    Created,        ///< The e-mail program has the message (or the user has closed it)
    NoMailProgram,  ///< No e-mail program is set up (MAPI missing, xdg-email failed to start)
    Failed          ///< The e-mail program has refused the message
};

/// Send by e-mail of a document tab (package E, D2; no Figma frame - the dialogs
/// are the message boxes of the design). The file of the document on the disk is
/// attached, so a document with unsaved changes or without a file is saved first
/// (the message box "Save the document first?" - Cancel / Save and Send).
///
/// - Windows: Simple MAPI (MAPISendMailW of the system MAPI32.dll with its dialog)
///   in its own thread, the window stays usable while the e-mail program runs.
/// - Linux and other Unix systems: `xdg-email --subject <title> --attach <file>`
///   started detached with an argument list (no shell, no quoting).
/// - macOS: not available (the command is disabled; the window hides it).
///
/// The launcher is replaced by a handler in the tests.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSendMail : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Hands the file over to the e-mail program (runs in a worker thread on
    /// Windows, in the GUI thread elsewhere)
    using Launcher = std::function<PDFAppMailResult(const QString& filePath, const QString& subject)>;

    explicit PDFAppSendMail(PDFAppDocumentTab* tab, PDFAppDocumentSaver* saver, QObject* parent);
    virtual ~PDFAppSendMail() override;

    /// E-mail is supported on this platform (or a test launcher is set); the
    /// override of setAvailableOverride decides over both
    static bool isAvailable();

    /// Replaces the launcher of the platform (tests); an empty handler restores it
    static void setLauncher(Launcher launcher);

    /// Overrides the support of the platform (tests: a platform without e-mail -
    /// macOS, D2 - on another system); std::nullopt restores it
    static void setAvailableOverride(std::optional<bool> isAvailable);

    /// Arguments of xdg-email for the file and the subject (the subject without
    /// apostrophes - U+2019 -, control and format characters, at most
    /// MAXIMUM_SUBJECT_LENGTH characters)
    static QStringList getXdgEmailArguments(const QString& filePath, const QString& subject);

    /// Longest subject of a message
    static constexpr int MAXIMUM_SUBJECT_LENGTH = 512;

    /// The command can be used: a shown document, the platform supports e-mail and
    /// no e-mail is being created
    bool canSend() const;

    /// Sends the document (asks to save it first, when needed)
    void send();

    /// The file is being handed over to the e-mail program
    bool isSending() const { return !m_watcher.isNull(); }

    /// The shown dialog (Save the document first? or the error), nullptr if none
    PDFMessageBox* getDialog() const;

Q_SIGNALS:
    /// The state of the command may have changed
    void stateChanged();

    /// The handover has ended
    void finished(pdf::PDFAppMailResult result);

private:
    static Launcher& getLauncher();
    static std::optional<bool>& getAvailableOverride();
    static PDFAppMailResult launch(QString filePath, QString subject);
    static PDFAppMailResult launchPlatform(const QString& filePath, const QString& subject);

    void sendFile();
    void showSaveFirstDialog();
    void showErrorDialog(PDFAppMailResult result);
    void closeDialog();
    void onSaveFirstFinished(int result);
    void onSaverFinished(bool isSaved);
    void onLaunchFinished();

    QPointer<PDFAppDocumentTab> m_tab;
    QPointer<PDFAppDocument> m_document;
    QPointer<PDFAppDocumentSaver> m_saver;
    QPointer<PDFMessageBox> m_dialog;
    QPointer<QFutureWatcher<PDFAppMailResult>> m_watcher;
    bool m_isWaitingForSave = false;
};

}   // namespace pdf

#endif // PDFAPPSENDMAIL_H
