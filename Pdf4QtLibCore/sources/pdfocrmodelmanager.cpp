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

#include "pdfocrmodelmanager.h"
#include "config.h"

#include <QDir>
#include <QFile>
#include <QUuid>
#include <QLockFile>
#include <QDateTime>
#include <QFileInfo>
#include <QDirIterator>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStorageInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QNetworkProxyFactory>
#include <QNetworkAccessManager>

#include <atomic>
#include <algorithm>

namespace pdf
{

static constexpr const char* TRAINEDDATA_SUFFIX = ".traineddata";
static constexpr const char* RUNTIME_COMPLETE_FILE = "complete.json";
static constexpr const char* STATE_FILE = "state.json";
static constexpr const char* IMPORT_FILE = "import.json";
static constexpr const char* LOCK_FILE = "ocr.lock";
static constexpr const char* INSTALL_METADATA_SUFFIX = ".meta.json";
static constexpr const char* LEASE_FILE_PATTERN = "in-use.*.lock";

// -------------------------------------------------------------------------
// PDFOCRCatalog
// -------------------------------------------------------------------------

const PDFOCRCatalogEntry* PDFOCRCatalog::find(const QString& id) const
{
    for (const PDFOCRCatalogEntry& entry : entries)
    {
        if (entry.id == id)
        {
            return &entry;
        }
    }
    return nullptr;
}

PDFOCRCatalog PDFOCRCatalog::fromJson(const QJsonObject& object)
{
    PDFOCRCatalog catalog;

    if (object.value(QStringLiteral("format")).toString() != QStringLiteral("pdf4qt-ocr-catalog"))
    {
        return catalog;
    }

    catalog.version = object.value(QStringLiteral("version")).toInt();
    catalog.engineId = object.value(QStringLiteral("engine")).toString();
    catalog.generated = object.value(QStringLiteral("generated")).toString();
    catalog.compatibility = object.value(QStringLiteral("engineCompatibility")).toString();

    const QJsonObject sources = object.value(QStringLiteral("sources")).toObject();
    for (auto it = sources.begin(); it != sources.end(); ++it)
    {
        const QJsonObject source = it.value().toObject();
        catalog.sourceRepositories[it.key()] = source.value(QStringLiteral("repository")).toString();
        catalog.sourceCommits[it.key()] = source.value(QStringLiteral("commit")).toString();
    }

    for (const QJsonValue& value : object.value(QStringLiteral("models")).toArray())
    {
        const QJsonObject model = value.toObject();
        PDFOCRCatalogEntry entry;
        entry.id = model.value(QStringLiteral("id")).toString();
        entry.engineId = model.value(QStringLiteral("engine")).toString();
        entry.language = model.value(QStringLiteral("language")).toString();
        entry.name = model.value(QStringLiteral("name")).toString();
        entry.profile = PDFOCRConfiguration::parseProfileIdentifier(model.value(QStringLiteral("profile")).toString());
        entry.family = model.value(QStringLiteral("family")).toString();
        entry.version = model.value(QStringLiteral("version")).toString();
        entry.url = model.value(QStringLiteral("url")).toString();
        entry.fileName = model.value(QStringLiteral("fileName")).toString();
        entry.size = qint64(model.value(QStringLiteral("size")).toDouble());
        entry.sha256 = model.value(QStringLiteral("sha256")).toString().toLower();
        entry.license = model.value(QStringLiteral("license")).toString();
        for (const QJsonValue& dependency : model.value(QStringLiteral("dependencies")).toArray())
        {
            entry.dependencies << dependency.toString();
        }

        if (entry.id.isEmpty() || entry.language.isEmpty() || entry.fileName.isEmpty())
        {
            continue;
        }

        catalog.entries.push_back(std::move(entry));
    }

    return catalog;
}

PDFOCRCatalog PDFOCRCatalog::fromBytes(const QByteArray& data, QString* errorMessage)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (!document.isObject())
    {
        if (errorMessage)
        {
            *errorMessage = parseError.errorString();
        }
        return PDFOCRCatalog();
    }

    PDFOCRCatalog catalog = fromJson(document.object());
    if (!catalog.isValid() && errorMessage)
    {
        *errorMessage = PDFTranslationContext::tr("Invalid model catalog.");
    }
    return catalog;
}

QString PDFOCRCatalog::getSetId(PDFOCRModelProfile profile) const
{
    const QString profileId = PDFOCRConfiguration::getProfileIdentifier(profile);
    auto it = sourceCommits.find(profileId);
    if (it != sourceCommits.end() && !it->second.isEmpty())
    {
        return QStringLiteral("tessdata_%1-%2").arg(profileId, it->second.left(12));
    }
    return QStringLiteral("tessdata_%1").arg(profileId);
}

// -------------------------------------------------------------------------
// PDFOCRModelInfo
// -------------------------------------------------------------------------

QString PDFOCRModelInfo::getStateName(PDFOCRModelState state)
{
    switch (state)
    {
        case PDFOCRModelState::BuiltIn:
            return PDFTranslationContext::tr("Built-in");
        case PDFOCRModelState::Installed:
            return PDFTranslationContext::tr("Installed");
        case PDFOCRModelState::Available:
            return PDFTranslationContext::tr("Available for download");
        case PDFOCRModelState::UpdateAvailable:
            return PDFTranslationContext::tr("Update available");
        case PDFOCRModelState::Downloading:
            return PDFTranslationContext::tr("Downloading");
        case PDFOCRModelState::Verifying:
            return PDFTranslationContext::tr("Verifying");
        case PDFOCRModelState::Error:
            return PDFTranslationContext::tr("Error");
        case PDFOCRModelState::Incompatible:
            return PDFTranslationContext::tr("Incompatible");
    }

    return QString();
}

QString PDFOCRModelInfo::getOriginName(PDFOCRModelOrigin origin)
{
    switch (origin)
    {
        case PDFOCRModelOrigin::None:
            return PDFTranslationContext::tr("Not installed");
        case PDFOCRModelOrigin::BuiltIn:
            return PDFTranslationContext::tr("Built-in");
        case PDFOCRModelOrigin::Downloaded:
            return PDFTranslationContext::tr("Downloaded");
        case PDFOCRModelOrigin::Imported:
            return PDFTranslationContext::tr("Imported (origin not verified)");
    }

    return QString();
}

// -------------------------------------------------------------------------
// PDFOCRModelManager
// -------------------------------------------------------------------------

PDFOCRModelManager::PDFOCRModelManager(QObject* parent) :
    QObject(parent),
    m_userDirectory(getDefaultUserDirectory()),
    m_builtInDirectory(getDefaultBuiltInDirectory())
{
    // Installations are serialized by the lock of the data directory anyway
    m_verificationPool.setMaxThreadCount(1);
    m_verificationPool.setExpiryTimeout(30000);
}

PDFOCRModelManager::~PDFOCRModelManager()
{
    cancelAllDownloads();

    // The verification worker posts its outcome to this object, so it must
    // finish before the object is destroyed (the posted event is then discarded)
    m_verificationPool.waitForDone();

    if (m_ownsNetworkAccessManager)
    {
        delete m_networkAccessManager;
    }
}

QString PDFOCRModelManager::getApplicationDataRoot()
{
    const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    if (locations.isEmpty())
    {
        return QDir::homePath() + QStringLiteral("/.pdf4qt");
    }
    return locations.front();
}

QString PDFOCRModelManager::getDefaultUserDirectory()
{
    return QDir(getApplicationDataRoot() + QStringLiteral("/ocr")).absolutePath();
}

QString PDFOCRModelManager::getDefaultBuiltInDirectory()
{
    const QByteArray environmentDirectory = qgetenv("PDF4QT_OCR_DATA_DIRECTORY");
    if (!environmentDirectory.isEmpty())
    {
        return QDir(QString::fromLocal8Bit(environmentDirectory)).absolutePath();
    }

    QDir applicationDirectory(QCoreApplication::applicationDirPath());
    if (!applicationDirectory.cd(QString::fromLatin1(PDF4QT_OCR_DATA_RELATIVE_PATH)))
    {
        return QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/") + QString::fromLatin1(PDF4QT_OCR_DATA_RELATIVE_PATH)).absolutePath();
    }
    return applicationDirectory.absolutePath();
}

void PDFOCRModelManager::setUserDirectory(const QString& directory)
{
    m_userDirectory = QDir(directory).absolutePath();
}

void PDFOCRModelManager::setBuiltInDirectory(const QString& directory)
{
    m_builtInDirectory = QDir(directory).absolutePath();
}

void PDFOCRModelManager::loadBundledCatalog()
{
    QFile file(QStringLiteral(":/ocr/tesseract-catalog.json"));
    if (file.open(QFile::ReadOnly))
    {
        QString errorMessage;
        PDFOCRCatalog catalog = PDFOCRCatalog::fromBytes(file.readAll(), &errorMessage);
        if (catalog.isValid())
        {
            setCatalog(std::move(catalog));
        }
    }
}

void PDFOCRModelManager::setCatalog(PDFOCRCatalog catalog)
{
    {
        QMutexLocker lock(&m_mutex);
        m_catalog = std::move(catalog);
    }
    refresh();
}

QString PDFOCRModelManager::getProfileDirectoryName(PDFOCRModelProfile profile)
{
    return PDFOCRConfiguration::getProfileIdentifier(profile);
}

QString PDFOCRModelManager::getEngineUserDirectory(const QString& engineId) const
{
    return m_userDirectory + QStringLiteral("/") + sanitizeIdentifier(engineId);
}

QString PDFOCRModelManager::getRuntimeDirectory(const QString& engineId) const
{
    return getEngineUserDirectory(engineId) + QStringLiteral("/runtime");
}

QString PDFOCRModelManager::sanitizeIdentifier(const QString& identifier)
{
    QString result;
    for (const QChar& character : identifier)
    {
        if (character.isLetterOrNumber() || character == QChar('_') || character == QChar('-') || character == QChar('.'))
        {
            result += character;
        }
        else
        {
            result += QChar('_');
        }
    }

    if (result.startsWith(QChar('.')))
    {
        result.prepend(QChar('_'));
    }

    return result;
}

bool PDFOCRModelManager::isValidLanguageCode(const QString& language)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z][A-Za-z0-9_]{1,31}$"));
    return expression.match(language).hasMatch();
}

QString PDFOCRModelManager::computeSha256(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly))
    {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
    {
        return QString();
    }

    return QString::fromLatin1(hash.result().toHex());
}

void PDFOCRModelManager::scanBuiltIn(std::vector<InstalledFile>& files) const
{
    QDir engineDirectory(m_builtInDirectory + QStringLiteral("/tesseract"));
    if (!engineDirectory.exists())
    {
        return;
    }

    for (const PDFOCRModelProfile profile : PDFOCRConfiguration::getProfiles())
    {
        const QString profileDirectory = engineDirectory.absolutePath() + QStringLiteral("/") + getProfileDirectoryName(profile);
        const QString tessdataDirectory = profileDirectory + QStringLiteral("/tessdata");

        QDir directory(tessdataDirectory);
        if (!directory.exists())
        {
            continue;
        }

        // Manifest (LANG-02)
        std::map<QString, QJsonObject> manifestModels;
        QString setVersion;
        QFile manifestFile(profileDirectory + QStringLiteral("/manifest.json"));
        if (manifestFile.open(QFile::ReadOnly))
        {
            const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
            for (const QJsonValue& value : manifest.value(QStringLiteral("models")).toArray())
            {
                const QJsonObject model = value.toObject();
                manifestModels[model.value(QStringLiteral("language")).toString()] = model;
            }
            setVersion = manifest.value(QStringLiteral("setId")).toString();
        }

        const QFileInfoList entries = directory.entryInfoList(QStringList() << QStringLiteral("*") + QLatin1String(TRAINEDDATA_SUFFIX), QDir::Files, QDir::Name);
        for (const QFileInfo& fileInfo : entries)
        {
            const QString language = fileInfo.completeBaseName();
            if (!isValidLanguageCode(language))
            {
                continue;
            }

            InstalledFile file;
            file.id = QStringLiteral("tesseract/%1/%2").arg(getProfileDirectoryName(profile), language);
            file.language = language;
            file.profile = profile;
            file.origin = PDFOCRModelOrigin::BuiltIn;
            file.path = fileInfo.absoluteFilePath();
            file.size = fileInfo.size();
            file.version = setVersion;

            auto it = manifestModels.find(language);
            if (it != manifestModels.end())
            {
                file.sha256 = it->second.value(QStringLiteral("sha256")).toString().toLower();
                file.name = it->second.value(QStringLiteral("name")).toString();
                file.license = it->second.value(QStringLiteral("license")).toString();
                file.version = it->second.value(QStringLiteral("version")).toString();
                file.verified = true;
            }

            files.push_back(std::move(file));
        }
    }
}

void PDFOCRModelManager::scanUser(std::vector<InstalledFile>& files) const
{
    const QString engineDirectoryPath = getEngineUserDirectory(QStringLiteral("tesseract"));
    QDir engineDirectory(engineDirectoryPath);
    if (!engineDirectory.exists())
    {
        return;
    }

    // Downloaded models: <profile>/<set-id>/tessdata/*.traineddata
    for (const PDFOCRModelProfile profile : PDFOCRConfiguration::getProfiles())
    {
        QDir profileDirectory(engineDirectoryPath + QStringLiteral("/") + getProfileDirectoryName(profile));
        if (!profileDirectory.exists())
        {
            continue;
        }

        const QFileInfoList sets = profileDirectory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& setInfo : sets)
        {
            QDir tessdataDirectory(setInfo.absoluteFilePath() + QStringLiteral("/tessdata"));
            if (!tessdataDirectory.exists())
            {
                continue;
            }

            // Script models are in the "script" subdirectory
            QStringList subdirectories = { QString(), QStringLiteral("script") };
            for (const QString& subdirectory : subdirectories)
            {
                QDir directory(subdirectory.isEmpty() ? tessdataDirectory.absolutePath() : tessdataDirectory.absolutePath() + QStringLiteral("/") + subdirectory);
                if (!directory.exists())
                {
                    continue;
                }

                const QFileInfoList entries = directory.entryInfoList(QStringList() << QStringLiteral("*") + QLatin1String(TRAINEDDATA_SUFFIX), QDir::Files, QDir::Name);
                for (const QFileInfo& fileInfo : entries)
                {
                    const QString baseName = fileInfo.completeBaseName();
                    if (!isValidLanguageCode(baseName))
                    {
                        continue;
                    }

                    const QString language = subdirectory.isEmpty() ? baseName : subdirectory + QStringLiteral("/") + baseName;

                    InstalledFile file;
                    file.id = QStringLiteral("tesseract/%1/%2").arg(getProfileDirectoryName(profile), language);
                    file.language = language;
                    file.profile = profile;
                    file.origin = PDFOCRModelOrigin::Downloaded;
                    file.path = fileInfo.absoluteFilePath();
                    file.size = fileInfo.size();
                    file.version = setInfo.fileName();

                    // Installed file is verified against the catalog by the recorded hash
                    QFile hashFile(fileInfo.absoluteFilePath() + QStringLiteral(".sha256"));
                    if (hashFile.open(QFile::ReadOnly))
                    {
                        file.sha256 = QString::fromLatin1(hashFile.readAll().trimmed()).toLower();
                    }

                    // Installation metadata (engine version of the installation, LANG-12)
                    QFile metadataFile(getInstallMetadataPath(fileInfo.absoluteFilePath()));
                    if (metadataFile.open(QFile::ReadOnly))
                    {
                        const QJsonObject metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
                        file.engineVersion = metadata.value(QStringLiteral("engineVersion")).toString();
                    }

                    if (const PDFOCRCatalogEntry* entry = m_catalog.find(file.id))
                    {
                        file.verified = !file.sha256.isEmpty() && file.sha256 == entry->sha256 && file.size == entry->size;
                        file.name = entry->name;
                        file.license = entry->license;
                        if (file.verified)
                        {
                            file.version = entry->version;
                        }
                    }

                    files.push_back(std::move(file));
                }
            }
        }
    }

    // Imported models: custom/<import-id>/tessdata/*.traineddata + import.json
    QDir customDirectory(engineDirectoryPath + QStringLiteral("/custom"));
    if (customDirectory.exists())
    {
        const QFileInfoList imports = customDirectory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& importInfo : imports)
        {
            QFile importFile(importInfo.absoluteFilePath() + QStringLiteral("/") + QLatin1String(IMPORT_FILE));
            if (!importFile.open(QFile::ReadOnly))
            {
                continue;
            }

            const QJsonObject import = QJsonDocument::fromJson(importFile.readAll()).object();
            const QString importId = importInfo.fileName();
            const PDFOCRModelProfile profile = PDFOCRConfiguration::parseProfileIdentifier(import.value(QStringLiteral("profile")).toString());

            QDir tessdataDirectory(importInfo.absoluteFilePath() + QStringLiteral("/tessdata"));
            const QFileInfoList entries = tessdataDirectory.entryInfoList(QStringList() << QStringLiteral("*") + QLatin1String(TRAINEDDATA_SUFFIX), QDir::Files, QDir::Name);
            for (const QFileInfo& fileInfo : entries)
            {
                const QString language = fileInfo.completeBaseName();
                if (!isValidLanguageCode(language))
                {
                    continue;
                }

                InstalledFile file;
                file.id = QStringLiteral("tesseract/custom/%1/%2").arg(importId, language);
                file.language = language + QStringLiteral("@") + importId;
                file.profile = profile;
                file.origin = PDFOCRModelOrigin::Imported;
                file.path = fileInfo.absoluteFilePath();
                file.size = fileInfo.size();
                file.version = import.value(QStringLiteral("imported")).toString();
                file.sha256 = import.value(QStringLiteral("sha256")).toString();
                file.name = import.value(QStringLiteral("name")).toString();
                file.license = import.value(QStringLiteral("license")).toString();
                file.engineVersion = import.value(QStringLiteral("engineVersion")).toString();
                file.verified = false;
                files.push_back(std::move(file));
            }
        }
    }
}

void PDFOCRModelManager::loadState()
{
    m_hiddenModels.clear();

    QFile file(m_userDirectory + QStringLiteral("/catalog/") + QLatin1String(STATE_FILE));
    if (file.open(QFile::ReadOnly))
    {
        const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
        for (const QJsonValue& value : state.value(QStringLiteral("hidden")).toArray())
        {
            m_hiddenModels << value.toString();
        }
    }
}

void PDFOCRModelManager::saveState() const
{
    QDir().mkpath(m_userDirectory + QStringLiteral("/catalog"));

    QJsonObject state;
    QJsonArray hidden;
    for (const QString& id : m_hiddenModels)
    {
        hidden.append(id);
    }
    state[QStringLiteral("hidden")] = hidden;

    QSaveFile file(m_userDirectory + QStringLiteral("/catalog/") + QLatin1String(STATE_FILE));
    if (file.open(QFile::WriteOnly | QFile::Truncate))
    {
        file.write(QJsonDocument(state).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

bool PDFOCRModelManager::isInsideDirectory(const QString& path, const QString& directory)
{
    const QString cleanDirectory = QDir::cleanPath(directory);
    if (cleanDirectory.isEmpty())
    {
        return false;
    }

    // Separator is part of the prefix, so a sibling directory with the same prefix does not pass
    const QString prefix = cleanDirectory.endsWith(QChar('/')) ? cleanDirectory : cleanDirectory + QChar('/');
    const Qt::CaseSensitivity caseSensitivity =
#ifdef Q_OS_WIN
            Qt::CaseInsensitive;
#else
            Qt::CaseSensitive;
#endif
    return QDir::cleanPath(path).startsWith(prefix, caseSensitivity);
}

void PDFOCRModelManager::performHousekeeping()
{
    if (m_housekeepingDone || !QDir(m_userDirectory).exists() || !m_activeDownloads.empty() || !m_downloadQueue.empty())
    {
        return;
    }
    m_housekeepingDone = true;

    // Another running instance can be in the middle of an installation
    QLockFile lock(m_userDirectory + QStringLiteral("/") + QLatin1String(LOCK_FILE));
    lock.setStaleLockTime(60 * 1000);
    if (!lock.tryLock(0))
    {
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    auto isOlderThan = [&now](const QFileInfo& info, qint64 seconds)
    {
        return info.lastModified().isValid() && info.lastModified().secsTo(now) > seconds;
    };

    // 1. Interrupted activation of a model: "<file>.old" without the file is the last
    // working version and it is returned back; other leftovers are removed.
    const QString engineDirectory = getEngineUserDirectory(QStringLiteral("tesseract"));
    for (const PDFOCRModelProfile profile : PDFOCRConfiguration::getProfiles())
    {
        QDirIterator it(engineDirectory + QStringLiteral("/") + getProfileDirectoryName(profile), QStringList() << QStringLiteral("*.old") << QStringLiteral("*.new"), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString leftoverPath = it.next();
            const QString targetPath = leftoverPath.left(leftoverPath.size() - 4);
            if (leftoverPath.endsWith(QStringLiteral(".old")) && !QFile::exists(targetPath))
            {
                QFile::rename(leftoverPath, targetPath);
            }
            else
            {
                QFile::setPermissions(leftoverPath, QFile::ReadOwner | QFile::WriteOwner);
                QFile::remove(leftoverPath);
            }
        }
    }

    // 2. Temporary files of the downloads and of the verification left by a crash
    constexpr qint64 ONE_DAY = 24 * 3600;
    const QFileInfoList downloads = QDir(m_userDirectory + QStringLiteral("/downloads")).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& info : downloads)
    {
        if (!isOlderThan(info, ONE_DAY))
        {
            continue;
        }

        if (info.isDir())
        {
            QDir(info.absoluteFilePath()).removeRecursively();
        }
        else
        {
            QFile::remove(info.absoluteFilePath());
        }
    }

    // 3. Runtime sets: unfinished sets left by a crash, and the sets not used for a long time.
    // Every set is a full copy of the models, so they must not grow without a limit.
    constexpr qint64 UNUSED_SET_LIFETIME = 30 * ONE_DAY;
    const QFileInfoList runtimeSets = QDir(getRuntimeDirectory(QStringLiteral("tesseract"))).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& info : runtimeSets)
    {
        const bool isTemporary = info.fileName().contains(QStringLiteral(".tmp-"));
        const QFileInfo completeInfo(info.absoluteFilePath() + QStringLiteral("/") + QLatin1String(RUNTIME_COMPLETE_FILE));
        const bool remove = isTemporary ? isOlderThan(info, ONE_DAY) : (!completeInfo.exists() ? isOlderThan(info, ONE_DAY) : isOlderThan(completeInfo, UNUSED_SET_LIFETIME));
        if (!remove)
        {
            continue;
        }

        // A set leased by a running recognition (also of another instance) is kept (LANG-07)
        if (isRuntimeSetInUse(info.absoluteFilePath()))
        {
            continue;
        }

        QDirIterator fileIt(info.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
        while (fileIt.hasNext())
        {
            // Models of the runtime sets are read-only
            QFile::setPermissions(fileIt.next(), QFile::ReadOwner | QFile::WriteOwner);
        }
        QDir(info.absoluteFilePath()).removeRecursively();
    }
}

void PDFOCRModelManager::refresh()
{
    performHousekeeping();
    loadState();

    std::vector<InstalledFile> installedFiles;
    scanBuiltIn(installedFiles);
    scanUser(installedFiles);

    std::vector<PDFOCRModelInfo> models;

    // Catalog entries
    for (const PDFOCRCatalogEntry& entry : m_catalog.entries)
    {
        PDFOCRModelInfo info;
        info.id = entry.id;
        info.engineId = entry.engineId;
        info.language = entry.language;
        info.name = entry.name;
        info.profile = entry.profile;
        info.family = entry.family;
        info.version = entry.version;
        info.size = entry.size;
        info.sha256 = entry.sha256;
        info.license = entry.license;
        info.state = PDFOCRModelState::Available;
        info.origin = PDFOCRModelOrigin::None;
        models.push_back(std::move(info));
    }

    // Installed files
    for (const InstalledFile& file : installedFiles)
    {
        auto it = std::find_if(models.begin(), models.end(), [&file](const PDFOCRModelInfo& model) { return model.id == file.id; });
        if (it == models.end())
        {
            PDFOCRModelInfo info;
            info.id = file.id;
            info.engineId = QStringLiteral("tesseract");
            info.language = file.language;
            info.name = file.name.isEmpty() ? file.language : file.name;
            info.profile = file.profile;
            info.family = file.language == QStringLiteral("osd") ? QStringLiteral("osd") : (file.language.startsWith(QStringLiteral("script/")) ? QStringLiteral("script") : QStringLiteral("language"));
            info.license = file.license;
            models.push_back(std::move(info));
            it = std::prev(models.end());
        }

        PDFOCRModelInfo& info = *it;

        // Built-in file wins over downloaded file with the same identifier only
        // if the downloaded file is not verified (LANG-05: verified user variant
        // of the same profile has priority over the built-in variant).
        const bool replace = info.origin == PDFOCRModelOrigin::None ||
                             (info.origin == PDFOCRModelOrigin::BuiltIn && file.origin == PDFOCRModelOrigin::Downloaded && file.verified) ||
                             (info.origin == PDFOCRModelOrigin::Downloaded && file.origin == PDFOCRModelOrigin::Downloaded && file.verified && !info.catalogVerified);
        if (!replace)
        {
            continue;
        }

        info.origin = file.origin;
        info.path = file.path;
        info.installedVersion = file.version;
        info.installedSha256 = file.sha256;
        info.catalogVerified = file.verified;
        if (info.size == 0)
        {
            info.size = file.size;
        }
        if (!file.sha256.isEmpty() && info.sha256.isEmpty())
        {
            info.sha256 = file.sha256;
        }
        if (info.name.isEmpty() && !file.name.isEmpty())
        {
            info.name = file.name;
        }

        switch (file.origin)
        {
            case PDFOCRModelOrigin::BuiltIn:
                info.state = PDFOCRModelState::BuiltIn;
                break;

            case PDFOCRModelOrigin::Downloaded:
                info.state = (!info.version.isEmpty() && !file.verified && m_catalog.find(file.id)) ? PDFOCRModelState::UpdateAvailable : PDFOCRModelState::Installed;
                break;

            case PDFOCRModelOrigin::Imported:
                info.state = PDFOCRModelState::Installed;
                break;

            case PDFOCRModelOrigin::None:
                break;
        }

        // A user model installed for a different major version of the engine cannot
        // be expected to load after an upgrade of the application (LANG-12)
        if ((file.origin == PDFOCRModelOrigin::Downloaded || file.origin == PDFOCRModelOrigin::Imported) && !file.engineVersion.isEmpty())
        {
            const QString currentVersion = getEngineVersion(info.engineId);
            if (!currentVersion.isEmpty() && getMajorVersion(currentVersion) != getMajorVersion(file.engineVersion))
            {
                info.state = PDFOCRModelState::Incompatible;
                info.errorMessage = PDFTranslationContext::tr("Model was installed for the engine version %1, but the current engine version is %2. Download or import the model again.").arg(file.engineVersion, currentVersion);
            }
        }
    }

    for (PDFOCRModelInfo& info : models)
    {
        info.isHidden = m_hiddenModels.contains(info.id);

        // Failures of the last download attempt are kept until the next attempt
        auto errorIt = m_errorStates.find(info.id);
        if (errorIt != m_errorStates.end() && !info.isUsable())
        {
            info.state = errorIt->second.first;
            info.errorMessage = errorIt->second.second;
        }
    }

    // Keep the download states
    {
        QMutexLocker lock(&m_mutex);
        for (PDFOCRModelInfo& info : models)
        {
            for (const auto& download : m_activeDownloads)
            {
                if (download->modelId == info.id)
                {
                    info.state = download->verifying ? PDFOCRModelState::Verifying : PDFOCRModelState::Downloading;
                    info.downloadProgress = download->verifying ? 100 : (download->entry.size > 0 ? int(100 * download->received / download->entry.size) : 0);
                }
            }
            if (std::find(m_downloadQueue.begin(), m_downloadQueue.end(), info.id) != m_downloadQueue.end())
            {
                info.state = PDFOCRModelState::Downloading;
            }
        }

        std::stable_sort(models.begin(), models.end(), [](const PDFOCRModelInfo& left, const PDFOCRModelInfo& right)
        {
            if (left.name != right.name)
            {
                return left.name < right.name;
            }
            return left.id < right.id;
        });

        m_models = std::move(models);
    }

    Q_EMIT modelsChanged();
}

std::vector<PDFOCRModelInfo> PDFOCRModelManager::getModels() const
{
    QMutexLocker lock(&m_mutex);
    return m_models;
}

std::optional<PDFOCRModelInfo> PDFOCRModelManager::getModel(const QString& id) const
{
    QMutexLocker lock(&m_mutex);
    for (const PDFOCRModelInfo& model : m_models)
    {
        if (model.id == id)
        {
            return model;
        }
    }
    return std::nullopt;
}

std::vector<PDFOCRModelInfo> PDFOCRModelManager::getUsableLanguageModels(const QString& engineId, PDFOCRModelProfile profile, bool includeHidden) const
{
    std::vector<PDFOCRModelInfo> result;
    for (const PDFOCRModelInfo& model : getModels())
    {
        if (model.engineId != engineId || model.profile != profile || model.isOrientationData() || !model.isUsable())
        {
            continue;
        }

        if (model.isHidden && !includeHidden)
        {
            continue;
        }

        result.push_back(model);
    }
    return result;
}

bool PDFOCRModelManager::isLanguageUsable(const QString& engineId, const QString& language, PDFOCRModelProfile profile) const
{
    for (const PDFOCRModelInfo& model : getModels())
    {
        if (model.engineId == engineId && model.profile == profile && model.language == language && model.isUsable())
        {
            return true;
        }
    }
    return false;
}

bool PDFOCRModelManager::isOrientationDataUsable(const QString& engineId, PDFOCRModelProfile profile) const
{
    for (const PDFOCRModelInfo& model : getModels())
    {
        if (model.engineId == engineId && model.isOrientationData() && model.isUsable() && (model.profile == profile || model.origin == PDFOCRModelOrigin::BuiltIn))
        {
            return true;
        }
    }
    return false;
}

QString PDFOCRModelManager::getModelDisplayText(const PDFOCRModelInfo& model)
{
    QString origin;
    switch (model.origin)
    {
        case PDFOCRModelOrigin::None:
            origin = PDFTranslationContext::tr("not installed");
            break;
        case PDFOCRModelOrigin::BuiltIn:
            origin = PDFTranslationContext::tr("built-in");
            break;
        case PDFOCRModelOrigin::Downloaded:
            origin = PDFTranslationContext::tr("downloaded");
            break;
        case PDFOCRModelOrigin::Imported:
            origin = PDFTranslationContext::tr("imported");
            break;
    }

    return QStringLiteral("%1 (%2) - %3 - %4").arg(model.name, model.language, origin, PDFOCRConfiguration::getProfileName(model.profile));
}

QString PDFOCRModelManager::getLanguageName(const QString& engineId, const QString& language)  const
{
    for (const PDFOCRModelInfo& model : getModels())
    {
        if (model.engineId == engineId && model.language == language)
        {
            return model.name;
        }
    }
    return language;
}

void PDFOCRModelManager::setModelHidden(const QString& id, bool hidden)
{
    if (hidden && !m_hiddenModels.contains(id))
    {
        m_hiddenModels << id;
    }
    else if (!hidden)
    {
        m_hiddenModels.removeAll(id);
    }

    saveState();

    {
        QMutexLocker lock(&m_mutex);
        for (PDFOCRModelInfo& model : m_models)
        {
            if (model.id == id)
            {
                model.isHidden = hidden;
            }
        }
    }

    Q_EMIT modelStateChanged(id);
    Q_EMIT modelsChanged();
}

PDFOCRError PDFOCRModelManager::acquireLock(const QString& userDirectory, std::unique_ptr<QLockFile>& lock)
{
    auto createNotWritableError = [&userDirectory]()
    {
        return PDFOCRError::create(PDFOCRErrorCode::InsufficientPermissions,
                                   PDFTranslationContext::tr("OCR data directory '%1' is not writable. Check the permissions of the directory.").arg(userDirectory),
                                   PDFTranslationContext::tr("Model management"));
    };

    if (!QDir().mkpath(userDirectory))
    {
        return createNotWritableError();
    }

    lock = std::make_unique<QLockFile>(userDirectory + QStringLiteral("/") + QLatin1String(LOCK_FILE));
    lock->setStaleLockTime(60 * 1000);
    if (!lock->tryLock(10000))
    {
        if (lock->error() == QLockFile::PermissionError || lock->error() == QLockFile::UnknownError)
        {
            return createNotWritableError();
        }

        return PDFOCRError::create(PDFOCRErrorCode::InitializationFailed,
                                   PDFTranslationContext::tr("OCR data directory '%1' is locked by another instance of the application.").arg(userDirectory),
                                   PDFTranslationContext::tr("Model management"));
    }
    return PDFOCRError::none();
}

QString PDFOCRModelManager::getEngineVersion(const QString& engineId)
{
    if (std::shared_ptr<PDFOCREngineFactory> factory = PDFOCREngineRegistry::getInstance()->getFactory(engineId))
    {
        return factory->getVersion();
    }
    return QString();
}

int PDFOCRModelManager::getMajorVersion(const QString& version)
{
    bool ok = false;
    const int major = version.section(QChar('.'), 0, 0).trimmed().toInt(&ok);
    return ok ? major : -1;
}

QString PDFOCRModelManager::getInstallMetadataPath(const QString& modelPath)
{
    return modelPath + QLatin1String(INSTALL_METADATA_SUFFIX);
}

std::unique_ptr<QLockFile> PDFOCRModelManager::acquireRuntimeSetLease(const QString& dataPath)
{
    if (dataPath.isEmpty())
    {
        return nullptr;
    }

    QDir setDirectory(dataPath);
    if (!setDirectory.exists() || !setDirectory.cdUp())
    {
        return nullptr;
    }

    // Only the runtime sets of the manager are leased (built-in data are never removed)
    if (!QFile::exists(setDirectory.absoluteFilePath(QLatin1String(RUNTIME_COMPLETE_FILE))))
    {
        return nullptr;
    }

    static std::atomic<int> counter = { 0 };
    const QString fileName = setDirectory.absoluteFilePath(QStringLiteral("in-use.%1.%2.lock").arg(QCoreApplication::applicationPid()).arg(++counter));

    auto lease = std::make_unique<QLockFile>(fileName);
    lease->setStaleLockTime(0);
    if (!lease->tryLock(0))
    {
        return nullptr;
    }

    return lease;
}

bool PDFOCRModelManager::isRuntimeSetInUse(const QString& setDirectory)
{
    const QFileInfoList leases = QDir(setDirectory).entryInfoList(QStringList() << QLatin1String(LEASE_FILE_PATTERN), QDir::Files);
    for (const QFileInfo& info : leases)
    {
        // A lease of a living process cannot be taken; the lease of a dead process is
        // stale (QLockFile checks the process), it is taken and removed by the unlock
        QLockFile probe(info.absoluteFilePath());
        probe.setStaleLockTime(0);
        if (!probe.tryLock(0))
        {
            return true;
        }
        probe.unlock();
    }
    return false;
}

QString PDFOCRModelManager::getLanguageWithoutImport(const QString& language, QString* importId) const
{
    const int index = language.indexOf(QChar('@'));
    if (index == -1)
    {
        if (importId)
        {
            importId->clear();
        }
        return language;
    }

    if (importId)
    {
        *importId = language.mid(index + 1);
    }
    return language.left(index);
}

QStringList PDFOCRModelManager::getMissingModels(const QString& engineId, const QStringList& languages, PDFOCRModelProfile profile) const
{
    QStringList missing;
    for (const QString& language : languages)
    {
        const QString modelId = QStringLiteral("%1/%2/%3").arg(engineId, PDFOCRConfiguration::getProfileIdentifier(profile), language);
        if (!isLanguageUsable(engineId, language, profile))
        {
            missing << modelId;
        }

        // Dependencies of the catalog entry (LANG-06)
        if (const PDFOCRCatalogEntry* entry = m_catalog.find(modelId))
        {
            for (const QString& dependency : entry->dependencies)
            {
                std::optional<PDFOCRModelInfo> model = getModel(dependency);
                if ((!model || !model->isUsable()) && !missing.contains(dependency))
                {
                    missing << dependency;
                }
            }
        }
    }
    return missing;
}

PDFOCRResolvedModelSet PDFOCRModelManager::resolveModelSet(const QString& engineId,
                                                           const QStringList& languages,
                                                           PDFOCRModelProfile profile,
                                                           PDFOCRError* error)
{
    PDFOCRResolvedModelSet set;
    set.profile = profile;

    auto fail = [&](PDFOCRErrorCode code, const QString& message)
    {
        if (error)
        {
            *error = PDFOCRError::create(code, message, PDFTranslationContext::tr("Model resolution"));
        }
        return PDFOCRResolvedModelSet();
    };

    if (languages.isEmpty())
    {
        return fail(PDFOCRErrorCode::InvalidConfiguration, PDFTranslationContext::tr("No language selected."));
    }

    // Select the models (LANG-05): the model list already contains the priority
    // (verified user variant of the same profile before built-in variant); custom
    // imports are selected only by the explicit "language@import" syntax.
    struct Selected
    {
        QString language;
        QString languageCode;
        QString path;
        QString sha256;
        QString id;
    };

    std::vector<Selected> selected;

    // Snapshot of the shared data (the function can run in a worker thread, R12)
    const std::vector<PDFOCRModelInfo> models = getModels();
    PDFOCRCatalog catalog;
    {
        QMutexLocker lock(&m_mutex);
        catalog = m_catalog;
    }

    for (const QString& language : languages)
    {
        const PDFOCRModelInfo* found = nullptr;
        for (const PDFOCRModelInfo& model : models)
        {
            if (model.engineId == engineId && model.profile == profile && model.language == language && model.isUsable() && !model.isOrientationData())
            {
                found = &model;
                break;
            }
        }

        if (!found)
        {
            return fail(PDFOCRErrorCode::MissingModel, PDFTranslationContext::tr("Language model '%1' is not installed for the profile '%2'.").arg(language, PDFOCRConfiguration::getProfileName(profile)));
        }

        Selected item;
        item.language = language;
        item.languageCode = getLanguageWithoutImport(language, nullptr);
        item.path = found->path;
        item.sha256 = found->installedSha256.isEmpty() ? computeSha256(found->path) : found->installedSha256;
        item.id = found->id;

        // Two different models must not be mapped to the same file name
        for (const Selected& other : selected)
        {
            if (other.languageCode == item.languageCode && other.id != item.id)
            {
                return fail(PDFOCRErrorCode::InvalidConfiguration, PDFTranslationContext::tr("Two different models with the language code '%1' cannot be used together.").arg(item.languageCode));
            }
        }

        selected.push_back(std::move(item));
    }

    // Dependencies of the selected models from the catalog (LANG-06): an installed
    // dependency becomes a part of the set, a missing one is reported by name.
    for (size_t index = 0; index < selected.size(); ++index)
    {
        const PDFOCRCatalogEntry* entry = catalog.find(selected[index].id);
        if (!entry)
        {
            continue;
        }

        const QStringList dependencies = entry->dependencies;
        for (const QString& dependencyId : dependencies)
        {
            const bool alreadySelected = std::any_of(selected.begin(), selected.end(), [&dependencyId](const Selected& other) { return other.id == dependencyId; });
            if (alreadySelected)
            {
                continue;
            }

            const PDFOCRModelInfo* dependency = nullptr;
            for (const PDFOCRModelInfo& model : models)
            {
                if (model.id == dependencyId && model.engineId == engineId && model.isUsable())
                {
                    dependency = &model;
                    break;
                }
            }

            if (!dependency)
            {
                return fail(PDFOCRErrorCode::MissingModel, PDFTranslationContext::tr("Language model '%1' requires the model '%2', which is not installed.").arg(selected[index].language, dependencyId));
            }

            Selected item;
            item.language = dependency->language;
            item.languageCode = getLanguageWithoutImport(dependency->language, nullptr);
            item.path = dependency->path;
            item.sha256 = dependency->installedSha256.isEmpty() ? computeSha256(dependency->path) : dependency->installedSha256;
            item.id = dependency->id;

            for (const Selected& other : selected)
            {
                if (other.languageCode == item.languageCode && other.id != item.id)
                {
                    return fail(PDFOCRErrorCode::InvalidConfiguration, PDFTranslationContext::tr("Two different models with the language code '%1' cannot be used together.").arg(item.languageCode));
                }
            }

            if (dependency->isOrientationData())
            {
                set.hasOrientationData = true;
            }
            selected.push_back(std::move(item));
        }
    }

    // Orientation data (osd) of the profile, or built-in
    const PDFOCRModelInfo* osd = nullptr;
    for (const PDFOCRModelInfo& model : models)
    {
        if (model.engineId == engineId && model.isOrientationData() && model.isUsable())
        {
            if (!osd || (model.profile == profile && osd->profile != profile))
            {
                osd = &model;
            }
        }
    }

    if (osd && !set.hasOrientationData)
    {
        Selected item;
        item.language = QStringLiteral("osd");
        item.languageCode = QStringLiteral("osd");
        item.path = osd->path;
        item.sha256 = osd->installedSha256.isEmpty() ? computeSha256(osd->path) : osd->installedSha256;
        item.id = osd->id;
        selected.push_back(std::move(item));
        set.hasOrientationData = true;
    }

    // Hash of the set
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("PDF4QT-OCR-RUNTIME-SET-1"));
    std::vector<QString> hashItems;
    for (const Selected& item : selected)
    {
        hashItems.push_back(item.languageCode + QStringLiteral(":") + item.sha256);
    }
    std::sort(hashItems.begin(), hashItems.end());
    for (const QString& item : hashItems)
    {
        hash.addData(item.toUtf8());
    }
    const QString setHash = QString::fromLatin1(hash.result().toHex().left(24));

    const QString runtimeDirectory = getRuntimeDirectory(engineId) + QStringLiteral("/") + setHash;
    const QString tessdataDirectory = runtimeDirectory + QStringLiteral("/tessdata");

    // Build the set under the lock (LANG-07)
    std::unique_ptr<QLockFile> lock;
    if (PDFOCRError lockError = acquireLock(lock))
    {
        if (error)
        {
            *error = lockError;
        }
        return PDFOCRResolvedModelSet();
    }

    bool complete = QFile::exists(runtimeDirectory + QStringLiteral("/") + QLatin1String(RUNTIME_COMPLETE_FILE));
    if (complete)
    {
        for (const Selected& item : selected)
        {
            if (!QFile::exists(tessdataDirectory + QStringLiteral("/") + item.languageCode + QLatin1String(TRAINEDDATA_SUFFIX)))
            {
                complete = false;
                break;
            }
        }
    }

    if (complete)
    {
        // Time of the last use (sets not used for a long time are removed by the housekeeping)
        QFile completeFile(runtimeDirectory + QStringLiteral("/") + QLatin1String(RUNTIME_COMPLETE_FILE));
        if (completeFile.open(QFile::ReadWrite))
        {
            completeFile.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
        }
    }

    if (!complete)
    {
        // Free space check
        qint64 requiredBytes = 0;
        for (const Selected& item : selected)
        {
            requiredBytes += QFileInfo(item.path).size();
        }

        const qint64 availableBytes = getAvailableSpace();
        if (availableBytes >= 0 && availableBytes < requiredBytes + (qint64(16) << 20))
        {
            return fail(PDFOCRErrorCode::OutOfDiskSpace, PDFTranslationContext::tr("Not enough free space in '%1' to prepare the model set (%2 MB required).").arg(m_userDirectory).arg(requiredBytes / (1024 * 1024) + 1));
        }

        const QString temporaryDirectory = getRuntimeDirectory(engineId) + QStringLiteral("/") + setHash + QStringLiteral(".tmp-") + QString::number(QCoreApplication::applicationPid());
        QDir().mkpath(temporaryDirectory + QStringLiteral("/tessdata"));

        for (const Selected& item : selected)
        {
            const QString targetPath = temporaryDirectory + QStringLiteral("/tessdata/") + item.languageCode + QLatin1String(TRAINEDDATA_SUFFIX);

            // Script models are stored in a subdirectory ("script/Arabic")
            QDir().mkpath(QFileInfo(targetPath).absolutePath());
            QFile::remove(targetPath);
            if (!QFile::copy(item.path, targetPath))
            {
                QDir(temporaryDirectory).removeRecursively();
                return fail(PDFOCRErrorCode::OutOfDiskSpace, PDFTranslationContext::tr("Cannot copy model '%1' into the runtime set directory '%2'.").arg(item.path, temporaryDirectory));
            }

            // The copy is verified against the recorded checksum (LANG-02, LANG-06): a damaged
            // or replaced model is reported by name, not as a generic failure of the engine.
            if (!item.sha256.isEmpty())
            {
                const QString copyHash = computeSha256(targetPath);
                if (copyHash.compare(item.sha256, Qt::CaseInsensitive) != 0)
                {
                    QFile::setPermissions(targetPath, QFile::ReadOwner | QFile::WriteOwner);
                    QDir(temporaryDirectory).removeRecursively();
                    return fail(PDFOCRErrorCode::VerificationFailed, PDFTranslationContext::tr("Model file '%1' is damaged, its checksum does not match. Reinstall the application or download the model again.").arg(item.path));
                }
            }

            // Copies must not allow the modification of the original data
            QFile::setPermissions(targetPath, QFile::ReadOwner | QFile::ReadUser | QFile::ReadGroup | QFile::ReadOther);
        }

        QJsonObject completeObject;
        QJsonArray modelArray;
        for (const Selected& item : selected)
        {
            QJsonObject model;
            model[QStringLiteral("id")] = item.id;
            model[QStringLiteral("language")] = item.languageCode;
            model[QStringLiteral("sha256")] = item.sha256;
            modelArray.append(model);
        }
        completeObject[QStringLiteral("models")] = modelArray;
        completeObject[QStringLiteral("hash")] = setHash;

        QFile completeFile(temporaryDirectory + QStringLiteral("/") + QLatin1String(RUNTIME_COMPLETE_FILE));
        if (!completeFile.open(QFile::WriteOnly | QFile::Truncate))
        {
            QDir(temporaryDirectory).removeRecursively();
            return fail(PDFOCRErrorCode::OutOfDiskSpace, PDFTranslationContext::tr("Cannot write into the runtime set directory '%1'.").arg(temporaryDirectory));
        }
        completeFile.write(QJsonDocument(completeObject).toJson(QJsonDocument::Indented));
        completeFile.close();

        // Atomic activation: the set is published only when complete (LANG-06)
        if (QDir(runtimeDirectory).exists())
        {
            QDir(runtimeDirectory).removeRecursively();
        }

        if (!QDir().rename(temporaryDirectory, runtimeDirectory))
        {
            QDir(temporaryDirectory).removeRecursively();
            if (!QFile::exists(runtimeDirectory + QStringLiteral("/") + QLatin1String(RUNTIME_COMPLETE_FILE)))
            {
                return fail(PDFOCRErrorCode::OutOfDiskSpace, PDFTranslationContext::tr("Cannot activate the runtime set directory '%1'.").arg(runtimeDirectory));
            }
        }
    }

    set.dataPath = tessdataDirectory;
    set.hash = setHash;
    for (const Selected& item : selected)
    {
        if (item.languageCode != QStringLiteral("osd"))
        {
            set.languages << item.languageCode;
        }
        set.modelIds << item.id;
    }

    if (error)
    {
        *error = PDFOCRError::none();
    }

    return set;
}

qint64 PDFOCRModelManager::getAvailableSpace() const
{
    QDir().mkpath(m_userDirectory);
    QStorageInfo storage(m_userDirectory);
    if (!storage.isValid())
    {
        return -1;
    }
    return storage.bytesAvailable();
}

void PDFOCRModelManager::setNetworkAccessManager(QNetworkAccessManager* manager)
{
    if (m_ownsNetworkAccessManager)
    {
        delete m_networkAccessManager;
        m_ownsNetworkAccessManager = false;
    }
    m_networkAccessManager = manager;
}

QString PDFOCRModelManager::getBuiltInSetId(const QString& engineId) const
{
    QFile manifestFile(m_builtInDirectory + QStringLiteral("/") + sanitizeIdentifier(engineId) + QStringLiteral("/fast/manifest.json"));
    if (manifestFile.open(QFile::ReadOnly))
    {
        return QJsonDocument::fromJson(manifestFile.readAll()).object().value(QStringLiteral("setId")).toString();
    }
    return QString();
}

void PDFOCRModelManager::setMaximumParallelDownloads(int count)
{
    m_maximumParallelDownloads = qBound(1, count, 8);
}

QString PDFOCRModelManager::getModelTargetPath(const PDFOCRCatalogEntry& entry) const
{
    const QString setId = sanitizeIdentifier(m_catalog.getSetId(entry.profile));
    return getEngineUserDirectory(entry.engineId) + QStringLiteral("/") + getProfileDirectoryName(entry.profile) + QStringLiteral("/") + setId + QStringLiteral("/tessdata/") + entry.fileName;
}

void PDFOCRModelManager::updateModelState(const QString& id, PDFOCRModelState state, const QString& errorMessage, int progress)
{
    if (state == PDFOCRModelState::Error || state == PDFOCRModelState::Incompatible)
    {
        m_errorStates[id] = std::make_pair(state, errorMessage);
    }
    else
    {
        m_errorStates.erase(id);
    }

    {
        QMutexLocker lock(&m_mutex);
        for (PDFOCRModelInfo& model : m_models)
        {
            if (model.id == id)
            {
                // A failed update must not make the installed version unusable (LANG-11)
                const bool keepInstalledState = (state == PDFOCRModelState::Error || state == PDFOCRModelState::Incompatible) && !model.path.isEmpty() && QFile::exists(model.path);
                if (!keepInstalledState)
                {
                    model.state = state;
                }
                model.errorMessage = errorMessage;
                model.downloadProgress = progress;
            }
        }
    }

    Q_EMIT modelStateChanged(id);
}

void PDFOCRModelManager::download(const QStringList& modelIds)
{
    // Dependencies of the catalog entries, which are not installed yet, are
    // enqueued before the model itself (LANG-06)
    QStringList orderedIds;
    std::function<void(const QString&, int)> collect = [&](const QString& modelId, int depth)
    {
        if (depth > 8 || orderedIds.contains(modelId))
        {
            return;
        }

        if (const PDFOCRCatalogEntry* entry = m_catalog.find(modelId))
        {
            for (const QString& dependencyId : entry->dependencies)
            {
                std::optional<PDFOCRModelInfo> dependency = getModel(dependencyId);
                if (dependencyId == modelId || (dependency && dependency->isUsable()))
                {
                    continue;
                }
                collect(dependencyId, depth + 1);
            }
        }

        orderedIds << modelId;
    };

    for (const QString& modelId : modelIds)
    {
        collect(modelId, 0);
    }

    for (const QString& modelId : orderedIds)
    {
        const PDFOCRCatalogEntry* entry = m_catalog.find(modelId);
        if (!entry)
        {
            Q_EMIT downloadFinished(modelId, false, PDFTranslationContext::tr("Model '%1' is not in the catalog.").arg(modelId));
            continue;
        }

        bool active = std::any_of(m_activeDownloads.begin(), m_activeDownloads.end(), [&modelId](const auto& download) { return download->modelId == modelId; });
        bool queued = std::find(m_downloadQueue.begin(), m_downloadQueue.end(), modelId) != m_downloadQueue.end();
        if (active || queued)
        {
            continue;
        }

        m_downloadQueue.push_back(modelId);
        updateModelState(modelId, PDFOCRModelState::Downloading, QString(), 0);
    }

    Q_EMIT downloadQueueChanged();
    startNextDownloads();
}

void PDFOCRModelManager::startNextDownloads()
{
    while (int(m_activeDownloads.size()) < m_maximumParallelDownloads && !m_downloadQueue.empty())
    {
        const QString modelId = m_downloadQueue.front();
        m_downloadQueue.erase(m_downloadQueue.begin());

        const PDFOCRCatalogEntry* entry = m_catalog.find(modelId);
        if (!entry)
        {
            continue;
        }

        // Validate the URL (LANG-11)
        const QUrl url(entry->url);
        const bool isLoopback = url.host() == QStringLiteral("127.0.0.1") || url.host() == QStringLiteral("localhost") || url.host() == QStringLiteral("::1");
        const bool schemeAllowed = url.scheme() == QStringLiteral("https") || (m_allowInsecureLoopback && isLoopback && url.scheme() == QStringLiteral("http"));
        if (!url.isValid() || !schemeAllowed)
        {
            updateModelState(modelId, PDFOCRModelState::Error, PDFTranslationContext::tr("Invalid or insecure download URL."), 0);
            Q_EMIT downloadFinished(modelId, false, PDFTranslationContext::tr("Invalid or insecure download URL '%1'.").arg(entry->url));
            continue;
        }

        // Validate the target path (must be inside the user directory)
        const QString targetPath = getModelTargetPath(*entry);
        if (!isInsideDirectory(targetPath, m_userDirectory) || entry->fileName.contains(QStringLiteral("..")) || entry->fileName.contains(QChar(0x5C)))
        {
            updateModelState(modelId, PDFOCRModelState::Error, PDFTranslationContext::tr("Invalid target path."), 0);
            Q_EMIT downloadFinished(modelId, false, PDFTranslationContext::tr("Invalid target path of the model '%1'.").arg(modelId));
            continue;
        }

        // Free space
        const qint64 availableBytes = getAvailableSpace();
        if (availableBytes >= 0 && availableBytes < entry->size * 2 + (qint64(16) << 20))
        {
            const QString message = PDFTranslationContext::tr("Not enough free space in '%1' for the model '%2' (%3 MB required).").arg(m_userDirectory, entry->name).arg(entry->size / (1024 * 1024) + 1);
            updateModelState(modelId, PDFOCRModelState::Error, message, 0);
            Q_EMIT downloadFinished(modelId, false, message);
            continue;
        }

        if (!m_networkAccessManager)
        {
            QNetworkProxyFactory::setUseSystemConfiguration(true);
            m_networkAccessManager = new QNetworkAccessManager(this);
            m_ownsNetworkAccessManager = true;
        }

        const QString downloadDirectory = m_userDirectory + QStringLiteral("/downloads");
        QDir().mkpath(downloadDirectory);

        auto download = std::make_unique<Download>();
        download->modelId = modelId;
        download->entry = *entry;
        download->temporaryPath = downloadDirectory + QStringLiteral("/") + sanitizeIdentifier(modelId) + QStringLiteral(".") + QUuid::createUuid().toString(QUuid::Id128).left(8) + QStringLiteral(".part");
        download->file = std::make_unique<QFile>(download->temporaryPath);
        if (!download->file->open(QFile::WriteOnly | QFile::Truncate))
        {
            const QString message = PDFTranslationContext::tr("Cannot create temporary file '%1' (%2).").arg(download->temporaryPath, download->file->errorString());
            updateModelState(modelId, PDFOCRModelState::Error, message, 0);
            Q_EMIT downloadFinished(modelId, false, message);
            continue;
        }

        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(120000);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PDF4QT OCR model downloader"));

        QNetworkReply* reply = m_networkAccessManager->get(request);
        download->reply = reply;
        Download* downloadPointer = download.get();
        m_activeDownloads.push_back(std::move(download));

        connect(reply, &QNetworkReply::readyRead, this, [this, downloadPointer]() { onDownloadReadyRead(downloadPointer); });
        connect(reply, &QNetworkReply::finished, this, [this, downloadPointer]() { onDownloadFinished(downloadPointer); });
        connect(reply, &QNetworkReply::downloadProgress, this, [this, downloadPointer](qint64 received, qint64 total)
        {
            const qint64 expected = downloadPointer->entry.size > 0 ? downloadPointer->entry.size : total;
            const int progress = expected > 0 ? int(qBound(qint64(0), qint64(100) * received / expected, qint64(100))) : 0;
            updateModelState(downloadPointer->modelId, PDFOCRModelState::Downloading, QString(), progress);
            Q_EMIT downloadProgress(downloadPointer->modelId, received, expected);
        });

        updateModelState(modelId, PDFOCRModelState::Downloading, QString(), 0);
    }

    Q_EMIT downloadQueueChanged();
}

void PDFOCRModelManager::onDownloadReadyRead(Download* download)
{
    if (!download->reply || !download->file)
    {
        return;
    }

    const QByteArray data = download->reply->readAll();
    download->received += data.size();

    // Refuse files larger than declared (LANG-11)
    if (download->entry.size > 0 && download->received > download->entry.size)
    {
        download->cancelled = true;
        download->reply->abort();
        return;
    }

    if (download->file->write(data) != data.size())
    {
        download->writeFailed = true;
        download->cancelled = true;
        download->reply->abort();
    }
}

void PDFOCRModelManager::onDownloadFinished(Download* downloadPointer)
{
    auto it = std::find_if(m_activeDownloads.begin(), m_activeDownloads.end(), [downloadPointer](const auto& item) { return item.get() == downloadPointer; });
    if (it == m_activeDownloads.end())
    {
        return;
    }

    std::unique_ptr<Download> download = std::move(*it);
    m_activeDownloads.erase(it);

    QNetworkReply* reply = download->reply;
    download->reply = nullptr;
    reply->deleteLater();

    // Remaining data
    if (!download->cancelled && download->file)
    {
        const QByteArray data = reply->readAll();
        download->received += data.size();
        if (download->file->write(data) != data.size())
        {
            download->writeFailed = true;
        }
    }

    if (download->file && !download->file->flush())
    {
        download->writeFailed = true;
    }

    if (download->file)
    {
        download->file->close();
    }

    if (download->writeFailed)
    {
        const QString directory = QFileInfo(download->temporaryPath).absolutePath();
        finishDownload(std::move(download), false, PDFTranslationContext::tr("Downloaded data cannot be written into '%1'. Check the free space on the disk.").arg(directory));
        return;
    }

    if (download->cancelled)
    {
        const bool userCancelled = reply->error() == QNetworkReply::OperationCanceledError && download->received <= download->entry.size;
        finishDownload(std::move(download), false, userCancelled ? PDFTranslationContext::tr("Download was cancelled.") : PDFTranslationContext::tr("Downloaded file is larger than declared in the catalog."));
        return;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString message = PDFTranslationContext::tr("Network error: %1").arg(reply->errorString());
        if (status > 0)
        {
            message = PDFTranslationContext::tr("Network error (HTTP %1): %2").arg(status).arg(reply->errorString());
        }
        finishDownload(std::move(download), false, message);
        return;
    }

    // Content type must not be HTML (LANG-11)
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
    if (contentType.contains(QStringLiteral("text/html")))
    {
        finishDownload(std::move(download), false, PDFTranslationContext::tr("Server returned a HTML page instead of the model file."));
        return;
    }

    // Size
    if (download->entry.size > 0 && download->received != download->entry.size)
    {
        finishDownload(std::move(download), false, PDFTranslationContext::tr("Downloaded file has size %1 bytes, but %2 bytes were expected.").arg(download->received).arg(download->entry.size));
        return;
    }

    // HTML content check
    {
        QFile file(download->temporaryPath);
        if (file.open(QFile::ReadOnly))
        {
            const QByteArray head = file.read(64).trimmed().toLower();
            if (head.startsWith("<!doctype") || head.startsWith("<html"))
            {
                finishDownload(std::move(download), false, PDFTranslationContext::tr("Downloaded file is a HTML page, not a model file."));
                return;
            }
        }
    }

    // Hash, loadability by the engine and the installation are slow (the engine loads
    // the model for the verification, the installation waits for the lock of the data
    // directory), so they run in the worker thread (R12). The download stays active
    // until the outcome is delivered back to this thread by a queued call.
    updateModelState(download->modelId, PDFOCRModelState::Verifying, QString(), 100);
    download->verifying = true;

    Download* verifyingDownload = download.get();
    m_activeDownloads.push_back(std::move(download));

    const QString userDirectory = m_userDirectory;
    const ModelValidator validator = m_validator;
    const PDFOCRCatalogEntry entry = verifyingDownload->entry;
    const QString temporaryPath = verifyingDownload->temporaryPath;
    const QString targetPath = getModelTargetPath(entry);
    const QString engineVersion = getEngineVersion(entry.engineId);

    m_verificationPool.start([this, verifyingDownload, userDirectory, validator, entry, temporaryPath, targetPath, engineVersion]()
    {
        const VerificationOutcome outcome = verifyAndInstall(userDirectory, validator, entry, temporaryPath, targetPath, engineVersion);
        QMetaObject::invokeMethod(this, [this, verifyingDownload, outcome]() { completeDownload(verifyingDownload, outcome); }, Qt::QueuedConnection);
    });
}

PDFOCRModelManager::VerificationOutcome PDFOCRModelManager::verifyAndInstall(const QString& userDirectory,
                                                                             const ModelValidator& validator,
                                                                             const PDFOCRCatalogEntry& entry,
                                                                             const QString& temporaryPath,
                                                                             const QString& targetPath,
                                                                             const QString& engineVersion)
{
    VerificationOutcome outcome;

    // Hash
    const QString sha256 = computeSha256(temporaryPath);
    if (sha256.isEmpty() || sha256 != entry.sha256)
    {
        outcome.message = PDFTranslationContext::tr("Checksum of the downloaded file does not match the catalog.");
        return outcome;
    }

    // Loadability by the engine (LANG-08): a model, which the engine cannot load,
    // is incompatible and it is not installed
    const PDFOCRError validationError = validateModelFile(userDirectory, validator, entry.engineId, temporaryPath, entry.language);
    if (validationError)
    {
        outcome.message = validationError.message;
        outcome.failureState = validationError.code == PDFOCRErrorCode::IncompatibleModel ? PDFOCRModelState::Incompatible : PDFOCRModelState::Error;
        return outcome;
    }

    // Atomic activation
    const PDFOCRError installError = installModelFile(userDirectory, temporaryPath, targetPath);
    if (installError)
    {
        outcome.message = installError.message;
        return outcome;
    }

    // Record the hash next to the file
    QSaveFile hashFile(targetPath + QStringLiteral(".sha256"));
    if (hashFile.open(QFile::WriteOnly | QFile::Truncate))
    {
        hashFile.write(sha256.toLatin1());
        hashFile.commit();
    }

    // Installation metadata (LANG-12): the engine version is checked after an upgrade
    {
        QJsonObject metadata;
        metadata[QStringLiteral("format")] = QStringLiteral("pdf4qt-ocr-install");
        metadata[QStringLiteral("version")] = 1;
        metadata[QStringLiteral("modelId")] = entry.id;
        metadata[QStringLiteral("engineId")] = entry.engineId;
        metadata[QStringLiteral("engineVersion")] = engineVersion;
        metadata[QStringLiteral("modelVersion")] = entry.version;
        metadata[QStringLiteral("sha256")] = sha256;
        metadata[QStringLiteral("installed")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        QSaveFile metadataFile(getInstallMetadataPath(targetPath));
        if (metadataFile.open(QFile::WriteOnly | QFile::Truncate))
        {
            metadataFile.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented));
            metadataFile.commit();
        }
    }

    // Older downloaded versions of the model are removed. Running recognitions
    // are not affected, because they use their own runtime set (LANG-07).
    {
        const QString profileDirectoryPath = userDirectory + QStringLiteral("/") + sanitizeIdentifier(entry.engineId) + QStringLiteral("/") + getProfileDirectoryName(entry.profile);
        const QFileInfoList sets = QDir(profileDirectoryPath).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& setInfo : sets)
        {
            const QString oldPath = setInfo.absoluteFilePath() + QStringLiteral("/tessdata/") + entry.fileName;
            if (QFileInfo(oldPath) != QFileInfo(targetPath) && QFile::exists(oldPath))
            {
                QFile::remove(oldPath);
                QFile::remove(oldPath + QStringLiteral(".sha256"));
                QFile::remove(getInstallMetadataPath(oldPath));
            }
        }
    }

    outcome.success = true;
    outcome.message = PDFTranslationContext::tr("Model '%1' was installed.").arg(entry.name);
    return outcome;
}

void PDFOCRModelManager::completeDownload(Download* downloadPointer, const VerificationOutcome& outcome)
{
    auto it = std::find_if(m_activeDownloads.begin(), m_activeDownloads.end(), [downloadPointer](const auto& item) { return item.get() == downloadPointer; });
    if (it == m_activeDownloads.end())
    {
        return;
    }

    std::unique_ptr<Download> download = std::move(*it);
    m_activeDownloads.erase(it);

    finishDownload(std::move(download), outcome.success, outcome.message, outcome.failureState);
}

void PDFOCRModelManager::finishDownload(std::unique_ptr<Download> download, bool success, const QString& message, PDFOCRModelState failureState)
{
    if (download->file)
    {
        download->file->close();
        download->file.reset();
    }
    QFile::remove(download->temporaryPath);

    const QString modelId = download->modelId;
    download.reset();

    refresh();

    if (!success)
    {
        updateModelState(modelId, failureState, message, 0);
    }

    Q_EMIT downloadFinished(modelId, success, message);
    Q_EMIT downloadQueueChanged();

    startNextDownloads();
}

PDFOCRError PDFOCRModelManager::validateModelFile(const QString& userDirectory, const ModelValidator& validator, const QString& engineId, const QString& filePath, const QString& language)
{
    // The engine loads the model from a directory, so the file is placed
    // into a temporary directory under the expected name.
    const QString languageCode = language.split(QChar('/')).last();
    const QString temporaryDirectory = userDirectory + QStringLiteral("/downloads/verify-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
    QDir().mkpath(temporaryDirectory);
    const QString temporaryFile = temporaryDirectory + QStringLiteral("/") + languageCode + QLatin1String(TRAINEDDATA_SUFFIX);

    PDFOCRError result;
    if (!QFile::copy(filePath, temporaryFile))
    {
        result = PDFOCRError::create(PDFOCRErrorCode::VerificationFailed, PDFTranslationContext::tr("Cannot prepare the model for verification."), PDFTranslationContext::tr("Model verification"));
    }
    else if (validator)
    {
        result = validator(engineId, temporaryDirectory, languageCode);
    }
    else if (std::shared_ptr<PDFOCREngineFactory> factory = PDFOCREngineRegistry::getInstance()->getFactory(engineId))
    {
        result = factory->validateModel(temporaryDirectory, languageCode);
    }

    QDir(temporaryDirectory).removeRecursively();
    return result;
}

PDFOCRError PDFOCRModelManager::installModelFile(const QString& userDirectory, const QString& temporaryPath, const QString& targetPath)
{
    std::unique_ptr<QLockFile> lock;
    if (PDFOCRError lockError = acquireLock(userDirectory, lock))
    {
        return lockError;
    }

    QDir().mkpath(QFileInfo(targetPath).absolutePath());

    // Existing model is replaced only after the new file is completely in place
    const QString stagingPath = targetPath + QStringLiteral(".new");
    QFile::remove(stagingPath);
    if (!QFile::copy(temporaryPath, stagingPath))
    {
        return PDFOCRError::create(PDFOCRErrorCode::OutOfDiskSpace, PDFTranslationContext::tr("Cannot write the model into '%1'. Check the free space and the permissions of the directory.").arg(QFileInfo(targetPath).absolutePath()), PDFTranslationContext::tr("Model installation"));
    }

    const QString backupPath = targetPath + QStringLiteral(".old");
    QFile::remove(backupPath);
    const bool hadOld = QFile::exists(targetPath);
    if (hadOld && !QFile::rename(targetPath, backupPath))
    {
        QFile::remove(stagingPath);
        return PDFOCRError::create(PDFOCRErrorCode::WriteFailed, PDFTranslationContext::tr("Cannot replace the existing model '%1'.").arg(targetPath), PDFTranslationContext::tr("Model installation"));
    }

    if (!QFile::rename(stagingPath, targetPath))
    {
        if (hadOld)
        {
            QFile::rename(backupPath, targetPath);
        }
        QFile::remove(stagingPath);
        return PDFOCRError::create(PDFOCRErrorCode::WriteFailed, PDFTranslationContext::tr("Cannot activate the model '%1'.").arg(targetPath), PDFTranslationContext::tr("Model installation"));
    }

    QFile::remove(backupPath);
    return PDFOCRError::none();
}

void PDFOCRModelManager::cancelDownload(const QString& modelId)
{
    auto queueIt = std::find(m_downloadQueue.begin(), m_downloadQueue.end(), modelId);
    if (queueIt != m_downloadQueue.end())
    {
        m_downloadQueue.erase(queueIt);
        refresh();
        Q_EMIT downloadFinished(modelId, false, PDFTranslationContext::tr("Download was cancelled."));
        Q_EMIT downloadQueueChanged();
        return;
    }

    for (const auto& download : m_activeDownloads)
    {
        if (download->modelId == modelId && download->reply)
        {
            download->cancelled = true;
            download->reply->abort();
            return;
        }
    }
}

void PDFOCRModelManager::cancelAllDownloads()
{
    const std::vector<QString> queue = m_downloadQueue;
    for (const QString& modelId : queue)
    {
        cancelDownload(modelId);
    }

    std::vector<QString> active;
    for (const auto& download : m_activeDownloads)
    {
        active.push_back(download->modelId);
    }
    for (const QString& modelId : active)
    {
        cancelDownload(modelId);
    }
}

bool PDFOCRModelManager::isDownloading() const
{
    return !m_activeDownloads.empty() || !m_downloadQueue.empty();
}

QStringList PDFOCRModelManager::getDownloadQueue() const
{
    QStringList result;
    for (const auto& download : m_activeDownloads)
    {
        result << download->modelId;
    }
    for (const QString& modelId : m_downloadQueue)
    {
        result << modelId;
    }
    return result;
}

PDFOCRError PDFOCRModelManager::importModel(const QString& filePath, const QString& engineId, PDFOCRModelProfile profile, QString* modelId)
{
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        return PDFOCRError::create(PDFOCRErrorCode::MissingModel, PDFTranslationContext::tr("File '%1' does not exist.").arg(filePath), PDFTranslationContext::tr("Model import"));
    }

    if (fileInfo.suffix().toLower() != QStringLiteral("traineddata"))
    {
        return PDFOCRError::create(PDFOCRErrorCode::IncompatibleModel, PDFTranslationContext::tr("File '%1' is not a Tesseract model (.traineddata).").arg(filePath), PDFTranslationContext::tr("Model import"));
    }

    const QString language = fileInfo.completeBaseName();
    if (!isValidLanguageCode(language))
    {
        return PDFOCRError::create(PDFOCRErrorCode::IncompatibleModel, PDFTranslationContext::tr("Invalid language code '%1'.").arg(language), PDFTranslationContext::tr("Model import"));
    }

    const PDFOCRError validationError = validateModelFile(m_userDirectory, m_validator, engineId, filePath, language);
    if (validationError)
    {
        return validationError;
    }

    std::unique_ptr<QLockFile> lock;
    if (PDFOCRError lockError = acquireLock(lock))
    {
        return lockError;
    }

    const QString importId = QUuid::createUuid().toString(QUuid::Id128).left(12);
    const QString importDirectory = getEngineUserDirectory(engineId) + QStringLiteral("/custom/") + importId;
    QDir().mkpath(importDirectory + QStringLiteral("/tessdata"));

    const QString targetPath = importDirectory + QStringLiteral("/tessdata/") + language + QLatin1String(TRAINEDDATA_SUFFIX);
    if (!QFile::copy(filePath, targetPath))
    {
        QDir(importDirectory).removeRecursively();
        return PDFOCRError::create(PDFOCRErrorCode::OutOfDiskSpace, PDFTranslationContext::tr("Cannot copy the model into '%1'.").arg(importDirectory), PDFTranslationContext::tr("Model import"));
    }

    QJsonObject import;
    import[QStringLiteral("format")] = QStringLiteral("pdf4qt-ocr-import");
    import[QStringLiteral("version")] = 1;
    import[QStringLiteral("profile")] = PDFOCRConfiguration::getProfileIdentifier(profile);
    import[QStringLiteral("language")] = language;
    import[QStringLiteral("name")] = PDFTranslationContext::tr("%1 (imported)").arg(getLanguageName(engineId, language));
    import[QStringLiteral("sourceFile")] = fileInfo.fileName();
    import[QStringLiteral("sha256")] = computeSha256(filePath);
    import[QStringLiteral("imported")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    import[QStringLiteral("originVerified")] = false;
    import[QStringLiteral("engineVersion")] = getEngineVersion(engineId);

    QSaveFile importFile(importDirectory + QStringLiteral("/") + QLatin1String(IMPORT_FILE));
    if (!importFile.open(QFile::WriteOnly | QFile::Truncate))
    {
        QDir(importDirectory).removeRecursively();
        return PDFOCRError::create(PDFOCRErrorCode::WriteFailed, PDFTranslationContext::tr("Cannot write into '%1'.").arg(importDirectory), PDFTranslationContext::tr("Model import"));
    }
    importFile.write(QJsonDocument(import).toJson(QJsonDocument::Indented));
    importFile.commit();

    lock.reset();
    refresh();

    if (modelId)
    {
        *modelId = QStringLiteral("%1/custom/%2/%3").arg(engineId, importId, language);
    }

    return PDFOCRError::none();
}

PDFOCRError PDFOCRModelManager::removeUserModel(const QString& modelId)
{
    std::optional<PDFOCRModelInfo> model = getModel(modelId);
    if (!model)
    {
        return PDFOCRError::create(PDFOCRErrorCode::MissingModel, PDFTranslationContext::tr("Model '%1' was not found.").arg(modelId), PDFTranslationContext::tr("Model removal"));
    }

    if (model->origin == PDFOCRModelOrigin::BuiltIn || model->origin == PDFOCRModelOrigin::None)
    {
        return PDFOCRError::create(PDFOCRErrorCode::InsufficientPermissions, PDFTranslationContext::tr("Built-in model '%1' cannot be removed. It can be hidden in the language selection.").arg(model->name), PDFTranslationContext::tr("Model removal"));
    }

    if (model->state == PDFOCRModelState::Downloading || model->state == PDFOCRModelState::Verifying)
    {
        return PDFOCRError::create(PDFOCRErrorCode::InvalidConfiguration, PDFTranslationContext::tr("Model '%1' is being downloaded.").arg(model->name), PDFTranslationContext::tr("Model removal"));
    }

    // Path must be inside the user directory
    if (!isInsideDirectory(model->path, m_userDirectory))
    {
        return PDFOCRError::create(PDFOCRErrorCode::InsufficientPermissions, PDFTranslationContext::tr("Model '%1' is not a user model.").arg(model->name), PDFTranslationContext::tr("Model removal"));
    }

    std::unique_ptr<QLockFile> lock;
    if (PDFOCRError lockError = acquireLock(lock))
    {
        return lockError;
    }

    if (model->origin == PDFOCRModelOrigin::Imported)
    {
        // Remove the whole import directory
        QDir importDirectory(QFileInfo(model->path).absolutePath());
        importDirectory.cdUp();
        if (!importDirectory.removeRecursively())
        {
            return PDFOCRError::create(PDFOCRErrorCode::WriteFailed, PDFTranslationContext::tr("Cannot remove the model '%1'.").arg(model->path), PDFTranslationContext::tr("Model removal"));
        }
    }
    else
    {
        if (!QFile::remove(model->path))
        {
            return PDFOCRError::create(PDFOCRErrorCode::WriteFailed, PDFTranslationContext::tr("Cannot remove the model '%1'.").arg(model->path), PDFTranslationContext::tr("Model removal"));
        }
        QFile::remove(model->path + QStringLiteral(".sha256"));
        QFile::remove(getInstallMetadataPath(model->path));
    }

    lock.reset();
    refresh();
    return PDFOCRError::none();
}

PDFOCRError PDFOCRModelManager::cleanRuntimeSets()
{
    std::unique_ptr<QLockFile> lock;
    if (PDFOCRError lockError = acquireLock(lock))
    {
        return lockError;
    }

    // Sets leased by a running recognition (of this or of another instance) are kept (LANG-07)
    QDir runtimeDirectory(getRuntimeDirectory(QStringLiteral("tesseract")));
    if (runtimeDirectory.exists())
    {
        const QFileInfoList sets = runtimeDirectory.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo& info : sets)
        {
            if (info.isDir() && isRuntimeSetInUse(info.absoluteFilePath()))
            {
                continue;
            }

            if (info.isDir())
            {
                QDirIterator fileIt(info.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
                while (fileIt.hasNext())
                {
                    // Models of the runtime sets are read-only
                    QFile::setPermissions(fileIt.next(), QFile::ReadOwner | QFile::WriteOwner);
                }
            }

            const bool removed = info.isDir() ? QDir(info.absoluteFilePath()).removeRecursively() : QFile::remove(info.absoluteFilePath());
            if (!removed)
            {
                return PDFOCRError::create(PDFOCRErrorCode::WriteFailed, PDFTranslationContext::tr("Cannot remove the runtime set '%1'. A model set may be in use.").arg(info.absoluteFilePath()), PDFTranslationContext::tr("Cache cleanup"));
            }
        }
    }

    QDir downloadDirectory(m_userDirectory + QStringLiteral("/downloads"));
    if (downloadDirectory.exists() && !isDownloading())
    {
        downloadDirectory.removeRecursively();
    }

    return PDFOCRError::none();
}

}   // namespace pdf
