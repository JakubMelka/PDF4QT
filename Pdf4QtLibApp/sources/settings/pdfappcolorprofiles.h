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

#ifndef PDFAPPCOLORPROFILES_H
#define PDFAPPCOLORPROFILES_H

#include "pdfappglobal.h"
#include "pdfsettingdefinition.h"

#include <QString>

#include <map>
#include <memory>
#include <vector>

namespace pdf
{

/// State of the additional folder of ICC profiles (cms.profileDirectory)
enum class PDFAppProfileDirectoryState
{
    None,           ///< No folder
    Usable,         ///< A local folder within the limits
    Missing,        ///< The folder does not exist
    NotFolder,      ///< The path is not a folder
    Network,        ///< A folder on a network (never read by the application by itself)
    TooManyFiles,   ///< More entries or profiles than the limits
    TooLarge        ///< A profile or all the profiles together are larger than the limits
};

/// Profiles of the color management offered by the Settings page (lists of
/// PDFCMSManager: output, device gray, device RGB, device CMYK). The first
/// profile of a list is the built-in default, which the setting stores as an
/// empty text.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColorProfileList
{
public:
    /// A profile: its id (the stored value) and its name
    class Profile
    {
    public:
        QString id;
        QString name;
    };

    QString directory;
    PDFAppProfileDirectoryState directoryState = PDFAppProfileDirectoryState::None;
    std::vector<Profile> outputProfiles;
    std::vector<Profile> grayProfiles;
    std::vector<Profile> rgbProfiles;
    std::vector<Profile> cmykProfiles;

    /// Profiles of the setting (cms.outputProfile, cms.deviceGrayProfile, …),
    /// nullptr for another key
    const std::vector<Profile>* getProfiles(const QString& key) const;
};

/// The folder of the profiles and the lists of the profiles (Settings › Color
/// management, package B of step 5).
///
/// PDFCMSManager reads every *.icc file of the folder completely, without a limit
/// of the count or of the size, in the thread, which asks for a list (the thread of
/// the user interface of a document view; SECURITY_TODO.md). The application
/// gives it only a folder, which passed checkDirectory: a local folder of at most
/// MAXIMUM_ENTRIES entries and MAXIMUM_PROFILES profiles of at most
/// MAXIMUM_PROFILE_SIZE bytes each and MAXIMUM_TOTAL_SIZE together. A network
/// folder is never read by the application by itself (a server, which does not
/// answer, would block the thread). The lists of the page are read by scan in a
/// worker thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColorProfiles
{
public:
    PDFAppColorProfiles() = delete;

    static constexpr int MAXIMUM_ENTRIES = 1000;
    static constexpr int MAXIMUM_PROFILES = 64;
    static constexpr qint64 MAXIMUM_PROFILE_SIZE = 8 * 1024 * 1024;
    static constexpr qint64 MAXIMUM_TOTAL_SIZE = 64 * 1024 * 1024;

    /// Longest name of a profile shown (names are data of the profile files)
    static constexpr int MAXIMUM_NAME_LENGTH = 128;

    /// Longest id of a profile, which can be stored (the length of the setting)
    static constexpr int MAXIMUM_ID_LENGTH = 1024;

    /// Time, for which the result of isDirectoryUsable is reused (ms)
    static constexpr qint64 CHECK_CACHE_DURATION = 10000;

    /// Checks the folder without reading the files (a network path is not touched,
    /// at most MAXIMUM_ENTRIES + 1 entries are listed). Any thread.
    static PDFAppProfileDirectoryState checkDirectory(const QString& path);

    /// The folder may be given to PDFCMSManager (an empty path is usable - no
    /// folder). The result of a path is reused for CHECK_CACHE_DURATION ms, so a
    /// document view asks at each application of the settings. Any thread.
    static bool isDirectoryUsable(const QString& path);

    /// Forgets the cached result of isDirectoryUsable (tests, Browse…)
    static void clearCache();

    /// Reads the lists of the profiles with the folder (if it is usable). Call it
    /// in a worker thread: it asks the system for its installed profiles and reads
    /// the profiles of the folder. It uses nothing of the caller, so a section
    /// deleted meanwhile drops only its result. At the exit ~QCoreApplication waits
    /// for the tasks of the global thread pool; PDFCMSManager::finalize (it
    /// unregisters the plugins of Little CMS) is called by no program of PDF4QT -
    /// a caller would have to call it after QCoreApplication is gone, never as a
    /// post routine (those run before the wait for the pool).
    static PDFAppColorProfileList scan(const QString& directory);

    /// Text of a state of the folder, which is not read ("The folder is on a
    /// network and is not read"), empty for None and Usable
    static QString getStateText(PDFAppProfileDirectoryState state);

    /// Name of a profile safe to show (control and format characters removed,
    /// at most MAXIMUM_NAME_LENGTH characters)
    static QString sanitizeName(const QString& name);
};

/// Options of the combo boxes of the profiles (the list of the last scan). The
/// first profile of a list is the option "" (the built-in default); while the
/// lists are read, a stored profile is offered as "Loading profiles…", so the
/// combo box does not show it as not available.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColorProfileOptionsProvider : public PDFSettingOptionsProvider
{
public:
    PDFAppColorProfileOptionsProvider() = default;

    virtual std::vector<PDFSettingOption> getOptions(const PDFSettingDefinition& definition) const override;

    /// Lists of a finished scan (the loading state ends)
    void setList(std::shared_ptr<const PDFAppColorProfileList> list);
    const std::shared_ptr<const PDFAppColorProfileList>& getList() const { return m_list; }

    /// The lists are being read: the stored values (key -> value) are offered
    void setLoading(std::map<QString, QString> storedValues);
    bool isLoading() const { return m_isLoading; }

private:
    std::shared_ptr<const PDFAppColorProfileList> m_list;
    std::map<QString, QString> m_loadingValues;
    bool m_isLoading = true;
};

/// Options of the color management system: Generic and Little CMS with the
/// version of the library ("Little CMS 2.16" - 944:9100)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColorSystemOptionsProvider : public PDFSettingOptionsProvider
{
public:
    virtual std::vector<PDFSettingOption> getOptions(const PDFSettingDefinition& definition) const override;
};

}   // namespace pdf

#endif // PDFAPPCOLORPROFILES_H
