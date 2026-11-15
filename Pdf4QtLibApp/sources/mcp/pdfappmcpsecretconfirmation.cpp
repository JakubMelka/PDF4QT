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
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolrunner.h"

#include "pdffluentwidgets.h"
#include "pdfsecurityhandler.h"
#include "pdfthemefonts.h"

#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QVBoxLayout>

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
    m_mode(PDFDocumentToolEncryption::findMode(toolId))
{
    setObjectName(QStringLiteral("mcpSecretConfirmation"));
    setAttribute(Qt::WA_DeleteOnClose);

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
    if (!mode)
    {
        return confirmation;
    }

    const std::vector<std::pair<QString, QString>> facts = PDFAppMcpSecretConfirmationHelper::createFacts(*mode, parameters);
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
    if (m_mode != PDFDocumentToolEncryption::Mode::Certificate)
    {
        const QVariantMap values = PDFAppMcpSecretConfirmationHelper::normalize(PDFDocumentToolEncryption(*m_mode).getParameters(), m_parameters);
        card->setAlgorithm(PDFAppMcpSecretConfirmationHelper::getAlgorithm(values.value(QString::fromLatin1(PDFDocumentToolEncryption::PARAMETER_ALGORITHM)).toString()));
    }
    else
    {
        card->setAlgorithm(PDFSecurityHandlerFactory::Certificate);
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
            break;
        }

        case Tool::Mode::Unlock:
            break;
    }
    addContent(m_cardsContainer);

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

    if (m_secretNote)
    {
        m_secretNote->setText(m_certificateCard ? tr("The password of the certificate is typed only here. PDF4QT never sends it to the assistant and does not store it.")
                                                : tr("The password is typed only here. PDF4QT never sends it to the assistant and does not store it."));
    }
}

bool PDFAppMcpSecretConfirmation::isConfirmEnabled() const
{
    if (!m_mode || *m_mode == PDFDocumentToolEncryption::Mode::Unlock || m_outcome != Outcome::Pending)
    {
        return false;
    }

    for (const QPointer<PDFAppToolPasswordCard>& card : { m_openCard, m_ownerCard, m_certificateCard })
    {
        if (card && !card->isComplete())
        {
            return false;
        }
    }
    return !m_certificateList || !m_certificateList->getChosenFilePath().isEmpty();
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
        else if (error.parameter == QLatin1String(Tool::PARAMETER_OWNER_PASSWORD) || error.parameter == QLatin1String(Tool::PARAMETER_OWNER_SAME_AS_OPEN))
        {
            if (*m_mode == Tool::Mode::Password)
            {
                showOwnerCard();
            }
            card = m_ownerCard;
        }
        else if (error.parameter == QLatin1String(Tool::PARAMETER_CERTIFICATE) || error.parameter == QLatin1String(Tool::PARAMETER_CERTIFICATE_PASSWORD))
        {
            card = m_certificateCard;
        }

        if (card)
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
        m_errorCallout->setTexts(otherErrors.isEmpty() ? QString() : tr("The protection cannot be applied"), otherErrors.join(QLatin1Char('\n')));
        m_errorCallout->setVisible(!otherErrors.isEmpty());
    }
    updateConfirmState();
}

bool PDFAppMcpSecretConfirmation::prepareRunValues()
{
    using Tool = PDFDocumentToolEncryption;

    QVariantMap values;
    std::vector<PDFCertificateEntry> certificates;
    if (m_certificateList && m_certificateCard && m_provider)
    {
        // The .pfx file is opened by its password once (433:3972 for a wrong one); its public
        // certificate goes to the cache of the application (Q5.11)
        QString password = m_certificateCard->getPassword();
        QString errorMessage;
        std::optional<PDFCertificateEntry> entry = m_provider->unlock(m_certificateList->getChosenFilePath(), password, &errorMessage);
        if (!entry)
        {
            PDFSecureMemory::clear(password);
            m_certificateCard->setErrorText(errorMessage.isEmpty() ? PDFAppToolCertificateProvider::tr("Invalid certificate or password.") : errorMessage);
            updateConfirmState();
            return false;
        }

        values.insert(QString::fromLatin1(Tool::PARAMETER_CERTIFICATE), Tool::getCertificateFingerprint(entry->info.getCertificateData()));
        values.insert(QString::fromLatin1(Tool::PARAMETER_CERTIFICATE_PASSWORD), password);
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
    std::vector<PDFDocumentToolParameterError> errors;
    const PDFAppDocument* document = getConfirmation()->document.data();
    if (document && document->getDocument())
    {
        const Tool tool(*m_mode);
        const PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
        QVariantMap parameters = m_parameters;
        parameters.insert(values);
        QVariantMap normalized;
        errors = PDFDocumentToolParameters::normalize(definitions, parameters, &normalized);
        if (errors.empty())
        {
            PDFDocumentToolContext context = PDFDocumentToolRunner::createContext(document->getSnapshot());
            context.userCertificates = certificates;
            errors = tool.validate(context, normalized);
            PDFAppMcpSecretConfirmationHelper::clearCertificates(context.userCertificates);
        }
        clearSecretValues(normalized);
        clearSecretValues(parameters);
    }

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

    // The dialog goes with its scrim (later: the opener reads the values in the slot of the signal)
    if (PDFAppMcpConfirmScrim* scrim = qobject_cast<PDFAppMcpConfirmScrim*>(parentWidget()))
    {
        scrim->hide();
        scrim->deleteLater();
    }
    else
    {
        hide();
        deleteLater();
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
