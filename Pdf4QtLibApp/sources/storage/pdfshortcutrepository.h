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

#ifndef PDFSHORTCUTREPOSITORY_H
#define PDFSHORTCUTREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QPointer>
#include <QString>

#include <map>

namespace pdf
{

/// Shortcuts changed by the user (table shortcut, newapp_storage.md 4). The keys are
/// stored as the portable text of QKeySequence; an empty text means, that the user
/// has removed the shortcut of the command. Commands, which are not stored, have
/// their default shortcut (PDFAppCommandRegistry). Every read goes to the database.
class PDF4QTLIBAPPSHARED_EXPORT PDFShortcutRepository
{
public:
    /// Topic of the change notifications
    static QString getTopic() { return QStringLiteral("shortcut"); }

    /// Maximal length of the stored text of the keys
    static constexpr qsizetype MAXIMUM_KEYS_LENGTH = 256;

    explicit PDFShortcutRepository(PDFDatabase* database);

    /// Shortcuts of the user: command id -> portable text of the keys
    std::map<QString, QString> getShortcuts(PDFDatabaseError* error = nullptr) const;

    /// Stores the shortcut of the command (empty \p keys = no shortcut)
    PDFDatabaseError setShortcut(const QString& commandId, const QString& keys);

    /// Removes the shortcut of the user (the default applies)
    PDFDatabaseError resetShortcut(const QString& commandId);

    /// Restore defaults of the section Shortcuts: deletes all rows
    PDFDatabaseError resetAll();

private:
    QPointer<PDFDatabase> m_database;
};

}   // namespace pdf

#endif // PDFSHORTCUTREPOSITORY_H
