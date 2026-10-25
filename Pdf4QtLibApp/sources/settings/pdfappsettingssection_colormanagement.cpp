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

#include "pdfappsettingssection_colormanagement.h"
#include "pdfappcontext.h"
#include "pdfappsettingcontrol.h"
#include "pdfappsettingrow.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include <QtConcurrent/QtConcurrentRun>

namespace pdf
{

PDFAppColorManagementSettingsSection::PDFAppColorManagementSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) :
    BaseClass(context, sectionId, parent),
    m_profileProvider(std::make_shared<PDFAppColorProfileOptionsProvider>()),
    m_systemProvider(std::make_shared<PDFAppColorSystemOptionsProvider>())
{

}

PDFAppColorManagementSettingsSection::~PDFAppColorManagementSettingsSection() = default;

QStringList PDFAppColorManagementSettingsSection::getProfileKeys()
{
    return { PDFSettingKeys::cmsOutputProfile(), PDFSettingKeys::cmsDeviceGrayProfile(), PDFSettingKeys::cmsDeviceRgbProfile(), PDFSettingKeys::cmsDeviceCmykProfile() };
}

bool PDFAppColorManagementSettingsSection::isScanning() const
{
    return m_watcher && m_watcher->isRunning();
}

std::shared_ptr<const PDFAppColorProfileList> PDFAppColorManagementSettingsSection::getProfileList() const
{
    return m_profileProvider ? m_profileProvider->getList() : nullptr;
}

PDFSettingDefinition PDFAppColorManagementSettingsSection::createOptionDefinition(const PDFSettingDefinition& definition, std::shared_ptr<const PDFSettingOptionsProvider> provider)
{
    // The key, the texts and the dependency of the definition of the registry; the
    // repository validates the written value by the definition of the registry
    PDFSettingDefinition result = PDFSettingDefinition::createOption(definition.getKey(), definition.getDefaultValue().toString(), std::move(provider));
    result.setSection(definition.getSection(), definition.getGroup());
    result.setAnchor(definition.getAnchor());
    result.setTitle(definition.getTitleSource());
    result.setDescription(definition.getDescriptionSource());
    result.setKeywords(definition.getKeywords());
    if (definition.hasEnabledWhen())
    {
        result.setEnabledWhen(definition.getEnabledWhenKey(), definition.getEnabledWhenValue());
    }
    return result;
}

PDFAppSettingRow* PDFAppColorManagementSettingsSection::createRow(const PDFSettingDefinition& definition)
{
    if (definition.getKey() == PDFSettingKeys::cmsSystem())
    {
        return BaseClass::createRow(createOptionDefinition(definition, m_systemProvider));
    }

    if (getProfileKeys().contains(definition.getKey()))
    {
        return BaseClass::createRow(createOptionDefinition(definition, m_profileProvider));
    }

    return BaseClass::createRow(definition);
}

void PDFAppColorManagementSettingsSection::finishContent()
{
    BaseClass::finishContent();
    startScan();
}

void PDFAppColorManagementSettingsSection::refresh(const QString& topic, const QString& key)
{
    BaseClass::refresh(topic, key);

    // A new folder of the profiles (also of another instance) gives new lists
    const bool isSetting = topic.isEmpty() || topic == PDFSettingsRepository::getTopic();
    if (isSetting && (topic.isEmpty() || key.isEmpty() || key == PDFSettingKeys::cmsProfileDirectory()))
    {
        startScan();
    }
}

std::vector<PDFAppSettingChange> PDFAppColorManagementSettingsSection::getChangedValues() const
{
    std::vector<PDFAppSettingChange> changes = BaseClass::getChangedValues();
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase())
    {
        return changes;
    }

    const PDFSettingsRepository settings = context->getSettings();
    const QStringList profileKeys = getProfileKeys();
    for (PDFAppSettingChange& change : changes)
    {
        const PDFSettingDefinition* definition = context->getSettingRegistry()->findDefinition(change.key);
        if (!definition || !profileKeys.contains(change.key))
        {
            continue;
        }

        // The name of the profile of the lists, a sanitized id otherwise
        const PDFSettingDefinition optionDefinition = createOptionDefinition(*definition, m_profileProvider);
        change.currentValue = PDFAppSettingControl::formatValue(optionDefinition, settings.getString(change.key));
        change.defaultValue = PDFAppSettingControl::formatValue(optionDefinition, QString());
    }
    return changes;
}

void PDFAppColorManagementSettingsSection::startScan()
{
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase() || !m_profileProvider)
    {
        return;
    }

    // One reading at a time: a change during a reading reads the lists again after it
    if (isScanning())
    {
        m_isRescanRequested = true;
        return;
    }

    // While the lists are read, the stored profiles are offered as "Loading profiles…"
    const PDFSettingsRepository settings = context->getSettings();
    std::map<QString, QString> storedValues;
    for (const QString& key : getProfileKeys())
    {
        storedValues[key] = settings.getString(key);
    }
    m_profileProvider->setLoading(std::move(storedValues));
    updateProfileRows();

    m_isRescanRequested = false;
    m_scannedDirectory = settings.getString(PDFSettingKeys::cmsProfileDirectory());
    if (!m_watcher)
    {
        m_watcher = new QFutureWatcher<PDFAppColorProfileList>(this);
        connect(m_watcher.data(), &QFutureWatcherBase::finished, this, &PDFAppColorManagementSettingsSection::onScanFinished);
    }
    m_watcher->setFuture(QtConcurrent::run(&PDFAppColorProfiles::scan, m_scannedDirectory));
}

void PDFAppColorManagementSettingsSection::onScanFinished()
{
    if (!m_watcher || !m_profileProvider)
    {
        return;
    }

    ++m_scanCount;
    const QFuture<PDFAppColorProfileList> future = m_watcher->future();
    PDFAppColorProfileList list;
    if (future.isValid() && future.resultCount() > 0)
    {
        list = future.result();
    }

    // The folder has changed meanwhile: the lists are read again
    PDFAppContext* context = getContext();
    const QString directory = context && context->getDatabase() ? context->getSettings().getString(PDFSettingKeys::cmsProfileDirectory()) : m_scannedDirectory;
    if (m_isRescanRequested || directory != list.directory)
    {
        m_isRescanRequested = false;
        startScan();
        return;
    }

    m_profileProvider->setList(std::make_shared<const PDFAppColorProfileList>(std::move(list)));
    updateProfileRows();
    updateDirectoryDescription();
}

void PDFAppColorManagementSettingsSection::updateProfileRows()
{
    const QStringList keys = getProfileKeys();
    for (PDFAppSettingRow* row : getRows())
    {
        const QString key = row->getKey();
        if (keys.contains(key))
        {
            row->updateFromSettings(key);
        }
    }
}

void PDFAppColorManagementSettingsSection::updateDirectoryDescription()
{
    PDFAppContext* context = getContext();
    const PDFSettingDefinition* definition = context ? context->getSettingRegistry()->findDefinition(PDFSettingKeys::cmsProfileDirectory()) : nullptr;
    PDFAppSettingRow* row = definition ? findRow(definition->getAnchor()) : nullptr;
    if (!row)
    {
        return;
    }

    // A folder, which is not read, says why below the title of its row
    const std::shared_ptr<const PDFAppColorProfileList> list = getProfileList();
    const QString stateText = list ? PDFAppColorProfiles::getStateText(list->directoryState) : QString();
    row->setDescription(stateText.isEmpty() ? definition->getDescription() : stateText);
}

PDFAppSettingsSection* PDFAppColorManagementSettingsSectionFactory::createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const
{
    return new PDFAppColorManagementSettingsSection(context, sectionId, parent);
}

}   // namespace pdf
