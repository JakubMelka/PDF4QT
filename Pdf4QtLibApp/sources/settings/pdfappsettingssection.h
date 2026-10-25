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

#ifndef PDFAPPSETTINGSSECTION_H
#define PDFAPPSETTINGSSECTION_H

#include "pdfappglobal.h"
#include "pdfappcontext.h"
#include "pdfdatabase.h"
#include "pdfapproute.h"

#include <QList>
#include <QVBoxLayout>
#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <memory>
#include <vector>

namespace pdf
{

class PDFAppContext;
class PDFAppSettingGroup;
class PDFAppSettingRow;
class PDFAppSettingsSection;
class PDFSearchQuery;
class PDFSettingDefinition;

/// A changed value of a section listed by Restore defaults (941:135918): the
/// title of the setting and its value now and after the restore ("3.0 × → 2.0 ×")
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingChange
{
public:
    QString key;
    QString title;
    QString currentValue;
    QString defaultValue;
};

/// Content of a section of Settings (plan 2.1): the groups and the rows of one
/// section, shown by PDFAppSettingsPage below its section header. The section
/// is created by its factory (PDFAppSettingsSectionRegistry) and initialized at
/// once (initialize calls the virtual buildContent). The page asks it for the
/// changed values (Restore defaults), the anchors, the filter of /settings?q= and
/// gives it the changes of the database (refresh).
///
/// The base keeps the groups (PDFAppSettingGroup) in a vertical layout with the
/// gap 24 of the design; a package subclasses PDFAppGenericSettingsSection, which
/// builds the generic rows of the definitions, and adds its own rows and groups
/// in finishContent.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingsSection : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Gap between the groups (content-column 146:3131)
    static constexpr int GROUP_SPACING = 24;

    explicit PDFAppSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent = nullptr);
    virtual ~PDFAppSettingsSection() override;

    /// Builds the content once (the factory calls it after the construction)
    void initialize();
    bool isInitialized() const { return m_isInitialized; }

    const QString& getSectionId() const { return m_sectionId; }
    PDFAppContext* getContext() const { return m_context; }

    /// Icon, title and subtitle of the section header (the registry by default)
    virtual QString getIconName() const;
    virtual QString getTitle() const;
    virtual QString getSubtitle() const;

    /// The section has the button Restore defaults (Storage has none)
    virtual bool isRestoreSupported() const;

    /// Keys of the changed settings of the section (the button is disabled without them)
    virtual QStringList getChangedKeys() const;

    /// Rows of the dialog Restore defaults: the changed settings with the current
    /// and the default value
    virtual std::vector<PDFAppSettingChange> getChangedValues() const;

    /// Text of the dialog Restore defaults for \p count changed settings
    virtual QString getRestoreDefaultsText(int count) const;

    /// Restores the defaults of the section (one transaction)
    virtual PDFDatabaseError restoreDefaults();

    /// Topics of PDFDatabaseNotifier, which can change the changed values of the
    /// section (the button Restore defaults and the rows of its dialog read them
    /// again only for these topics; the settings by default, Shortcuts its shortcuts)
    virtual QStringList getRestoreTopics() const;

    /// A change of the topic (empty = everything) can change the changed values
    bool isRestoreTopic(const QString& topic) const;

    /// Widget of the anchor (a row), nullptr for an unknown anchor
    virtual QWidget* findAnchorWidget(const QString& anchor) const;

    /// Filter of /settings?q=: shows the rows matching the query (an empty query
    /// shows everything). A query matching the title or the keywords of the section
    /// shows the whole section. \returns Number of the matches (0 = the section is
    /// not shown in the filtered page)
    virtual int applyFilter(const PDFSearchQuery& query);

    /// The database has changed (topic and key of PDFDatabaseNotifier; both empty =
    /// everything): the rows show the stored values. A package overrides it for
    /// its own topics (tool_pair, trusted_certificate, shortcut).
    virtual void refresh(const QString& topic, const QString& key);

    /// Writes the pending values of the controls (the page is hidden or left)
    virtual void commitPendingValues();

    /// Ends the activity of the section, which may notify others (the sample of
    /// the speech) - from the event loop, before the page is destroyed by its
    /// deferred deletion, so nothing is emitted from a destructor
    virtual void finishActivity();

    /// Groups and rows in the order of the section
    QList<PDFAppSettingGroup*> getGroups() const;
    QList<PDFAppSettingRow*> getRows() const;

    /// Group of the key (the source text of the group of its definitions)
    PDFAppSettingGroup* findGroup(const QString& key) const;

    /// Row of the anchor (nullptr = none)
    PDFAppSettingRow* findRow(const QString& anchor) const;

    /// Adds a group before \p before (nullptr = at the end)
    PDFAppSettingGroup* addGroup(const QString& key, const QString& title, const QString& hint, PDFAppSettingGroup* before = nullptr);

    /// Layout of the groups
    QVBoxLayout* getContentLayout() const { return m_layout; }

Q_SIGNALS:
    /// A value could not be stored: the page shows the message
    void statusMessageRequested(const QString& message);

    /// The section asks for a route (a link of a custom row)
    void navigationRequested(const pdf::PDFAppRoute& route);

protected:
    /// Builds the groups and the rows
    virtual void buildContent() = 0;

    /// Connects the signals of a row of a definition (write failures)
    void connectRow(PDFAppSettingRow* row);

private:
    void onWriteFailed(const QString& message);

    QPointer<PDFAppContext> m_context;
    QString m_sectionId;
    QPointer<QVBoxLayout> m_layout;
    bool m_isInitialized = false;
};

/// Section of the generic rows (Viewing, Editing, Advanced and the base of the
/// sections of the packages): one group per group of the definitions of the
/// section (PDFSettingRegistry::getSectionDefinitions, in the order of the first
/// definition of each group), one row per definition. A subclass changes a row
/// by createRow and adds its own content in finishContent.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppGenericSettingsSection : public PDFAppSettingsSection
{
    Q_OBJECT

private:
    using BaseClass = PDFAppSettingsSection;

public:
    explicit PDFAppGenericSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent = nullptr);
    virtual ~PDFAppGenericSettingsSection() override;

protected:
    virtual void buildContent() override;

    /// Row of the definition (the generic row by default; nullptr = no row)
    virtual PDFAppSettingRow* createRow(const PDFSettingDefinition& definition);

    /// Content of a package after the generic rows (nothing by default)
    virtual void finishContent();
};

/// Factory of the content of a section (one class per section, no lambdas)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingsSectionFactory
{
public:
    PDFAppSettingsSectionFactory() = default;
    virtual ~PDFAppSettingsSectionFactory();

    PDFAppSettingsSectionFactory(const PDFAppSettingsSectionFactory&) = delete;
    PDFAppSettingsSectionFactory& operator=(const PDFAppSettingsSectionFactory&) = delete;

    /// Creates the section (not initialized yet - the registry initializes it)
    virtual PDFAppSettingsSection* createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const = 0;
};

/// Factory of the generic section (Viewing, Editing, Advanced)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppGenericSettingsSectionFactory : public PDFAppSettingsSectionFactory
{
public:
    virtual PDFAppSettingsSection* createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const override;
};

/// Section of the navigation of Settings (Settings nav item 145:2879)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingsSectionInfo
{
public:
    QString id;                     ///< Route id (/settings/{id})
    QString iconName;               ///< Icon of the nav item and of the section header
    const char* title = "";         ///< Source text of the title ("General")
    const char* navigationSubtitle = "";    ///< Summary of the nav item ("Language, theme, documents")
    const char* subtitle = "";      ///< Subtitle of the section header
    QStringList keywords;           ///< Words of the filter (English)
    std::shared_ptr<const PDFAppSettingsSectionFactory> factory;

    QString getTitle() const;
    QString getNavigationSubtitle() const;
    QString getSubtitle() const;
};

/// Sections of Settings in the order of the navigation (146:2843: General …
/// Advanced; Storage below Advanced - owner decision Q1) with their factories. The
/// only place, which knows the sections: the page, its navigation, the route table
/// test and the global search take them from here.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingsSectionRegistry
{
public:
    PDFAppSettingsSectionRegistry() = delete;

    static constexpr const char* TRANSLATION_CONTEXT = "pdf::PDFAppSettingsSectionRegistry";

    static const std::vector<PDFAppSettingsSectionInfo>& getSections();
    static const PDFAppSettingsSectionInfo* findSection(const QString& id);
    static QStringList getSectionIds();

    /// Creates and initializes the section (nullptr for an unknown id)
    static PDFAppSettingsSection* createSection(PDFAppContext* context, const QString& id, QWidget* parent);

    /// Hint of a group of the definitions (the trailing text of the group header:
    /// "Stored in the document, visible to anyone who opens it"); empty = none
    static QString getGroupHint(const QString& groupKey);

private:
    static std::vector<PDFAppSettingsSectionInfo> createSections();
};

}   // namespace pdf

#endif // PDFAPPSETTINGSSECTION_H
