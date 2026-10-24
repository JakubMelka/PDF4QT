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

#ifndef PDFAPPANNOTATIONSTYLESTORAGE_H
#define PDFAPPANNOTATIONSTYLESTORAGE_H

#include "pdfappglobal.h"
#include "pdfannotationstylerepository.h"

#include "pdfaction.h"
#include "pdfannotation.h"
#include "pdfannotationstyle.h"

#include <QJsonObject>
#include <QPointer>

namespace pdf
{

/// Storage of the styles of the annotation tools in the database of the new
/// application (step 4 plan chapter 4, E1): PDFAnnotationStyleSettings of the
/// engine reads and writes the styles through it instead of QSettings. The
/// context installs it (PDFAnnotationStyleSettings::setStorage) when the database
/// is opened and removes it before the database is closed. Every read goes to the
/// database (no cache): a style changed by another instance is read by the next
/// call; the notification of the topic PDFAnnotationStyleRepository::getTopic()
/// tells the open tool bars to read it again.
///
/// The tool rows (sticky note icon, stamp, text box style, link destination,
/// last markup type) are read and written by the typed functions below; their
/// values are stored as stable names, never as numbers of enumerations.
/// Values read are bounded, unknown or damaged values give the defaults.
/// The GUI thread only (PDFAnnotationStyleStorage).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationStyleStorage : public PDFAnnotationStyleStorage
{
public:
    explicit PDFAppAnnotationStyleStorage(PDFDatabase* database);
    virtual ~PDFAppAnnotationStyleStorage() override;

    PDFAppAnnotationStyleStorage(const PDFAppAnnotationStyleStorage&) = delete;
    PDFAppAnnotationStyleStorage& operator=(const PDFAppAnnotationStyleStorage&) = delete;

    virtual PDFAnnotationStyle getStyle(const QString& styleId, const PDFAnnotationStyle& defaultStyle) const override;
    virtual void setStyle(const QString& styleId, const PDFAnnotationStyle& style) override;

    /// Error of the last write (an empty error after a successful one)
    const PDFDatabaseError& getLastError() const { return m_lastError; }

    /// Repository of the rows
    PDFAnnotationStyleRepository getRepository() const { return PDFAnnotationStyleRepository(m_database); }

    // ---------------------------------------------------------------- tool rows

    TextAnnotationIcon getStickyNoteIcon(TextAnnotationIcon defaultIcon = TextAnnotationIcon::Comment) const;
    void setStickyNoteIcon(TextAnnotationIcon icon);

    Stamp getStamp(Stamp defaultStamp = Stamp::Approved) const;
    void setStamp(Stamp stamp);

    /// Style of the text box tool; \p autoResize (may be nullptr) gets the
    /// automatic resizing to the contents (default true)
    PDFFreeTextStyle getFreeTextStyle(const PDFFreeTextStyle& defaultStyle, bool* autoResize) const;
    void setFreeTextStyle(const PDFFreeTextStyle& style, bool autoResize);

    /// Destination type of the Link to page tool (XYZ - keeps the zoom - by default)
    DestinationType getPageLinkDestinationType(DestinationType defaultType = DestinationType::XYZ) const;
    void setPageLinkDestinationType(DestinationType type);

    /// Text markup type used last (Highlight, Underline, Squiggly, StrikeOut)
    AnnotationType getLastMarkupType(AnnotationType defaultType = AnnotationType::Highlight) const;
    void setLastMarkupType(AnnotationType type);

    // ---------------------------------------------------------------- conversions

    /// JSON value of a style row {strokeColor, fillColor|null, penWidth}
    static QJsonObject toJson(const PDFAnnotationStyle& style);

    /// Style of a JSON value (missing or invalid members take the default ones,
    /// the result is bounded by PDFAnnotationStyleSettings::boundStyle)
    static PDFAnnotationStyle fromJson(const QJsonObject& value, const PDFAnnotationStyle& defaultStyle);

    /// Stable names of the values of the tool rows ("" for an invalid value) and
    /// back (nullopt for an unknown name)
    static QString getIconName(TextAnnotationIcon icon);
    static std::optional<TextAnnotationIcon> getIconFromName(const QString& name);
    static QString getStampName(Stamp stamp);
    static std::optional<Stamp> getStampFromName(const QString& name);
    static QString getDestinationTypeName(DestinationType type);
    static std::optional<DestinationType> getDestinationTypeFromName(const QString& name);
    static QString getMarkupTypeName(AnnotationType type);
    static std::optional<AnnotationType> getMarkupTypeFromName(const QString& name);

private:
    QString getString(const QString& styleId, const QString& key) const;
    void setValue(const QString& styleId, const QJsonObject& value);

    QPointer<PDFDatabase> m_database;
    PDFDatabaseError m_lastError;
};

}   // namespace pdf

#endif // PDFAPPANNOTATIONSTYLESTORAGE_H
