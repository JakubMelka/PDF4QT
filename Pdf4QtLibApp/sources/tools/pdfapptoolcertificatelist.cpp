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

#include "pdfapptoolcertificatelist.h"
#include "pdfappcertificatedialogs.h"
#include "pdfappcontext.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfappworkspace.h"
#include "pdfcertificatecacherepository.h"
#include "pdfrepositoryhelper.h"

#include "pdfcertificatemanager.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentrows.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocumenttool_encryption.h"
#include "pdffluentwidgets.h"
#include "pdfsecurityhandler.h"
#include "pdfthemefonts.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTimeZone>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

namespace pdf
{

/// File-local constants and helpers of the list of the certificates
class PDFAppToolCertificateListHelper
{
public:
    PDFAppToolCertificateListHelper() = delete;

    /// The property of a row with its index in the list
    static constexpr const char* ROW_INDEX_PROPERTY = "pdf4qtCertificateRow";

    /// card-certificates 433:3186: padding 8 + the border, the rows 2 apart
    static constexpr int LIST_PADDING = 9;
    static constexpr int LIST_SPACING = 2;

    /// form 433:3182: the parts 12 apart; heading 433:3183: the title and the text 2 apart
    static constexpr int FORM_GAP = 12;
    static constexpr int HEADING_GAP = 2;

    /// card-certificate-password 433:3221: padding 14 + the border, gap 10; the field 433:3236 gap 4
    static constexpr int PASSWORD_PADDING = 15;
    static constexpr int PASSWORD_GAP = 10;
    static constexpr int FIELD_GAP = 4;

    /// empty-state 433:3422: padding 28 + the border, gap 10; the tile 44 with the icon 16, radius 8
    static constexpr int EMPTY_PADDING = 29;
    static constexpr int EMPTY_GAP = 10;
    static constexpr int EMPTY_TILE = 44;
    static constexpr int EMPTY_ICON = 16;
    static constexpr int EMPTY_TILE_RADIUS = 8;
    static constexpr int EMPTY_BUTTON_GAP = 8;

    /// The current time of the states of the rows (tests fix it by PDFAppLocale::setCurrentTimeOverride)
    static QDateTime getNow()
    {
        return QDateTime::fromMSecsSinceEpoch(PDFAppLocale::getCurrentTime());
    }

    /// The file name of a path (the rows show only the name of a .pfx file, never its folder)
    static QString getFileName(const QString& filePath)
    {
        return QFileInfo(filePath).fileName();
    }

    /// A label of a plain text of the font of the weight and size and its line
    static PDFLabel* createLabel(const QString& text, QFont::Weight weight, int pixelSize, int lineHeight, PDFThemeColors::Color color, QWidget* parent)
    {
        PDFLabel* label = PDFAppPageWidgets::createLabel(text, weight, pixelSize, color, parent);
        label->setLineHeight(lineHeight);
        return label;
    }

    /// The row of the file (\p isChoosableOnly: only a row, which can be chosen), nullptr = none
    static const PDFAppToolCertificateItem* findItem(const std::vector<PDFAppToolCertificateItem>& items, const QString& filePath, bool isChoosableOnly)
    {
        if (filePath.isEmpty())
        {
            return nullptr;
        }

        for (const PDFAppToolCertificateItem& item : items)
        {
            if (item.filePath == filePath && (!isChoosableOnly || item.isChoosable()))
            {
                return &item;
            }
        }
        return nullptr;
    }

    /// The first row, which can be chosen, nullptr = none
    static const PDFAppToolCertificateItem* findFirstChoosable(const std::vector<PDFAppToolCertificateItem>& items)
    {
        for (const PDFAppToolCertificateItem& item : items)
        {
            if (item.isChoosable())
            {
                return &item;
            }
        }
        return nullptr;
    }

    /// The row can be chosen (the predicate of sortChoosableFirst)
    static bool isChoosable(const PDFAppToolCertificateItem& item)
    {
        return item.isChoosable();
    }

    /// The rows, which can be chosen, first, each group in its order (433:3049: the expired row last)
    static void sortChoosableFirst(std::vector<PDFAppToolCertificateItem>* items)
    {
        std::stable_partition(items->begin(), items->end(), &PDFAppToolCertificateListHelper::isChoosable);
    }

    /// The file of an old folder has its copy (the same bytes) in the folder of the application
    static bool hasCopyInApplicationFolder(const PDFCertificateEntries& entries, const PDFCertificateEntry& entry)
    {
        for (const PDFCertificateEntry& other : entries)
        {
            if (!other.isReadOnlyLocation && !other.pkcs12.isEmpty() && other.pkcs12 == entry.pkcs12)
            {
                return true;
            }
        }
        return false;
    }
};

// ------------------------------------------------------------ PDFAppToolCertificateItem

QString PDFAppToolCertificateItem::getTitle() const
{
    if (!name.isEmpty())
    {
        return name;
    }
    if (source == Source::SystemStore)
    {
        // A certificate of the system store without a name (no file to name it by)
        return QCoreApplication::translate("pdf::PDFAppToolCertificateItem", "Certificate without a name");
    }
    return PDFAppToolCertificateListHelper::getFileName(filePath);
}

// ------------------------------------------------------------ PDFAppToolCertificateProvider

PDFAppToolCertificateProvider::PDFAppToolCertificateProvider(PDFAppContext* context) :
    m_context(context)
{

}

std::vector<PDFAppToolCertificateItem> PDFAppToolCertificateProvider::getCertificates(Usage usage) const
{
    std::vector<PDFAppToolCertificateItem> items = createItems(usage, false);

    // The rows, which can be chosen, first (433:3049: the expired row last)
    PDFAppToolCertificateListHelper::sortChoosableFirst(&items);
    return items;
}

std::vector<PDFAppToolCertificateItem> PDFAppToolCertificateProvider::getManagedCertificates() const
{
    return createItems(Usage::Signing, true);
}

std::vector<PDFAppToolCertificateItem> PDFAppToolCertificateProvider::createItems(Usage usage, bool isEverySystemCertificate) const
{
    const QDateTime now = PDFAppToolCertificateListHelper::getNow();
    const PDFCertificateEntries entries = PDFCertificateManager::getCertificates(PDFCertificateUsageFilter::Any);

    std::vector<PDFAppToolCertificateItem> items;
    for (const PDFCertificateEntry& entry : entries)
    {
        std::optional<PDFCertificateInfo> info;
        if (!entry.pkcs12.isEmpty())
        {
            // A file of an old folder, which was copied into the folder of the application, is
            // listed once - as the copy (Q5.5)
            if (entry.isReadOnlyLocation && PDFAppToolCertificateListHelper::hasCopyInApplicationFolder(entries, entry))
            {
                continue;
            }

            // A file without a password has its certificate in the entry, the other files have
            // it only in the cache after their first unlock (Q5.11)
            if (!entry.info.getCertificateData().isEmpty())
            {
                info = entry.info;
            }
            else
            {
                info = findCachedCertificate(entry.pkcs12FilePath);
            }
        }
        else
        {
            if (!isSystemStoreIncluded() || entry.info.getCertificateData().isEmpty())
            {
                continue;
            }

            // The personal store has other certificates too (a server, an internal one of the
            // system): a list of a usage shows those of the usage - the recipients of the encryption
            // (the addendum of 15. 11.), Q6.2 the ones, which may sign; Manage Certificates all of them
            const bool isOfUsage = usage == Usage::Encryption ? PDFCertificateManager::isUsableForEncryption(entry.info.getCertificateData())
                                                              : isUsableForSigning(entry.info);
            if (!isEverySystemCertificate && !isOfUsage)
            {
                continue;
            }
            info = entry.info;
        }

        items.push_back(createItem(entry, info, now, usage));
    }
    return items;
}

QString PDFAppToolCertificateProvider::getSystemStorePath(const QString& fingerprint)
{
    return QString::fromLatin1(SYSTEM_STORE_PREFIX) + fingerprint;
}

bool PDFAppToolCertificateProvider::isSystemStorePath(const QString& filePath)
{
    return filePath.startsWith(QLatin1String(SYSTEM_STORE_PREFIX));
}

bool PDFAppToolCertificateProvider::isUsableForSigning(const PDFCertificateInfo& info)
{
    // The one rule of PDFSignatureFactory::unlock (step 6 of phase 2, the extended key usages of
    // documentSigning, Adobe, Microsoft Document Signing and anyExtendedKeyUsage among it), by the
    // certificate itself. An info without its certificate is no certificate, which could sign (a
    // .pfx, whose certificate is not known, is Locked before its usage matters)
    return !info.getCertificateData().isEmpty() && PDFCertificateManager::isUsableForSigning(info.getCertificateData(), nullptr);
}

void PDFAppToolCertificateProvider::applyUsage(PDFAppToolCertificateItem* item, Usage usage, const QDateTime& now)
{
    using State = PDFAppToolCertificateItem::State;

    if (!item)
    {
        return;
    }

    // A row of the system store follows the same rules: it signs by the store (Q6.2), it is a
    // recipient of the encryption by its public certificate only (the addendum of the owner of 15. 11.)
    const bool isSystem = item->source == PDFAppToolCertificateItem::Source::SystemStore;
    if (item->certificateData.isEmpty())
    {
        item->state = isSystem ? State::NoSigningUsage : State::Locked;
    }
    else if ((item->validUntil.isValid() && now > item->validUntil) || (item->validFrom.isValid() && now < item->validFrom))
    {
        item->state = State::Expired;
    }
    else if (usage == Usage::Encryption && !item->isUsableForEncryption)
    {
        // B99: an RSA key with keyEncipherment (an old certificate of the old Editor has not it)
        item->state = State::NoEncryptionUsage;
    }
    else if (usage == Usage::Signing && !item->isUsableForSigning)
    {
        // 5.3, B116
        item->state = State::NoSigningUsage;
    }
    else
    {
        item->state = State::Valid;
    }
}

std::optional<PDFCertificateEntry> PDFAppToolCertificateProvider::findEntry(const QString& filePath) const
{
    if (filePath.isEmpty())
    {
        return std::nullopt;
    }

    // Q6.2: a certificate of the system store by the fingerprint of its identifier
    if (isSystemStorePath(filePath))
    {
        const QString fingerprint = filePath.mid(int(qstrlen(SYSTEM_STORE_PREFIX)));
        if (fingerprint.isEmpty() || !isSystemStoreIncluded())
        {
            return std::nullopt;
        }
        for (PDFCertificateEntry& entry : PDFCertificateManager::getCertificates(PDFCertificateUsageFilter::Any))
        {
            const QByteArray certificateData = entry.info.getCertificateData();
            if (entry.pkcs12.isEmpty() && !certificateData.isEmpty() && PDFDocumentToolEncryption::getCertificateFingerprint(certificateData) == fingerprint)
            {
                return std::move(entry);
            }
        }
        return std::nullopt;
    }

    const QString normalized = QFileInfo(filePath).absoluteFilePath();
    for (PDFCertificateEntry& entry : PDFCertificateManager::getCertificates(PDFCertificateUsageFilter::Any))
    {
        if (!entry.pkcs12.isEmpty() && QFileInfo(entry.pkcs12FilePath).absoluteFilePath() == normalized)
        {
            return std::move(entry);
        }
    }
    return std::nullopt;
}

std::optional<PDFCertificateEntry> PDFAppToolCertificateProvider::unlock(const QString& filePath, const QString& password, QString* errorMessage) const
{
    std::optional<PDFCertificateEntry> entry = findEntry(filePath);
    std::optional<PDFCertificateInfo> info = entry ? PDFCertificateManager::readCertificate(entry->pkcs12, password) : std::nullopt;
    if (!entry || !info)
    {
        // 433:3972: one text for a wrong password and a damaged file (no hint for a guesser)
        if (errorMessage)
        {
            *errorMessage = tr("Invalid certificate or password.");
        }
        return std::nullopt;
    }

    entry->info = std::move(*info);
    storeCertificate(entry->pkcs12FilePath, entry->info.getCertificateData());
    return entry;
}

QString PDFAppToolCertificateProvider::copyToApplicationFolder(const QString& filePath, QString* errorMessage) const
{
    std::optional<PDFCertificateEntry> entry = findEntry(filePath);
    if (!entry)
    {
        if (errorMessage)
        {
            *errorMessage = tr("The certificate file no longer exists.");
        }
        return QString();
    }

    if (!PDFCertificateManager::copyCertificateToUserDirectory(*entry, errorMessage))
    {
        return QString();
    }

    const QString copyPath = QDir(PDFCertificateManager::getCertificateDirectory()).absoluteFilePath(entry->pkcs12fileName);

    // The copy has the same certificate: the cache knows it under the new path too
    if (std::optional<PDFCertificateInfo> info = findCachedCertificate(entry->pkcs12FilePath))
    {
        storeCertificate(copyPath, info->getCertificateData());
    }
    return copyPath;
}

void PDFAppToolCertificateProvider::storeCertificate(const QString& filePath, const QByteArray& certificateData) const
{
    if (!m_context || !m_context->getDatabase())
    {
        return;
    }

    const QString path = PDFRepositoryHelper::normalizePath(filePath);
    const QFileInfo fileInfo(filePath);
    if (path.isEmpty() || !fileInfo.exists())
    {
        return;
    }

    // Only the public certificate: the repository refuses anything else (a PKCS#12, a key)
    PDFCertificateCacheRepository repository(m_context->getDatabase());
    repository.insert(path, certificateData, fileInfo.size(), fileInfo.lastModified(QTimeZone::UTC).toMSecsSinceEpoch());
}

std::optional<PDFCertificateInfo> PDFAppToolCertificateProvider::findCachedCertificate(const QString& filePath) const
{
    if (!m_context || !m_context->getDatabase())
    {
        return std::nullopt;
    }

    const QString path = PDFRepositoryHelper::normalizePath(filePath);
    if (path.isEmpty())
    {
        return std::nullopt;
    }

    PDFCertificateCacheRepository repository(m_context->getDatabase());
    std::optional<PDFCachedCertificate> cached = repository.find(path);
    if (!cached)
    {
        return std::nullopt;
    }

    // A file replaced since the row was written (another size or time of change) may hold another
    // certificate: its details come again after its password
    const QFileInfo fileInfo(filePath);
    const qint64 modified = fileInfo.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
    if (!fileInfo.exists() || cached->fileSize != fileInfo.size() || cached->fileModified != modified)
    {
        return std::nullopt;
    }

    return PDFCertificateInfo::getCertificateInfo(cached->data);
}

bool PDFAppToolCertificateProvider::isSystemStoreIncluded()
{
    return getSystemStoreIncluded().load();
}

void PDFAppToolCertificateProvider::setSystemStoreIncluded(bool isIncluded)
{
    getSystemStoreIncluded().store(isIncluded);
}

std::atomic<bool>& PDFAppToolCertificateProvider::getSystemStoreIncluded()
{
    // Read also by the run of protect.certificates (PDFAppDocumentToolCertificates)
    static std::atomic<bool> isIncluded = true;
    return isIncluded;
}

PDFAppToolCertificateItem PDFAppToolCertificateProvider::createItem(const PDFCertificateEntry& entry, const std::optional<PDFCertificateInfo>& info, const QDateTime& now, Usage usage)
{
    PDFAppToolCertificateItem item;
    item.source = entry.pkcs12.isEmpty() ? PDFAppToolCertificateItem::Source::SystemStore : PDFAppToolCertificateItem::Source::PfxFile;
    item.filePath = entry.pkcs12FilePath;
    item.isInOldFolder = entry.isReadOnlyLocation;
    item.isPasswordProtected = item.source == PDFAppToolCertificateItem::Source::PfxFile && entry.info.getCertificateData().isEmpty();

    const QByteArray certificateData = info ? info->getCertificateData() : QByteArray();
    if (!certificateData.isEmpty())
    {
        item.certificateData = certificateData;
        item.fingerprint = PDFDocumentToolEncryption::getCertificateFingerprint(certificateData);
        item.name = info->getName(PDFCertificateInfo::CommonName);
        item.organization = info->getName(PDFCertificateInfo::OrganizationName);
        item.email = info->getName(PDFCertificateInfo::Email);
        item.issuer = PDFCertificateInfo::getIssuerName(certificateData);
        item.validFrom = info->getNotValidBefore();
        item.validUntil = info->getNotValidAfter();
        item.keyBits = info->getKeySize();
        item.isUsableForEncryption = PDFCertificateManager::isUsableForEncryption(certificateData);
        item.isUsableForSigning = PDFCertificateManager::isUsableForSigning(certificateData, nullptr);
    }
    if (item.name.isEmpty())
    {
        item.name = entry.friendlyName;
    }

    // Q6.2: a certificate of the system store has no file - its identifier is its fingerprint
    if (item.source == PDFAppToolCertificateItem::Source::SystemStore && !item.fingerprint.isEmpty())
    {
        item.filePath = getSystemStorePath(item.fingerprint);
    }

    applyUsage(&item, usage, now);
    return item;
}

QString PDFAppToolCertificateProvider::getDescription(const PDFAppToolCertificateItem& item)
{
    using State = PDFAppToolCertificateItem::State;

    QStringList parts;
    switch (item.state)
    {
        case State::Valid:
        {
            // 433:3198: "Acme Corp · jane.novak@example.com · valid until Sep 12, 2029 · password protected"
            if (!item.organization.isEmpty())
            {
                parts << item.organization;
            }
            if (!item.email.isEmpty())
            {
                parts << item.email;
            }
            if (item.validUntil.isValid())
            {
                parts << tr("valid until %1").arg(PDFAppLocale::formatDate(item.validUntil.toLocalTime().date()));
            }
            if (item.isPasswordProtected)
            {
                parts << tr("password protected");
            }
            break;
        }

        case State::Locked:
            // Q5.11: a file never unlocked
            parts << tr("Details are shown after the password");
            break;

        case State::Expired:
        {
            // 433:3220
            const QDateTime now = PDFAppToolCertificateListHelper::getNow();
            if (item.validFrom.isValid() && now < item.validFrom)
            {
                return tr("Valid from %1 – cannot be used yet").arg(PDFAppLocale::formatDate(item.validFrom.toLocalTime().date()));
            }
            return tr("Expired on %1 – cannot be used").arg(PDFAppLocale::formatDate(item.validUntil.toLocalTime().date()));
        }

        case State::NoEncryptionUsage:
            return tr("Not meant for encryption – cannot be used");

        case State::NoSigningUsage:
            // Step 6 of phase 2 (5.3, B116; the text is P5's, the skeleton names the reason)
            return tr("Not meant for signing – cannot be used");
    }

    // Q5.5: a file of the old Editor or Viewer
    if (item.isInOldFolder)
    {
        parts << tr("from an older PDF4QT application");
    }
    return parts.join(tr(" · ", "Separator of the parts of the description of a certificate"));
}

// ------------------------------------------------------------ PDFAppToolCertificateList

PDFAppToolCertificateList::PDFAppToolCertificateList(QWidget* parent) :
    BaseClass(parent)
{
    using Helper = PDFAppToolCertificateListHelper;

    setObjectName(QStringLiteral("certificateList"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(this, 0);

    // card-certificates 433:3186 with its caption 433:3187
    m_card = new PDFAppToolCard(this);
    m_card->setObjectName(QStringLiteral("certificateCard"));
    m_card->getBodyLayout()->setContentsMargins(Helper::LIST_PADDING, Helper::LIST_PADDING, Helper::LIST_PADDING, Helper::LIST_PADDING);
    m_card->getBodyLayout()->setSpacing(Helper::LIST_SPACING);
    m_caption = new PDFTileRow(tr("Certificate"), QString(), QString(), PDFTileRow::Kind::Caption, m_card);
    m_caption->setObjectName(QStringLiteral("certificateCaption"));
    m_caption->setClickable(false);
    m_card->getBodyLayout()->addWidget(m_caption);
    layout->addWidget(m_card);
}

PDFAppToolCertificateList::~PDFAppToolCertificateList() = default;

void PDFAppToolCertificateList::setCertificates(std::vector<PDFAppToolCertificateItem> certificates)
{
    m_certificates = std::move(certificates);
    applyUsage();
    rebuildRows();
}

void PDFAppToolCertificateList::setUsage(Usage usage)
{
    if (m_usage == usage)
    {
        return;
    }

    m_usage = usage;
    applyUsage();
    rebuildRows();
}

void PDFAppToolCertificateList::applyUsage()
{
    // The states of the rows of the usage (a row of the provider may be made for another one); a
    // row without its certificate keeps its state (Locked - nothing is known of it)
    const QDateTime now = PDFAppToolCertificateListHelper::getNow();
    for (PDFAppToolCertificateItem& item : m_certificates)
    {
        if (!item.certificateData.isEmpty())
        {
            PDFAppToolCertificateProvider::applyUsage(&item, m_usage, now);
        }
    }
    PDFAppToolCertificateListHelper::sortChoosableFirst(&m_certificates);

    const PDFAppToolCertificateItem* chosen = getChosenItem();
    if (!chosen || !chosen->isChoosable())
    {
        m_chosenFilePath.clear();
    }
}

void PDFAppToolCertificateList::choose(const QString& filePath)
{
    if (!PDFAppToolCertificateListHelper::findItem(m_certificates, filePath, true))
    {
        return;
    }

    m_chosenFilePath = filePath;
    updateRows();
}

QString PDFAppToolCertificateList::getChosenFilePath() const
{
    return m_chosenFilePath;
}

QString PDFAppToolCertificateList::getChosenFingerprint() const
{
    const PDFAppToolCertificateItem* item = getChosenItem();
    return item ? item->fingerprint : QString();
}

const PDFAppToolCertificateItem* PDFAppToolCertificateList::getChosenItem() const
{
    return PDFAppToolCertificateListHelper::findItem(m_certificates, m_chosenFilePath, false);
}

PDFTileRow* PDFAppToolCertificateList::getRow(size_t index) const
{
    return index < m_rows.size() ? m_rows[index].data() : nullptr;
}

void PDFAppToolCertificateList::setCaption(const QString& caption)
{
    if (m_caption)
    {
        m_caption->setTitle(caption);
    }
}

void PDFAppToolCertificateList::rebuildRows()
{
    using Helper = PDFAppToolCertificateListHelper;
    using State = PDFAppToolCertificateItem::State;

    // The old rows are retired from the event loop (step A: a rebuilt row): one of them may be the sender of the
    // click, which leads here
    for (const QPointer<PDFTileRow>& row : m_rows)
    {
        if (row)
        {
            PDFDeferredDeletion::retireLater(row.data());
        }
    }
    m_rows.clear();

    if (!m_card)
    {
        return;
    }

    for (size_t i = 0; i < m_certificates.size(); ++i)
    {
        const PDFAppToolCertificateItem& item = m_certificates[i];
        const bool isPfx = item.source == PDFAppToolCertificateItem::Source::PfxFile;

        // item-Jane Novak 433:3192: the tile pfx-file / monitor, the name, the tag, the description
        PDFTileRow* row = new PDFTileRow(item.getTitle(), PDFAppToolCertificateProvider::getDescription(item), isPfx ? QStringLiteral("pfx-file") : QStringLiteral("monitor"),
                                         PDFTileRow::Kind::ListItem, m_card);
        row->setObjectName(QStringLiteral("certificateRow-%1").arg(i));
        row->setProperty(Helper::ROW_INDEX_PROPERTY, qint64(i));
        row->setTitleKind(PDFTileRow::TextKind::Data);
        row->setTag(isPfx ? tr(".pfx file") : tr("System store"));
        if (item.state == State::Expired || item.state == State::NoEncryptionUsage || item.state == State::NoSigningUsage)
        {
            // 433:3220: the reason in accent/red/icon
            row->setDescriptionColor(PDFThemeColors::AccentRedIcon);
        }

        if (item.isChoosable())
        {
            row->setClickable(true);
            row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
            connect(row, &PDFTileRow::clicked, this, &PDFAppToolCertificateList::onRowClicked);
        }
        else
        {
            // A row, which cannot be chosen, is a text with its reason (no focus, no hover)
            row->setClickable(false);
        }
        m_card->getBodyLayout()->addWidget(row);
        m_rows.emplace_back(row);
    }
    updateRows();
}

void PDFAppToolCertificateList::updateRows()
{
    for (size_t i = 0; i < m_rows.size() && i < m_certificates.size(); ++i)
    {
        PDFTileRow* row = m_rows[i].data();
        if (!row)
        {
            continue;
        }

        // 433:3192: the chosen row bg/accent-subtle, its tile Accent and ✓; the other rows Muted
        const PDFAppToolCertificateItem& item = m_certificates[i];
        const bool isChosen = item.isChoosable() && !m_chosenFilePath.isEmpty() && item.filePath == m_chosenFilePath;
        if (row->isCheckable())
        {
            row->setChecked(isChosen);
        }
        row->setTone(isChosen ? PDFTileRow::Tone::Accent : PDFTileRow::Tone::Muted);
        row->setEndIcon(isChosen ? QStringLiteral("check") : QString(), PDFThemeColors::IconAccent);
    }
}

void PDFAppToolCertificateList::onRowClicked()
{
    PDFTileRow* row = qobject_cast<PDFTileRow*>(sender());
    bool isValid = false;
    const qint64 index = row ? row->property(PDFAppToolCertificateListHelper::ROW_INDEX_PROPERTY).toLongLong(&isValid) : -1;
    if (!isValid || index < 0 || size_t(index) >= m_certificates.size())
    {
        return;
    }

    const PDFAppToolCertificateItem& item = m_certificates[size_t(index)];
    if (!item.isChoosable())
    {
        return;
    }

    if (item.filePath == m_chosenFilePath)
    {
        // The chosen row stays chosen (Exclusive keeps it checked)
        updateRows();
        return;
    }

    m_chosenFilePath = item.filePath;
    const QString fingerprint = item.fingerprint;
    updateRows();
    Q_EMIT certificateChosen(fingerprint);
}

// ------------------------------------------------------------ PDFAppToolCertificatePresetPart

PDFAppToolCertificatePresetPart::PDFAppToolCertificatePresetPart(PDFAppContext* context, QObject* parent) :
    BaseClass(parent),
    m_provider(context),
    m_context(context)
{

}

PDFAppToolCertificatePresetPart::~PDFAppToolCertificatePresetPart()
{
    // S5.1: the password goes with the tool; the card reports nothing into the dying part
    if (m_passwordCard)
    {
        disconnect(m_passwordCard.data(), nullptr, this, nullptr);
    }
    clearSecrets();

    // The dialogs belong to the tool: they close with it (a form with a password or a question
    // about a file of a tool, which is gone; they delete themselves later). They report nothing
    // more: their signals to this part are disconnected first.
    if (m_createDialog)
    {
        disconnect(m_createDialog.data(), nullptr, this, nullptr);
        m_createDialog->close();
    }
    if (m_importDialog)
    {
        disconnect(m_importDialog.data(), nullptr, this, nullptr);
        m_importDialog->close();
    }
}

QWidget* PDFAppToolCertificatePresetPart::createCanvasContent(QWidget* parent)
{
    using Helper = PDFAppToolCertificateListHelper;

    // A second call replaces the parts: the old content belongs to its parent, which deletes it
    clearSecrets();

    QWidget* content = new QWidget(parent);
    content->setObjectName(QStringLiteral("certificateContent"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(content, Helper::FORM_GAP);
    m_content = content;

    // heading 433:3183: Semi Bold 18 in lines of 22, the text Regular 12 text/secondary, 2 apart
    QWidget* heading = new QWidget(content);
    heading->setObjectName(QStringLiteral("certificateHeading"));
    QVBoxLayout* headingLayout = PDFAppPageWidgets::createColumn(heading, Helper::HEADING_GAP);
    headingLayout->addWidget(Helper::createLabel(tr("Encrypt for your digital ID"), QFont::DemiBold, 18, 22, PDFThemeColors::TextPrimary, heading));
    headingLayout->addWidget(PDFAppPageWidgets::createText(tr("Only someone who has this certificate with its private key – the certificate file and its password – can open the document."),
                                                           PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, heading));
    layout->addWidget(heading);

    // card-certificates 433:3186
    m_list = new PDFAppToolCertificateList(content);
    connect(m_list.data(), &PDFAppToolCertificateList::certificateChosen, this, &PDFAppToolCertificatePresetPart::onCertificateChosen);
    layout->addWidget(m_list);

    // empty-state 433:3422; portable mode (owner 2026-11-15): no certificate is created or
    // imported, the text says why
    const bool isCreationAvailable = this->isCreationAvailable();
    const QString emptyTextValue = isCreationAvailable ? tr("Create a self-signed certificate or import a .pfx file. Both are kept in your certificate store and can be used for signing as well.")
                                                       : tr("In portable mode no certificate can be created or imported – its private key would stay on this computer. A .pfx file, which is in your certificate folder already, can be used.");
    QPushButton* emptyCreate = nullptr;
    QPushButton* emptyImport = nullptr;
    PDFAppToolCard* empty = createEmptyState(tr("No certificate with a private key yet"), emptyTextValue, isCreationAvailable, content, &emptyCreate, &emptyImport);
    connect(emptyCreate, &QPushButton::clicked, this, &PDFAppToolCertificatePresetPart::openCreateDialog);
    connect(emptyImport, &QPushButton::clicked, this, &PDFAppToolCertificatePresetPart::openImportDialog);
    layout->addWidget(empty);
    m_emptyState = empty;

    // Q5.5: the copy of a .pfx file of the folder of an old application (no frame; the blue
    // callout of the canvas of Remove Hidden Data with a Secondary button)
    m_copyCallout = new PDFAppToolCallout(PDFAccentHue::Blue, QStringLiteral("info"), QString(), QString(), content);
    m_copyCallout->setObjectName(QStringLiteral("certificateCopyCallout"));
    m_copyCallout->setCompactStyle();
    m_copyButton = PDFAppPageWidgets::createButton(tr("Copy to My Certificates"), QStringLiteral("copy"), false, m_copyCallout);
    m_copyButton->setObjectName(QStringLiteral("certificateCopyButton"));
    m_copyCallout->getBodyLayout()->addWidget(m_copyButton, 0, Qt::AlignLeading);
    connect(m_copyButton.data(), &QPushButton::clicked, this, &PDFAppToolCertificatePresetPart::onCopyClicked);
    layout->addWidget(m_copyCallout);

    // card-certificate-password 433:3221: the title Semi Bold 13, the field 433:3236 (the label
    // Medium 12 text/secondary, the bare field of P2 with its hint or error, 4 apart)
    PDFAppToolCard* passwordFrame = new PDFAppToolCard(content);
    passwordFrame->setObjectName(QStringLiteral("certificatePasswordFrame"));
    passwordFrame->getBodyLayout()->setContentsMargins(Helper::PASSWORD_PADDING, Helper::PASSWORD_PADDING, Helper::PASSWORD_PADDING, Helper::PASSWORD_PADDING);
    passwordFrame->getBodyLayout()->setSpacing(Helper::PASSWORD_GAP);
    passwordFrame->getBodyLayout()->addWidget(Helper::createLabel(tr("Certificate password"), QFont::DemiBold, 13, 16, PDFThemeColors::TextPrimary, passwordFrame));
    QWidget* field = new QWidget(passwordFrame);
    field->setObjectName(QStringLiteral("certificatePasswordField"));
    QVBoxLayout* fieldLayout = PDFAppPageWidgets::createColumn(field, Helper::FIELD_GAP);
    m_passwordLabel = Helper::createLabel(QString(), QFont::Medium, 12, 15, PDFThemeColors::TextSecondary, field);
    m_passwordLabel->setObjectName(QStringLiteral("certificatePasswordLabel"));
    fieldLayout->addWidget(m_passwordLabel);
    m_passwordCard = new PDFAppToolPasswordCard(PDFAppToolPasswordCard::Kind::Certificate, field);
    m_passwordCard->setObjectName(QStringLiteral("certificatePasswordCard"));
    m_passwordCard->setAlgorithm(PDFSecurityHandlerFactory::Certificate);
    connect(m_passwordCard.data(), &PDFAppToolPasswordCard::passwordChanged, this, &PDFAppToolCertificatePresetPart::onPasswordChanged);
    fieldLayout->addWidget(m_passwordCard);
    passwordFrame->getBodyLayout()->addWidget(field);
    m_passwordField = field;
    m_noPasswordText = PDFAppPageWidgets::createText(tr("Not needed – the certificate file has no password."), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, passwordFrame);
    m_noPasswordText->setObjectName(QStringLiteral("certificateNoPassword"));
    passwordFrame->getBodyLayout()->addWidget(m_noPasswordText);
    layout->addWidget(passwordFrame);
    m_passwordFrame = passwordFrame;

    // R5-21 (no frame, the amber callout of 430:1776): restrictions lock the holder out of a change
    m_restrictionCallout = new PDFAppToolCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), tr("You will not be able to change or remove this protection yourself"),
                                                 tr("The restrictions apply to the holder of the certificate too. Keep everything allowed to be able to change or remove the encryption later."), content);
    m_restrictionCallout->setObjectName(QStringLiteral("certificateRestrictionCallout"));
    m_restrictionCallout->setCompactStyle();
    layout->addWidget(m_restrictionCallout);

    reload(QString());
    updateParts();
    return content;
}

void PDFAppToolCertificatePresetPart::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    // 433:3069: Encrypt (of the GUI), a divider, Create Certificate…, Import…
    builder.addActionDivider();
    m_createButton = builder.addAction(tr("Create Certificate…"), QStringLiteral("certificate-add"), this, &PDFAppToolCertificatePresetPart::openCreateDialog);
    m_createButton->setObjectName(QStringLiteral("certificateCreateButton"));
    m_importButton = builder.addAction(tr("Import…"), QStringLiteral("import"), this, &PDFAppToolCertificatePresetPart::openImportDialog);
    m_importButton->setObjectName(QStringLiteral("certificateImportButton"));

    // Portable mode (owner 2026-11-15): disabled with the reason
    const bool isCreationAvailable = this->isCreationAvailable();
    for (QToolButton* button : { m_createButton.data(), m_importButton.data() })
    {
        button->setEnabled(isCreationAvailable);
        button->setToolTip(isCreationAvailable ? QString() : getPortableText());
    }
}

bool PDFAppToolCertificatePresetPart::isCreationAvailable() const
{
    return !m_context || !m_context->isPortable() || getCreationInPortableModeAllowed();
}

void PDFAppToolCertificatePresetPart::setCreationInPortableModeAllowed(bool isAllowed)
{
    getCreationInPortableModeAllowed() = isAllowed;
}

bool& PDFAppToolCertificatePresetPart::getCreationInPortableModeAllowed()
{
    static bool isAllowed = false;
    return isAllowed;
}

QString PDFAppToolCertificatePresetPart::getPortableText()
{
    return tr("Not available in portable mode – the private key would stay on this computer");
}

PDFAppToolCard* PDFAppToolCertificatePresetPart::createEmptyState(const QString& title, const QString& text, bool isCreationAvailable, QWidget* parent, QPushButton** createButton, QPushButton** importButton)
{
    using Helper = PDFAppToolCertificateListHelper;

    // empty-state 433:3422, 436:9646: the tile, the title Semi Bold 15, the text Regular 12
    // centered, the buttons
    PDFAppToolCard* empty = new PDFAppToolCard(parent);
    empty->setObjectName(QStringLiteral("certificateEmptyState"));
    empty->getBodyLayout()->setContentsMargins(Helper::EMPTY_PADDING, Helper::EMPTY_PADDING, Helper::EMPTY_PADDING, Helper::EMPTY_PADDING);
    empty->getBodyLayout()->setSpacing(Helper::EMPTY_GAP);
    PDFIconBadge* tile = new PDFIconBadge(QStringLiteral("certificate"), Helper::EMPTY_TILE, Helper::EMPTY_ICON, empty);
    tile->setColors(PDFThemeColors::BgAccentSubtle, PDFThemeColors::IconAccent);
    tile->setRadius(Helper::EMPTY_TILE_RADIUS);
    empty->getBodyLayout()->addWidget(tile, 0, Qt::AlignHCenter);
    PDFLabel* titleLabel = Helper::createLabel(title, QFont::DemiBold, 15, 18, PDFThemeColors::TextPrimary, empty);
    titleLabel->setObjectName(QStringLiteral("certificateEmptyTitle"));
    empty->getBodyLayout()->addWidget(titleLabel, 0, Qt::AlignHCenter);
    PDFAppParagraph* emptyText = PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, empty);
    emptyText->setObjectName(QStringLiteral("certificateEmptyText"));
    emptyText->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    emptyText->setMinimumWidth(1);
    empty->getBodyLayout()->addWidget(emptyText);
    QWidget* buttons = new QWidget(empty);
    QHBoxLayout* buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    buttonsLayout->setSpacing(Helper::EMPTY_BUTTON_GAP);
    QPushButton* emptyCreate = PDFAppPageWidgets::createButton(tr("Create Certificate…"), QStringLiteral("certificate-add"), true, buttons);
    emptyCreate->setObjectName(QStringLiteral("certificateEmptyCreate"));
    QPushButton* emptyImport = PDFAppPageWidgets::createButton(tr("Import .pfx…"), QStringLiteral("import"), false, buttons);
    emptyImport->setObjectName(QStringLiteral("certificateEmptyImport"));
    for (QPushButton* button : { emptyCreate, emptyImport })
    {
        button->setEnabled(isCreationAvailable);
        button->setToolTip(isCreationAvailable ? QString() : getPortableText());
    }
    buttonsLayout->addWidget(emptyCreate);
    buttonsLayout->addWidget(emptyImport);
    empty->getBodyLayout()->addWidget(buttons, 0, Qt::AlignHCenter);

    if (createButton)
    {
        *createButton = emptyCreate;
    }
    if (importButton)
    {
        *importButton = emptyImport;
    }
    return empty;
}

bool PDFAppToolCertificatePresetPart::isConfirmEnabled() const
{
    const PDFAppToolCertificateItem* item = m_list ? m_list->getChosenItem() : nullptr;
    if (!item || !item->isChoosable() || !m_unlockError.isEmpty())
    {
        return false;
    }
    return !item->isPasswordProtected || (m_passwordCard && m_passwordCard->isComplete());
}

bool PDFAppToolCertificatePresetPart::prepareRun(QVariantMap* parameters, std::vector<PDFCertificateEntry>* certificates, QString* errorMessage)
{
    const PDFAppToolCertificateItem* chosen = m_list ? m_list->getChosenItem() : nullptr;
    if (!chosen || !chosen->isChoosable() || !parameters || !certificates)
    {
        if (errorMessage)
        {
            *errorMessage = tr("Choose a certificate, which can encrypt the document.");
        }
        return false;
    }

    // S5.1: the password is used once here and in the run; the local copy is cleared. A row of the
    // system store (the addendum of the owner of 15. 11.): its public certificate is the recipient, no
    // password, its key is never used
    const QString filePath = chosen->filePath;
    const bool isSystemStore = chosen->source == PDFAppToolCertificateItem::Source::SystemStore;
    QString password = (!isSystemStore && chosen->isPasswordProtected && m_passwordCard) ? m_passwordCard->getPassword() : QString();
    QString error;
    std::optional<PDFCertificateEntry> entry;
    if (isSystemStore)
    {
        entry = m_provider.findEntry(filePath);
        if (!entry)
        {
            error = tr("The certificate is no longer in the system store.");
        }
    }
    else
    {
        entry = m_provider.unlock(filePath, password, &error);
    }

    std::optional<PDFAppToolCertificateItem> unlocked;
    if (entry)
    {
        unlocked = PDFAppToolCertificateProvider::createItem(*entry, entry->info, PDFAppToolCertificateListHelper::getNow());
        if (unlocked->state == PDFAppToolCertificateItem::State::Expired)
        {
            error = tr("The certificate has expired – it cannot be used.");
        }
        else if (unlocked->state == PDFAppToolCertificateItem::State::NoEncryptionUsage)
        {
            error = tr("The certificate is not meant for encryption – it cannot be used.");
        }
    }

    if (!entry || !error.isEmpty())
    {
        PDFSecureMemory::clear(password);
        m_unlockError = error;
        if (m_passwordCard)
        {
            m_passwordCard->setErrorText(error);
        }
        if (errorMessage)
        {
            *errorMessage = error;
        }

        // The details of an unlocked file (an expired certificate) are in the cache now
        reload(filePath);
        updateParts();
        notifyStateChanged();
        return false;
    }

    parameters->insert(QString::fromLatin1(PDFDocumentToolEncryption::PARAMETER_CERTIFICATE), unlocked->fingerprint);
    if (!isSystemStore)
    {
        parameters->insert(QString::fromLatin1(PDFDocumentToolEncryption::PARAMETER_CERTIFICATE_PASSWORD), password);
    }
    certificates->push_back(std::move(*entry));
    PDFSecureMemory::clear(password);

    // The row shows its details now (Q5.11: the cache has the certificate)
    reload(filePath);
    updateParts();
    notifyStateChanged();
    return true;
}

void PDFAppToolCertificatePresetPart::clearSecrets()
{
    if (m_passwordCard)
    {
        m_passwordCard->clear();
        m_passwordCard->setErrorText(QString());
    }
    m_unlockError.clear();
    updateParts();
}

PDFAppToolPasswordCard* PDFAppToolCertificatePresetPart::getPasswordCard() const
{
    return m_passwordCard.data();
}

PDFLabel* PDFAppToolCertificatePresetPart::getPasswordLabel() const
{
    return m_passwordLabel.data();
}

PDFAppToolCallout* PDFAppToolCertificatePresetPart::getCopyCallout() const
{
    return m_copyCallout.data();
}

PDFAppToolCallout* PDFAppToolCertificatePresetPart::getRestrictionCallout() const
{
    return m_restrictionCallout.data();
}

QToolButton* PDFAppToolCertificatePresetPart::getCreateButton() const
{
    return m_createButton.data();
}

QToolButton* PDFAppToolCertificatePresetPart::getImportButton() const
{
    return m_importButton.data();
}

PDFAppCertificateCreateDialog* PDFAppToolCertificatePresetPart::getCreateDialog() const
{
    return m_createDialog.data();
}

PDFAppCertificateImportDialog* PDFAppToolCertificatePresetPart::getImportDialog() const
{
    return m_importDialog.data();
}

bool PDFAppToolCertificatePresetPart::hasCertificates() const
{
    return !m_certificates.empty();
}

std::optional<PDFAppToolCertificateItem> PDFAppToolCertificatePresetPart::getChosenCertificate() const
{
    const PDFAppToolCertificateItem* item = m_list ? m_list->getChosenItem() : nullptr;
    if (!item)
    {
        return std::nullopt;
    }
    return *item;
}

QString PDFAppToolCertificatePresetPart::getStatusText() const
{
    if (!hasCertificates())
    {
        // 433:3282; portable mode creates and imports nothing (owner 2026-11-15)
        return isCreationAvailable() ? tr("Create or import a certificate with a private key first")
                                     : tr("No certificate with a private key – none can be created or imported in portable mode");
    }

    const PDFAppToolCertificateItem* item = m_list ? m_list->getChosenItem() : nullptr;
    if (!item)
    {
        return tr("Choose a certificate, which can encrypt the document");
    }
    if (!m_unlockError.isEmpty())
    {
        // 433:3972 (the text of the field without its full stop)
        QString text = m_unlockError;
        if (text.endsWith(QLatin1Char('.')))
        {
            text.chop(1);
        }
        return text;
    }
    if (item->isPasswordProtected && (!m_passwordCard || !m_passwordCard->isComplete()))
    {
        // 433:3742
        return tr("Enter the password of the certificate file");
    }
    // 433:3049, 433:4208
    return tr("Only the holder of the private key of “%1” will be able to open the document").arg(item->getTitle());
}

void PDFAppToolCertificatePresetPart::setPermissionsRestricted(bool isRestricted)
{
    if (m_isRestricted != isRestricted)
    {
        m_isRestricted = isRestricted;
        updateParts();
    }
}

void PDFAppToolCertificatePresetPart::refresh()
{
    // The GUI shows the new list too (Opens for, the card of the permissions, the status): the
    // signal comes from the event loop, the caller may go on with the part
    reload(QString());
    updateParts();
    notifyStateChanged();
}

void PDFAppToolCertificatePresetPart::choose(const QString& filePath)
{
    if (!m_list)
    {
        return;
    }

    const QString previous = m_list->getChosenFilePath();
    m_list->choose(filePath);
    if (m_list->getChosenFilePath() != previous)
    {
        onCertificateChosen();
    }
}

void PDFAppToolCertificatePresetPart::openCreateDialog()
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
    PDFAppCertificateCreateDialog* dialog = new PDFAppCertificateCreateDialog(m_context, getDialogParent());
    // A dialog of the window owned by its creator (step A): retire closes it unanswered
    PDFDeferredDeletion::setOwner(dialog, this);
    m_createDialog = dialog;
    connect(dialog, &PDFAppCertificateCreateDialog::certificateCreated, this, &PDFAppToolCertificatePresetPart::onCertificateCreated);
    dialog->open();
}

void PDFAppToolCertificatePresetPart::openImportDialog()
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

    // Plan 7.2: the file comes only from the file dialog of the user. A0: the file dialog answers onImportFileChosen
    // (the request is a child of the part: a part gone meanwhile gets nothing); one file dialog at a time.
    if (m_fileRequest && m_fileRequest->isPending())
    {
        return;
    }
    m_fileRequest = PDFAppPanelFileDialogs::requestOpenFileName(getDialogParent(), tr("Import Certificate"), QString(), PDFAppCertificateImportDialog::getFileFilter(), this,
                                                                &PDFAppToolCertificatePresetPart::onImportFileChosen);
}

void PDFAppToolCertificatePresetPart::onImportFileChosen(const QString& filePath)
{
    // The state again: the import still available, no import dialog opened meanwhile
    if (filePath.isEmpty() || !isCreationAvailable() || (m_importDialog && m_importDialog->isVisible()))
    {
        return;
    }

    PDFAppCertificateImportDialog* dialog = new PDFAppCertificateImportDialog(m_context, filePath, getDialogParent());
    // A dialog of the window owned by its creator (step A): retire closes it unanswered
    PDFDeferredDeletion::setOwner(dialog, this);
    m_importDialog = dialog;
    connect(dialog, &PDFAppCertificateImportDialog::certificateImported, this, &PDFAppToolCertificatePresetPart::onCertificateImported);

    // The last call: the import may report its file (and close the dialog) at once
    dialog->start();
}

void PDFAppToolCertificatePresetPart::updateParts()
{
    const bool hasRows = hasCertificates();
    const PDFAppToolCertificateItem* item = m_list ? m_list->getChosenItem() : nullptr;

    if (m_list)
    {
        m_list->setVisible(hasRows);
    }
    if (m_emptyState)
    {
        m_emptyState->setVisible(!hasRows);
    }

    if (m_copyCallout)
    {
        // Q5.5: the copy is offered, when a file of an old folder is used
        m_copyCallout->setVisible(item && item->isInOldFolder);
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

    if (m_passwordFrame)
    {
        m_passwordFrame->setVisible(hasRows);
        m_passwordFrame->setEnabled(item != nullptr);
    }

    // A row of the system store needs no password (its public certificate is the recipient)
    const bool isSystemStore = item && item->source == PDFAppToolCertificateItem::Source::SystemStore;
    const bool isPasswordNeeded = !item || (!isSystemStore && item->isPasswordProtected);
    if (m_passwordField)
    {
        m_passwordField->setVisible(isPasswordNeeded);
    }
    if (m_noPasswordText)
    {
        m_noPasswordText->setVisible(!isPasswordNeeded);
        m_noPasswordText->setText(isSystemStore ? tr("Not needed – the document is encrypted for the public certificate of the system store.")
                                                : tr("Not needed – the certificate file has no password."));
    }
    if (m_passwordLabel)
    {
        // 433:3237 "Password of jane_novak.pfx"
        m_passwordLabel->setText(item ? tr("Password of %1").arg(PDFAppToolCertificateListHelper::getFileName(item->filePath)) : tr("Password of the certificate file"));
    }

    if (m_restrictionCallout)
    {
        m_restrictionCallout->setVisible(hasRows && m_isRestricted);
    }
}

QWidget* PDFAppToolCertificatePresetPart::getDialogParent() const
{
    return m_content ? m_content->window() : nullptr;
}

void PDFAppToolCertificatePresetPart::notifyStateChanged()
{
    // prepareRun is called by the GUI in the middle of its run: the signal comes from the event
    // loop, not into the code of the caller
    if (!m_isNotifyPending)
    {
        m_isNotifyPending = true;
        QMetaObject::invokeMethod(this, &PDFAppToolCertificatePresetPart::onNotifyStateChanged, Qt::QueuedConnection);
    }
}

void PDFAppToolCertificatePresetPart::onNotifyStateChanged()
{
    m_isNotifyPending = false;
    Q_EMIT stateChanged();
}

void PDFAppToolCertificatePresetPart::onCertificateChosen()
{
    // A password typed for another file is not kept (S5.1)
    if (m_passwordCard)
    {
        m_passwordCard->clear();
        m_passwordCard->setErrorText(QString());
    }
    m_unlockError.clear();
    m_copyError.clear();
    updateParts();
    Q_EMIT stateChanged();
}

void PDFAppToolCertificatePresetPart::onPasswordChanged()
{
    // 433:3972: the error stays until the password is changed
    if (!m_unlockError.isEmpty())
    {
        m_unlockError.clear();
        if (m_passwordCard)
        {
            m_passwordCard->setErrorText(QString());
        }
    }
    updateParts();
    Q_EMIT stateChanged();
}

void PDFAppToolCertificatePresetPart::onCertificateCreated(const QString& fileName)
{
    // 433:3282 documentation note: the new certificate comes back chosen
    reload(fileName);
    onCertificateChosen();
}

void PDFAppToolCertificatePresetPart::onCertificateImported(const QString& fileName)
{
    reload(fileName);
    onCertificateChosen();
}

void PDFAppToolCertificatePresetPart::onCopyClicked()
{
    const PDFAppToolCertificateItem* item = m_list ? m_list->getChosenItem() : nullptr;
    if (!item || !item->isInOldFolder)
    {
        return;
    }

    QString error;
    const QString copyPath = m_provider.copyToApplicationFolder(item->filePath, &error);
    if (copyPath.isEmpty())
    {
        m_copyError = error.isEmpty() ? tr("The certificate file could not be written into your certificate folder.") : error;
        updateParts();
        Q_EMIT stateChanged();
        return;
    }

    m_copyError.clear();
    reload(copyPath);
    updateParts();
    Q_EMIT stateChanged();
}

void PDFAppToolCertificatePresetPart::reload(const QString& filePath)
{
    QString choice = filePath;
    if (choice.isEmpty() && m_list)
    {
        choice = m_list->getChosenFilePath();
    }

    using Helper = PDFAppToolCertificateListHelper;

    m_certificates = m_provider.getCertificates();

    // Plan 4.1: the last used certificate (the most recently used row of the cache), else the
    // first one, which can be chosen
    if (!Helper::findItem(m_certificates, choice, true) && m_context && m_context->getDatabase())
    {
        choice.clear();
        std::map<QString, QString> normalizedPaths;
        for (const PDFAppToolCertificateItem& item : m_certificates)
        {
            if (item.isChoosable())
            {
                normalizedPaths[PDFRepositoryHelper::normalizePath(item.filePath)] = item.filePath;
            }
        }

        PDFCertificateCacheRepository repository(m_context->getDatabase());
        for (const PDFCachedCertificate& cached : repository.getCertificates())
        {
            auto it = normalizedPaths.find(cached.path);
            if (it != normalizedPaths.end())
            {
                choice = it->second;
                break;
            }
        }
    }
    if (!Helper::findItem(m_certificates, choice, true))
    {
        const PDFAppToolCertificateItem* first = Helper::findFirstChoosable(m_certificates);
        choice = first ? first->filePath : QString();
    }

    if (m_list)
    {
        m_list->setCertificates(m_certificates);
        m_list->choose(choice);
    }
}

}   // namespace pdf

#include "moc_pdfapptoolcertificatelist.cpp"
