// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef DIMENSIONSETTINGS_H
#define DIMENSIONSETTINGS_H

#include "dimensionunits.h"

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QString>

#include <vector>

namespace pdf
{
class PDFDocument;
}

namespace pdfplugin
{

/// Identification of a document, under which its scale is remembered. A document
/// is primarily identified by the path of its file, because the scale is
/// remembered for each file. The permanent identifier from the trailer dictionary
/// is stored as well, so the scale is not lost, when the file is renamed or moved,
/// and it is the only identification of a document, which has no file name at all.
///
/// The permanent identifier is not unique - a copy of a document keeps it - so
/// it is used only when it cannot lead to a different, still existing file. As
/// a consequence, a document saved under a new name while the original still
/// exists is treated as a new document and has to be calibrated again.
struct DocumentIdentity
{
    QString filePath;       ///< Canonical path of the file, empty if it is not known
    QString permanentId;    ///< Hexadecimal form of the first part of the trailer ID, empty if the document has none

    bool isValid() const { return !filePath.isEmpty() || !permanentId.isEmpty(); }
    bool operator==(const DocumentIdentity&) const = default;

    /// Returns the identity of the document. Returns an invalid identity,
    /// if the document cannot be identified at all.
    /// \param document Document
    /// \param fileName Original file name of the document
    static DocumentIdentity create(const pdf::PDFDocument* document, const QString& fileName);
};

struct DimensionsPluginSettings
{
    /// Determines, how the created measurements are stored
    enum class StorageMode
    {
        Temporary,      ///< Measurements are drawn by the plugin and are lost when the document is closed
        Annotations     ///< Measurements are stored in the document as measurement annotations
    };

    DimensionUnit lengthUnit;
    DimensionUnit areaUnit;
    DimensionUnit angleUnit;

    /// Scale used for documents, which have no scale of their own. It is never
    /// inherited from another document - the scale currently in effect is held
    /// by the plugin, not by the settings.
    DimensionScale defaultScale;

    StorageMode storageMode = StorageMode::Temporary;
    bool isScaleStoredPerDocument = true;
    QFont font;
    QColor textColor = QColor(Qt::black);

    /// Color, by which is the interior of the area and the perimeter measurements
    /// filled and by which is the displayed value backed. Its alpha channel is
    /// respected, a fully transparent color means, that nothing is filled.
    QColor backgroundColor = QColor(0, 0, 0, 25);

    /// Returns the settings with the default values
    static DimensionsPluginSettings createDefault();
};

/// Persistent storage of the dimension plugin settings, of the scale presets
/// and of the scales, which were calibrated for the particular documents.
/// The data are stored in the application settings, so they survive the restart
/// of the application.
class DimensionsSettingsStorage
{
public:
    explicit DimensionsSettingsStorage();

    /// Loads everything from the application settings
    void load();

    /// Stores everything to the application settings
    void save() const;

    DimensionsPluginSettings& getSettings() { return m_settings; }
    const DimensionsPluginSettings& getSettings() const { return m_settings; }
    void setSettings(DimensionsPluginSettings settings) { m_settings = qMove(settings); }

    const std::vector<DimensionScale>& getPresets() const { return m_presets; }
    void setPresets(std::vector<DimensionScale> presets) { m_presets = qMove(presets); }

    /// Adds the scale to the presets. If a preset with the same name already
    /// exists, then it is replaced.
    /// \param scale Named scale
    void addPreset(const DimensionScale& scale);

    /// Returns the scale, which was stored for the document, or an invalid scale,
    /// if no scale is known for it. The file path has a priority over the permanent
    /// identifier, so a document, which was calibrated under its own path, is not
    /// affected by another document sharing the identifier with it.
    /// \param identity Document identification
    DimensionScale getDocumentScale(const DocumentIdentity& identity) const;

    /// Stores the scale for the document. The oldest records are discarded,
    /// so the settings file does not grow indefinitely.
    /// \param identity Document identification
    /// \param scale Scale of the document
    void setDocumentScale(const DocumentIdentity& identity, const DimensionScale& scale);

private:
    /// Maximum count of the documents, for which the scale is remembered
    static constexpr size_t MAXIMUM_DOCUMENT_SCALES = 200;

    struct DocumentScale
    {
        DocumentIdentity identity;
        DimensionScale scale;
        QDateTime lastUsed;
    };

    /// Returns the record of the document, or nullptr, if it does not exist
    /// \param identity Document identification
    const DocumentScale* findDocumentScale(const DocumentIdentity& identity) const;

    DimensionsPluginSettings m_settings;
    std::vector<DimensionScale> m_presets;
    std::vector<DocumentScale> m_documentScales;
};

}   // namespace pdfplugin

#endif // DIMENSIONSETTINGS_H
