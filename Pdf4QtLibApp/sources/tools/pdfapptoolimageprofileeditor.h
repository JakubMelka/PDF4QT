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

#ifndef PDFAPPTOOLIMAGEPROFILEEDITOR_H
#define PDFAPPTOOLIMAGEPROFILEEDITOR_H

#include "pdfappglobal.h"

#include "pdfapppagelayout.h"
#include "pdfappvaluestore.h"
#include "pdfcomponentbuttons.h"
#include "pdfdocumentimagelist.h"
#include "pdffluentwidgets.h"

#include <QCheckBox>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <optional>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QPushButton;
class QVBoxLayout;

namespace pdf
{

class PDFAppContext;
class PDFAppParagraph;
class PDFAppSettingControl;
class PDFAppToolCard;
class PDFAppToolParameterModel;
class PDFLabel;
class PDFSpinBox;
class PDFTag;

/// The store of the fields of the profile editor of an override (301:3594): a field of a profile
/// ("tool.colorTargetDpi", "tool.bitonalThreshold", "tool.targetDpi" of Grayscale) is read from the
/// exception of the first image of the target in the parameter "images" (the global value, when it
/// has none) and written into the exceptions of all images of the target - one value of the model,
/// one undo step (PDFDocumentToolOptimizeImages::parseImageExceptions / writeImageExceptions, the
/// syntax of the core, never a second parser). An image, which is excluded, stays excluded.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImageOverrideStore : public PDFAppValueStore
{
public:
    explicit PDFAppToolImageOverrideStore(PDFAppToolParameterModel* model, std::vector<PDFDocumentImageList::Key> target);
    virtual ~PDFAppToolImageOverrideStore() override;

    virtual bool isAvailable() const override;
    virtual QVariant readValue(const QString& key) const override;
    virtual QString writeValue(const QString& key, const QVariant& value) override;

    /// The name of the setting of an exception of a parameter of a profile ("colorTargetDpi" ->
    /// "targetDpi", "bitonalThreshold" -> "threshold") or of the toolbar ("mode", "colorMode",
    /// "goal", "keepOriginalIfLarger", "preserveTransparency" - the same names); empty = the
    /// parameter has no setting of an exception
    static QString getExceptionSetting(const QString& parameter);

    /// Writes a setting of the exceptions of the images (an image without an exception gets one,
    /// an excluded one stays excluded) as one value of the model
    /// \returns the translated reason of a refusal, empty on success
    static QString writeSetting(PDFAppToolParameterModel* model, const std::vector<PDFDocumentImageList::Key>& target, const QString& setting, const QVariant& value);

    /// The value of a setting of the exception of the image, invalid = the image has none
    static QVariant readSetting(const PDFAppToolParameterModel* model, const PDFDocumentImageList::Key& key, const QString& setting);

private:
    QPointer<PDFAppToolParameterModel> m_model;
    std::vector<PDFDocumentImageList::Key> m_target;
};

/// The Settings editor of Optimize Images (297:3135, 301:3594) and the one profile of Grayscale
/// (308:6911): the segments Color / Grayscale / Bitonal (or one profile), Algorithm, Target DPI,
/// Resample, JPEG quality, JPEG2000 rate, PNG predictor, Threshold with Auto. The values are the
/// parameters of the model of the tool (PDFAppToolParameterModel, every change one undo step): the
/// flattened profile parameters of PDFDocumentToolOptimizeImages ("colorTargetDpi", ...; the one
/// profile of Grayscale without a prefix), or - for an override of the selected images (301:3594,
/// "Toolbar menus change the override too") - their exceptions in the parameter "images".
/// GUI thread; profileChanged is emitted last.
///
/// The section (section-Settings editor 298:3332, gap 8): the title Semi Bold 13 with the Tag of
/// the scope ("Global defaults", "Override · 3 images", "All converted images"), the note Regular
/// 11 text/muted, the Filter chips of the profiles (298:3338), the card of the fields (298:3357:
/// the rows of a label Regular 12 text/secondary and a control 140 x 34, gap 8), the check box
/// PNG predictor and the help Regular 11 text/muted.
///
/// Ownership: a child of the right panel of the tool GUI; the model is held by QPointer; the
/// controls are its children (rebuilt by a change of the profile or of the target).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImageProfileEditor : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Which profiles the editor shows
    enum class Mode
    {
        ThreeProfiles,  ///< Optimize Images: the segments Color / Grayscale / Bitonal
        SingleProfile   ///< Grayscale: one gray profile (no prefix of the parameters)
    };

    static constexpr int FIELD_LABEL_WIDTH = 111;
    static constexpr int FIELD_WIDTH = 140;
    static constexpr int FIELD_HEIGHT = 34;
    static constexpr int FIELD_GAP = 10;

    explicit PDFAppToolImageProfileEditor(PDFAppToolParameterModel* model, Mode mode, QWidget* parent);
    virtual ~PDFAppToolImageProfileEditor() override;

    Mode getMode() const { return m_mode; }

    /// The profile shown (PDFDocumentToolOptimizeImages::PROFILE_COLOR, _GRAY, _BITONAL); ignored by SingleProfile
    QString getProfile() const { return m_profile; }
    void setProfile(const QString& profile);

    /// The editor edits the override of these images (301:3594) instead of the global defaults;
    /// empty = the global defaults
    void setOverrideTarget(const std::vector<PDFDocumentImageList::Key>& keys);
    const std::vector<PDFDocumentImageList::Key>& getOverrideTarget() const { return m_overrideTarget; }

    /// The name of the parameter of a field of the shown profile ("colorTargetDpi", "targetDpi")
    QString getParameterName(const char* field) const;

    /// Parts (tests)
    PDFLabel* getTitleLabel() const { return m_title; }
    PDFTag* getScopeTag() const { return m_scopeTag; }
    PDFAppParagraph* getNoteLabel() const { return m_note; }
    PDFAppParagraph* getHelpLabel() const { return m_help; }
    QPushButton* getProfileChip(const QString& profile) const;
    QCheckBox* getThresholdAutoCheckBox() const { return m_thresholdAuto; }
    PDFSpinBox* getThresholdSpinBox() const { return m_thresholdSpinBox; }

    /// The binder of the parameter of the shown profile, nullptr = none
    PDFAppSettingControl* getControl(const QString& parameter) const;

Q_SIGNALS:
    /// The user has chosen another profile segment
    void profileChanged(const QString& profile);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void rebuild();
    void updateTexts();
    void refreshControls();
    void addField(const char* field, const QString& title);
    void addThreshold();
    std::shared_ptr<PDFAppValueStore> createStore() const;
    void onProfileChipToggled(int id, bool isChecked);
    void onModelChanged(const QString& name);
    void onThresholdAutoToggled(bool isChecked);
    void onThresholdValueChanged(int value);
    bool isBitonalProfile() const;

    /// Writes the threshold of the check box Auto / the spin box from the event loop: they are
    /// still in their own event, while a slot of the model may delete the window with them. A
    /// typed value (\p isTyped) waits for WRITE_DELAY ms of rest or for the end of the editing
    /// (RS4APP: one undo step for "128", not one per key, as PDFAppSettingControl).
    void scheduleThresholdWrite(int threshold, bool isTyped);
    void onThresholdEditingFinished();
    void onThresholdTimeout();
    void writePendingThreshold();

    /// The pending threshold is written from the event loop; while the editor is retired (step A: retire drops
    /// the queued calls, the timer is flushed, the focus leaves the field) it is written at once, the last call
    void queueThresholdWrite();

    QPointer<PDFAppToolParameterModel> m_model;
    Mode m_mode;
    QString m_profile;
    std::vector<PDFDocumentImageList::Key> m_overrideTarget;

    QPointer<PDFLabel> m_title;
    QPointer<PDFTag> m_scopeTag;
    QPointer<PDFAppParagraph> m_note;
    QPointer<QWidget> m_chips;
    QPointer<QButtonGroup> m_chipGroup;
    QPointer<PDFAppToolCard> m_card;
    QPointer<QVBoxLayout> m_cardLayout;
    QPointer<PDFAppParagraph> m_help;
    QPointer<QCheckBox> m_thresholdAuto;
    QPointer<PDFSpinBox> m_thresholdSpinBox;
    std::vector<QPointer<PDFAppSettingControl>> m_controls;
    std::vector<QPointer<PDFLabel>> m_fieldLabels;
    bool m_isUpdating = false;
    std::optional<int> m_pendingThreshold;                      ///< The threshold to be written (-1 = Auto)
    std::shared_ptr<PDFAppValueStore> m_pendingThresholdStore;  ///< The store of the target of the change
    bool m_isThresholdWriteQueued = false;                      ///< writePendingThreshold is posted to the event loop
    QTimer m_thresholdTimer;                                    ///< The rest after a typed threshold (queued to writePendingThreshold)
};

}   // namespace pdf

#endif // PDFAPPTOOLIMAGEPROFILEEDITOR_H
