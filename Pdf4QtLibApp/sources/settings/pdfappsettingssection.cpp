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

#include "pdfappsettingssection.h"
#include "pdfappcontext.h"
#include "pdfappsettingcontrol.h"
#include "pdfappsettingrow.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfappsettingssection_colormanagement.h"
#include "pdfappsettingssection_colormodes.h"
#include "pdfappsettingssection_general.h"
#include "pdfappsettingssection_security.h"
#include "pdfappsettingssection_shortcuts.h"
#include "pdfappsettingssection_speech.h"
#include "pdfappsettingssection_storage.h"

#include "pdfdocumenttoolsearch.h"

#include <QCoreApplication>
#include <QVBoxLayout>

namespace pdf
{

PDFAppSettingsSection::PDFAppSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) :
    BaseClass(parent),
    m_context(context),
    m_sectionId(sectionId)
{
    setObjectName(QStringLiteral("section-%1").arg(sectionId));
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(GROUP_SPACING);
}

PDFAppSettingsSection::~PDFAppSettingsSection() = default;

void PDFAppSettingsSection::initialize()
{
    if (m_isInitialized)
    {
        return;
    }

    m_isInitialized = true;
    buildContent();
    applyFilter(PDFSearchQuery());
}

QString PDFAppSettingsSection::getIconName() const
{
    const PDFAppSettingsSectionInfo* info = PDFAppSettingsSectionRegistry::findSection(m_sectionId);
    return info ? info->iconName : QStringLiteral("settings");
}

QString PDFAppSettingsSection::getTitle() const
{
    const PDFAppSettingsSectionInfo* info = PDFAppSettingsSectionRegistry::findSection(m_sectionId);
    return info ? info->getTitle() : m_sectionId;
}

QString PDFAppSettingsSection::getSubtitle() const
{
    const PDFAppSettingsSectionInfo* info = PDFAppSettingsSectionRegistry::findSection(m_sectionId);
    return info ? info->getSubtitle() : QString();
}

bool PDFAppSettingsSection::isRestoreSupported() const
{
    return true;
}

QStringList PDFAppSettingsSection::getChangedKeys() const
{
    if (!m_context || !m_context->getDatabase())
    {
        return QStringList();
    }
    return m_context->getSettings().getChangedKeys(m_sectionId);
}

std::vector<PDFAppSettingChange> PDFAppSettingsSection::getChangedValues() const
{
    std::vector<PDFAppSettingChange> changes;
    if (!m_context || !m_context->getDatabase())
    {
        return changes;
    }

    const PDFSettingsRepository settings = m_context->getSettings();
    for (const QString& key : getChangedKeys())
    {
        const PDFSettingDefinition* definition = m_context->getSettingRegistry()->findDefinition(key);
        if (!definition)
        {
            continue;
        }

        PDFAppSettingChange change;
        change.key = key;
        change.title = definition->getTitle().isEmpty() ? key : definition->getTitle();
        change.currentValue = PDFAppSettingControl::formatValue(*definition, settings.getValue(key));
        change.defaultValue = PDFAppSettingControl::formatValue(*definition, definition->getDefaultValue());
        changes.push_back(std::move(change));
    }
    return changes;
}

QString PDFAppSettingsSection::getRestoreDefaultsText(int count) const
{
    if (count == 1)
    {
        return tr("This setting of the %1 section goes back to its default value. The other sections, your history and your shortcuts stay as they are.").arg(getTitle());
    }
    // %Ln: the plural forms of the language and the number of its locale (O17)
    return tr("These %Ln settings of the %1 section go back to their default values. The other sections, your history and your shortcuts stay as they are.", nullptr, count).arg(getTitle());
}

PDFDatabaseError PDFAppSettingsSection::restoreDefaults()
{
    if (!m_context || !m_context->getDatabase())
    {
        return PDFDatabaseError::fromMessage(tr("The settings are not available."));
    }

    // A value being edited is written first, so it is restored too (the slots of
    // the written value may delete the section with its page)
    QPointer<PDFAppSettingsSection> guard(this);
    QPointer<PDFAppContext> context = m_context;
    const QString sectionId = m_sectionId;
    commitPendingValues();
    if (!guard || !context || !context->getDatabase())
    {
        return PDFDatabaseError::fromMessage(tr("The settings are not available."));
    }
    return context->getSettings().restoreDefaults(sectionId);
}

QStringList PDFAppSettingsSection::getRestoreTopics() const
{
    return { PDFSettingsRepository::getTopic() };
}

bool PDFAppSettingsSection::isRestoreTopic(const QString& topic) const
{
    return topic.isEmpty() || getRestoreTopics().contains(topic);
}

QWidget* PDFAppSettingsSection::findAnchorWidget(const QString& anchor) const
{
    return findRow(anchor);
}

int PDFAppSettingsSection::applyFilter(const PDFSearchQuery& query)
{
    // A query of the title or the keywords of the section shows all of it
    bool isSectionMatch = query.isEmpty();
    if (!isSectionMatch)
    {
        QStringList titles = { getTitle() };
        QStringList keywords;
        if (const PDFAppSettingsSectionInfo* info = PDFAppSettingsSectionRegistry::findSection(m_sectionId))
        {
            titles << QString::fromUtf8(info->title);
            keywords = info->keywords;
        }
        const std::vector<PDFSearchField> fields = { PDFSearchField(titles, 100, 90, 60, true), PDFSearchField(keywords, 50, 45, 30) };
        isSectionMatch = query.match(fields).has_value();
    }

    int matches = 0;
    for (PDFAppSettingGroup* group : getGroups())
    {
        matches += group->applyFilter(query, isSectionMatch);
    }

    if (isSectionMatch && !query.isEmpty())
    {
        return qMax(1, matches);
    }
    return matches;
}

void PDFAppSettingsSection::refresh(const QString& topic, const QString& key)
{
    if (!topic.isEmpty() && topic != PDFSettingsRepository::getTopic())
    {
        return;
    }

    for (PDFAppSettingRow* row : getRows())
    {
        row->updateFromSettings(topic.isEmpty() ? QString() : key);
    }
}

void PDFAppSettingsSection::commitPendingValues()
{
    // A slot of a write failure may delete the page: the rows are guarded
    QList<QPointer<PDFAppSettingRow>> rows;
    for (PDFAppSettingRow* row : getRows())
    {
        rows.append(row);
    }

    for (const QPointer<PDFAppSettingRow>& row : rows)
    {
        if (row)
        {
            row->commitPendingValue();
        }
    }
}

void PDFAppSettingsSection::finishActivity()
{
    // A section without an activity of its own
}

QList<PDFAppSettingGroup*> PDFAppSettingsSection::getGroups() const
{
    QList<PDFAppSettingGroup*> groups;
    for (int i = 0; m_layout && i < m_layout->count(); ++i)
    {
        if (PDFAppSettingGroup* group = qobject_cast<PDFAppSettingGroup*>(m_layout->itemAt(i)->widget()))
        {
            groups.append(group);
        }
    }
    return groups;
}

QList<PDFAppSettingRow*> PDFAppSettingsSection::getRows() const
{
    QList<PDFAppSettingRow*> rows;
    for (PDFAppSettingGroup* group : getGroups())
    {
        rows << group->getRows();
    }
    return rows;
}

PDFAppSettingGroup* PDFAppSettingsSection::findGroup(const QString& key) const
{
    for (PDFAppSettingGroup* group : getGroups())
    {
        if (group->getKey() == key)
        {
            return group;
        }
    }
    return nullptr;
}

PDFAppSettingRow* PDFAppSettingsSection::findRow(const QString& anchor) const
{
    if (anchor.isEmpty())
    {
        return nullptr;
    }

    for (PDFAppSettingRow* row : getRows())
    {
        if (row->getAnchor() == anchor)
        {
            return row;
        }
    }
    return nullptr;
}

PDFAppSettingGroup* PDFAppSettingsSection::addGroup(const QString& key, const QString& title, const QString& hint, PDFAppSettingGroup* before)
{
    if (!m_layout)
    {
        return nullptr;
    }

    PDFAppSettingGroup* group = new PDFAppSettingGroup(key, title, hint, this);
    const int index = before ? m_layout->indexOf(before) : -1;
    m_layout->insertWidget(index < 0 ? m_layout->count() : index, group);
    return group;
}

void PDFAppSettingsSection::connectRow(PDFAppSettingRow* row)
{
    if (row && row->getControl())
    {
        connect(row->getControl(), &PDFAppSettingControl::writeFailed, this, &PDFAppSettingsSection::onWriteFailed);
    }
}

void PDFAppSettingsSection::onWriteFailed(const QString& message)
{
    Q_EMIT statusMessageRequested(tr("The setting could not be saved: %1").arg(message));
}

PDFAppGenericSettingsSection::PDFAppGenericSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) :
    BaseClass(context, sectionId, parent)
{

}

PDFAppGenericSettingsSection::~PDFAppGenericSettingsSection() = default;

void PDFAppGenericSettingsSection::buildContent()
{
    PDFAppContext* context = getContext();
    if (context)
    {
        // Groups in the order of their first definition, rows in the order of the registration
        for (const PDFSettingDefinition* definition : context->getSettingRegistry()->getSectionDefinitions(getSectionId()))
        {
            PDFAppSettingGroup* group = findGroup(definition->getGroup());
            if (!group)
            {
                group = addGroup(definition->getGroup(), definition->getGroupTitle(), PDFAppSettingsSectionRegistry::getGroupHint(definition->getGroup()));
            }

            if (PDFAppSettingRow* row = createRow(*definition))
            {
                group->addRow(row);
                connectRow(row);
            }
        }
    }

    finishContent();
}

PDFAppSettingRow* PDFAppGenericSettingsSection::createRow(const PDFSettingDefinition& definition)
{
    return new PDFAppSettingRow(getContext(), definition);
}

void PDFAppGenericSettingsSection::finishContent()
{

}

PDFAppSettingsSectionFactory::~PDFAppSettingsSectionFactory() = default;

PDFAppSettingsSection* PDFAppGenericSettingsSectionFactory::createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const
{
    return new PDFAppGenericSettingsSection(context, sectionId, parent);
}

QString PDFAppSettingsSectionInfo::getTitle() const
{
    return QCoreApplication::translate(PDFAppSettingsSectionRegistry::TRANSLATION_CONTEXT, title);
}

QString PDFAppSettingsSectionInfo::getNavigationSubtitle() const
{
    return QCoreApplication::translate(PDFAppSettingsSectionRegistry::TRANSLATION_CONTEXT, navigationSubtitle);
}

QString PDFAppSettingsSectionInfo::getSubtitle() const
{
    return QCoreApplication::translate(PDFAppSettingsSectionRegistry::TRANSLATION_CONTEXT, subtitle);
}

std::vector<PDFAppSettingsSectionInfo> PDFAppSettingsSectionRegistry::createSections()
{
    using Definitions = PDFSettingDefinitions;

    struct Section
    {
        QString id;
        const char* iconName;
        const char* title;
        const char* navigationSubtitle;
        const char* subtitle;
        QStringList keywords;
        std::shared_ptr<const PDFAppSettingsSectionFactory> factory;
    };

    // Nav items and section headers of 146:2843 … 147:4565 (texts of the frames)
    const std::shared_ptr<const PDFAppSettingsSectionFactory> generic = std::make_shared<PDFAppGenericSettingsSectionFactory>();
    const Section sections[] =
    {
        { Definitions::sectionGeneral(), "settings", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "General"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Language, theme, documents"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Language, appearance and how documents open."),
          { QStringLiteral("general"), QStringLiteral("appearance"), QStringLiteral("theme") }, std::make_shared<PDFAppGeneralSettingsSectionFactory>() },
        { Definitions::sectionViewing(), "eye", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Viewing"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Page display, scrolling, magnifier"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "How pages are drawn, scrolled and magnified."),
          { QStringLiteral("view"), QStringLiteral("display"), QStringLiteral("rendering") }, generic },
        { Definitions::sectionColorModes(), "droplet", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Color modes"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Reading colors, contrast"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "How pages are recolored for comfortable reading."),
          { QStringLiteral("dark mode"), QStringLiteral("night"), QStringLiteral("reading") }, std::make_shared<PDFAppColorModesSettingsSectionFactory>() },
        { Definitions::sectionEditing(), "edit", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Editing"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Undo, author, forms"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Undo history, your name in annotations and form behavior."),
          { QStringLiteral("edit"), QStringLiteral("annotations"), QStringLiteral("forms") }, generic },
        { Definitions::sectionSecurity(), "lock", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Security"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Links, signatures, certificates"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "What documents may launch, and how signatures are verified."),
          { QStringLiteral("security"), QStringLiteral("signatures"), QStringLiteral("certificates") }, std::make_shared<PDFAppSecuritySettingsSectionFactory>() },
        { Definitions::sectionSpeech(), "volume-2", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Speech"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Voice for reading aloud"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Voice used by Read Aloud."),
          { QStringLiteral("read aloud"), QStringLiteral("text to speech"), QStringLiteral("voice") }, std::make_shared<PDFAppSpeechSettingsSectionFactory>() },
        { Definitions::sectionShortcuts(), "keyboard", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Shortcuts"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Keyboard shortcuts"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Keyboard shortcuts of all actions."),
          { QStringLiteral("keyboard"), QStringLiteral("shortcuts"), QStringLiteral("keys"), QStringLiteral("hotkeys") }, std::make_shared<PDFAppShortcutsSettingsSectionFactory>() },
        { Definitions::sectionColorManagement(), "monitor", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Color management"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Color engine, ICC profiles"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Color engine and ICC profiles used to convert document colors."),
          { QStringLiteral("cms"), QStringLiteral("icc"), QStringLiteral("profiles") }, std::make_shared<PDFAppColorManagementSettingsSectionFactory>() },
        { Definitions::sectionAdvanced(), "sliders", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Advanced"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Engine, cache, developer"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Rendering engine, memory and developer options."),
          { QStringLiteral("advanced"), QStringLiteral("performance"), QStringLiteral("developer") }, generic },
        // Owner decision Q1: Storage below Advanced (1269:14388, package D)
        { Definitions::sectionStorage(), "database", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Storage"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Database, backup, other files"),
          QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Where PDF4QT keeps your settings and history."),
          { QStringLiteral("database"), QStringLiteral("backup"), QStringLiteral("export"), QStringLiteral("import"), QStringLiteral("storage") },
          std::make_shared<PDFAppStorageSettingsSectionFactory>() }
    };

    std::vector<PDFAppSettingsSectionInfo> result;
    for (const Section& section : sections)
    {
        PDFAppSettingsSectionInfo info;
        info.id = section.id;
        info.iconName = QString::fromLatin1(section.iconName);
        info.title = section.title;
        info.navigationSubtitle = section.navigationSubtitle;
        info.subtitle = section.subtitle;
        info.keywords = section.keywords;
        info.factory = section.factory;
        result.push_back(std::move(info));
    }
    return result;
}

const std::vector<PDFAppSettingsSectionInfo>& PDFAppSettingsSectionRegistry::getSections()
{
    static const std::vector<PDFAppSettingsSectionInfo> sections = createSections();
    return sections;
}

const PDFAppSettingsSectionInfo* PDFAppSettingsSectionRegistry::findSection(const QString& id)
{
    for (const PDFAppSettingsSectionInfo& info : getSections())
    {
        if (info.id == id)
        {
            return &info;
        }
    }
    return nullptr;
}

QStringList PDFAppSettingsSectionRegistry::getSectionIds()
{
    QStringList ids;
    for (const PDFAppSettingsSectionInfo& info : getSections())
    {
        ids << info.id;
    }
    return ids;
}

PDFAppSettingsSection* PDFAppSettingsSectionRegistry::createSection(PDFAppContext* context, const QString& id, QWidget* parent)
{
    const PDFAppSettingsSectionInfo* info = findSection(id);
    if (!info || !info->factory)
    {
        return nullptr;
    }

    PDFAppSettingsSection* section = info->factory->createSection(context, id, parent);
    if (section)
    {
        section->initialize();
    }
    return section;
}

QString PDFAppSettingsSectionRegistry::getGroupHint(const QString& groupKey)
{
    // Subtitles of the groups of the frames (Editing 146:3887, Advanced 147:4907,
    // Color modes 160:4346 / 160:4412, Security 147:3781, Shortcuts 147:4434)
    static const std::pair<const char*, const char*> hints[] =
    {
        { "Author identity", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Stored in the document, visible to anyone who opens it") },
        { "Shading", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Quality of gradients and mesh shadings") },
        { "Color modes", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "How a page looks in each mode") },
        { "Custom colors", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Page and text color of the Custom colors mode") },
        { "Trusted certificates", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Certificates you trust in addition to the system store") },
        { "Keyboard shortcuts", QT_TRANSLATE_NOOP("pdf::PDFAppSettingsSectionRegistry", "Click a shortcut to record a new one") }
    };

    for (const auto& [key, hint] : hints)
    {
        if (groupKey == QLatin1String(key))
        {
            return QCoreApplication::translate(TRANSLATION_CONTEXT, hint);
        }
    }
    return QString();
}

}   // namespace pdf
