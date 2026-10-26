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

#ifndef PDFAPPBUILTINTOOLCOLLECTIONS_H
#define PDFAPPBUILTINTOOLCOLLECTIONS_H

#include "pdfappglobal.h"

#include <QString>
#include <QStringList>
#include <QStringView>

#include <vector>

namespace pdf
{

/// Built-in collection of tools (Figma Tools Overview 272:12821, side column
/// Collections; Manage collections 283:1888). Data only: the texts are American
/// English in the table and translated by the getters (context
/// "pdf::PDFAppBuiltinToolCollections").
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBuiltinToolCollection
{
public:
    constexpr PDFAppBuiltinToolCollection(const char* id, const char* name, const char* iconName, const char* toolIds) :
        m_id(id),
        m_name(name),
        m_iconName(iconName),
        m_toolIds(toolIds)
    {

    }

    /// Stable identifier ("prepare-for-sharing"): the column builtin_id of a
    /// changed built-in collection and its route /tools/collections/{id} - never change it
    QString getId() const;

    /// Translated name ("Prepare for sharing")
    QString getName() const;

    /// Name in American English (the stored name of a built-in collection, which
    /// the user has not renamed)
    QString getSourceName() const;

    /// Icon of the design (one of PDFAppBuiltinToolCollections::getIconChoices)
    QString getIconName() const;

    /// Tools of the collection (identifiers of PDFDocumentToolCatalog) in their order
    QStringList getToolIds() const;

private:
    const char* m_id;
    const char* m_name;
    const char* m_iconName;
    const char* m_toolIds;
};

/// The seven built-in collections of the Tools page (plan of step 6, chapter 2)
/// and the icons, which a collection may have. A built-in collection exists in
/// the database only after the user has changed it (a row with builtin_id, its
/// override): Reset to default deletes the row. A built-in collection cannot be
/// deleted. Every function is thread safe and never fails: an unknown identifier
/// gives nullptr / false.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBuiltinToolCollections
{
public:
    PDFAppBuiltinToolCollections() = delete;

    /// Translation context of the names
    static constexpr const char* TRANSLATION_CONTEXT = "pdf::PDFAppBuiltinToolCollections";

    /// Longest identifier of a built-in collection, which is looked up at all
    static constexpr int MAXIMUM_ID_LENGTH = 64;

    /// Built-in collections in their order (the order of the Tools page)
    static const std::vector<PDFAppBuiltinToolCollection>& getCollections();

    /// Built-in collection by its identifier, nullptr for an unknown one
    static const PDFAppBuiltinToolCollection* findCollection(QStringView id);

    /// Position of the built-in collection (0 to 6), -1 for an unknown one
    static int getIndex(QStringView id);

    /// The ten icons of the icon chooser of the collections editor (283:1888) in
    /// their order: rotate-cw, camera, mail, pen-tool, briefcase, printer, download,
    /// folder, star, zap
    static const QStringList& getIconChoices();

    /// The icon is one of getIconChoices
    static bool isValidIconName(const QString& iconName);

    /// Icon of a collection without an icon or with an icon, which is not allowed ("folder")
    static QString getDefaultIconName() { return QStringLiteral("folder"); }
};

}   // namespace pdf

#endif // PDFAPPBUILTINTOOLCOLLECTIONS_H
