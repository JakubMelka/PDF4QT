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

#ifndef PDFSETTINGSREPOSITORY_H
#define PDFSETTINGSREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QPointer>
#include <QStringList>
#include <QVariant>

namespace pdf
{

class PDFSettingRegistry;
class PDFSettingDefinition;

/// Values of the settings (table setting, newapp_storage.md 4). Every read goes to the
/// database (no cache). Values are typed and validated by the definitions of the
/// registry: a missing key, a stored value of another type or out of the range means
/// the default value. A value equal to the default is not stored (the row is deleted),
/// so the table holds only the changed settings and Restore defaults of a section is
/// the deletion of its keys. Changes are notified by PDFDatabaseNotifier (topic
/// "setting", key = key of the setting) to all instances.
///
/// The repository holds nothing but the pointers, it can be created where it is needed.
/// It can be used in any thread (each thread has its own connection).
class PDF4QTLIBAPPSHARED_EXPORT PDFSettingsRepository
{
public:
    /// Topic of the change notifications of the settings
    static QString getTopic() { return QStringLiteral("setting"); }

    PDFSettingsRepository(PDFDatabase* database, const PDFSettingRegistry* registry);

    /// Valid value of the setting (default, when it is not stored or not valid).
    /// An unknown key returns an invalid QVariant.
    QVariant getValue(const QString& key) const;

    bool getBool(const QString& key) const;
    qint64 getInteger(const QString& key) const;
    double getReal(const QString& key) const;
    QString getString(const QString& key) const;

    /// Value stored in the database without validation (invalid QVariant = not stored).
    /// The Settings page shows a stored option, which the system does not offer, as
    /// "(not available)".
    QVariant getStoredValue(const QString& key, PDFDatabaseError* error = nullptr) const;

    /// The setting has a stored valid value different from the default
    bool isChanged(const QString& key) const;

    /// Keys of the section with a stored value (dialog Restore defaults lists them)
    QStringList getChangedKeys(const QString& section) const;

    /// Stores the value. An invalid value (type, range, option) is refused with an
    /// error and nothing is stored. A busy database is reported as an error.
    PDFDatabaseError setValue(const QString& key, const QVariant& value);

    /// Removes the stored value (the default applies)
    PDFDatabaseError resetValue(const QString& key);

    /// Restore defaults of a section of Settings: deletes the keys of the section
    PDFDatabaseError restoreDefaults(const QString& section);

private:
    const PDFSettingDefinition* getDefinition(const QString& key) const;

    QPointer<PDFDatabase> m_database;
    const PDFSettingRegistry* m_registry = nullptr;
};

}   // namespace pdf

#endif // PDFSETTINGSREPOSITORY_H
