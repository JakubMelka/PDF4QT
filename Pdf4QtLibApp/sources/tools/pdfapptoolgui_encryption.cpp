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

#include "pdfapptoolgui_encryption.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpermissions.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfapptoolchangelist.h"
#include "pdfapptoolgui_protect.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfapptoolpermissionlist.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocument.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfexception.h"
#include "pdfthemefonts.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <tuple>

namespace pdf
{

/// The callback of a handler, which offers no password: the handler tries the empty one by itself
/// (7.6.4.4) - a class, not a lambda (Pdf4QtLibApp)
class PDFAppToolEmptyPasswordCallback
{
public:
    QString operator()(bool* ok) const
    {
        *ok = false;
        return QString();
    }
};

/// Helpers of the GUI of the encryption, no state
class PDFAppToolGUIEncryptionHelper
{
public:
    PDFAppToolGUIEncryptionHelper() = delete;

    static constexpr const char* PRESET_PROPERTY = "pdf4qtPreset";

    /// Gap of the rows of the key value cards (430:1178, 430:1258, 432:2373: rows of 28, 8 apart)
    static constexpr int KEY_VALUE_ROW_GAP = 8;

    using Tool = PDFDocumentToolEncryption;

    static QString name(const char* text)
    {
        return QString::fromLatin1(text);
    }

    /// The values of the rows of a key value list: the same keys keep their widgets
    static void setRows(PDFKeyValueList* list, const std::vector<std::pair<QString, QString>>& rows)
    {
        if (!list)
        {
            return;
        }

        bool isSameKeys = list->getRowCount() == int(rows.size());
        for (int i = 0; isSameKeys && i < int(rows.size()); ++i)
        {
            isSameKeys = list->getKey(i) == rows[size_t(i)].first;
        }
        if (!isSameKeys)
        {
            list->clear();
            for (const auto& row : rows)
            {
                list->addRow(row.first, row.second);
            }
            return;
        }
        for (int i = 0; i < int(rows.size()); ++i)
        {
            list->setValue(i, rows[size_t(i)].second);
        }
    }

    /// A card of the canvas (430:1198: padding 14 + the border, the title Semi Bold 13 in lines of 16)
    static PDFAppToolCard* createCanvasCard(const QString& objectName, const QString& title, QWidget* parent, PDFLabel** titleLabel)
    {
        PDFAppToolCard* card = new PDFAppToolCard(parent);
        card->setObjectName(objectName);
        card->getBodyLayout()->setContentsMargins(15, 15, 15, 15);
        card->getBodyLayout()->setSpacing(PDFAppToolPasswordCard::CANVAS_GAP);
        if (!title.isEmpty())
        {
            PDFLabel* label = PDFAppPageWidgets::createLabel(title, QFont::DemiBold, 13, PDFThemeColors::TextPrimary, card);
            label->setObjectName(objectName + QStringLiteral("Title"));
            card->getBodyLayout()->addWidget(label);
            if (titleLabel)
            {
                *titleLabel = label;
            }
        }
        return card;
    }

    /// A card of a side panel (432:2235: padding 12 + the border, the parts 8 apart)
    static PDFAppToolCard* createPanelCard(const QString& objectName, QWidget* parent)
    {
        PDFAppToolCard* card = new PDFAppToolCard(parent);
        card->setObjectName(objectName);
        card->getBodyLayout()->setContentsMargins(13, 13, 13, 13);
        card->getBodyLayout()->setSpacing(PDFAppToolPasswordCard::PANEL_GAP);
        return card;
    }

    /// A text of a card: Regular 12 text/secondary in lines of 15 (430:1243), Regular 11 text/muted
    /// in lines of 13 (432:2236)
    static PDFAppParagraph* createText(const QString& text, bool isSmall, QWidget* parent)
    {
        PDFAppParagraph* paragraph = isSmall ? PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, parent)
                                             : PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, parent);
        paragraph->setMinimumWidth(1);
        return paragraph;
    }

    static PDFAppToolCallout* createCallout(PDFAccentHue hue, const QString& iconName, const QString& objectName, QWidget* parent)
    {
        PDFAppToolCallout* callout = new PDFAppToolCallout(hue, iconName, QString(), QString(), parent);
        callout->setObjectName(objectName);
        setCompactStyle(callout);
        return callout;
    }

    /// The compact callout (430:1188); its title wraps - two lines of a narrow panel (433:3049
    /// "Without the private key the document is lost", 433:5811), never clipped
    static void setCompactStyle(PDFAppToolCallout* callout)
    {
        callout->setCompactStyle();
        callout->getTitleLabel()->setWordWrap(true);
    }

    static void setVisible(QWidget* widget, bool isVisible)
    {
        if (widget)
        {
            widget->setVisible(isVisible);
        }
    }

    static int getAllowedCount(const QVariantMap& values)
    {
        // Access for screen readers is always allowed (Q5.2) and counts
        int count = 1;
        for (const auto& item : Tool::getPermissionParameters())
        {
            count += values.value(name(item.first)).toBool() ? 1 : 0;
        }
        return count;
    }

    /// What the document restricts now in one line by the groups of 432:2105 (433:5022 "Editing,
    /// forms and copying are restricted now"): printing (ISO 32000-2 Table 22, bits 3 and 12),
    /// editing (bits 4, 6 and 11), forms (bit 9), copying (bit 5); bit 10 is never one (Q5.2)
    static QString getRestrictedText(const std::vector<PDFSecurityHandler::Permission>& forbidden)
    {
        using Permission = PDFSecurityHandler::Permission;
        bool isPrinting = false;
        bool isEditing = false;
        bool isForms = false;
        bool isCopying = false;
        for (const Permission permission : forbidden)
        {
            switch (permission)
            {
                case Permission::PrintLowResolution:
                case Permission::PrintHighResolution:
                    isPrinting = true;
                    break;
                case Permission::Modify:
                case Permission::ModifyInteractiveItems:
                case Permission::Assemble:
                    isEditing = true;
                    break;
                case Permission::ModifyFormFields:
                    isForms = true;
                    break;
                case Permission::CopyContent:
                    isCopying = true;
                    break;
                case Permission::Accessibility:
                    break;
            }
        }

        QStringList groups;
        if (isPrinting)
        {
            groups << PDFAppToolGUIEncryption::tr("printing", "A group of the restricted permissions (433:5022)");
        }
        if (isEditing)
        {
            groups << PDFAppToolGUIEncryption::tr("editing", "A group of the restricted permissions (433:5022)");
        }
        if (isForms)
        {
            groups << PDFAppToolGUIEncryption::tr("forms", "A group of the restricted permissions (433:5022)");
        }
        if (isCopying)
        {
            groups << PDFAppToolGUIEncryption::tr("copying", "A group of the restricted permissions (433:5022)");
        }

        if (groups.isEmpty())
        {
            return PDFAppToolGUIEncryption::tr("Nothing is restricted now");
        }
        if (groups.size() == 1)
        {
            // One group: the sentences of their own (the grammatical number of the group)
            if (isPrinting)
            {
                return PDFAppToolGUIEncryption::tr("Printing is restricted now");
            }
            if (isEditing)
            {
                return PDFAppToolGUIEncryption::tr("Editing is restricted now");
            }
            if (isForms)
            {
                return PDFAppToolGUIEncryption::tr("Forms are restricted now");
            }
            return PDFAppToolGUIEncryption::tr("Copying is restricted now");
        }

        // "Editing, forms and copying are restricted now"
        const QString last = groups.takeLast();
        const QString list = PDFAppToolGUIEncryption::tr("%1 and %2", "The last group of the restricted permissions").arg(groups.join(PDFAppToolGUIEncryption::tr(", ", "Separator of the groups of the restricted permissions")), last);
        return PDFAppToolGUIEncryption::tr("%1 are restricted now", "A list of the groups of the restricted permissions, the first one capitalized").arg(list.left(1).toUpper() + list.mid(1));
    }

    /// The forbidden permissions of the values of the permission parameters
    static std::vector<PDFSecurityHandler::Permission> getForbidden(const QVariantMap& values)
    {
        std::vector<PDFSecurityHandler::Permission> forbidden;
        for (const auto& item : Tool::getPermissionParameters())
        {
            if (!values.value(name(item.first)).toBool())
            {
                forbidden.push_back(item.second);
            }
        }
        return forbidden;
    }
};

// ---------------------------------------------------------- PDFAppToolProtectionFacts

QString PDFAppToolProtectionFacts::getAlgorithmTitle(const PDFSecurityHandler* securityHandler)
{
    if (!securityHandler || securityHandler->getMode() == EncryptionMode::None)
    {
        return QString();
    }
    // 433:5811 "Certificate" (the core says "Certificate · AES 256-bit")
    if (securityHandler->getMode() == EncryptionMode::PublicKey)
    {
        return PDFAppToolGUIEncryption::tr("Certificate");
    }
    return QString();
}

QString PDFAppToolProtectionFacts::getEncryptionTitle(const PDFDocument* document)
{
    // The one reading of the core (ISO 32000-2, 7.6.4.1: revisions 5 and 6 AES-256, revision 4 by
    // the method of its crypt filter, RC4 of /Length bits); a certificate by 433:5811
    const PDFSecurityHandler* securityHandler = document ? document->getStorage().getSecurityHandler() : nullptr;
    const QString certificate = getAlgorithmTitle(securityHandler);
    return certificate.isEmpty() ? PDFDocumentToolEncryption::getEncryptionName(document) : certificate;
}

QString PDFAppToolProtectionFacts::getPermissionsText(const std::vector<PDFSecurityHandler::Permission>& forbidden)
{
    using Permission = PDFSecurityHandler::Permission;

    // 430:2481 "Printing only", 433:5022 "All allowed"; the accessibility is never forbidden by
    // the tools (bit 10, Q5.2) and is not one of the actions here
    std::vector<Permission> forbiddenActions;
    for (const Permission permission : forbidden)
    {
        if (permission != Permission::Accessibility)
        {
            forbiddenActions.push_back(permission);
        }
    }
    const bool isPrintLowForbidden = std::find(forbiddenActions.cbegin(), forbiddenActions.cend(), Permission::PrintLowResolution) != forbiddenActions.cend();
    const bool isPrintHighForbidden = std::find(forbiddenActions.cbegin(), forbiddenActions.cend(), Permission::PrintHighResolution) != forbiddenActions.cend();

    if (forbiddenActions.empty())
    {
        return PDFDocumentToolEncryption::getAllAllowedText();
    }
    const int allowedCount = 7 - int(forbiddenActions.size());
    if (allowedCount <= 0)
    {
        return PDFAppToolGUIEncryption::tr("Viewing only");
    }
    if (!isPrintLowForbidden && allowedCount <= 2 && (allowedCount == 1 || !isPrintHighForbidden))
    {
        return PDFAppToolGUIEncryption::tr("Printing only");
    }
    return PDFAppToolGUIEncryption::tr("%1 of 8 allowed").arg(allowedCount + 1);
}

QString PDFAppToolProtectionFacts::getOpenPasswordText() const
{
    // An encryption of the attachments only always has their password (isOpenPasswordRequired is
    // false for it: the document itself opens freely)
    return PDFDocumentToolEncryption::getOpenPasswordText(isOpenPasswordRequired || isAttachmentsOnly, isAttachmentsOnly);
}

PDFAppToolProtectionFacts PDFAppToolProtectionFacts::create(const PDFDocument* document)
{
    PDFAppToolProtectionFacts facts;
    const PDFSecurityHandler* securityHandler = document ? document->getStorage().getSecurityHandler() : nullptr;
    if (!securityHandler || securityHandler->getMode() == EncryptionMode::None)
    {
        return facts;
    }

    facts.mode = securityHandler->getMode();
    facts.algorithmTitle = getEncryptionTitle(document);
    facts.forbidden = PDFAppDocumentPermissions::getFileForbiddenPermissions(document);
    facts.isOwner = PDFAppDocumentPermissions::isAllowed(document, PDFAppPermissionRequest::create(PDFAppPermissionAction::ChangeSecurity));

    // File attachments only (7.6.6): the document itself opens without a password or a private
    // key, only its attachments need them (RS5SEC L5)
    facts.isAttachmentsOnly = securityHandler->isEmbeddedFilesOnlyEncryption();

    if (facts.mode == EncryptionMode::PublicKey)
    {
        // The private key opens it (7.6.5); its certificate names the holder (only public data)
        facts.isOpenPasswordRequired = !facts.isAttachmentsOnly;
        const std::optional<PDFCertificateInfo>& certificate = static_cast<const PDFPublicKeySecurityHandler*>(securityHandler)->getAuthorizedCertificate();
        if (certificate)
        {
            facts.recipientName = certificate->getName(PDFCertificateInfo::CommonName).trimmed();
        }
        return facts;
    }

    if (facts.isAttachmentsOnly)
    {
        return facts;
    }

    // The empty user password opens it, or not: a copy of the handler is asked (7.6.4.4), the
    // file is not read again and nothing of a password is kept
    facts.isOpenPasswordRequired = true;
    PDFSecurityHandlerPointer copy(securityHandler->clone());
    try
    {
        const PDFSecurityHandler::AuthorizationResult result = copy->authenticate(PDFAppToolEmptyPasswordCallback(), false);
        facts.isOpenPasswordRequired = result != PDFSecurityHandler::AuthorizationResult::UserAuthorized && result != PDFSecurityHandler::AuthorizationResult::OwnerAuthorized;
    }
    catch (const PDFException&)
    {
        // A damaged /Perms: the document was opened by a password
    }
    return facts;
}

// ------------------------------------------------------------ PDFAppToolGUIEncryption

PDFAppToolGUIEncryption::PDFAppToolGUIEncryption(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolGUIEncryption::~PDFAppToolGUIEncryption()
{
    // S5.1: the cards empty their fields when their panels go; the values of a run are dropped
    m_certificateRunValues.clear();
}

void PDFAppToolGUIEncryption::registerEncryptionTools()
{
    for (const Mode mode : { Mode::Password, Mode::Permissions, Mode::Certificate, Mode::Unlock })
    {
        const QString toolId = PDFDocumentToolEncryption::getToolId(mode);
        if (!PDFDocumentToolRegistry::isImplemented(toolId))
        {
            PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolEncryption>(mode));
        }
        if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
        {
            PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIEncryption>>());
        }
    }
}

PDFDocumentToolEncryption::Mode PDFAppToolGUIEncryption::getMode() const
{
    const PDFDocumentToolCatalogEntry* entry = getTool();
    const std::optional<PDFDocumentToolEncryption::Mode> mode = entry ? PDFDocumentToolEncryption::findMode(entry->getId()) : std::nullopt;
    return mode.value_or(PDFDocumentToolEncryption::Mode::Password);
}

PDFSecurityHandlerFactory::Algorithm PDFAppToolGUIEncryption::getAlgorithm() const
{
    using Tool = PDFDocumentToolEncryption;
    if (getMode() == Mode::Certificate)
    {
        return PDFSecurityHandlerFactory::Certificate;
    }
    if (getMode() == Mode::Unlock)
    {
        return PDFSecurityHandlerFactory::None;
    }

    const PDFAppToolParameterModel* model = getModel();
    const QString algorithm = model ? model->getValue(QString::fromLatin1(Tool::PARAMETER_ALGORITHM)).toString() : QString();
    if (algorithm == QLatin1String(Tool::ALGORITHM_RC4))
    {
        return PDFSecurityHandlerFactory::RC4;
    }
    if (algorithm == QLatin1String(Tool::ALGORITHM_AES_128))
    {
        return PDFSecurityHandlerFactory::AES_128;
    }
    return PDFSecurityHandlerFactory::AES_256;
}

PDFAppToolProtectionFacts PDFAppToolGUIEncryption::getFacts() const
{
    // Read once per version (the empty password is tried by a copy of the handler)
    if (!m_facts)
    {
        const PDFAppDocument* document = getDocument();
        m_facts = PDFAppToolProtectionFacts::create(document && document->isReady() ? document->getDocument().data() : nullptr);
    }
    return *m_facts;
}

bool PDFAppToolGUIEncryption::isOwnerPasswordRequired() const
{
    using Tool = PDFDocumentToolEncryption;
    switch (getMode())
    {
        case Mode::Password:
        {
            // Q5.1, K5.2, K5.19: the kept restrictions need an owner password of their own - with the
            // open password as the owner password every reader would be the owner
            const PDFAppToolParameterModel* model = getModel();
            const bool isKept = !model || model->getValue(QString::fromLatin1(Tool::PARAMETER_KEEP_RESTRICTIONS)).toBool();
            return isKept && !getFacts().forbidden.empty();
        }
        case Mode::Permissions:
            return true;
        case Mode::Certificate:
        case Mode::Unlock:
            break;
    }
    return false;
}

bool PDFAppToolGUIEncryption::isOwnerPasswordSeparate() const
{
    using Tool = PDFDocumentToolEncryption;
    if (getMode() != Mode::Password)
    {
        return getMode() == Mode::Permissions;
    }
    const PDFAppToolParameterModel* model = getModel();
    return isOwnerPasswordRequired() || (model && !model->getValue(QString::fromLatin1(Tool::PARAMETER_OWNER_SAME_AS_OPEN)).toBool());
}

PDFAppToolGUIEncryption::Preset PDFAppToolGUIEncryption::getCurrentPreset() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? PDFDocumentToolEncryption::findPreset(model->getValues()) : Preset::Custom;
}

PDFTileRow* PDFAppToolGUIEncryption::getPresetRow(Preset preset) const
{
    const auto it = m_presetRows.find(preset);
    return it != m_presetRows.end() ? it->second.data() : nullptr;
}

QString PDFAppToolGUIEncryption::getPresetTitle(Preset preset)
{
    switch (preset)
    {
        case Preset::ViewOnly:
            return tr("View only");
        case Preset::ViewAndPrint:
            return tr("View and print");
        case Preset::PrintAndFillForms:
            return tr("Print and fill forms");
        case Preset::AllowEverything:
            return tr("Allow everything");
        case Preset::Custom:
            return tr("Custom");
    }
    return QString();
}

QString PDFAppToolGUIEncryption::getScopeTitle() const
{
    // The title of the chosen option of the parameter Encrypt (the menu 430:1497, the summary 430:1258):
    // the words of the definition of the core, never a copy
    const QString name = QString::fromLatin1(PDFDocumentToolEncryption::PARAMETER_SCOPE);
    const PDFAppToolParameterModel* model = getModel();
    const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), name) : nullptr;
    if (!definition)
    {
        return QString();
    }

    const QString scope = model->getValues().value(name, definition->getDefaultValue()).toString();
    for (const PDFDocumentToolParameterOption& option : definition->getOptions())
    {
        if (option.id == scope)
        {
            return option.getTitle();
        }
    }
    return QString();
}

std::vector<PDFDocumentToolParameterError> PDFAppToolGUIEncryption::getCoreErrors() const
{
    // The rules of the passwords are the core's (Q5.1, 432:3721, the characters of the factory):
    // its validation with the values of a run; the certificate validates in the part of P3
    const Mode mode = getMode();
    const PDFAppDocument* document = getDocument();
    const PDFDocumentToolCatalogEntry* entry = getTool();
    if ((mode != Mode::Password && mode != Mode::Permissions) || !document || !document->isReady() || !entry)
    {
        return { };
    }
    const std::shared_ptr<const PDFDocumentTool> tool = PDFDocumentToolRegistry::getTool(entry->getId());
    if (!tool)
    {
        return { };
    }

    QVariantMap parameters = getRunParameters();
    const std::vector<PDFDocumentToolParameterError> errors = tool->validate(PDFDocumentToolRunner::createContext(document->getSnapshot()), parameters);

    // S5.1: the copies of the passwords go at once (best effort)
    for (auto it = parameters.begin(); it != parameters.end(); ++it)
    {
        if (it.value().typeId() == QMetaType::QString)
        {
            QString text = it.value().toString();
            it.value() = QVariant();
            PDFSecureMemory::clear(text);
        }
    }
    return errors;
}

QString PDFAppToolGUIEncryption::getFieldError(const std::vector<PDFDocumentToolParameterError>& errors, bool isOwner) const
{
    // An error of the core at a typed field (an empty field is no error yet, 430:1287); the owner
    // field takes also the error of the check box Use the open password for owner access too
    using Tool = PDFDocumentToolEncryption;
    const PDFAppToolPasswordCard* card = isOwner ? m_ownerCard.data() : m_openCard.data();
    if (!card || card->isEmpty())
    {
        return QString();
    }
    for (const PDFDocumentToolParameterError& error : errors)
    {
        const bool isOwnerError = error.parameter == QLatin1String(Tool::PARAMETER_OWNER_PASSWORD) || error.parameter == QLatin1String(Tool::PARAMETER_OWNER_SAME_AS_OPEN);
        const bool isOpenError = error.parameter == QLatin1String(Tool::PARAMETER_OPEN_PASSWORD);
        if (isOwner ? isOwnerError : isOpenError)
        {
            return error.message;
        }
    }
    return QString();
}

bool PDFAppToolGUIEncryption::isSamePasswordError() const
{
    // 432:3721, Q5.1: the core refuses the owner password, while both passwords are typed (the same
    // password for opening and for the owner gives every reader owner access)
    if (!m_openCard || !m_ownerCard || m_openCard->isEmpty() || m_ownerCard->isEmpty())
    {
        return false;
    }
    return !getFieldError(getCoreErrors(), true).isEmpty();
}

void PDFAppToolGUIEncryption::clearSecrets()
{
    // S5.1: the fields, their undo history, the password of the .pfx file and the values of the
    // run, which the GUI keeps between prepareEnvironment and getRunParameters
    if (m_openCard)
    {
        m_openCard->clear();
    }
    if (m_ownerCard)
    {
        m_ownerCard->clear();
    }
    if (m_certificatePart)
    {
        m_certificatePart->clearSecrets();
    }
    dropCertificateRunValues();
}

void PDFAppToolGUIEncryption::dropCertificateRunValues() const
{
    // The password of the .pfx file of prepareEnvironment is overwritten, when nobody else holds it
    for (auto it = m_certificateRunValues.begin(); it != m_certificateRunValues.end(); ++it)
    {
        if (it.value().typeId() == QMetaType::QString)
        {
            QString text = it.value().toString();
            it.value() = QVariant();
            PDFSecureMemory::clear(text);
        }
    }
    m_certificateRunValues.clear();
}

void PDFAppToolGUIEncryption::onRunRefused()
{
    // The run did not start (busy, or the confirmation was disabled meanwhile): the values prepared
    // for it go (S5.1, review RS5APP); the field of the password keeps its text for another try
    dropCertificateRunValues();
}

// ------------------------------------------------------------------- toolbar

void PDFAppToolGUIEncryption::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Tool = PDFDocumentToolEncryption;
    using Helper = PDFAppToolGUIEncryptionHelper;

    const Mode mode = getMode();
    m_presetRows.clear();
    if (mode == Mode::Permissions)
    {
        // 432:2698: the presets check the list, nothing runs; then the divider of 432:2203
        m_presetPanel = builder.addMenu(QStringLiteral("preset"), QStringLiteral("sliders"), QString(), PRESET_PANEL_WIDTH);
        if (m_presetPanel)
        {
            m_presetPanel->addGroupCaption(tr("Start from a preset"));
            const std::vector<std::tuple<Preset, const char*, QString>> presets = {
                { Preset::ViewOnly, QT_TR_NOOP("No printing, editing or copying"), QStringLiteral("book-open") },
                { Preset::ViewAndPrint, QT_TR_NOOP("Printing allowed, no changes or copying"), QStringLiteral("printer") },
                { Preset::PrintAndFillForms, QT_TR_NOOP("Readers can also fill in form fields"), QStringLiteral("form-fill") },
                { Preset::AllowEverything, QT_TR_NOOP("Only the security settings are protected"), QStringLiteral("unlock") },
            };
            for (const auto& [preset, description, iconName] : presets)
            {
                PDFTileRow* row = m_presetPanel->addRow(getPresetTitle(preset), tr(description), iconName, PDFTileRow::Kind::MenuItem);
                row->setObjectName(QStringLiteral("encryptionPreset-%1").arg(int(preset)));
                row->setProperty(Helper::PRESET_PROPERTY, int(preset));
                row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
                connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIEncryption::onPresetRowClicked);
                m_presetRows[preset] = row;
            }
            m_presetPanel->addGroupDivider();
            m_presetPanel->addGroupCaption(tr("A preset only checks the list – adjust it freely afterwards"));
        }
        builder.addActionDivider();
    }

    if (mode == Mode::Password || mode == Mode::Permissions)
    {
        // 430:1497: Algorithm and Encrypt
        builder.addEnumMenu(QString::fromLatin1(Tool::PARAMETER_ALGORITHM), QStringLiteral("key"), tr("Algorithm: %1"), ALGORITHM_PANEL_WIDTH, tr("Encryption algorithm"));
    }
    if (mode != Mode::Unlock)
    {
        builder.addEnumMenu(QString::fromLatin1(Tool::PARAMETER_SCOPE), QStringLiteral("file-text"), tr("Encrypt: %1"), SCOPE_PANEL_WIDTH, tr("What is encrypted"));
    }
    if (mode == Mode::Certificate)
    {
        if (!m_certificatePart)
        {
            m_certificatePart = new PDFAppToolCertificatePresetPart(getContext(), this);
            connect(m_certificatePart.data(), &PDFAppToolCertificatePresetPart::stateChanged, this, &PDFAppToolGUIEncryption::onCertificateStateChanged);
        }
        m_certificatePart->describeToolbar(builder);
    }

    updateParts();
}

// ---------------------------------------------------------------- left panel

void PDFAppToolGUIEncryption::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    // header 430:1171: Current protection and the file, section-Now 430:1175
    const PDFAppDocument* document = getDocument();
    builder.addPanelHeader(tr("Current protection"), document ? document->getTitle() : QString());
    builder.addSection(tr("Now"));
    m_nowRows = builder.addKeyValues();
    m_nowRows->setObjectName(QStringLiteral("encryptionNow"));
    m_nowRows->setRowGap(PDFAppToolGUIEncryptionHelper::KEY_VALUE_ROW_GAP);
    builder.endSection();

    switch (getMode())
    {
        case Mode::Password:
        {
            // 430:2481: the current protection is replaced; 430:1188: a lost password
            m_replacedCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), QString());
            m_replacedCallout->setObjectName(QStringLiteral("encryptionReplacedCallout"));
            PDFAppToolGUIEncryptionHelper::setCompactStyle(m_replacedCallout);
            m_lostCallout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), tr("A lost password cannot be recovered"),
                                               tr("PDF4QT does not store passwords. Keep a copy of the password in a safe place."));
            m_lostCallout->setObjectName(QStringLiteral("encryptionLostCallout"));
            PDFAppToolGUIEncryptionHelper::setCompactStyle(m_lostCallout);
            break;
        }

        case Mode::Permissions:
        {
            // section-Owner password 432:2232, section-Open password 432:2275
            m_replacedCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), QString());
            m_replacedCallout->setObjectName(QStringLiteral("encryptionReplacedCallout"));
            PDFAppToolGUIEncryptionHelper::setCompactStyle(m_replacedCallout);
            builder.addSection(tr("Owner password"));
            builder.addWidget(createOwnerPasswordCard(builder.getWidget()));
            builder.endSection();
            builder.addSection(tr("Open password"));
            builder.addWidget(createOpenPasswordCard(builder.getWidget()));
            builder.endSection();
            break;
        }

        case Mode::Certificate:
        {
            // 433:3049: the private key is the only way in
            m_replacedCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), QString());
            m_replacedCallout->setObjectName(QStringLiteral("encryptionReplacedCallout"));
            PDFAppToolGUIEncryptionHelper::setCompactStyle(m_replacedCallout);
            m_lostCallout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), tr("Without the private key the document is lost"),
                                               tr("Keep a backup of the certificate file and its password. PDF4QT cannot open the document without them."));
            m_lostCallout->setObjectName(QStringLiteral("encryptionLostCallout"));
            PDFAppToolGUIEncryptionHelper::setCompactStyle(m_lostCallout);
            break;
        }

        case Mode::Unlock:
        {
            // 433:5135 / 433:5811: PDF4QT never breaks a protection (tools.md 5.4)
            m_infoCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), QString());
            m_infoCallout->setObjectName(QStringLiteral("encryptionInfoCallout"));
            PDFAppToolGUIEncryptionHelper::setCompactStyle(m_infoCallout);
            break;
        }
    }

    updateParts();
}

QWidget* PDFAppToolGUIEncryption::createOwnerPasswordCard(QWidget* parent)
{
    using Helper = PDFAppToolGUIEncryptionHelper;

    // card-owner-password 432:2235: the hint, Password, the meter, Repeat password
    PDFAppToolCard* card = Helper::createPanelCard(QStringLiteral("encryptionOwnerPasswordCard"), parent);
    card->getBodyLayout()->addWidget(Helper::createText(tr("Whoever knows this password can change or remove the restrictions."), true, card));
    m_ownerCard = new PDFAppToolPasswordCard(PDFAppToolPasswordCard::Kind::Owner, card);
    m_ownerCard->setObjectName(QStringLiteral("encryptionOwnerPassword"));
    m_ownerCard->setGap(PDFAppToolPasswordCard::PANEL_GAP);
    m_ownerCard->setAccessibleLabels(tr("Owner password"), tr("Repeat owner password"));
    m_ownerCard->setAlgorithm(getAlgorithm());
    connect(m_ownerCard.data(), &PDFAppToolPasswordCard::passwordChanged, this, &PDFAppToolGUIEncryption::onPasswordChanged);
    card->getBodyLayout()->addWidget(m_ownerCard);
    m_ownerCardFrame = card;
    return card;
}

QWidget* PDFAppToolGUIEncryption::createOpenPasswordCard(QWidget* parent)
{
    using Tool = PDFDocumentToolEncryption;
    using Helper = PDFAppToolGUIEncryptionHelper;

    // card-open-password 432:2278: Also require a password to open, its hint; checked (432:3377)
    // the password card instead of the hint
    PDFAppToolCard* card = Helper::createPanelCard(QStringLiteral("encryptionOpenPasswordCard"), parent);
    m_requireOpenCheck = new QCheckBox(tr("Also require a password to open"), card);
    m_requireOpenCheck->setObjectName(QStringLiteral("encryptionRequireOpen"));
    const PDFAppToolParameterModel* model = getModel();
    m_requireOpenCheck->setChecked(model && model->getValue(QString::fromLatin1(Tool::PARAMETER_REQUIRE_OPEN_PASSWORD)).toBool());
    // clicked, not toggled: QCheckBox works after toggled (checkStateSet), a slot of it may not
    // delete the check box - clicked is the last, guarded emission of a click
    connect(m_requireOpenCheck.data(), &QCheckBox::clicked, this, &PDFAppToolGUIEncryption::onRequireOpenToggled);
    card->getBodyLayout()->addWidget(m_requireOpenCheck);

    m_requireOpenHint = Helper::createText(tr("Without it anyone can open the document, but only do what is allowed here."), true, card);
    m_requireOpenHint->setObjectName(QStringLiteral("encryptionRequireOpenHint"));
    card->getBodyLayout()->addWidget(m_requireOpenHint);

    m_openCard = new PDFAppToolPasswordCard(PDFAppToolPasswordCard::Kind::Open, card);
    m_openCard->setObjectName(QStringLiteral("encryptionOpenPassword"));
    m_openCard->setGap(PDFAppToolPasswordCard::PANEL_GAP);
    m_openCard->setAccessibleLabels(tr("Open password"), tr("Repeat open password"));
    m_openCard->setAlgorithm(getAlgorithm());
    connect(m_openCard.data(), &PDFAppToolPasswordCard::passwordChanged, this, &PDFAppToolGUIEncryption::onPasswordChanged);
    card->getBodyLayout()->addWidget(m_openCard);
    m_openCardFrame = card;
    return card;
}

// --------------------------------------------------------------- right panel

void PDFAppToolGUIEncryption::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    switch (getMode())
    {
        case Mode::Password:
        case Mode::Certificate:
        {
            // 430:1251 After protecting / 433:3049 After encrypting: Encryption and the strength
            const bool isPassword = getMode() == Mode::Password;
            builder.addPanelHeader(isPassword ? tr("After protecting") : tr("After encrypting"), tr("Summary of the result"));
            builder.addSection(tr("Encryption"));
            m_summaryRows = builder.addKeyValues();
            m_summaryRows->setObjectName(QStringLiteral("encryptionSummary"));
            m_summaryRows->setRowGap(PDFAppToolGUIEncryptionHelper::KEY_VALUE_ROW_GAP);
            builder.endSection();

            builder.addSection(isPassword ? tr("Algorithm strength") : tr("Encryption strength"));
            PDFAppToolCard* card = PDFAppToolGUIEncryptionHelper::createPanelCard(QStringLiteral("encryptionStrengthCard"), builder.getWidget());
            m_algorithmMeter = new PDFStrengthMeter(card);
            m_algorithmMeter->setObjectName(QStringLiteral("encryptionAlgorithmMeter"));
            m_algorithmMeter->setDetailVisible(false);
            m_algorithmMeter->setCriteriaVisible(false);
            card->getBodyLayout()->addWidget(m_algorithmMeter);
            m_algorithmText = PDFAppToolGUIEncryptionHelper::createText(QString(), true, card);
            m_algorithmText->setObjectName(QStringLiteral("encryptionAlgorithmText"));
            card->getBodyLayout()->addWidget(m_algorithmText);
            builder.addWidget(card);
            builder.endSection();
            break;
        }

        case Mode::Permissions:
        {
            // 432:2366 After applying: Readers, Encryption and the callout of 432:2402
            builder.addPanelHeader(tr("After applying"), tr("Summary of the result"));
            builder.addSection(tr("Readers"));
            m_readersRows = builder.addKeyValues();
            m_readersRows->setObjectName(QStringLiteral("encryptionReaders"));
            m_readersRows->setRowGap(PDFAppToolGUIEncryptionHelper::KEY_VALUE_ROW_GAP);
            builder.endSection();
            builder.addSection(tr("Encryption"));
            m_summaryRows = builder.addKeyValues();
            m_summaryRows->setObjectName(QStringLiteral("encryptionSummary"));
            m_summaryRows->setRowGap(PDFAppToolGUIEncryptionHelper::KEY_VALUE_ROW_GAP);
            builder.endSection();
            PDFAppToolCallout* callout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("Restrictions rely on the reader"),
                                                            tr("PDF readers that respect the standard enforce these limits. They do not stop someone determined – use an open password for confidential content."));
            callout->setObjectName(QStringLiteral("encryptionReaderCallout"));
            PDFAppToolGUIEncryptionHelper::setCompactStyle(callout);
            break;
        }

        case Mode::Unlock:
        {
            // 433:5193 After removing: Protection
            builder.addPanelHeader(tr("After removing"), tr("Summary of the result"));
            builder.addSection(tr("Protection"));
            m_summaryRows = builder.addKeyValues();
            m_summaryRows->setObjectName(QStringLiteral("encryptionSummary"));
            m_summaryRows->setRowGap(PDFAppToolGUIEncryptionHelper::KEY_VALUE_ROW_GAP);
            builder.endSection();
            break;
        }
    }

    updateParts();
}

// ---------------------------------------------------------------------- canvas

QWidget* PDFAppToolGUIEncryption::createCanvasPage(QWidget* parent)
{
    m_canvasPage = new PDFAppToolCanvasPage(parent);
    buildForm();
    return m_canvasPage.data();
}

void PDFAppToolGUIEncryption::buildForm()
{
    if (!m_canvasPage)
    {
        return;
    }

    // The old form is retired from the event loop: it may hold the sender of the change. Taken out of the scroll area
    // it has no parent: it belongs to this GUI until then (a retire of the tool meanwhile retires it too)
    if (QWidget* old = m_canvasPage->takeWidget())
    {
        PDFDeferredDeletion::setOwner(old, this);
        PDFDeferredDeletion::retireLater(old);
    }

    const Mode mode = getMode();
    const bool isWide = mode == Mode::Permissions || mode == Mode::Certificate;
    QWidget* content = new QWidget();
    content->setObjectName(QStringLiteral("encryptionContent"));
    QHBoxLayout* centering = new QHBoxLayout(content);
    centering->setContentsMargins(0, FORM_TOP, 0, FORM_TOP);

    // form 430:1194 (620) / 432:2283 (640) in the middle of the canvas
    QWidget* form = new QWidget(content);
    form->setObjectName(QStringLiteral("encryptionForm"));
    form->setFixedWidth(isWide ? WIDE_FORM_WIDTH : FORM_WIDTH);
    QVBoxLayout* layout = new QVBoxLayout(form);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(FORM_GAP);
    centering->addStretch(1);
    centering->addWidget(form, 0, Qt::AlignTop);
    centering->addStretch(1);
    m_form = form;

    if (mode != Mode::Certificate)
    {
        // heading 430:1195: Semi Bold 18 in lines of 22, the text Regular 12 text/secondary, 2 apart
        QWidget* heading = new QWidget(form);
        heading->setObjectName(QStringLiteral("encryptionHeading"));
        QVBoxLayout* headingLayout = new QVBoxLayout(heading);
        headingLayout->setContentsMargins(0, 0, 0, 0);
        headingLayout->setSpacing(2);
        m_headingTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 18, PDFThemeColors::TextPrimary, heading);
        m_headingTitle->setLineHeight(22);
        headingLayout->addWidget(m_headingTitle);
        m_headingText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, heading);
        m_headingText->setMinimumWidth(1);
        headingLayout->addWidget(m_headingText);
        layout->addWidget(heading);
    }

    switch (mode)
    {
        case Mode::Password:
            buildPasswordForm(form);
            break;
        case Mode::Permissions:
            buildPermissionsForm(form);
            break;
        case Mode::Certificate:
            buildCertificateForm(form);
            break;
        case Mode::Unlock:
            buildUnlockForm(form);
            break;
    }

    m_canvasPage->setWidget(content);
    updateParts();
}

void PDFAppToolGUIEncryption::buildPasswordForm(QWidget* form)
{
    using Tool = PDFDocumentToolEncryption;
    using Helper = PDFAppToolGUIEncryptionHelper;

    QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(form->layout());

    // card-open-password 430:1198: Open password, the field, the meter, Repeat password
    PDFAppToolCard* openCard = Helper::createCanvasCard(QStringLiteral("encryptionOpenPasswordCard"), tr("Open password"), form, nullptr);
    m_openCard = new PDFAppToolPasswordCard(PDFAppToolPasswordCard::Kind::Open, openCard);
    m_openCard->setObjectName(QStringLiteral("encryptionOpenPassword"));
    m_openCard->setAccessibleLabels(tr("Open password"), tr("Repeat open password"));
    m_openCard->setAlgorithm(getAlgorithm());
    connect(m_openCard.data(), &PDFAppToolPasswordCard::passwordChanged, this, &PDFAppToolGUIEncryption::onPasswordChanged);
    openCard->getBodyLayout()->addWidget(m_openCard);
    layout->addWidget(openCard);
    m_openCardFrame = openCard;

    // card-owner-password 430:1241 / 430:2228: the text, the check box, the owner password
    PDFAppToolCard* ownerCard = Helper::createCanvasCard(QStringLiteral("encryptionOwnerPasswordCard"), tr("Owner password"), form, nullptr);
    ownerCard->getBodyLayout()->addWidget(Helper::createText(tr("The owner password is needed to change or remove the protection later."), false, ownerCard));
    m_ownerSameCheck = new QCheckBox(tr("Use the open password for owner access too"), ownerCard);
    m_ownerSameCheck->setObjectName(QStringLiteral("encryptionOwnerSame"));
    const PDFAppToolParameterModel* model = getModel();
    m_ownerSameCheck->setChecked(model && model->getValue(QString::fromLatin1(Tool::PARAMETER_OWNER_SAME_AS_OPEN)).toBool());
    // clicked, not toggled (as the check box of the open password above)
    connect(m_ownerSameCheck.data(), &QCheckBox::clicked, this, &PDFAppToolGUIEncryption::onOwnerSameToggled);
    ownerCard->getBodyLayout()->addWidget(m_ownerSameCheck);

    // Q5.1: why the check box is off and disabled (no frame: the text of the field explains it)
    m_ownerSameHint = Helper::createText(QString(), true, ownerCard);
    m_ownerSameHint->setObjectName(QStringLiteral("encryptionOwnerSameHint"));
    ownerCard->getBodyLayout()->addWidget(m_ownerSameHint);

    m_ownerCard = new PDFAppToolPasswordCard(PDFAppToolPasswordCard::Kind::Owner, ownerCard);
    m_ownerCard->setObjectName(QStringLiteral("encryptionOwnerPassword"));
    m_ownerCard->setLabels(tr("Owner password"), tr("Repeat owner password"));
    m_ownerCard->setAlgorithm(getAlgorithm());
    connect(m_ownerCard.data(), &PDFAppToolPasswordCard::passwordChanged, this, &PDFAppToolGUIEncryption::onPasswordChanged);
    ownerCard->getBodyLayout()->addWidget(m_ownerCard);
    layout->addWidget(ownerCard);
    m_ownerCardFrame = ownerCard;

    // 430:1776 a weak password (amber), 430:1999 RC4 (red)
    m_weakCallout = Helper::createCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QStringLiteral("encryptionWeakCallout"), form);
    layout->addWidget(m_weakCallout);
    m_legacyCallout = Helper::createCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), QStringLiteral("encryptionLegacyCallout"), form);
    layout->addWidget(m_legacyCallout);
}

void PDFAppToolGUIEncryption::buildPermissionsForm(QWidget* form)
{
    using Helper = PDFAppToolGUIEncryptionHelper;

    // 432:2287 ... 432:2345: the three cards; 432:4069: Nothing is restricted; the legacy
    // algorithm as Protect with Password says it (430:1999)
    QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(form->layout());
    m_permissionList = new PDFAppToolPermissionList(PDFAppToolPermissionList::Layout::Grouped, form);
    m_permissionList->setObjectName(QStringLiteral("encryptionPermissions"));
    if (const PDFAppToolParameterModel* model = getModel())
    {
        m_permissionList->setValues(model->getValues());
    }
    connect(m_permissionList.data(), &PDFAppToolPermissionList::valuesEdited, this, &PDFAppToolGUIEncryption::onPermissionsEdited);
    layout->addWidget(m_permissionList);

    m_nothingRestrictedCallout = Helper::createCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QStringLiteral("encryptionNothingRestrictedCallout"), form);
    layout->addWidget(m_nothingRestrictedCallout);
    m_legacyCallout = Helper::createCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), QStringLiteral("encryptionLegacyCallout"), form);
    layout->addWidget(m_legacyCallout);
}

void PDFAppToolGUIEncryption::buildCertificateForm(QWidget* form)
{
    // The heading, the list and the password of package P3 (433:3182 ... 433:3221), the card of
    // the permissions below it (433:3239, 433:4426: collapsed by default, Everything is allowed)
    QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(form->layout());
    if (!m_certificatePart)
    {
        m_certificatePart = new PDFAppToolCertificatePresetPart(getContext(), this);
        connect(m_certificatePart.data(), &PDFAppToolCertificatePresetPart::stateChanged, this, &PDFAppToolGUIEncryption::onCertificateStateChanged);
    }
    if (QWidget* content = m_certificatePart->createCanvasContent(form))
    {
        layout->addWidget(content);
    }

    PDFAppToolCard* card = new PDFAppToolCard(form);
    card->setObjectName(QStringLiteral("encryptionPermissionCard"));
    card->getBodyLayout()->setContentsMargins(15, 15, 10, 15);
    card->getBodyLayout()->setSpacing(PDFAppToolPasswordCard::PANEL_GAP);

    // The head: the title and the summary (collapsed) or the caption (open), the chevron
    QWidget* header = new QWidget(card);
    header->setObjectName(QStringLiteral("encryptionPermissionHeader"));
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(PDFAppToolPasswordCard::PANEL_GAP);
    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    m_permissionTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, header);
    m_permissionTitle->setObjectName(QStringLiteral("encryptionPermissionTitle"));
    textLayout->addWidget(m_permissionTitle);
    m_permissionSummary = PDFAppToolGUIEncryptionHelper::createText(QString(), false, header);
    m_permissionSummary->setObjectName(QStringLiteral("encryptionPermissionSummary"));
    textLayout->addWidget(m_permissionSummary);
    headerLayout->addLayout(textLayout, 1);
    m_permissionToggle = new PDFIconButton(QStringLiteral("chevron-down"), PDFIconButton::Size::Small, header);
    m_permissionToggle->setObjectName(QStringLiteral("encryptionPermissionToggle"));
    connect(m_permissionToggle.data(), &QAbstractButton::clicked, this, &PDFAppToolGUIEncryption::onPermissionToggleClicked);
    headerLayout->addWidget(m_permissionToggle, 0, Qt::AlignVCenter);
    card->getBodyLayout()->addWidget(header);
    m_permissionHeader = header;

    m_permissionList = new PDFAppToolPermissionList(PDFAppToolPermissionList::Layout::Compact, card);
    m_permissionList->setObjectName(QStringLiteral("encryptionPermissions"));
    if (const PDFAppToolParameterModel* model = getModel())
    {
        m_permissionList->setValues(model->getValues());
    }
    connect(m_permissionList.data(), &PDFAppToolPermissionList::valuesEdited, this, &PDFAppToolGUIEncryption::onPermissionsEdited);
    card->getBodyLayout()->addWidget(m_permissionList);
    layout->addWidget(card);
    m_permissionCard = card;
}

void PDFAppToolGUIEncryption::buildUnlockForm(QWidget* form)
{
    using Helper = PDFAppToolGUIEncryptionHelper;

    // card-changes 433:5145 and the callout 433:5187; 433:5671: the card of a document, which is
    // not protected (the tile of the open lock, the title Semi Bold 15, the text)
    QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(form->layout());
    m_changeList = new PDFAppToolChangeList(form);
    m_changeList->setObjectName(QStringLiteral("encryptionChanges"));
    layout->addWidget(m_changeList);

    m_removeCallout = Helper::createCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QStringLiteral("encryptionRemoveCallout"), form);
    layout->addWidget(m_removeCallout);

    PDFAppToolCard* empty = new PDFAppToolCard(form);
    empty->setObjectName(QStringLiteral("encryptionNotProtected"));
    empty->getBodyLayout()->setContentsMargins(15, 28, 15, 28);
    empty->getBodyLayout()->setSpacing(10);
    PDFIconBadge* tile = new PDFIconBadge(QStringLiteral("unlock"), 44, 18, empty);
    tile->setRadius(8);
    tile->setColors(PDFThemeColors::AccentBlueBg, PDFThemeColors::AccentBlueIcon);
    tile->setFixedSize(44, 44);
    empty->getBodyLayout()->addWidget(tile, 0, Qt::AlignHCenter);
    PDFLabel* title = PDFAppPageWidgets::createLabel(tr("This document is not protected"), QFont::DemiBold, 15, PDFThemeColors::TextPrimary, empty);
    title->setObjectName(QStringLiteral("encryptionNotProtectedTitle"));
    title->setAlignment(Qt::AlignHCenter);
    empty->getBodyLayout()->addWidget(title, 0, Qt::AlignHCenter);
    PDFAppParagraph* text = Helper::createText(tr("Anyone can already open, print, copy and edit it."), false, empty);
    text->setAlignment(Qt::AlignHCenter);
    empty->getBodyLayout()->addWidget(text);
    layout->addWidget(empty);
    m_emptyCard = empty;
}

// --------------------------------------------------------------------- updates

void PDFAppToolGUIEncryption::updateParts()
{
    using Helper = PDFAppToolGUIEncryptionHelper;

    const Mode mode = getMode();
    const PDFSecurityHandlerFactory::Algorithm algorithm = getAlgorithm();
    if (m_openCard)
    {
        m_openCard->setAlgorithm(algorithm);
    }
    if (m_ownerCard)
    {
        m_ownerCard->setAlgorithm(algorithm);
    }

    // The heading of the canvas (430:1195, 432:2284, 433:5142 / 433:5671)
    const PDFAppDocument* document = getDocument();
    const PDFAppToolProtectionFacts facts = getFacts();
    switch (mode)
    {
        case Mode::Password:
            if (m_headingTitle)
            {
                m_headingTitle->setText(tr("Require a password to open the document"));
                m_headingText->setText(tr("Everyone who opens the file has to type this password first."));
            }
            break;
        case Mode::Permissions:
            if (m_headingTitle)
            {
                m_headingTitle->setText(tr("Choose what readers may do"));
                m_headingText->setText(tr("Checked actions stay allowed. Everything else needs the owner password."));
            }
            break;
        case Mode::Unlock:
            if (m_headingTitle)
            {
                const bool isProtected = facts.mode != EncryptionMode::None;
                m_headingTitle->setText(isProtected ? tr("Remove the protection") : tr("Nothing to remove"));
                m_headingText->setText(isProtected ? tr("The document will open without a password and without any restrictions.")
                                                   : tr("%1 has no password, encryption or restrictions.").arg(document ? document->getTitle() : QString()));
            }
            break;
        case Mode::Certificate:
            break;
    }

    // The owner password of Protect with Password (430:1241, 430:2228, Q5.1)
    if (mode == Mode::Password)
    {
        const bool isRequired = isOwnerPasswordRequired();
        if (m_ownerSameCheck)
        {
            const QSignalBlocker blocker(m_ownerSameCheck);
            const PDFAppToolParameterModel* model = getModel();
            m_ownerSameCheck->setEnabled(!isRequired);
            m_ownerSameCheck->setChecked(!isRequired && model && model->getValue(QString::fromLatin1(PDFDocumentToolEncryption::PARAMETER_OWNER_SAME_AS_OPEN)).toBool());
        }
        if (m_ownerSameHint)
        {
            m_ownerSameHint->setText(isRequired ? tr("The document keeps its restrictions (%1), so it needs an owner password of its own – with the open password every reader would get owner access.")
                                                      .arg(PDFAppToolProtectionFacts::getPermissionsText(facts.forbidden).toLower())
                                                : QString());
            m_ownerSameHint->setVisible(isRequired);
        }
        Helper::setVisible(m_ownerCard, isOwnerPasswordSeparate());
    }

    // The open password of Restrict Permissions (432:2278, 432:3377)
    if (mode == Mode::Permissions)
    {
        const bool isOpenRequired = m_requireOpenCheck && m_requireOpenCheck->isChecked();
        Helper::setVisible(m_requireOpenHint, !isOpenRequired);
        Helper::setVisible(m_openCard, isOpenRequired);
        if (m_permissionList)
        {
            if (const PDFAppToolParameterModel* model = getModel())
            {
                m_permissionList->setValues(model->getValues());
            }
        }

        const Preset preset = getCurrentPreset();
        if (m_presetPanel && m_presetPanel->getButton())
        {
            m_presetPanel->getButton()->setText(tr("Preset: %1").arg(getPresetTitle(preset)));
            m_presetPanel->getButton()->setToolTip(m_presetPanel->getButton()->text());
        }
        for (const auto& [rowPreset, row] : m_presetRows)
        {
            if (row)
            {
                const QSignalBlocker blocker(row);
                row->setChecked(rowPreset == preset);
            }
        }
    }

    updatePasswordErrors();
    updatePermissionCard();
    updateUnlockForm();
    updateNowRows();
    updateSummary();
    updateCallouts();
}

void PDFAppToolGUIEncryption::updatePasswordErrors()
{
    // The texts of the validation of the core (Q5.1, 432:3721): the text at the owner password; the
    // open password is red without a text, when the core says the same of it (432:3721)
    const std::vector<PDFDocumentToolParameterError> errors = getCoreErrors();
    const QString ownerError = getFieldError(errors, true);
    const QString openError = getFieldError(errors, false);
    if (m_ownerCard)
    {
        m_ownerCard->setErrorText(ownerError);
    }
    if (m_openCard)
    {
        m_openCard->setErrorText(openError == ownerError ? QString() : openError);
        if (m_openCard->getPasswordField())
        {
            m_openCard->getPasswordField()->setInvalid(!openError.isEmpty() || m_openCard->hasInvalidCharacters());
        }
    }
}

void PDFAppToolGUIEncryption::updatePermissionCard()
{
    if (getMode() != Mode::Certificate)
    {
        return;
    }

    // 433:3239 collapsed: the title and "Everything is allowed. Open to limit printing, editing or
    // copying."; 433:4426 open: the caption and the rows; hidden in the empty state 433:3282
    const PDFAppToolParameterModel* model = getModel();
    const int allowed = model ? PDFAppToolGUIEncryptionHelper::getAllowedCount(model->getValues()) : PDFAppToolPermissionList::PERMISSION_COUNT;
    const bool isEverything = allowed == PDFAppToolPermissionList::PERMISSION_COUNT;
    if (m_permissionCard)
    {
        m_permissionCard->setVisible(!m_certificatePart || m_certificatePart->hasCertificates());
    }

    // 433:3239 collapsed: padding 14 + the border, the chevron at the inner edge; 433:4426 open: the
    // card of the list as 432:2105 - padding 8 + the border, the caption of 24 (its title 10 in,
    // the chevron 10 from the edge), the rows 2 apart (the icon of 14 sits 5 inside of its button)
    if (PDFAppToolCard* card = qobject_cast<PDFAppToolCard*>(m_permissionCard.data()))
    {
        card->getBodyLayout()->setContentsMargins(m_isPermissionsExpanded ? QMargins(9, 9, 9, 9) : QMargins(15, 15, 10, 15));
        card->getBodyLayout()->setSpacing(m_isPermissionsExpanded ? 2 : PDFAppToolPasswordCard::PANEL_GAP);
    }
    if (m_permissionHeader && m_permissionHeader->layout())
    {
        m_permissionHeader->layout()->setContentsMargins(m_isPermissionsExpanded ? QMargins(10, 0, 5, 0) : QMargins());
    }
    if (m_permissionTitle)
    {
        m_permissionTitle->setText(tr("Permissions of whoever opens the document"));
        m_permissionTitle->setFont(m_isPermissionsExpanded ? PDFThemeFonts::getFont(QFont::DemiBold, 11) : PDFThemeFonts::getFont(QFont::DemiBold, 13));
        m_permissionTitle->setLineHeight(m_isPermissionsExpanded ? 13 : 16);
    }
    if (m_permissionSummary)
    {
        m_permissionSummary->setText(isEverything ? tr("Everything is allowed. Open to limit printing, editing or copying.")
                                                  : tr("%1 of %2 actions are allowed. Open to change them.").arg(allowed).arg(PDFAppToolPermissionList::PERMISSION_COUNT));
        m_permissionSummary->setVisible(!m_isPermissionsExpanded);
    }
    if (m_permissionToggle)
    {
        m_permissionToggle->setIconName(m_isPermissionsExpanded ? QStringLiteral("chevron-up") : QStringLiteral("chevron-down"));
        const QString toggleText = m_isPermissionsExpanded ? tr("Hide the permissions") : tr("Show the permissions");
        m_permissionToggle->setToolTip(toggleText);
        m_permissionToggle->setAccessibleName(toggleText);
    }
    PDFAppToolGUIEncryptionHelper::setVisible(m_permissionList, m_isPermissionsExpanded);
    if (m_permissionList && model)
    {
        m_permissionList->setValues(model->getValues());
    }

    // R5-21 (Q5.3 b): restricted permissions lock the holder out of a later change
    if (m_certificatePart)
    {
        m_certificatePart->setPermissionsRestricted(!isEverything);
    }
}

void PDFAppToolGUIEncryption::updateUnlockForm()
{
    if (getMode() != Mode::Unlock)
    {
        return;
    }

    const PDFAppToolProtectionFacts facts = getFacts();
    const bool isProtected = facts.mode != EncryptionMode::None;
    PDFAppToolGUIEncryptionHelper::setVisible(m_changeList, isProtected);
    PDFAppToolGUIEncryptionHelper::setVisible(m_removeCallout, isProtected);
    PDFAppToolGUIEncryptionHelper::setVisible(m_emptyCard, !isProtected);
    if (!m_changeList || !isProtected)
    {
        return;
    }

    // 433:5022 / 433:5811: Encryption, Open password (Private key), Permissions: before › after
    const bool isCertificate = facts.mode == EncryptionMode::PublicKey;
    const QString permissionsBefore = PDFAppToolProtectionFacts::getPermissionsText(facts.forbidden);
    std::vector<PDFAppToolChangeList::Change> changes;
    PDFAppToolChangeList::Change encryption;
    encryption.title = tr("Encryption");
    encryption.description = isCertificate ? tr("Contents are encrypted for one certificate") : tr("Contents are encrypted in the file");
    encryption.iconName = QStringLiteral("key");
    encryption.before = facts.algorithmTitle;
    encryption.after = PDFDocumentToolEncryption::getNoEncryptionText();
    changes.push_back(encryption);

    PDFAppToolChangeList::Change password;
    password.title = isCertificate ? tr("Private key") : tr("Open password");
    password.description = isCertificate ? tr("Needed to open the document") : tr("Asked every time the document is opened");
    password.iconName = isCertificate ? QStringLiteral("certificate") : QStringLiteral("password");
    password.before = facts.getOpenPasswordText();
    password.after = tr("Not needed");
    changes.push_back(password);

    PDFAppToolChangeList::Change permissions;
    permissions.title = tr("Permissions");
    permissions.description = PDFAppToolGUIEncryptionHelper::getRestrictedText(facts.forbidden);
    permissions.iconName = QStringLiteral("shield-lock");
    permissions.before = permissionsBefore;
    permissions.after = PDFDocumentToolEncryption::getAllAllowedText();
    changes.push_back(permissions);

    m_changeList->setCaption(tr("What changes"));
    m_changeList->setChanges(std::move(changes));
}

void PDFAppToolGUIEncryption::updateNowRows()
{
    using Helper = PDFAppToolGUIEncryptionHelper;

    // section-Now 430:1175 (not encrypted), 430:2481 (with Your access, K5.13: in every frame of
    // an encrypted document), 433:5811 (a certificate: Opens for, Your access Private key)
    const PDFAppToolProtectionFacts facts = getFacts();
    std::vector<std::pair<QString, QString>> rows;
    const QString permissions = PDFAppToolProtectionFacts::getPermissionsText(facts.forbidden);
    switch (facts.mode)
    {
        case EncryptionMode::None:
            rows = { { tr("Encryption"), PDFDocumentToolEncryption::getNoEncryptionText() }, { tr("Open password"), facts.getOpenPasswordText() },
                     { tr("Permissions"), permissions } };
            break;
        case EncryptionMode::PublicKey:
        {
            // 433:5811: the holder of the certificate, whose private key opened the document (the
            // handler keeps its public certificate); a certificate without a common name is the
            // private key holder
            QString opensFor = !facts.recipientName.isEmpty() ? facts.recipientName : tr("Private key holder");
            if (facts.isAttachmentsOnly)
            {
                // The words of the fact of the core (RS5SEC L5): the document opens for everyone
                opensFor = tr("Everyone · attachments: %1").arg(opensFor);
            }
            rows = { { tr("Encryption"), facts.algorithmTitle }, { tr("Opens for"), opensFor }, { tr("Permissions"), permissions },
                     { tr("Your access"), tr("Private key") } };
            break;
        }
        default:
            rows = { { tr("Encryption"), facts.algorithmTitle }, { tr("Open password"), facts.getOpenPasswordText() },
                     { tr("Permissions"), permissions }, { tr("Your access"), facts.isOwner ? tr("Owner") : tr("Reader") } };
            break;
    }
    Helper::setRows(m_nowRows, rows);
}

void PDFAppToolGUIEncryption::updateSummary()
{
    using Tool = PDFDocumentToolEncryption;
    using Helper = PDFAppToolGUIEncryptionHelper;

    const PDFAppToolParameterModel* model = getModel();
    const QVariantMap values = model ? model->getValues() : QVariantMap();
    const PDFAppToolProtectionFacts facts = getFacts();
    const QString algorithmTitle = PDFAppToolPasswordCard::getAlgorithmTitle(getAlgorithm());
    const QString notEntered = tr("Not entered yet");

    // File attachments only: the result opens without a password (RS5SEC L5)
    const bool isAttachmentsScope = values.value(Helper::name(Tool::PARAMETER_SCOPE)).toString() == QLatin1String(Tool::SCOPE_ATTACHMENTS);
    const QString required = Tool::getOpenPasswordText(true, isAttachmentsScope);

    switch (getMode())
    {
        case Mode::Password:
        {
            // 430:1258: Algorithm, Encrypted, Open password, Owner password, Permissions (430:2481 "· kept")
            const bool isOpen = m_openCard && !m_openCard->isEmpty();
            QString owner = tr("Same as open");
            if (isOwnerPasswordSeparate())
            {
                owner = m_ownerCard && !m_ownerCard->isEmpty() ? tr("Separate") : notEntered;
            }
            const QString permissions = facts.forbidden.empty() ? Tool::getAllAllowedText() : tr("%1 · kept").arg(PDFAppToolProtectionFacts::getPermissionsText(facts.forbidden));
            Helper::setRows(m_summaryRows, { { tr("Algorithm"), algorithmTitle }, { tr("Encrypted"), getScopeTitle() }, { tr("Open password"), isOpen ? required : notEntered },
                                             { tr("Owner password"), owner }, { tr("Permissions"), permissions } });
            break;
        }

        case Mode::Permissions:
        {
            // 432:2373 Readers: Allowed, Printing, Editing, Copying; 432:2389 Encryption
            const bool isPrintHigh = values.value(Helper::name(Tool::PARAMETER_PRINT_HIGH)).toBool();
            const bool isPrintLow = values.value(Helper::name(Tool::PARAMETER_PRINT_LOW)).toBool();
            const QString printing = isPrintHigh ? tr("High resolution") : (isPrintLow ? tr("Low resolution") : tr("Not allowed"));
            const int editCount = int(values.value(Helper::name(Tool::PARAMETER_MODIFY)).toBool()) + int(values.value(Helper::name(Tool::PARAMETER_ASSEMBLE)).toBool()) +
                                  int(values.value(Helper::name(Tool::PARAMETER_ANNOTATE)).toBool()) + int(values.value(Helper::name(Tool::PARAMETER_FILL_FORMS)).toBool());
            QString editing = tr("Partly allowed");
            if (editCount == 4)
            {
                editing = tr("Allowed");
            }
            else if (editCount == 0)
            {
                editing = tr("Not allowed");
            }
            else if (editCount == 1 && values.value(Helper::name(Tool::PARAMETER_FILL_FORMS)).toBool())
            {
                editing = tr("Forms only");
            }
            const QString copying = values.value(Helper::name(Tool::PARAMETER_COPY)).toBool() ? tr("Allowed") : tr("Not allowed");
            Helper::setRows(m_readersRows, { { tr("Allowed"), tr("%1 of %2 actions").arg(Helper::getAllowedCount(values)).arg(PDFAppToolPermissionList::PERMISSION_COUNT) },
                                             { tr("Printing"), printing }, { tr("Editing"), editing }, { tr("Copying"), copying } });

            const bool isOpenRequired = values.value(Helper::name(Tool::PARAMETER_REQUIRE_OPEN_PASSWORD)).toBool();
            const QString open = !isOpenRequired ? Tool::getOpenPasswordText(false, false) :(m_openCard && !m_openCard->isEmpty() ? required : notEntered);
            const QString owner = m_ownerCard && !m_ownerCard->isEmpty() ? tr("Required") : notEntered;
            Helper::setRows(m_summaryRows, { { tr("Algorithm"), algorithmTitle }, { tr("Encrypted"), getScopeTitle() }, { tr("Open password"), open }, { tr("Owner password"), owner } });
            break;
        }

        case Mode::Certificate:
        {
            // 433:3049: Method, Strength, Encrypted, Opens for, Permissions
            const std::optional<PDFAppToolCertificateItem> chosen = m_certificatePart ? m_certificatePart->getChosenCertificate() : std::nullopt;
            const int allowed = Helper::getAllowedCount(values);
            const QString permissions = allowed == PDFAppToolPermissionList::PERMISSION_COUNT ? Tool::getAllAllowedText() :tr("%1 of %2 actions").arg(allowed).arg(PDFAppToolPermissionList::PERMISSION_COUNT);
            // 433:3282: no certificate is "Nobody selected"; a file, whose details are not known before
            // its password (Q5.11), has no name yet
            QString opensFor = tr("Nobody selected");
            if (chosen)
            {
                opensFor = !chosen->name.isEmpty() ? chosen->name : QStringLiteral("–");
            }
            Helper::setRows(m_summaryRows, { { tr("Method"), tr("Certificate") }, { tr("Strength"), tr("AES 256-bit") }, { tr("Encrypted"), getScopeTitle() },
                                             { tr("Opens for"), opensFor }, { tr("Permissions"), permissions } });
            break;
        }

        case Mode::Unlock:
            Helper::setRows(m_summaryRows, { { tr("Encryption"), PDFDocumentToolEncryption::getNoEncryptionText() }, { tr("Open password"), Tool::getOpenPasswordText(false, false) },
                                             { tr("Permissions"), Tool::getAllAllowedText() } });
            break;
    }

    // Algorithm strength (430:1277): AES-256 Very strong, AES-128 Strong, RC4 Weak (430:1999); the
    // key of a certificate (R5-20)
    if (m_algorithmMeter)
    {
        PDFStrengthMeter::Level level = PDFStrengthMeter::Level::VeryStrong;
        QString text = tr("Recommended. Opens in all current PDF readers.");
        switch (getAlgorithm())
        {
            case PDFSecurityHandlerFactory::AES_128:
                level = PDFStrengthMeter::Level::Strong;
                text = tr("For readers older than 2010. PDF 2.0 marks it as deprecated.");
                break;
            case PDFSecurityHandlerFactory::RC4:
                level = PDFStrengthMeter::Level::Weak;
                text = tr("Only for backward compatibility with very old readers.");
                break;
            case PDFSecurityHandlerFactory::Certificate:
            {
                const std::optional<PDFAppToolCertificateItem> chosen = m_certificatePart ? m_certificatePart->getChosenCertificate() : std::nullopt;
                const int keyBits = chosen ? chosen->keyBits : 0;
                level = keyBits > 0 ? static_cast<PDFStrengthMeter::Level>(PDFAppToolPasswordCard::toMeterLevel(PDFPasswordStrength::getKeyLevel(keyBits))) : PDFStrengthMeter::Level::Empty;
                text = keyBits > 0 ? tr("Follows the key of the certificate (RSA %1-bit). No password travels with the document.").arg(keyBits)
                                   : tr("Follows the key of the chosen certificate. No password travels with the document.");

                // Not measured yet: the meter of a key says so, not "Password strength" (no frame;
                // 433:3282 draws the level of a chosen certificate)
                m_algorithmMeter->setTitle(chosen ? tr("Key strength") : tr("No certificate chosen"));
                break;
            }
            default:
                break;
        }
        m_algorithmMeter->setLevel(level);
        if (m_algorithmText)
        {
            m_algorithmText->setText(text);
        }
    }
}

void PDFAppToolGUIEncryption::updateCallouts()
{
    using Helper = PDFAppToolGUIEncryptionHelper;

    const PDFAppToolProtectionFacts facts = getFacts();
    const bool isEncrypted = facts.mode != EncryptionMode::None;
    const QString algorithmTitle = PDFAppToolPasswordCard::getAlgorithmTitle(getAlgorithm());

    // 430:2481 (owner) / 430:2709 (reader): the current protection is replaced
    if (m_replacedCallout)
    {
        const QString kept = facts.forbidden.empty() || getMode() != Mode::Password ? QString()
                                                                                      : tr(" The current restrictions (%1) are kept.").arg(PDFAppToolProtectionFacts::getPermissionsText(facts.forbidden).toLower());
        const QString text = facts.isOwner ? tr("The new password and algorithm replace the %1 protection.%2").arg(facts.algorithmTitle, kept)
                                           : tr("The new password and settings take the place of the existing %1 protection.").arg(facts.algorithmTitle);
        m_replacedCallout->setTexts(tr("The current protection is replaced"), text);
        m_replacedCallout->setVisible(isEncrypted);
    }

    // 433:5135 / 433:5811
    if (m_infoCallout)
    {
        if (facts.mode == EncryptionMode::PublicKey)
        {
            m_infoCallout->setTexts(tr("PDF4QT cannot open it without the private key"), tr("Protection can be removed only by someone who has the private key."));
        }
        else
        {
            m_infoCallout->setTexts(tr("PDF4QT does not break passwords"), tr("Protection can be removed only by someone who has owner access to the document."));
        }
        m_infoCallout->setVisible(isEncrypted);
    }

    // 433:5187
    if (m_removeCallout)
    {
        m_removeCallout->setTexts(tr("Anyone will be able to open, copy and edit the document"),
                                  tr("Remove the protection only when the content no longer needs it. The protected original stays untouched if you save the result as a copy."));
    }

    // 430:1776: a weak open password (amber; the run is still allowed)
    if (m_weakCallout)
    {
        const bool isWeak = m_openCard && !m_openCard->isEmpty() && !m_openCard->hasInvalidCharacters() &&
                            (m_openCard->getStrength().level == PDFPasswordStrength::Level::VeryWeak || m_openCard->getStrength().level == PDFPasswordStrength::Level::Weak);
        const bool isVeryWeak = isWeak && m_openCard->getStrength().level == PDFPasswordStrength::Level::VeryWeak;
        m_weakCallout->setTexts(tr("This password is easy to guess"),
                                isVeryWeak ? tr("A very weak password gives little protection even with %1. You can still continue.").arg(algorithmTitle)
                                           : tr("A weak password gives little protection even with %1. You can still continue.").arg(algorithmTitle));
        m_weakCallout->setVisible(isWeak);
    }

    // 430:1999: RC4 (red)
    if (m_legacyCallout)
    {
        m_legacyCallout->setTexts(tr("RC4 128-bit is weak and can be broken"), tr("Choose it only when the recipient uses a very old PDF reader. AES 256-bit is strongly recommended."));
        m_legacyCallout->setVisible(getAlgorithm() == PDFSecurityHandlerFactory::RC4);
    }

    // 432:4069: nothing is restricted
    if (m_nothingRestrictedCallout)
    {
        const PDFAppToolParameterModel* model = getModel();
        const bool isNothing = model && Helper::getAllowedCount(model->getValues()) == PDFAppToolPermissionList::PERMISSION_COUNT;
        m_nothingRestrictedCallout->setTexts(tr("Nothing is restricted"), tr("All eight actions are allowed, so the owner password protects only the security settings themselves."));
        m_nothingRestrictedCallout->setVisible(isNothing);
    }
}

void PDFAppToolGUIEncryption::notifyStateChanged()
{
    // The Primary button and the status bar read the parts; both emit (the last calls)
    updateConfirmState();
    if (!PDFDeferredDeletion::isRetired(this))
    {
        updateStatusText();
    }
}

// ------------------------------------------------------------------ the shell

QString PDFAppToolGUIEncryption::getConfirmText() const
{
    // 430:1093, 432:2130, 433:3049, 433:5047 (K5.3: the result, not a uniform Apply)
    switch (getMode())
    {
        case Mode::Password:
            return tr("Protect Document");
        case Mode::Permissions:
            return tr("Apply Restrictions");
        case Mode::Certificate:
            return tr("Encrypt Document");
        case Mode::Unlock:
            return tr("Remove Protection");
    }
    return BaseClass::getConfirmText();
}

QString PDFAppToolGUIEncryption::getConfirmIconName() const
{
    switch (getMode())
    {
        case Mode::Password:
            return QStringLiteral("lock");
        case Mode::Permissions:
            return QStringLiteral("shield-lock");
        case Mode::Certificate:
            return QStringLiteral("certificate-lock");
        case Mode::Unlock:
            return QStringLiteral("unlock");
    }
    return BaseClass::getConfirmIconName();
}

bool PDFAppToolGUIEncryption::isConfirmEnabled() const
{
    using Tool = PDFDocumentToolEncryption;
    if (!BaseClass::isConfirmEnabled())
    {
        return false;
    }

    switch (getMode())
    {
        case Mode::Password:
            // 430:1287 empty, 430:1999 characters, 447:15766 do not match; 430:1776 weak is allowed
            return m_openCard && m_openCard->isComplete() && (!isOwnerPasswordSeparate() || (m_ownerCard && m_ownerCard->isComplete())) && getCoreErrors().empty();

        case Mode::Permissions:
        {
            // 432:2408 no owner password, 432:3721 the same passwords
            const PDFAppToolParameterModel* model = getModel();
            const bool isOpenRequired = model && model->getValue(QString::fromLatin1(Tool::PARAMETER_REQUIRE_OPEN_PASSWORD)).toBool();
            return m_ownerCard && m_ownerCard->isComplete() && (!isOpenRequired || (m_openCard && m_openCard->isComplete())) && getCoreErrors().empty();
        }

        case Mode::Certificate:
            return m_certificatePart && m_certificatePart->isConfirmEnabled();

        case Mode::Unlock:
            // 433:5671: nothing to remove
            return getFacts().mode != EncryptionMode::None;
    }
    return false;
}

QString PDFAppToolGUIEncryption::getStatusText() const
{
    using Tool = PDFDocumentToolEncryption;

    const PDFDocumentToolShell* shell = getShell();
    const PDFAppToolProtectionFacts facts = getFacts();
    const QString algorithmTitle = PDFAppToolPasswordCard::getAlgorithmTitle(getAlgorithm());
    if (shell && shell->isPermissionRefused())
    {
        // 430:2709, 433:5210 (the owner password dialog is open)
        if (facts.mode == EncryptionMode::PublicKey)
        {
            return getMode() == Mode::Unlock ? tr("The certificate does not allow removing the protection of this document")
                                             : tr("The certificate does not allow changing the security of this document");
        }
        return getMode() == Mode::Unlock ? tr("Owner access is needed to remove the protection") : tr("Owner access is needed to change the security of this document");
    }

    switch (getMode())
    {
        case Mode::Password:
        {
            if (!m_openCard || m_openCard->isEmpty())
            {
                return tr("Enter a password to protect the document");
            }
            if (m_openCard->hasInvalidCharacters() || (m_ownerCard && isOwnerPasswordSeparate() && m_ownerCard->hasInvalidCharacters()))
            {
                return tr("The password contains characters that %1 cannot use").arg(algorithmTitle);
            }
            if (m_openCard->isMismatch() || (m_ownerCard && isOwnerPasswordSeparate() && m_ownerCard->isMismatch()))
            {
                return tr("The passwords do not match");
            }
            if (isOwnerPasswordSeparate() && (!m_ownerCard || m_ownerCard->isEmpty()))
            {
                return isOwnerPasswordRequired() ? tr("Enter an owner password – the document keeps its restrictions") : tr("Enter an owner password");
            }
            if (isSamePasswordError())
            {
                return tr("The owner password must differ from the open password");
            }
            if (!isConfirmEnabled())
            {
                const std::vector<PDFDocumentToolParameterError> errors = getCoreErrors();
                return errors.empty() ? tr("Repeat the password to protect the document") : errors.front().message;
            }
            if (m_openCard->getStrength().level == PDFPasswordStrength::Level::VeryWeak)
            {
                return tr("The password is very weak – the document can still be protected with it");
            }
            return tr("The document will be encrypted with %1 · nothing is changed until you confirm").arg(algorithmTitle);
        }

        case Mode::Permissions:
        {
            const PDFAppToolParameterModel* model = getModel();
            const bool isOpenRequired = model && model->getValue(QString::fromLatin1(Tool::PARAMETER_REQUIRE_OPEN_PASSWORD)).toBool();
            if (!m_ownerCard || m_ownerCard->isEmpty())
            {
                return tr("Enter an owner password to apply the restrictions");
            }
            if (m_ownerCard->hasInvalidCharacters() || (isOpenRequired && m_openCard && m_openCard->hasInvalidCharacters()))
            {
                return tr("The password contains characters that %1 cannot use").arg(algorithmTitle);
            }
            if (m_ownerCard->isMismatch() || (isOpenRequired && m_openCard && m_openCard->isMismatch()))
            {
                return tr("The passwords do not match");
            }
            if (isSamePasswordError())
            {
                return tr("The owner password must differ from the open password");
            }
            if (isOpenRequired && (!m_openCard || m_openCard->isEmpty()))
            {
                return tr("Enter the open password");
            }
            if (!isConfirmEnabled())
            {
                const std::vector<PDFDocumentToolParameterError> errors = getCoreErrors();
                return errors.empty() ? tr("Repeat the password to apply the restrictions") : errors.front().message;
            }
            const int allowed = model ? PDFAppToolGUIEncryptionHelper::getAllowedCount(model->getValues()) : 0;
            return tr("%1 of %2 actions stay allowed · the document will be encrypted with %3").arg(allowed).arg(PDFAppToolPermissionList::PERMISSION_COUNT).arg(algorithmTitle);
        }

        case Mode::Certificate:
            return m_certificatePart ? m_certificatePart->getStatusText() : BaseClass::getStatusText();

        case Mode::Unlock:
            if (facts.mode == EncryptionMode::None)
            {
                return tr("This document is not encrypted");
            }
            return tr("Encryption, password and restrictions will be removed · nothing is changed until you confirm");
    }
    return BaseClass::getStatusText();
}

QString PDFAppToolGUIEncryption::getPermissionTitle() const
{
    // The refusal of a certificate without bit 2 (K5.12, Q5.3 b); a password document asks for the
    // owner password instead (430:2709)
    return getMode() == Mode::Unlock ? tr("The protection cannot be removed") : tr("The security cannot be changed");
}

QString PDFAppToolGUIEncryption::getPermissionSubtitle() const
{
    return tr("Only the owner of the document can change its security.");
}

QVariantMap PDFAppToolGUIEncryption::getRunParameters() const
{
    using Tool = PDFDocumentToolEncryption;

    // S5.2: the passwords never enter the model; they go to the values of the run only (the empty
    // values of the model of a Secret are dropped: a password, which is not needed, is no value)
    QVariantMap values = BaseClass::getRunParameters();
    for (const char* name : { Tool::PARAMETER_OPEN_PASSWORD, Tool::PARAMETER_OWNER_PASSWORD, Tool::PARAMETER_CERTIFICATE_PASSWORD })
    {
        values.remove(QString::fromLatin1(name));
    }
    switch (getMode())
    {
        case Mode::Password:
        {
            const bool isSeparate = isOwnerPasswordSeparate();
            values.insert(QString::fromLatin1(Tool::PARAMETER_OPEN_PASSWORD), m_openCard ? m_openCard->getPassword() : QString());
            values.insert(QString::fromLatin1(Tool::PARAMETER_OWNER_SAME_AS_OPEN), !isSeparate);
            if (isSeparate)
            {
                values.insert(QString::fromLatin1(Tool::PARAMETER_OWNER_PASSWORD), m_ownerCard ? m_ownerCard->getPassword() : QString());
            }
            break;
        }

        case Mode::Permissions:
        {
            values.insert(QString::fromLatin1(Tool::PARAMETER_OWNER_PASSWORD), m_ownerCard ? m_ownerCard->getPassword() : QString());
            if (values.value(QString::fromLatin1(Tool::PARAMETER_REQUIRE_OPEN_PASSWORD)).toBool())
            {
                values.insert(QString::fromLatin1(Tool::PARAMETER_OPEN_PASSWORD), m_openCard ? m_openCard->getPassword() : QString());
            }
            break;
        }

        case Mode::Certificate:
            // The fingerprint and the password of the .pfx file of prepareEnvironment (P3's part)
            for (auto it = m_certificateRunValues.cbegin(); it != m_certificateRunValues.cend(); ++it)
            {
                values.insert(it.key(), it.value());
            }
            break;

        case Mode::Unlock:
            break;
    }
    return values;
}

void PDFAppToolGUIEncryption::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    BaseClass::prepareEnvironment(environment);
    if (getMode() != Mode::Certificate || !m_certificatePart || !environment)
    {
        return;
    }

    // The chosen certificate with its PKCS#12 for the run only (PDFAppToolRunEnvironment::
    // userCertificates); a wrong password is shown by the part (433:3972) and the run is refused
    // by the validation of the tool (no certificate)
    dropCertificateRunValues();
    QString errorMessage;
    std::vector<PDFCertificateEntry> certificates;
    if (m_certificatePart->prepareRun(&m_certificateRunValues, &certificates, &errorMessage))
    {
        environment->userCertificates = std::move(certificates);
    }
}

void PDFAppToolGUIEncryption::fillResultInput(PDFAppToolResultInput* input) const
{
    // 430:3226, 432:4388, 433:4721, 433:5999
    switch (getMode())
    {
        case Mode::Password:
            input->title = tr("Document protected");
            break;
        case Mode::Permissions:
            input->title = tr("Permissions restricted");
            break;
        case Mode::Certificate:
            input->title = tr("Document encrypted with a certificate");
            break;
        case Mode::Unlock:
            input->title = tr("Protection removed");
            break;
    }
}

void PDFAppToolGUIEncryption::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);
    updateParts();
}

void PDFAppToolGUIEncryption::onRunStateChanged(bool isRunning)
{
    // S5.1: the passwords are emptied, when the run has taken its values
    if (isRunning)
    {
        clearSecrets();
        updateParts();
    }
}

void PDFAppToolGUIEncryption::onDocumentChanged()
{
    // Unlocked by the owner password (430:2709), another version: the facts again
    m_facts.reset();
    updateParts();
    notifyStateChanged();
}

void PDFAppToolGUIEncryption::retranslate()
{
    // The panels are made again by the shell (their cards with them - the typed passwords go, S5.1)
    m_facts.reset();
    buildForm();
}

// ------------------------------------------------------------------- the slots

void PDFAppToolGUIEncryption::onPasswordChanged()
{
    updateParts();
    notifyStateChanged();
}

void PDFAppToolGUIEncryption::onOwnerSameToggled(bool isChecked)
{
    if (PDFAppToolParameterModel* model = getModel())
    {
        // One undo step (K5.18); onParametersChanged updates the parts
        model->setValue(QString::fromLatin1(PDFDocumentToolEncryption::PARAMETER_OWNER_SAME_AS_OPEN), isChecked);
    }
}

void PDFAppToolGUIEncryption::onRequireOpenToggled(bool isChecked)
{
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValue(QString::fromLatin1(PDFDocumentToolEncryption::PARAMETER_REQUIRE_OPEN_PASSWORD), isChecked);
    }
}

void PDFAppToolGUIEncryption::onPermissionsEdited(const QVariantMap& values)
{
    if (PDFAppToolParameterModel* model = getModel())
    {
        // The values with the dependencies of K5.8 as one undo step
        model->setValues(values);
    }
}

void PDFAppToolGUIEncryption::onPresetRowClicked()
{
    PDFTileRow* row = qobject_cast<PDFTileRow*>(sender());
    if (!row)
    {
        return;
    }

    const Preset preset = static_cast<Preset>(row->property(PDFAppToolGUIEncryptionHelper::PRESET_PROPERTY).toInt());

    // The panel closes first: the change of the values may start anything
    if (m_presetPanel)
    {
        m_presetPanel->close();
    }
    if (!PDFDeferredDeletion::isRetired(this) && getModel())
    {
        getModel()->setValues(PDFDocumentToolEncryption::getPresetValues(preset));
    }
}

void PDFAppToolGUIEncryption::onPermissionToggleClicked()
{
    m_isPermissionsExpanded = !m_isPermissionsExpanded;
    updatePermissionCard();
}

void PDFAppToolGUIEncryption::onCertificateStateChanged()
{
    updateParts();
    notifyStateChanged();
}

}   // namespace pdf

#include "moc_pdfapptoolgui_encryption.cpp"
