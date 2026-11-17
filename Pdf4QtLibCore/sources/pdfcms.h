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

#ifndef PDFCMS_H
#define PDFCMS_H

#include "pdfglobal.h"
#include "pdfcolorspaces.h"
#include "pdfexception.h"
#include "pdfutils.h"
#include "pdfcolorconvertor.h"

#include <QRecursiveMutex>
#include <QSharedPointer>

#include <compare>

namespace pdf
{

/// This simple structure stores settings for color management system, and what
/// color management system should be used. At default, two color management
/// system are available - generic (which uses default imprecise color management),
/// and CMS using engine LittleCMS2, which was written by Marti Maria, and is
/// linked as separate library.
struct PDFCMSSettings
{
    /// Type of color management system
    enum class System
    {
        Generic,
        LittleCMS2
    };

    /// Controls accuracy of the color transformations. High accuracy
    /// could mean high memory consumption, but better color accuracy,
    /// low accuracy means low memory consumption and low color accuracy.
    enum class Accuracy
    {
        Low,
        Medium,
        High
    };

    enum class ColorAdaptationXYZ
    {
        None,
        XYZScaling,
        CAT97,
        CAT02,
        Bradford
    };

    bool operator==(const PDFCMSSettings&) const = default;

    /// The settings without the soft proof and the gamut check (plan of step 7 of phase 2, 1 point 6, 4.7,
    /// Q7.10): the proofing is a state of the view of one tab (PDFAppDocumentSoftProof). Every copy of the
    /// settings of a tab for something else - the thumbnails, the printing, the tools, the stages and the
    /// overlays of the tools, the comparison - takes this one, so a proof never leaks into a print, an export
    /// or a preview. The values, which only a proof uses (the profile, the intent and the alarm color of the
    /// proof), get their defaults too (package P3b): the copies of two states of the proof are equal, so a
    /// change of the proof of the tab never makes a new color management of the copies (PDFCMS::isCompatible,
    /// PDFCMSManager::setSettings compare the whole settings) - the thumbnails are not rendered again.
    PDFCMSSettings withoutProofing() const
    {
        const PDFCMSSettings defaults;
        PDFCMSSettings settings = *this;
        settings.isSoftProofing = false;
        settings.isGamutChecking = false;
        settings.softProofingProfile = defaults.softProofingProfile;
        settings.proofingIntent = defaults.proofingIntent;
        settings.outOfGamutColor = defaults.outOfGamutColor;
        return settings;
    }

    System system = System::Generic;
    Accuracy accuracy = Accuracy::Medium;
    RenderingIntent intent = RenderingIntent::Auto;
    RenderingIntent proofingIntent = RenderingIntent::RelativeColorimetric;
    ColorAdaptationXYZ colorAdaptationXYZ = ColorAdaptationXYZ::Bradford;
    bool isBlackPointCompensationActive = true;
    bool isWhitePaperColorTransformed = false;
    bool isGamutChecking = false;
    bool isSoftProofing = false;
    bool isConsiderOutputIntent = true;
    QColor outOfGamutColor = Qt::red; ///< Color, which marks out-of-gamut when soft-proofing is proceeded (the alarm codes of the own context of the color management, B200)
    QString outputCS;               ///< Output (rendering) color space
    QString deviceGray;             ///< Identifiers for color space (device gray)
    QString deviceRGB;              ///< Identifiers for color space (device RGB)
    QString deviceCMYK;             ///< Identifiers for color space (device CMYK)
    QString softProofingProfile;    ///< Identifier of the CMYK profile of the soft proof (PDFSoftProofingProfile::id); empty = the first CMYK output intent of the document
    QString profileDirectory;       ///< Directory containing color profiles

    // Postprocessing
    QColor foregroundColor = Qt::green;
    QColor backgroundColor = Qt::black;
    int bitonalThreshold = 128;
    double sigmoidSlopeFactor = 10.0;
};

/// Color management system base class. It contains functions to transform
/// colors from various color system to device color system. If color management
/// system can't handle color transform, it should return invalid color.
class PDFCMS
{
public:
    explicit inline PDFCMS() = default;
    virtual ~PDFCMS() = default;

    /// This function should decide, if color management system is compatible with these
    /// settings (so, it transforms colors according to this setting). If this
    /// function returns false, then this color management system should be replaced
    /// by newly created one, according these settings.
    virtual bool isCompatible(const PDFCMSSettings& settings) const = 0;

    /// Returns color of the white paper
    virtual QColor getPaperColor() const = 0;

    /// Converts color in Device Gray color space to the target device
    /// color space. If error occurs, then invalid color is returned.
    /// Caller then should handle this - try to convert color as accurate
    /// as possible.
    /// \param color Single color channel value
    /// \param intent Rendering intent
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual QColor getColorFromDeviceGray(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const = 0;

    /// Converts color in Device RGB color space to the target device
    /// color space. If error occurs, then invalid color is returned.
    /// Caller then should handle this - try to convert color as accurate
    /// as possible.
    /// \param color Three color channel value (R,G,B channel)
    /// \param intent Rendering intent
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual QColor getColorFromDeviceRGB(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const = 0;

    /// Converts color in Device CMYK color space to the target device
    /// color space. If error occurs, then invalid color is returned.
    /// Caller then should handle this - try to convert color as accurate
    /// as possible.
    /// \param color Four color channel value (C,M,Y,K channel)
    /// \param intent Rendering intent
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual QColor getColorFromDeviceCMYK(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const = 0;

    /// Converts color in XYZ color space to the target device
    /// color space. If error occurs, then invalid color is returned.
    /// Caller then should handle this - try to convert color as accurate
    /// as possible.
    /// \param whitePoint White point of source XYZ color space
    /// \param Three color channel value (X,Y,Z channel)
    /// \param intent Rendering intent
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual QColor getColorFromXYZ(const PDFColor3& whitePoint,
                                   const PDFColor3& color,
                                   RenderingIntent intent,
                                   PDFRenderErrorReporter* reporter) const = 0;

    /// Computes color from ICC color profile
    /// \param color Input color
    /// \param iccID Unique ICC profile identifier
    /// \param iccData Color profile data
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual QColor getColorFromICC(const PDFColor& color,
                                   RenderingIntent renderingIntent,
                                   const QByteArray& iccID,
                                   const QByteArray& iccData,
                                   PDFRenderErrorReporter* reporter) const = 0;

    /// Fills colors in Device Gray color space to the RGB buffer. If error occurs, then false is returned.
    /// Caller then should handle this - try to convert color as accurate as possible.
    /// \param color Gray values
    /// \param intent Rendering intent
    /// \param outputBuffer Output buffer in format RGB_888 (8-bit RGB values)
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual bool fillRGBBufferFromDeviceGray(const std::vector<float>& colors,
                                             RenderingIntent intent,
                                             unsigned char* outputBuffer,
                                             PDFRenderErrorReporter* reporter) const = 0;

    /// Fills colors in Device RGB color space to RGB buffer. If error occurs, then false is returned.
    /// Caller then should handle this - try to convert color as accurate as possible.
    /// \param colors Buffer with three color channels, so it has pixels * tuple(R, G, B) size
    /// \param intent Rendering intent
    /// \param outputBuffer Output buffer in format RGB_888 (8-bit RGB values)
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual bool fillRGBBufferFromDeviceRGB(const std::vector<float>& colors,
                                            RenderingIntent intent,
                                            unsigned char* outputBuffer,
                                            PDFRenderErrorReporter* reporter) const = 0;

    /// Fills colors in Device CMYK color space to the RGB buffer. If error occurs, then false is returned.
    /// Caller then should handle this - try to convert color as accurate as possible.
    /// \param colors FBuffer with four color channels (C,M,Y,K channel), so it has pixels * tuple(C, M, Y, K) size
    /// \param intent Rendering intent
    /// \param outputBuffer Output buffer in format RGB_888 (8-bit RGB values)
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual bool fillRGBBufferFromDeviceCMYK(const std::vector<float>& colors,
                                             RenderingIntent intent,
                                             unsigned char* outputBuffer,
                                             PDFRenderErrorReporter* reporter) const = 0;

    /// Fills colors in XYZ color space to the RGB buffer. If error occurs, then false is returned.
    /// Caller then should handle this - try to convert color as accurate as possible.
    /// \param whitePoint White point of source XYZ color space
    /// \param Three color channel value (X,Y,Z channel)
    /// \param intent Rendering intent
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual bool fillRGBBufferFromXYZ(const PDFColor3& whitePoint,
                                      const std::vector<float>& colors,
                                      RenderingIntent intent,
                                      unsigned char* outputBuffer,
                                      PDFRenderErrorReporter* reporter) const = 0;

    /// Fills RGB buffer from ICC color profile colors
    /// \param colors Input colors
    /// \param iccID Unique ICC profile identifier
    /// \param iccData Color profile data
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual bool fillRGBBufferFromICC(const std::vector<float>& colors,
                                      RenderingIntent renderingIntent,
                                      unsigned char* outputBuffer,
                                      const QByteArray& iccID,
                                      const QByteArray& iccData,
                                      PDFRenderErrorReporter* reporter) const = 0;

    /// Returns color convertor of post-processing colors
    /// produced by color management system. Color convertor
    /// does not have set color conversion mode, it must be
    /// set manually.
    /// \return Color convertor according to current settings.
    virtual PDFColorConvertor getColorConvertor() const = 0;

    enum ColorSpaceType
    {
        Invalid,
        DeviceGray,
        DeviceRGB,
        DeviceCMYK,
        XYZ,
        ICC
    };

    struct ColorSpaceTransformParams
    {
        ColorSpaceType sourceType = ColorSpaceType::Invalid;
        ColorSpaceType targetType = ColorSpaceType::Invalid;

        QByteArray sourceIccId;
        QByteArray targetIccId;

        QByteArray sourceIccData;
        QByteArray targetIccData;

        PDFColorBuffer input;
        PDFColorBuffer output;

        RenderingIntent intent = RenderingIntent::Unknown;

        PDFInteger multithreadingThreshold = 4096;
    };

    /// Transforms color between two color spaces. Doesn't do soft-proofing,
    /// it just transforms two float buffers from input color space to output color space.
    virtual bool transformColorSpace(const ColorSpaceTransformParams& params) const = 0;

    /// Get D50 white point for XYZ color space
    static PDFColor3 getDefaultXYZWhitepoint();
};

using PDFCMSPointer = QSharedPointer<PDFCMS>;

class PDF4QTLIBCORESHARED_EXPORT PDFCMSGeneric : public PDFCMS
{
public:
    explicit inline PDFCMSGeneric() = default;
    explicit inline PDFCMSGeneric(const PDFColorConvertor& colorConvertor);

    virtual bool isCompatible(const PDFCMSSettings& settings) const override;
    virtual QColor getPaperColor() const override;
    virtual QColor getColorFromDeviceGray(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const override;
    virtual QColor getColorFromDeviceRGB(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const override;
    virtual QColor getColorFromDeviceCMYK(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const override;
    virtual QColor getColorFromXYZ(const PDFColor3& whitePoint, const PDFColor3& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const override;
    virtual QColor getColorFromICC(const PDFColor& color, RenderingIntent renderingIntent,  const QByteArray& iccID, const QByteArray& iccData, PDFRenderErrorReporter* reporter) const override;
    virtual bool fillRGBBufferFromDeviceGray(const std::vector<float>& colors, RenderingIntent intent, unsigned char* outputBuffer, PDFRenderErrorReporter* reporter) const override;
    virtual bool fillRGBBufferFromDeviceRGB(const std::vector<float>& colors, RenderingIntent intent, unsigned char* outputBuffer, PDFRenderErrorReporter* reporter) const override;
    virtual bool fillRGBBufferFromDeviceCMYK(const std::vector<float>& colors, RenderingIntent intent, unsigned char* outputBuffer, PDFRenderErrorReporter* reporter) const override;
    virtual bool fillRGBBufferFromXYZ(const PDFColor3& whitePoint, const std::vector<float>& colors, RenderingIntent intent, unsigned char* outputBuffer, PDFRenderErrorReporter* reporter) const override;
    virtual bool fillRGBBufferFromICC(const std::vector<float>& colors, RenderingIntent renderingIntent, unsigned char* outputBuffer, const QByteArray& iccID, const QByteArray& iccData, PDFRenderErrorReporter* reporter) const override;
    virtual bool transformColorSpace(const ColorSpaceTransformParams& params) const override;
    virtual PDFColorConvertor getColorConvertor() const override;

private:
    PDFColorConvertor m_colorConvertor;
};

struct PDFColorProfileIdentifier
{
    enum class Type
    {
        Gray,
        sRGB,
        RGB,
        FileGray,
        FileRGB,
        FileCMYK,
        WindowsSystemGray,
        WindowsSystemRGB,
        WindowsSystemCMYK,
        MemoryGray,
        MemoryRGB,
        MemoryCMYK,
        Invalid
    };

    bool operator==(const PDFColorProfileIdentifier&) const = default;
    bool operator!=(const PDFColorProfileIdentifier&) const = default;

    Type type = Type::sRGB;
    QString name;
    QString id;
    PDFReal temperature = 6500.0;
    QPointF primaryR;
    QPointF primaryG;
    QPointF primaryB;
    PDFReal gamma = 1.0;
    bool isOutputIntentProfile = false;

    /// Data for MemoryGray, MemoryRGB and MemoryCMYK
    QByteArray profileMemoryData;

    /// Creates gray color profile identifier
    /// \param name Name of color profile
    /// \param id Identifier of color profile
    /// \param temperature White point temperature
    /// \param gamma Gamma correction
    static PDFColorProfileIdentifier createGray(QString name, QString id, PDFReal temperature, PDFReal gamma);

    /// Creates sRGB color profile identifier
    static PDFColorProfileIdentifier createSRGB();

    /// Creates RGB color space identifier
    /// \param name Name of color profile
    /// \param id Identifier of color profile
    /// \param temperature White point temperature
    /// \param primaryR Primary red
    /// \param primaryG Primary green
    /// \param primaryB Primary blue
    /// \param gamma Gamma correction
    static PDFColorProfileIdentifier createRGB(QString name, QString id, PDFReal temperature, QPointF primaryR, QPointF primaryG, QPointF primaryB, PDFReal gamma);

    /// Create file color profile identifier
    static PDFColorProfileIdentifier createFile(Type type, QString name, QString id);

    /// Create Windows system color profile identifier
    static PDFColorProfileIdentifier createWindowsSystem(Type type, QString name, QString id);

    /// Create memory output intent color profile identifier
    static PDFColorProfileIdentifier createOutputIntent(Type type, QString name, QString id, QByteArray profileData);
};

using PDFColorProfileIdentifiers = std::vector<PDFColorProfileIdentifier>;

/// A profile of the menu of the soft proof (plan of step 7 of phase 2, 4.7, FP-6, 479:12333; owner answer
/// Q7.11: PDF4QT has no built-in CMYK profiles - the sources are the output intents of the document, the
/// installed profiles of the system and the profile folder of Settings › Color management). An output
/// intent, which cannot be the profile of a proof (an RGB or a gray profile, no embedded profile, a damaged
/// profile), is in the list too, not available, with its reason (the menu shows it Disabled). A value.
class PDF4QTLIBCORESHARED_EXPORT PDFSoftProofingProfile
{
public:
    /// Where the profile comes from
    enum class Source
    {
        OutputIntent,   ///< An output intent of the document (ISO 32000-2, 14.11.5: /DestOutputProfile)
        System,         ///< An installed profile of the system
        ProfileFolder   ///< A profile of the folder of Settings › Color management (PDFCMSSettings::profileDirectory)
    };

    bool operator==(const PDFSoftProofingProfile&) const = default;

    Source source = Source::System;
    QString id;                 ///< The value of PDFCMSSettings::softProofingProfile; empty for a profile, which is not available
    QString name;               ///< The description of the profile (ICC tag desc), else the name of its file, else the printing condition of the output intent
    QString fileName;           ///< The name of the file of the profile (without a folder); empty for an output intent
    QString outputCondition;    ///< The printing condition of an output intent (/OutputConditionIdentifier, else /OutputCondition); empty otherwise
    bool isAvailable = true;    ///< The profile can be the profile of a proof (a CMYK profile, which can be read)
    QString reason;             ///< Translated reason, why the profile is not available (empty when it is)
};

using PDFSoftProofingProfiles = std::vector<PDFSoftProofingProfile>;

/// Manager, that manages current color management system and also list
/// of usable input and output color profiles. It has color profiles
/// for outout device, and color profiles for input (gray/RGB/CMYK).
/// It also handles settings, and it's changes. Constant functions
/// is save to call from multiple threads, this also holds for some
/// non-constant functions - manager is protected by mutexes.
class PDF4QTLIBCORESHARED_EXPORT PDFCMSManager : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFCMSManager(QObject* parent);

    /// Finalizes cms manager. Call this function
    /// only at program exit. Frees all allocated
    /// resources. Function is not thread-safe.
    static void finalize();

    /// Returns current CMS. This function possibly creates CMS,
    /// of no CMS is found.
    PDFCMSPointer getCurrentCMS() const;

    const PDFCMSSettings& getSettings() const { return m_settings; }
    void setSettings(const PDFCMSSettings& settings);

    /// Returns color convertor of post-processing colors
    /// produced by color management system. Color convertor
    /// does not have set color conversion mode, it must be
    /// set manually.
    /// \return Color convertor according to current settings.
    PDFColorConvertor getColorConvertor() const;

    const PDFColorProfileIdentifiers& getOutputProfiles() const;
    const PDFColorProfileIdentifiers& getGrayProfiles() const;
    const PDFColorProfileIdentifiers& getRGBProfiles() const;
    const PDFColorProfileIdentifiers& getCMYKProfiles() const;

    /// Returns default color management settings
    PDFCMSSettings getDefaultSettings() const;

    /// Set current document. Document is examined for output
    /// rendering intents and they can be used when rendering.
    /// \param document Document
    void setDocument(const PDFDocument* document);

    /// Get translated name for color management system
    /// \param system System
    static QString getSystemName(PDFCMSSettings::System system);

    /// Most output intents of a document, which are read (ISO 32000-2, 14.11.5; a document has one per
    /// standard it conforms to - a hostile array of thousands of intents is not decoded beyond this count)
    static constexpr size_t MAXIMUM_OUTPUT_INTENTS = 32;

    /// The profiles of the menu of the soft proof (FP-6, 479:12333): first the output intents of the document
    /// (setDocument) in their order - also those, which cannot be used, with their reasons -, then the CMYK
    /// profiles of the system and of the profile folder of the settings. The descriptions of the installed
    /// profiles of the system are read once per list (the list is kept until the settings or the document
    /// change). Thread-safe, as the other lists.
    PDFSoftProofingProfiles getSoftProofingProfiles() const;

    /// The identifier of the profile of a proof with \p settings: \p settings.softProofingProfile, or for an
    /// empty value the first available output intent of the document (PDFCMSSettings::softProofingProfile).
    /// Empty, when there is none. Thread-safe.
    QString getEffectiveSoftProofingProfile(const PDFCMSSettings& settings) const;

    /// The absolute path of the file of a profile of a folder (PDFColorProfileIdentifier::Type::FileGray,
    /// FileRGB, FileCMYK): their identifiers are relative to the folder of the application
    static QString getProfileFilePath(const QString& id);

    /// Enables the cache of the external profiles of the process (opt-in, off by
    /// default). Every change of the settings clears the lists of a manager and the
    /// next list asks the system for its installed profiles and reads the profile
    /// directory again (more than 100 ms on Windows). With the cache, the external
    /// profiles of a profile directory are listed once per process and shared by
    /// every manager, until clearExternalProfilesCache is called. Thread-safe.
    static void setExternalProfilesCacheEnabled(bool enabled);
    static bool isExternalProfilesCacheEnabled();

    /// Forgets the cached external profiles (the next list asks the system again).
    /// Thread-safe.
    static void clearExternalProfilesCache();

signals:
    void colorManagementSystemChanged();

private:
    /// Creates new CMS based on current settings
    PDFCMSPointer getCurrentCMSImpl() const;

    /// Clear cached items
    void clearCache();

    /// This function returns external profiles. It is not protected by mutex,
    /// so it is not thread-safe. For this reason, it is not in public
    /// interface.
    const PDFColorProfileIdentifiers& getExternalProfiles() const;

    PDFColorProfileIdentifiers getOutputProfilesImpl() const;
    PDFColorProfileIdentifiers getGrayProfilesImpl() const;
    PDFColorProfileIdentifiers getRGBProfilesImpl() const;
    PDFColorProfileIdentifiers getCMYKProfilesImpl() const;
    PDFColorProfileIdentifiers getExternalProfilesImpl() const;
    PDFSoftProofingProfiles getSoftProofingProfilesImpl() const;

    /// Lists the installed profiles of the system and the profiles of the profile
    /// directory (without the cache of the process)
    PDFColorProfileIdentifiers listExternalProfiles() const;

    /// Returns filtered list of external profiles (list are filtered by type,
    /// so, for example, only CMYK profiles are returned)
    /// \param type Type of profile
    PDFColorProfileIdentifiers getFilteredExternalProfiles(PDFColorProfileIdentifier::Type type) const;

    /// Returns filtered list of output intent profiles (list are filtered by type,
    /// so, for example, only CMYK profiles are returned)
    /// \param type Type of profile
    PDFColorProfileIdentifiers getFilteredOutputIntentProfiles(PDFColorProfileIdentifier::Type type) const;

    /// Gets list of color profiles from external directory
    /// \param profileDirectory Directory with profiles
    PDFColorProfileIdentifiers getExternalColorProfiles(QString profileDirectory) const;

    PDFCMSSettings m_settings;
    const PDFDocument* m_document;
    PDFColorProfileIdentifiers m_outputIntentProfiles;
    PDFSoftProofingProfiles m_outputIntentProofingProfiles;    ///< Every output intent of the document as a profile of the soft proof (also the unavailable ones)

    mutable QRecursiveMutex m_mutex;
    mutable PDFCachedItem<PDFCMSPointer> m_CMS;
    mutable PDFCachedItem<PDFColorProfileIdentifiers> m_outputProfiles;
    mutable PDFCachedItem<PDFColorProfileIdentifiers> m_grayProfiles;
    mutable PDFCachedItem<PDFColorProfileIdentifiers> m_RGBProfiles;
    mutable PDFCachedItem<PDFColorProfileIdentifiers> m_CMYKProfiles;
    mutable PDFCachedItem<PDFColorProfileIdentifiers> m_externalProfiles;
    mutable PDFCachedItem<PDFSoftProofingProfiles> m_softProofingProfiles;
};

/// Class providing chromatic adaptation of whitepoints
/// using various method.
class PDFChromaticAdaptationXYZ
{
public:
    PDFChromaticAdaptationXYZ() = delete;

    static PDFColorComponentMatrix_3x3 createWhitepointChromaticAdaptation(const PDFColor3& targetWhitePoint,
                                                                           const PDFColor3& sourceWhitePoint,
                                                                           PDFCMSSettings::ColorAdaptationXYZ method);
};

}   // namespace pdf

#endif // PDFCMS_H
