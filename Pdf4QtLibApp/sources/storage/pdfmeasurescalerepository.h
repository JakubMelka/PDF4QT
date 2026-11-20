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

#ifndef PDFMEASURESCALEREPOSITORY_H
#define PDFMEASURESCALEREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"
#include "pdfmeasure.h"

#include <QByteArray>
#include <QPointer>
#include <QString>

#include <optional>
#include <vector>

namespace pdf
{

/// The scales of the tool Measure (the tables measure_scale_preset, measure_scale_state and
/// measure_document_scale of migration 4; plan of step 7 of phase 2, 4.8, owner answer Q7.4): the presets of the
/// menu Scale (471:6943) and of Manage Presets (473:7412, 473:7860, 473:8249 Restore Defaults), the default scale
/// (473:92070) and the scale remembered for a document (Remember for this document, 473:9500). It replaces the
/// QSettings of the old DimensionsPlugin. A scale is never written into a document (Q7.4: no /VP).
///
/// A document is identified by its path and the first identifier of its /ID (ISO 32000-2, 14.4: the bytes of the
/// first string): a document without a file or without /ID cannot be remembered (isDocumentIdentifiable; the state
/// 473:89712 "scale not remembered"). At most MAXIMUM_DOCUMENT_SCALES documents are kept: a write prunes the least
/// recently used rows (LRU by last_used: set by setDocumentScale and by markDocumentScaleUsed, when the tool takes the
/// remembered scale of a document; a read of findDocumentScale does not write).
///
/// The presets: the built-in ones (PDFMeasureScale::getDefaultPresets) until the user edits the list
/// (arePresetsEdited); restoreDefaultPresets returns to them. At most MAXIMUM_PRESETS. A preset and the default
/// scale keep their description (PDFMeasureScale::getDescription; the field Description of 473:7860, 473:92070);
/// the scale of a document keeps none (no frame shows it).
///
/// The rows are untrusted (another version, a damaged or a hostile file): a row with an invalid scale (a value
/// that is not a positive finite number, an unknown unit), a name longer than MAXIMUM_NAME_LENGTH, a description
/// that is no text or longer than MAXIMUM_DESCRIPTION_LENGTH, a path that is
/// not a valid stored path (PDFRepositoryHelper::isValidStoredPath) or an identifier longer than
/// MAXIMUM_DOCUMENT_ID_LENGTH is skipped. A value object as the other repositories: reads go to the database, a
/// write is one short transaction, notified with the topics of the tables (getPresetTopic, getStateTopic,
/// getDocumentTopic). Usable in any thread.
///
/// Package P7 implements it (the migration is the skeleton's: PDFDatabaseSchema::createVersion4); the skeleton
/// has the interface (stubs: the built-in values, every write an error).
class PDF4QTLIBAPPSHARED_EXPORT PDFMeasureScaleRepository
{
public:
    static QString getPresetTopic() { return QStringLiteral("measure_scale_preset"); }
    static QString getStateTopic() { return QStringLiteral("measure_scale_state"); }
    static QString getDocumentTopic() { return QStringLiteral("measure_document_scale"); }

    /// Most documents, whose scales are remembered (plan 4.8)
    static constexpr int MAXIMUM_DOCUMENT_SCALES = 200;

    /// Most presets
    static constexpr int MAXIMUM_PRESETS = 200;

    /// Longest name of a scale
    static constexpr int MAXIMUM_NAME_LENGTH = 256;

    /// Longest description of a preset or of the default scale (the field Description of 473:7860, 473:92070)
    static constexpr int MAXIMUM_DESCRIPTION_LENGTH = 1024;

    /// Longest first identifier of /ID, which is stored (ISO 32000-2, 14.4 recommends 16 bytes of MD5)
    static constexpr int MAXIMUM_DOCUMENT_ID_LENGTH = 1024;

    explicit PDFMeasureScaleRepository(PDFDatabase* database);

    /// Fixed time of the writes (tests); 0 = the current time
    void setCurrentTime(qint64 time) { m_currentTime = time; }

    /// The presets in their order: the stored ones after the user edited them, else the built-in ones
    std::vector<PDFMeasureScale> getPresets() const;

    /// The user edited the presets (the built-in ones are no longer offered)
    bool arePresetsEdited() const;

    /// Replaces the presets (valid scales, at most MAXIMUM_PRESETS; the order is kept)
    PDFDatabaseError setPresets(const std::vector<PDFMeasureScale>& presets);

    /// Forgets the edited presets: the built-in ones are offered again (473:8249)
    PDFDatabaseError restoreDefaultPresets();

    /// The default scale of a document without its own scale (473:92070); 1:1 without one
    PDFMeasureScale getDefaultScale() const;

    /// Stores the default scale (a valid scale)
    PDFDatabaseError setDefaultScale(const PDFMeasureScale& scale);

    /// A document can be remembered: an absolute stored path and a non-empty first identifier of /ID of at most
    /// MAXIMUM_DOCUMENT_ID_LENGTH bytes
    static bool isDocumentIdentifiable(const QString& path, const QByteArray& documentId);

    /// The scale remembered for the document, std::nullopt = none (or a damaged row)
    std::optional<PDFMeasureScale> findDocumentScale(const QString& path, const QByteArray& documentId) const;

    /// Remembers the scale of the document (a valid scale, an identifiable document); prunes to
    /// MAXIMUM_DOCUMENT_SCALES rows
    PDFDatabaseError setDocumentScale(const QString& path, const QByteArray& documentId, const PDFMeasureScale& scale);

    /// The remembered scale of the document is used now (Measure opened on it): its last_used is the current time,
    /// so the pruning of setDocumentScale keeps the scales of the documents measured lately, not only of those whose
    /// scale was set lately (review RS7CCORE L8). Nothing for a document without a remembered scale.
    PDFDatabaseError markDocumentScaleUsed(const QString& path, const QByteArray& documentId);

    /// Forgets the scale of the document
    PDFDatabaseError removeDocumentScale(const QString& path, const QByteArray& documentId);

private:
    qint64 getCurrentTime() const;
    PDFDatabaseConnection* getConnection() const;

    QPointer<PDFDatabase> m_database;
    qint64 m_currentTime = 0;
};

}   // namespace pdf

#endif // PDFMEASURESCALEREPOSITORY_H
