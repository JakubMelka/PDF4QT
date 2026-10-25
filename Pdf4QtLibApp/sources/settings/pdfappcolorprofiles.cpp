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

#include "pdfappcolorprofiles.h"
#include "pdfappsysteminfo.h"
#include "pdfrepositoryhelper.h"
#include "pdfsettingdefinitions.h"

#include "pdfcms.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

namespace pdf
{

/// Helpers of the profiles of the color management
class PDFAppColorProfilesHelper
{
public:
    PDFAppColorProfilesHelper() = delete;

    static constexpr const char* TRANSLATION_CONTEXT = "pdf::PDFAppColorProfiles";

    /// Profiles of a list of the manager (ids too long to be stored are skipped)
    static std::vector<PDFAppColorProfileList::Profile> convert(const PDFColorProfileIdentifiers& identifiers)
    {
        std::vector<PDFAppColorProfileList::Profile> profiles;
        for (const PDFColorProfileIdentifier& identifier : identifiers)
        {
            if (identifier.id.size() > PDFAppColorProfiles::MAXIMUM_ID_LENGTH)
            {
                continue;
            }

            QString name = PDFAppColorProfiles::sanitizeName(identifier.name);
            if (name.isEmpty())
            {
                name = PDFAppColorProfiles::sanitizeName(QFileInfo(identifier.id).completeBaseName());
            }
            profiles.push_back({ identifier.id, name });
        }
        return profiles;
    }

    /// Cache of isDirectoryUsable (one folder - the folder of the settings)
    class CheckCache
    {
    public:
        QMutex mutex;
        QString path;
        bool isUsable = false;
        bool isValid = false;
        QElapsedTimer timer;
    };

    static CheckCache& getCheckCache()
    {
        static CheckCache cache;
        return cache;
    }
};

const std::vector<PDFAppColorProfileList::Profile>* PDFAppColorProfileList::getProfiles(const QString& key) const
{
    if (key == PDFSettingKeys::cmsOutputProfile())
    {
        return &outputProfiles;
    }
    if (key == PDFSettingKeys::cmsDeviceGrayProfile())
    {
        return &grayProfiles;
    }
    if (key == PDFSettingKeys::cmsDeviceRgbProfile())
    {
        return &rgbProfiles;
    }
    if (key == PDFSettingKeys::cmsDeviceCmykProfile())
    {
        return &cmykProfiles;
    }
    return nullptr;
}

PDFAppProfileDirectoryState PDFAppColorProfiles::checkDirectory(const QString& path)
{
    using State = PDFAppProfileDirectoryState;

    if (path.isEmpty())
    {
        return State::None;
    }

    // A network folder is never touched (not even its existence)
    if (PDFAppNetworkPaths::isNetworkPathByName(path) || PDFAppNetworkPaths().isNetworkPath(path))
    {
        return State::Network;
    }

    // A device path of Windows is never touched either: \\.\UNC\server\share and
    // \\?\GLOBALROOT\Device\Mup\server\share reach a server (its existence check
    // would already send the credentials of the user), \\.\pipe\... a device
    if (PDFRepositoryHelper::isDevicePath(path))
    {
        return State::NotFolder;
    }

    const QFileInfo folderInfo(path);
    if (!folderInfo.exists())
    {
        return State::Missing;
    }
    if (!folderInfo.isDir())
    {
        return State::NotFolder;
    }

    // The color management lists every file of the folder and reads every *.icc
    // file (as QDir does: case insensitive on Windows and macOS)
    int entries = 0;
    int profiles = 0;
    qint64 totalSize = 0;
    QDirIterator iterator(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    while (iterator.hasNext())
    {
        const QFileInfo info = iterator.nextFileInfo();
        if (++entries > MAXIMUM_ENTRIES)
        {
            return State::TooManyFiles;
        }

        if (!info.fileName().endsWith(QStringLiteral(".icc"), Qt::CaseInsensitive))
        {
            continue;
        }

        if (++profiles > MAXIMUM_PROFILES)
        {
            return State::TooManyFiles;
        }

        // The size of the file of a link (a link to a device has no size and is
        // not a file - the color management skips it, but it is refused here)
        if (!info.isFile())
        {
            return State::TooLarge;
        }

        const qint64 size = info.size();
        totalSize += size;
        if (size > MAXIMUM_PROFILE_SIZE || totalSize > MAXIMUM_TOTAL_SIZE)
        {
            return State::TooLarge;
        }
    }

    return State::Usable;
}

bool PDFAppColorProfiles::isDirectoryUsable(const QString& path)
{
    if (path.isEmpty())
    {
        return true;
    }

    PDFAppColorProfilesHelper::CheckCache& cache = PDFAppColorProfilesHelper::getCheckCache();
    QMutexLocker locker(&cache.mutex);
    if (cache.isValid && cache.path == path && cache.timer.isValid() && cache.timer.elapsed() < CHECK_CACHE_DURATION)
    {
        return cache.isUsable;
    }

    cache.path = path;
    cache.isUsable = checkDirectory(path) == PDFAppProfileDirectoryState::Usable;
    cache.isValid = true;
    cache.timer.start();
    return cache.isUsable;
}

void PDFAppColorProfiles::clearCache()
{
    PDFAppColorProfilesHelper::CheckCache& cache = PDFAppColorProfilesHelper::getCheckCache();
    QMutexLocker locker(&cache.mutex);
    cache.isValid = false;
    cache.path.clear();
}

PDFAppColorProfileList PDFAppColorProfiles::scan(const QString& directory)
{
    PDFAppColorProfileList list;
    list.directory = directory;
    list.directoryState = checkDirectory(directory);

    // The lists of Settings are always current (profiles installed or added to the
    // folder meanwhile); the documents take them at their next change of the
    // settings (the cache of the process, see PDFAppDocumentView)
    PDFCMSManager::clearExternalProfilesCache();

    // A manager of this thread with the folder (only a usable one), which lists
    // the profiles of the system and of the folder
    PDFCMSManager manager(nullptr);
    PDFCMSSettings settings;
    settings.system = PDFCMSSettings::System::LittleCMS2;
    if (list.directoryState == PDFAppProfileDirectoryState::Usable)
    {
        settings.profileDirectory = directory;
    }
    manager.setSettings(settings);

    list.outputProfiles = PDFAppColorProfilesHelper::convert(manager.getOutputProfiles());
    list.grayProfiles = PDFAppColorProfilesHelper::convert(manager.getGrayProfiles());
    list.rgbProfiles = PDFAppColorProfilesHelper::convert(manager.getRGBProfiles());
    list.cmykProfiles = PDFAppColorProfilesHelper::convert(manager.getCMYKProfiles());
    return list;
}

QString PDFAppColorProfiles::getStateText(PDFAppProfileDirectoryState state)
{
    using State = PDFAppProfileDirectoryState;
    using Helper = PDFAppColorProfilesHelper;

    switch (state)
    {
        case State::None:
        case State::Usable:
            return QString();
        case State::Missing:
            return QCoreApplication::translate(Helper::TRANSLATION_CONTEXT, "The folder does not exist");
        case State::NotFolder:
            return QCoreApplication::translate(Helper::TRANSLATION_CONTEXT, "The path is not a folder");
        case State::Network:
            return QCoreApplication::translate(Helper::TRANSLATION_CONTEXT, "Folders on a network are not read");
        case State::TooManyFiles:
            return QCoreApplication::translate(Helper::TRANSLATION_CONTEXT, "Not read: the folder has more than %1 files or %2 profiles").arg(MAXIMUM_ENTRIES).arg(MAXIMUM_PROFILES);
        case State::TooLarge:
            return QCoreApplication::translate(Helper::TRANSLATION_CONTEXT, "Not read: the profiles of the folder are too large");
    }
    return QString();
}

QString PDFAppColorProfiles::sanitizeName(const QString& name)
{
    QString result;
    result.reserve(qMin(name.size(), qsizetype(MAXIMUM_NAME_LENGTH)));
    for (const QChar character : name)
    {
        if (result.size() >= MAXIMUM_NAME_LENGTH)
        {
            break;
        }

        // Control, format (bidirectional controls), unassigned and surrogate code
        // units (characters outside of the BMP) are dropped, separators are spaces
        switch (character.category())
        {
            case QChar::Other_Control:
            case QChar::Other_Format:
            case QChar::Other_Surrogate:
            case QChar::Other_NotAssigned:
                break;
            case QChar::Separator_Line:
            case QChar::Separator_Paragraph:
                result.append(QLatin1Char(' '));
                break;
            default:
                result.append(character);
                break;
        }
    }
    return result.trimmed();
}

std::vector<PDFSettingOption> PDFAppColorProfileOptionsProvider::getOptions(const PDFSettingDefinition& definition) const
{
    std::vector<PDFSettingOption> options;
    const std::vector<PDFAppColorProfileList::Profile>* profiles = m_list ? m_list->getProfiles(definition.getKey()) : nullptr;

    // The built-in default is the first profile of the list, stored as ""
    QString defaultName = QCoreApplication::translate(PDFAppColorProfilesHelper::TRANSLATION_CONTEXT, "None (simple conversion)");
    if (profiles && !profiles->empty())
    {
        defaultName = profiles->front().name;
    }
    options.push_back({ QString(), defaultName, true });

    if (profiles)
    {
        for (size_t i = 1; i < profiles->size(); ++i)
        {
            const PDFAppColorProfileList::Profile& profile = (*profiles)[i];
            if (!profile.id.isEmpty() && profile.id != profiles->front().id)
            {
                options.push_back({ profile.id, profile.name, true });
            }
        }
    }

    // While the lists are read, a stored profile is not shown as not available
    if (m_isLoading)
    {
        auto it = m_loadingValues.find(definition.getKey());
        if (it != m_loadingValues.end() && !it->second.isEmpty())
        {
            bool isOffered = false;
            for (const PDFSettingOption& option : options)
            {
                isOffered = isOffered || option.id == it->second;
            }
            if (!isOffered)
            {
                options.push_back({ it->second, QCoreApplication::translate(PDFAppColorProfilesHelper::TRANSLATION_CONTEXT, "Loading profiles…"), true });
            }
        }
    }

    return options;
}

void PDFAppColorProfileOptionsProvider::setList(std::shared_ptr<const PDFAppColorProfileList> list)
{
    m_list = std::move(list);
    m_loadingValues.clear();
    m_isLoading = false;
}

void PDFAppColorProfileOptionsProvider::setLoading(std::map<QString, QString> storedValues)
{
    m_loadingValues = std::move(storedValues);
    m_isLoading = true;
}

std::vector<PDFSettingOption> PDFAppColorSystemOptionsProvider::getOptions(const PDFSettingDefinition& definition) const
{
    Q_UNUSED(definition);

    // The code appends the version of the library (944:9100)
    return { { QStringLiteral("generic"), PDFCMSManager::getSystemName(PDFCMSSettings::System::Generic), true },
             { QStringLiteral("lcms2"), PDFCMSManager::getSystemName(PDFCMSSettings::System::LittleCMS2), true } };
}

}   // namespace pdf
