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

#include "pdfappdocumentpermissions.h"
#include "pdfappdocument.h"
#include "pdfapppassworddialog.h"

#include "pdfdocumentmerger.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfexception.h"
#include "pdfmessagebox.h"

#include <QDialog>
#include <QWidget>

#include <algorithm>

namespace pdf
{

/// Values of the permissions (Table 22), no state
class PDFAppDocumentPermissionsHelper
{
public:
    PDFAppDocumentPermissionsHelper() = delete;

    using Permission = PDFSecurityHandler::Permission;

    /// Plain words of the permissions (translated in the GUI, English in MCP)
    static const char* getNameSource(Permission permission)
    {
        switch (permission)
        {
            case Permission::PrintLowResolution:
                return QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPermissions", "printing");
            case Permission::Modify:
                return QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPermissions", "changing the document");
            case Permission::CopyContent:
                return QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPermissions", "copying text and images");
            case Permission::ModifyInteractiveItems:
                return QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPermissions", "adding and changing annotations");
            case Permission::ModifyFormFields:
                return QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPermissions", "filling in forms");
            case Permission::Accessibility:
                return QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPermissions", "extracting text for accessibility");
            case Permission::Assemble:
                return QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPermissions", "inserting, rotating and removing pages");
            case Permission::PrintHighResolution:
                return QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPermissions", "printing in high quality");
        }
        return "";
    }

    /// The permissions, which the action needs: all of \p required, or one of \p alternatives
    static void getNeeds(const PDFAppPermissionRequest& request, std::vector<Permission>* required, std::vector<Permission>* alternatives)
    {
        switch (request.action)
        {
            case PDFAppPermissionAction::Print:
                required->push_back(Permission::PrintLowResolution);
                break;
            case PDFAppPermissionAction::CopyContent:
                required->push_back(Permission::CopyContent);
                break;
            case PDFAppPermissionAction::Annotate:
                required->push_back(Permission::ModifyInteractiveItems);
                break;
            case PDFAppPermissionAction::FillForm:
                *alternatives = { Permission::ModifyFormFields, Permission::ModifyInteractiveItems };
                break;
            case PDFAppPermissionAction::EditOutline:
                *alternatives = { Permission::Assemble, Permission::Modify };
                break;
            case PDFAppPermissionAction::AssemblePages:
                required->push_back(Permission::Assemble);
                break;
            case PDFAppPermissionAction::Unlock:
                *required = PDFAppDocumentPermissions::getAllPermissions();
                break;
            case PDFAppPermissionAction::Tool:
            {
                // An unknown tool needs nothing here (the catalog refuses it elsewhere); a new
                // document made of the input documents is not encrypted: every restriction of
                // an input would be lost (owner answer Q2 of step 2 of phase 2, the rule of the core)
                const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(request.toolId);
                if (entry)
                {
                    *required = isNewDocumentOfInputs(request) ? PDFDocumentMerger::getRequiredPermissions() : entry->getRequiredPermissions();
                }
                for (const Permission permission : request.permissions)
                {
                    if (std::find(required->cbegin(), required->cend(), permission) == required->cend())
                    {
                        required->push_back(permission);
                    }
                }
                break;
            }
        }
    }

    /// The tool of the request makes new documents of its input documents (Merge, Extract Pages,
    /// Interleave Pages, Split - plan of step 3 of phase 2, Q4, R1 e): the results are not
    /// encrypted, so they keep no restriction of an input
    static bool isNewDocumentOfInputs(const PDFAppPermissionRequest& request)
    {
        const PDFDocumentToolCatalogEntry* entry = request.action == PDFAppPermissionAction::Tool ? PDFDocumentToolCatalog::findTool(request.toolId) : nullptr;
        return entry && entry->isNewDocumentOfInputs();
    }

    /// The new documents of the tool are more files (Split): the texts name them in plural
    static bool isFilesOfInputs(const PDFAppPermissionRequest& request)
    {
        const PDFDocumentToolCatalogEntry* entry = request.action == PDFAppPermissionAction::Tool ? PDFDocumentToolCatalog::findTool(request.toolId) : nullptr;
        return entry && entry->getResultKind() == PDFDocumentToolResultKind::Files && isNewDocumentOfInputs(request);
    }

    /// English list of names ("printing and copying text and images")
    static QString joinEnglish(const std::vector<Permission>& permissions)
    {
        QStringList names;
        for (const Permission permission : permissions)
        {
            names << QString::fromLatin1(getNameSource(permission));
        }
        if (names.size() <= 1)
        {
            return names.value(0);
        }
        const QString last = names.takeLast();
        return QStringLiteral("%1 and %2").arg(names.join(QStringLiteral(", ")), last);
    }
};

/// The callback of the security handler, which gives the owner password once (a class,
/// not a lambda - Pdf4QtLibApp); the handler tries the empty password first by itself
class PDFAppOwnerPasswordCallback
{
public:
    explicit PDFAppOwnerPasswordCallback(const QString& password) :
        m_password(password)
    {

    }

    QString operator()(bool* ok)
    {
        *ok = m_isFirstCall;
        m_isFirstCall = false;
        return m_password;
    }

private:
    QString m_password;
    bool m_isFirstCall = true;
};

PDFAppPermissionRequest PDFAppPermissionRequest::create(PDFAppPermissionAction action)
{
    PDFAppPermissionRequest request;
    request.action = action;
    return request;
}

PDFAppPermissionRequest PDFAppPermissionRequest::createTool(const QString& toolId)
{
    PDFAppPermissionRequest request;
    request.action = PDFAppPermissionAction::Tool;
    request.toolId = toolId;
    return request;
}

PDFAppPermissionRequest PDFAppPermissionRequest::createTool(const QString& toolId, std::vector<PDFSecurityHandler::Permission> permissions)
{
    PDFAppPermissionRequest request = createTool(toolId);
    request.permissions = std::move(permissions);
    return request;
}

std::vector<PDFSecurityHandler::Permission> PDFAppDocumentPermissions::getAllPermissions()
{
    return { Permission::PrintLowResolution, Permission::Modify, Permission::CopyContent, Permission::ModifyInteractiveItems,
             Permission::ModifyFormFields, Permission::Accessibility, Permission::Assemble, Permission::PrintHighResolution };
}

int PDFAppDocumentPermissions::getRevision(const PDFSecurityHandler* securityHandler)
{
    if (!securityHandler || securityHandler->getMode() != EncryptionMode::Standard)
    {
        return 0;
    }

    // /R of the dictionary, which the handler writes (the dictionary it was read from)
    const PDFObject dictionaryObject = securityHandler->createEncryptionDictionaryObject();
    const PDFDictionary* dictionary = dictionaryObject.isDictionary() ? dictionaryObject.getDictionary() : nullptr;
    if (!dictionary)
    {
        return 0;
    }
    const PDFObject& revision = dictionary->get("R");
    return revision.isInt() ? int(revision.getInteger()) : 0;
}

bool PDFAppDocumentPermissions::isAllowed(const PDFDocument* document, Permission permission)
{
    if (!document)
    {
        return false;
    }

    const PDFSecurityHandler* securityHandler = document->getStorage().getSecurityHandler();
    if (!securityHandler || securityHandler->getMode() == EncryptionMode::None ||
        securityHandler->getAuthorizationResult() == PDFSecurityHandler::AuthorizationResult::OwnerAuthorized)
    {
        return true;
    }

    // SECURITY_TODO 31: the table of revision 2 for a handler of revision 2 - the one rule of
    // the core (the merge and the check of a tool before its run read it the same way)
    return PDFSecurityHandler::isAllowedByRevision(securityHandler, permission);
}

bool PDFAppDocumentPermissions::isAllowed(const PDFDocument* document, const PDFAppPermissionRequest& request)
{
    return document && getMissingPermissions(document, request).empty();
}

std::vector<PDFSecurityHandler::Permission> PDFAppDocumentPermissions::getMissingPermissions(const PDFDocument* document, const PDFAppPermissionRequest& request)
{
    std::vector<Permission> required;
    std::vector<Permission> alternatives;
    PDFAppDocumentPermissionsHelper::getNeeds(request, &required, &alternatives);

    std::vector<Permission> missing;
    for (const Permission permission : required)
    {
        if (!isAllowed(document, permission))
        {
            missing.push_back(permission);
        }
    }

    if (!alternatives.empty())
    {
        bool isAnyAllowed = false;
        for (const Permission permission : alternatives)
        {
            isAnyAllowed = isAnyAllowed || isAllowed(document, permission);
        }
        if (!isAnyAllowed)
        {
            missing.push_back(alternatives.front());
        }
    }
    return missing;
}

std::vector<PDFSecurityHandler::Permission> PDFAppDocumentPermissions::getForbiddenPermissions(const PDFDocument* document)
{
    std::vector<Permission> forbidden;
    if (!document)
    {
        return forbidden;
    }

    for (const Permission permission : getAllPermissions())
    {
        if (!isAllowed(document, permission))
        {
            forbidden.push_back(permission);
        }
    }
    return forbidden;
}

bool PDFAppDocumentPermissions::isRestricted(const PDFDocument* document)
{
    return !getForbiddenPermissions(document).empty();
}

bool PDFAppDocumentPermissions::canUnlock(const PDFDocument* document)
{
    const PDFSecurityHandler* securityHandler = document ? document->getStorage().getSecurityHandler() : nullptr;
    return securityHandler && securityHandler->getMode() == EncryptionMode::Standard &&
           securityHandler->getAuthorizationResult() == PDFSecurityHandler::AuthorizationResult::UserAuthorized &&
           isRestricted(document);
}

bool PDFAppDocumentPermissions::isCertificateSecurity(const PDFDocument* document)
{
    const PDFSecurityHandler* securityHandler = document ? document->getStorage().getSecurityHandler() : nullptr;
    return securityHandler && securityHandler->getMode() == EncryptionMode::PublicKey;
}

bool PDFAppDocumentPermissions::isAvailable(const PDFDocument* document, const PDFAppPermissionRequest& request)
{
    return isAllowed(document, request) || canUnlock(document);
}

QString PDFAppDocumentPermissions::getPermissionName(Permission permission)
{
    return QCoreApplication::translate("pdf::PDFAppDocumentPermissions", PDFAppDocumentPermissionsHelper::getNameSource(permission));
}

QString PDFAppDocumentPermissions::getPermissionNameEnglish(Permission permission)
{
    return QString::fromLatin1(PDFAppDocumentPermissionsHelper::getNameSource(permission));
}

QString PDFAppDocumentPermissions::joinNames(const std::vector<Permission>& permissions)
{
    QStringList names;
    for (const Permission permission : permissions)
    {
        names << getPermissionName(permission);
    }
    if (names.size() <= 1)
    {
        return names.value(0);
    }

    // "a and b", "a, b and c"
    const QString last = names.takeLast();
    return tr("%1 and %2", "The last item of a list of permissions").arg(names.join(tr(", ", "Separator of a list of permissions")), last);
}

QString PDFAppDocumentPermissions::getDialogTitle(const QString& documentTitle)
{
    return tr("Owner password for %1").arg(documentTitle);
}

QString PDFAppDocumentPermissions::getDialogText(const PDFDocument* document, const PDFAppPermissionRequest& request)
{
    const std::vector<Permission> missing = getMissingPermissions(document, request);
    if (missing.empty() || request.action == PDFAppPermissionAction::Unlock)
    {
        // Unlock without an action (document properties)
        return tr("The author of this document restricts what can be done with it. If you own the document, enter its owner password to lift the restrictions until the document is closed.");
    }

    if (request.action == PDFAppPermissionAction::Tool)
    {
        const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(request.toolId);
        if (entry && PDFAppDocumentPermissionsHelper::isFilesOfInputs(request))
        {
            return tr("The author of this document does not allow %1. The new files of %2 are not encrypted and would not keep this restriction. If you own the document, enter its owner password to allow it until the document is closed.")
                    .arg(joinNames(missing), entry->getTitle());
        }
        if (entry && PDFAppDocumentPermissionsHelper::isNewDocumentOfInputs(request))
        {
            return tr("The author of this document does not allow %1. The new document of %2 is not encrypted and would not keep this restriction. If you own the document, enter its owner password to allow it until the document is closed.")
                    .arg(joinNames(missing), entry->getTitle());
        }
        if (entry)
        {
            return tr("The author of this document does not allow %1, which %2 needs. If you own the document, enter its owner password to allow it until the document is closed.")
                    .arg(joinNames(missing), entry->getTitle());
        }
    }
    return tr("The author of this document does not allow %1. If you own the document, enter its owner password to allow it until the document is closed.").arg(joinNames(missing));
}

QString PDFAppDocumentPermissions::getForbiddenListText(const PDFDocument* document)
{
    const std::vector<Permission> forbidden = getForbiddenPermissions(document);
    if (forbidden.empty())
    {
        return QString();
    }
    return tr("Not allowed: %1.").arg(joinNames(forbidden));
}

QString PDFAppDocumentPermissions::getRefusalText(const PDFDocument* document, const PDFAppPermissionRequest& request)
{
    const std::vector<Permission> missing = getMissingPermissions(document, request);
    const QString names = missing.empty() ? tr("this action") : joinNames(missing);
    if (isCertificateSecurity(document))
    {
        return tr("The document does not allow %1. Its permissions come from the certificate, with which it was opened; no password can change them.").arg(names);
    }
    const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(request.toolId);
    if (entry && PDFAppDocumentPermissionsHelper::isFilesOfInputs(request))
    {
        // Split writes several files (plan of step 3 of phase 2, Q4)
        return canUnlock(document) ? tr("The document does not allow %1. The new files of %2 are not encrypted and would not keep this restriction. If you own it, unlock it with its owner password.").arg(names, entry->getTitle())
                                   : tr("The document does not allow %1. The new files of %2 are not encrypted and would not keep this restriction.").arg(names, entry->getTitle());
    }
    if (entry && PDFAppDocumentPermissionsHelper::isNewDocumentOfInputs(request))
    {
        return canUnlock(document) ? tr("The document does not allow %1. The new document of %2 is not encrypted and would not keep this restriction. If you own it, unlock it with its owner password.").arg(names, entry->getTitle())
                                   : tr("The document does not allow %1. The new document of %2 is not encrypted and would not keep this restriction.").arg(names, entry->getTitle());
    }
    if (canUnlock(document))
    {
        return tr("The document does not allow %1. If you own it, unlock it with its owner password.").arg(names);
    }
    return tr("The document does not allow %1.").arg(names);
}

QString PDFAppDocumentPermissions::getMcpRefusalText(const PDFDocument* document, const PDFAppPermissionRequest& request)
{
    const std::vector<Permission> missing = getMissingPermissions(document, request);
    const QString names = missing.empty() ? QStringLiteral("this action") : PDFAppDocumentPermissionsHelper::joinEnglish(missing);
    if (isCertificateSecurity(document))
    {
        return QStringLiteral("The document does not allow %1 (its security settings, ISO 32000-2 Table 22). Its permissions come from the certificate, with which it was opened (public-key security); no password can lift them.").arg(names);
    }
    const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(request.toolId);
    const QString newDocumentText = entry && PDFAppDocumentPermissionsHelper::isFilesOfInputs(request)
                                  ? QStringLiteral(" The new files of %1 are not encrypted and would not keep this restriction.").arg(entry->getSourceTitle())
                                  : (entry && PDFAppDocumentPermissionsHelper::isNewDocumentOfInputs(request)
                                     ? QStringLiteral(" The new document of %1 is not encrypted and would not keep this restriction.").arg(entry->getSourceTitle()) : QString());
    return QStringLiteral("The document does not allow %1 (its security settings, ISO 32000-2 Table 22).%2 The user can unlock it in PDF4QT with the owner password of the document; it stays unlocked until its tab is closed. Never ask the user for the password - it is typed only in PDF4QT. Tell the user and try again after the user has unlocked the document.").arg(names, newDocumentText);
}

PDFSecurityHandlerPointer PDFAppDocumentPermissions::authenticateOwner(const PDFSecurityHandler* securityHandler, const QString& ownerPassword)
{
    if (!securityHandler || securityHandler->getMode() != EncryptionMode::Standard)
    {
        return PDFSecurityHandlerPointer();
    }

    // A copy authenticates by the encryption dictionary, which the handler holds (the
    // handler of the document is not touched, the file is not read again); only the owner
    // password is tried (7.6.4.4)
    PDFSecurityHandlerPointer copy(securityHandler->clone());
    try
    {
        if (copy->authenticate(PDFAppOwnerPasswordCallback(ownerPassword), true) == PDFSecurityHandler::AuthorizationResult::OwnerAuthorized)
        {
            return copy;
        }
    }
    catch (const PDFException&)
    {
        // A damaged /Perms of revision 6 (Algorithm 13): never unlocked
    }
    return PDFSecurityHandlerPointer();
}

PDFDocumentPointer PDFAppDocumentPermissions::createWithSecurityHandler(const PDFDocument* document, PDFSecurityHandlerPointer securityHandler)
{
    if (!document || !securityHandler)
    {
        return PDFDocumentPointer();
    }

    PDFObjectStorage storage = document->getStorage();
    storage.setSecurityHandler(std::move(securityHandler));
    try
    {
        return PDFDocumentPointer(new PDFDocument(std::move(storage), document->getInfo()->version, document->getSourceDataHash()));
    }
    catch (const PDFException&)
    {
        // The same objects were read already; a failure keeps the document as it is
    }
    return PDFDocumentPointer();
}

bool PDFAppDocumentPermissions::hasSameEncryption(const PDFSecurityHandler* first, const PDFSecurityHandler* second)
{
    if (!first || !second || first->getMode() != EncryptionMode::Standard || second->getMode() != EncryptionMode::Standard)
    {
        return false;
    }
    return first == second || first->createEncryptionDictionaryObject() == second->createEncryptionDictionaryObject();
}

bool PDFAppDocumentPermissions::request(PDFAppDocument* document, const PDFAppPermissionRequest& request, QWidget* parent, QDialog** dialog)
{
    if (dialog)
    {
        *dialog = nullptr;
    }

    const PDFDocumentPointer pdfDocument = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    if (!pdfDocument)
    {
        return false;
    }
    if (isAllowed(pdfDocument.data(), request))
    {
        return true;
    }

    if (canUnlock(pdfDocument.data()))
    {
        PDFAppPasswordDialog* passwordDialog = PDFAppPasswordDialog::openOwnerDialog(document, request, parent);
        if (dialog)
        {
            *dialog = passwordDialog;
        }
        return false;
    }

    // A public-key document (the certificate gives its permissions) or one opened by a
    // handler, which cannot be unlocked: the refusal only
    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("This action is not allowed"), getRefusalText(pdfDocument.data(), request),
                                           QDialogButtonBox::Ok, parent ? parent->window() : nullptr);
    box->setObjectName(QStringLiteral("permissionRefusalDialog"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowModality(Qt::WindowModal);
    if (dialog)
    {
        *dialog = box;
    }
    box->open();
    return false;
}

}   // namespace pdf
