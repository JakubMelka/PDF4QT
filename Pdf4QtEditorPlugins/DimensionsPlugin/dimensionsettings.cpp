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

#include "dimensionsettings.h"

#include "pdfdocument.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>

namespace pdfplugin
{

namespace
{

constexpr const char* SETTINGS_GROUP = "DimensionsPlugin";
constexpr const char* SETTINGS_PRESETS_ARRAY = "ScalePresets";
constexpr const char* SETTINGS_DOCUMENT_SCALES_ARRAY = "DocumentScales";

}   // namespace

DocumentIdentity DocumentIdentity::create(const pdf::PDFDocument* document, const QString& fileName)
{
    DocumentIdentity identity;

    if (!fileName.isEmpty())
    {
        // Canonical path resolves the symbolic links, so the same file opened
        // through different paths is recognized. It is empty for a file, which
        // does not exist anymore, the absolute path is used then.
        const QFileInfo fileInfo(fileName);
        identity.filePath = fileInfo.canonicalFilePath();

        if (identity.filePath.isEmpty())
        {
            identity.filePath = fileInfo.absoluteFilePath();
        }
    }

    if (document)
    {
        const QByteArray id = document->getIdPart(0);

        if (!id.isEmpty())
        {
            identity.permanentId = QString::fromLatin1(id.toHex());
        }
    }

    return identity;
}

DimensionsPluginSettings DimensionsPluginSettings::createDefault()
{
    DimensionsPluginSettings settings;

    settings.lengthUnit = DimensionUnit::getLengthUnits().front();
    settings.areaUnit = DimensionUnit::getAreaUnits().front();
    settings.angleUnit = DimensionUnit::getAngleUnits().front();
    settings.defaultScale = DimensionScale::createIdentity();
    settings.storageMode = StorageMode::Temporary;
    settings.isScaleStoredPerDocument = true;
    settings.textColor = QColor(Qt::black);

    // The background color fills the area and the perimeter measurements and it
    // is drawn under the displayed value, so it must not hide the drawing
    settings.backgroundColor = QColor(0, 0, 0, 25);

    return settings;
}

DimensionsSettingsStorage::DimensionsSettingsStorage() :
    m_settings(DimensionsPluginSettings::createDefault()),
    m_presets(DimensionScale::getDefaultPresets())
{

}

void DimensionsSettingsStorage::load()
{
    m_settings = DimensionsPluginSettings::createDefault();
    m_presets = DimensionScale::getDefaultPresets();
    m_documentScales.clear();

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(SETTINGS_GROUP);

    m_settings.lengthUnit = DimensionUnit::getLengthUnit(settings.value("lengthUnit", m_settings.lengthUnit.id).toByteArray());
    m_settings.areaUnit = DimensionUnit::getAreaUnit(settings.value("areaUnit", m_settings.areaUnit.id).toByteArray());
    m_settings.angleUnit = DimensionUnit::getAngleUnit(settings.value("angleUnit", m_settings.angleUnit.id).toByteArray());

    DimensionScale defaultScale = DimensionScale::fromStringList(settings.value("defaultScale").toStringList());
    if (defaultScale.isValid())
    {
        m_settings.defaultScale = qMove(defaultScale);
    }

    m_settings.storageMode = settings.value("storeAsAnnotations", false).toBool() ? DimensionsPluginSettings::StorageMode::Annotations
                                                                                  : DimensionsPluginSettings::StorageMode::Temporary;
    m_settings.isScaleStoredPerDocument = settings.value("scalePerDocument", m_settings.isScaleStoredPerDocument).toBool();

    const QString fontDescriptor = settings.value("font").toString();
    if (!fontDescriptor.isEmpty())
    {
        QFont font;
        if (font.fromString(fontDescriptor))
        {
            m_settings.font = qMove(font);
        }
    }

    const QColor textColor(settings.value("textColor", m_settings.textColor.name(QColor::HexArgb)).toString());
    if (textColor.isValid())
    {
        m_settings.textColor = textColor;
    }

    const QColor backgroundColor(settings.value("backgroundColor", m_settings.backgroundColor.name(QColor::HexArgb)).toString());
    if (backgroundColor.isValid())
    {
        m_settings.backgroundColor = backgroundColor;
    }

    // The presets were never stored yet, when the flag is missing. Without it,
    // an empty array could not be distinguished from an uninitialized one and
    // the user would not be able to delete all the presets permanently.
    const bool arePresetsStored = settings.value("presetsStored", false).toBool();

    const int presetCount = settings.beginReadArray(SETTINGS_PRESETS_ARRAY);
    if (arePresetsStored)
    {
        std::vector<DimensionScale> presets;
        presets.reserve(size_t(presetCount));

        for (int i = 0; i < presetCount; ++i)
        {
            settings.setArrayIndex(i);
            DimensionScale preset = DimensionScale::fromStringList(settings.value("scale").toStringList());
            if (preset.isValid())
            {
                presets.push_back(qMove(preset));
            }
        }

        m_presets = qMove(presets);
    }
    settings.endArray();

    const int documentScaleCount = settings.beginReadArray(SETTINGS_DOCUMENT_SCALES_ARRAY);
    m_documentScales.reserve(size_t(documentScaleCount));
    for (int i = 0; i < documentScaleCount; ++i)
    {
        settings.setArrayIndex(i);

        DocumentScale documentScale;
        documentScale.identity.filePath = settings.value("path").toString();
        documentScale.identity.permanentId = settings.value("id").toString();
        documentScale.scale = DimensionScale::fromStringList(settings.value("scale").toStringList());
        documentScale.lastUsed = settings.value("lastUsed").toDateTime();

        if (documentScale.identity.isValid() && documentScale.scale.isValid())
        {
            m_documentScales.push_back(qMove(documentScale));
        }
    }
    settings.endArray();

    settings.endGroup();
}

void DimensionsSettingsStorage::save() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(SETTINGS_GROUP);

    settings.setValue("lengthUnit", m_settings.lengthUnit.id);
    settings.setValue("areaUnit", m_settings.areaUnit.id);
    settings.setValue("angleUnit", m_settings.angleUnit.id);
    settings.setValue("defaultScale", m_settings.defaultScale.toStringList());
    settings.setValue("storeAsAnnotations", m_settings.storageMode == DimensionsPluginSettings::StorageMode::Annotations);
    settings.setValue("scalePerDocument", m_settings.isScaleStoredPerDocument);
    settings.setValue("font", m_settings.font.toString());
    settings.setValue("textColor", m_settings.textColor.name(QColor::HexArgb));
    settings.setValue("backgroundColor", m_settings.backgroundColor.name(QColor::HexArgb));
    settings.setValue("presetsStored", true);

    settings.beginWriteArray(SETTINGS_PRESETS_ARRAY, int(m_presets.size()));
    for (size_t i = 0; i < m_presets.size(); ++i)
    {
        settings.setArrayIndex(int(i));
        settings.setValue("scale", m_presets[i].toStringList());
    }
    settings.endArray();

    settings.beginWriteArray(SETTINGS_DOCUMENT_SCALES_ARRAY, int(m_documentScales.size()));
    for (size_t i = 0; i < m_documentScales.size(); ++i)
    {
        settings.setArrayIndex(int(i));
        settings.setValue("path", m_documentScales[i].identity.filePath);
        settings.setValue("id", m_documentScales[i].identity.permanentId);
        settings.setValue("scale", m_documentScales[i].scale.toStringList());
        settings.setValue("lastUsed", m_documentScales[i].lastUsed);
    }
    settings.endArray();

    settings.endGroup();
}

void DimensionsSettingsStorage::addPreset(const DimensionScale& scale)
{
    if (!scale.isValid() || scale.getName().isEmpty())
    {
        return;
    }

    auto it = std::find_if(m_presets.begin(), m_presets.end(), [&scale](const DimensionScale& preset) { return preset.getName() == scale.getName(); });

    if (it != m_presets.end())
    {
        *it = scale;
    }
    else
    {
        m_presets.push_back(scale);
    }
}

const DimensionsSettingsStorage::DocumentScale* DimensionsSettingsStorage::findDocumentScale(const DocumentIdentity& identity) const
{
    if (!identity.isValid())
    {
        return nullptr;
    }

    if (!identity.filePath.isEmpty())
    {
        // The file path identifies exactly one file, so it has a priority
        auto it = std::find_if(m_documentScales.cbegin(), m_documentScales.cend(),
                               [&identity](const DocumentScale& item) { return item.identity.filePath == identity.filePath; });

        if (it != m_documentScales.cend())
        {
            return &*it;
        }
    }

    if (identity.permanentId.isEmpty())
    {
        return nullptr;
    }

    // The file was not found by its path. The permanent identifier survives
    // renaming and moving of the file, but it is not unique - a copy of a document
    // keeps it as well. A record is therefore accepted only when it cannot belong
    // to a different, still existing file.
    for (const DocumentScale& item : m_documentScales)
    {
        if (item.identity.permanentId != identity.permanentId)
        {
            continue;
        }

        if (identity.filePath.isEmpty() || item.identity.filePath.isEmpty())
        {
            // One of the documents has no file at all, the identifier is
            // the only thing they can be matched by
            return &item;
        }

        if (!QFileInfo::exists(item.identity.filePath))
        {
            // The file of the record does not exist anymore, so the document
            // was renamed or moved
            return &item;
        }
    }

    return nullptr;
}

DimensionScale DimensionsSettingsStorage::getDocumentScale(const DocumentIdentity& identity) const
{
    if (const DocumentScale* documentScale = findDocumentScale(identity))
    {
        return documentScale->scale;
    }

    return DimensionScale();
}

void DimensionsSettingsStorage::setDocumentScale(const DocumentIdentity& identity, const DimensionScale& scale)
{
    if (!identity.isValid() || !scale.isValid())
    {
        return;
    }

    DocumentScale* documentScale = const_cast<DocumentScale*>(findDocumentScale(identity));

    if (!documentScale)
    {
        m_documentScales.emplace_back();
        documentScale = &m_documentScales.back();
    }

    // The identity is always rewritten, so a record found by the permanent
    // identifier is moved to the current path of the file
    documentScale->identity = identity;
    documentScale->scale = scale;
    documentScale->lastUsed = QDateTime::currentDateTime();

    if (m_documentScales.size() > MAXIMUM_DOCUMENT_SCALES)
    {
        // Discard the records, which were not used for the longest time
        std::sort(m_documentScales.begin(), m_documentScales.end(), [](const DocumentScale& left, const DocumentScale& right) { return left.lastUsed > right.lastUsed; });
        m_documentScales.resize(MAXIMUM_DOCUMENT_SCALES);
    }
}

}   // namespace pdfplugin
