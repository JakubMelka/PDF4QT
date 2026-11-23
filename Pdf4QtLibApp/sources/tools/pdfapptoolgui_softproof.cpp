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

#include "pdfapptoolgui_softproof.h"

#include "pdfappcolorpicker.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumentview.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolgui_extracttext.h"
#include "pdfapptooldrawingpart.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdeferreddeletion.h"
#include "pdffluentwidgets.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"

#include "pdfdocumenttool_softproof.h"
#include "pdfdocumenttoolregistry.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the GUI of Soft Proofing (no free functions, CLAUDE.md)
class PDFAppToolGUISoftProofHelper
{
public:
    PDFAppToolGUISoftProofHelper() = delete;

    /// Width of the panels of the profile and of the intent (panel-intent 497:19752: 340)
    static constexpr int PANEL_WIDTH = 340;

    /// Property of a row of a menu: the identifier of its profile / intent
    static constexpr const char* ROW_ID_PROPERTY = "pdf4qtSoftProofId";

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    /// The icon of an intent (497:19614): Auto monitor, Perceptual image, Relative intent, Absolute paper-white,
    /// Saturation bar-chart-2
    static QString getIntentIcon(RenderingIntent intent)
    {
        switch (intent)
        {
            case RenderingIntent::Auto:
                return QStringLiteral("monitor");
            case RenderingIntent::Perceptual:
                return QStringLiteral("image");
            case RenderingIntent::AbsoluteColorimetric:
                return QStringLiteral("paper-white");
            case RenderingIntent::Saturation:
                return QStringLiteral("bar-chart-2");
            case RenderingIntent::RelativeColorimetric:
            case RenderingIntent::Unknown:
                break;
        }
        return QStringLiteral("intent");
    }

    /// The intents of the menu in the order of 497:19614
    static std::vector<RenderingIntent> getIntents()
    {
        return { RenderingIntent::Auto, RenderingIntent::Perceptual, RenderingIntent::RelativeColorimetric, RenderingIntent::AbsoluteColorimetric, RenderingIntent::Saturation };
    }
};

// ------------------------------------------------------------------ PDFAppProofColorField

PDFAppProofColorField::PDFAppProofColorField(const QString& label, QWidget* parent) :
    BaseClass(parent)
{
    // color-Alarm color 497:19687: the head of 15, the swatches 6 below it
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(this, GAP);
    QHBoxLayout* head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(8);
    head->addWidget(PDFAppPageWidgets::createLabel(label, QFont::Normal, 12, PDFThemeColors::TextSecondary, this), 1);
    m_name = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextTertiary, this);
    m_name->setObjectName(QStringLiteral("proofColorName"));
    head->addWidget(m_name);
    layout->addLayout(head);

    m_row = new PDFColorSwatchRow(this);
    m_row->setObjectName(QStringLiteral("proofColorSwatches"));
    m_row->setOptions(PDFColorSwatchRow::Option::Inline | PDFColorSwatchRow::Option::More);
    m_row->setAccessibleName(label);
    connect(m_row.data(), &PDFColorSwatchRow::colorSelected, this, &PDFAppProofColorField::onColorSelected);
    PDFAppColorPicker::attach(m_row.data());
    layout->addWidget(m_row);
}

PDFAppProofColorField::~PDFAppProofColorField() = default;

void PDFAppProofColorField::setColor(const QColor& color)
{
    if (m_row)
    {
        m_row->setColor(color);
    }
    if (m_name)
    {
        m_name->setText(PDFAppToolDrawingStylePanel::getColorName(color));
    }
}

void PDFAppProofColorField::onColorSelected(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }
    if (m_name)
    {
        m_name->setText(PDFAppToolDrawingStylePanel::getColorName(color));
    }
    Q_EMIT colorSelected(color);
}

// ------------------------------------------------------------------ PDFAppSoftProofCanvasPage

PDFAppSoftProofCanvasPage::PDFAppSoftProofCanvasPage(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("softProofCanvasPage"));

    // document-canvas 479:11760: padding 20, gap 12
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(GAP);

    // preview-bar 497:19667: the title Semi Bold 13, the hint Regular 12 text/tertiary at the trailing edge
    QWidget* bar = new QWidget(this);
    bar->setFixedHeight(BAR_HEIGHT);
    QHBoxLayout* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(8);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, bar);
    m_title->setObjectName(QStringLiteral("softProofTitle"));
    barLayout->addWidget(m_title, 1);
    m_hint = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextTertiary, bar);
    m_hint->setObjectName(QStringLiteral("softProofHint"));
    barLayout->addWidget(m_hint);
    layout->addWidget(bar);

    // viewport 497:19670: the page fitted
    m_picture = new PDFAppToolPagePicture(this);
    m_picture->setObjectName(QStringLiteral("softProofPicture"));
    m_picture->setFit(true);
    layout->addWidget(m_picture, 1);
}

PDFAppSoftProofCanvasPage::~PDFAppSoftProofCanvasPage() = default;

PDFAppToolPagePicture* PDFAppSoftProofCanvasPage::getPicture() const
{
    return m_picture.data();
}

void PDFAppSoftProofCanvasPage::setTexts(const QString& title, const QString& hint)
{
    if (m_title)
    {
        m_title->setText(title);
    }
    if (m_hint)
    {
        m_hint->setText(hint);
        m_hint->setVisible(!hint.isEmpty());
    }
}

void PDFAppSoftProofCanvasPage::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas: bg/track
    QPainter painter(this);
    painter.fillRect(rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgTrack));
}

// ------------------------------------------------------------------ PDFAppToolGUISoftProof

PDFAppToolGUISoftProof::PDFAppToolGUISoftProof(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolGUISoftProof::~PDFAppToolGUISoftProof()
{
    // The dying GUI never runs its own slot of the state (it would update its parts and emit its signals)
    if (m_softProof)
    {
        m_softProof->disconnect(this);
    }

    // Close without Keep Proofing On gives the state of the entry back (485:17529, Q7.10). A kept proof of the
    // entry, which was turned off in the tool, stays off (497:19702 "Keep Proofing On leaves it active after the
    // tool is closed, until you turn it off here" - Keep Proofing On is Disabled without the proof, K7.9, so Close
    // is the only way to end it); the gamut check is never kept alone. The state emits changed, its only slot
    // now schedules an application of the settings of the view (nothing runs here).
    if (m_softProof && m_proofView && m_isEntered && !m_isKept)
    {
        PDFAppDocumentSoftProof::State state = m_entryState;
        if (m_entryState.isKept && !m_softProof->isProofing())
        {
            state = m_softProof->getState();
            state.isGamutChecking = false;
            state.isKept = false;
        }
        m_softProof->setState(state);
    }
}

void PDFAppToolGUISoftProof::registerSoftProofTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolSoftProof::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolSoftProof>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUISoftProof>>());
    }
}

PDFAppThumbnailsPanel* PDFAppToolGUISoftProof::getThumbnailsPanel() const
{
    return m_thumbnails.data();
}

PDFAppDocumentView* PDFAppToolGUISoftProof::getView() const
{
    const PDFAppToolHost* host = getHost();
    return host ? host->getView() : nullptr;
}

void PDFAppToolGUISoftProof::ensureSoftProof()
{
    if (m_isEntered)
    {
        return;
    }

    PDFAppDocumentView* view = getView();
    m_softProof = PDFAppDocumentSoftProof::getSoftProof(view);
    if (!m_softProof)
    {
        return;
    }

    m_isEntered = true;
    m_proofView = view;
    m_entryState = m_softProof->getState();

    // The tool shows the proof while it is open (479:11760: Soft Proof on at the entry); the status item of a
    // kept proof hides meanwhile (the tool shows the state itself). The parameters give the profile, the
    // intent and the alarm color. The state changes before the GUI follows it (its parts are being created).
    PDFAppDocumentSoftProof::State state = m_entryState;
    state.isKept = false;
    if (!state.isProofing && !state.isGamutChecking && getAvailability() == Availability::Available)
    {
        state.isProofing = true;
    }
    m_softProof->setState(state);
    applyParameters();
    if (m_softProof)
    {
        PDFDeferredDeletion::connect(m_softProof.data(), &PDFAppDocumentSoftProof::changed, this, &PDFAppToolGUISoftProof::onSoftProofChanged);
    }
}

PDFAppToolGUISoftProof::Availability PDFAppToolGUISoftProof::getAvailability() const
{
    const PDFAppDocumentView* view = getView();
    const PDFCMSManager* manager = view ? view->getCMSManager() : nullptr;
    if (!manager || manager->getSettings().system == PDFCMSSettings::System::Generic)
    {
        return Availability::GenericEngine;
    }

    for (const PDFSoftProofingProfile& profile : manager->getSoftProofingProfiles())
    {
        if (profile.isAvailable)
        {
            return Availability::Available;
        }
    }
    return Availability::NoProfile;
}

PDFSoftProofingProfiles PDFAppToolGUISoftProof::getProfiles() const
{
    const PDFAppDocumentView* view = getView();
    const PDFCMSManager* manager = view ? view->getCMSManager() : nullptr;
    return manager ? manager->getSoftProofingProfiles() : PDFSoftProofingProfiles();
}

QString PDFAppToolGUISoftProof::getEffectiveProfileId() const
{
    const PDFAppDocumentView* view = getView();
    const PDFCMSManager* manager = view ? view->getCMSManager() : nullptr;
    if (!manager)
    {
        return QString();
    }

    // The profile of the parameter, an empty one = the first available output intent of the document
    PDFCMSSettings settings = manager->getSettings();
    const PDFAppToolParameterModel* model = getModel();
    settings.softProofingProfile = model ? model->getValue(QString::fromLatin1(PDFDocumentToolSoftProof::PARAMETER_PROFILE)).toString() : QString();
    const QString id = manager->getEffectiveSoftProofingProfile(settings);
    if (!id.isEmpty())
    {
        return id;
    }

    // A document without an output intent (RS7FIGMA): the first available profile of the list, so the proof shown
    // as on is really a proof of a named profile (479:11760 "Page 3 · proof of Coated FOGRA39") - never "on" with no
    // profile, an empty name in the texts and the colors of the screen
    for (const PDFSoftProofingProfile& profile : manager->getSoftProofingProfiles())
    {
        if (profile.isAvailable)
        {
            return profile.id;
        }
    }
    return QString();
}

QString PDFAppToolGUISoftProof::getProfileTitle() const
{
    const PDFAppDocumentView* view = getView();
    const PDFCMSManager* manager = view ? view->getCMSManager() : nullptr;
    const QString id = getEffectiveProfileId();
    if (!manager || id.isEmpty())
    {
        return QString();
    }

    for (const PDFSoftProofingProfile& profile : manager->getSoftProofingProfiles())
    {
        if (profile.isAvailable && profile.id == id)
        {
            return !profile.name.isEmpty() ? profile.name : profile.fileName;
        }
    }
    return QString();
}

RenderingIntent PDFAppToolGUISoftProof::getIntent(const QString& id)
{
    using Tool = PDFDocumentToolSoftProof;
    if (id == QLatin1String(Tool::INTENT_AUTO))
    {
        return RenderingIntent::Auto;
    }
    if (id == QLatin1String(Tool::INTENT_PERCEPTUAL))
    {
        return RenderingIntent::Perceptual;
    }
    if (id == QLatin1String(Tool::INTENT_SATURATION))
    {
        return RenderingIntent::Saturation;
    }
    if (id == QLatin1String(Tool::INTENT_ABSOLUTE_COLORIMETRIC))
    {
        return RenderingIntent::AbsoluteColorimetric;
    }
    return RenderingIntent::RelativeColorimetric;
}

QString PDFAppToolGUISoftProof::getIntentId(RenderingIntent intent)
{
    using Tool = PDFDocumentToolSoftProof;
    switch (intent)
    {
        case RenderingIntent::Auto:
            return QString::fromLatin1(Tool::INTENT_AUTO);
        case RenderingIntent::Perceptual:
            return QString::fromLatin1(Tool::INTENT_PERCEPTUAL);
        case RenderingIntent::Saturation:
            return QString::fromLatin1(Tool::INTENT_SATURATION);
        case RenderingIntent::AbsoluteColorimetric:
            return QString::fromLatin1(Tool::INTENT_ABSOLUTE_COLORIMETRIC);
        case RenderingIntent::RelativeColorimetric:
        case RenderingIntent::Unknown:
            break;
    }
    return QString::fromLatin1(Tool::INTENT_RELATIVE_COLORIMETRIC);
}

QString PDFAppToolGUISoftProof::getIntentShortName(RenderingIntent intent)
{
    // The value of the row Intent of the card Proofing (479:11760 "Relative")
    switch (intent)
    {
        case RenderingIntent::Auto:
            return tr("Auto");
        case RenderingIntent::Perceptual:
            return tr("Perceptual");
        case RenderingIntent::Saturation:
            return tr("Saturation");
        case RenderingIntent::AbsoluteColorimetric:
            return tr("Absolute", "Short name of the absolute colorimetric rendering intent");
        case RenderingIntent::RelativeColorimetric:
        case RenderingIntent::Unknown:
            break;
    }
    return tr("Relative", "Short name of the relative colorimetric rendering intent");
}

QString PDFAppToolGUISoftProof::getIntentName(RenderingIntent intent)
{
    switch (intent)
    {
        case RenderingIntent::Auto:
            return tr("Auto");
        case RenderingIntent::Perceptual:
            return tr("Perceptual");
        case RenderingIntent::Saturation:
            return tr("Saturation");
        case RenderingIntent::AbsoluteColorimetric:
            return tr("Absolute colorimetric");
        case RenderingIntent::RelativeColorimetric:
        case RenderingIntent::Unknown:
            break;
    }
    return tr("Relative colorimetric");
}

RenderingIntent PDFAppToolGUISoftProof::getModelIntent() const
{
    const PDFAppToolParameterModel* model = getModel();
    return getIntent(model ? model->getValue(QString::fromLatin1(PDFDocumentToolSoftProof::PARAMETER_INTENT)).toString() : QString());
}

QColor PDFAppToolGUISoftProof::getModelAlarmColor() const
{
    const PDFAppToolParameterModel* model = getModel();
    const QVariant value = model ? model->getValue(QString::fromLatin1(PDFDocumentToolSoftProof::PARAMETER_ALARM_COLOR)) : QVariant();
    QColor color = value.value<QColor>();
    if (!color.isValid())
    {
        color = QColor(value.toString());
    }
    return color.isValid() ? color : QColor(Qt::red);
}

void PDFAppToolGUISoftProof::applyParameters()
{
    if (!m_softProof)
    {
        return;
    }

    // The parameters of the tool into the state of the tab: one change at once (the view applies it once)
    const PDFAppToolParameterModel* model = getModel();
    PDFAppDocumentSoftProof::State state = m_softProof->getState();
    state.profile = model ? model->getValue(QString::fromLatin1(PDFDocumentToolSoftProof::PARAMETER_PROFILE)).toString() : QString();
    if (state.profile.isEmpty())
    {
        // No profile chosen and no output intent of the document: the proof is of the profile the tool names
        state.profile = getEffectiveProfileId();
    }
    state.profileTitle = getProfileTitle();
    state.intent = getModelIntent();
    state.alarmColor = getModelAlarmColor();
    if (getAvailability() != Availability::Available)
    {
        // Nothing can be proofed (479:14751, 479:15335): the switches are off and Disabled
        state.isProofing = false;
        state.isGamutChecking = false;
    }
    m_softProof->setState(state);
}

void PDFAppToolGUISoftProof::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using H = PDFAppToolGUISoftProofHelper;

    ensureSoftProof();

    // tool-actions 497:19634: Soft Proof, Gamut Check | the profile, the intent
    m_proofButton = builder.addAction(tr("Soft Proof"), QStringLiteral("soft-proof"), this, &PDFAppToolGUISoftProof::onProofClicked);
    m_proofButton->setObjectName(QStringLiteral("softProofSwitch"));
    m_proofButton->setCheckable(true);
    m_gamutButton = builder.addAction(tr("Gamut Check"), QStringLiteral("gamut"), this, &PDFAppToolGUISoftProof::onGamutClicked);
    m_gamutButton->setObjectName(QStringLiteral("softProofGamutSwitch"));
    m_gamutButton->setCheckable(true);
    builder.addActionDivider();

    // Profile 479:12333: FROM THIS DOCUMENT (the output intents, also the unavailable ones with their reasons,
    // FP-6), CMYK PROFILES, the footer (Q7.11: no built-in profiles)
    m_profilePanel = builder.addMenu(QStringLiteral("profile"), QStringLiteral("icc-profile"), QString(), H::PANEL_WIDTH);
    m_profilePanel->getButton()->setObjectName(QStringLiteral("softProofProfileMenu"));
    bool isFirstIntent = true;
    bool isFirstProfile = true;
    for (const PDFSoftProofingProfile& profile : getProfiles())
    {
        const bool isOutputIntent = profile.source == PDFSoftProofingProfile::Source::OutputIntent;
        if (isOutputIntent && isFirstIntent)
        {
            m_profilePanel->addGroupCaption(tr("From this document"));
            isFirstIntent = false;
        }
        if (!isOutputIntent && isFirstProfile)
        {
            m_profilePanel->addGroupCaption(tr("CMYK profiles"));
            isFirstProfile = false;
        }

        QString title = !profile.name.isEmpty() ? profile.name : profile.fileName;
        QString description = profile.fileName;
        if (isOutputIntent)
        {
            title = tr("Output intent of the document");
            description = tr("%1, embedded in the file", "An output intent of 479:12333; %1 is its profile").arg(!profile.name.isEmpty() ? profile.name : profile.outputCondition);
        }
        if (!profile.isAvailable)
        {
            description = profile.reason;
        }

        PDFTileRow* row = m_profilePanel->addRow(title, description, isOutputIntent ? QStringLiteral("file") : QStringLiteral("icc-profile"), PDFTileRow::Kind::MenuItem);
        row->setProperty(H::ROW_ID_PROPERTY, profile.id);
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        row->setEnabled(profile.isAvailable);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUISoftProof::onProfileRowClicked);
    }
    m_profilePanel->addGroupDivider();
    PDFLabel* footer = m_profilePanel->addCaption(tr("Installed profiles and the profile folder in Settings › Color management."));
    footer->setWordWrap(true);
    footer->setObjectName(QStringLiteral("softProofProfileFooter"));

    // Intent 497:19614 (the icons of the rendering intents)
    m_intentPanel = builder.addMenu(QStringLiteral("intent"), QStringLiteral("intent"), QString(), H::PANEL_WIDTH);
    m_intentPanel->getButton()->setObjectName(QStringLiteral("softProofIntentMenu"));
    const std::vector<std::pair<RenderingIntent, QString>> intents =
    {
        { RenderingIntent::Auto, tr("Follows the rendering intent of the view") },
        { RenderingIntent::Perceptual, tr("Keeps the relations between colors, good for photos") },
        { RenderingIntent::RelativeColorimetric, tr("Keeps in-gamut colors exact, the default") },
        { RenderingIntent::AbsoluteColorimetric, tr("Also simulates the white of the paper") },
        { RenderingIntent::Saturation, tr("Keeps colors vivid, for charts") }
    };
    for (const auto& [intent, description] : intents)
    {
        PDFTileRow* row = m_intentPanel->addRow(getIntentName(intent), description, H::getIntentIcon(intent), PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("softProofIntent-") + getIntentId(intent));
        row->setProperty(H::ROW_ID_PROPERTY, getIntentId(intent));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUISoftProof::onIntentRowClicked);
    }

    updateParts();
}

void PDFAppToolGUISoftProof::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    ensureSoftProof();

    // 479:14751, 479:15335: why nothing can be proofed (amber, at the top)
    m_callout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), QString());
    m_callout->setObjectName(QStringLiteral("softProofCallout"));

    // section-Proofing 497:19675
    builder.addSection(tr("Proofing"));
    m_proofingRows = builder.addKeyValues();
    m_proofingRows->setObjectName(QStringLiteral("softProofProofingRows"));
    m_proofingRows->addRow(tr("Soft proofing"), QString());
    m_proofingRows->addRow(tr("Gamut checking"), QString());
    m_proofingRows->addRow(tr("Profile"), QString());
    m_proofingRows->addRow(tr("Intent"), QString());
    builder.endSection();

    // section-Out of gamut 497:19683: the alarm color and its text
    builder.addSection(tr("Out of gamut"));
    PDFAppToolCard* card = new PDFAppToolCard(builder.getWidget());
    card->setObjectName(QStringLiteral("softProofGamutCard"));
    card->setFieldSpacing();
    m_alarmField = new PDFAppProofColorField(tr("Alarm color"), card);
    m_alarmField->setObjectName(QStringLiteral("softProofAlarmColor"));
    connect(m_alarmField.data(), &PDFAppProofColorField::colorSelected, this, &PDFAppToolGUISoftProof::onAlarmColorSelected);
    card->getBodyLayout()->addWidget(m_alarmField);
    m_gamutText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, card);
    m_gamutText->setObjectName(QStringLiteral("softProofGamutText"));
    card->getBodyLayout()->addWidget(m_gamutText);
    builder.addWidget(card);
    builder.endSection();

    // 497:19702: what the proof changes
    builder.addHint(tr("Soft proofing changes only how the document is shown. The file is not modified. Keep Proofing On leaves it active after the tool is closed, until you turn it off here."));

    updateParts();
}

void PDFAppToolGUISoftProof::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-left 497:19647: Pages, the count, the thumbnails following the canvas
    const PDFAppDocument* document = getDocument();
    const PDFInteger pageCount = document && document->getDocument() ? PDFInteger(document->getDocument()->getCatalog()->getPageCount()) : 0;
    builder.addPanelHeader(tr("Pages"), tr("%Ln pages", "", int(pageCount)));

    m_thumbnails = new PDFAppThumbnailsPanel(builder.getWidget());
    m_thumbnails->setObjectName(QStringLiteral("softProofThumbnails"));
    m_thumbnails->setDocument(getDocument(), getView());
    m_thumbnails->getListView()->setSidePadding(0);
    builder.addWidget(m_thumbnails);
    builder.setFillsPanel(m_thumbnails);
}

QWidget* PDFAppToolGUISoftProof::createCanvasPage(QWidget* parent)
{
    ensureSoftProof();

    QPointer<PDFAppSoftProofCanvasPage> page = new PDFAppSoftProofCanvasPage(parent);
    m_canvasPage = page;
    PDFAppDocumentView* view = getView();
    // The only picture of a page with the proof of the tab (the others leave it out, Q7.10)
    page->getPicture()->setProofShown(true);
    page->getPicture()->setView(view);
    if (view && m_connectedView != view)
    {
        PDFDeferredDeletion::connect(view, &PDFAppDocumentView::displayedPageChanged, this, &PDFAppToolGUISoftProof::onDisplayedPageChanged);
        m_connectedView = view;
    }
    updateParts();
    return page.data();
}

QString PDFAppToolGUISoftProof::getCancelText() const
{
    // 479:11760 … 479:15922: Close (K7.9: it ends the proof of the tool, the state of before comes back)
    return tr("Close");
}

QString PDFAppToolGUISoftProof::getConfirmText() const
{
    return tr("Keep Proofing On");
}

QString PDFAppToolGUISoftProof::getConfirmIconName() const
{
    return QStringLiteral("soft-proof");
}

bool PDFAppToolGUISoftProof::isConfirmEnabled() const
{
    // K7.9 (designer 17. 11.): Disabled while the proof is off; a pressed button waits for the close
    return !m_isKeepRequested && m_softProof && m_softProof->isProofing() && getAvailability() == Availability::Available && BaseClass::isConfirmEnabled();
}

bool PDFAppToolGUISoftProof::confirmWithoutRun()
{
    markUsed();

    // Keep Proofing On never runs the tool (RS7APP L4: no environment, no refused run). The shell is inside of
    // its confirm: the close runs from the event loop; the button is Disabled until then (isConfirmEnabled; its
    // signals run foreign slots - the last call, the shell does nothing after it)
    if (!m_isKeepRequested)
    {
        m_isKeepRequested = true;
        QTimer::singleShot(0, this, &PDFAppToolGUISoftProof::onKeepQueued);
        updateConfirmState();
    }
    return true;
}

void PDFAppToolGUISoftProof::onKeepQueued()
{
    PDFDocumentToolShell* shell = getShell();
    if (!shell || shell->getState() != PDFDocumentToolShell::State::Editing)
    {
        m_isKeepRequested = false;
        updateConfirmState();
        return;
    }

    // 485:17529: the proof stays, the status item shows it; the tool closes as Close does (the last call)
    m_isKept = true;
    if (m_softProof)
    {
        m_softProof->setKept(true);
    }
    shell->closeAfterQuestion();
}

QString PDFAppToolGUISoftProof::getStatusText() const
{
    switch (getAvailability())
    {
        case Availability::GenericEngine:
            return tr("Soft proofing is not available with the Generic color engine.");
        case Availability::NoProfile:
            return tr("No CMYK profile found. Add a profile folder in Settings › Color management.");
        case Availability::Available:
            break;
    }

    const bool isProofing = m_softProof && m_softProof->isProofing();
    const bool isGamut = m_softProof && m_softProof->isGamutChecking();
    const QString profile = getProfileTitle();
    if (isProofing && isGamut)
    {
        return tr("Colors outside the gamut of %1 are painted in the alarm color.").arg(profile);
    }
    if (isProofing)
    {
        return tr("The page is shown as %1 would print it.").arg(profile);
    }
    if (isGamut)
    {
        return tr("Gamut checking without the proof colors.");
    }
    return tr("Soft proofing and gamut checking are off.");
}

void PDFAppToolGUISoftProof::onParametersChanged(const QString& name)
{
    BaseClass::onParametersChanged(name);

    // A change of the state runs updateParts (its signals may delete this GUI): nothing after it
    QPointer<PDFAppToolGUISoftProof> guard(this);
    applyParameters();
    if (guard)
    {
        updateParts();
    }
}

void PDFAppToolGUISoftProof::onDocumentChanged()
{
    BaseClass::onDocumentChanged();

    // Another version may have other output intents (the menu of the next entry lists them; the title now)
    QPointer<PDFAppToolGUISoftProof> guard(this);
    applyParameters();
    if (guard)
    {
        updateParts();
    }
}

void PDFAppToolGUISoftProof::onProofClicked()
{
    markUsed();

    // The state changes, its signal updates the parts (the last call); without a change the button follows
    const bool isChecked = m_proofButton && m_proofButton->isChecked();
    if (m_softProof && m_softProof->isProofing() != isChecked)
    {
        m_softProof->setProofing(isChecked);
        return;
    }
    updateParts();
}

void PDFAppToolGUISoftProof::onGamutClicked()
{
    markUsed();

    const bool isChecked = m_gamutButton && m_gamutButton->isChecked();
    if (m_softProof && m_softProof->isGamutChecking() != isChecked)
    {
        m_softProof->setGamutChecking(isChecked);
        return;
    }
    updateParts();
}

void PDFAppToolGUISoftProof::onProfileRowClicked()
{
    QObject* row = sender();
    PDFAppToolParameterModel* model = getModel();
    if (!row || !model)
    {
        return;
    }

    // The panel closes first, the change of the parameter is the last call (it runs the slots of the model)
    const QString id = row->property(PDFAppToolGUISoftProofHelper::ROW_ID_PROPERTY).toString();
    if (m_profilePanel)
    {
        m_profilePanel->close();
    }
    model->setValue(QString::fromLatin1(PDFDocumentToolSoftProof::PARAMETER_PROFILE), id);
}

void PDFAppToolGUISoftProof::onIntentRowClicked()
{
    QObject* row = sender();
    PDFAppToolParameterModel* model = getModel();
    if (!row || !model)
    {
        return;
    }

    const QString id = row->property(PDFAppToolGUISoftProofHelper::ROW_ID_PROPERTY).toString();
    if (m_intentPanel)
    {
        m_intentPanel->close();
    }
    model->setValue(QString::fromLatin1(PDFDocumentToolSoftProof::PARAMETER_INTENT), id);
}

void PDFAppToolGUISoftProof::onAlarmColorSelected(const QColor& color)
{
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValue(QString::fromLatin1(PDFDocumentToolSoftProof::PARAMETER_ALARM_COLOR), color);
    }
}

void PDFAppToolGUISoftProof::onSoftProofChanged()
{
    updateParts();
}

void PDFAppToolGUISoftProof::onDisplayedPageChanged(PDFInteger pageIndex)
{
    Q_UNUSED(pageIndex);
    updateParts();
}

void PDFAppToolGUISoftProof::updateParts()
{
    using H = PDFAppToolGUISoftProofHelper;

    const Availability availability = getAvailability();
    const bool isAvailable = availability == Availability::Available;
    const bool isProofing = m_softProof && m_softProof->isProofing();
    const bool isGamut = m_softProof && m_softProof->isGamutChecking();
    const QString profileTitle = getProfileTitle();
    const RenderingIntent intent = getModelIntent();
    const QString profileId = getEffectiveProfileId();

    // The toolbar (479:14751: only the profile menu stays; 479:15335: everything Disabled)
    if (m_proofButton)
    {
        m_proofButton->setChecked(isProofing);
        m_proofButton->setEnabled(isAvailable);
    }
    if (m_gamutButton)
    {
        m_gamutButton->setChecked(isGamut);
        m_gamutButton->setEnabled(isAvailable);
    }
    if (m_profilePanel)
    {
        QToolButton* button = m_profilePanel->getButton();
        const QString text = profileTitle.isEmpty() ? tr("No profile") : profileTitle;
        button->setText(text);
        button->setToolTip(text);
        button->setEnabled(availability != Availability::GenericEngine);

        for (const QPointer<PDFTileRow>& row : m_profilePanel->getRows())
        {
            if (row)
            {
                const QSignalBlocker blocker(row.data());
                const QString rowId = row->property(H::ROW_ID_PROPERTY).toString();
                row->setChecked(!rowId.isEmpty() && rowId == profileId);
            }
        }
    }
    if (m_intentPanel)
    {
        QToolButton* button = m_intentPanel->getButton();
        button->setText(getIntentName(intent));
        button->setToolTip(getIntentName(intent));
        button->setIcon(PDFThemeIcons::getIcon(H::getIntentIcon(intent)));
        button->setEnabled(isAvailable);
        const QString intentId = getIntentId(intent);
        for (const QPointer<PDFTileRow>& row : m_intentPanel->getRows())
        {
            if (row)
            {
                const QSignalBlocker blocker(row.data());
                row->setChecked(row->property(H::ROW_ID_PROPERTY).toString() == intentId);
            }
        }
    }

    // The right panel
    if (m_callout)
    {
        switch (availability)
        {
            case Availability::NoProfile:
                m_callout->setTexts(tr("No CMYK profile found"), tr("Soft proofing and gamut checking need a CMYK profile of the press or printer you want to simulate. Add a profile folder in Settings › Color management."));
                break;
            case Availability::GenericEngine:
                m_callout->setTexts(tr("Soft proofing needs the Little CMS 2 color engine"), tr("The Generic color engine is selected in Settings › Color management, and it cannot simulate another device."));
                break;
            case Availability::Available:
                break;
        }
        m_callout->setVisible(!isAvailable);
    }
    if (m_proofingRows && m_proofingRows->getRowCount() == 4)
    {
        m_proofingRows->setValue(0, isProofing ? tr("On") : tr("Off"));
        m_proofingRows->setValue(1, isGamut ? tr("On") : tr("Off"));
        m_proofingRows->setValue(2, profileTitle.isEmpty() ? tr("None") : profileTitle);
        m_proofingRows->setValue(3, getIntentShortName(intent));
    }
    if (m_alarmField)
    {
        m_alarmField->setColor(getModelAlarmColor());
    }
    if (m_gamutText)
    {
        m_gamutText->setText(isGamut ? tr("Colors that the chosen profile cannot reproduce are painted in the alarm color.")
                                     : tr("Turn Gamut Checking on to paint the colors that the chosen profile cannot reproduce."));
    }

    // The canvas: the current page as the view draws it
    if (m_canvasPage)
    {
        const PDFAppDocumentView* view = getView();
        const PDFAppDocument* document = getDocument();
        const PDFInteger pageCount = document && document->getDocument() ? PDFInteger(document->getDocument()->getCatalog()->getPageCount()) : 0;
        const PDFInteger pageIndex = view && pageCount > 0 ? qBound<PDFInteger>(0, view->getDisplayedPageIndex(), pageCount - 1) : -1;
        m_canvasPage->getPicture()->setPageIndex(pageIndex);
        const QString pageText = pageIndex >= 0 ? view->getPageLabel(pageIndex) : QString();
        const QString title = isProofing ? tr("Page %1 · proof of %2", "479:11760").arg(pageText, profileTitle) : tr("Page %1 · screen colors", "479:15922").arg(pageText);
        m_canvasPage->setTexts(pageIndex >= 0 ? title : QString(), isGamut ? tr("Gamut checking on") : QString());
    }

    // The signals of the Primary button and of the status bar run foreign slots, which may delete this GUI
    // with its shell: each is the last call of what follows it (emit last)
    QPointer<PDFAppToolGUISoftProof> guard(this);
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

}   // namespace pdf

#include "moc_pdfapptoolgui_softproof.cpp"
