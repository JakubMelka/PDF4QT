//    Copyright (C) 2019-2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFMULTIMEDIA_H
#define PDFMULTIMEDIA_H

#include "pdfobject.h"
#include "pdffile.h"

#include <QColor>

#include <map>
#include <optional>

namespace pdf
{
class PDFObjectStorage;

struct PDFMediaMultiLanguageTexts
{
    static PDFMediaMultiLanguageTexts parse(const PDFObjectStorage* storage, PDFObject object);

    std::map<QByteArray, QString> texts;
};

class PDFMediaOffset
{
public:

    enum class Type
    {
        Invalid,
        Time,
        Frame,
        Marker
    };

    struct TimeData
    {
        PDFInteger seconds = 0;
    };

    struct FrameData
    {
        PDFInteger frame = 0;
    };

    struct MarkerData
    {
        QString namedOffset;
    };

    explicit inline PDFMediaOffset() :
        m_type(Type::Invalid)
    {

    }

    template<typename Data>
    explicit inline PDFMediaOffset(Type type, Data&& data) :
        m_type(type),
        m_data(qMove(data))
    {

    }

    static PDFMediaOffset parse(const PDFObjectStorage* storage, PDFObject object);

    const TimeData* getTimeData() const { return std::holds_alternative<TimeData>(m_data) ? &std::get<TimeData>(m_data) : nullptr; }
    const FrameData* getFrameData() const { return std::holds_alternative<FrameData>(m_data) ? &std::get<FrameData>(m_data) : nullptr; }
    const MarkerData* getMarkerData() const { return std::holds_alternative<MarkerData>(m_data) ? &std::get<MarkerData>(m_data) : nullptr; }

private:
    Type m_type;
    std::variant<std::monostate, TimeData, FrameData, MarkerData> m_data;
};

class PDFMediaSoftwareIdentifier
{
public:
    explicit inline PDFMediaSoftwareIdentifier(QByteArray&& software, std::vector<PDFInteger>&& lowVersion, std::vector<PDFInteger>&& highVersion,
                                               bool lowVersionInclusive, bool highVersionInclusive, std::vector<QByteArray>&& languages) :
        m_software(qMove(software)),
        m_lowVersion(qMove(lowVersion)),
        m_highVersion(qMove(highVersion)),
        m_lowVersionInclusive(lowVersionInclusive),
        m_highVersionInclusive(highVersionInclusive),
        m_languages(qMove(languages))
    {

    }

    static PDFMediaSoftwareIdentifier parse(const PDFObjectStorage* storage, PDFObject object);

    const QByteArray& getSoftware() const { return m_software; }
    const std::vector<PDFInteger>& getLowVersion() const { return m_lowVersion; }
    const std::vector<PDFInteger>& getHighVersion() const { return m_highVersion; }
    bool isLowVersionInclusive() const { return m_lowVersionInclusive; }
    bool isHighVersionInclusive() const { return m_highVersionInclusive; }
    const std::vector<QByteArray>& getLanguages() const { return m_languages; }

private:
    QByteArray m_software;
    std::vector<PDFInteger> m_lowVersion;
    std::vector<PDFInteger> m_highVersion;
    bool m_lowVersionInclusive;
    bool m_highVersionInclusive;
    std::vector<QByteArray> m_languages;
};

class PDFMediaPlayer
{
public:
    explicit inline PDFMediaPlayer(PDFMediaSoftwareIdentifier&& softwareIdentifier) :
        m_softwareIdentifier(qMove(softwareIdentifier))
    {

    }

    static PDFMediaPlayer parse(const PDFObjectStorage* storage, PDFObject object);

    const PDFMediaSoftwareIdentifier* getSoftwareIdentifier() const { return &m_softwareIdentifier; }

private:
    PDFMediaSoftwareIdentifier m_softwareIdentifier;
};

class PDFMediaPlayers
{
public:
    explicit inline PDFMediaPlayers() = default;

    explicit inline PDFMediaPlayers(std::vector<PDFMediaPlayer>&& playersMustUsed,
                                    std::vector<PDFMediaPlayer>&& playersAlternate,
                                    std::vector<PDFMediaPlayer>&& playersNeverUsed) :
        m_playersMustUsed(qMove(playersMustUsed)),
        m_playersNeverUsed(qMove(playersNeverUsed)),
        m_playersAlternate(qMove(playersAlternate))
    {

    }

    static PDFMediaPlayers parse(const PDFObjectStorage* storage, PDFObject object);

    const std::vector<PDFMediaPlayer>& getPlayersMustUsed() const { return m_playersMustUsed; }
    const std::vector<PDFMediaPlayer>& getPlayersAlternate() const { return m_playersAlternate; }
    const std::vector<PDFMediaPlayer>& getPlayersNeverUsed() const { return m_playersNeverUsed; }

private:
    std::vector<PDFMediaPlayer> m_playersMustUsed;
    std::vector<PDFMediaPlayer> m_playersAlternate;
    std::vector<PDFMediaPlayer> m_playersNeverUsed;
};

class PDFMediaPermissions
{
public:

    /// Are we allowed to save temporary file to play rendition?
    enum class Permission
    {
        Never,
        Extract,
        Access,
        Always
    };

    explicit inline PDFMediaPermissions() :
        m_permission(Permission::Never)
    {

    }

    explicit inline PDFMediaPermissions(Permission permission) :
        m_permission(permission)
    {

    }

    static PDFMediaPermissions parse(const PDFObjectStorage* storage, PDFObject object);

    Permission getPermission() const { return m_permission; }

private:
    Permission m_permission;
};

class PDFMediaPlayParameters
{
public:
    explicit inline PDFMediaPlayParameters() = default;

    enum class FitMode
    {
        Meet,
        Slice,
        Fill,
        Scroll,
        Hidden,
        Default
    };

    enum class Duration
    {
        Intrinsic,
        Infinity,
        Seconds
    };

    struct PlayParameters
    {
        PDFInteger volume = 100;
        bool controllerUserInterface = false;
        FitMode fitMode = FitMode::Default;
        bool playAutomatically = true;
        PDFReal repeat = 1.0;
        Duration duration = Duration::Intrinsic;
        PDFReal durationSeconds = 0.0;
    };

    static PDFMediaPlayParameters parse(const PDFObjectStorage* storage, PDFObject object);

    const PDFMediaPlayers* getPlayers() const { return &m_players; }
    const PlayParameters* getPlayParametersMustHonored() const { return &m_mustHonored; }
    const PlayParameters* getPlayParametersBestEffort() const { return &m_bestEffort; }

private:
    PDFMediaPlayers m_players;
    PlayParameters m_mustHonored;
    PlayParameters m_bestEffort;
};

class PDFMediaScreenParameters
{
public:

    enum class WindowType
    {
        Floating,
        FullScreen,
        Hidden,
        ScreenAnnotation
    };

    enum class WindowRelativeTo
    {
        DocumentWindow,
        ApplicationWindow,
        VirtualDesktop,
        Monitor
    };

    enum class OffscreenMode
    {
        NoAction,
        MoveOnScreen,
        NonViable
    };

    enum class ResizeMode
    {
        Fixed,
        ResizableKeepAspectRatio,
        Resizeble
    };

    struct ScreenParameters
    {
        WindowType windowType = WindowType::ScreenAnnotation;
        QColor backgroundColor = QColor(Qt::white);
        PDFReal opacity = 1.0;
        PDFInteger monitorSpecification = 0;

        QSize floatingWindowSize;
        WindowRelativeTo floatingWindowReference = WindowRelativeTo::DocumentWindow;
        Qt::Alignment floatingWindowAlignment = Qt::AlignCenter;
        OffscreenMode floatingWindowOffscreenMode = OffscreenMode::MoveOnScreen;
        bool floatingWindowHasTitleBar = true;
        bool floatingWindowCloseable = true;
        ResizeMode floatingWindowResizeMode = ResizeMode::Fixed;
        PDFMediaMultiLanguageTexts floatingWindowTitle;
    };

    explicit inline PDFMediaScreenParameters() = default;
    explicit inline PDFMediaScreenParameters(ScreenParameters&& mustHonored, ScreenParameters&& bestEffort) :
        m_mustHonored(qMove(mustHonored)),
        m_bestEffort(qMove(bestEffort))
    {

    }

    static PDFMediaScreenParameters parse(const PDFObjectStorage* storage, PDFObject object);

    const ScreenParameters* getScreenParametersMustHonored() const { return &m_mustHonored; }
    const ScreenParameters* getScreenParametersBestEffort() const { return &m_bestEffort; }

private:
    ScreenParameters m_mustHonored;
    ScreenParameters m_bestEffort;
};

class PDFMediaClip
{
public:
    struct MediaClipData
    {
        QString name;
        PDFFileSpecification fileSpecification;
        PDFObject dataStream;
        QByteArray contentType;
        PDFMediaPermissions permissions;
        PDFMediaMultiLanguageTexts alternateTextDescriptions;
        PDFMediaPlayers players;
        QByteArray m_baseUrlMustHonored;
        QByteArray m_baseUrlBestEffort;
    };

    struct MediaSectionBeginEnd
    {
        PDFMediaOffset offsetBegin;
        PDFMediaOffset offsetEnd;
    };

    struct MediaSectionData
    {
        QString name;
        PDFMediaMultiLanguageTexts alternateTextDescriptions;
        MediaSectionBeginEnd m_mustHonored;
        MediaSectionBeginEnd m_bestEffort;
    };

    explicit inline PDFMediaClip() = default;

    explicit inline PDFMediaClip(MediaClipData&& mediaClipData, std::vector<MediaSectionData>&& sections) :
        m_mediaClipData(qMove(mediaClipData)),
        m_sections(qMove(sections))
    {

    }

    static PDFMediaClip parse(const PDFObjectStorage* storage, PDFObject object);

    const MediaClipData& getMediaClipData() const { return m_mediaClipData; }
    const std::vector<MediaSectionData>& getClipSections() const { return m_sections; }

private:
    MediaClipData m_mediaClipData;
    std::vector<MediaSectionData> m_sections;
};

class PDFMediaMinimumBitDepth
{
public:
    explicit inline PDFMediaMinimumBitDepth(PDFInteger screenMinimumBitDepth, PDFInteger monitorSpecifier) :
        m_screenMinimumBitDepth(screenMinimumBitDepth),
        m_monitorSpecifier(monitorSpecifier)
    {

    }

    static PDFMediaMinimumBitDepth parse(const PDFObjectStorage* storage, PDFObject object);

    PDFInteger getScreenMinimumBitDepth() const { return m_screenMinimumBitDepth; }
    PDFInteger getMonitorSpecifier() const { return m_monitorSpecifier; }

private:
    PDFInteger m_screenMinimumBitDepth;
    PDFInteger m_monitorSpecifier;
};

class PDFMediaMinimumScreenSize
{
public:
    explicit inline PDFMediaMinimumScreenSize(PDFInteger minimumWidth, PDFInteger minimumHeight, PDFInteger monitorSpecifier) :
        m_minimumWidth(minimumWidth),
        m_minimumHeight(minimumHeight),
        m_monitorSpecifier(monitorSpecifier)
    {

    }

    static PDFMediaMinimumScreenSize parse(const PDFObjectStorage* storage, PDFObject object);

private:
    PDFInteger m_minimumWidth;
    PDFInteger m_minimumHeight;
    PDFInteger m_monitorSpecifier;
};

/// Media critera object (see PDF 1.7 reference, chapter 9.1.2). Some values are optional,
/// so they are implemented using std::optional. Always call "has" functions before
/// accessing the getters.
class PDFMediaCriteria
{
public:
    explicit inline PDFMediaCriteria() = default;

    static PDFMediaCriteria parse(const PDFObjectStorage* storage, PDFObject object);

    bool hasAudioDescriptions() const { return m_audioDescriptions.has_value(); }
    bool hasTextCaptions() const { return m_textCaptions.has_value(); }
    bool hasAudioOverdubs() const { return m_audioOverdubs.has_value(); }
    bool hasSubtitles() const { return m_subtitles.has_value(); }
    bool hasBitrate() const { return m_bitrate.has_value(); }
    bool hasMinimumBitDepth() const { return m_minimumBitDepth.has_value(); }
    bool hasMinimumScreenSize() const { return m_minimumScreenSize.has_value(); }
    bool hasViewers() const { return m_viewers.has_value(); }
    bool hasMinimumPdfVersion() const { return m_minimumPdfVersion.has_value(); }
    bool hasMaximumPdfVersion() const { return m_maximumPdfVersion.has_value(); }
    bool hasLanguages() const { return m_languages.has_value(); }

    bool getAudioDescriptions() const { return m_audioDescriptions.value(); }
    bool getTextCaptions() const { return m_textCaptions.value(); }
    bool getAudioOverdubs() const { return m_audioOverdubs.value(); }
    bool getSubtitles() const { return m_subtitles.value(); }
    PDFInteger getBitrate() const { return m_bitrate.value(); }
    const PDFMediaMinimumBitDepth& getMinimumBitDepth() const { return m_minimumBitDepth.value(); }
    const PDFMediaMinimumScreenSize& getMinimumScreenSize() const { return m_minimumScreenSize.value(); }
    const std::vector<PDFMediaSoftwareIdentifier>& getViewers() const { return m_viewers.value(); }
    const QByteArray& getMinimumPdfVersion() const { return m_minimumPdfVersion.value(); }
    const QByteArray& getMaximumPdfVersion() const { return m_maximumPdfVersion.value(); }
    const std::vector<QByteArray>& getLanguages() const { return m_languages.value(); }

private:
    std::optional<bool> m_audioDescriptions;
    std::optional<bool> m_textCaptions;
    std::optional<bool> m_audioOverdubs;
    std::optional<bool> m_subtitles;
    std::optional<PDFInteger> m_bitrate;
    std::optional<PDFMediaMinimumBitDepth> m_minimumBitDepth;
    std::optional<PDFMediaMinimumScreenSize> m_minimumScreenSize;
    std::optional<std::vector<PDFMediaSoftwareIdentifier>> m_viewers;
    std::optional<QByteArray> m_minimumPdfVersion;
    std::optional<QByteArray> m_maximumPdfVersion;
    std::optional<std::vector<QByteArray>> m_languages;
};

/// Rendition object
class PDFRendition
{
public:

    enum class Type
    {
        Invalid,
        Media,
        Selector
    };

    struct MediaRenditionData
    {
        PDFMediaClip clip;
        PDFMediaPlayParameters playParameters;
        PDFMediaScreenParameters screenParameters;
    };

    struct SelectorRenditionData
    {
        PDFObject renditions;
    };

    static PDFRendition parse(const PDFObjectStorage* storage, PDFObject object);

    Type getType() const { return m_type; }
    const QString& getName() const { return m_name; }
    const PDFMediaCriteria* getMediaCriteriaMustHonored() const { return &m_mustHonored; }
    const PDFMediaCriteria* getMediaCriteriaBestEffort() const { return &m_bestEffort; }
    const MediaRenditionData* getMediaRenditionData() const { return std::holds_alternative<MediaRenditionData>(m_data) ? &std::get<MediaRenditionData>(m_data) : nullptr; }
    const SelectorRenditionData* getSelectorRenditionData() const { return std::holds_alternative<SelectorRenditionData>(m_data) ? &std::get<SelectorRenditionData>(m_data) : nullptr; }

private:
    Type m_type = Type::Invalid;
    QString m_name;

    PDFMediaCriteria m_mustHonored;
    PDFMediaCriteria m_bestEffort;
    std::variant<std::monostate, MediaRenditionData, SelectorRenditionData> m_data;
};

/// Sound object, see chapter 9.2 in PDF 1.7 reference
class PDFSound
{
public:
    explicit inline PDFSound() = default;

    enum class Format
    {
        Raw,
        Signed,
        muLaw,
        ALaw
    };

    const PDFFileSpecification* getFileSpecification() const { return &m_fileSpecification; }
    PDFReal getSamplingRate() const { return m_samplingRate; }
    PDFInteger getChannels() const { return m_channels; }
    PDFInteger getBitsPerSample() const { return m_bitsPerSample; }
    Format getFormat() const { return m_format; }
    const QByteArray& getSoundCompression() { return m_soundCompression; }
    const PDFObject& getSoundCompressionParameters() const { return m_soundCompressionParameters; }
    const PDFStream* getStream() const { return m_streamObject.isStream() ? m_streamObject.getStream() : nullptr; }

    /// If this function returns true, sound is valid
    bool isValid() const { return getStream(); }

    /// Creates a new sound from the object. If data are invalid, then invalid object
    /// is returned, no exception is thrown.
    static PDFSound parse(const PDFObjectStorage* storage, PDFObject object);

private:
    PDFFileSpecification m_fileSpecification;
    PDFReal m_samplingRate = 0.0;
    PDFInteger m_channels = 0;
    PDFInteger m_bitsPerSample = 0;
    Format m_format = Format::Raw;
    QByteArray m_soundCompression;
    PDFObject m_soundCompressionParameters;
    PDFObject m_streamObject;
};

/// Movie object, see chapter 9.3 in PDF 1.7 reference
class PDFMovie
{
public:
    explicit inline PDFMovie() = default;

    const PDFFileSpecification* getMovieFileSpecification() const { return &m_movieFile; }
    QSize getWindowSize() const { return m_windowSize; }
    PDFInteger getRotationAngle() const { return m_rotationAngle; }
    bool isPosterVisible() const { return m_showPoster; }
    const PDFObject& getPosterObject() const { return m_poster; }

    /// Creates a new movie from the object. If data are invalid, then invalid object
    /// is returned, no exception is thrown.
    static PDFMovie parse(const PDFObjectStorage* storage, PDFObject object);

private:
    PDFFileSpecification m_movieFile;
    QSize m_windowSize;
    PDFInteger m_rotationAngle = 0;
    bool m_showPoster = false;
    PDFObject m_poster;
};

/// Movie activation object, see table 9.31 in PDF 1.7 reference
class PDFMovieActivation
{
public:
    explicit inline PDFMovieActivation() = default;

    struct MovieTime
    {
        PDFInteger value = 0;
        std::optional<PDFInteger> unitsPerSecond;
    };

    enum class Mode
    {
        Once,
        Open,
        Repeat,
        Palindrome
    };

    MovieTime getStartTime() const { return m_start; }
    MovieTime getDuration() const { return m_duration; }
    PDFReal getRate() const { return m_rate; }
    PDFReal getVolume() const { return m_volume; }
    bool isShowControls() const { return m_showControls; }
    bool isSynchronous() const { return m_synchronous; }
    Mode getMode() const { return m_mode; }
    bool hasScale() const { return m_scaleDenominator != 0; }
    PDFInteger getScaleNumerator() const { return m_scaleNumerator; }
    PDFInteger getScaleDenominator() const { return m_scaleDenominator; }
    QPointF getRelativeWindowPosition() const { return m_relativeWindowPosition; }

    /// Creates a new moview from the object. If data are invalid, then invalid object
    /// is returned, no exception is thrown.
    static PDFMovieActivation parse(const PDFObjectStorage* storage, PDFObject object);

private:
    static MovieTime parseMovieTime(const PDFObjectStorage* storage, PDFObject object);
    static PDFInteger parseMovieTimeFromString(const QByteArray& string);

    MovieTime m_start;
    MovieTime m_duration;
    PDFReal m_rate = 1.0;
    PDFReal m_volume = 1.0;
    bool m_showControls = false;
    bool m_synchronous = false;
    Mode m_mode = Mode::Once;
    PDFInteger m_scaleNumerator = 0;
    PDFInteger m_scaleDenominator = 0;
    QPointF m_relativeWindowPosition;
};

}   // namespace pdf

#endif // PDFMULTIMEDIA_H
