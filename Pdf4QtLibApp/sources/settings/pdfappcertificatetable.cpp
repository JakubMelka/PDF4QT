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

#include "pdfappcertificatetable.h"
#include "pdfappcontext.h"
#include "pdfapphomedialogs.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingrow.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdftrustedcertificaterepository.h"

#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>

namespace pdf
{

/// Helpers of the table of the trusted certificates
class PDFAppCertificateTableHelper
{
public:
    PDFAppCertificateTableHelper() = delete;

    static constexpr const char* TRANSLATION_CONTEXT = "pdf::PDFAppCertificateTable";

    /// Margins of the viewport of PDFTreeView (the rounded rows do not touch its edges)
    static constexpr int VIEW_MARGIN = 6;

    /// Default height of a row of the row style Certificate
    static constexpr int ROW_HEIGHT = 34;

    static QString translate(const char* text)
    {
        return QCoreApplication::translate(TRANSLATION_CONTEXT, text);
    }

    /// System certificates sorted by their names (the same names by the data)
    static bool isLess(const PDFAppCertificateEntry& left, const PDFAppCertificateEntry& right)
    {
        const int comparison = left.name.compare(right.name, Qt::CaseInsensitive);
        if (comparison != 0)
        {
            return comparison < 0;
        }
        return left.data < right.data;
    }

    /// Date of the validity of a certificate (O17: the language of the interface)
    static QString formatDate(const QDateTime& dateTime)
    {
        return dateTime.isValid() ? PDFAppLocale::formatDate(dateTime.toLocalTime().date()) : QString();
    }

    static bool isSystem(const PDFAppCertificateEntry& entry)
    {
        return entry.type == PDFAppCertificateEntry::Type::System;
    }

    static PDFCertificateEntries getSystemCertificates()
    {
        return PDFCertificateStore::getSystemCertificates();
    }
};

PDFAppSystemCertificatesTask::Provider PDFAppCertificateTable::s_provider = nullptr;

QString PDFAppCertificateEntry::sanitizeText(const QString& text)
{
    // A name of a certificate is untrusted text: no control, format (bidirectional
    // overrides, joiners) or unpaired surrogate characters, one line
    QString result;
    result.reserve(qMin<qsizetype>(text.size(), MAXIMUM_TEXT_LENGTH * 2));
    for (const char32_t codePoint : text.toUcs4())
    {
        switch (QChar::category(codePoint))
        {
            case QChar::Other_Control:
            case QChar::Other_Format:
            case QChar::Other_Surrogate:
            case QChar::Other_NotAssigned:
            case QChar::Separator_Line:
            case QChar::Separator_Paragraph:
                result += QLatin1Char(' ');
                break;

            default:
                result += QString::fromUcs4(&codePoint, 1);
                break;
        }

        if (result.size() > MAXIMUM_TEXT_LENGTH * 2)
        {
            break;
        }
    }

    result = result.simplified();
    if (result.size() > MAXIMUM_TEXT_LENGTH)
    {
        // A pair of surrogates is never cut
        qsizetype length = MAXIMUM_TEXT_LENGTH;
        if (result.at(length - 1).isHighSurrogate())
        {
            --length;
        }
        result.truncate(length);
    }
    return result;
}

QString PDFAppCertificateEntry::getFingerprint(const QByteArray& data)
{
    if (data.isEmpty())
    {
        return PDFAppCertificateTableHelper::translate("Unknown");
    }

    const QByteArray hex = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toUpper();
    QStringList groups;
    for (qsizetype i = 0; i < hex.size(); i += 4)
    {
        groups << QString::fromLatin1(hex.mid(i, 4));
    }
    return groups.join(QLatin1Char(' '));
}

PDFAppCertificateEntry PDFAppCertificateEntry::create(Type type, qint64 id, const QByteArray& data)
{
    std::optional<PDFCertificateInfo> info;
    if (PDFTrustedCertificateRepository::isValidCertificate(data))
    {
        info = PDFCertificateInfo::getCertificateInfo(data);
    }

    PDFAppCertificateEntry entry;
    if (info)
    {
        entry = create(type, *info);
    }
    else
    {
        // A damaged row is still listed, so it can be removed
        entry.type = type;
        entry.isDamaged = true;
        entry.name = PDFAppCertificateTableHelper::translate("Damaged certificate");
    }

    entry.id = id;
    entry.data = data;
    return entry;
}

PDFAppCertificateEntry PDFAppCertificateEntry::create(Type type, const PDFCertificateInfo& info)
{
    PDFAppCertificateEntry entry;
    entry.type = type;
    entry.data = info.getCertificateData();
    for (PDFCertificateInfo::NameEntry nameEntry : { PDFCertificateInfo::CommonName, PDFCertificateInfo::OrganizationName, PDFCertificateInfo::Email })
    {
        entry.name = sanitizeText(info.getName(nameEntry));
        if (!entry.name.isEmpty())
        {
            break;
        }
    }
    if (entry.name.isEmpty())
    {
        entry.name = PDFAppCertificateTableHelper::translate("Unknown");
    }

    entry.organization = sanitizeText(info.getName(PDFCertificateInfo::OrganizationName));
    entry.issuer = sanitizeText(PDFCertificateInfo::getIssuerName(entry.data));
    entry.validFrom = info.getNotValidBefore();
    entry.validTo = info.getNotValidAfter();
    return entry;
}

PDFAppCertificateTableModel::PDFAppCertificateTableModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppCertificateTableModel::~PDFAppCertificateTableModel() = default;

int PDFAppCertificateTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

int PDFAppCertificateTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QString PDFAppCertificateTableModel::getTypeText(PDFAppCertificateEntry::Type type)
{
    return type == PDFAppCertificateEntry::Type::User ? tr("User") : tr("System");
}

QVariant PDFAppCertificateTableModel::data(const QModelIndex& index, int role) const
{
    const PDFAppCertificateEntry* entry = index.isValid() ? getEntry(index.row()) : nullptr;
    if (!entry)
    {
        return QVariant();
    }

    const PDFThemeManager* themeManager = PDFThemeManager::getInstance();
    switch (role)
    {
        case Qt::DisplayRole:
            switch (index.column())
            {
                case TypeColumn:
                    return getTypeText(entry->type);
                case NameColumn:
                    return entry->name;
                case OrganizationColumn:
                    return entry->organization;
                case ValidFromColumn:
                    return PDFAppCertificateTableHelper::formatDate(entry->validFrom);
                case ValidToColumn:
                    return PDFAppCertificateTableHelper::formatDate(entry->validTo);
                default:
                    break;
            }
            break;

        case Qt::FontRole:
            // Certificate row 145:2929: the name Semi Bold 12, the rest Regular 12
            return PDFThemeFonts::getFont(index.column() == NameColumn ? PDFThemeFonts::Role::SmallSemiBold : PDFThemeFonts::Role::Small);

        case Qt::ForegroundRole:
        {
            PDFThemeColors::Color color = PDFThemeColors::TextTertiary;
            if (index.column() == NameColumn)
            {
                color = entry->isDamaged ? PDFThemeColors::TextDanger : PDFThemeColors::TextPrimary;
            }
            else if (index.column() == OrganizationColumn)
            {
                color = PDFThemeColors::TextSecondary;
            }
            return QBrush(themeManager->getColor(color));
        }

        case Qt::ToolTipRole:
            if (index.column() == NameColumn)
            {
                if (entry->isDamaged)
                {
                    return tr("The stored data are not a certificate. Remove the row.");
                }
                return tr("%1\nIssued by: %2\nSHA-256: %3").arg(entry->name, entry->issuer.isEmpty() ? tr("Unknown") : entry->issuer, PDFAppCertificateEntry::getFingerprint(entry->data));
            }
            if (index.column() == ValidFromColumn || index.column() == ValidToColumn)
            {
                const QDateTime& dateTime = index.column() == ValidFromColumn ? entry->validFrom : entry->validTo;
                return dateTime.isValid() ? PDFAppLocale::formatDateTime(dateTime.toLocalTime()) : QVariant();
            }
            if (index.column() == OrganizationColumn && !entry->organization.isEmpty())
            {
                return entry->organization;
            }
            break;

        case Qt::TextAlignmentRole:
            return int(Qt::AlignLeading | Qt::AlignVCenter);

        default:
            break;
    }

    return QVariant();
}

QVariant PDFAppCertificateTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return BaseClass::headerData(section, orientation, role);
    }

    // columns 147:3786
    switch (section)
    {
        case TypeColumn:
            return tr("Type");
        case NameColumn:
            return tr("Certificate");
        case OrganizationColumn:
            return tr("Organization");
        case ValidFromColumn:
            return tr("Valid from");
        case ValidToColumn:
            return tr("Valid to");
        default:
            break;
    }
    return QVariant();
}

void PDFAppCertificateTableModel::setEntries(std::vector<PDFAppCertificateEntry> userEntries, std::vector<PDFAppCertificateEntry> systemEntries)
{
    beginResetModel();
    m_entries = std::move(userEntries);
    std::sort(systemEntries.begin(), systemEntries.end(), PDFAppCertificateTableHelper::isLess);
    m_entries.insert(m_entries.end(), std::make_move_iterator(systemEntries.begin()), std::make_move_iterator(systemEntries.end()));
    endResetModel();
}

const PDFAppCertificateEntry* PDFAppCertificateTableModel::getEntry(int row) const
{
    if (row < 0 || row >= int(m_entries.size()))
    {
        return nullptr;
    }
    return &m_entries[size_t(row)];
}

int PDFAppCertificateTableModel::getCount(PDFAppCertificateEntry::Type type) const
{
    int count = 0;
    for (const PDFAppCertificateEntry& entry : m_entries)
    {
        count += entry.type == type ? 1 : 0;
    }
    return count;
}

int PDFAppCertificateTableModel::findUserRow(qint64 id) const
{
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].type == PDFAppCertificateEntry::Type::User && m_entries[i].id == id)
        {
            return int(i);
        }
    }
    return -1;
}

int PDFAppCertificateTableModel::findSystemRow(const QByteArray& data) const
{
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].type == PDFAppCertificateEntry::Type::System && m_entries[i].data == data)
        {
            return int(i);
        }
    }
    return -1;
}

PDFAppSystemCertificatesTask::PDFAppSystemCertificatesTask(bool isSystemStoreUsed, Provider provider) :
    m_isSystemStoreUsed(isSystemStoreUsed),
    m_provider(provider ? provider : &PDFAppCertificateTableHelper::getSystemCertificates)
{

}

PDFAppSystemCertificates PDFAppSystemCertificatesTask::operator()() const
{
    // Worker thread: nothing of the user interface is used, the entries are parsed here
    PDFAppSystemCertificates result;
    result.isSystemStoreUsed = m_isSystemStoreUsed;
    result.trustListCount = int(std::min<size_t>(PDFCertificateStore::getAATLCertificates().size(), size_t(std::numeric_limits<int>::max())));

    if (m_isSystemStoreUsed)
    {
        const PDFCertificateEntries entries = m_provider();
        for (const PDFCertificateEntry& certificate : entries)
        {
            if (int(result.entries.size()) >= PDFAppCertificateTable::MAXIMUM_SYSTEM_ROWS)
            {
                break;
            }
            result.entries.push_back(PDFAppCertificateEntry::create(PDFAppCertificateEntry::Type::System, certificate.info));
        }
    }
    return result;
}

PDFAppCertificateTable::PDFAppCertificateTable(PDFAppContext* context, QWidget* parent) :
    BaseClass(parent),
    m_context(context)
{
    setObjectName(QStringLiteral("certificateTable"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // card 147:3785: padding 8 inside the border (the view has no margins of its
    // viewport at the sides and at the bottom, while its header is shown)
    PDFAppCard* card = PDFAppPageWidgets::createCard(8, 0, this);
    card->setObjectName(QStringLiteral("card"));
    layout->addWidget(card);
    QVBoxLayout* cardLayout = card->getLayout();

    m_model = new PDFAppCertificateTableModel(this);
    m_view = new PDFTreeView(card);
    m_view->setObjectName(QStringLiteral("certificateView"));
    m_view->setRowStyle(PDFTreeView::RowStyle::Certificate);
    m_view->setRootIsDecorated(false);
    m_view->setItemsExpandable(false);
    m_view->setUniformRowHeights(true);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setTextElideMode(Qt::ElideRight);
    m_view->setModel(m_model);
    m_view->setAccessibleName(tr("Trusted certificates"));
    m_view->installEventFilter(this);

    QHeaderView* header = m_view->header();
    header->setStretchLastSection(false);
    header->setSectionsMovable(false);
    header->setSectionResizeMode(QHeaderView::Fixed);
    header->setSectionResizeMode(PDFAppCertificateTableModel::NameColumn, QHeaderView::Stretch);
    header->resizeSection(PDFAppCertificateTableModel::TypeColumn, TYPE_COLUMN_WIDTH);
    header->resizeSection(PDFAppCertificateTableModel::OrganizationColumn, ORGANIZATION_COLUMN_WIDTH);
    header->resizeSection(PDFAppCertificateTableModel::ValidFromColumn, VALID_FROM_COLUMN_WIDTH);
    header->resizeSection(PDFAppCertificateTableModel::ValidToColumn, VALID_TO_COLUMN_WIDTH);
    cardLayout->addWidget(m_view);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFAppCertificateTable::onSelectionChanged);

    // No certificate at all (no own one, no system store)
    m_emptyLabel = new PDFLabel(tr("No trusted certificates. Trust a certificate in the Signatures panel of a signed document."), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, card);
    m_emptyLabel->setObjectName(QStringLiteral("emptyLabel"));
    m_emptyLabel->setTextFormat(Qt::PlainText);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setContentsMargins(10, 6, 10, 4);
    m_emptyLabel->hide();
    cardLayout->addWidget(m_emptyLabel);

    // The certificates of the Adobe Approved Trust List are trusted too (no rows)
    m_trustListLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, card);
    m_trustListLabel->setObjectName(QStringLiteral("trustListLabel"));
    m_trustListLabel->setTextFormat(Qt::PlainText);
    m_trustListLabel->setWordWrap(true);
    m_trustListLabel->setContentsMargins(10, 8, 10, 0);
    cardLayout->addWidget(m_trustListLabel);

    // actions 147:3828: paddings 10 / 10 / 2 / 2, the status Regular 12 text/muted
    // and Remove (Secondary, trash)
    QWidget* actions = new QWidget(card);
    actions->setObjectName(QStringLiteral("actions"));
    QHBoxLayout* actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(10, 10, 2, 2);
    actionsLayout->setSpacing(12);
    m_statusLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, actions);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_statusLabel->setTextFormat(Qt::PlainText);
    actionsLayout->addWidget(m_statusLabel, 1);
    m_removeButton = PDFAppPageWidgets::createButton(tr("Remove"), QStringLiteral("trash"), false, actions);
    m_removeButton->setObjectName(QStringLiteral("removeButton"));
    m_removeButton->setAccessibleDescription(tr("Removes the selected certificates you trust"));
    connect(m_removeButton.data(), &QPushButton::clicked, this, &PDFAppCertificateTable::onRemoveClicked);
    actionsLayout->addWidget(m_removeButton);
    cardLayout->addWidget(actions);

    reloadUserCertificates();
    reloadSystemCertificates();
}

PDFAppCertificateTable::~PDFAppCertificateTable()
{
    // The confirmation of a table, which is gone, removes nothing
    if (m_removeDialog)
    {
        m_removeDialog->disconnect(this);
        m_removeDialog->reject();
    }

    // A running reading is not waited for: its result is dropped with the watcher
    if (m_systemWatcher)
    {
        m_systemWatcher->disconnect(this);
    }
}

void PDFAppCertificateTable::setSystemCertificatesProvider(PDFAppSystemCertificatesTask::Provider provider)
{
    s_provider = provider;
}

bool PDFAppCertificateTable::isSystemStoreUsed() const
{
    return m_context && m_context->getDatabase() && m_context->getSettings().getBool(PDFSettingKeys::signaturesUseSystemStore());
}

bool PDFAppCertificateTable::isLoadingSystemCertificates() const
{
    // Until the result is taken by onSystemCertificatesLoaded (the signal of the
    // watcher comes from the event loop after the end of the task)
    return m_systemWatcher != nullptr;
}

void PDFAppCertificateTable::refresh(const QString& topic, const QString& key)
{
    if (topic.isEmpty())
    {
        reloadUserCertificates();
        reloadSystemCertificates();
        return;
    }

    if (topic == PDFTrustedCertificateRepository::getTopic())
    {
        reloadUserCertificates();
    }
    else if (topic == PDFSettingsRepository::getTopic() && key == PDFSettingKeys::signaturesUseSystemStore())
    {
        reloadSystemCertificates();
    }
}

void PDFAppCertificateTable::reloadUserCertificates()
{
    std::vector<PDFAppCertificateEntry> entries;
    if (m_context && m_context->getDatabase())
    {
        const PDFTrustedCertificateRepository repository(m_context->getDatabase());
        for (const PDFTrustedCertificate& row : repository.getRows())
        {
            entries.push_back(PDFAppCertificateEntry::create(PDFAppCertificateEntry::Type::User, row.id, row.data));
        }
    }

    m_userEntries = std::move(entries);
    updateModel();
}

void PDFAppCertificateTable::reloadSystemCertificates()
{
    // A reading, which is running, is dropped (its result may be of the old setting)
    if (m_systemWatcher)
    {
        m_systemWatcher->disconnect(this);
        m_systemWatcher->deleteLater();
        m_systemWatcher = nullptr;
    }

    m_systemWatcher = new QFutureWatcher<PDFAppSystemCertificates>(this);
    connect(m_systemWatcher.data(), &QFutureWatcherBase::finished, this, &PDFAppCertificateTable::onSystemCertificatesLoaded);
    m_systemWatcher->setFuture(QtConcurrent::run(PDFAppSystemCertificatesTask(isSystemStoreUsed(), s_provider)));
    updateState();
}

void PDFAppCertificateTable::onSystemCertificatesLoaded()
{
    QFutureWatcher<PDFAppSystemCertificates>* watcher = m_systemWatcher;
    if (!watcher)
    {
        return;
    }

    PDFAppSystemCertificates result = watcher->result();
    m_systemWatcher = nullptr;
    watcher->deleteLater();

    m_trustListCount = result.trustListCount;
    m_systemEntries = std::move(result.entries);
    updateModel();
}

void PDFAppCertificateTable::updateModel()
{
    if (!m_model || !m_view)
    {
        return;
    }

    // The selection and the current row are kept by the certificates
    std::vector<qint64> selectedUserIds;
    std::vector<QByteArray> selectedSystemData;
    for (const PDFAppCertificateEntry& entry : getSelectedEntries())
    {
        if (entry.type == PDFAppCertificateEntry::Type::User)
        {
            selectedUserIds.push_back(entry.id);
        }
        else
        {
            selectedSystemData.push_back(entry.data);
        }
    }

    const PDFAppCertificateEntry* current = m_model->getEntry(m_view->currentIndex().row());
    const PDFAppCertificateEntry::Type currentType = current ? current->type : PDFAppCertificateEntry::Type::User;
    const qint64 currentId = current ? current->id : 0;
    const QByteArray currentData = current ? current->data : QByteArray();

    m_model->setEntries(m_userEntries, m_systemEntries);

    QItemSelection selection;
    for (const qint64 id : selectedUserIds)
    {
        const int row = m_model->findUserRow(id);
        if (row >= 0)
        {
            selection.select(m_model->index(row, 0), m_model->index(row, PDFAppCertificateTableModel::ColumnCount - 1));
        }
    }
    for (const QByteArray& certificateData : selectedSystemData)
    {
        const int row = m_model->findSystemRow(certificateData);
        if (row >= 0)
        {
            selection.select(m_model->index(row, 0), m_model->index(row, PDFAppCertificateTableModel::ColumnCount - 1));
        }
    }

    if (current)
    {
        const int row = currentType == PDFAppCertificateEntry::Type::User ? m_model->findUserRow(currentId) : m_model->findSystemRow(currentData);
        if (row >= 0)
        {
            m_view->selectionModel()->setCurrentIndex(m_model->index(row, 0), QItemSelectionModel::NoUpdate);
        }
    }
    m_view->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);

    updateViewHeight();
    updateState();
}

void PDFAppCertificateTable::updateViewHeight()
{
    if (!m_view || !m_model)
    {
        return;
    }

    // The view is as high as its rows (at most MAXIMUM_VISIBLE_ROWS, then it scrolls)
    const int rows = qBound(1, m_model->rowCount(), MAXIMUM_VISIBLE_ROWS);
    const int rowHeight = m_model->rowCount() > 0 ? qMax(PDFAppCertificateTableHelper::ROW_HEIGHT, m_view->sizeHintForRow(0)) : PDFAppCertificateTableHelper::ROW_HEIGHT;
    // (the tree view replaces the margins of its viewport by the height of its header,
    // while the header is shown - PDFTreeView::updateGeometries)
    const int margins = m_view->isHeaderHidden() ? 2 * PDFAppCertificateTableHelper::VIEW_MARGIN : m_view->header()->sizeHint().height();
    const int height = margins + rows * rowHeight + 2 * m_view->frameWidth();
    if (m_view->minimumHeight() == height && m_view->maximumHeight() == height)
    {
        return;
    }
    m_view->setFixedHeight(height);

    // The parts below the table do not cover its new rows
    PDFAppSettingGroup::updateAncestorLayouts(m_view);
}

std::vector<PDFAppCertificateEntry> PDFAppCertificateTable::getSelectedEntries() const
{
    std::vector<PDFAppCertificateEntry> entries;
    if (!m_view || !m_model || !m_view->selectionModel())
    {
        return entries;
    }

    QModelIndexList rows = m_view->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end());
    for (const QModelIndex& index : rows)
    {
        if (const PDFAppCertificateEntry* entry = m_model->getEntry(index.row()))
        {
            entries.push_back(*entry);
        }
    }
    return entries;
}

void PDFAppCertificateTable::updateState()
{
    if (!m_model || !m_view)
    {
        return;
    }

    const int total = m_model->rowCount();
    const std::vector<PDFAppCertificateEntry> selected = getSelectedEntries();
    const bool hasSystemSelected = std::any_of(selected.cbegin(), selected.cend(), &PDFAppCertificateTableHelper::isSystem);

    // "1 of 5 selected" of 147:3829, the number of the certificates without a selection
    if (m_statusLabel)
    {
        m_statusLabel->setText(selected.empty() ? tr("%n certificates", nullptr, total) : tr("%1 of %2 selected").arg(PDFAppLocale::formatNumber(qint64(selected.size())), PDFAppLocale::formatNumber(total)));
    }

    // Only the own certificates can be removed (owner decision Q4)
    if (m_removeButton)
    {
        const bool canWrite = m_context && m_context->getDatabase();
        m_removeButton->setEnabled(canWrite && !selected.empty() && !hasSystemSelected && !m_removeDialog);
        m_removeButton->setToolTip(hasSystemSelected ? tr("System certificates are managed by the operating system and cannot be removed here.") : QString());
    }

    const bool isEmpty = total == 0 && !isLoadingSystemCertificates();
    m_view->setHidden(isEmpty);
    if (m_emptyLabel)
    {
        m_emptyLabel->setVisible(isEmpty);
    }

    if (m_trustListLabel)
    {
        QString text;
        if (isLoadingSystemCertificates())
        {
            text = tr("Reading the certificates of the system…");
        }
        else if (m_trustListCount > 0)
        {
            text = tr("Also trusted: %n certificates of the Adobe Approved Trust List", nullptr, m_trustListCount);
        }
        m_trustListLabel->setText(text);
        m_trustListLabel->setHidden(text.isEmpty());
    }
}

void PDFAppCertificateTable::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // The heights of the header and of the rows are known, when the style has
    // polished the view
    updateViewHeight();
}

void PDFAppCertificateTable::onSelectionChanged()
{
    updateState();
}

bool PDFAppCertificateTable::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) && keyEvent->modifiers() == Qt::NoModifier)
        {
            openRemoveDialog();
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppCertificateTable::onRemoveClicked()
{
    openRemoveDialog();
}

bool PDFAppCertificateTable::openRemoveDialog()
{
    if (m_removeDialog)
    {
        m_removeDialog->raise();
        return true;
    }

    const std::vector<PDFAppCertificateEntry> selected = getSelectedEntries();
    if (selected.empty() || !m_context || !m_context->getDatabase())
    {
        return false;
    }

    for (const PDFAppCertificateEntry& entry : selected)
    {
        if (entry.type != PDFAppCertificateEntry::Type::User)
        {
            return false;
        }
    }

    // The rows are removed only with the data the dialog names (another instance
    // may give the id of a removed row to a new certificate meanwhile)
    m_pendingRemoval.clear();
    for (const PDFAppCertificateEntry& entry : selected)
    {
        PDFTrustedCertificate row;
        row.id = entry.id;
        row.data = entry.data;
        m_pendingRemoval.push_back(std::move(row));
    }

    const int count = int(selected.size());
    const QString title = count == 1 ? tr("Remove the certificate “%1”?").arg(selected.front().name) : tr("Remove %n certificates?", nullptr, count);
    const QString text = tr("Signatures made by certificates issued by them are no longer shown as valid, unless the system or the Adobe Approved Trust List trusts them. This cannot be undone.");
    PDFAppCardDialog* dialog = new PDFAppCardDialog(PDFThemeIcons::Badge::Warning, true, title, text, 520, window());
    dialog->setObjectName(QStringLiteral("removeCertificatesDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    // The subject and the SHA-256 fingerprint, which the user can compare with the
    // one published by the owner of the certificate (O18)
    QFrame* facts = new QFrame(dialog);
    facts->setObjectName(QStringLiteral("certificateFacts"));
    PDFWidgetStyleProperties::set(facts, PDFWidgetStyleProperties::NOTE);
    QVBoxLayout* factsLayout = new QVBoxLayout(facts);
    factsLayout->setContentsMargins(12, 8, 12, 8);
    factsLayout->setSpacing(2);
    for (int i = 0; i < count && i < MAXIMUM_DIALOG_CERTIFICATES; ++i)
    {
        const PDFAppCertificateEntry& entry = selected[size_t(i)];
        if (i > 0)
        {
            factsLayout->addSpacing(8);
        }

        PDFLabel* name = new PDFLabel(entry.name, PDFThemeFonts::Role::SmallSemiBold, entry.isDamaged ? PDFThemeColors::TextDanger : PDFThemeColors::TextPrimary, facts);
        name->setObjectName(QStringLiteral("certificateName"));
        name->setTextFormat(Qt::PlainText);
        name->setWordWrap(true);
        factsLayout->addWidget(name);

        PDFLabel* fingerprintTitle = new PDFLabel(tr("SHA-256 fingerprint"), PDFThemeFonts::Role::Small, PDFThemeColors::TextTertiary, facts);
        fingerprintTitle->setObjectName(QStringLiteral("fingerprintTitle"));
        factsLayout->addWidget(fingerprintTitle);

        PDFLabel* fingerprint = new PDFLabel(PDFAppCertificateEntry::getFingerprint(entry.data), PDFThemeFonts::Role::SmallMedium, PDFThemeColors::TextPrimary, facts);
        fingerprint->setObjectName(QStringLiteral("fingerprint"));
        fingerprint->setTextFormat(Qt::PlainText);
        fingerprint->setWordWrap(true);
        fingerprint->setTextInteractionFlags(Qt::TextSelectableByMouse);
        factsLayout->addWidget(fingerprint);
    }
    if (count > MAXIMUM_DIALOG_CERTIFICATES)
    {
        PDFLabel* more = new PDFLabel(tr("and %n more certificates", nullptr, count - MAXIMUM_DIALOG_CERTIFICATES), PDFThemeFonts::Role::Small, PDFThemeColors::TextTertiary, facts);
        more->setObjectName(QStringLiteral("moreCertificates"));
        factsLayout->addSpacing(8);
        factsLayout->addWidget(more);
    }
    dialog->addContent(facts);

    // Remove (Secondary, leading - a destructive action) and Cancel (Primary)
    QPushButton* removeButton = dialog->addActionButton(tr("Remove"), false, true);
    removeButton->setObjectName(QStringLiteral("removeButton"));
    QPushButton* cancelButton = dialog->addActionButton(tr("Cancel"), true, false);
    cancelButton->setObjectName(QStringLiteral("cancelButton"));
    connect(removeButton, &QPushButton::clicked, dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(dialog, &QDialog::finished, this, &PDFAppCertificateTable::onRemoveDialogFinished);

    m_removeDialog = dialog;
    updateState();
    dialog->open();
    return true;
}

void PDFAppCertificateTable::onRemoveDialogFinished(int result)
{
    m_removeDialog = nullptr;
    const std::vector<PDFTrustedCertificate> rows = std::move(m_pendingRemoval);
    m_pendingRemoval.clear();

    if (result == QDialog::Accepted && !rows.empty() && m_context && m_context->getDatabase())
    {
        PDFTrustedCertificateRepository repository(m_context->getDatabase());
        const PDFDatabaseError error = repository.removeRows(rows);

        // The notification of the change may delete the page with the table
        QPointer<PDFAppCertificateTable> guard(this);
        if (error.isError())
        {
            Q_EMIT statusMessageRequested(tr("The certificates could not be removed: %1").arg(error.toString()));
        }
        if (!guard)
        {
            return;
        }
        reloadUserCertificates();
        return;
    }

    updateState();
}

}   // namespace pdf
