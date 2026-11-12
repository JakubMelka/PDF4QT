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

#ifndef PDFAPPDOCUMENTPERMISSIONS_H
#define PDFAPPDOCUMENTPERMISSIONS_H

#include "pdfappglobal.h"

#include "pdfdocument.h"
#include "pdfsecurityhandler.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>
#include <vector>

class QDialog;
class QWidget;

namespace pdf
{

class PDFAppDocument;

/// Action of the user, which a permission of an encrypted document governs
/// (ISO 32000-2, 7.6.4.2, Table 22). The new application asks every such question
/// through PDFAppDocumentPermissions (owner 2026-11-08: one shared function).
enum class PDFAppPermissionAction
{
    Print,          ///< Bit 3 (low resolution; bit 12 only adds the high quality)
    CopyContent,    ///< Bit 5 (copy of the text and of the images)
    Annotate,       ///< Bit 6 (create, change, delete annotations)
    FillForm,       ///< Bit 9, or bit 6 (Table 22: bit 9 allows filling also without bit 6)
    EditOutline,    ///< Bit 11, or bit 4 (the outline belongs to the assembly and to the modification)
    AssemblePages,  ///< Bit 11 (insert, rotate, remove pages - the commands of the Thumbnails)
    Tool,           ///< Every permission of the catalog entry of a tool (getRequiredPermissions); a tool, which makes a new document of its input documents (Merge), every permission the not encrypted result would lose (PDFDocumentMerger::getRequiredPermissions)
    Unlock          ///< No action: every permission (Unlock with Owner Password of the document properties)
};

/// What an action needs: the action and, for a tool, the id of its catalog entry
/// (an id, not a pointer: a request may wait in a dialog)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPermissionRequest
{
public:
    PDFAppPermissionAction action = PDFAppPermissionAction::Print;
    QString toolId;     ///< Tool: the id of the catalog entry (PDFDocumentToolCatalog::findTool)
    std::vector<PDFSecurityHandler::Permission> permissions;   ///< Tool: what the chosen values need besides the catalog entry (Remove Hidden Data: bit 6 for comments, attachments, signature fields)
    std::optional<QVariantMap> parameters;  ///< Tool: the parameters of the run - a tool, whose output decides, answers by them whether it makes new documents of its inputs (PDFDocumentTool::isNewDocumentOfInputs; Manage Pages, RB3 a of step 3b); std::nullopt = the row of the catalog

    static PDFAppPermissionRequest create(PDFAppPermissionAction action);
    static PDFAppPermissionRequest createTool(const QString& toolId);
    static PDFAppPermissionRequest createTool(const QString& toolId, std::vector<PDFSecurityHandler::Permission> permissions);
    static PDFAppPermissionRequest createTool(const QString& toolId, std::vector<PDFSecurityHandler::Permission> permissions, QVariantMap parameters);
};

/// The permissions of an encrypted document in the new application - the one place of
/// every check (owner 2026-11-08, package O of step 2 of phase 2):
///
/// - The rule (isAllowed): the security handler of the document (ISO 32000-2, 7.6.4.2,
///   Table 22); a document opened by its owner password, or unlocked by it in this
///   session (PDFAppDocument::unlockWithOwnerPassword), allows everything. A handler of
///   revision 2 is read by the table of revision 2 (bits 9 to 12 by bits 3, 4, 5 and 6,
///   SECURITY_TODO 31): the rule of the core, PDFSecurityHandler::isAllowedByRevision, which
///   the merge and the check of a tool before its run use too; the old applications keep
///   their rule.
/// - The question (request): an action, which is not allowed, offers the owner password
///   (PDFAppPasswordDialog in its document mode), when the document can be unlocked
///   (canUnlock: the standard security handler, opened by the user password). A document
///   of the public-key handler (7.6.5) has no owner password: the refusal says, that its
///   permissions come from the certificate. There is no way to ignore a restriction
///   without the owner password (owner 2026-11-08, ISO 32000-2 7.6.4).
/// - The texts: plain words of the permissions (tr), the English texts of MCP (an
///   assistant never asks for a password and never receives it).
///
/// Threads: the functions of a document read only its immutable security handler, they
/// may run in any thread; request opens a dialog (GUI thread only).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPermissions
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppDocumentPermissions)

public:
    using Permission = PDFSecurityHandler::Permission;

    PDFAppDocumentPermissions() = delete;

    /// The permissions of Table 22 in the order of the table (bits 3, 4, 5, 6, 9, 10, 11, 12)
    static std::vector<Permission> getAllPermissions();

    /// The document allows the permission (no document = false; not encrypted = true).
    /// The handler of revision 2 is read by its own table (SECURITY_TODO 31).
    static bool isAllowed(const PDFDocument* document, Permission permission);

    /// The document allows the action (every needed permission, or one of the alternatives)
    static bool isAllowed(const PDFDocument* document, const PDFAppPermissionRequest& request);

    /// The permissions of the action, which the document does not allow (empty = allowed);
    /// an action with alternatives names its main permission (FillForm bit 9, EditOutline bit 11)
    static std::vector<Permission> getMissingPermissions(const PDFDocument* document, const PDFAppPermissionRequest& request);

    /// The permissions of Table 22, which the document does not allow
    static std::vector<Permission> getForbiddenPermissions(const PDFDocument* document);

    /// The document restricts something (an encrypted document, which does not allow everything)
    static bool isRestricted(const PDFDocument* document);

    /// The owner password can lift the restrictions: the standard security handler, the
    /// document opened by the user password and restricting something. False for the
    /// public-key handler (no owner password, 7.6.5) and for a document opened by its
    /// owner password or unlocked already.
    static bool canUnlock(const PDFDocument* document);

    /// The permissions come from a certificate (public-key security handler, 7.6.5)
    static bool isCertificateSecurity(const PDFDocument* document);

    /// The action can be started: it is allowed, or the owner password can allow it (the
    /// enabled state of a command, whose action offers the owner password)
    static bool isAvailable(const PDFDocument* document, const PDFAppPermissionRequest& request);

    /// Revision of the standard security handler (/R of its encryption dictionary), 0 for none
    static int getRevision(const PDFSecurityHandler* securityHandler);

    // Texts ---------------------------------------------------------------

    /// Plain words of a permission ("printing"), translated
    static QString getPermissionName(Permission permission);

    /// Plain words of a permission ("printing"), English (MCP)
    static QString getPermissionNameEnglish(Permission permission);

    /// The translated names of the permissions joined into a sentence part ("printing and copying text and images")
    static QString joinNames(const std::vector<Permission>& permissions);

    /// Title of the owner password dialog ("Owner password for Contract.pdf")
    static QString getDialogTitle(const QString& documentTitle);

    /// Text of the owner password dialog: the action, the missing permission in plain words
    /// and what the password does ("The author of this document does not allow printing. ...")
    static QString getDialogText(const PDFDocument* document, const PDFAppPermissionRequest& request);

    /// The list of what the document forbids ("Not allowed: printing, copying text and images.")
    static QString getForbiddenListText(const PDFDocument* document);

    /// Refusal of an action without a dialog (a status message, a disabled control, a
    /// public-key document): which permission is missing and what can be done
    static QString getRefusalText(const PDFDocument* document, const PDFAppPermissionRequest& request);

    /// English refusal for an assistant (MCP): the missing permission, the unlock by the user
    /// in PDF4QT with the owner password (or the certificate of a public-key document)
    static QString getMcpRefusalText(const PDFDocument* document, const PDFAppPermissionRequest& request);

    // Owner password ------------------------------------------------------

    /// A copy of the handler authorized by the owner password (ISO 32000-2, 7.6.4.4,
    /// Algorithm 7 for revisions 2-4, Algorithm 2.A for revision 6) - the file is not
    /// read again. Nothing is kept of the password. \returns null for a wrong password,
    /// a handler, which is not the standard one, or a damaged encryption dictionary
    static PDFSecurityHandlerPointer authenticateOwner(const PDFSecurityHandler* securityHandler, const QString& ownerPassword);

    /// The same document with another security handler (the objects are shared, the
    /// encryption dictionary written by the handler stays the same: /P, /O, /U, /Perms)
    static PDFDocumentPointer createWithSecurityHandler(const PDFDocument* document, PDFSecurityHandlerPointer securityHandler);

    /// The two handlers have the same encryption dictionary (the same file encryption: the
    /// owner password of one is the owner password of the other)
    static bool hasSameEncryption(const PDFSecurityHandler* first, const PDFSecurityHandler* second);

    // The question --------------------------------------------------------

    /// The shared check of an action of the user. Allowed: true. Not allowed: the owner
    /// password dialog is opened (canUnlock; window modal, without a nested event loop) or
    /// a refusal message (a public-key document, or one which cannot be unlocked), false is
    /// returned and the user starts the action again after the unlock (the tool shell
    /// continues by itself, it waits for the dialog).
    /// \param document The document of the action (null = false)
    /// \param request What the action needs
    /// \param parent Parent of the dialog (its window gets the scrim)
    /// \param dialog Receives the opened dialog (null, when none was opened); not owned
    static bool request(PDFAppDocument* document, const PDFAppPermissionRequest& request, QWidget* parent, QDialog** dialog = nullptr);
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPERMISSIONS_H
