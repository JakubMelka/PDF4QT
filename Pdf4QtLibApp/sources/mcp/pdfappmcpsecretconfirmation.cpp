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

#include "pdfappmcpsecretconfirmation.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappmcpchange.h"
#include "pdfappmcpdocumentreader.h"
#include "pdfappcertificatedialogs.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfapptoolgui_encryption.h"
#include "pdfapptoolpanelparts.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolrunner.h"

#include "pdfdocumenttool_signature.h"
#include "pdffluentwidgets.h"
#include "pdfsecurityhandler.h"
#include "pdfthemefonts.h"
#include "pdftimestampclient.h"

#include <QDateTime>
#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>
#include <utility>

namespace pdf
{

int PDFAppMcpSecretConfirmation::s_timeout = PDFAppMcpSecretConfirmation::TIMEOUT;

/// Helpers of the confirmation with a password
class PDFAppMcpSecretConfirmationHelper
{
public:
    PDFAppMcpSecretConfirmationHelper() = delete;

    using Tool = PDFDocumentToolEncryption;

    /// The parameters of the tool normalized (the defaults of the omitted ones); the errors are
    /// those of the run, which reports them
    static QVariantMap normalize(const PDFDocumentToolParameterDefinitions& definitions, const QVariantMap& parameters)
    {
        QVariantMap normalized;
        PDFDocumentToolParameters::normalize(definitions, parameters, &normalized);
        return normalized;
    }

    static QString getValueTitle(const PDFDocumentToolParameterDefinitions& definitions, const QVariantMap& values, const char* name)
    {
        const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(definitions, QString::fromLatin1(name));
        if (!definition)
        {
            return QString();
        }

        const QString id = values.value(QString::fromLatin1(name), definition->getDefaultValue()).toString();
        for (const PDFDocumentToolParameterOption& option : definition->getOptions())
        {
            if (option.id == id)
            {
                return option.getTitle();
            }
        }
        return id;
    }

    static bool getBool(const QVariantMap& values, const char* name)
    {
        return values.value(QString::fromLatin1(name)).toBool();
    }

    /// The algorithm of the factory of an id of the parameter algorithm
    static PDFSecurityHandlerFactory::Algorithm getAlgorithm(const QString& id)
    {
        if (id == QLatin1String(Tool::ALGORITHM_AES_128))
        {
            return PDFSecurityHandlerFactory::AES_128;
        }
        if (id == QLatin1String(Tool::ALGORITHM_RC4))
        {
            return PDFSecurityHandlerFactory::RC4;
        }
        return PDFSecurityHandlerFactory::AES_256;
    }

    /// The facts of the asked protection (key, value) in the order of the -done frames
    static std::vector<std::pair<QString, QString>> createFacts(Tool::Mode mode, const QVariantMap& parameters)
    {
        const Tool tool(mode);
        const PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
        const QVariantMap values = normalize(definitions, parameters);

        std::vector<std::pair<QString, QString>> facts;
        const QString scope = getValueTitle(definitions, values, Tool::PARAMETER_SCOPE);
        if (mode == Tool::Mode::Certificate)
        {
            facts.emplace_back(PDFAppMcpSecretConfirmation::tr("Encryption"), PDFAppMcpSecretConfirmation::tr("Certificate, AES 256-bit · %1").arg(scope));
            facts.emplace_back(PDFAppMcpSecretConfirmation::tr("Opens for"), PDFAppMcpSecretConfirmation::tr("The holder of the certificate you choose"));
        }
        else
        {
            facts.emplace_back(PDFAppMcpSecretConfirmation::tr("Encryption"), QStringLiteral("%1 · %2").arg(getValueTitle(definitions, values, Tool::PARAMETER_ALGORITHM), scope));
            // File attachments only: the result opens without a password (review RS5SEC L5)
            const bool isOpenPasswordRequired = mode == Tool::Mode::Password || getBool(values, Tool::PARAMETER_REQUIRE_OPEN_PASSWORD);
            const bool isAttachmentsOnly = values.value(QString::fromLatin1(Tool::PARAMETER_SCOPE)).toString() == QLatin1String(Tool::SCOPE_ATTACHMENTS);
            facts.emplace_back(PDFAppMcpSecretConfirmation::tr("Open password"), Tool::getOpenPasswordText(isOpenPasswordRequired, isAttachmentsOnly));
            if (mode == Tool::Mode::Password)
            {
                facts.emplace_back(PDFAppMcpSecretConfirmation::tr("Owner password"), getBool(values, Tool::PARAMETER_OWNER_SAME_AS_OPEN) ? PDFAppMcpSecretConfirmation::tr("Same as open")
                                                                                                                                    : PDFAppMcpSecretConfirmation::tr("Separate"));
            }
        }

        if (mode == Tool::Mode::Permissions || mode == Tool::Mode::Certificate)
        {
            QStringList allowed;
            const std::vector<std::pair<const char*, PDFSecurityHandler::Permission>> permissions = Tool::getPermissionParameters();
            for (const auto& permission : permissions)
            {
                const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(definitions, QString::fromLatin1(permission.first));
                if (definition && getBool(values, permission.first))
                {
                    allowed << definition->getTitle();
                }
            }

            QString text = allowed.join(QStringLiteral(", "));
            if (allowed.size() == int(permissions.size()))
            {
                text = PDFAppMcpSecretConfirmation::tr("Everything");
            }
            else if (allowed.isEmpty())
            {
                text = PDFAppMcpSecretConfirmation::tr("Only viewing");
            }
            facts.emplace_back(PDFAppMcpSecretConfirmation::tr("Allowed"), text);
        }
        return facts;
    }

    /// The facts of the asked signature of Sign with Certificate (plan of step 6 of phase 2, 6.5: the
    /// confirmation shows the reason and the contact of the assistant; the address of the authority of a
    /// timestamp, to which a hash of the document goes - S6.6). At most 5 facts: the body of a save has
    /// room for 8 rows with its own 3 (PDFAppMcpConfirmDialog)
    static std::vector<std::pair<QString, QString>> createSigningFacts(const QVariantMap& parameters)
    {
        using Signature = PDFDocumentToolSignature;

        const Signature tool(Signature::Mode::Sign);
        const PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
        const QVariantMap values = normalize(definitions, parameters);
        const bool isTimestamped = values.value(QString::fromLatin1(Signature::PARAMETER_TYPE)).toString() == QLatin1String(Signature::TYPE_SIGNATURE_TIMESTAMP);

        // MCP signs only invisibly (Q6.7): the appearance is not a value of the assistant
        std::vector<std::pair<QString, QString>> facts;
        facts.emplace_back(PDFAppMcpSecretConfirmation::tr("Signature"), PDFAppMcpSecretConfirmation::tr("%1 · %2 · invisible")
                                                                            .arg(getValueTitle(definitions, values, Signature::PARAMETER_TYPE), getValueTitle(definitions, values, Signature::PARAMETER_FORMAT)));
        if (isTimestamped)
        {
            const QUrl url(values.value(QString::fromLatin1(Signature::PARAMETER_TSA_URL)).toString().trimmed());
            facts.emplace_back(PDFAppMcpSecretConfirmation::tr("Timestamp authority"), PDFTimestampClient::getDisplayUrl(url));
        }
        for (const auto& [name, title] : { std::pair<const char*, QString>(Signature::PARAMETER_FIELD, PDFAppMcpSecretConfirmation::tr("Field")),
                                           std::pair<const char*, QString>(Signature::PARAMETER_REASON, PDFAppMcpSecretConfirmation::tr("Reason")),
                                           std::pair<const char*, QString>(Signature::PARAMETER_CONTACT_INFO, PDFAppMcpSecretConfirmation::tr("Contact")) })
        {
            const QString value = values.value(QString::fromLatin1(name)).toString();
            if (!value.isEmpty())
            {
                facts.emplace_back(title, value);
            }
        }
        return facts;
    }

    /// The encryption of the document now: the method of document.get_info (the one description,
    /// which the assistant also reads); empty = not encrypted
    static QString getCurrentMethod(const PDFAppDocument* document)
    {
        if (!document || !document->isReady())
        {
            return QString();
        }
        const QJsonObject info = QJsonDocument::fromJson(PDFAppMcpDocumentReader::getInfo(document)).object();
        return info.value(QStringLiteral("encryption")).toObject().value(QStringLiteral("method")).toString();
    }

    static void clearCertificates(std::vector<PDFCertificateEntry>& certificates)
    {
        for (PDFCertificateEntry& entry : certificates)
        {
            PDFSecureMemory::clear(entry.pkcs12);
        }
        certificates.clear();
    }
};

PDFAppMcpSecretConfirmation::PDFAppMcpSecretConfirmation(PDFAppContext* context, const QString& toolId, const QVariantMap& parameters, QWidget* parent) :
    PDFAppMcpSecretConfirmation(context, nullptr, toolId, parameters, parent)
{

}

PDFAppMcpSecretConfirmation::PDFAppMcpSecretConfirmation(PDFAppContext* context, std::shared_ptr<PDFAppMcpConfirmation> confirmation, const QString& toolId,
                                                         const QVariantMap& parameters, QWidget* parent) :
    BaseClass(prepareConfirmation(std::move(confirmation), toolId, parameters), nullptr, parent),
    m_context(context),
    m_toolId(toolId),
    m_parameters(parameters),
    m_mode(PDFDocumentToolEncryption::findMode(toolId)),
    m_isSigning(PDFDocumentToolSignature::findMode(toolId) == PDFDocumentToolSignature::Mode::Sign)
{
    setObjectName(QStringLiteral("mcpSecretConfirmation"));
    setAttribute(Qt::WA_DeleteOnClose);

    // The timers are members, made children: retire (step A) stops them with the dialog
    m_timer.setParent(this);
    m_tickTimer.setParent(this);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &PDFAppMcpSecretConfirmation::onTimeout);
    m_tickTimer.setInterval(1000);
    connect(&m_tickTimer, &QTimer::timeout, this, &PDFAppMcpSecretConfirmation::onTick);
    connect(this, &PDFAppMcpConfirmDialog::allowed, this, &PDFAppMcpSecretConfirmation::onAllowed);
    connect(this, &PDFAppMcpConfirmDialog::denied, this, &PDFAppMcpSecretConfirmation::onDenied);

    // The bound of the wait before the dialog is shown (a hidden tab); the time of the user
    // starts with the first show
    m_timer.start(qMax(QUEUE_TIMEOUT, getTimeout()));
    setRemainingTime(getTimeout());

    m_currentMethod = PDFAppMcpSecretConfirmationHelper::getCurrentMethod(getConfirmation()->document.data());
    build();
    updateTexts();
    updateConfirmState();
}

PDFAppMcpSecretConfirmation::~PDFAppMcpSecretConfirmation()
{
    // S5.1: nothing of the passwords outlives the dialog, when nobody took them
    clearSecretValues(m_runValues);
    PDFAppMcpSecretConfirmationHelper::clearCertificates(m_certificates);
}

std::shared_ptr<PDFAppMcpConfirmation> PDFAppMcpSecretConfirmation::prepareConfirmation(std::shared_ptr<PDFAppMcpConfirmation> confirmation, const QString& toolId, const QVariantMap& parameters)
{
    if (!confirmation)
    {
        confirmation = std::make_shared<PDFAppMcpConfirmation>();
        confirmation->kind = PDFAppMcpConfirmationKind::Change;
    }

    const std::optional<PDFDocumentToolEncryption::Mode> mode = PDFDocumentToolEncryption::findMode(toolId);
    const bool isSigning = PDFDocumentToolSignature::findMode(toolId) == PDFDocumentToolSignature::Mode::Sign;
    if (!mode && !isSigning)
    {
        return confirmation;
    }

    const std::vector<std::pair<QString, QString>> facts = isSigning ? PDFAppMcpSecretConfirmationHelper::createSigningFacts(parameters)
                                                                     : PDFAppMcpSecretConfirmationHelper::createFacts(*mode, parameters);
    if (confirmation->kind == PDFAppMcpConfirmationKind::Change)
    {
        // keep (940:133693): one change row of the tool with the asked protection as its detail
        QStringList details;
        for (const auto& fact : facts)
        {
            details << QStringLiteral("%1: %2").arg(fact.first, fact.second);
        }

        const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(toolId);
        PDFAppMcpChangeRow row;
        row.iconName = entry ? entry->getIconName() : QStringLiteral("lock");
        row.text = entry ? entry->getTitle() : toolId;
        row.detail = details.join(QStringLiteral(" · "));
        QJsonArray rows = confirmation->details.value(QStringLiteral("rows")).toArray();
        rows.prepend(row.toJson());
        confirmation->details.insert(QStringLiteral("rows"), rows);
    }
    else
    {
        // save_copy (958:27539), overwrite (940:134248): the facts of the protection first, then the
        // facts of the caller (the signatures)
        QJsonArray rows;
        for (const auto& fact : facts)
        {
            rows.append(QJsonObject({ { QStringLiteral("key"), fact.first }, { QStringLiteral("value"), fact.second } }));
        }
        for (const QJsonValue& value : confirmation->details.value(QStringLiteral("facts")).toArray())
        {
            rows.append(value);
        }
        confirmation->details.insert(QStringLiteral("facts"), rows);
    }
    return confirmation;
}

PDFAppMcpSecretConfirmation* PDFAppMcpSecretConfirmation::showInTab(PDFAppContext* context, std::shared_ptr<PDFAppMcpConfirmation> confirmation, const QString& toolId,
                                                                    const QVariantMap& parameters, QWidget* tab)
{
    if (!tab)
    {
        return nullptr;
    }

    // The scrim owns the dialog and goes with it (finish); the tab closed meanwhile deletes both
    PDFAppMcpConfirmScrim* scrim = new PDFAppMcpConfirmScrim(tab);
    PDFAppMcpSecretConfirmation* dialog = new PDFAppMcpSecretConfirmation(context, std::move(confirmation), toolId, parameters, scrim);
    scrim->setDialog(dialog);
    scrim->raise();
    scrim->show();

    // The first card of the dialog: the owner password comes first in Restrict Permissions (build)
    const bool isOwnerFirst = dialog->m_mode == PDFDocumentToolEncryption::Mode::Permissions || !dialog->m_openCard;
    PDFAppToolPasswordCard* card = dialog->m_certificateCard ? dialog->m_certificateCard.data() : (isOwnerFirst ? dialog->m_ownerCard.data() : dialog->m_openCard.data());
    if (dialog->m_certificateList && !dialog->isCertificatePasswordNeeded())
    {
        // A certificate of the system store has no password here
        card = nullptr;
    }
    QWidget* field = card ? card->getPasswordField() : nullptr;
    if (field && tab->isVisible())
    {
        field->setFocus(Qt::OtherFocusReason);
    }
    else if (tab->isVisible())
    {
        dialog->setFocus(Qt::OtherFocusReason);
    }
    return dialog;
}

PDFAppToolPasswordCard* PDFAppMcpSecretConfirmation::createCard(PDFAppToolPasswordCard::Kind kind)
{
    PDFAppToolPasswordCard* card = new PDFAppToolPasswordCard(kind, m_cardsContainer);

    updateCardLabels(card);
    if (m_isSigning || m_mode == PDFDocumentToolEncryption::Mode::Certificate)
    {
        card->setAlgorithm(PDFSecurityHandlerFactory::Certificate);
    }
    else
    {
        const QVariantMap values = PDFAppMcpSecretConfirmationHelper::normalize(PDFDocumentToolEncryption(*m_mode).getParameters(), m_parameters);
        card->setAlgorithm(PDFAppMcpSecretConfirmationHelper::getAlgorithm(values.value(QString::fromLatin1(PDFDocumentToolEncryption::PARAMETER_ALGORITHM)).toString()));
    }
    connect(card, &PDFAppToolPasswordCard::passwordChanged, this, &PDFAppMcpSecretConfirmation::onCardChanged);
    if (QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(m_cardsContainer->layout()))
    {
        layout->addWidget(card);
    }
    return card;
}

void PDFAppMcpSecretConfirmation::build()
{
    using Tool = PDFDocumentToolEncryption;

    if (m_isSigning)
    {
        buildSigning();
        return;
    }

    if (!m_mode || *m_mode == Tool::Mode::Unlock)
    {
        // Remove Password needs no password: the dialog has nothing to ask
        return;
    }

    const QVariantMap values = PDFAppMcpSecretConfirmationHelper::normalize(Tool(*m_mode).getParameters(), m_parameters);
    const bool isCertificate = *m_mode == Tool::Mode::Certificate;

    // 6.9: a weaker algorithm than the current one (an assistant may ask for RC4 over AES 256-bit)
    const int requested = getAlgorithmStrength(values.value(QString::fromLatin1(Tool::PARAMETER_ALGORITHM)).toString(), isCertificate);
    const int current = getMethodStrength(m_currentMethod);
    if (current > 0 && requested < current)
    {
        m_downgradeCallout = new PDFAppToolCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), QString(), this);
        m_downgradeCallout->setObjectName(QStringLiteral("downgradeCallout"));
        addContent(m_downgradeCallout);
    }

    // R5-21 (owner answer Q5.3 b): the certificate allows the change of the encryption only with every
    // permission (Table 24, bit 2) - restricted, the user cannot remove the protection later
    if (isCertificate)
    {
        bool isRestricted = false;
        for (const auto& permission : Tool::getPermissionParameters())
        {
            isRestricted = isRestricted || !values.value(QString::fromLatin1(permission.first)).toBool();
        }
        if (isRestricted)
        {
            m_lockoutCallout = new PDFAppToolCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), QString(), this);
            m_lockoutCallout->setObjectName(QStringLiteral("lockoutCallout"));
            addContent(m_lockoutCallout);
        }
    }

    // The cards: the open password first in Protect with Password, the owner password first in
    // Restrict Permissions (its left panel 432:2105), the list and the password of the .pfx file
    m_cardsContainer = new QWidget(this);
    m_cardsContainer->setObjectName(QStringLiteral("secretCards"));
    QVBoxLayout* cardsLayout = new QVBoxLayout(m_cardsContainer);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    cardsLayout->setSpacing(12);
    switch (*m_mode)
    {
        case Tool::Mode::Password:
            m_openCard = createCard(PDFAppToolPasswordCard::Kind::Open);
            if (!values.value(QString::fromLatin1(Tool::PARAMETER_OWNER_SAME_AS_OPEN)).toBool())
            {
                m_ownerCard = createCard(PDFAppToolPasswordCard::Kind::Owner);
            }
            break;

        case Tool::Mode::Permissions:
            m_ownerCard = createCard(PDFAppToolPasswordCard::Kind::Owner);
            if (values.value(QString::fromLatin1(Tool::PARAMETER_REQUIRE_OPEN_PASSWORD)).toBool())
            {
                m_openCard = createCard(PDFAppToolPasswordCard::Kind::Open);
            }
            break;

        case Tool::Mode::Certificate:
        {
            m_provider = std::make_unique<PDFAppToolCertificateProvider>(m_context.data());
            m_certificateList = new PDFAppToolCertificateList(m_cardsContainer);
            m_certificateList->setObjectName(QStringLiteral("certificateList"));
            const std::vector<PDFAppToolCertificateItem> certificates = m_provider->getCertificates();
            for (const PDFAppToolCertificateItem& item : certificates)
            {
                // The first row, which can be chosen (the list has the valid ones first)
                if (item.state == PDFAppToolCertificateItem::State::Valid || item.state == PDFAppToolCertificateItem::State::Locked)
                {
                    m_certificateList->setCertificates(certificates);
                    m_certificateList->choose(item.filePath);
                    break;
                }
            }
            if (m_certificateList->getCertificates().empty())
            {
                m_certificateList->setCertificates(certificates);
            }
            connect(m_certificateList, &PDFAppToolCertificateList::certificateChosen, this, &PDFAppMcpSecretConfirmation::onCertificateChosen);
            cardsLayout->addWidget(m_certificateList);
            m_certificateCard = createCard(PDFAppToolPasswordCard::Kind::Certificate);

            // A certificate of the system store is a recipient by its public certificate (the addendum of
            // the owner of 15. 11.): no password, its card is hidden
            updateCertificateCard();
            break;
        }

        case Tool::Mode::Unlock:
            break;
    }
    addContent(m_cardsContainer);
    buildEnd();
}

void PDFAppMcpSecretConfirmation::buildSigning()
{
    // Sign with Certificate (plan of step 6 of phase 2, 6.5, Q6.7): the certificates, which may sign
    // (PDFAppToolCertificateUsage::Signing - the .pfx files and the personal certificates of the system
    // store of Windows, Q6.2), the one of the assistant preselected when the user can choose it, else
    // the first one, which can be chosen; the card of the password of a .pfx file below the list
    m_cardsContainer = new QWidget(this);
    m_cardsContainer->setObjectName(QStringLiteral("secretCards"));
    QVBoxLayout* cardsLayout = new QVBoxLayout(m_cardsContainer);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    cardsLayout->setSpacing(12);

    m_provider = std::make_unique<PDFAppToolCertificateProvider>(m_context.data());
    m_certificateList = new PDFAppToolCertificateList(m_cardsContainer);
    m_certificateList->setObjectName(QStringLiteral("certificateList"));
    m_certificateList->setUsage(PDFAppToolCertificateUsage::Signing);
    m_certificateList->setCertificates(m_provider->getCertificates(PDFAppToolCertificateUsage::Signing));

    const QString preselected = m_parameters.value(QString::fromLatin1(PDFDocumentToolSignature::PARAMETER_CERTIFICATE)).toString().trimmed().toLower();
    QString choice;
    for (const PDFAppToolCertificateItem& item : m_certificateList->getCertificates())
    {
        if (!item.isChoosable())
        {
            continue;
        }
        if (choice.isEmpty())
        {
            choice = item.filePath;
        }
        if (!preselected.isEmpty() && item.fingerprint == preselected)
        {
            choice = item.filePath;
            break;
        }
    }
    if (!choice.isEmpty())
    {
        m_certificateList->choose(choice);
    }
    else
    {
        // No certificate can sign: the user creates or imports one in Manage Certificates first
        m_noCertificateCallout = new PDFAppToolCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), QString(), this);
        m_noCertificateCallout->setObjectName(QStringLiteral("noCertificateCallout"));
        addContent(m_noCertificateCallout);
    }
    connect(m_certificateList, &PDFAppToolCertificateList::certificateChosen, this, &PDFAppMcpSecretConfirmation::onCertificateChosen);
    cardsLayout->addWidget(m_certificateList);
    m_certificateCard = createCard(PDFAppToolPasswordCard::Kind::Certificate);
    addContent(m_cardsContainer);
    updateCertificateCard();
    buildEnd();
}

void PDFAppMcpSecretConfirmation::buildEnd()
{
    // The errors of the validation, which belong to no card
    m_errorCallout = new PDFAppToolCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), QString(), QString(), this);
    m_errorCallout->setObjectName(QStringLiteral("errorCallout"));
    m_errorCallout->hide();
    addContent(m_errorCallout);

    m_secretNote = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this);
    m_secretNote->setObjectName(QStringLiteral("secretNote"));
    m_secretNote->setTextFormat(Qt::PlainText);
    m_secretNote->setWordWrap(true);
    m_secretNote->setLineHeight(16);
    addContent(m_secretNote);

    // The reason row of the confirmation stays the last row above the buttons (958:28030)
    if (QWidget* reason = findChild<QWidget*>(QStringLiteral("reason"), Qt::FindDirectChildrenOnly))
    {
        if (layout())
        {
            layout()->removeWidget(reason);
        }
        addContent(reason);
    }
}

void PDFAppMcpSecretConfirmation::updateCardLabels(PDFAppToolPasswordCard* card)
{
    // The cards are below each other without a heading: their labels say which password it is
    // (the open password typed as the owner one would give every reader owner access); the
    // labels are set again after a change of the language (review RS5APP)
    if (!card)
    {
        return;
    }
    if (card->getKind() == PDFAppToolPasswordCard::Kind::Owner)
    {
        card->setLabels(tr("Owner password"), tr("Repeat owner password"));
    }
    else if (card->getKind() == PDFAppToolPasswordCard::Kind::Open)
    {
        card->setLabels(tr("Open password"), tr("Repeat open password"));
    }
}

void PDFAppMcpSecretConfirmation::updateTexts()
{
    using Tool = PDFDocumentToolEncryption;

    updateCardLabels(m_ownerCard);
    updateCardLabels(m_openCard);

    if (m_downgradeCallout && m_mode)
    {
        const PDFDocumentToolParameterDefinitions definitions = Tool(*m_mode).getParameters();
        const QVariantMap values = PDFAppMcpSecretConfirmationHelper::normalize(definitions, m_parameters);
        const QString requested = *m_mode == Tool::Mode::Certificate ? tr("a certificate") : PDFAppMcpSecretConfirmationHelper::getValueTitle(definitions, values, Tool::PARAMETER_ALGORITHM);
        m_downgradeCallout->setTexts(tr("The new encryption is weaker than the current one"),
                                     tr("The document is encrypted with %1 now; the assistant asks for %2. Older algorithms are easier to break.").arg(PDFAppMcpConfirmDialog::sanitize(m_currentMethod), requested));
    }

    if (m_lockoutCallout)
    {
        m_lockoutCallout->setTexts(tr("You will not be able to change or remove this protection yourself"),
                                   tr("With restricted permissions, the certificate does not allow changing the encryption later."));
    }

    if (m_noCertificateCallout)
    {
        m_noCertificateCallout->setTexts(tr("No certificate can sign"),
                                         tr("Create or import a certificate in Manage Certificates of PDF4QT, then ask the assistant again."));
    }

    if (m_secretNote)
    {
        if (m_isSigning)
        {
            m_secretNote->setText(tr("You choose the certificate here; the password of its file is typed only here, a certificate of the system asks for its PIN itself. "
                                     "PDF4QT never sends them to the assistant and does not store them."));
        }
        else
        {
            m_secretNote->setText(m_certificateCard ? tr("The password of the certificate is typed only here. PDF4QT never sends it to the assistant and does not store it.")
                                                    : tr("The password is typed only here. PDF4QT never sends it to the assistant and does not store it."));
        }
    }
}

bool PDFAppMcpSecretConfirmation::isConfirmEnabled() const
{
    const bool isEncryption = m_mode && *m_mode != PDFDocumentToolEncryption::Mode::Unlock;
    if ((!isEncryption && !m_isSigning) || m_outcome != Outcome::Pending)
    {
        return false;
    }

    for (const QPointer<PDFAppToolPasswordCard>& card : { m_openCard, m_ownerCard, m_certificateCard })
    {
        // A certificate of the system store has nothing to type (its card is hidden)
        const bool isNeeded = card != m_certificateCard || isCertificatePasswordNeeded();
        if (card && isNeeded && !card->isComplete())
        {
            return false;
        }
    }
    return !m_certificateList || !m_certificateList->getChosenFilePath().isEmpty();
}

bool PDFAppMcpSecretConfirmation::isCertificatePasswordNeeded() const
{
    const PDFAppToolCertificateItem* item = m_certificateList ? m_certificateList->getChosenItem() : nullptr;
    return item && item->source == PDFAppToolCertificateItem::Source::PfxFile && item->isPasswordProtected;
}

void PDFAppMcpSecretConfirmation::updateCertificateCard()
{
    if (!m_certificateList || !m_certificateCard)
    {
        return;
    }

    // A certificate of the system store has no password here (Sign: its system asks for the PIN; Encrypt
    // with Certificate: only its public certificate is the recipient), nor a .pfx file without a password;
    // the card of another certificate is empty again
    const bool isNeeded = isCertificatePasswordNeeded();
    if (!isNeeded)
    {
        m_certificateCard->clear();
    }
    m_certificateCard->setVisible(isNeeded);
}

void PDFAppMcpSecretConfirmation::updateConfirmState()
{
    if (QPushButton* button = getAllowButton())
    {
        button->setEnabled(isConfirmEnabled());
    }
}

void PDFAppMcpSecretConfirmation::onCardChanged()
{
    // A typed change makes the error of the field old (the card clears its own errors of the
    // repetition and of the characters)
    if (m_errorCallout)
    {
        m_errorCallout->hide();
    }
    updateConfirmState();
}

void PDFAppMcpSecretConfirmation::onCertificateChosen(const QString& fingerprint)
{
    Q_UNUSED(fingerprint);

    if (m_certificateCard)
    {
        m_certificateCard->setErrorText(QString());
    }
    updateCertificateCard();
    updateConfirmState();
}

void PDFAppMcpSecretConfirmation::showOwnerCard()
{
    // Q5.1: the document keeps restrictions, the open password cannot be the owner password
    m_isOwnerForced = true;
    if (!m_ownerCard && m_cardsContainer)
    {
        m_ownerCard = createCard(PDFAppToolPasswordCard::Kind::Owner);
    }
}

void PDFAppMcpSecretConfirmation::showErrors(const std::vector<PDFDocumentToolParameterError>& errors)
{
    using Tool = PDFDocumentToolEncryption;

    for (const QPointer<PDFAppToolPasswordCard>& card : { m_openCard, m_ownerCard, m_certificateCard })
    {
        if (card)
        {
            card->setErrorText(QString());
        }
    }

    QStringList otherErrors;
    for (const PDFDocumentToolParameterError& error : errors)
    {
        PDFAppToolPasswordCard* card = nullptr;
        if (error.parameter == QLatin1String(Tool::PARAMETER_OPEN_PASSWORD))
        {
            card = m_openCard;
        }
        else if (!m_isSigning && (error.parameter == QLatin1String(Tool::PARAMETER_OWNER_PASSWORD) || error.parameter == QLatin1String(Tool::PARAMETER_OWNER_SAME_AS_OPEN)))
        {
            if (m_mode && *m_mode == Tool::Mode::Password)
            {
                showOwnerCard();
            }
            card = m_ownerCard;
        }
        else if (error.parameter == QLatin1String(Tool::PARAMETER_CERTIFICATE) || error.parameter == QLatin1String(Tool::PARAMETER_CERTIFICATE_PASSWORD) ||
                 error.parameter == QLatin1String(PDFDocumentToolSignature::PARAMETER_CERTIFICATE) || error.parameter == QLatin1String(PDFDocumentToolSignature::PARAMETER_CERTIFICATE_PASSWORD))
        {
            card = m_certificateCard;
        }

        // The card of a certificate of the system store is hidden: its error goes to the callout
        if (card && (card != m_certificateCard || isCertificatePasswordNeeded()))
        {
            card->setErrorText(error.message);
        }
        else
        {
            otherErrors << error.message;
        }
    }

    if (m_errorCallout)
    {
        const QString title = m_isSigning ? tr("The document cannot be signed") : tr("The protection cannot be applied");
        m_errorCallout->setTexts(otherErrors.isEmpty() ? QString() : title, otherErrors.join(QLatin1Char('\n')));
        m_errorCallout->setVisible(!otherErrors.isEmpty());
    }
    updateConfirmState();
}

std::vector<PDFDocumentToolParameterError> PDFAppMcpSecretConfirmation::validate(const QVariantMap& values, const std::vector<PDFCertificateEntry>& certificates) const
{
    // The validation of the tool on the document now (the run validates again)
    std::vector<PDFDocumentToolParameterError> errors;
    const PDFAppDocument* document = getConfirmation()->document.data();
    if (!document || !document->getDocument())
    {
        return errors;
    }

    std::unique_ptr<PDFDocumentTool> tool;
    if (m_isSigning)
    {
        tool = std::make_unique<PDFDocumentToolSignature>(PDFDocumentToolSignature::Mode::Sign);
    }
    else if (m_mode)
    {
        tool = std::make_unique<PDFDocumentToolEncryption>(*m_mode);
    }
    if (!tool)
    {
        return errors;
    }

    const PDFDocumentToolParameterDefinitions definitions = tool->getParameters();
    QVariantMap parameters = m_parameters;
    parameters.insert(values);
    QVariantMap normalized;
    errors = PDFDocumentToolParameters::normalize(definitions, parameters, &normalized);
    if (errors.empty())
    {
        PDFDocumentToolContext context = PDFDocumentToolRunner::createContext(document->getSnapshot());
        context.userCertificates = certificates;
        errors = tool->validate(context, normalized);
        PDFAppMcpSecretConfirmationHelper::clearCertificates(context.userCertificates);
    }
    clearSecretValues(normalized);
    clearSecretValues(parameters);
    return errors;
}

bool PDFAppMcpSecretConfirmation::prepareSigningValues(QVariantMap* values, std::vector<PDFCertificateEntry>* certificates)
{
    using Signature = PDFDocumentToolSignature;

    const PDFAppToolCertificateItem* item = m_certificateList ? m_certificateList->getChosenItem() : nullptr;
    if (!item || !item->isChoosable() || !m_provider || !m_certificateCard)
    {
        return false;
    }

    std::optional<PDFCertificateEntry> entry;
    QString fingerprint = item->fingerprint;
    QString password;
    QString errorMessage;
    if (item->source == PDFAppToolCertificateItem::Source::SystemStore)
    {
        // Q6.2: the key stays in the system, which asks for its PIN during the run
        entry = m_provider->findEntry(item->filePath);
    }
    else
    {
        // The .pfx file is opened by its password once (433:3972 for a wrong one; the cache gets its
        // certificate, Q5.11); the certificate must be one, which may sign (5.3, B116)
        password = isCertificatePasswordNeeded() ? m_certificateCard->getPassword() : QString();
        entry = m_provider->unlock(item->filePath, password, &errorMessage);
        if (entry)
        {
            const PDFAppToolCertificateItem unlocked = PDFAppToolCertificateProvider::createItem(*entry, entry->info, QDateTime::currentDateTimeUtc(), PDFAppToolCertificateUsage::Signing);
            fingerprint = unlocked.fingerprint;
            if (!unlocked.isChoosable())
            {
                errorMessage = PDFAppToolCertificateProvider::getDescription(unlocked);
                PDFSecureMemory::clear(entry->pkcs12);
                entry.reset();
            }
        }
    }

    if (!entry)
    {
        PDFSecureMemory::clear(password);
        showErrors({ { QString::fromLatin1(Signature::PARAMETER_CERTIFICATE_PASSWORD),
                       errorMessage.isEmpty() ? PDFAppToolCertificateProvider::tr("Invalid certificate or password.") : errorMessage } });
        return false;
    }

    values->insert(QString::fromLatin1(Signature::PARAMETER_CERTIFICATE), fingerprint);
    if (!password.isEmpty())
    {
        values->insert(QString::fromLatin1(Signature::PARAMETER_CERTIFICATE_PASSWORD), password);
    }
    PDFSecureMemory::clear(password);
    certificates->push_back(std::move(*entry));
    return true;
}

bool PDFAppMcpSecretConfirmation::prepareRunValues()
{
    using Tool = PDFDocumentToolEncryption;

    QVariantMap values;
    std::vector<PDFCertificateEntry> certificates;
    if (m_isSigning)
    {
        if (!prepareSigningValues(&values, &certificates))
        {
            updateConfirmState();
            return false;
        }
    }
    else if (m_certificateList && m_certificateCard && m_provider)
    {
        // A certificate of the system store (the addendum of the owner of 15. 11., the rule of
        // PDFAppToolCertificatePresetPart::prepareRun): its entry of the store - the public certificate is
        // the recipient, no password, its private key is never used. A .pfx file is opened by its password
        // once (433:3972 for a wrong one); its public certificate goes to the cache of the application
        // (Q5.11)
        const PDFAppToolCertificateItem* item = m_certificateList->getChosenItem();
        const QString filePath = m_certificateList->getChosenFilePath();
        const bool isSystemStore = (item && item->source == PDFAppToolCertificateItem::Source::SystemStore) || PDFAppToolCertificateProvider::isSystemStorePath(filePath);
        QString password = isCertificatePasswordNeeded() ? m_certificateCard->getPassword() : QString();
        QString errorMessage;
        std::optional<PDFCertificateEntry> entry;
        if (isSystemStore)
        {
            entry = m_provider->findEntry(filePath);
            if (!entry)
            {
                errorMessage = PDFAppToolCertificatePresetPart::tr("The certificate is no longer in the system store.");
            }
        }
        else
        {
            entry = m_provider->unlock(filePath, password, &errorMessage);
        }
        if (!entry)
        {
            PDFSecureMemory::clear(password);
            showErrors({ { QString::fromLatin1(Tool::PARAMETER_CERTIFICATE), errorMessage.isEmpty() ? PDFAppToolCertificateProvider::tr("Invalid certificate or password.") : errorMessage } });
            return false;
        }

        // The fingerprint of the DER of the certificate, the same parameter for a .pfx file and the store
        values.insert(QString::fromLatin1(Tool::PARAMETER_CERTIFICATE), Tool::getCertificateFingerprint(entry->info.getCertificateData()));
        if (!isSystemStore)
        {
            values.insert(QString::fromLatin1(Tool::PARAMETER_CERTIFICATE_PASSWORD), password);
        }
        PDFSecureMemory::clear(password);
        certificates.push_back(std::move(*entry));
    }
    if (m_openCard)
    {
        values.insert(QString::fromLatin1(Tool::PARAMETER_OPEN_PASSWORD), m_openCard->getPassword());
    }
    if (m_ownerCard)
    {
        values.insert(QString::fromLatin1(Tool::PARAMETER_OWNER_PASSWORD), m_ownerCard->getPassword());
    }
    if (m_isOwnerForced)
    {
        values.insert(QString::fromLatin1(Tool::PARAMETER_OWNER_SAME_AS_OPEN), false);
    }

    // The validation of the tool on the document now (the run validates again): an error of a
    // password is shown at its card and the user fixes it here
    const std::vector<PDFDocumentToolParameterError> errors = validate(values, certificates);
    if (!errors.empty())
    {
        clearSecretValues(values);
        PDFAppMcpSecretConfirmationHelper::clearCertificates(certificates);
        showErrors(errors);
        return false;
    }

    m_runValues = std::move(values);
    m_certificates = std::move(certificates);
    return true;
}

void PDFAppMcpSecretConfirmation::onAllowed(quint64 confirmationId)
{
    Q_UNUSED(confirmationId);

    if (m_outcome != Outcome::Pending || !isConfirmEnabled())
    {
        return;
    }

    if (prepareRunValues())
    {
        finish(Outcome::Confirmed);
    }
}

void PDFAppMcpSecretConfirmation::onDenied(quint64 confirmationId)
{
    Q_UNUSED(confirmationId);
    finish(Outcome::Canceled);
}

void PDFAppMcpSecretConfirmation::onTimeout()
{
    finish(Outcome::TimedOut);
}

void PDFAppMcpSecretConfirmation::onTick()
{
    const qint64 elapsed = m_shownTimer.isValid() ? m_shownTimer.elapsed() : 0;
    setRemainingTime(qMax<qint64>(0, getTimeout() - elapsed));
}

void PDFAppMcpSecretConfirmation::withdraw()
{
    finish(Outcome::Withdrawn);
}

QVariantMap PDFAppMcpSecretConfirmation::takeRunValues()
{
    return std::exchange(m_runValues, QVariantMap());
}

std::vector<PDFCertificateEntry> PDFAppMcpSecretConfirmation::takeUserCertificates()
{
    return std::exchange(m_certificates, std::vector<PDFCertificateEntry>());
}

PDFAppToolCertificateList* PDFAppMcpSecretConfirmation::getCertificateList() const
{
    return m_certificateList.data();
}

QStringList PDFAppMcpSecretConfirmation::getCalloutTexts() const
{
    QStringList texts;
    for (const QPointer<PDFAppToolCallout>& callout : { m_downgradeCallout, m_lockoutCallout, m_errorCallout })
    {
        if (callout && !callout->isHidden())
        {
            texts << callout->getTitleLabel()->text() << callout->getTextLabel()->text();
        }
    }
    return texts;
}

void PDFAppMcpSecretConfirmation::clearSecrets()
{
    for (const QPointer<PDFAppToolPasswordCard>& card : { m_openCard, m_ownerCard, m_certificateCard })
    {
        if (card)
        {
            card->clear();
        }
    }
}

void PDFAppMcpSecretConfirmation::finish(Outcome outcome)
{
    if (m_outcome != Outcome::Pending)
    {
        return;
    }

    m_outcome = outcome;
    m_timer.stop();
    m_tickTimer.stop();

    // S5.1: the fields are empty from now on; the values of a confirmed run wait for the opener
    clearSecrets();
    if (outcome != Outcome::Confirmed)
    {
        clearSecretValues(m_runValues);
        PDFAppMcpSecretConfirmationHelper::clearCertificates(m_certificates);
    }

    // The dialog goes with its scrim, hidden now and retired from the event loop: finish runs inside a signal of the
    // dialog's own subtree (a button, its timer), and the opener reads the values in the slot of the signal
    if (PDFAppMcpConfirmScrim* scrim = qobject_cast<PDFAppMcpConfirmScrim*>(parentWidget()))
    {
        PDFDeferredDeletion::retireLater(scrim);
    }
    else
    {
        PDFDeferredDeletion::retireLater(this);
    }

    Q_EMIT confirmationEnded(outcome);
}

void PDFAppMcpSecretConfirmation::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // The time of the user starts when the question appears (as every confirmation)
    if (!m_isShown && m_outcome == Outcome::Pending)
    {
        m_isShown = true;
        m_shownTimer.start();
        m_timer.start(getTimeout());
        m_tickTimer.start();
        onTick();
    }
}

void PDFAppMcpSecretConfirmation::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
        onTick();
    }
}

bool PDFAppMcpSecretConfirmation::isSupported(const QString& toolId)
{
    const std::optional<PDFDocumentToolEncryption::Mode> mode = PDFDocumentToolEncryption::findMode(toolId);
    return (mode && *mode != PDFDocumentToolEncryption::Mode::Unlock) || PDFDocumentToolSignature::findMode(toolId) == PDFDocumentToolSignature::Mode::Sign;
}

void PDFAppMcpSecretConfirmation::setTimeout(int timeout)
{
    s_timeout = qMax(1, timeout);
}

int PDFAppMcpSecretConfirmation::getTimeout()
{
    return s_timeout;
}

int PDFAppMcpSecretConfirmation::getMethodStrength(const QString& method)
{
    if (method == QLatin1String("RC4 40-bit"))
    {
        return 1;
    }
    if (method == QLatin1String("RC4"))
    {
        return 2;
    }
    if (method == QLatin1String("AES 128-bit") || method == QLatin1String("certificate"))
    {
        // A certificate encryption is AES 128-bit at least (/V 4 AESV2 or /V 5 AESV3 of PDF4QT)
        return 3;
    }
    if (method == QLatin1String("AES 256-bit"))
    {
        return 4;
    }
    return 0;
}

int PDFAppMcpSecretConfirmation::getAlgorithmStrength(const QString& algorithm, bool isCertificate)
{
    if (isCertificate || algorithm == QLatin1String(PDFDocumentToolEncryption::ALGORITHM_AES_256))
    {
        return 4;
    }
    if (algorithm == QLatin1String(PDFDocumentToolEncryption::ALGORITHM_AES_128))
    {
        return 3;
    }
    if (algorithm == QLatin1String(PDFDocumentToolEncryption::ALGORITHM_RC4))
    {
        return 2;
    }
    return 0;
}

void PDFAppMcpSecretConfirmation::clearSecretValues(QVariantMap& values)
{
    while (!values.isEmpty())
    {
        // The map gives its copy up first: the string is cleared, when nobody else holds it
        QVariant value = values.take(values.firstKey());
        if (value.typeId() == QMetaType::QString)
        {
            QString text = value.toString();
            value.clear();
            PDFSecureMemory::clear(text);
        }
    }
}

}   // namespace pdf

#include "moc_pdfappmcpsecretconfirmation.cpp"
