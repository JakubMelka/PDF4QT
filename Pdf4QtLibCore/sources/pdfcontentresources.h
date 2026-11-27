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

#ifndef PDFCONTENTRESOURCES_H
#define PDFCONTENTRESOURCES_H

#include "pdfcontentoperation.h"
#include "pdfobject.h"

#include <QByteArray>

#include <array>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace pdf
{
class PDFDocument;
class PDFDocumentBuilder;

// Edit Content, stage E0, package P01. Plan: zadani_jadro.md 4.2 (container), 4.4 R-4.

/// The resources of a container by category: name -> object and back. Rule R-4 of the plan: an
/// entry, which existed when the container was loaded, is never removed and never renamed
/// (resources inherited by a form without /Resources and by glyph procedures, DefaultGray / RGB /
/// CMYK, names used by inline images and by opaque operations); new names are allocated here and
/// avoid every name in use.
///
/// Threads and ownership: a value (the objects are PDFObject values, implicitly shared). The
/// resources inside of a container are immutable and read by any thread; a command works on
/// a copy, which becomes a part of the next container.
class PDF4QTLIBCORESHARED_EXPORT PDFContentResources
{
public:
    /// One named resource (a plain value; its object is an owned PDFObject value)
    struct Entry
    {
        QByteArray name;                ///< Without the solidus
        PDFObject object;               ///< The value in the dictionary of the category: a reference (also a provisional one of PDFContentEditSession::addNewObject) or a direct object (the ExtGState dictionaries of the editor are direct)
        bool isOriginal = false;        ///< Existed when the container was loaded: never removed
    };

    PDFContentResources() = default;

    /// Reads the resources of a content.
    /// \param document The document
    /// \param resources The /Resources of the content (a dictionary or a reference; null for a
    ///        content without resources of its own). A page gives its resources inherited from
    ///        the page tree (PDFPage::getResources).
    /// \param operations The operations of the content: the names they use without a definition
    ///        in \p resources are remembered as undefined (allocateName avoids them)
    static PDFContentResources read(const PDFDocument* document, const PDFObject& resources, const PDFContentOperationList& operations);

    /// The entries of a category in the order of the dictionary, the added ones after them
    const std::vector<Entry>& getEntries(PDFContentResourceCategory category) const;

    /// The object of a name: a null object, when the name is not defined
    PDFObject getObject(PDFContentResourceCategory category, const QByteArray& name) const;

    /// The name of an object in a category (the same reference, or an equal direct object): empty,
    /// when the object is not a resource of the category
    QByteArray getName(PDFContentResourceCategory category, const PDFObject& object) const;

    /// Names, which the operations use, but the dictionary does not define
    const std::set<QByteArray>& getUndefinedNames(PDFContentResourceCategory category) const;

    /// Names of a category, which content drawn by this one uses and could take from these
    /// resources once a name is added to them: a form, a tiling pattern or a Type 3 font without
    /// /Resources of its own looks a name up in the page (ISO 32000-2, 7.8.3) and, where the page
    /// does not define it, in the content, which draws it (NOTES.txt 7.8.3). allocateName avoids
    /// them, so an added name never becomes a name such content picks up (A1L2-4). The tree
    /// builder sets them: every name the operations of such content name (as far as its bounded
    /// walk reaches), which the page does not define in the category. They are not undefined
    /// names of this content (getUndefinedNames).
    const std::set<QByteArray>& getReservedNames(PDFContentResourceCategory category) const;
    void setReservedNames(PDFContentResourceCategory category, std::set<QByteArray> names);

    /// A new name of a category: \p prefix and the smallest positive number, which gives a name
    /// neither defined nor used without a definition nor reserved (getReservedNames). The
    /// prefixes of the plan: F (Font), Im and Fm (XObject), GS (ExtGState), CS (ColorSpace),
    /// P (Pattern), Sh (Shading), MC (Properties).
    QByteArray allocateName(PDFContentResourceCategory category, const QByteArray& prefix) const;

    /// Adds an object to a category and returns its name: the name it already has there (the
    /// object is not added twice), else a new one of allocateName
    QByteArray addObject(PDFContentResourceCategory category, const QByteArray& prefix, const PDFObject& object);

    /// Removes an entry added by addObject (never an original one: false then, also for a name,
    /// which is not defined)
    bool removeAdded(PDFContentResourceCategory category, const QByteArray& name);

    /// Replaces the object of an entry added by addObject; its name stays (the document writer
    /// replaces the provisional references of new objects by the real ones so).
    /// \returns false for an original entry and for a name, which is not defined
    bool setObject(PDFContentResourceCategory category, const QByteArray& name, const PDFObject& object);

    /// Something was added or removed since read
    bool isChanged() const;

    /// The key of a category in a resource dictionary ("Font", "XObject", ...)
    static QByteArray getCategoryKey(PDFContentResourceCategory category);

    /// All categories in the order of the enum
    static std::vector<PDFContentResourceCategory> getCategories();

    /// Writes the resources as a new dictionary of this container only (R-4): every other key
    /// of the original /Resources is kept (/ProcSet, unknown keys), each category dictionary,
    /// which has an added entry, is a new dictionary (a shared indirect one is copied, never
    /// changed in place), a category without a change keeps its original value.
    /// \param builder Builder of the result
    /// \returns the new /Resources dictionary (a direct object)
    PDFObject write(PDFDocumentBuilder* builder) const;

private:
    /// The data of one category (defined in the source file). The data held by more copies of
    /// the resources are never changed: a copy, which adds or removes an entry, makes its own
    /// copy of this one category first, so the copy of the resources made by every command
    /// costs seven pointers.
    struct CategoryData;
    using CategoryDataPointer = std::shared_ptr<CategoryData>;

    static constexpr size_t CATEGORY_COUNT = 7;

    /// The data of a category, nullptr for a category without data and for a value, which is
    /// no category
    const CategoryData* getCategoryData(PDFContentResourceCategory category) const;

    /// The data of a category for writing: owned by this copy only afterwards; nullptr for
    /// a value, which is no category
    CategoryData* detachCategoryData(PDFContentResourceCategory category);

    /// The reserved names of a category, nullptr when there are none
    const std::set<QByteArray>* getReservedNamesPointer(PDFContentResourceCategory category) const;

    PDFObject m_originalResources;  ///< The dictionary /Resources as it was read (dereferenced); null, when there was none
    std::array<CategoryDataPointer, CATEGORY_COUNT> m_categories;
    std::array<std::shared_ptr<const std::set<QByteArray>>, CATEGORY_COUNT> m_reservedNames;   ///< getReservedNames by category; nullptr = none (shared by the copies, never changed)
};

}   // namespace pdf

#endif // PDFCONTENTRESOURCES_H
