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

#ifndef PDFAPPDOCUMENTACTIONS_H
#define PDFAPPDOCUMENTACTIONS_H

#include "pdfappglobal.h"

#include "pdfaction.h"

#include <QCoreApplication>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <functional>

class QPushButton;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentTab;
class PDFAppDocumentView;
class PDFMessageBox;

/// Request to open something outside of the application, which a document has
/// asked for (a web or e-mail link, a program or a file of Launch). It is passed
/// to the launcher (the system, or a handler of the tests).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppExternalLaunch
{
public:
    enum class Kind
    {
        Url,        ///< A web or e-mail link (http, https, mailto)
        Program,    ///< A program with its parameters
        File        ///< A file opened by the program of the system for its type
    };

    Kind kind = Kind::Url;
    QUrl url;               ///< Address of Url
    QString filePath;       ///< Program or file (absolute path)
    QString parameters;     ///< Parameters of a program (as the document gives them)
    QString directory;      ///< Working directory of a program (empty = the folder of the program)
    QString operation;      ///< Operation of the shell of Windows ("open", "print", empty = default)
};

/// Confirmation dialogs of links and actions of a document (message boxes of
/// the design, 958:28033 rules): Open the web link? (1028:22475), Start a
/// program from … (1028:22931), Open a file from … (1028:23389 - also used for
/// the attachments of the panel Attachments, package C). The dialogs are modal
/// over \p parent (the scrim covers it) and safe against the deletion of the
/// parent while they are shown (the result is Rejected then).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentActionDialogs
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppDocumentActionDialogs)

public:
    PDFAppDocumentActionDialogs() = delete;

    /// Result of a confirmation
    enum class Result
    {
        Rejected,   ///< Cancel, Don't Start, Don't Open, Esc, closed
        Accepted,   ///< Open Link, Start Program, Open File
        CopyLink    ///< Copy Link (the link is not opened)
    };

    /// Width of the cards of the dialogs (440 / 520 with facts)
    static constexpr int LINK_WIDTH = 440;
    static constexpr int LAUNCH_WIDTH = 520;

    /// Open the web (or e-mail) link? (1028:22475): Copy Link / Cancel / Open Link
    /// (primary, Enter); Esc = Cancel
    static Result confirmLink(QWidget* parent, const QString& documentTitle, const QUrl& url);

    /// Start a program from … (1028:22931): Program, Parameters, Start in;
    /// Start Program / Don't Start (primary, Enter and Esc)
    static Result confirmProgram(QWidget* parent, const QString& documentTitle, const QString& program, const QString& parameters, const QString& directory);

    /// Open a file from … (1028:23389): File, Folder, Opens in; Open File / Don't
    /// Open (primary, Enter and Esc)
    static Result confirmFile(QWidget* parent, const QString& documentTitle, const QString& filePath);

    /// Creates the dialog of confirmLink (not shown; tests and the caller of exec)
    static PDFMessageBox* createLinkDialog(QWidget* parent, const QString& documentTitle, const QUrl& url);
    static PDFMessageBox* createProgramDialog(QWidget* parent, const QString& documentTitle, const QString& program, const QString& parameters, const QString& directory);
    static PDFMessageBox* createFileDialog(QWidget* parent, const QString& documentTitle, const QString& filePath);

    /// Runs the dialog (exec) and returns its result; the dialog is deleted
    static Result run(PDFMessageBox* dialog);

    /// Text of a value shortened in its middle to the width of the dialog
    static QString shorten(const QString& text, int maximumLength);
};

/// Links and actions of the documents of a tab (package F, owner decisions of
/// step 3): the actions of links and of form fields, which the user triggers on
/// the canvas, and the hover of a link (tooltip 1028:22472 and the status
/// message). JavaScript is not run in phase 1 (an info bar says it), SubmitForm
/// is blocked. A web link opens only http, https and mailto addresses, after the
/// confirmation of each click; with security.openLinks off the info bar
/// link-blocked (1028:24277) says why. Launch (a program or a file) is always
/// confirmed and refused with security.launchFiles off. GoToR and a Launch of a
/// PDF file open the document in a new tab; a target on another computer than
/// the document (UNC) is not touched before Open Anyway of an info bar (the
/// connection would send the sign-in of the user). A missing destination is a status
/// message. The action is copied first, the document may change while a dialog
/// is shown; the handler survives the deletion of the tab from a dialog. An
/// action triggered on the canvas (a link of the annotation manager, a button of
/// the form manager) is performed after the input event has been delivered, from
/// the event loop: its dialog never runs inside of the mouse handler of the
/// canvas, which the deletion of the tab would leave with destroyed objects. The
/// text selection lets a click on a link through (PDFSelectTextTool::setLinksClickable).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentActionHandler : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Ids of the info bars and of their buttons
    static QString getLinkBlockedBarId() { return QStringLiteral("link-blocked"); }
    static QString getLaunchBlockedBarId() { return QStringLiteral("launch-blocked"); }
    static QString getScriptsBarId() { return QStringLiteral("scripts-not-run"); }
    static QString getSubmitBlockedBarId() { return QStringLiteral("submit-form-blocked"); }
    static QString getLinkFailedBarId() { return QStringLiteral("link-failed"); }
    static QString getCopyLinkActionId() { return QStringLiteral("copy-link"); }
    static QString getSecuritySettingsActionId() { return QStringLiteral("security-settings"); }
    static QString getNetworkBlockedBarId() { return QStringLiteral("network-link-blocked"); }
    static QString getOpenAnywayActionId() { return QStringLiteral("open-anyway"); }

    /// Longest address of a link, which is opened
    static constexpr int MAXIMUM_URL_LENGTH = 8192;

    /// Most actions of one chain, which are performed (a chain can be very long)
    static constexpr int MAXIMUM_CHAIN_LENGTH = 64;

    explicit PDFAppDocumentActionHandler(PDFAppDocumentTab* tab, QObject* parent);
    virtual ~PDFAppDocumentActionHandler() override;

    /// Performs the action (and its chain) as if the user has clicked it
    void performAction(const PDFAction* action);

    /// An action triggered on the canvas waits for the event loop (performed by
    /// performPendingAction)
    bool hasPendingAction() const { return m_pendingAction != nullptr; }

    /// Address of a URI action: the URI resolved against the base URI of the
    /// document (a relative URI only), invalid for an invalid or too long text;
    /// a mailto address keeps only the fields to, cc, bcc, subject and body (no
    /// "attach" of a local file)
    static QUrl resolveUri(const QByteArray& baseUri, const QByteArray& uri);

    /// The address may be opened by a link (http and https with a host, mailto
    /// with an address)
    static bool isAllowedUrl(const QUrl& url);

    /// The file is a program or a script (it is started, not opened)
    static bool isExecutable(const QString& filePath);

    /// Operation of the shell of Windows of a Launch action: "open", "print" or
    /// empty (the default operation) - other verbs ("runas") are not used
    static QString getShellOperation(const QByteArray& operation);

    /// Server of a path of another computer (\\server\share\..., //server/share,
    /// \\?\UNC\server\..., \\.\UNC\server\...) in lowercase; empty for other paths
    static QString getNetworkServer(const QString& filePath);

    /// A device path of Windows (\\.\..., \\?\... except UNC), never opened by a link
    static bool isDevicePath(const QString& filePath);

    /// Launcher of the tests (nullptr = the system). It returns false, when the
    /// launch has failed.
    using LaunchHandler = std::function<bool(const PDFAppExternalLaunch&)>;
    static void setLaunchHandler(LaunchHandler handler);

    /// Text of the hover of the link under the pointer (empty = no link): the
    /// title and the description of the tooltip, the message of the status bar
    const QString& getHoverTitle() const { return m_hoverTitle; }
    const QString& getHoverDescription() const { return m_hoverDescription; }
    const QString& getHoverStatus() const { return m_hoverStatus; }

    /// Describes the action for its hover (title, description and status message)
    void describe(const PDFAction* action, QString* title, QString* description, QString* status) const;

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    PDFAppDocumentTab* getTab() const;
    PDFAppDocumentView* getView() const;
    PDFAppDocument* getDocument() const;
    QString getDocumentTitle() const;
    QString getDocumentFolder() const;

    void onActionTriggered(const pdf::PDFAction* action);
    void performPendingAction();
    void onInfoBarActionTriggered(const QString& infoBarId, const QString& actionId);
    void onDocumentStateChanged();
    void onHoverTimeout();
    void updateHover(const QPoint& position);
    void clearHover();
    void showStatusMessage(const QString& text);

    /// Performs one action of a chain. \returns false, when the chain must stop
    /// (the tab or the document is gone, the document has changed)
    bool performOne(const PDFAction* action, quint64 version);
    void performGoTo(const PDFActionGoTo* action);
    bool performUri(const PDFActionURI* action);
    bool performLaunch(const PDFActionLaunch* action);
    void performGoToR(const PDFActionGoToR* action);
    void performNamed(const PDFActionNamed* action);
    void performSetOCGState(const PDFActionSetOCGState* action);
    void showScriptsBar();

    /// Resolves the file of a file specification against the folder of the document
    QString resolveFile(const PDFFileSpecification& specification) const;

    /// Opens a PDF file in a new tab of the window (page from 1, 0 = default)
    void openDocument(const QString& filePath, int page);

    /// The target of GoToR / Launch may be touched: a device path is refused, a
    /// path of another computer than the one of the document shows the info bar
    /// network-link-blocked (Open Anyway for a document) - before any access of
    /// the file system, which would connect to the server and send the sign-in
    /// of the user (NTLM). \returns false, when the target must not be used.
    bool checkTarget(const QString& filePath, int page, bool isDocument);

    /// Launches the request (the handler of the tests, or the system)
    static bool launch(const PDFAppExternalLaunch& request);

    void showLinkBlocked(const QUrl& url, const QString& title, bool isSettingOff);

    QPointer<PDFAppDocumentTab> m_tab;
    QPointer<QWidget> m_drawWidget;
    QTimer m_hoverTimer;
    QPoint m_hoverPosition;
    PDFActionPtr m_pendingAction;   ///< Copy of the action triggered on the canvas, performed from the event loop
    QString m_hoverTitle;
    QString m_hoverDescription;
    QString m_hoverStatus;
    QString m_shownStatus;
    QString m_blockedLink;
    QString m_blockedNetworkPath;   ///< Document of the info bar network-link-blocked (Open Anyway)
    int m_blockedNetworkPage = 0;
    bool m_isPerforming = false;
    bool m_scriptsChecked = false;

    static LaunchHandler s_launchHandler;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTACTIONS_H
