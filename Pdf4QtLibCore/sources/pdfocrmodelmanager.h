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

#ifndef PDFOCRMODELMANAGER_H
#define PDFOCRMODELMANAGER_H

#include "pdfglobal.h"
#include "pdfocrmodel.h"
#include "pdfocrengine.h"
#include "pdfocrconfiguration.h"

#include <QObject>
#include <QMutex>
#include <QThreadPool>
#include <QJsonObject>

#include <map>
#include <memory>
#include <functional>

class QFile;
class QLockFile;
class QNetworkReply;
class QNetworkAccessManager;

namespace pdf
{

/// Entry of the model catalog (LANG-09)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRCatalogEntry
{
    QString id;
    QString engineId;
    QString language;
    QString name;
    PDFOCRModelProfile profile = PDFOCRModelProfile::Fast;
    QString family;
    QString version;
    QString url;
    QString fileName;
    qint64 size = 0;
    QString sha256;
    QString license;
    QStringList dependencies;

    bool isOrientationData() const { return language == QStringLiteral("osd"); }
};

/// Model catalog shipped with the application (LANG-09, LANG-11)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRCatalog
{
    int version = 0;
    QString engineId;
    QString generated;
    QString compatibility;
    std::map<QString, QString> sourceRepositories;
    std::map<QString, QString> sourceCommits;
    std::vector<PDFOCRCatalogEntry> entries;

    bool isValid() const { return version > 0 && !entries.empty(); }
    const PDFOCRCatalogEntry* find(const QString& id) const;

    static PDFOCRCatalog fromJson(const QJsonObject& object);
    static PDFOCRCatalog fromBytes(const QByteArray& data, QString* errorMessage);

    /// Returns identifier of the model set of the profile (derived from the commit)
    QString getSetId(PDFOCRModelProfile profile) const;
};

/// State of the model (LANG-08)
enum class PDFOCRModelState
{
    BuiltIn,
    Installed,
    Available,
    UpdateAvailable,
    Downloading,
    Verifying,
    Error,
    Incompatible
};

/// Origin of the model
enum class PDFOCRModelOrigin
{
    None,
    BuiltIn,
    Downloaded,
    Imported
};

/// Information about the model (LANG-08)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRModelInfo
{
    /// Model identifier (e.g. "tesseract/fast/ces" or "tesseract/custom/<import>/ces")
    QString id;
    QString engineId;

    /// Language code usable in the configuration (e.g. "ces" or "ces@<import>")
    QString language;
    QString name;
    PDFOCRModelProfile profile = PDFOCRModelProfile::Fast;
    QString family;
    PDFOCRModelOrigin origin = PDFOCRModelOrigin::None;
    PDFOCRModelState state = PDFOCRModelState::Available;
    QString version;
    QString installedVersion;
    qint64 size = 0;
    QString path;
    QString sha256;

    /// Checksum of the installed file (can differ from the catalog, when an update is available)
    QString installedSha256;
    QString license;
    bool isHidden = false;
    bool catalogVerified = false;
    QString errorMessage;
    int downloadProgress = 0;

    bool isOrientationData() const { return family == QStringLiteral("osd") || language == QStringLiteral("osd"); }
    bool isUsable() const { return state == PDFOCRModelState::BuiltIn || state == PDFOCRModelState::Installed || state == PDFOCRModelState::UpdateAvailable; }

    /// Returns translated name of the state
    static QString getStateName(PDFOCRModelState state);

    /// Returns translated name of the origin
    static QString getOriginName(PDFOCRModelOrigin origin);
};

/// Manager of the OCR language models (chapter 7.2 and 7.3 of the OCR
/// specification). Built-in models are located in the distribution data
/// directory, user models in the application data directory next to the
/// certificate directory. The manager builds unified runtime sets for the
/// engine, downloads and verifies models from the catalog and imports
/// local model files.
class PDF4QTLIBCORESHARED_EXPORT PDFOCRModelManager : public QObject
{
    Q_OBJECT

public:
    explicit PDFOCRModelManager(QObject* parent);
    virtual ~PDFOCRModelManager() override;

    /// Returns the application data root (same as used for the certificates)
    static QString getApplicationDataRoot();

    /// Returns the OCR data directory of the user (<AppDataLocation>/ocr)
    static QString getDefaultUserDirectory();

    /// Returns the default built-in data directory (distribution data)
    static QString getDefaultBuiltInDirectory();

    void setUserDirectory(const QString& directory);
    QString getUserDirectory() const { return m_userDirectory; }

    void setBuiltInDirectory(const QString& directory);
    QString getBuiltInDirectory() const { return m_builtInDirectory; }

    /// Loads the catalog bundled with the application
    void loadBundledCatalog();

    void setCatalog(PDFOCRCatalog catalog);
    const PDFOCRCatalog& getCatalog() const { return m_catalog; }

    /// Rescans the directories and updates the model list
    void refresh();

    /// Returns all models
    std::vector<PDFOCRModelInfo> getModels() const;

    /// Returns model by identifier
    std::optional<PDFOCRModelInfo> getModel(const QString& id) const;

    /// Returns usable (built-in or installed) language models of the engine and profile,
    /// without orientation data. Hidden models are excluded, if requested.
    std::vector<PDFOCRModelInfo> getUsableLanguageModels(const QString& engineId, PDFOCRModelProfile profile, bool includeHidden) const;

    /// Returns true, if language model is usable in the profile
    bool isLanguageUsable(const QString& engineId, const QString& language, PDFOCRModelProfile profile) const;

    /// Returns true, if orientation data are usable
    bool isOrientationDataUsable(const QString& engineId, PDFOCRModelProfile profile) const;

    /// Returns human readable display text of the language model, e.g. "Czech (ces) - built-in - Fast"
    static QString getModelDisplayText(const PDFOCRModelInfo& model);

    /// Returns display name of the language code (from catalog), or the code itself
    QString getLanguageName(const QString& engineId, const QString& language) const;

    /// Hides/unhides the model in the language selection (LANG-12)
    void setModelHidden(const QString& id, bool hidden);

    /// Resolves the model set for the recognition (LANG-05, LANG-06). Builds
    /// an immutable unified runtime set from the built-in and user models,
    /// including the installed dependencies of the selected models (from the
    /// catalog). Never falls back to a different model silently.
    ///
    /// The function can be called from a worker thread (R12): it works on a
    /// snapshot of the model list and of the catalog taken under the mutex and
    /// touches only the file system. The directories of the manager must not be
    /// changed while it runs.
    PDFOCRResolvedModelSet resolveModelSet(const QString& engineId,
                                           const QStringList& languages,
                                           PDFOCRModelProfile profile,
                                           PDFOCRError* error);

    /// Returns model identifiers, which are missing for the languages and the profile
    QStringList getMissingModels(const QString& engineId, const QStringList& languages, PDFOCRModelProfile profile) const;

    /// Starts the download of the models (LANG-10, LANG-11). Models already
    /// being downloaded are ignored.
    void download(const QStringList& modelIds);

    /// Cancels the download
    void cancelDownload(const QString& modelId);

    /// Cancels all downloads
    void cancelAllDownloads();

    /// Returns true, if any download is in progress or queued
    bool isDownloading() const;

    /// Returns identifiers of the models being downloaded or queued
    QStringList getDownloadQueue() const;

    void setMaximumParallelDownloads(int count);

    /// Imports the model file (LANG-13). The model is copied into the custom directory.
    PDFOCRError importModel(const QString& filePath, const QString& engineId, PDFOCRModelProfile profile, QString* modelId);

    /// Task of the import, which can run on a worker thread (JOB-01): it validates the
    /// model by the engine, takes the lock of the data directory, copies and hashes the
    /// file. It does not touch the state of the manager; call refresh() on the thread
    /// of the manager after a successful import.
    using ImportTask = std::function<PDFOCRError(QString* modelId)>;
    ImportTask createImportTask(const QString& filePath, const QString& engineId, PDFOCRModelProfile profile) const;

    /// Removes the user model (downloaded or imported), never a built-in model (LANG-12)
    PDFOCRError removeUserModel(const QString& modelId);

    /// Removes all runtime sets (cache); models are preserved (OPS-06). Sets leased
    /// by a running recognition are kept (LANG-07).
    PDFOCRError cleanRuntimeSets();

    /// Acquires the lease of the runtime set (LANG-07): a running recognition holds
    /// the lease for its lifetime, so the housekeeping and the cache cleanup (also of
    /// another instance of the application) do not remove the set. The lease is the
    /// lock file "<set directory>/in-use.<pid>.<counter>.lock", the set directory is
    /// the parent of the data path ("tessdata"). Returns nullptr, if the data path is
    /// not a runtime set of the manager (for example the data of a test engine).
    static std::unique_ptr<QLockFile> acquireRuntimeSetLease(const QString& dataPath);

    /// Returns true, if any lease of the runtime set is held by a living process.
    /// Stale leases of dead processes are removed.
    static bool isRuntimeSetInUse(const QString& setDirectory);

    /// Returns the runtime directory
    QString getRuntimeDirectory(const QString& engineId) const;

    /// Returns the directory of the user models of the engine
    QString getEngineUserDirectory(const QString& engineId) const;

    /// Returns free space in the user directory in bytes (-1 if unknown)
    qint64 getAvailableSpace() const;

    /// Sets the network access manager (owned by the caller). If not set,
    /// an internal manager is created.
    void setNetworkAccessManager(QNetworkAccessManager* manager);

    /// Allows plain http for tests (loopback only)
    void setAllowInsecureLoopback(bool allow) { m_allowInsecureLoopback = allow; }

    /// Returns the version of the built-in model set (from the manifest)
    QString getBuiltInSetId(const QString& engineId) const;

    /// Validator of the model (engine dependent). If not set, the engine factory
    /// registered in the registry is used.
    using ModelValidator = std::function<PDFOCRError(const QString& engineId, const QString& dataPath, const QString& language)>;
    void setModelValidator(ModelValidator validator) { m_validator = std::move(validator); }

signals:
    void modelsChanged();
    void modelStateChanged(const QString& modelId);
    void downloadProgress(const QString& modelId, qint64 received, qint64 total);
    void downloadFinished(const QString& modelId, bool success, const QString& message);
    void downloadQueueChanged();

private:
    struct Download
    {
        QString modelId;
        PDFOCRCatalogEntry entry;
        QNetworkReply* reply = nullptr;
        std::unique_ptr<QFile> file;
        QString temporaryPath;
        qint64 received = 0;
        bool cancelled = false;
        bool writeFailed = false;   ///< Data cannot be written (full disk), reported as such and not as a wrong checksum
        bool verifying = false;     ///< Hash, verification and installation run in the worker thread
    };

    /// Outcome of the verification and the installation of the downloaded file
    struct VerificationOutcome
    {
        bool success = false;
        PDFOCRModelState failureState = PDFOCRModelState::Error;
        QString message;
    };

    struct InstalledFile
    {
        QString id;
        QString language;
        PDFOCRModelProfile profile = PDFOCRModelProfile::Fast;
        PDFOCRModelOrigin origin = PDFOCRModelOrigin::None;
        QString path;
        QString version;
        QString sha256;
        QString name;
        QString license;
        qint64 size = 0;
        bool verified = false;

        /// Version of the engine, for which the model was installed (LANG-12)
        QString engineVersion;
    };

    void scanBuiltIn(std::vector<InstalledFile>& files) const;
    void scanUser(std::vector<InstalledFile>& files) const;
    void loadState();
    void saveState() const;
    void startNextDownloads();
    void onDownloadReadyRead(Download* download);
    void onDownloadFinished(Download* download);
    void completeDownload(Download* download, const VerificationOutcome& outcome);
    void finishDownload(std::unique_ptr<Download> download, bool success, const QString& message, PDFOCRModelState failureState = PDFOCRModelState::Error);

    /// Verifies the hash and the loadability of the downloaded file and installs it
    /// (runs in the worker thread, R12). All inputs are copies, the manager is not touched.
    static VerificationOutcome verifyAndInstall(const QString& userDirectory,
                                                const ModelValidator& validator,
                                                const PDFOCRCatalogEntry& entry,
                                                const QString& temporaryPath,
                                                const QString& targetPath,
                                                const QString& engineVersion);

    static PDFOCRError validateModelFile(const QString& userDirectory, const ModelValidator& validator, const QString& engineId, const QString& filePath, const QString& language);
    static PDFOCRError installModelFile(const QString& userDirectory, const QString& temporaryPath, const QString& targetPath);
    QString getModelTargetPath(const PDFOCRCatalogEntry& entry) const;
    void updateModelState(const QString& id, PDFOCRModelState state, const QString& errorMessage, int progress);
    static QString sanitizeIdentifier(const QString& identifier);
    static QString computeSha256(const QString& filePath);
    static bool isValidLanguageCode(const QString& language);
    static QString getProfileDirectoryName(PDFOCRModelProfile profile);
    static PDFOCRError acquireLock(const QString& userDirectory, std::unique_ptr<QLockFile>& lock);
    PDFOCRError acquireLock(std::unique_ptr<QLockFile>& lock) const { return acquireLock(m_userDirectory, lock); }

    /// Returns the version of the engine registered in the registry (empty, if the engine is not registered)
    static QString getEngineVersion(const QString& engineId);

    /// Returns the major version of the version string ("5.3.1" -> 5), or -1
    static int getMajorVersion(const QString& version);

    /// Returns the path of the installation metadata of the downloaded model
    static QString getInstallMetadataPath(const QString& modelPath);

    /// Returns true, if the path lies inside of the directory
    static bool isInsideDirectory(const QString& path, const QString& directory);

    /// Repairs the leftovers of an interrupted installation and removes stale
    /// temporary files and runtime sets, which were not used for a long time
    /// (LANG-07, OPS-04). It is done once, when no other instance holds the lock.
    void performHousekeeping();
    QString getLanguageWithoutImport(const QString& language, QString* importId) const;

    mutable QMutex m_mutex;
    QString m_userDirectory;
    QString m_builtInDirectory;
    PDFOCRCatalog m_catalog;
    std::vector<PDFOCRModelInfo> m_models;
    QStringList m_hiddenModels;

    /// Failure states of the last download attempts (Error or Incompatible) with the message
    std::map<QString, std::pair<PDFOCRModelState, QString>> m_errorStates;
    QNetworkAccessManager* m_networkAccessManager = nullptr;
    bool m_ownsNetworkAccessManager = false;
    bool m_housekeepingDone = false;
    std::vector<QString> m_downloadQueue;
    std::vector<std::unique_ptr<Download>> m_activeDownloads;
    int m_maximumParallelDownloads = 2;
    bool m_allowInsecureLoopback = false;
    ModelValidator m_validator;

    /// Worker of the verification and installation of the downloads (R12)
    QThreadPool m_verificationPool;
};

}   // namespace pdf

#endif // PDFOCRMODELMANAGER_H
