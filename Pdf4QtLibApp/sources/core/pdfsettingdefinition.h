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

#ifndef PDFSETTINGDEFINITION_H
#define PDFSETTINGDEFINITION_H

#include "pdfappglobal.h"

#include <QString>
#include <QStringList>
#include <QVariant>

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

class PDFSettingDefinition;

/// Option of a setting with options (PDFSettingDefinition::Type::Option)
class PDF4QTLIBAPPSHARED_EXPORT PDFSettingOption
{
public:
    /// Stored value of the option (stable, English, lowercase)
    QString id;

    /// Title of the option. For built-in options it is a source text translated
    /// in the context PDFSettingDefinition::TRANSLATION_CONTEXT, an options provider
    /// gives a ready text (name of a voice, of a profile) and sets isTranslated.
    QString title;

    /// Title is already translated (or a name, which is not translated)
    bool isTranslated = false;

    /// Translated title
    QString getTitle() const;
};

/// Provider of the options, which depend on the system (voices, languages of the
/// speech, ICC profiles, languages of the user interface; newapp_storage.md 4).
/// A stored value, which the system does not offer, is treated as not available:
/// the setting has its default value and the page shows "(not available)".
class PDF4QTLIBAPPSHARED_EXPORT PDFSettingOptionsProvider
{
public:
    virtual ~PDFSettingOptionsProvider();

    /// Options of the setting (called each time the options are needed - no cache)
    virtual std::vector<PDFSettingOption> getOptions(const PDFSettingDefinition& definition) const = 0;
};

/// Typed definition of one setting (newapp_storage.md 4). The definition is the only
/// source of the Settings page, of the global search, of the validation (a stored
/// value of another type or out of the range = the default value) and of Restore
/// defaults. Values of the settings are stored in the table setting only, when they
/// differ from the default.
///
/// Texts are source texts in English translated in the context TRANSLATION_CONTEXT
/// (mark them by QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "...")).
class PDF4QTLIBAPPSHARED_EXPORT PDFSettingDefinition
{
public:
    enum class Type
    {
        Bool,       ///< bool, stored as an integer 0 / 1
        Integer,    ///< qint64 in [minimum, maximum]
        Real,       ///< double in [minimum, maximum], finite
        String,     ///< QString of at most maximumLength characters
        Option      ///< QString, id of one of the options (static or of the provider)
    };

    /// Control of the row of the Settings page (presentation hint, step 5). Default
    /// is chosen by the type: Bool = check box, Option = combo box, Integer = spin
    /// box, Real = spin box of real numbers, String = text field.
    enum class Control
    {
        Default,    ///< The control of the type
        Slider,     ///< Slider with the value text (Integer, Real)
        Color,      ///< Color button, the value is "#RRGGBB" (String)
        Path,       ///< Text field with the button Browse… of a folder (String)
        Segmented   ///< Segmented control of the options (Option, a few short titles; the permissions of the page AI 955:143435)
    };

    /// Translation context of the texts of the definitions
    static constexpr const char* TRANSLATION_CONTEXT = "pdf::PDFSettingDefinitions";

    /// Maximal length of a string value
    static constexpr qsizetype DEFAULT_MAXIMUM_LENGTH = 64 * 1024;

    PDFSettingDefinition() = default;

    static PDFSettingDefinition createBool(QString key, bool defaultValue);
    static PDFSettingDefinition createInteger(QString key, qint64 defaultValue, qint64 minimum, qint64 maximum);
    static PDFSettingDefinition createReal(QString key, double defaultValue, double minimum, double maximum);
    static PDFSettingDefinition createString(QString key, QString defaultValue, qsizetype maximumLength = DEFAULT_MAXIMUM_LENGTH);
    static PDFSettingDefinition createOption(QString key, QString defaultValue, std::vector<PDFSettingOption> options);
    static PDFSettingDefinition createOption(QString key, QString defaultValue, std::shared_ptr<const PDFSettingOptionsProvider> provider);

    /// Builder functions (return the definition, so they can be chained)
    PDFSettingDefinition& setSection(QString section, QString group = QString());
    PDFSettingDefinition& setAnchor(QString anchor);
    PDFSettingDefinition& setTitle(const char* title);
    PDFSettingDefinition& setDescription(const char* description);
    PDFSettingDefinition& setKeywords(QStringList keywords);

    /// Presentation hints of the Settings page (step 5); the defaults keep the
    /// behavior of a definition without them.
    /// Unit shown after the value of a number (source text: "%", "px", "×", "kB")
    PDFSettingDefinition& setUnit(const char* unit);
    /// Decimals of a real number (spin box, slider); negative = the default (2)
    PDFSettingDefinition& setDecimals(int decimals);
    /// Control of the row (Control::Default = the control of the type)
    PDFSettingDefinition& setControl(Control control);
    /// The row is enabled only when the setting \p key has the value \p value
    /// (Custom author name only for the author mode Custom name)
    PDFSettingDefinition& setEnabledWhen(QString key, QVariant value);
    /// Tag shown beside the title of the row (source text, "Restart required")
    PDFSettingDefinition& setTag(const char* tag);
    /// A change is expensive (the language rebuilds the whole user interface, owner
    /// 2026-10-25): the combo box of an Option writes only a choice activated in its
    /// open list (a click, Enter) or by Enter on the closed combo box, not every
    /// step of the arrows or of a typed letter on the closed combo box
    PDFSettingDefinition& setCommitOnActivation();

    /// Key "section.name" (for example appearance.colorScheme)
    const QString& getKey() const { return m_key; }
    Type getType() const { return m_type; }
    const QVariant& getDefaultValue() const { return m_defaultValue; }
    const QVariant& getMinimum() const { return m_minimum; }
    const QVariant& getMaximum() const { return m_maximum; }
    qsizetype getMaximumLength() const { return m_maximumLength; }

    /// Section of the Settings page (route /settings/{section}); empty = internal
    /// state, which is not shown in Settings, not searched and not restored
    const QString& getSection() const { return m_section; }

    /// Group inside of the section (source text, for example "Updates")
    const QString& getGroup() const { return m_group; }

    /// Anchor of the row (/settings/{section}#{anchor}, search)
    const QString& getAnchor() const { return m_anchor; }

    /// Source texts and their translations
    const char* getTitleSource() const { return m_title; }
    const char* getDescriptionSource() const { return m_description; }
    QString getTitle() const;
    QString getDescription() const;
    QString getGroupTitle() const;

    /// Keywords for the search (English, lowercase)
    const QStringList& getKeywords() const { return m_keywords; }

    /// Presentation hints (setUnit, setDecimals, setControl, setEnabledWhen, setTag)
    const char* getUnitSource() const { return m_unit; }
    QString getUnit() const;
    /// Decimals of a real number (2, when no hint is set; 0 for other types)
    int getDecimals() const;
    Control getControl() const { return m_control; }
    bool hasEnabledWhen() const { return !m_enabledWhenKey.isEmpty(); }
    const QString& getEnabledWhenKey() const { return m_enabledWhenKey; }
    const QVariant& getEnabledWhenValue() const { return m_enabledWhenValue; }
    const char* getTagSource() const { return m_tag; }
    QString getTag() const;
    bool isCommittedOnActivation() const { return m_isCommittedOnActivation; }

    /// Options of an Option setting (static ones or of the provider)
    std::vector<PDFSettingOption> getOptions() const;

    /// The options are given by a provider (system dependent)
    bool hasOptionsProvider() const { return m_provider != nullptr; }

    /// Setting is shown in Settings
    bool isVisible() const { return !m_section.isEmpty(); }

    /// Returns the value converted to the type of the setting, or nothing, if the
    /// value is not valid (other type, out of the range, unknown option)
    std::optional<QVariant> normalize(const QVariant& value) const;

    /// Returns the valid value: the normalized \p storedValue, or the default value
    QVariant validate(const QVariant& storedValue) const;

    /// The definition is complete (key format, valid default value)
    bool isValid() const;

    /// Key has the form section.name
    static bool isValidKey(const QString& key);

private:
    static PDFSettingDefinition create(QString key, Type type, QVariant defaultValue);

    QString m_key;
    Type m_type = Type::Bool;
    QVariant m_defaultValue;
    QVariant m_minimum;
    QVariant m_maximum;
    qsizetype m_maximumLength = DEFAULT_MAXIMUM_LENGTH;
    std::vector<PDFSettingOption> m_options;
    std::shared_ptr<const PDFSettingOptionsProvider> m_provider;
    QString m_section;
    QString m_group;
    QString m_anchor;
    const char* m_title = "";
    const char* m_description = "";
    QStringList m_keywords;
    const char* m_unit = "";
    int m_decimals = -1;
    Control m_control = Control::Default;
    QString m_enabledWhenKey;
    QVariant m_enabledWhenValue;
    const char* m_tag = "";
    bool m_isCommittedOnActivation = false;
};

/// Registry of the definitions of the settings. Definitions are registered at the
/// start (PDFSettingDefinitions::registerBuiltinDefinitions), pointers to them stay
/// valid for the lifetime of the registry.
class PDF4QTLIBAPPSHARED_EXPORT PDFSettingRegistry
{
public:
    PDFSettingRegistry() = default;

    PDFSettingRegistry(const PDFSettingRegistry&) = delete;
    PDFSettingRegistry& operator=(const PDFSettingRegistry&) = delete;

    /// Registers the definition. \returns false for an invalid definition or a key,
    /// which is already registered (the first definition is kept)
    bool registerDefinition(PDFSettingDefinition definition);

    /// Returns the definition of the key, or nullptr
    const PDFSettingDefinition* findDefinition(const QString& key) const;

    /// All definitions in the order of the registration
    std::vector<const PDFSettingDefinition*> getDefinitions() const;

    /// Definitions of the section of Settings (route id, e.g. "general")
    std::vector<const PDFSettingDefinition*> getSectionDefinitions(const QString& section) const;

    /// Sections, which have a definition, in the order of their first definition
    QStringList getSections() const;

private:
    std::deque<PDFSettingDefinition> m_definitions;
    std::map<QString, size_t> m_index;
};

}   // namespace pdf

#endif // PDFSETTINGDEFINITION_H
