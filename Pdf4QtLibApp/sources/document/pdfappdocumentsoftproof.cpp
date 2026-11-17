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

#include "pdfappdocumentsoftproof.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"
#include "pdfapproute.h"

#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"

#include <QFontMetrics>
#include <QPainter>
#include <QtMath>

namespace pdf
{

// ------------------------------------------------------------------ status item

PDFAppSoftProofStatusItem::PDFAppSoftProofStatusItem(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("statusSoftProof"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover);
}

PDFAppSoftProofStatusItem::~PDFAppSoftProofStatusItem() = default;

QSize PDFAppSoftProofStatusItem::sizeHint() const
{
    const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption);
    const qreal textWidth = PDFStylePainter::textWidth(text(), font);
    return QSize(PADDING + ICON + GAP + qCeil(textWidth) + PADDING, HEIGHT);
}

QSize PDFAppSoftProofStatusItem::minimumSizeHint() const
{
    return sizeHint();
}

void PDFAppSoftProofStatusItem::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // 485:17944: the pill of accent/teal, the icon and the text in the direction of the layout
    QPainter painter(this);
    const QRectF shape = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const PDFStylePainter::Radii radii(RADIUS);
    const bool isHovered = underMouse() && isEnabled();
    const bool isPressed = isDown();
    PDFStylePainter::fill(&painter, shape, radii, isPressed ? PDFThemeColors::AccentTealBgPressed : (isHovered ? PDFThemeColors::AccentTealBgHover : PDFThemeColors::AccentTealBg));
    PDFStylePainter::strokeInside(&painter, QRectF(rect()), radii, PDFThemeColors::AccentTealBorder);

    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption);
    const qreal textWidth = qMax(0.0, qreal(width() - PADDING - ICON - GAP - PADDING));
    const qreal iconX = isRightToLeft ? width() - PADDING - ICON : PADDING;
    const qreal textX = isRightToLeft ? PADDING : PADDING + ICON + GAP;
    PDFStylePainter::icon(&painter, QRectF(iconX, (height() - ICON) / 2.0, ICON, ICON), QStringLiteral("soft-proof"), PDFThemeColors::AccentTealIcon);

    // The name of a profile is data: elided at its end
    const QString shownText = QFontMetrics(font).elidedText(text(), Qt::ElideRight, int(textWidth));
    PDFStylePainter::text(&painter, QRectF(textX, 0.0, textWidth, height()), Qt::AlignVCenter | (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute,
                          shownText, font, PDFStylePainter::color(PDFThemeColors::TextPrimary));

    if (hasFocus())
    {
        PDFStylePainter::focusRing(&painter, shape, radii, false);
    }
}

// ------------------------------------------------------------------ state

PDFAppDocumentSoftProof::PDFAppDocumentSoftProof(QObject* parent) :
    BaseClass(parent)
{
    setObjectName(QString::fromLatin1(OBJECT_NAME));
}

PDFAppDocumentSoftProof::~PDFAppDocumentSoftProof() = default;

void PDFAppDocumentSoftProof::setProofing(bool isProofing)
{
    State state = m_state;
    state.isProofing = isProofing;
    setStateImpl(state);
}

void PDFAppDocumentSoftProof::setGamutChecking(bool isGamutChecking)
{
    State state = m_state;
    state.isGamutChecking = isGamutChecking;
    setStateImpl(state);
}

void PDFAppDocumentSoftProof::setProfile(const QString& profile)
{
    State state = m_state;
    state.profile = profile;
    setStateImpl(state);
}

void PDFAppDocumentSoftProof::setProfileTitle(const QString& title)
{
    State state = m_state;
    state.profileTitle = title;
    setStateImpl(state);
}

void PDFAppDocumentSoftProof::setIntent(RenderingIntent intent)
{
    State state = m_state;
    state.intent = intent;
    setStateImpl(state);
}

void PDFAppDocumentSoftProof::setAlarmColor(const QColor& color)
{
    State state = m_state;
    state.alarmColor = color;
    setStateImpl(state);
}

void PDFAppDocumentSoftProof::setKept(bool isKept)
{
    State state = m_state;
    state.isKept = isKept;
    setStateImpl(state);
}

void PDFAppDocumentSoftProof::setState(const State& state)
{
    setStateImpl(state);
}

void PDFAppDocumentSoftProof::setStateImpl(const State& state)
{
    State newState = state;

    // Q7.10: only the proof is kept after the tool (the gamut check goes with it, never alone)
    if (!newState.isProofing)
    {
        newState.isKept = false;
    }

    if (m_state == newState)
    {
        return;
    }

    m_state = newState;
    updateStatusItem();

    // Emit last (the view schedules an application of the settings)
    Q_EMIT changed();
}

PDFCMSSettings PDFAppDocumentSoftProof::applyTo(const PDFCMSSettings& settings) const
{
    PDFCMSSettings result = settings.withoutProofing();
    if (!isActive())
    {
        return result;
    }

    result.isSoftProofing = m_state.isProofing;
    result.isGamutChecking = m_state.isGamutChecking;
    result.softProofingProfile = m_state.profile;
    result.proofingIntent = m_state.intent;
    result.outOfGamutColor = m_state.alarmColor;
    return result;
}

QString PDFAppDocumentSoftProof::getStatusText() const
{
    const QString profile = !m_state.profileTitle.isEmpty() ? m_state.profileTitle : m_state.profile;
    return tr("Soft proof: %1", "Item of the status bar of a kept soft proof, 485:17529; %1 is the profile").arg(profile);
}

QToolButton* PDFAppDocumentSoftProof::createStatusItem(QWidget* parent)
{
    if (m_statusItem)
    {
        return m_statusItem;
    }

    PDFAppSoftProofStatusItem* item = new PDFAppSoftProofStatusItem(parent);
    item->setToolTip(tr("Open Soft Proofing"));
    connect(item, &QAbstractButton::clicked, this, &PDFAppDocumentSoftProof::onStatusItemClicked);
    connect(item, &QObject::destroyed, this, &PDFAppDocumentSoftProof::onStatusItemDestroyed);
    m_statusItem = item;
    updateStatusItem();
    return item;
}

void PDFAppDocumentSoftProof::updateStatusItem()
{
    if (!m_statusItem)
    {
        return;
    }

    const QString text = getStatusText();
    if (m_statusItem->text() != text)
    {
        m_statusItem->setText(text);
        m_statusItem->setAccessibleName(text);
        m_statusItem->updateGeometry();
    }
    m_statusItem->setVisible(m_state.isKept && m_state.isProofing);
}

void PDFAppDocumentSoftProof::onChanged()
{
    if (m_view)
    {
        m_view->scheduleApplySettings();
    }
}

void PDFAppDocumentSoftProof::onStatusItemClicked()
{
    // 485:17529: the item opens Soft Proofing in the tab of the document (the navigation is the last call)
    const PDFAppDocument* document = m_view ? m_view->getDocument() : nullptr;
    if (m_statusItem && document)
    {
        PDFAppPanelWidgets::navigate(m_statusItem, PDFAppRoutes::documentTool(document->getId(), QString::fromLatin1(TOOL_ID)));
    }
}

void PDFAppDocumentSoftProof::onStatusItemDestroyed()
{
    // A slot of destroyed only forgets the pointer (CLAUDE.md); QPointer is already null
}

PDFAppDocumentSoftProof* PDFAppDocumentSoftProof::findSoftProof(const PDFAppDocumentView* view)
{
    return view ? view->findChild<PDFAppDocumentSoftProof*>(QString::fromLatin1(OBJECT_NAME), Qt::FindDirectChildrenOnly) : nullptr;
}

PDFAppDocumentSoftProof* PDFAppDocumentSoftProof::getSoftProof(PDFAppDocumentView* view)
{
    if (!view)
    {
        return nullptr;
    }

    if (PDFAppDocumentSoftProof* softProof = findSoftProof(view))
    {
        return softProof;
    }

    // Owned by the view; a change is applied from the event loop (coalesced with the changes of the settings).
    // The view is held by QPointer: a change during the destruction of the view (a tool closing with it) does
    // not reach the view any more.
    PDFAppDocumentSoftProof* softProof = new PDFAppDocumentSoftProof(view);
    softProof->m_view = view;
    connect(softProof, &PDFAppDocumentSoftProof::changed, softProof, &PDFAppDocumentSoftProof::onChanged);
    if (PDFAppDocumentStatusBar* statusBar = view->getStatusBar())
    {
        view->addStatusItem(softProof->createStatusItem(statusBar), PDFAppDocumentStatusBar::Area::Trailing);
    }
    return softProof;
}

PDFCMSPointer PDFAppDocumentSoftProof::createCMSWithoutProofing(const PDFCMSManager* manager, const PDFDocument* document)
{
    if (!manager)
    {
        return PDFCMSPointer();
    }

    const PDFCMSSettings& settings = manager->getSettings();
    if (!settings.isSoftProofing && !settings.isGamutChecking)
    {
        return manager->getCurrentCMS();
    }

    // A manager of its own only for the creation (the shared object outlives it, as PDFAppDocumentView::createCMS)
    PDFCMSManager copy(nullptr);
    copy.setDocument(document);
    copy.setSettings(settings.withoutProofing());
    return copy.getCurrentCMS();
}

}   // namespace pdf

#include "moc_pdfappdocumentsoftproof.cpp"
