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

#include "pdfapptoolgui_certificates.h"
#include "pdfappcertificatedetails.h"
#include "pdfappcertificatedialogs.h"
#include "pdfappcontext.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfapppage.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolgui_protect.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpathrules.h"
#include "pdfapptooltab.h"
#include "pdfapptooltoolbar.h"
#include "pdfcertificatecacherepository.h"
#include "pdfdatabasenotifier.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfdocumenttoolshell.h"
#include "pdfrepositoryhelper.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdftrustedcertificaterepository.h"

#include "pdfcertificatemanager.h"
#include "pdfcertificatestore.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttool_encryption.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"

#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QPushButton>
#include <QTimeZone>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace pdf
{

/// File-local constants and helpers of Manage Certificates
class PDFAppToolGUICertificatesHelper
{
public:
    PDFAppToolGUICertificatesHelper() = delete;

    /// The property of a row of a certificate with its index in the list
    static constexpr const char* ROW_INDEX_PROPERTY = "pdf4qtManagedCertificateRow";

    /// list 436:9550: 20 from the edges of the canvas, the parts 12 apart; heading 436:9551 the
    /// title and the text 2 apart
    static constexpr int CANVAS_MARGIN = 20;
    static constexpr int CANVAS_GAP = 12;
    static constexpr int HEADING_GAP = 2;

    /// card-files 436:9554, card-views 436:9525: padding 8 + the border, the rows 2 apart
    static constexpr int CARD_PADDING = 9;
    static constexpr int VIEWS_PADDING = 7;
    static constexpr int CARD_SPACING = 2;

    /// card-trusted 436:12230: padding 6 + the border; the rows shown without the scroll bar
    static constexpr int TABLE_PADDING = 7;
    static constexpr int MAXIMUM_VISIBLE_ROWS = 15;

    /// sidebar-left 436:9425: x 12 of 300 (the frame of the panels has 10 / 11)
    static constexpr int LEFT_EXTRA_MARGIN = 2;

    /// The current time of the states (tests fix it by PDFAppLocale::setCurrentTimeOverride)
    static QDateTime getNow()
    {
        return QDateTime::fromMSecsSinceEpoch(PDFAppLocale::getCurrentTime());
    }

    /// The absolute path of a folder or a file for comparisons (canonical when it exists)
    static QString normalize(const QString& path)
    {
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
    }

    /// heading 436:9551: Semi Bold 18 in lines of 22, the text Regular 12 text/secondary
    static QWidget* createHeading(const QString& title, const QString& text, const QString& objectName, QWidget* parent)
    {
        QWidget* heading = new QWidget(parent);
        heading->setObjectName(objectName);
        QVBoxLayout* layout = PDFAppPageWidgets::createColumn(heading, HEADING_GAP);
        PDFLabel* titleLabel = PDFAppPageWidgets::createLabel(title, QFont::DemiBold, 18, PDFThemeColors::TextPrimary, heading);
        titleLabel->setLineHeight(22);
        layout->addWidget(titleLabel);
        PDFAppParagraph* textLabel = PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, heading);
        textLabel->setMinimumWidth(1);
        layout->addWidget(textLabel);
        return heading;
    }

    /// A card of rows (card-files 436:9554) with its caption
    static PDFAppToolCard* createRowsCard(const QString& caption, const QString& objectName, QWidget* parent)
    {
        PDFAppToolCard* card = new PDFAppToolCard(parent);
        card->setObjectName(objectName);
        card->getBodyLayout()->setContentsMargins(CARD_PADDING, CARD_PADDING, CARD_PADDING, CARD_PADDING);
        card->getBodyLayout()->setSpacing(CARD_SPACING);
        PDFTileRow* captionRow = new PDFTileRow(caption, QString(), QString(), PDFTileRow::Kind::Caption, card);
        captionRow->setClickable(false);
        card->getBodyLayout()->addWidget(captionRow);
        return card;
    }

    /// The name of a certificate (the common name, else the file name) as one safe line
    static QString getName(const PDFAppToolCertificateItem& item)
    {
        return PDFAppCertificateEntry::sanitizeText(item.getTitle());
    }

    /// A date of the user interface (O17)
    static QString formatDate(const QDateTime& dateTime)
    {
        return PDFAppLocale::formatDate(dateTime.toLocalTime().date());
    }

    /// The text of a count of one or more things ("1 certificate", "3 certificates")
    static QString plural(int count, const char* one, const char* more)
    {
        return count == 1 ? PDFAppToolGUICertificates::tr(one, nullptr, count) : PDFAppToolGUICertificates::tr(more, nullptr, count);
    }

    /// An ISO 8601 date and time in UTC of the result of the tool (empty for none)
    static QString toIso(const QDateTime& dateTime)
    {
        return dateTime.isValid() ? dateTime.toUTC().toString(Qt::ISODate) : QString();
    }

    /// The data of a row of the core tool (no path, no secret)
    /// A .pfx row without details (a file with a password, the run has no database) with the public
    /// certificate of the certificate cache, which the caller read in the GUI thread
    /// (PDFAppDocumentToolCertificates::createCachedCertificates): the details of the certificate, the
    /// file of the row, the state of the usage of Manage Certificates (Signing, as getManagedCertificates).
    /// Another row, or a file the cache does not know, stays as it is.
    static PDFAppToolCertificateItem applyCachedCertificate(const PDFAppToolCertificateItem& item, const std::vector<PDFCertificateEntry>& cachedCertificates, const QDateTime& now)
    {
        if (item.source != PDFAppToolCertificateItem::Source::PfxFile || !item.certificateData.isEmpty())
        {
            return item;
        }

        for (const PDFCertificateEntry& cached : cachedCertificates)
        {
            if (cached.pkcs12FilePath != item.filePath || cached.info.getCertificateData().isEmpty())
            {
                continue;
            }

            // The details by the one rule of a row (createItem), then the row of the file again
            PDFAppToolCertificateItem result = PDFAppToolCertificateProvider::createItem(cached, cached.info, now, PDFAppToolCertificateUsage::Signing);
            result.source = PDFAppToolCertificateItem::Source::PfxFile;
            result.filePath = item.filePath;
            result.isInOldFolder = item.isInOldFolder;
            result.isPasswordProtected = item.isPasswordProtected;
            PDFAppToolCertificateProvider::applyUsage(&result, PDFAppToolCertificateUsage::Signing, now);
            return result;
        }
        return item;
    }

    static QJsonObject createCertificateJson(const PDFAppToolCertificateItem& item, const QDateTime& now)
    {
        using State = PDFAppToolCertificateItem::State;

        QJsonObject object;
        const bool isSystem = item.source == PDFAppToolCertificateItem::Source::SystemStore;
        object.insert(QStringLiteral("source"), isSystem ? QStringLiteral("systemStore") : QStringLiteral("file"));
        object.insert(QStringLiteral("readOnly"), isSystem || item.isInOldFolder);
        if (item.certificateData.isEmpty())
        {
            object.insert(QStringLiteral("state"), QStringLiteral("locked"));
            return object;
        }

        QString state = QStringLiteral("valid");
        if (item.state == State::Expired)
        {
            state = (item.validFrom.isValid() && now < item.validFrom) ? QStringLiteral("notYetValid") : QStringLiteral("expired");
        }
        object.insert(QStringLiteral("state"), state);
        object.insert(QStringLiteral("name"), PDFAppCertificateEntry::sanitizeText(item.name));
        object.insert(QStringLiteral("organization"), PDFAppCertificateEntry::sanitizeText(item.organization));
        object.insert(QStringLiteral("email"), PDFAppCertificateEntry::sanitizeText(item.email));
        object.insert(QStringLiteral("issuer"), PDFAppCertificateEntry::sanitizeText(item.issuer));
        object.insert(QStringLiteral("validFrom"), toIso(item.validFrom));
        object.insert(QStringLiteral("validTo"), toIso(item.validUntil));
        object.insert(QStringLiteral("fingerprint"), item.fingerprint);
        if (std::optional<PDFCertificateInfo> info = PDFCertificateInfo::getCertificateInfo(item.certificateData))
        {
            const QString keyText = PDFAppCertificateDetails::getKeyText(*info);
            object.insert(QStringLiteral("keyAlgorithm"), keyText.section(QLatin1Char(' '), 0, 0));
            object.insert(QStringLiteral("keyBits"), qint64(info->getKeySize()));
        }
        QJsonArray usableFor;
        if (item.isUsableForSigning)
        {
            usableFor.append(QStringLiteral("signing"));
        }
        if (item.isUsableForEncryption)
        {
            usableFor.append(QStringLiteral("encryption"));
        }
        object.insert(QStringLiteral("usableFor"), usableFor);
        return object;
    }

    /// The data of a trusted certificate of the core tool
    static QJsonObject createTrustedJson(const PDFCertificateEntry& entry)
    {
        const PDFAppCertificateEntry::Type type = entry.type == PDFCertificateEntry::EntryType::User ? PDFAppCertificateEntry::Type::User : PDFAppCertificateEntry::Type::System;
        const PDFAppCertificateEntry row = PDFAppCertificateEntry::create(type, 0, entry.info.getCertificateData());

        QJsonObject object;
        object.insert(QStringLiteral("type"), type == PDFAppCertificateEntry::Type::User ? QStringLiteral("user") : QStringLiteral("system"));
        object.insert(QStringLiteral("name"), row.name);
        object.insert(QStringLiteral("organization"), row.organization);
        object.insert(QStringLiteral("validFrom"), toIso(row.validFrom));
        object.insert(QStringLiteral("validTo"), toIso(row.validTo));
        // The fingerprint of the certificates of the user (SHA-256, lower case hexadecimal)
        object.insert(QStringLiteral("fingerprint"), entry.info.getCertificateData().isEmpty() ? QString() : PDFDocumentToolEncryption::getCertificateFingerprint(entry.info.getCertificateData()));
        return object;
    }
};

// ------------------------------------------------------------ PDFAppDocumentToolCertificates

QString PDFAppDocumentToolCertificates::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFAppDocumentToolCertificates::getParameters() const
{
    // A list without a choice (MCP: the action certificates, list only - plan 6.5)
    return PDFDocumentToolParameterDefinitions();
}

PDFDocumentToolResult PDFAppDocumentToolCertificates::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(parameters);
    Q_UNUSED(control);
    Q_UNUSED(progress);

    using Helper = PDFAppToolGUICertificatesHelper;

    // The thread of the run: the files of the folders and the store of the system (thread-safe
    // reads); no database - a file with a password has the details of the certificate cache, which the
    // caller read in the GUI thread (createCachedCertificates), a file never opened stays locked
    const QDateTime now = Helper::getNow();
    const std::vector<PDFAppToolCertificateItem> items = PDFAppToolCertificateProvider(nullptr).getManagedCertificates();
    QJsonArray certificates;
    int fileCount = 0;
    int systemCount = 0;
    for (const PDFAppToolCertificateItem& item : items)
    {
        if (certificates.size() >= MAXIMUM_ROWS)
        {
            break;
        }
        certificates.append(Helper::createCertificateJson(Helper::applyCachedCertificate(item, context.userCertificates, now), now));
        (item.source == PDFAppToolCertificateItem::Source::SystemStore ? systemCount : fileCount) += 1;
    }

    QJsonArray trusted;
    int userCount = 0;
    if (context.certificateStore)
    {
        for (const PDFCertificateEntry& entry : context.certificateStore->getCertificates())
        {
            if (trusted.size() >= MAXIMUM_ROWS)
            {
                break;
            }
            if (entry.type == PDFCertificateEntry::EntryType::AATL)
            {
                continue;
            }
            trusted.append(Helper::createTrustedJson(entry));
            userCount += entry.type == PDFCertificateEntry::EntryType::User ? 1 : 0;
        }
    }

    PDFDocumentToolResult result;
    result.status = PDFDocumentToolResultStatus::Success;
    result.data.insert(QStringLiteral("certificates"), certificates);
    result.data.insert(QStringLiteral("trusted"), trusted);
    result.facts.push_back({ QStringLiteral("certificateFiles"), tr("Certificate files"), PDFAppLocale::formatNumber(fileCount) });
    result.facts.push_back({ QStringLiteral("systemCertificates"), tr("From the system store"), PDFAppLocale::formatNumber(systemCount) });
    result.facts.push_back({ QStringLiteral("trustedCertificates"), tr("Trusted certificates"), PDFAppLocale::formatNumber(trusted.size()) });
    result.facts.push_back({ QStringLiteral("trustedByUser"), tr("Added by you"), PDFAppLocale::formatNumber(userCount) });
    return result;
}

std::vector<PDFCertificateEntry> PDFAppDocumentToolCertificates::createCachedCertificates(PDFAppContext* context)
{
    std::vector<PDFCertificateEntry> certificates;
    if (!context || !context->getDatabase())
    {
        return certificates;
    }

    // Only the public certificate of a file, which the cache knows for its current size and time of
    // change; its PKCS#12 (the encrypted private key) is not copied
    const PDFAppToolCertificateProvider provider(context);
    for (const PDFCertificateEntry& entry : PDFCertificateManager::getCertificates(PDFCertificateUsageFilter::Any))
    {
        if (int(certificates.size()) >= MAXIMUM_ROWS)
        {
            break;
        }
        if (entry.pkcs12.isEmpty() || !entry.info.getCertificateData().isEmpty())
        {
            continue;
        }

        if (std::optional<PDFCertificateInfo> info = provider.findCachedCertificate(entry.pkcs12FilePath))
        {
            PDFCertificateEntry cached;
            cached.type = entry.type;
            cached.info = std::move(*info);
            cached.pkcs12FilePath = entry.pkcs12FilePath;
            cached.isReadOnlyLocation = entry.isReadOnlyLocation;
            certificates.push_back(std::move(cached));
        }
    }
    return certificates;
}

std::shared_ptr<const PDFCertificateStore> PDFAppDocumentToolCertificates::createTrustedStore(PDFDatabase* database)
{
    if (!database)
    {
        return nullptr;
    }

    std::shared_ptr<PDFCertificateStore> store = std::make_shared<PDFCertificateStore>();
    const PDFTrustedCertificateRepository repository(database);
    for (const PDFTrustedCertificate& certificate : repository.getCertificates())
    {
        store->add(PDFCertificateEntry::EntryType::User, certificate.data);
    }
    return store;
}

std::shared_ptr<const PDFCertificateStore> PDFAppDocumentToolCertificates::createVerificationStore(PDFAppContext* context)
{
    PDFDatabase* database = context ? context->getDatabase() : nullptr;
    std::shared_ptr<const PDFCertificateStore> userStore = createTrustedStore(database);
    if (!userStore || !context->getSettings().getBool(PDFSettingKeys::signaturesUseSystemStore()))
    {
        return userStore;
    }

    // The root certificates of the system (O15: the trust of the Signatures panel), at most the rows of
    // the trusted table; the core reads no system store by itself in a run
    std::shared_ptr<PDFCertificateStore> store = std::make_shared<PDFCertificateStore>(*userStore);
    const PDFAppSystemCertificatesTask::Provider provider = PDFAppToolGUICertificates::getSystemCertificatesProvider();
    const PDFCertificateEntries systemCertificates = provider ? provider() : PDFCertificateStore::getSystemCertificates();
    const size_t count = qMin(systemCertificates.size(), size_t(PDFAppCertificateTable::MAXIMUM_SYSTEM_ROWS));
    for (size_t i = 0; i < count; ++i)
    {
        store->add(PDFCertificateEntry::EntryType::System, systemCertificates[i].info);
    }
    return store;
}

// ------------------------------------------------------------ PDFAppToolGUICertificates

PDFAppSystemCertificatesTask::Provider PDFAppToolGUICertificates::s_systemProvider = nullptr;

PDFAppToolGUICertificates::PDFAppToolGUICertificates(QObject* parent) :
    BaseClass(parent)
{
    // The model of the trusted table lives with the GUI (the canvas is made again in a new language)
    m_trustedModel = new PDFAppCertificateTableModel(this);
}

PDFAppToolGUICertificates::~PDFAppToolGUICertificates()
{
    // The dialogs belong to the tool: they close with it and report nothing more; a running
    // reading of the system certificates is dropped with its watcher (a child)
    closeDialogs();
    if (m_systemWatcher)
    {
        m_systemWatcher->disconnect(this);
    }
}

void PDFAppToolGUICertificates::registerCertificatesTools()
{
    const QString toolId = QString::fromLatin1(PDFAppDocumentToolCertificates::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFAppDocumentToolCertificates>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUICertificates>>());
    }
}

PDFAppToolTab* PDFAppToolGUICertificates::findCertificatesTab(const PDFAppMainWindow* window)
{
    if (!window)
    {
        return nullptr;
    }
    for (PDFAppToolTab* tab : window->getToolTabs())
    {
        if (tab && tab->getToolId() == QLatin1String(PDFAppDocumentToolCertificates::TOOL_ID))
        {
            return tab;
        }
    }
    return nullptr;
}

QByteArray PDFAppToolGUICertificates::readCertificateFile(const QByteArray& data)
{
    // RFC 7468, 5.1: the first block of the label CERTIFICATE (the text around it is allowed)
    static constexpr const char* BEGIN = "-----BEGIN CERTIFICATE-----";
    static constexpr const char* END = "-----END CERTIFICATE-----";

    QByteArray der = data;
    const qsizetype begin = data.indexOf(BEGIN);
    if (begin >= 0)
    {
        const qsizetype start = begin + qsizetype(qstrlen(BEGIN));
        const qsizetype end = data.indexOf(END, start);
        if (end < 0)
        {
            return QByteArray();
        }
        QByteArray text;
        for (const char character : data.mid(start, end - start))
        {
            // The lines of the base64 text (RFC 7468: white space between them)
            if (character != ' ' && character != '\t' && character != '\r' && character != '\n')
            {
                text.append(character);
            }
        }
        QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(text, QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
        {
            return QByteArray();
        }
        der = std::move(decoded.decoded);
    }

    return PDFTrustedCertificateRepository::isValidCertificate(der) ? der : QByteArray();
}

void PDFAppToolGUICertificates::setSystemCertificatesProvider(PDFAppSystemCertificatesTask::Provider provider)
{
    s_systemProvider = provider;
}

// ------------------------------------------------------------------- the shell

QString PDFAppToolGUICertificates::getConfirmText() const
{
    // No Primary button (K6.11): the text of the hidden button
    const PDFDocumentToolCatalogEntry* tool = getTool();
    return tool ? tool->getTitle() : QString();
}

bool PDFAppToolGUICertificates::isConfirmEnabled() const
{
    // An analysis without a run of the GUI (K6.11: nothing is written by a "run")
    return false;
}

bool PDFAppToolGUICertificates::isConfirmShown() const
{
    // btn-confirm 436:9423 is hidden (K6.11)
    return false;
}

bool PDFAppToolGUICertificates::isUndoShown() const
{
    // tb-undo 436:9414 and tb-redo 436:9415 are hidden: the tool has no parameters, no history
    return false;
}

bool PDFAppToolGUICertificates::isSingleOwnTab() const
{
    // Plan 4.5: one instance of the tab Certificates
    return true;
}

QString PDFAppToolGUICertificates::getOwnTabTitle() const
{
    // tab-item 436:9402
    return tr("Certificates");
}

QString PDFAppToolGUICertificates::getOwnTabIconName() const
{
    return QStringLiteral("certificate");
}

QString PDFAppToolGUICertificates::getCancelText() const
{
    // btn-cancel 436:9422: the tab has nothing to cancel
    return tr("Close");
}

void PDFAppToolGUICertificates::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    BaseClass::prepareEnvironment(environment);
    if (environment)
    {
        environment->certificateStore = PDFAppDocumentToolCertificates::createTrustedStore(getContext() ? getContext()->getDatabase() : nullptr);
        environment->userCertificates = PDFAppDocumentToolCertificates::createCachedCertificates(getContext());
    }
}

void PDFAppToolGUICertificates::retranslate()
{
    BaseClass::retranslate();

    // The shell makes the panels again after it; the canvas stays, its content is made again
    buildCanvas();
}

QString PDFAppToolGUICertificates::getStatusText() const
{
    using Helper = PDFAppToolGUICertificatesHelper;
    using Source = PDFAppToolCertificateItem::Source;

    if (!m_statusMessage.isEmpty())
    {
        return m_statusMessage;
    }

    if (m_view == View::Trusted)
    {
        // 436:12086: "5 trusted certificates · 2 added by you, 3 from the system store"
        const int userCount = int(m_userEntries.size());
        const int systemCount = int(m_systemEntries.size());
        const QString total = Helper::plural(userCount + systemCount, QT_TR_NOOP("%Ln trusted certificate"), QT_TR_NOOP("%Ln trusted certificates"));
        return tr("%1 · %2, %3").arg(total, tr("%Ln added by you", nullptr, userCount), tr("%Ln from the system store", nullptr, systemCount));
    }

    if (m_certificates.empty())
    {
        // 436:9646
        return tr("No certificates yet");
    }

    // 436:9398: "3 certificates · 2 files in your certificate folder, 1 from the system store"
    int fileCount = 0;
    int oldCount = 0;
    int systemCount = 0;
    for (const PDFAppToolCertificateItem& item : m_certificates)
    {
        if (item.source == Source::SystemStore)
        {
            ++systemCount;
        }
        else
        {
            (item.isInOldFolder ? oldCount : fileCount) += 1;
        }
    }
    QStringList parts;
    if (fileCount > 0)
    {
        parts << Helper::plural(fileCount, QT_TR_NOOP("%Ln file in your certificate folder"), QT_TR_NOOP("%Ln files in your certificate folder"));
    }
    if (oldCount > 0)
    {
        parts << Helper::plural(oldCount, QT_TR_NOOP("%Ln file of an older PDF4QT application"), QT_TR_NOOP("%Ln files of an older PDF4QT application"));
    }
    if (systemCount > 0)
    {
        parts << tr("%Ln from the system store", nullptr, systemCount);
    }
    const QString total = Helper::plural(int(m_certificates.size()), QT_TR_NOOP("%Ln certificate"), QT_TR_NOOP("%Ln certificates"));
    return tr("%1 · %2").arg(total, parts.join(tr(", ", "Separator of the parts of the status of Manage Certificates")));
}

// ------------------------------------------------------------------- the panels

void PDFAppToolGUICertificates::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    // The first part made at the entry (the shell is attached): the certificates are read here once
    if (!m_isLoaded)
    {
        m_isLoaded = true;
        reloadCertificates(QString());
        reloadTrusted();
        reloadSystemTrusted();
        if (PDFDatabaseNotifier* notifier = getContext() ? getContext()->getNotifier() : nullptr)
        {
            connect(notifier, &PDFDatabaseNotifier::changed, this, &PDFAppToolGUICertificates::onDatabaseChanged, Qt::UniqueConnection);
            connect(notifier, &PDFDatabaseNotifier::everythingChanged, this, &PDFAppToolGUICertificates::onEverythingChanged, Qt::UniqueConnection);
        }
    }

    // tool-actions 436:9418: Create…, Import… | Delete, Open Folder; 436:12106: Add Trusted…, Remove
    m_createButton = builder.addAction(tr("Create…"), QStringLiteral("certificate-add"), this, &PDFAppToolGUICertificates::openCreateDialog);
    m_createButton->setObjectName(QStringLiteral("certificatesCreate"));
    m_importButton = builder.addAction(tr("Import…"), QStringLiteral("import"), this, &PDFAppToolGUICertificates::openImportDialog);
    m_importButton->setObjectName(QStringLiteral("certificatesImport"));
    m_actionsDivider = new PDFAppToolbarDivider(Qt::Vertical, 20, 6, PDFThemeColors::BorderSeparator, builder.getWidget());
    m_actionsDivider->setObjectName(QStringLiteral("certificatesDivider"));
    builder.addWidget(m_actionsDivider);
    m_deleteButton = builder.addAction(tr("Delete"), QStringLiteral("trash"), this, &PDFAppToolGUICertificates::openDeleteDialog);
    m_deleteButton->setObjectName(QStringLiteral("certificatesDelete"));
    m_folderButton = builder.addAction(tr("Open Folder"), QStringLiteral("folder-open"), this, &PDFAppToolGUICertificates::openFolder);
    m_folderButton->setObjectName(QStringLiteral("certificatesFolder"));
    m_addTrustedButton = builder.addAction(tr("Add Trusted…"), QStringLiteral("shield-plus"), this, &PDFAppToolGUICertificates::addTrusted);
    m_addTrustedButton->setObjectName(QStringLiteral("certificatesAddTrusted"));
    m_removeTrustedButton = builder.addAction(tr("Remove"), QStringLiteral("list-remove"), this, &PDFAppToolGUICertificates::openRemoveTrustedDialog);
    m_removeTrustedButton->setObjectName(QStringLiteral("certificatesRemoveTrusted"));

    // btn-confirm 436:9423 is hidden (K6.11): isConfirmShown, the shell keeps it hidden
    updateParts();
}

void PDFAppToolGUICertificates::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUICertificatesHelper;

    if (QWidget* widget = builder.getWidget())
    {
        if (QLayout* layout = widget->layout())
        {
            layout->setContentsMargins(Helper::LEFT_EXTRA_MARGIN, 0, Helper::LEFT_EXTRA_MARGIN, 0);
        }
    }

    // header 436:9521
    m_storeHeader = builder.addPanelHeader(tr("Certificate store"), tr("On this computer"));
    m_storeHeader->setObjectName(QStringLiteral("certificatesStoreHeader"));

    // card-views 436:9525: My certificates, Trusted certificates with their counts
    PDFAppToolCard* views = new PDFAppToolCard(builder.getWidget());
    views->setObjectName(QStringLiteral("certificatesViews"));
    views->getBodyLayout()->setContentsMargins(Helper::VIEWS_PADDING, Helper::VIEWS_PADDING, Helper::VIEWS_PADDING, Helper::VIEWS_PADDING);
    views->getBodyLayout()->setSpacing(Helper::CARD_SPACING);
    m_myRow = new PDFTileRow(tr("My certificates"), tr("Sign and encrypt with them"), QStringLiteral("certificate"), PDFTileRow::Kind::ListItem, views);
    m_myRow->setObjectName(QStringLiteral("certificatesViewMy"));
    m_trustedRow = new PDFTileRow(tr("Trusted certificates"), tr("Used to verify signatures"), QStringLiteral("shield-check"), PDFTileRow::Kind::ListItem, views);
    m_trustedRow->setObjectName(QStringLiteral("certificatesViewTrusted"));
    for (PDFTileRow* row : { m_myRow.data(), m_trustedRow.data() })
    {
        row->setClickable(true);
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUICertificates::onViewRowClicked);
        views->getBodyLayout()->addWidget(row);
    }
    builder.addWidget(views);

    // callout-blue 436:9544 / 436:12220 (the texts of the view)
    m_viewCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), QString(), QString());
    m_viewCallout->setObjectName(QStringLiteral("certificatesViewCallout"));
    m_viewCallout->setCompactStyle();
    m_viewCallout->getTitleLabel()->setWordWrap(true);

    updateParts();
}

void PDFAppToolGUICertificates::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-right 436:9432: the detail of the chosen certificate
    m_details = new PDFAppCertificateDetails(builder.getWidget());
    builder.addWidget(m_details);
    updateDetails();
}

QWidget* PDFAppToolGUICertificates::createCanvasPage(QWidget* parent)
{
    // document-canvas 436:9431: the lists over the track of the canvas
    m_canvasPage = new PDFAppToolCanvasPage(parent);
    m_canvasPage->setObjectName(QStringLiteral("certificatesCanvas"));
    buildCanvas();
    return m_canvasPage.data();
}

void PDFAppToolGUICertificates::buildCanvas()
{
    using Helper = PDFAppToolGUICertificatesHelper;

    if (!m_canvasPage)
    {
        return;
    }

    // The old content goes later: it may hold the sender of the change
    if (QWidget* old = m_canvasPage->takeWidget())
    {
        old->hide();
        old->deleteLater();
    }
    m_rows.clear();

    QWidget* content = new QWidget();
    content->setObjectName(QStringLiteral("certificatesContent"));
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(Helper::CANVAS_MARGIN, Helper::CANVAS_MARGIN, Helper::CANVAS_MARGIN, Helper::CANVAS_MARGIN);
    layout->setSpacing(0);
    m_myPage = createMyPage(content);
    layout->addWidget(m_myPage);
    m_trustedPage = createTrustedPage(content);
    layout->addWidget(m_trustedPage);
    layout->addStretch(1);
    m_canvasPage->setWidget(content);

    rebuildRows();
    updateTrustedModel();
    updateParts();
}

QWidget* PDFAppToolGUICertificates::createMyPage(QWidget* parent)
{
    using Helper = PDFAppToolGUICertificatesHelper;

    // list 436:9550: the heading, the card of the files, the card of the system store
    QWidget* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("certificatesMyPage"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(page, Helper::CANVAS_GAP);
    layout->addWidget(Helper::createHeading(tr("My certificates"), tr("Digital IDs with a private key – for signing and for certificate encryption."), QStringLiteral("certificatesMyHeading"), page));

    m_filesCard = Helper::createRowsCard(tr("Certificate files"), QStringLiteral("certificatesFiles"), page);
    layout->addWidget(m_filesCard);
    m_systemCard = Helper::createRowsCard(tr("From the system store"), QStringLiteral("certificatesSystem"), page);
    layout->addWidget(m_systemCard);

    // 436:9646: No certificates yet (portable mode: the buttons Disabled with the reason)
    const bool isCreationAvailable = this->isCreationAvailable();
    QPushButton* emptyCreate = nullptr;
    QPushButton* emptyImport = nullptr;
    m_emptyState = PDFAppToolCertificatePresetPart::createEmptyState(tr("No certificates yet"),
                                                                     isCreationAvailable ? tr("Create a self-signed certificate for yourself, or import a .pfx file you received from a certificate authority.")
                                                                                         : tr("In portable mode no certificate can be created or imported – its private key would stay on this computer."),
                                                                     isCreationAvailable, page, &emptyCreate, &emptyImport);
    m_emptyCreateButton = emptyCreate;
    m_emptyImportButton = emptyImport;
    connect(emptyCreate, &QPushButton::clicked, this, &PDFAppToolGUICertificates::openCreateDialog);
    connect(emptyImport, &QPushButton::clicked, this, &PDFAppToolGUICertificates::openImportDialog);
    layout->addWidget(m_emptyState);

    // Q5.5 (no frame; the callout of the preset of Encrypt with Certificate): the copy of a file of
    // an old application
    m_copyCallout = new PDFAppToolCallout(PDFAccentHue::Blue, QStringLiteral("info"), QString(), QString(), page);
    m_copyCallout->setObjectName(QStringLiteral("certificatesCopyCallout"));
    m_copyCallout->setCompactStyle();
    QPushButton* copyButton = PDFAppPageWidgets::createButton(tr("Copy to My Certificates"), QStringLiteral("copy"), false, m_copyCallout);
    copyButton->setObjectName(QStringLiteral("certificatesCopyButton"));
    m_copyCallout->getBodyLayout()->addWidget(copyButton, 0, Qt::AlignLeading);
    connect(copyButton, &QPushButton::clicked, this, &PDFAppToolGUICertificates::onCopyClicked);
    layout->addWidget(m_copyCallout);
    return page;
}

QWidget* PDFAppToolGUICertificates::createTrustedPage(QWidget* parent)
{
    using Helper = PDFAppToolGUICertificatesHelper;

    // list 436:12226: the heading, the table, the note
    QWidget* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("certificatesTrustedPage"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(page, Helper::CANVAS_GAP);
    layout->addWidget(Helper::createHeading(tr("Trusted certificates"), tr("Certificates you rely on when signatures are verified."), QStringLiteral("certificatesTrustedHeading"), page));

    // card-trusted 436:12230 with the header view of the table (1567:19625, K6.17) - the model and
    // the columns of Settings › Security
    PDFAppToolCard* card = new PDFAppToolCard(page);
    card->setObjectName(QStringLiteral("certificatesTrustedCard"));
    card->getBodyLayout()->setContentsMargins(Helper::TABLE_PADDING, Helper::TABLE_PADDING, Helper::TABLE_PADDING, Helper::TABLE_PADDING);
    m_trustedView = new PDFTreeView(card);
    m_trustedView->setObjectName(QStringLiteral("certificatesTrustedView"));
    m_trustedView->setRowStyle(PDFTreeView::RowStyle::Certificate);
    m_trustedView->setRootIsDecorated(false);
    m_trustedView->setItemsExpandable(false);
    m_trustedView->setUniformRowHeights(true);
    m_trustedView->setFrameShape(QFrame::NoFrame);
    m_trustedView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_trustedView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_trustedView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_trustedView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_trustedView->setTextElideMode(Qt::ElideRight);
    m_trustedView->setModel(m_trustedModel);
    m_trustedView->setAccessibleName(tr("Trusted certificates"));
    QHeaderView* header = m_trustedView->header();
    header->setStretchLastSection(false);
    header->setSectionsMovable(false);
    header->setSectionResizeMode(QHeaderView::Fixed);
    header->setSectionResizeMode(PDFAppCertificateTableModel::NameColumn, QHeaderView::Stretch);
    header->resizeSection(PDFAppCertificateTableModel::TypeColumn, PDFAppCertificateTable::TYPE_COLUMN_WIDTH);
    header->resizeSection(PDFAppCertificateTableModel::OrganizationColumn, PDFAppCertificateTable::ORGANIZATION_COLUMN_WIDTH);
    header->resizeSection(PDFAppCertificateTableModel::ValidFromColumn, PDFAppCertificateTable::VALID_FROM_COLUMN_WIDTH);
    header->resizeSection(PDFAppCertificateTableModel::ValidToColumn, PDFAppCertificateTable::VALID_TO_COLUMN_WIDTH);
    connect(m_trustedView->selectionModel(), &QItemSelectionModel::currentChanged, this, &PDFAppToolGUICertificates::onTrustedCurrentChanged);
    card->getBodyLayout()->addWidget(m_trustedView);
    layout->addWidget(card);

    // 436:12272: the note below the table
    m_trustedNote = PDFAppPageWidgets::createText(tr("“System” certificates come from the operating system and cannot be removed here. You can also trust a certificate from the Signatures panel of a signed document."),
                                                  PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, page);
    m_trustedNote->setObjectName(QStringLiteral("certificatesTrustedNote"));
    m_trustedNote->setMinimumWidth(1);
    layout->addWidget(m_trustedNote);
    return page;
}

// ------------------------------------------------------------------- the state

void PDFAppToolGUICertificates::setView(View view)
{
    if (m_view == view)
    {
        updateParts();
        return;
    }

    m_view = view;
    m_statusMessage.clear();
    updateParts();
    updateDetails();

    // The last call: the shell reads the new status
    updateStatusText();
}

void PDFAppToolGUICertificates::chooseCertificate(const QString& filePath)
{
    m_chosenPath = filePath;
    m_copyError.clear();
    updateRows();
    updateParts();
    updateDetails();
}

void PDFAppToolGUICertificates::chooseTrustedRow(int row)
{
    if (m_trustedView && m_trustedModel && row >= 0 && row < m_trustedModel->rowCount())
    {
        m_trustedView->selectionModel()->setCurrentIndex(m_trustedModel->index(row, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    updateParts();
    updateDetails();
}

const PDFAppToolCertificateItem* PDFAppToolGUICertificates::getChosenCertificate() const
{
    for (const PDFAppToolCertificateItem& item : m_certificates)
    {
        if (!m_chosenPath.isEmpty() && item.filePath == m_chosenPath)
        {
            return &item;
        }
    }
    return nullptr;
}

const PDFAppCertificateEntry* PDFAppToolGUICertificates::getChosenTrustedEntry() const
{
    if (!m_trustedView || !m_trustedModel || !m_trustedView->selectionModel())
    {
        return nullptr;
    }
    const QModelIndex current = m_trustedView->selectionModel()->currentIndex();
    return current.isValid() ? m_trustedModel->getEntry(current.row()) : nullptr;
}

void PDFAppToolGUICertificates::reload()
{
    reloadCertificates(QString());
    reloadTrusted();
    reloadSystemTrusted();
}

QWidget* PDFAppToolGUICertificates::getActionsDivider() const
{
    return m_actionsDivider.data();
}

PDFTileRow* PDFAppToolGUICertificates::getViewRow(View view) const
{
    return view == View::My ? m_myRow.data() : m_trustedRow.data();
}

PDFTileRow* PDFAppToolGUICertificates::getCertificateRow(const QString& filePath) const
{
    for (size_t i = 0; i < m_certificates.size() && i < m_rows.size(); ++i)
    {
        if (m_certificates[i].filePath == filePath)
        {
            return m_rows[i].data();
        }
    }
    return nullptr;
}

PDFAppCertificateCreateDialog* PDFAppToolGUICertificates::getCreateDialog() const
{
    return m_createDialog.data();
}

PDFAppCertificateImportDialog* PDFAppToolGUICertificates::getImportDialog() const
{
    return m_importDialog.data();
}

PDFMessageBox* PDFAppToolGUICertificates::getDeleteDialog() const
{
    return m_deleteDialog.data();
}

PDFMessageBox* PDFAppToolGUICertificates::getRemoveTrustedDialog() const
{
    return m_removeTrustedDialog.data();
}

PDFMessageBox* PDFAppToolGUICertificates::getAddTrustedDialog() const
{
    return m_addTrustedDialog.data();
}

QString PDFAppToolGUICertificates::getRowDescription(const PDFAppToolCertificateItem& item)
{
    using Helper = PDFAppToolGUICertificatesHelper;
    using State = PDFAppToolCertificateItem::State;

    QStringList parts;
    const bool isSystem = item.source == PDFAppToolCertificateItem::Source::SystemStore;
    if (!isSystem)
    {
        parts << PDFAppCertificateEntry::sanitizeText(QFileInfo(item.filePath).fileName());
    }

    const QDateTime now = Helper::getNow();
    if (item.certificateData.isEmpty())
    {
        // Q5.11: a file with a password never opened
        parts << PDFAppToolCertificateProvider::tr("Details are shown after the password");
    }
    else if (item.validUntil.isValid() && now > item.validUntil)
    {
        // 436:9568 "cert_1.pfx · expired on Jan 15, 2025"
        parts << tr("expired on %1").arg(Helper::formatDate(item.validUntil));
    }
    else if (item.validFrom.isValid() && now < item.validFrom)
    {
        parts << tr("valid from %1").arg(Helper::formatDate(item.validFrom));
    }
    else
    {
        // 436:9564 "jane_novak.pfx · Acme Corp · RSA 2048-bit · valid until Sep 12, 2029";
        // 436:9585 "Acme Corp · issued by Acme Issuing CA · valid until Mar 3, 2027"
        if (!item.organization.isEmpty())
        {
            parts << PDFAppCertificateEntry::sanitizeText(item.organization);
        }
        if (isSystem)
        {
            const QString issuer = PDFAppCertificateDetails::isSelfIssued(item.certificateData) ? tr("self-signed") : tr("issued by %1").arg(PDFAppCertificateEntry::sanitizeText(item.issuer));
            parts << issuer;
        }
        else if (std::optional<PDFCertificateInfo> info = PDFCertificateInfo::getCertificateInfo(item.certificateData))
        {
            parts << PDFAppCertificateDetails::getKeyText(*info);
        }
        if (item.validUntil.isValid())
        {
            parts << tr("valid until %1").arg(Helper::formatDate(item.validUntil));
        }
    }

    // Q5.5: a file of the old Editor or Viewer
    if (item.isInOldFolder)
    {
        parts << PDFAppToolCertificateProvider::tr("from an older PDF4QT application");
    }
    return parts.join(PDFAppToolCertificateProvider::tr(" · ", "Separator of the parts of the description of a certificate"));
}

void PDFAppToolGUICertificates::reloadCertificates(const QString& choice)
{
    using Helper = PDFAppToolGUICertificatesHelper;

    QString chosen = choice.isEmpty() ? m_chosenPath : choice;

    // 436:11280: a created or imported file first (the tag New); 436:9398: then the certificates,
    // which can be used, then the expired ones - each group in the order of the folders and the store
    std::vector<PDFAppToolCertificateItem> items = PDFAppToolCertificateProvider(getContext()).getManagedCertificates();
    std::vector<PDFAppToolCertificateItem> newItems;
    std::vector<PDFAppToolCertificateItem> usableItems;
    std::vector<PDFAppToolCertificateItem> expiredItems;
    for (PDFAppToolCertificateItem& item : items)
    {
        const bool isNew = item.source == PDFAppToolCertificateItem::Source::PfxFile && m_newFiles.contains(Helper::normalize(item.filePath));
        if (isNew)
        {
            newItems.push_back(std::move(item));
        }
        else
        {
            (item.state == PDFAppToolCertificateItem::State::Expired ? expiredItems : usableItems).push_back(std::move(item));
        }
    }
    newItems.insert(newItems.end(), std::make_move_iterator(usableItems.begin()), std::make_move_iterator(usableItems.end()));
    newItems.insert(newItems.end(), std::make_move_iterator(expiredItems.begin()), std::make_move_iterator(expiredItems.end()));
    m_certificates = std::move(newItems);

    // The choice stays, when its row is still there; else the first row (436:9398)
    bool isFound = false;
    for (const PDFAppToolCertificateItem& item : m_certificates)
    {
        isFound = isFound || (!chosen.isEmpty() && item.filePath == chosen);
    }
    if (!isFound)
    {
        chosen = m_certificates.empty() ? QString() : m_certificates.front().filePath;
    }
    m_chosenPath = chosen;

    rebuildRows();
    updateParts();
    updateDetails();
}

void PDFAppToolGUICertificates::rebuildRows()
{
    using Helper = PDFAppToolGUICertificatesHelper;

    // The old rows go later: one of them may be the sender of the click, which leads here
    for (const QPointer<PDFTileRow>& row : m_rows)
    {
        if (row)
        {
            row->hide();
            row->deleteLater();
        }
    }
    m_rows.clear();

    if (!m_filesCard || !m_systemCard)
    {
        return;
    }

    for (size_t i = 0; i < m_certificates.size(); ++i)
    {
        const PDFAppToolCertificateItem& item = m_certificates[i];
        const bool isSystem = item.source == PDFAppToolCertificateItem::Source::SystemStore;
        PDFAppToolCard* card = isSystem ? m_systemCard.data() : m_filesCard.data();

        // item-Jane Novak 436:9560: the tile pfx-file / monitor, the name, the description
        PDFTileRow* row = new PDFTileRow(Helper::getName(item), getRowDescription(item), isSystem ? QStringLiteral("monitor") : QStringLiteral("pfx-file"), PDFTileRow::Kind::ListItem, card);
        row->setObjectName(QStringLiteral("certificatesRow-%1").arg(i));
        row->setProperty(Helper::ROW_INDEX_PROPERTY, qint64(i));
        row->setTitleKind(PDFTileRow::TextKind::Data);
        row->setClickable(true);
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);

        // tag 436:9583 "Read-only" (the system store; Q5.5 a file of an old application), 436:11280 "New"
        if (isSystem || item.isInOldFolder)
        {
            row->setTag(tr("Read-only"));
        }
        else if (m_newFiles.contains(Helper::normalize(item.filePath)))
        {
            row->setTag(tr("New"));
        }

        // 436:9572: an expired certificate in accent/red/icon
        if (item.state == PDFAppToolCertificateItem::State::Expired)
        {
            row->setDescriptionColor(PDFThemeColors::AccentRedIcon);
        }
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUICertificates::onCertificateRowClicked);
        card->getBodyLayout()->addWidget(row);
        m_rows.emplace_back(row);
    }
    updateRows();
}

void PDFAppToolGUICertificates::updateRows()
{
    // 436:9560: the chosen row bg/accent-subtle with its tile Accent; the other rows Muted
    for (size_t i = 0; i < m_rows.size() && i < m_certificates.size(); ++i)
    {
        PDFTileRow* row = m_rows[i].data();
        if (!row)
        {
            continue;
        }
        const bool isChosen = !m_chosenPath.isEmpty() && m_certificates[i].filePath == m_chosenPath;
        if (row->isCheckable())
        {
            row->setChecked(isChosen);
        }
        row->setTone(isChosen ? PDFTileRow::Tone::Accent : PDFTileRow::Tone::Muted);
    }
}

void PDFAppToolGUICertificates::updateParts()
{
    using Source = PDFAppToolCertificateItem::Source;

    const bool isMy = m_view == View::My;
    const bool isCreationAvailable = this->isCreationAvailable();
    const PDFAppToolCertificateItem* chosen = getChosenCertificate();

    // The toolbar of the view (436:9418, 436:12106); portable mode Q6.13
    for (QWidget* widget : std::initializer_list<QWidget*>{ m_createButton, m_importButton, m_actionsDivider, m_deleteButton, m_folderButton })
    {
        if (widget)
        {
            widget->setVisible(isMy);
        }
    }
    for (QWidget* widget : std::initializer_list<QWidget*>{ m_addTrustedButton, m_removeTrustedButton })
    {
        if (widget)
        {
            widget->setVisible(!isMy);
        }
    }
    for (QWidget* widget : std::initializer_list<QWidget*>{ m_createButton, m_importButton, m_emptyCreateButton, m_emptyImportButton })
    {
        if (widget)
        {
            widget->setEnabled(isCreationAvailable);
            widget->setToolTip(isCreationAvailable ? QString() : PDFAppToolCertificatePresetPart::getPortableText());
        }
    }
    if (m_createButton)
    {
        m_createButton->setToolTip(isCreationAvailable ? tr("Create…") : PDFAppToolCertificatePresetPart::getPortableText());
    }
    if (m_importButton)
    {
        m_importButton->setToolTip(isCreationAvailable ? tr("Import…") : PDFAppToolCertificatePresetPart::getPortableText());
    }
    if (m_deleteButton)
    {
        QString reason;
        const bool isDeletable = this->isDeletable(chosen, &reason);
        m_deleteButton->setEnabled(isDeletable);
        m_deleteButton->setToolTip(isDeletable ? tr("Delete") : reason);
    }
    if (m_removeTrustedButton)
    {
        // A system row cannot be removed here (436:12272)
        const PDFAppCertificateEntry* entry = getChosenTrustedEntry();
        const bool isRemovable = entry && entry->type == PDFAppCertificateEntry::Type::User;
        m_removeTrustedButton->setEnabled(isRemovable);
        m_removeTrustedButton->setToolTip(isRemovable || !entry ? tr("Remove") : tr("Certificates of the system store are managed by the operating system"));
    }

    // card-views 436:9525: the active view, the counts
    int myCount = int(m_certificates.size());
    int trustedCount = int(m_userEntries.size() + m_systemEntries.size());
    for (PDFTileRow* row : { m_myRow.data(), m_trustedRow.data() })
    {
        if (!row)
        {
            continue;
        }
        const bool isActive = (row == m_myRow) == isMy;
        if (row->isCheckable())
        {
            row->setChecked(isActive);
        }
        row->setTone(isActive ? PDFTileRow::Tone::Accent : PDFTileRow::Tone::Muted);
        row->setShortcutText(PDFAppLocale::formatNumber(row == m_myRow ? myCount : trustedCount));
    }
    if (m_viewCallout)
    {
        if (isMy)
        {
            m_viewCallout->setTexts(tr("Where the files are"), tr("Certificate files (.pfx) are kept in your PDF4QT certificate folder. Certificates of the operating system are shown as well and are managed by the system."));
        }
        else
        {
            // K6.13: Settings › Security
            m_viewCallout->setTexts(tr("Who do you trust?"), tr("A signature is shown as valid when its certificate chain ends in one of these certificates. More verification options are in Settings › Security."));
        }
    }

    // The canvas of the view
    bool hasFiles = false;
    bool hasSystem = false;
    for (const PDFAppToolCertificateItem& item : m_certificates)
    {
        (item.source == Source::SystemStore ? hasSystem : hasFiles) = true;
    }
    if (m_myPage)
    {
        m_myPage->setVisible(isMy);
    }
    if (m_trustedPage)
    {
        m_trustedPage->setVisible(!isMy);
    }
    if (m_filesCard)
    {
        m_filesCard->setVisible(hasFiles);
    }
    if (m_systemCard)
    {
        // No card without certificates of the system store (Linux, macOS)
        m_systemCard->setVisible(hasSystem);
    }
    if (m_emptyState)
    {
        m_emptyState->setVisible(m_certificates.empty());
    }
    if (m_copyCallout)
    {
        // Q5.5: the copy of a file of an old application (it writes a .pfx - not in portable mode)
        m_copyCallout->setVisible(chosen && chosen->isInOldFolder && isCreationAvailable);
        if (m_copyError.isEmpty())
        {
            m_copyCallout->setTexts(tr("This certificate file belongs to an older PDF4QT application"),
                                    tr("Copy it into your certificate folder, so that it stays available when PDF4QT Editor and PDF4QT Viewer are removed."));
        }
        else
        {
            m_copyCallout->setTexts(tr("The certificate file could not be copied"), m_copyError);
        }
    }
}

void PDFAppToolGUICertificates::updateDetails()
{
    if (!m_details)
    {
        return;
    }

    if (m_view == View::Trusted)
    {
        const PDFAppCertificateEntry* entry = getChosenTrustedEntry();
        if (entry)
        {
            m_details->setTrustedCertificate(*entry);
        }
        else
        {
            m_details->setCertificate(std::nullopt);
        }
        return;
    }

    const PDFAppToolCertificateItem* item = getChosenCertificate();
    m_details->setCertificate(item ? std::optional<PDFAppToolCertificateItem>(*item) : std::nullopt);
}

void PDFAppToolGUICertificates::reloadTrusted()
{
    // The rows of the user (also the damaged ones, which can only be removed - Settings › Security)
    std::vector<PDFAppCertificateEntry> entries;
    if (getContext() && getContext()->getDatabase())
    {
        const PDFTrustedCertificateRepository repository(getContext()->getDatabase());
        for (const PDFTrustedCertificate& row : repository.getRows())
        {
            entries.push_back(PDFAppCertificateEntry::create(PDFAppCertificateEntry::Type::User, row.id, row.data));
        }
    }
    m_userEntries = std::move(entries);
    updateTrustedModel();
}

void PDFAppToolGUICertificates::reloadSystemTrusted()
{
    // A reading, which is running, is dropped (its result may be of the old setting)
    if (m_systemWatcher)
    {
        m_systemWatcher->disconnect(this);
        m_systemWatcher->deleteLater();
        m_systemWatcher = nullptr;
    }

    // The system certificates of Settings › Security (signatures.useSystemStore), read by one job
    PDFAppContext* context = getContext();
    const bool isSystemStoreUsed = context && context->getDatabase() && context->getSettings().getBool(PDFSettingKeys::signaturesUseSystemStore());
    QFutureWatcher<PDFAppSystemCertificates>* watcher = new QFutureWatcher<PDFAppSystemCertificates>(this);
    m_systemWatcher = watcher;
    connect(watcher, &QFutureWatcherBase::finished, this, &PDFAppToolGUICertificates::onSystemCertificatesLoaded);
    watcher->setFuture(QtConcurrent::run(PDFAppSystemCertificatesTask(isSystemStoreUsed, s_systemProvider)));
}

void PDFAppToolGUICertificates::updateTrustedModel()
{
    using Helper = PDFAppToolGUICertificatesHelper;

    if (!m_trustedModel)
    {
        return;
    }

    // The current row is kept by its certificate
    const PDFAppCertificateEntry* current = getChosenTrustedEntry();
    const bool hasCurrent = current != nullptr;
    const PDFAppCertificateEntry::Type currentType = current ? current->type : PDFAppCertificateEntry::Type::User;
    const qint64 currentId = current ? current->id : 0;
    const QByteArray currentData = current ? current->data : QByteArray();

    m_trustedModel->setEntries(m_userEntries, m_systemEntries);

    if (m_trustedView && m_trustedView->selectionModel())
    {
        int row = -1;
        if (hasCurrent)
        {
            row = currentType == PDFAppCertificateEntry::Type::User ? m_trustedModel->findUserRow(currentId) : m_trustedModel->findSystemRow(currentData);
        }
        if (row < 0 && m_trustedModel->rowCount() > 0)
        {
            // 436:12086: the first row is chosen
            row = 0;
        }
        if (row >= 0)
        {
            m_trustedView->selectionModel()->setCurrentIndex(m_trustedModel->index(row, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }

        // The table is as high as its rows (then it scrolls)
        const int rows = qBound(1, m_trustedModel->rowCount(), Helper::MAXIMUM_VISIBLE_ROWS);
        const int rowHeight = qMax(34, m_trustedModel->rowCount() > 0 ? m_trustedView->sizeHintForRow(0) : 0);
        m_trustedView->setFixedHeight(m_trustedView->header()->sizeHint().height() + rows * rowHeight + 2 * m_trustedView->frameWidth());
    }

    updateParts();
    updateDetails();
}

void PDFAppToolGUICertificates::setStatusMessage(const QString& message)
{
    m_statusMessage = message;
}

bool PDFAppToolGUICertificates::isCreationAvailable() const
{
    // Q6.13: the rule of step 5 (a private key is never written in portable mode)
    return PDFAppToolCertificatePresetPart(getContext(), nullptr).isCreationAvailable();
}

bool PDFAppToolGUICertificates::isDeletable(const PDFAppToolCertificateItem* item, QString* reason) const
{
    using Helper = PDFAppToolGUICertificatesHelper;

    QString text;
    if (!item)
    {
        text = tr("Choose a certificate file");
    }
    else if (item->source == PDFAppToolCertificateItem::Source::SystemStore)
    {
        // 436:9830
        text = tr("Certificates of the system store are managed by the operating system");
    }
    else if (item->isInOldFolder)
    {
        // Q5.5: the files of the old applications are only read
        text = tr("This file belongs to an older PDF4QT application and is not changed here");
    }
    else if (!isCreationAvailable())
    {
        // Q6.13
        text = PDFAppToolCertificatePresetPart::getPortableText();
    }
    else if (Helper::normalize(QFileInfo(item->filePath).absolutePath()) != Helper::normalize(PDFCertificateManager::getCertificateDirectory()))
    {
        text = tr("Only a file of your certificate folder can be deleted");
    }

    if (reason)
    {
        *reason = text;
    }
    return text.isEmpty();
}

QWidget* PDFAppToolGUICertificates::getDialogParent() const
{
    if (m_canvasPage)
    {
        return m_canvasPage->window();
    }
    return m_details ? m_details->window() : nullptr;
}

void PDFAppToolGUICertificates::closeDialogs()
{
    // The signals of the dialogs to this GUI first (they report nothing into a GUI, which goes)
    for (QWidget* dialog : std::initializer_list<QWidget*>{ m_createDialog, m_importDialog, m_deleteDialog, m_removeTrustedDialog, m_addTrustedDialog })
    {
        if (dialog)
        {
            disconnect(dialog, nullptr, this, nullptr);
            dialog->close();
        }
    }
}

// ------------------------------------------------------------------- the actions

void PDFAppToolGUICertificates::openCreateDialog()
{
    // Portable mode: no private key is written on this computer (also by a call of code)
    if (!isCreationAvailable())
    {
        return;
    }

    // A closed dialog may wait for its deletion: a new one opens then
    if (m_createDialog && m_createDialog->isVisible())
    {
        m_createDialog->raise();
        m_createDialog->activateWindow();
        return;
    }

    // new + QPointer: the dialog deletes itself, when it closes
    PDFAppCertificateCreateDialog* dialog = new PDFAppCertificateCreateDialog(getContext(), getDialogParent());
    m_createDialog = dialog;
    connect(dialog, &PDFAppCertificateCreateDialog::certificateCreated, this, &PDFAppToolGUICertificates::onCertificateCreated);
    dialog->open();
}

void PDFAppToolGUICertificates::openImportDialog()
{
    if (!isCreationAvailable())
    {
        return;
    }

    if (m_importDialog && m_importDialog->isVisible())
    {
        m_importDialog->raise();
        m_importDialog->activateWindow();
        return;
    }

    // Plan 7.2: the file comes only from the file dialog of the user. The file dialog runs an
    // event loop - this GUI, its tab or the window may be gone after it.
    QPointer<PDFAppToolGUICertificates> guard(this);
    const QString filePath = PDFAppPanelFileDialogs::getOpenFileName(getDialogParent(), tr("Import Certificate"), QString(), PDFAppCertificateImportDialog::getFileFilter());
    if (!guard || filePath.isEmpty())
    {
        return;
    }

    PDFAppCertificateImportDialog* dialog = new PDFAppCertificateImportDialog(getContext(), filePath, getDialogParent());
    m_importDialog = dialog;
    connect(dialog, &PDFAppCertificateImportDialog::certificateImported, this, &PDFAppToolGUICertificates::onCertificateImported);

    // The last call: the import may report its file (and close the dialog) at once
    dialog->start();
}

void PDFAppToolGUICertificates::openDeleteDialog()
{
    using Helper = PDFAppToolGUICertificatesHelper;

    const PDFAppToolCertificateItem* item = getChosenCertificate();
    if (!isDeletable(item, nullptr) || (m_deleteDialog && m_deleteDialog->isVisible()))
    {
        return;
    }

    // 436:11809 (R6-13: the frame - Cancel Primary, Delete Certificate Secondary)
    const QString fileName = PDFAppCertificateEntry::sanitizeText(QFileInfo(item->filePath).fileName());
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Critical, tr("Delete certificate “%1”?").arg(Helper::getName(*item)),
                                              tr("The file %1 is deleted from your certificate folder. Documents encrypted for this certificate can no longer be opened with it. This cannot be undone.").arg(fileName),
                                              QDialogButtonBox::NoButton, getDialogParent());
    dialog->setObjectName(QStringLiteral("certificatesDeleteDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    PDFAppMainWindow::prepareDialog(dialog, getDialogParent());
    QPushButton* cancel = dialog->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Trailing, true);
    cancel->setObjectName(QStringLiteral("certificatesDeleteCancel"));
    connect(cancel, &QPushButton::clicked, dialog, &PDFMessageBox::reject);
    QPushButton* remove = dialog->addActionButton(tr("Delete Certificate"), PDFMessageBox::ActionPosition::Trailing, false);
    remove->setObjectName(QStringLiteral("certificatesDeleteConfirm"));
    remove->setIcon(PDFThemeIcons::getIcon(QStringLiteral("trash")));
    connect(remove, &QPushButton::clicked, this, &PDFAppToolGUICertificates::onDeleteConfirmed);
    m_deleteDialog = dialog;
    m_pendingDeletePath = item->filePath;

    // The last call: the dialog is shown (window modal, no nested loop)
    dialog->open();
}

void PDFAppToolGUICertificates::openFolder()
{
    // Open Folder (436:9516): the certificate folder of the application, made when it is missing
    const QString folder = PDFCertificateManager::getCertificateDirectory();
    if (folder.isEmpty())
    {
        return;
    }
    QDir().mkpath(folder);

    // The page of the tab opens the folder (its navigator checks the address: local folders only;
    // the pattern of Show in Folder of Settings › Storage). The last call: code of the application.
    for (QWidget* widget = m_canvasPage ? m_canvasPage->parentWidget() : nullptr; widget; widget = widget->parentWidget())
    {
        if (PDFAppPage* page = qobject_cast<PDFAppPage*>(widget))
        {
            Q_EMIT page->externalUrlRequested(QUrl::fromLocalFile(QFileInfo(folder).absoluteFilePath()));
            return;
        }
    }
}

void PDFAppToolGUICertificates::addTrusted()
{
    if (!getContext() || !getContext()->getDatabase())
    {
        return;
    }

    // The file dialog runs an event loop: this GUI may be gone after it
    QPointer<PDFAppToolGUICertificates> guard(this);
    const QString filePath = PDFAppPanelFileDialogs::getOpenFileName(getDialogParent(), tr("Add Trusted Certificate"), QString(), tr("Certificates (*.cer *.crt *.der *.pem)"));
    if (!guard || filePath.isEmpty())
    {
        return;
    }

    // Plan 7: a network place is not contacted; a file of a hostile size is never read whole
    const QString fileName = PDFAppCertificateEntry::sanitizeText(QFileInfo(filePath).fileName());
    const QFileInfo fileInfo(filePath);
    QByteArray data;
    if (!PDFAppToolPathRules::isDeviceOrNetworkPath(filePath) && fileInfo.isFile() && fileInfo.size() <= MAXIMUM_TRUSTED_FILE_SIZE)
    {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly))
        {
            data = file.read(MAXIMUM_TRUSTED_FILE_SIZE + 1);
        }
    }
    const QByteArray der = data.size() <= MAXIMUM_TRUSTED_FILE_SIZE ? readCertificateFile(data) : QByteArray();
    if (der.isEmpty())
    {
        // No frame (2.6: 436:11538 with another text)
        showAddTrustedProblem(tr("The certificate cannot be added"), tr("The file %1 is not a certificate (DER or PEM) on this computer.").arg(fileName));
        return;
    }

    PDFTrustedCertificateRepository repository(getContext()->getDatabase());
    const PDFAppCertificateEntry entry = PDFAppCertificateEntry::create(PDFAppCertificateEntry::Type::User, 0, der);
    if (repository.contains(der))
    {
        showAddTrustedProblem(tr("This certificate is trusted already"), tr("“%1” is in your trusted certificates. Choose another file.").arg(entry.name));
        return;
    }

    // The commit notifies the database at once (PDFDatabaseNotifier): code of the application runs
    const PDFDatabaseError error = repository.addCertificate(der);
    if (!guard)
    {
        return;
    }
    if (error.isError() || !repository.contains(der))
    {
        showAddTrustedProblem(tr("The certificate cannot be added"), tr("The trusted certificates could not be saved. Try it again."));
        return;
    }

    // The new row is chosen (the notification of the database reads the rows again later too)
    reloadTrusted();
    if (m_trustedModel)
    {
        for (const PDFTrustedCertificate& row : repository.getRows())
        {
            if (row.data == der)
            {
                chooseTrustedRow(m_trustedModel->findUserRow(row.id));
            }
        }
    }
    setStatusMessage(tr("“%1” is trusted now").arg(entry.name));

    // The last call: the shell reads the new status
    updateStatusText();
}

void PDFAppToolGUICertificates::openRemoveTrustedDialog()
{
    const PDFAppCertificateEntry* entry = getChosenTrustedEntry();
    if (!entry || entry->type != PDFAppCertificateEntry::Type::User || (m_removeTrustedDialog && m_removeTrustedDialog->isVisible()))
    {
        return;
    }

    // 436:12324 (R6-13: Cancel Primary, Remove Secondary with list-remove)
    const QString name = entry->name.isEmpty() ? tr("Certificate without a name") : entry->name;
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("Stop trusting “%1”?").arg(name),
                                              tr("Signatures made with certificates issued by it will no longer be shown as valid. You can add it again later."),
                                              QDialogButtonBox::NoButton, getDialogParent());
    dialog->setObjectName(QStringLiteral("certificatesRemoveTrustedDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    PDFAppMainWindow::prepareDialog(dialog, getDialogParent());
    QPushButton* cancel = dialog->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Trailing, true);
    cancel->setObjectName(QStringLiteral("certificatesRemoveTrustedCancel"));
    connect(cancel, &QPushButton::clicked, dialog, &PDFMessageBox::reject);
    QPushButton* remove = dialog->addActionButton(tr("Remove"), PDFMessageBox::ActionPosition::Trailing, false);
    remove->setObjectName(QStringLiteral("certificatesRemoveTrustedConfirm"));
    remove->setIcon(PDFThemeIcons::getIcon(QStringLiteral("list-remove")));
    connect(remove, &QPushButton::clicked, this, &PDFAppToolGUICertificates::onRemoveTrustedConfirmed);
    m_removeTrustedDialog = dialog;

    PDFTrustedCertificate row;
    row.id = entry->id;
    row.data = entry->data;
    m_pendingRemoval = row;

    dialog->open();
}

void PDFAppToolGUICertificates::showAddTrustedProblem(const QString& title, const QString& text)
{
    if (m_addTrustedDialog)
    {
        disconnect(m_addTrustedDialog.data(), nullptr, this, nullptr);
        m_addTrustedDialog->close();
    }

    // 2.6: as the collision of the import 436:11538 - Cancel, Choose Another File… (Primary)
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning, title, text, QDialogButtonBox::NoButton, getDialogParent());
    dialog->setObjectName(QStringLiteral("certificatesAddTrustedDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setCardWidth(480);
    PDFAppMainWindow::prepareDialog(dialog, getDialogParent());
    QPushButton* cancel = dialog->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Trailing, false);
    cancel->setObjectName(QStringLiteral("certificatesAddTrustedCancel"));
    connect(cancel, &QPushButton::clicked, dialog, &PDFMessageBox::reject);
    QPushButton* choose = dialog->addActionButton(tr("Choose Another File…"), PDFMessageBox::ActionPosition::Trailing, true);
    choose->setObjectName(QStringLiteral("certificatesAddTrustedChoose"));
    choose->setIcon(PDFThemeIcons::getIcon(QStringLiteral("import")));
    connect(choose, &QPushButton::clicked, this, &PDFAppToolGUICertificates::onAddTrustedChooseAgain);
    m_addTrustedDialog = dialog;
    dialog->open();
}

// ------------------------------------------------------------------- the slots

void PDFAppToolGUICertificates::onViewRowClicked()
{
    const QObject* row = sender();
    if (row == m_myRow)
    {
        setView(View::My);
    }
    else if (row == m_trustedRow)
    {
        setView(View::Trusted);
    }
}

void PDFAppToolGUICertificates::onCertificateRowClicked()
{
    PDFTileRow* row = qobject_cast<PDFTileRow*>(sender());
    bool isValid = false;
    const qint64 index = row ? row->property(PDFAppToolGUICertificatesHelper::ROW_INDEX_PROPERTY).toLongLong(&isValid) : -1;
    if (!isValid || index < 0 || size_t(index) >= m_certificates.size())
    {
        return;
    }

    m_statusMessage.clear();
    chooseCertificate(m_certificates[size_t(index)].filePath);

    // The last call: the shell reads the new status
    updateStatusText();
}

void PDFAppToolGUICertificates::onTrustedCurrentChanged()
{
    updateParts();
    updateDetails();
}

void PDFAppToolGUICertificates::onCertificateCreated(const QString& fileName)
{
    using Helper = PDFAppToolGUICertificatesHelper;

    // 436:11280: the new row first with the tag New, chosen; the status names it
    m_newFiles.insert(Helper::normalize(fileName));
    reloadCertificates(fileName);
    const PDFAppToolCertificateItem* item = getChosenCertificate();
    setStatusMessage(tr("Certificate \"%1\" was created and saved as %2").arg(item ? Helper::getName(*item) : QString(), QFileInfo(fileName).fileName()));

    // The last call: the shell reads the new status
    updateStatusText();
}

void PDFAppToolGUICertificates::onCertificateImported(const QString& fileName)
{
    using Helper = PDFAppToolGUICertificatesHelper;

    m_newFiles.insert(Helper::normalize(fileName));
    reloadCertificates(fileName);
    setStatusMessage(tr("The certificate file %1 was imported").arg(PDFAppCertificateEntry::sanitizeText(QFileInfo(fileName).fileName())));
    updateStatusText();
}

void PDFAppToolGUICertificates::onDeleteConfirmed()
{
    using Helper = PDFAppToolGUICertificatesHelper;

    const QString filePath = m_pendingDeletePath;
    m_pendingDeletePath.clear();
    if (m_deleteDialog)
    {
        disconnect(m_deleteDialog.data(), nullptr, this, nullptr);
        m_deleteDialog->close();
    }

    // The rules again (the list may have changed meanwhile): only a file of the own folder
    const PDFAppToolCertificateItem* item = nullptr;
    for (const PDFAppToolCertificateItem& candidate : m_certificates)
    {
        item = (!filePath.isEmpty() && candidate.filePath == filePath) ? &candidate : item;
    }
    if (!item || !isDeletable(item, nullptr))
    {
        return;
    }

    // The row is not used after the write of the database: its commit notifies the database at once
    // (PDFDatabaseNotifier) - code of the application runs, the list may be read again
    const QString name = Helper::getName(*item);
    item = nullptr;
    if (QFile::remove(filePath))
    {
        // The row of the certificate cache goes with its file
        if (getContext() && getContext()->getDatabase())
        {
            QPointer<PDFAppToolGUICertificates> guard(this);
            PDFCertificateCacheRepository(getContext()->getDatabase()).remove(PDFRepositoryHelper::normalizePath(filePath));
            if (!guard)
            {
                return;
            }
        }
        m_newFiles.remove(Helper::normalize(filePath));
        setStatusMessage(tr("Certificate \"%1\" was deleted").arg(name));
    }
    else
    {
        setStatusMessage(tr("The file %1 could not be deleted").arg(PDFAppCertificateEntry::sanitizeText(QFileInfo(filePath).fileName())));
    }

    reloadCertificates(QString());

    // The last call: the shell reads the new status
    updateStatusText();
}

void PDFAppToolGUICertificates::onRemoveTrustedConfirmed()
{
    std::optional<PDFTrustedCertificate> row = m_pendingRemoval;
    m_pendingRemoval.reset();
    if (m_removeTrustedDialog)
    {
        disconnect(m_removeTrustedDialog.data(), nullptr, this, nullptr);
        m_removeTrustedDialog->close();
    }
    if (!row || !getContext() || !getContext()->getDatabase())
    {
        return;
    }

    // Only the row with the data, which the user confirmed (removeRows checks them)
    const PDFAppCertificateEntry entry = PDFAppCertificateEntry::create(PDFAppCertificateEntry::Type::User, row->id, row->data);
    PDFTrustedCertificateRepository repository(getContext()->getDatabase());
    QPointer<PDFAppToolGUICertificates> guard(this);
    const PDFDatabaseError error = repository.removeRows({ *row });
    if (!guard)
    {
        return;
    }
    setStatusMessage(error.isError() ? tr("The trusted certificates could not be saved. Try it again.") : tr("“%1” is no longer trusted").arg(entry.name));
    reloadTrusted();

    // The last call: the shell reads the new status
    updateStatusText();
}

void PDFAppToolGUICertificates::onAddTrustedChooseAgain()
{
    if (m_addTrustedDialog)
    {
        disconnect(m_addTrustedDialog.data(), nullptr, this, nullptr);
        m_addTrustedDialog->close();
    }

    // The last call: the file dialog runs an event loop
    addTrusted();
}

void PDFAppToolGUICertificates::onCopyClicked()
{
    const PDFAppToolCertificateItem* item = getChosenCertificate();
    if (!item || !item->isInOldFolder || !isCreationAvailable())
    {
        return;
    }

    // The copy writes the certificate cache: its commit notifies the database at once (code of the
    // application runs, the list may be read again - the row is not used after it)
    QString error;
    const QString filePath = item->filePath;
    item = nullptr;
    QPointer<PDFAppToolGUICertificates> guard(this);
    const QString copyPath = PDFAppToolCertificateProvider(getContext()).copyToApplicationFolder(filePath, &error);
    if (!guard)
    {
        return;
    }
    if (copyPath.isEmpty())
    {
        m_copyError = error.isEmpty() ? tr("The certificate file could not be written into your certificate folder.") : error;
        updateParts();
        return;
    }

    m_copyError.clear();
    reloadCertificates(copyPath);
    setStatusMessage(tr("The certificate file %1 was copied into your certificate folder").arg(PDFAppCertificateEntry::sanitizeText(QFileInfo(copyPath).fileName())));
    updateStatusText();
}

void PDFAppToolGUICertificates::onSystemCertificatesLoaded()
{
    QFutureWatcher<PDFAppSystemCertificates>* watcher = m_systemWatcher;
    if (!watcher || sender() != watcher)
    {
        return;
    }

    PDFAppSystemCertificates result = watcher->result();
    m_systemWatcher = nullptr;
    watcher->deleteLater();
    m_systemEntries = std::move(result.entries);
    updateTrustedModel();

    // The last call: the counts of the status
    updateStatusText();
}

void PDFAppToolGUICertificates::onDatabaseChanged(const QString& topic, const QString& key)
{
    // A change of Settings › Security, of the Signatures panel or of another instance
    if (topic == PDFTrustedCertificateRepository::getTopic())
    {
        reloadTrusted();
    }
    else if (topic == PDFCertificateCacheRepository::getTopic())
    {
        // A file was opened by its password elsewhere: its details are known now
        reloadCertificates(QString());
    }
    else if (topic == PDFSettingsRepository::getTopic() && key == PDFSettingKeys::signaturesUseSystemStore())
    {
        reloadSystemTrusted();
    }
    else
    {
        return;
    }

    // The last call: the counts of the status
    updateStatusText();
}

void PDFAppToolGUICertificates::onEverythingChanged()
{
    reload();
    updateStatusText();
}

}   // namespace pdf

#include "moc_pdfapptoolgui_certificates.cpp"
