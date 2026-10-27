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

#include "pdfappsettingssection_storage.h"
#include "pdfappcontext.h"
#include "pdfappdocumentinfobar.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfapplocale.h"
#include "pdfapppage.h"
#include "pdfapppagelayout.h"
#include "pdfappcertificatetable.h"
#include "pdfappsettingcontrol.h"
#include "pdfappsettingrow.h"
#include "pdfappsysteminfo.h"
#include "pdfdatabaseschema.h"
#include "pdfrepositoryhelper.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdftrustedcertificaterepository.h"

#include "pdfdocumenttoolsearch.h"

#include "pdfcomponentpanels.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QPainter>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QtMath>
#include <QVBoxLayout>

#include <algorithm>

namespace pdf
{

/// Texts of the section Storage
class PDFAppStorageSettingsSectionHelper
{
public:
    PDFAppStorageSettingsSectionHelper() = delete;

    /// Documents (the home folder, when the system has none)
    static QString getDocumentsFolder()
    {
        const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        return documents.isEmpty() ? QDir::homePath() : documents;
    }

    /// The database trusts a certificate, which the backup does not have (an import
    /// of the backup's rows removes it)
    static bool hasRemovedCertificates(PDFDatabase* database, const PDFDatabaseBackup& backup)
    {
        if (!database || !backup.isValid())
        {
            return false;
        }

        QSet<QByteArray> backupCertificates;
        for (const QByteArray& data : backup.getTrustedCertificates())
        {
            backupCertificates.insert(data);
        }
        for (const PDFTrustedCertificate& certificate : PDFTrustedCertificateRepository(database).getCertificates())
        {
            if (!backupCertificates.contains(certificate.data))
            {
                return true;
            }
        }
        return false;
    }

    /// A setting and its weaker values (getWeakenedSettings): a Bool setting is
    /// weaker with \p weakValue, an Option setting with a value later in
    /// \p strengthOrder (from the strongest value to the weakest one)
    struct SecurityRule
    {
        QString key;
        bool weakValue = false;
        QStringList strengthOrder;
    };

    /// Security, signature and AI settings and their weaker values (owner
    /// 2026-10-25, Q7; the AI settings of owner 2026-10-26, Q4 of step 7)
    static std::vector<SecurityRule> getSecurityRules()
    {
        const QStringList permissionOrder = { PDFSettingValues::permissionDeny(), PDFSettingValues::permissionAsk(), PDFSettingValues::permissionAllow() };
        return { { PDFSettingKeys::securityOpenLinks(), true, QStringList() },          // documents open web links
                 { PDFSettingKeys::securityLaunchFiles(), true, QStringList() },        // documents start programs
                 { PDFSettingKeys::signaturesVerify(), false, QStringList() },          // signatures not verified
                 { PDFSettingKeys::signaturesStrict(), false, QStringList() },          // warnings are no errors
                 { PDFSettingKeys::signaturesIgnoreExpiration(), true, QStringList() }, // expired certificates pass
                 { PDFSettingKeys::signaturesUseSystemStore(), true, QStringList() },   // more trusted roots
                 { PDFSettingKeys::mcpPermissionRead(), false, permissionOrder },       // Deny < Ask < Allow
                 { PDFSettingKeys::mcpPermissionEdit(), false, permissionOrder },
                 { PDFSettingKeys::mcpPermissionFiles(), false, permissionOrder },
                 { PDFSettingKeys::mcpAllowRemote(), true, QStringList() },             // other computers connect
                 { PDFSettingKeys::mcpRequireToken(), false, QStringList() } };         // no access token
    }

    /// The imported value of a rule is weaker than the current one (both values
    /// validated by the definition: an unknown value is the default)
    static bool isWeakened(const SecurityRule& rule, const PDFSettingDefinition& definition, const QVariant& currentValue, const QVariant& importedValue)
    {
        switch (definition.getType())
        {
            case PDFSettingDefinition::Type::Bool:
                return rule.strengthOrder.isEmpty() && currentValue.toBool() != rule.weakValue && importedValue.toBool() == rule.weakValue;

            case PDFSettingDefinition::Type::Option:
            {
                const qsizetype currentIndex = rule.strengthOrder.indexOf(currentValue.toString());
                const qsizetype importedIndex = rule.strengthOrder.indexOf(importedValue.toString());
                return currentIndex >= 0 && importedIndex >= 0 && importedIndex > currentIndex;
            }

            default:
                break;
        }
        return false;
    }

    /// Order of the rows of the other database files: by the schema version
    static bool isOlderFile(const PDFDatabaseFileInfo& left, const PDFDatabaseFileInfo& right)
    {
        return left.schemaVersion < right.schemaVersion;
    }

    /// Data size of a file ("2.4 MB")
    static QString formatSize(qint64 size)
    {
        return PDFAppLocale::formatDataSize(qMax<qint64>(0, size), size < 1000000 ? 0 : 1, QLocale::DataSizeSIFormat);
    }
};

PDFAppStorageStateValue::PDFAppStorageStateValue(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("storageState"));
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppStorageStateValue::update));
}

PDFAppStorageStateValue::~PDFAppStorageStateValue() = default;

void PDFAppStorageStateValue::setState(const QString& iconName, PDFThemeColors::Color iconColor, const QString& text)
{
    if (m_iconName == iconName && m_iconColor == iconColor && m_text == text)
    {
        return;
    }

    m_iconName = iconName;
    m_iconColor = iconColor;
    m_text = text;
    setAccessibleName(text);
    updateGeometry();
    update();
}

QSize PDFAppStorageStateValue::sizeHint() const
{
    // The advance of a text is fractional: the width is rounded up (an elided text otherwise)
    const QFontMetricsF metrics(PDFThemeFonts::getFont(PDFThemeFonts::Role::Body));
    return QSize(ICON_SIZE + SPACING + qCeil(metrics.horizontalAdvance(m_text)) + 1, qMax(ICON_SIZE, qCeil(metrics.height())));
}

QSize PDFAppStorageStateValue::minimumSizeHint() const
{
    return QSize(ICON_SIZE, sizeHint().height());
}

void PDFAppStorageStateValue::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // status 1269:14762: the icon 16, gap 6, the text Regular 13 text/primary
    QPainter painter(this);
    PDFStylePainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const QRect area = rect();
    const QRectF iconRect(isRightToLeft ? area.right() + 1 - ICON_SIZE : area.left(), area.top() + (area.height() - ICON_SIZE) / 2.0, ICON_SIZE, ICON_SIZE);
    if (!m_iconName.isEmpty())
    {
        PDFStylePainter::icon(&painter, iconRect, m_iconName, m_iconColor);
    }

    const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::Body);
    const QFontMetricsF metrics(font);
    const QRect textRect = area.adjusted(isRightToLeft ? 0 : ICON_SIZE + SPACING, 0, isRightToLeft ? -(ICON_SIZE + SPACING) : 0, 0);
    const QString text = metrics.elidedText(m_text, Qt::ElideRight, qreal(textRect.width()));
    // Absolute: the painter has the direction of the application, not of the widget
    PDFStylePainter::text(&painter, QRectF(textRect), int(Qt::AlignVCenter | Qt::AlignAbsolute | (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft)), text, font,
                          PDFThemeManager::getInstance()->getColor(PDFThemeColors::TextPrimary));
}

void PDFAppStorageStateValue::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LayoutDirectionChange)
    {
        update();
    }
}

PDFAppStorageDialog::PDFAppStorageDialog(PDFThemeIcons::Badge badge, const QString& title, const QString& text, const std::vector<Fact>& facts,
                                         const QString& actionText, QWidget* parent) :
    BaseClass(badge, true, title, text, CARD_WIDTH, parent),
    m_titleText(title),
    m_messageText(text)
{
    setObjectName(QStringLiteral("storageDialog"));

    // facts 1271:15374: bg/subtle, radius 10, paddings 4 / 12, Key value rows
    if (!facts.empty())
    {
        QFrame* factsFrame = new QFrame(this);
        factsFrame->setObjectName(QStringLiteral("facts"));
        PDFWidgetStyleProperties::set(factsFrame, PDFWidgetStyleProperties::NOTE);
        QVBoxLayout* factsLayout = new QVBoxLayout(factsFrame);
        factsLayout->setContentsMargins(12, 4, 12, 4);
        factsLayout->setSpacing(0);
        m_facts = new PDFKeyValueList(factsFrame);
        m_facts->setObjectName(QStringLiteral("factsList"));
        for (const Fact& fact : facts)
        {
            m_facts->addRow(fact.key, fact.value, fact.isFileName ? PDFComponentPainter::TextKind::FileName : PDFComponentPainter::TextKind::Data);
        }
        factsLayout->addWidget(m_facts);
        addContent(factsFrame);
    }

    // actions 1271:15377: the destructive action (Secondary) at the leading edge,
    // Cancel (Primary); an error has one OK (Primary) at the trailing edge
    if (!actionText.isEmpty())
    {
        m_actionButton = addActionButton(actionText, false, true);
        m_actionButton->setObjectName(QStringLiteral("actionButton"));
        connect(m_actionButton.data(), &QPushButton::clicked, this, &PDFAppStorageDialog::accept);
        m_cancelButton = addActionButton(tr("Cancel"), true, false);
    }
    else
    {
        m_cancelButton = addActionButton(tr("OK"), true, false);
    }
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppStorageDialog::reject);
}

PDFAppStorageDialog::~PDFAppStorageDialog() = default;

PDFKeyValueList* PDFAppStorageDialog::addFacts(const QString& objectName, const QString& heading, const std::vector<Fact>& facts)
{
    // The heading Semi Bold 13 text/primary, gap 8, then the facts of 1271:15374
    QWidget* block = new QWidget(this);
    block->setObjectName(objectName + QStringLiteral("Block"));
    QVBoxLayout* blockLayout = new QVBoxLayout(block);
    blockLayout->setContentsMargins(0, 0, 0, 0);
    blockLayout->setSpacing(8);
    if (!heading.isEmpty())
    {
        PDFAppParagraph* headingText = PDFAppPageWidgets::createText(heading, PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold), 18,
                                                                     PDFThemeColors::TextPrimary, block);
        headingText->setObjectName(objectName + QStringLiteral("Heading"));
        blockLayout->addWidget(headingText);
    }

    QFrame* factsFrame = new QFrame(block);
    PDFWidgetStyleProperties::set(factsFrame, PDFWidgetStyleProperties::NOTE);
    QVBoxLayout* factsLayout = new QVBoxLayout(factsFrame);
    factsLayout->setContentsMargins(12, 4, 12, 4);
    factsLayout->setSpacing(0);
    PDFKeyValueList* list = new PDFKeyValueList(factsFrame);
    list->setObjectName(objectName);
    for (const Fact& fact : facts)
    {
        list->addRow(fact.key, fact.value, fact.isFileName ? PDFComponentPainter::TextKind::FileName : PDFComponentPainter::TextKind::Data);
    }
    factsLayout->addWidget(list);
    blockLayout->addWidget(factsFrame);
    addContent(block);
    return list;
}

QCheckBox* PDFAppStorageDialog::addCheckBox(const QString& objectName, const QString& text, const QString& note)
{
    if (!m_checkBox)
    {
        m_checkBox = new QCheckBox(text, this);
        addContent(m_checkBox);
    }
    m_checkBox->setObjectName(objectName);
    m_checkBox->setText(text);

    if (!note.isEmpty() && !m_checkBoxNote)
    {
        m_checkBoxNote = PDFAppPageWidgets::createText(note, PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 16, PDFThemeColors::TextMuted, this);
        m_checkBoxNote->setObjectName(objectName + QStringLiteral("Note"));
        addContent(m_checkBoxNote);
    }
    else if (m_checkBoxNote)
    {
        m_checkBoxNote->setText(note);
        m_checkBoxNote->setVisible(!note.isEmpty());
    }
    return m_checkBox;
}

PDFAppStorageSettingsSection::PDFAppStorageSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) :
    BaseClass(context, sectionId, parent)
{
    if (PDFDatabase* database = getDatabase())
    {
        connect(database, &PDFDatabase::stateChanged, this, &PDFAppStorageSettingsSection::onDatabaseStateChanged);
    }
}

PDFAppStorageSettingsSection::~PDFAppStorageSettingsSection()
{
    // The dialog belongs to the window: it goes with the section, its result is
    // not used any more
    closeDialog();
}

bool PDFAppStorageSettingsSection::isRestoreSupported() const
{
    return false;
}

PDFDatabase* PDFAppStorageSettingsSection::getDatabase() const
{
    PDFAppContext* context = getContext();
    return context ? context->getDatabase() : nullptr;
}

bool PDFAppStorageSettingsSection::hasFolder() const
{
    // A portable PDF4QT never touches the files of the profile
    const PDFDatabase* database = getDatabase();
    return database && !database->getOptions().portable && !database->getOptions().directory.isEmpty() &&
           database->getState().mode != PDFDatabaseStorageMode::Portable;
}

void PDFAppStorageSettingsSection::finishContent()
{
    // group-Settings database 1269:14723
    m_databaseGroup = addGroup(QString::fromLatin1(DATABASE_GROUP_KEY), tr("Settings database"), QString());
    if (m_databaseGroup)
    {
        m_locationRow = new PDFAppSettingRow(tr("Location"), QString(), QStringLiteral("location"));
        m_locationRow->setSearchKeywords({ QStringLiteral("database"), QStringLiteral("file"), QStringLiteral("folder"), QStringLiteral("path") });
        m_showInFolderButton = PDFAppPageWidgets::createButton(tr("Show in Folder"), QStringLiteral("show-in-folder"), false, nullptr);
        m_showInFolderButton->setObjectName(QStringLiteral("showInFolderButton"));
        connect(m_showInFolderButton.data(), &QPushButton::clicked, this, &PDFAppStorageSettingsSection::onShowInFolderClicked);
        m_locationRow->setControlWidget(m_showInFolderButton);
        m_databaseGroup->addRow(m_locationRow);
        if (m_locationRow->getDescriptionLabel())
        {
            // The path is data text elided in the middle (its width follows the row)
            m_locationRow->getDescriptionLabel()->installEventFilter(this);
        }

        m_stateRow = new PDFAppSettingRow(tr("State"), QString(), QStringLiteral("state"));
        m_stateRow->setSearchKeywords({ QStringLiteral("portable"), QStringLiteral("memory"), QStringLiteral("read only") });
        m_stateValue = new PDFAppStorageStateValue();
        m_stateRow->setControlWidget(m_stateValue);
        m_databaseGroup->addRow(m_stateRow);
    }

    // group-Backup 1269:14766 with the hint
    m_backupGroup = addGroup(QString::fromLatin1(BACKUP_GROUP_KEY), tr("Backup"), tr("One file with everything PDF4QT keeps for you"));
    if (m_backupGroup)
    {
        m_exportRow = new PDFAppSettingRow(tr("Export settings"), QString(), QStringLiteral("export"));
        m_exportRow->setSearchKeywords({ QStringLiteral("backup"), QStringLiteral("export"), QStringLiteral("save") });
        m_exportButton = PDFAppPageWidgets::createButton(tr("Export…"), QStringLiteral("export"), false, nullptr);
        m_exportButton->setObjectName(QStringLiteral("exportButton"));
        connect(m_exportButton.data(), &QPushButton::clicked, this, &PDFAppStorageSettingsSection::onExportClicked);
        m_exportRow->setControlWidget(m_exportButton);
        m_backupGroup->addRow(m_exportRow);

        m_importRow = new PDFAppSettingRow(tr("Import settings"), QString(), QStringLiteral("import"));
        m_importRow->setSearchKeywords({ QStringLiteral("backup"), QStringLiteral("import"), QStringLiteral("restore") });
        m_importButton = PDFAppPageWidgets::createButton(tr("Import…"), QStringLiteral("import"), false, nullptr);
        m_importButton->setObjectName(QStringLiteral("importButton"));
        connect(m_importButton.data(), &QPushButton::clicked, this, &PDFAppStorageSettingsSection::onImportClicked);
        m_importRow->setControlWidget(m_importButton);
        m_backupGroup->addRow(m_importRow);

        // info-bar-exported 1271:15212: 12 below the card, the bar itself lies over
        // the section (its shadow reaches out of the slot)
        m_exportInfoBarSlot = new QWidget(m_backupGroup);
        m_exportInfoBarSlot->setObjectName(QStringLiteral("exportInfoBarSlot"));
        m_exportInfoBarSlot->setFixedHeight(PDFAppInfoBar::CARD_HEIGHT);
        m_exportInfoBarSlot->hide();
        m_exportInfoBarSlot->installEventFilter(this);
        m_backupGroup->getLayout()->addWidget(m_exportInfoBarSlot);
    }

    // group-Other database files 1269:14807 with the hint
    m_filesGroup = addGroup(QString::fromLatin1(FILES_GROUP_KEY), tr("Other database files"), tr("Files of other PDF4QT versions, kept untouched"));
    if (m_filesGroup)
    {
        // The card of 1270:14724: one line "No other database files", paddings 16, the
        // height of a row (56 inside the border) with the text centered (20 / 21)
        const int border = PDFAppPageWidgets::BORDER_WIDTH;
        m_emptyFilesCard = new PDFAppCard(QMargins(border + 16, border + 20, border + 16, border + 21), 0, m_filesGroup);
        m_emptyFilesCard->setObjectName(QStringLiteral("emptyFilesCard"));
        PDFAppParagraph* emptyText = PDFAppPageWidgets::createText(tr("No other database files"), PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 15,
                                                                   PDFThemeColors::TextMuted, m_emptyFilesCard);
        emptyText->setObjectName(QStringLiteral("emptyFilesText"));
        m_emptyFilesCard->getLayout()->addWidget(emptyText);
        m_filesGroup->insertWidget(m_emptyFilesCard);
    }

    updateState();
    updateFiles();
}

void PDFAppStorageSettingsSection::updateState()
{
    PDFDatabase* database = getDatabase();
    const PDFDatabaseState state = database ? database->getState() : PDFDatabaseState();
    const bool isPortable = state.mode == PDFDatabaseStorageMode::Portable;

    m_locationPath = isPortable ? QString() : state.filePath;
    if (m_showInFolderButton)
    {
        // Disabled in its place (1270:14982), so the order of the buttons stays
        m_showInFolderButton->setEnabled(!m_locationPath.isEmpty());
    }
    updateLocationText();

    QString stateDescription;
    if (m_stateValue)
    {
        switch (state.mode)
        {
            case PDFDatabaseStorageMode::Disk:
                m_stateValue->setState(QStringLiteral("check-circle"), PDFThemeColors::AccentGreenIcon, tr("Saved on this computer"));
                stateDescription = tr("Every PDF4QT window uses this file and sees each change at once.");
                break;

            case PDFDatabaseStorageMode::Portable:
                m_stateValue->setState(QStringLiteral("portable"), PDFThemeColors::AccentBlueIcon, tr("Portable – nothing is written"));
                stateDescription = tr("Settings and history live in memory until PDF4QT closes.");
                break;

            case PDFDatabaseStorageMode::ReadOnlyNewerSchema:
                // The tone of the banner Read only of 958:28036 (information, not a warning)
                m_stateValue->setState(QStringLiteral("lock"), PDFThemeColors::AccentBlueIcon, tr("Read only – newer version"));
                stateDescription = tr("A newer PDF4QT saved the settings. This version reads them but does not change them.");
                break;

            case PDFDatabaseStorageMode::MemoryFallback:
                m_stateValue->setState(QStringLiteral("alert-triangle"), PDFThemeColors::AccentAmberIcon, tr("In memory – kept until PDF4QT closes"));
                stateDescription = tr("The settings database could not be opened. Changes are kept only until PDF4QT closes.");
                break;
        }
    }
    if (m_stateRow)
    {
        m_stateRow->setDescription(stateDescription);
    }

    // Export settings / Import settings of 1269:14785 and of the portable 1270:15001
    if (m_exportRow)
    {
        m_exportRow->setDescription(isPortable ? tr("Settings, shortcuts, Recent files and folders, workspaces, bookmarks, trusted certificates, pinned tools, tool history and annotation styles of this session. Includes the paths of your documents, not the documents.")
                                               : tr("Settings, shortcuts, Recent files and folders, workspaces, bookmarks, trusted certificates, pinned tools, tool history and annotation styles. Includes the paths of your documents, not the documents."));
    }
    if (m_importRow)
    {
        const bool isReadOnly = state.mode == PDFDatabaseStorageMode::ReadOnlyNewerSchema;
        m_importRow->setDescription(isReadOnly ? tr("The settings of a newer PDF4QT cannot be replaced by this version.")
                                               : (isPortable ? tr("Replaces the settings and history of this session with the backup. Nothing is written to this computer.")
                                                             : tr("Replaces all current settings and history with the backup. Older backups are updated first.")));
        if (m_importButton)
        {
            m_importButton->setEnabled(!isReadOnly);
        }
    }

    updateExtraWidgets();
}

void PDFAppStorageSettingsSection::updateLocationText()
{
    if (!m_locationRow || m_isElidingLocation)
    {
        return;
    }

    PDFAppParagraph* label = m_locationRow->getDescriptionLabel();
    if (m_locationPath.isEmpty())
    {
        m_locationRow->setDescription(tr("In memory only – no file is written to this computer."));
        if (label)
        {
            label->setToolTip(QString());
        }
        return;
    }

    // The path is data: elided in the middle to the width of its line, the whole
    // path in the tooltip
    m_isElidingLocation = true;
    const QString path = QDir::toNativeSeparators(m_locationPath);
    QString text = path;
    if (label && label->width() > 0)
    {
        const QFontMetrics metrics(label->font());
        text = metrics.elidedText(path, Qt::ElideMiddle, label->contentsRect().width());
    }
    if (label)
    {
        label->setSizePolicy(QSizePolicy::Ignored, label->sizePolicy().verticalPolicy());
        label->setToolTip(path);
    }
    if (m_locationRow->getDescription() != text)
    {
        m_locationRow->setDescription(text);
    }
    m_isElidingLocation = false;
}

QString PDFAppStorageSettingsSection::getFileDescription(const PDFDatabaseFileInfo& file)
{
    using Helper = PDFAppStorageSettingsSectionHelper;

    // "PDF4QT 2.0 · 2.4 MB · Oct 12, 2026" (owner 2026-10-25: the version, not the schema;
    // a newer file says it by its label "Newer PDF4QT" and its tag Newer version)
    QStringList parts = { PDFDatabaseFiles::getVersionLabel(file.schemaVersion), Helper::formatSize(file.size) };
    if (file.modified.isValid())
    {
        parts << PDFAppLocale::formatDate(file.modified.date());
    }
    return parts.join(QStringLiteral(" · "));
}

QString PDFAppStorageSettingsSection::getBackupFolder(PDFAppContext* context)
{
    using Helper = PDFAppStorageSettingsSectionHelper;

    // The stored folder is untrusted (another version, another computer): a device
    // or a network path is never touched (a server, which does not answer, would
    // block the user interface), only a local folder is checked to exist
    const QString folder = (context && context->getDatabase()) ? context->getSettings().getString(PDFSettingKeys::storageBackupFolder()) : QString();
    if (folder.isEmpty() || !PDFRepositoryHelper::isValidStoredPath(folder) || PDFAppNetworkPaths::isNetworkPathByName(folder) ||
        PDFAppNetworkPaths().isNetworkPath(folder))
    {
        return Helper::getDocumentsFolder();
    }

    const QFileInfo info(folder);
    return info.isDir() ? QDir::cleanPath(info.absoluteFilePath()) : Helper::getDocumentsFolder();
}

void PDFAppStorageSettingsSection::storeBackupFolder(const QString& filePath)
{
    // A state of the application, not a visible setting; an error (a read only
    // database) only means the folder is not remembered
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase() || filePath.isEmpty())
    {
        return;
    }

    const QString folder = QDir::cleanPath(QFileInfo(filePath).absolutePath());
    if (PDFRepositoryHelper::isValidStoredPath(folder) && context->getSettings().getString(PDFSettingKeys::storageBackupFolder()) != folder)
    {
        context->getSettings().setValue(PDFSettingKeys::storageBackupFolder(), folder);
    }
}

std::vector<QByteArray> PDFAppStorageSettingsSection::getNewCertificates(PDFDatabase* database, const PDFDatabaseBackup& backup)
{
    std::vector<QByteArray> certificates;
    if (!backup.isValid())
    {
        return certificates;
    }

    QSet<QByteArray> known;
    if (database)
    {
        for (const PDFTrustedCertificate& certificate : PDFTrustedCertificateRepository(database).getCertificates())
        {
            known.insert(certificate.data);
        }
    }

    // A row, which is not a certificate, adds no trust (getCertificates skips it)
    for (const QByteArray& data : backup.getTrustedCertificates())
    {
        if (!known.contains(data) && PDFTrustedCertificateRepository::isValidCertificate(data))
        {
            known.insert(data);
            certificates.push_back(data);
        }
    }
    return certificates;
}

std::vector<PDFAppStorageDialog::Fact> PDFAppStorageSettingsSection::getWeakenedSettings(PDFAppContext* context, const PDFDatabaseBackup& backup)
{
    std::vector<PDFAppStorageDialog::Fact> facts;
    if (!context || !context->getDatabase() || !backup.isValid())
    {
        return facts;
    }

    // Values as the settings read them: a missing or invalid stored value is the default
    const PDFSettingsRepository settings = context->getSettings();
    for (const PDFAppStorageSettingsSectionHelper::SecurityRule& rule : PDFAppStorageSettingsSectionHelper::getSecurityRules())
    {
        const PDFSettingDefinition* definition = context->getSettingRegistry()->findDefinition(rule.key);
        if (!definition)
        {
            continue;
        }

        const QVariant currentValue = definition->validate(settings.getValue(rule.key));
        const QVariant importedValue = definition->validate(backup.getSecuritySetting(rule.key));
        if (PDFAppStorageSettingsSectionHelper::isWeakened(rule, *definition, currentValue, importedValue))
        {
            facts.push_back({ definition->getTitle(), QString::fromUtf8("%1 → %2").arg(PDFAppSettingControl::formatValue(*definition, currentValue),
                                                                                       PDFAppSettingControl::formatValue(*definition, importedValue)), false });
        }
    }
    return facts;
}

QList<PDFAppSettingRow*> PDFAppStorageSettingsSection::getFileRows() const
{
    QList<PDFAppSettingRow*> rows;
    for (const QPointer<PDFAppSettingRow>& row : m_fileRows)
    {
        if (row)
        {
            rows << row.data();
        }
    }
    return rows;
}

void PDFAppStorageSettingsSection::updateFiles()
{
    if (!m_filesGroup)
    {
        return;
    }

    PDFDatabase* database = getDatabase();
    const int applicationVersion = database ? database->getState().applicationSchemaVersion : PDFDatabaseSchema::getCurrentVersion();
    const QString openFilePath = database ? QDir::cleanPath(database->getState().filePath) : QString();

    std::vector<PDFDatabaseFileInfo> files;
    if (hasFolder())
    {
        for (const PDFDatabaseFileInfo& file : database->getDatabaseFiles())
        {
            // The file of this version and a newer file opened in memory are not
            // "other" files
            if (!file.isCurrent && file.filePath != openFilePath)
            {
                files.push_back(file);
            }
        }

        // The older files first, then the newer ones (1269:14388)
        std::stable_sort(files.begin(), files.end(), &PDFAppStorageSettingsSectionHelper::isOlderFile);
    }

    // The rows are built again (a deleted file, a file of a version started meanwhile)
    for (const QPointer<PDFAppSettingRow>& row : std::as_const(m_fileRows))
    {
        delete row.data();
    }
    m_fileRows.clear();

    for (const PDFDatabaseFileInfo& file : files)
    {
        const QString fileName = QFileInfo(file.filePath).fileName();
        PDFAppSettingRow* row = new PDFAppSettingRow(fileName, getFileDescription(file),
                                                     QString::fromLatin1(FILE_ANCHOR_PREFIX) + QString::number(file.schemaVersion));
        row->setSearchKeywords({ QStringLiteral("database"), QStringLiteral("older"), QStringLiteral("delete") });
        row->setProperty("filePath", file.filePath);
        if (file.schemaVersion > applicationVersion)
        {
            // A newer PDF4QT keeps its file up to date: no Delete (1269:14836)
            row->setTagText(tr("Newer version"));
        }
        else
        {
            QPushButton* deleteButton = PDFAppPageWidgets::createButton(tr("Delete…"), QStringLiteral("trash"), false, nullptr);
            deleteButton->setObjectName(QStringLiteral("deleteButton"));
            deleteButton->setProperty("filePath", file.filePath);
            deleteButton->setAccessibleName(tr("Delete %1").arg(fileName));
            connect(deleteButton, &QPushButton::clicked, this, &PDFAppStorageSettingsSection::onDeleteClicked);
            row->setControlWidget(deleteButton);
        }
        m_filesGroup->addRow(row);
        m_fileRows.append(row);
    }

    m_filesGroup->updateRows();

    // A new row is shown: the search of the page applies to it as well
    const PDFSearchQuery query(m_query);
    if (!query.isEmpty())
    {
        BaseClass::applyFilter(query);
    }
    updateExtraWidgets();
}

int PDFAppStorageSettingsSection::applyFilter(const PDFSearchQuery& query)
{
    // The text of the query: the rows of the files built again later are filtered too
    m_query = query.isEmpty() ? QString() : query.getText();
    const int matches = BaseClass::applyFilter(query);
    updateExtraWidgets();

    // A hidden group of the files does not count
    if (!hasFolder() && m_filesGroup)
    {
        int hiddenMatches = 0;
        for (PDFAppSettingRow* row : m_filesGroup->getRows())
        {
            hiddenMatches += row->isHidden() ? 0 : 1;
        }
        return qMax(0, matches - hiddenMatches);
    }
    return matches;
}

void PDFAppStorageSettingsSection::updateExtraWidgets()
{
    // The base shows the widgets of the groups whenever the section is not filtered:
    // the state of the storage decides here
    const bool isFiltered = !m_query.isEmpty();
    if (m_filesGroup)
    {
        if (!hasFolder())
        {
            m_filesGroup->hide();
        }
        else if (!isFiltered)
        {
            m_filesGroup->show();
        }
    }

    if (m_emptyFilesCard)
    {
        m_emptyFilesCard->setVisible(!isFiltered && getFileRows().isEmpty());
    }

    if (m_exportInfoBarSlot)
    {
        const bool isBarShown = !isFiltered && m_exportInfoBar && !m_exportedFilePath.isEmpty();
        m_exportInfoBarSlot->setVisible(isBarShown);
        if (m_exportInfoBar)
        {
            m_exportInfoBar->setVisible(isBarShown);
        }
        layoutExportInfoBar();
    }
}

void PDFAppStorageSettingsSection::refresh(const QString& topic, const QString& key)
{
    BaseClass::refresh(topic, key);

    // Everything has changed (an import, Try Again): the state and the files again
    if (topic.isEmpty())
    {
        updateState();
        updateFiles();
    }
}

bool PDFAppStorageSettingsSection::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (m_locationRow && watched == m_locationRow->getDescriptionLabel() && type == QEvent::Resize)
    {
        updateLocationText();
    }
    else if (watched == m_exportInfoBarSlot && (type == QEvent::Move || type == QEvent::Resize || type == QEvent::Show || type == QEvent::Hide))
    {
        layoutExportInfoBar();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppStorageSettingsSection::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // Files of other versions may have come or gone meanwhile
    updateFiles();
}

void PDFAppStorageSettingsSection::layoutExportInfoBar()
{
    if (!m_exportInfoBar || !m_exportInfoBarSlot)
    {
        return;
    }

    if (m_exportInfoBarSlot->isHidden())
    {
        m_exportInfoBar->hide();
        return;
    }

    // The card hugs its content at the leading edge of the group (1271:15212); the
    // widget of the bar holds the margins of its shadow around the card
    const QMargins shadow = PDFAppInfoBar::getShadowMargins();
    const QRect slot(m_exportInfoBarSlot->mapTo(this, QPoint(0, 0)), m_exportInfoBarSlot->size());
    const int cardWidth = qMax(0, qMin(m_exportInfoBar->sizeHint().width() - shadow.left() - shadow.right(), slot.width()));
    const int cardLeft = layoutDirection() == Qt::RightToLeft ? slot.right() + 1 - cardWidth : slot.left();
    m_exportInfoBar->setGeometry(QRect(cardLeft - shadow.left(), slot.top() - shadow.top(), cardWidth + shadow.left() + shadow.right(), m_exportInfoBar->height()));
    m_exportInfoBar->show();
    m_exportInfoBar->raise();
}

void PDFAppStorageSettingsSection::showExportInfoBar(const QString& filePath)
{
    m_exportedFilePath = filePath;

    PDFAppInfoBarRequest request;
    request.id = QString::fromLatin1(EXPORT_INFO_BAR_ID);
    request.tone = PDFAppInfoBarTone::Success;
    request.iconName = QStringLiteral("check-circle");
    request.title = tr("Settings exported to “%1”").arg(QFileInfo(filePath).fileName());
    request.actions = { PDFAppInfoBarAction{ QStringLiteral("show-in-folder"), tr("Show in Folder"), QString() } };
    request.isClosable = true;

    if (m_exportInfoBar)
    {
        m_exportInfoBar->setRequest(request);
    }
    else
    {
        m_exportInfoBar = new PDFAppInfoBar(request, this);
        connect(m_exportInfoBar.data(), &PDFAppInfoBar::actionTriggered, this, &PDFAppStorageSettingsSection::onInfoBarActionTriggered);
        connect(m_exportInfoBar.data(), &PDFAppInfoBar::closeRequested, this, &PDFAppStorageSettingsSection::onInfoBarCloseRequested);
    }

    updateExtraWidgets();
}

void PDFAppStorageSettingsSection::hideExportInfoBar()
{
    m_exportedFilePath.clear();
    if (m_exportInfoBar)
    {
        // The bar is deleted later: this may run from its own signal
        m_exportInfoBar->hide();
        m_exportInfoBar->deleteLater();
        m_exportInfoBar = nullptr;
    }
    updateExtraWidgets();
}

void PDFAppStorageSettingsSection::requestShowInFolder(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return;
    }

    // The page opens the folder (the navigator checks the address: local folders only)
    for (QWidget* widget = parentWidget(); widget; widget = widget->parentWidget())
    {
        if (PDFAppPage* page = qobject_cast<PDFAppPage*>(widget))
        {
            Q_EMIT page->externalUrlRequested(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
            return;
        }
    }
}

void PDFAppStorageSettingsSection::onShowInFolderClicked()
{
    requestShowInFolder(m_locationPath);
}

void PDFAppStorageSettingsSection::onInfoBarActionTriggered(const QString& infoBarId, const QString& actionId)
{
    if (infoBarId == QLatin1String(EXPORT_INFO_BAR_ID) && actionId == QLatin1String("show-in-folder"))
    {
        requestShowInFolder(m_exportedFilePath);
    }
}

void PDFAppStorageSettingsSection::onInfoBarCloseRequested(const QString& infoBarId)
{
    if (infoBarId == QLatin1String(EXPORT_INFO_BAR_ID))
    {
        hideExportInfoBar();
    }
}

void PDFAppStorageSettingsSection::onExportClicked()
{
    exportSettings();
}

void PDFAppStorageSettingsSection::onImportClicked()
{
    importSettings();
}

void PDFAppStorageSettingsSection::onDeleteClicked()
{
    // The clicked signal does not identify the button: its file is its property
    const QObject* button = sender();
    if (button)
    {
        requestDelete(button->property("filePath").toString());
    }
}

void PDFAppStorageSettingsSection::exportSettings()
{
    const QString proposed = QDir(getBackupFolder(getContext())).filePath(PDFDatabaseBackup::getDefaultFileName(QDate::currentDate()));
    QPointer<PDFAppStorageSettingsSection> guard(this);
    QString filePath = PDFAppFileDialogs::getSaveBackupFileName(window(), proposed);
    if (!guard || filePath.isEmpty())
    {
        return;
    }
    storeBackupFolder(filePath);

    // A backup has always its suffix (never the name of a database file of the
    // folder). The dialog has asked before an existing file of the chosen name is
    // replaced, not for a name with the suffix added here: such an existing file is
    // kept and the backup gets a free name ("name (2).pdf4qt-backup").
    const QString suffix = QStringLiteral(".") + QLatin1String(PDFDatabaseBackup::FILE_SUFFIX);
    if (!filePath.endsWith(suffix, Qt::CaseInsensitive))
    {
        const QString base = filePath;
        filePath = base + suffix;
        for (int number = 2; QFileInfo::exists(filePath) && number < 1000; ++number)
        {
            filePath = QStringLiteral("%1 (%2)%3").arg(base).arg(number).arg(suffix);
        }
    }

    PDFDatabase* database = getDatabase();
    const PDFDatabaseError error = database ? database->exportBackup(filePath) : PDFDatabaseError::fromMessage(tr("The settings are not available."));
    if (error.isError())
    {
        hideExportInfoBar();
        showError(tr("The settings could not be exported"), error.getMessage(), { { tr("File"), QFileInfo(filePath).fileName(), true } });
        return;
    }

    showExportInfoBar(filePath);
}

void PDFAppStorageSettingsSection::importSettings()
{
    PDFDatabase* database = getDatabase();
    if (!database)
    {
        return;
    }

    QPointer<PDFAppStorageSettingsSection> guard(this);
    const QString filePath = PDFAppFileDialogs::getOpenBackupFileName(window(), getBackupFolder(getContext()));
    if (!guard || filePath.isEmpty())
    {
        return;
    }
    storeBackupFolder(filePath);

    // The file is untrusted: it is read once and checked (PDFDatabaseBackup)
    database = getDatabase();
    if (!database)
    {
        return;
    }
    PDFDatabaseBackup backup = PDFDatabaseBackup::read(filePath, database->getOptions().migrations);
    const QString fileName = QFileInfo(filePath).fileName();
    if (!backup.isValid())
    {
        std::vector<PDFAppStorageDialog::Fact> facts = { { tr("File"), fileName, true } };
        if (backup.getStatus() == PDFDatabaseBackupStatus::Newer)
        {
            // 1271:15697: the version of the backup and what this PDF4QT reads
            facts.push_back({ tr("Version"), tr("%1 – this PDF4QT reads backups up to %2").arg(PDFDatabaseFiles::getVersionLabel(backup.getSchemaVersion()),
                                                                                             PDFDatabaseFiles::getVersionLabel(backup.getApplicationSchemaVersion())), false });
        }
        showError(tr("The file could not be imported"), tr("%1 Nothing was changed.").arg(backup.getErrorText()), facts);
        return;
    }

    // Owner 2026-10-25 (Q7): the certificates of the backup, which are not trusted
    // yet, need the check box; a backup without them only removes trust
    const std::vector<QByteArray> newCertificates = getNewCertificates(database, backup);
    m_hasNewCertificates = !newCertificates.empty();

    // 1271:15532: what is replaced (the trusted certificates are told apart: kept
    // unless checked, or replaced by a backup, which adds none), the file, its
    // time and its version
    const bool isPortable = database->getState().mode == PDFDatabaseStorageMode::Portable;
    QStringList textParts;
    textParts << (isPortable ? tr("The settings, shortcuts, Recent files and folders, workspaces, bookmarks, pinned tools, tool history and annotation styles of this session are replaced by the backup. Nothing is written to this computer.")
                             : tr("Your settings, shortcuts, Recent files and folders, workspaces, bookmarks, pinned tools, tool history and annotation styles are replaced by the backup. Other open PDF4QT windows use the imported settings at once."));
    if (!m_hasNewCertificates && PDFAppStorageSettingsSectionHelper::hasRemovedCertificates(database, backup))
    {
        textParts << tr("Your trusted certificates are replaced by those of the backup, which adds no new ones.");
    }
    textParts << tr("This cannot be undone.");
    const QString text = textParts.join(QLatin1Char(' '));
    QString version = PDFDatabaseFiles::getVersionLabel(backup.getSchemaVersion());
    if (backup.getSchemaVersion() < backup.getApplicationSchemaVersion())
    {
        version = tr("%1 – from an older PDF4QT, updated when imported").arg(version);
    }
    const std::vector<PDFAppStorageDialog::Fact> facts = { { tr("Backup file"), fileName, true },
                                                           { tr("Created"), backup.getCreated().isValid() ? PDFAppLocale::formatDateTime(backup.getCreated()) : tr("Unknown"), false },
                                                           { tr("Version"), version, false } };

    PDFAppStorageDialog* dialog = new PDFAppStorageDialog(PDFThemeIcons::Badge::Warning, tr("Import settings from the backup?"), text, facts, tr("Import Settings"), window());

    // Owner 2026-10-25 (Q7): the security settings, which the backup weakens, "old → new"
    const std::vector<PDFAppStorageDialog::Fact> weakened = getWeakenedSettings(getContext(), backup);
    if (!weakened.empty())
    {
        dialog->addFacts(QStringLiteral("weakenedSettings"), tr("The backup weakens these security settings"), weakened);
    }

    // ... and its certificates are trusted only when the user asks for it (the check
    // box is off; unchecked = the current trusted certificates stay)
    if (m_hasNewCertificates)
    {
        const int count = int(newCertificates.size());
        dialog->addCheckBox(QStringLiteral("trustCertificatesCheckBox"), count == 1 ? tr("Also trust the certificate of the backup")
                                                                                    : tr("Also trust the %1 certificates of the backup").arg(count),
                            tr("Your current trusted certificates are kept unless you check this."));

        std::vector<PDFAppStorageDialog::Fact> certificateFacts;
        for (int i = 0; i < count && i < MAXIMUM_LISTED_CERTIFICATES; ++i)
        {
            const QByteArray& certificate = newCertificates[size_t(i)];
            const PDFAppCertificateEntry entry = PDFAppCertificateEntry::create(PDFAppCertificateEntry::Type::User, 0, certificate);
            certificateFacts.push_back({ tr("Certificate"), entry.name, false });
            certificateFacts.push_back({ tr("SHA-256"), PDFAppCertificateEntry::getFingerprint(certificate), false });
        }
        if (count > MAXIMUM_LISTED_CERTIFICATES)
        {
            const int rest = count - MAXIMUM_LISTED_CERTIFICATES;
            certificateFacts.push_back({ tr("More"), rest == 1 ? tr("1 more certificate") : tr("%1 more certificates").arg(rest), false });
        }
        dialog->addFacts(QStringLiteral("newCertificates"), QString(), certificateFacts);
    }

    m_pendingBackup = std::move(backup);
    openDialog(DialogKind::Import, dialog);
}

void PDFAppStorageSettingsSection::requestDelete(const QString& filePath)
{
    using Helper = PDFAppStorageSettingsSectionHelper;

    PDFDatabase* database = getDatabase();
    if (!database || filePath.isEmpty())
    {
        return;
    }

    // Only a file, which the section lists (the database decides again)
    std::optional<PDFDatabaseFileInfo> found;
    for (const PDFDatabaseFileInfo& file : database->getDatabaseFiles())
    {
        if (file.filePath == filePath && file.schemaVersion < database->getState().applicationSchemaVersion)
        {
            found = file;
            break;
        }
    }
    if (!found)
    {
        // The list is old: built again from the event loop (the button of the row,
        // which has called this, is deleted with its row)
        QMetaObject::invokeMethod(this, &PDFAppStorageSettingsSection::updateFiles, Qt::QueuedConnection);
        return;
    }

    // 1271:15230 with the version of PDF4QT instead of the schema (owner 2026-10-25)
    const QString text = tr("The settings and history of %1 are deleted. That version starts with the default settings. This cannot be undone.")
                             .arg(PDFDatabaseFiles::getVersionLabel(found->schemaVersion));
    const std::vector<PDFAppStorageDialog::Fact> facts = { { tr("File"), QFileInfo(found->filePath).fileName(), true },
                                                           { tr("Size"), Helper::formatSize(found->size), false },
                                                           { tr("Modified"), found->modified.isValid() ? PDFAppLocale::formatDateTime(found->modified) : tr("Unknown"), false } };

    m_pendingDeletePath = found->filePath;
    openDialog(DialogKind::Delete, new PDFAppStorageDialog(PDFThemeIcons::Badge::Warning, tr("Delete the older database file?"), text, facts, tr("Delete File"), window()));
}

void PDFAppStorageSettingsSection::showError(const QString& title, const QString& text, const std::vector<PDFAppStorageDialog::Fact>& facts)
{
    // 1271:15697: the error badge, one OK
    openDialog(DialogKind::Error, new PDFAppStorageDialog(PDFThemeIcons::Badge::Critical, title, text, facts, QString(), window()));
}

void PDFAppStorageSettingsSection::openDialog(DialogKind kind, PDFAppStorageDialog* dialog)
{
    // One dialog at a time: a new one replaces an open one (its action is not done)
    if (m_dialog && m_dialog != dialog)
    {
        disconnect(m_dialog.data(), nullptr, this, nullptr);
        m_dialog->reject();
    }

    m_dialogKind = kind;
    m_dialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, &PDFAppStorageSettingsSection::onDialogFinished);
    dialog->open();
}

void PDFAppStorageSettingsSection::closeDialog()
{
    // Deleted later: the section may go inside of an event delivered to the widgets
    // of the window (LanguageChange rebuilds the page; QWidget::event sends it to
    // a copy of the list of the children of the window), so the dialog - a child
    // of the window - deleted now would be used by that delivery after its
    // deletion (its scrim is deleted later by PDFMessageBox itself)
    if (m_dialog)
    {
        disconnect(m_dialog.data(), nullptr, this, nullptr);
        m_dialog->deleteLater();
        m_dialog = nullptr;
    }
    m_dialogKind = DialogKind::None;
    m_pendingDeletePath.clear();
    m_pendingBackup.reset();
    m_hasNewCertificates = false;
}

void PDFAppStorageSettingsSection::onDialogFinished(int result)
{
    const DialogKind kind = m_dialogKind;
    const QString deletePath = m_pendingDeletePath;
    std::optional<PDFDatabaseBackup> backup = std::move(m_pendingBackup);

    // Owner 2026-10-25 (Q7): the certificates of the backup only when the user has
    // checked "Also trust …"
    const QCheckBox* trustCheckBox = m_dialog ? m_dialog->getCheckBox() : nullptr;
    const bool isTrustChecked = m_hasNewCertificates && trustCheckBox && trustCheckBox->isChecked();
    m_dialogKind = DialogKind::None;
    m_pendingDeletePath.clear();
    m_pendingBackup.reset();
    m_hasNewCertificates = false;
    m_dialog = nullptr;

    PDFDatabase* database = getDatabase();
    if (result != QDialog::Accepted || !database)
    {
        return;
    }

    switch (kind)
    {
        case DialogKind::Delete:
        {
            const PDFDatabaseError error = database->deleteOlderDatabaseFile(deletePath);
            updateFiles();
            if (error.isError())
            {
                showError(tr("The file could not be deleted"), error.getMessage(), { { tr("File"), QFileInfo(deletePath).fileName(), true } });
            }
            break;
        }

        case DialogKind::Import:
        {
            if (!backup)
            {
                break;
            }

            // A backup without a new certificate adds no trust, its rows replace the
            // current ones as everything else. The new certificates are found again:
            // the trusted certificates may have changed while the dialog was open (a
            // certificate of the backup removed by another instance) - unchecked, no
            // trust is added.
            const PDFDatabaseImportCertificates certificates = (isTrustChecked || getNewCertificates(database, *backup).empty())
                                                                   ? PDFDatabaseImportCertificates::FromBackup
                                                                   : PDFDatabaseImportCertificates::KeepCurrent;

            // Every page (also this one) follows the notification of the database
            QPointer<PDFAppStorageSettingsSection> guard(this);
            const PDFDatabaseError error = database->importBackup(*backup, certificates);
            if (guard && error.isError())
            {
                showError(tr("The file could not be imported"), tr("%1 Nothing was changed.").arg(error.getMessage()),
                          { { tr("File"), QFileInfo(backup->getFilePath()).fileName(), true } });
            }
            else if (guard)
            {
                // The imported settings carry the folder of the computer of the backup
                storeBackupFolder(backup->getFilePath());
            }
            break;
        }

        case DialogKind::None:
        case DialogKind::Error:
            break;
    }
}

void PDFAppStorageSettingsSection::onDatabaseStateChanged()
{
    updateState();
    updateFiles();
}

PDFAppSettingsSection* PDFAppStorageSettingsSectionFactory::createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const
{
    return new PDFAppStorageSettingsSection(context, sectionId, parent);
}

}   // namespace pdf
