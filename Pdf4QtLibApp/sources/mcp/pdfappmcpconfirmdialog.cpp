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

#include "pdfappmcpconfirmdialog.h"
#include "pdfappmcpchange.h"
#include "pdfappmcpclientnames.h"
#include "pdfappmcpdocumentresolver.h"
#include "pdfapplocale.h"
#include "pdfappdocument.h"

#include "pdfcomponentassistants.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfmcpjson.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace pdf
{

/// Helpers of the confirmation dialog
class PDFAppMcpConfirmDialogHelper
{
public:
    PDFAppMcpConfirmDialogHelper() = delete;

    using Color = PDFThemeColors::Color;
    using Role = PDFThemeFonts::Role;

    /// Icon of the design without a fill (16 px)
    static PDFIconBadge* createIcon(const QString& iconName, int extent, Color color, QWidget* parent)
    {
        PDFIconBadge* icon = new PDFIconBadge(iconName, extent, extent, parent);
        icon->setColors(std::nullopt, color);
        icon->setFixedSize(extent, extent);
        return icon;
    }

    static PDFLabel* createLabel(Role role, Color color, QWidget* parent, bool isWrapped)
    {
        PDFLabel* label = new PDFLabel(QString(), role, color, parent);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(isWrapped);
        return label;
    }

    /// Time "16:44" of a time stamp in milliseconds
    static QString formatTime(qint64 time)
    {
        return PDFAppLocale::formatClock(QDateTime::fromMSecsSinceEpoch(time).time());
    }

    static QString getPermissionName(PDFAppMcpPermissionKind kind)
    {
        switch (kind)
        {
            case PDFAppMcpPermissionKind::Read:
                return PDFAppMcpConfirmDialog::tr("Read documents");
            case PDFAppMcpPermissionKind::Edit:
                return PDFAppMcpConfirmDialog::tr("Edit documents");
            case PDFAppMcpPermissionKind::Files:
                return PDFAppMcpConfirmDialog::tr("Open and save files");
            case PDFAppMcpPermissionKind::None:
                break;
        }
        return QString();
    }

    static QString getFileName(const QString& path)
    {
        return QFileInfo(path).fileName();
    }
};

PDFAppMcpBodyBox::PDFAppMcpBodyBox(QWidget* parent) :
    BaseClass(parent)
{
    setAttribute(Qt::WA_NoSystemBackground, true);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppMcpBodyBox::update));
}

PDFAppMcpBodyBox::~PDFAppMcpBodyBox() = default;

void PDFAppMcpBodyBox::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainter::fill(&painter, QRectF(rect()), PDFStylePainter::Radii(10.0), PDFThemeColors::BgSubtle);
}

PDFAppMcpConfirmScrim::PDFAppMcpConfirmScrim(QWidget* host) :
    BaseClass(host),
    m_host(host)
{
    setObjectName(QStringLiteral("mcpConfirmScrim"));
    setAttribute(Qt::WA_NoSystemBackground, true);
    if (host)
    {
        setGeometry(host->rect());
        host->installEventFilter(this);
    }
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppMcpConfirmScrim::update));
}

PDFAppMcpConfirmScrim::~PDFAppMcpConfirmScrim()
{
    if (m_host)
    {
        m_host->removeEventFilter(this);
    }
}

void PDFAppMcpConfirmScrim::setDialog(QWidget* dialog)
{
    if (m_dialog)
    {
        m_dialog->removeEventFilter(this);
    }

    m_dialog = dialog;
    if (dialog)
    {
        dialog->setParent(this);
        dialog->installEventFilter(this);
        dialog->show();
    }
    placeDialog();
}

void PDFAppMcpConfirmScrim::placeDialog()
{
    QWidget* dialog = m_dialog.data();
    if (!dialog)
    {
        update();
        return;
    }

    // The card is centered over the tab; a low tab keeps its top inside
    const int width = qMin(dialog->sizeHint().width(), qMax(1, this->width() - 16));
    const int height = dialog->hasHeightForWidth() ? dialog->heightForWidth(width) : dialog->sizeHint().height();
    QRect geometry(0, 0, width, qMax(1, height));
    geometry.moveCenter(rect().center());
    geometry.moveTop(qMax(8, geometry.top()));
    geometry.moveLeft(qMax(8, geometry.left()));
    dialog->setGeometry(geometry);
    update();
}

void PDFAppMcpConfirmScrim::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgScrim);
    if (m_dialog && m_dialog->isVisible())
    {
        // The shadow of the card (0 / 8 / 24 effect/shadow) below the dialog
        PDFStylePainter::shadow(&painter, QRectF(m_dialog->geometry()), PDFStylePainter::Radii(16.0), QPointF(0.0, 8.0), 24.0,
                                PDFStylePainter::color(PDFThemeColors::EffectShadow));
    }
}

void PDFAppMcpConfirmScrim::mousePressEvent(QMouseEvent* event)
{
    // The tab below the scrim takes no input
    event->accept();
}

void PDFAppMcpConfirmScrim::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
}

void PDFAppMcpConfirmScrim::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->accept();
}

void PDFAppMcpConfirmScrim::wheelEvent(QWheelEvent* event)
{
    event->accept();
}

void PDFAppMcpConfirmScrim::contextMenuEvent(QContextMenuEvent* event)
{
    event->accept();
}

bool PDFAppMcpConfirmScrim::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_host && event->type() == QEvent::Resize)
    {
        setGeometry(m_host->rect());
        placeDialog();
    }
    else if (watched == m_dialog && (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show))
    {
        placeDialog();
    }
    return BaseClass::eventFilter(watched, event);
}

PDFAppMcpConfirmDialog::PDFAppMcpConfirmDialog(std::shared_ptr<PDFAppMcpConfirmation> confirmation, const PDFAppMcpDocumentResolver* resolver, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Question, QString(), QString(), QDialogButtonBox::NoButton, parent),
    m_confirmation(std::move(confirmation)),
    m_resolver(resolver)
{
    if (!m_confirmation)
    {
        m_confirmation = std::make_shared<PDFAppMcpConfirmation>();
    }

    setObjectName(QStringLiteral("mcpConfirmDialog"));

    // A widget inside of its scrim, modal for its tab only (958:28030)
    setModal(false);
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, true);

    setBadgeVisible(false);
    setCardWidth(CARD_WIDTH);
    setNoDefaultButton(true);
    setInputDelay(1000);
    build();
    updateTexts();
}

PDFAppMcpConfirmDialog::~PDFAppMcpConfirmDialog() = default;

QPushButton* PDFAppMcpConfirmDialog::getAllowButton() const
{
    return m_allowButton.data();
}

QPushButton* PDFAppMcpConfirmDialog::getDenyButton() const
{
    return m_denyButton.data();
}

PDFTextLink* PDFAppMcpConfirmDialog::getShowOnPageLink() const
{
    return m_showOnPageLink.data();
}

quint64 PDFAppMcpConfirmDialog::getConfirmationId() const
{
    return m_confirmation ? m_confirmation->id : 0;
}

QString PDFAppMcpConfirmDialog::sanitize(const QString& text, int maximumLength)
{
    QString result = PDFMcpJson::sanitizeString(text, PDFMcpJson::SanitizeFlag::RemoveBidi, qMax(1, maximumLength) * 4);
    result.replace(QLatin1Char('\n'), QLatin1Char(' '));
    result.replace(QLatin1Char('\t'), QLatin1Char(' '));
    result = result.simplified();
    if (result.size() > maximumLength)
    {
        // A pair of surrogates is never cut
        qsizetype cut = qMax<qsizetype>(0, maximumLength - 1);
        if (cut > 0 && result.at(cut - 1).isHighSurrogate())
        {
            --cut;
        }
        result = result.left(cut) + QChar(0x2026);
    }
    return result;
}

QString PDFAppMcpConfirmDialog::formatRemainingTime(qint64 milliseconds)
{
    const qint64 seconds = qMax<qint64>(0, (milliseconds + 999) / 1000);
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString PDFAppMcpConfirmDialog::createTitle(const PDFAppMcpConfirmation& confirmation)
{
    const QString client = sanitize(confirmation.clientName.isEmpty() ? tr("The assistant") : confirmation.clientName, 64);
    const QString document = confirmation.document ? sanitize(confirmation.document->getTitle()) : QString();
    const QString path = confirmation.details.value(QStringLiteral("path")).toString();

    switch (confirmation.kind)
    {
        case PDFAppMcpConfirmationKind::ReadDocuments:
            return tr("%1 wants to read your open documents").arg(client);
        case PDFAppMcpConfirmationKind::Change:
            return document.isEmpty() ? tr("%1 wants to change a document").arg(client) : tr("%1 wants to change %2").arg(client, document);
        case PDFAppMcpConfirmationKind::Save:
            return tr("%1 wants to save %2").arg(client, document);
        case PDFAppMcpConfirmationKind::SaveCopy:
            return tr("%1 wants to save a copy as %2").arg(client, sanitize(PDFAppMcpConfirmDialogHelper::getFileName(path)));
        case PDFAppMcpConfirmationKind::Open:
            return tr("%1 wants to open a document").arg(client);
        case PDFAppMcpConfirmationKind::Close:
            return tr("%1 wants to close %2").arg(client, document);
    }
    return QString();
}

QString PDFAppMcpConfirmDialog::createReason(const PDFAppMcpConfirmation& confirmation)
{
    // 958:28030: the reason names every permission set to Ask, which the request
    // needs; without one the rule, by which the dialog appears
    QStringList names;
    for (PDFAppMcpPermissionKind kind : { PDFAppMcpPermissionKind::Read, PDFAppMcpPermissionKind::Edit, PDFAppMcpPermissionKind::Files })
    {
        if (confirmation.permissions.testFlag(kind))
        {
            names << PDFAppMcpConfirmDialogHelper::getPermissionName(kind);
        }
    }

    if (names.size() == 1)
    {
        return tr("Asked because %1 is set to Ask").arg(names.front());
    }
    if (names.size() == 2)
    {
        return tr("Asked because %1 and %2 are set to Ask").arg(names[0], names[1]);
    }
    if (names.size() == 3)
    {
        return tr("Asked because %1, %2 and %3 are set to Ask").arg(names[0], names[1], names[2]);
    }
    if (confirmation.isAlwaysConfirmed)
    {
        return tr("Overwriting the original file is always confirmed");
    }
    if (confirmation.isAfterDialog)
    {
        return tr("Asked because a dialog was open when the request came");
    }
    return QString();
}

QWidget* PDFAppMcpConfirmDialog::createHead()
{
    using Helper = PDFAppMcpConfirmDialogHelper;

    QWidget* head = new QWidget(this);
    head->setObjectName(QStringLiteral("head"));
    QHBoxLayout* layout = new QHBoxLayout(head);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    // The monogram tile of the assistant (36, without the CLI badge in the
    // document dialogs 940:134119), the mcp icon of an unknown client
    const PDFAppMcpClientInfo client = PDFAppMcpClientNames::describe(m_confirmation->clientNameRaw);
    // (the tile of Connect an assistant: accent/<hue>/bg without a border, Bold 13 -
    // the avatar of the assistants draws the mcp icon in teal without a monogram)
    PDFComponentPainter::AssistantAvatar avatarData;
    avatarData.monogram = client.monogram;
    avatarData.hue = client.monogram.isEmpty() ? PDFAccentHue::Teal : client.hue;
    avatarData.extent = 36;
    avatarData.fontPixelSize = 13;
    PDFAssistantAvatar* avatar = new PDFAssistantAvatar(avatarData, head);
    avatar->setObjectName(QStringLiteral("monogram"));
    layout->addWidget(avatar, 0, Qt::AlignVCenter);

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    m_titleLabel = Helper::createLabel(PDFThemeFonts::Role::DialogTitle, PDFThemeColors::TextPrimary, head, true);
    m_titleLabel->setObjectName(QStringLiteral("title"));
    textLayout->addWidget(m_titleLabel);

    QHBoxLayout* originLayout = new QHBoxLayout();
    originLayout->setContentsMargins(0, 0, 0, 0);
    originLayout->setSpacing(6);
    if (m_confirmation->isRemote)
    {
        m_remoteTag = new PDFTag(QString(), head);
        m_remoteTag->setObjectName(QStringLiteral("remoteTag"));
        originLayout->addWidget(m_remoteTag, 0, Qt::AlignVCenter);
    }
    m_originLabel = Helper::createLabel(PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, head, false);
    m_originLabel->setObjectName(QStringLiteral("origin"));
    originLayout->addWidget(m_originLabel, 1);
    textLayout->addLayout(originLayout);
    layout->addLayout(textLayout, 1);

    // The position in the queue at the trailing end of the head (955:153984)
    m_queueTag = new PDFTag(QString(), head);
    m_queueTag->setObjectName(QStringLiteral("queueTag"));
    m_queueTag->hide();
    layout->addWidget(m_queueTag, 0, Qt::AlignVCenter);
    return head;
}

QWidget* PDFAppMcpConfirmDialog::createInfoRow(const QString& iconName, PDFLabel** label)
{
    using Helper = PDFAppMcpConfirmDialogHelper;

    // Info row (940:134144): icon 16, gap 8, Regular 12 text/secondary
    QWidget* row = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(Helper::createIcon(iconName, 16, PDFThemeColors::IconMuted, row), 0, Qt::AlignTop);
    PDFLabel* text = Helper::createLabel(PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, row, true);
    text->setLineHeight(16);
    layout->addWidget(text, 1);
    if (label)
    {
        *label = text;
    }
    return row;
}

void PDFAppMcpConfirmDialog::addChangeBody()
{
    using Helper = PDFAppMcpConfirmDialogHelper;

    PDFAppMcpBodyBox* box = new PDFAppMcpBodyBox(this);
    box->setObjectName(QStringLiteral("changes"));
    QVBoxLayout* layout = new QVBoxLayout(box);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(10);

    std::vector<PDFAppMcpChangeRow> rows;
    for (const QJsonValue& value : m_confirmation->details.value(QStringLiteral("rows")).toArray())
    {
        rows.push_back(PDFAppMcpChangeRow::fromJson(value.toObject()));
    }

    // A tool of the catalog: one row of the tool
    if (rows.empty())
    {
        PDFAppMcpChangeRow row;
        row.iconName = QStringLiteral("sliders");
        const QString tool = m_confirmation->details.value(QStringLiteral("tool")).toString();
        row.text = tool.isEmpty() ? tr("Change the document") : tr("Run %1").arg(sanitize(tool));
        rows.push_back(row);
    }

    const int shown = qMin<int>(int(rows.size()), MAXIMUM_ROWS);
    for (int i = 0; i < shown; ++i)
    {
        const PDFAppMcpChangeRow& row = rows[i];

        // Change row (940:134128): icon 16, gap 10, the action Medium 12 with the
        // page Regular 12 text/tertiary, the quote Regular 11 text/muted
        QWidget* rowWidget = new QWidget(box);
        rowWidget->setObjectName(QStringLiteral("changeRow"));
        QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(10);
        rowLayout->addWidget(Helper::createIcon(row.iconName.isEmpty() ? QStringLiteral("edit") : row.iconName, 16, PDFThemeColors::IconDefault, rowWidget), 0, Qt::AlignTop);

        QVBoxLayout* textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(2);
        QHBoxLayout* lineLayout = new QHBoxLayout();
        lineLayout->setContentsMargins(0, 0, 0, 0);
        lineLayout->setSpacing(8);
        PDFLabel* action = Helper::createLabel(PDFThemeFonts::Role::SmallMedium, PDFThemeColors::TextPrimary, rowWidget, false);
        action->setText(sanitize(row.text));
        lineLayout->addWidget(action, 1);
        if (!row.page.isEmpty())
        {
            PDFLabel* page = Helper::createLabel(PDFThemeFonts::Role::Small, PDFThemeColors::TextTertiary, rowWidget, false);
            page->setText(sanitize(row.page));
            lineLayout->addWidget(page, 0);
        }
        textLayout->addLayout(lineLayout);
        m_rowLabels.emplace_back(action);

        if (!row.detail.isEmpty())
        {
            PDFLabel* detail = Helper::createLabel(PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, rowWidget, true);
            detail->setLineHeight(15);
            detail->setText(sanitize(row.detail, MAXIMUM_QUOTE_LENGTH * 2));
            textLayout->addWidget(detail);
        }
        rowLayout->addLayout(textLayout, 1);
        layout->addWidget(rowWidget);
    }

    if (int(rows.size()) > shown)
    {
        PDFLabel* more = Helper::createLabel(PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, box, false);
        more->setText(tr("and %Ln more", nullptr, int(rows.size()) - shown));
        more->setContentsMargins(26, 0, 0, 0);
        layout->addWidget(more);
    }

    // Show on Page of the proposed annotations (955:143756, aligned to the text)
    const bool hasGhosts = !m_confirmation->details.value(QStringLiteral("ghosts")).toArray().isEmpty();
    if (hasGhosts && m_confirmation->document)
    {
        QHBoxLayout* linkLayout = new QHBoxLayout();
        linkLayout->setContentsMargins(26, 0, 0, 0);
        m_showOnPageLink = new PDFTextLink(QString(), box);
        m_showOnPageLink->setObjectName(QStringLiteral("showOnPage"));
        connect(m_showOnPageLink, &PDFTextLink::clicked, this, &PDFAppMcpConfirmDialog::onShowOnPageClicked);
        linkLayout->addWidget(m_showOnPageLink);
        linkLayout->addStretch(1);
        layout->addLayout(linkLayout);
    }
    addContent(box);

    // The signature warning of a signed or certified document (958:27533), above the undo note as in 958:27126
    if (m_confirmation->details.value(QStringLiteral("signatureCount")).toInt() > 0)
    {
        PDFTintedCard* card = new PDFTintedCard(PDFAccentHue::Amber, this);
        card->setObjectName(QStringLiteral("signatureWarning"));
        card->setRadius(10.0);
        QHBoxLayout* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(10);
        cardLayout->addWidget(Helper::createIcon(QStringLiteral("alert-triangle"), 16, PDFThemeColors::AccentAmberIcon, card), 0, Qt::AlignTop);
        QVBoxLayout* textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(2);
        PDFLabel* title = Helper::createLabel(PDFThemeFonts::Role::SmallMedium, PDFThemeColors::TextPrimary, card, true);
        title->setLineHeight(16);
        title->setProperty("noteKind", QStringLiteral("signatureTitle"));
        PDFLabel* text = Helper::createLabel(PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, card, true);
        text->setLineHeight(16);
        text->setProperty("noteKind", QStringLiteral("signatureText"));
        textLayout->addWidget(title);
        textLayout->addWidget(text);
        cardLayout->addLayout(textLayout, 1);
        m_noteLabels.emplace_back(title);
        m_noteLabels.emplace_back(text);
        addContent(card);
    }

    // The undo note (940:134144)
    PDFLabel* undoLabel = nullptr;
    QWidget* undoRow = createInfoRow(QStringLiteral("undo"), &undoLabel);
    undoRow->setObjectName(QStringLiteral("undoNote"));
    undoLabel->setProperty("noteKind", QStringLiteral("undo"));
    m_noteLabels.emplace_back(undoLabel);
    addContent(undoRow);
}

void PDFAppMcpConfirmDialog::addReadBody()
{
    using Helper = PDFAppMcpConfirmDialogHelper;

    // The open documents (966:150694)
    PDFAppMcpBodyBox* box = new PDFAppMcpBodyBox(this);
    box->setObjectName(QStringLiteral("documents"));
    QVBoxLayout* layout = new QVBoxLayout(box);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(10);
    m_bodyLabel = Helper::createLabel(PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, box, true);
    m_bodyLabel->setLineHeight(16);
    m_bodyLabel->setProperty("noteKind", QStringLiteral("readMeans"));
    layout->addWidget(m_bodyLabel);

    std::vector<PDFAppDocument*> documents = m_resolver ? m_resolver->getDocuments() : std::vector<PDFAppDocument*>();
    const PDFAppDocument* activeDocument = m_resolver ? m_resolver->getActiveDocument() : nullptr;
    const int shown = qMin<int>(int(documents.size()), MAXIMUM_ROWS);
    for (int i = 0; i < shown; ++i)
    {
        const PDFAppDocument* document = documents[i];
        QWidget* rowWidget = new QWidget(box);
        rowWidget->setObjectName(QStringLiteral("documentRow"));
        QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(10);
        rowLayout->addWidget(Helper::createIcon(QStringLiteral("file"), 16, PDFThemeColors::IconDefault, rowWidget), 0, Qt::AlignTop);

        QVBoxLayout* textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(2);
        QHBoxLayout* lineLayout = new QHBoxLayout();
        lineLayout->setContentsMargins(0, 0, 0, 0);
        lineLayout->setSpacing(8);
        PDFLabel* name = Helper::createLabel(PDFThemeFonts::Role::SmallMedium, PDFThemeColors::TextPrimary, rowWidget, false);
        name->setText(sanitize(document->getTitle()));
        lineLayout->addWidget(name, 1);
        if (document->isReady())
        {
            PDFLabel* pages = Helper::createLabel(PDFThemeFonts::Role::Small, PDFThemeColors::TextTertiary, rowWidget, false);
            const PDFInteger pageCount = document->getPageCount();
            pages->setText(pageCount == 1 ? tr("1 page") : tr("%1 pages").arg(PDFAppLocale::formatNumber(pageCount)));
            lineLayout->addWidget(pages);
        }
        textLayout->addLayout(lineLayout);
        PDFLabel* tab = Helper::createLabel(PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, rowWidget, false);
        tab->setText(document == activeDocument ? tr("Active tab") : tr("Background tab"));
        textLayout->addWidget(tab);
        rowLayout->addLayout(textLayout, 1);
        layout->addWidget(rowWidget);
        m_rowLabels.emplace_back(name);
    }

    if (int(documents.size()) > shown)
    {
        PDFLabel* more = Helper::createLabel(PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, box, false);
        more->setText(tr("and %Ln more", nullptr, int(documents.size()) - shown));
        layout->addWidget(more);
    }
    addContent(box);

    // The scope of the approval (966:150711)
    PDFLabel* scopeLabel = nullptr;
    QWidget* scopeRow = createInfoRow(QStringLiteral("unplug"), &scopeLabel);
    scopeRow->setObjectName(QStringLiteral("scope"));
    m_scopeLabel = scopeLabel;
    addContent(scopeRow);
}

void PDFAppMcpConfirmDialog::addSaveBody()
{
    // The facts of the save (940:134688): Key value rows in the body box (py 6)
    PDFAppMcpBodyBox* box = new PDFAppMcpBodyBox(this);
    box->setObjectName(QStringLiteral("facts"));
    QVBoxLayout* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->setSpacing(0);

    const bool isCopy = m_confirmation->kind == PDFAppMcpConfirmationKind::SaveCopy;
    const bool isOverwrite = m_confirmation->details.value(QStringLiteral("overwrite")).toBool();
    const QString path = m_confirmation->details.value(QStringLiteral("path")).toString();

    PDFKeyValueList* facts = new PDFKeyValueList(box);
    facts->setObjectName(QStringLiteral("factsList"));
    if (isCopy)
    {
        facts->addRow(tr("Save"), isOverwrite ? tr("Replace the existing file") : tr("As a new file – the original stays unchanged"));
        facts->addRow(tr("File"), sanitize(path, 1024), PDFComponentPainter::TextKind::FileName);
        const QString content = m_confirmation->details.value(QStringLiteral("content")).toString();
        facts->addRow(tr("Content"), sanitize(content.isEmpty() && m_confirmation->document ? m_confirmation->document->getTitle() : content, 256));
    }
    else
    {
        facts->addRow(tr("Save"), tr("Overwrite the file on the disk"));
        facts->addRow(tr("File"), sanitize(path, 1024), PDFComponentPainter::TextKind::FileName);
        const QString unsaved = m_confirmation->details.value(QStringLiteral("unsaved")).toString();
        facts->addRow(tr("Unsaved changes"), unsaved.isEmpty() ? tr("None") : sanitize(unsaved, 256));
    }

    // A tool of the catalog gives its own facts
    for (const QJsonValue& value : m_confirmation->details.value(QStringLiteral("facts")).toArray())
    {
        const QJsonObject fact = value.toObject();
        const QString key = fact.value(QStringLiteral("key")).toString();
        if (!key.isEmpty() && facts->getRowCount() < 8)
        {
            facts->addRow(sanitize(key, 40), sanitize(fact.value(QStringLiteral("value")).toString(), 256));
        }
    }
    layout->addWidget(facts);
    addContent(box);

    // The overwrite note (940:134698): the previous content of the file is lost
    if (!isCopy || isOverwrite)
    {
        PDFLabel* noteLabel = nullptr;
        QWidget* noteRow = createInfoRow(QStringLiteral("alert-triangle"), &noteLabel);
        noteRow->setObjectName(QStringLiteral("overwriteNote"));
        noteLabel->setProperty("noteKind", QStringLiteral("overwrite"));
        m_noteLabels.emplace_back(noteLabel);
        addContent(noteRow);
    }
}

void PDFAppMcpConfirmDialog::addOpenBody()
{
    using Helper = PDFAppMcpConfirmDialogHelper;

    // The file (941:135721): icon, the name Semi Bold 13, the path in mono 12
    const QString path = m_confirmation->details.value(QStringLiteral("path")).toString();
    PDFAppMcpBodyBox* box = new PDFAppMcpBodyBox(this);
    box->setObjectName(QStringLiteral("file"));
    QHBoxLayout* layout = new QHBoxLayout(box);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(10);
    layout->addWidget(Helper::createIcon(QStringLiteral("file"), 16, PDFThemeColors::IconDefault, box), 0, Qt::AlignTop);
    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    PDFLabel* name = Helper::createLabel(PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, box, false);
    name->setText(sanitize(PDFAppMcpConfirmDialogHelper::getFileName(path)));
    PDFLabel* pathLabel = Helper::createLabel(PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, box, true);
    pathLabel->setFont(PDFThemeFonts::getMonospaceFont(QFont::Normal, 12));
    pathLabel->setText(sanitize(path, 1024));
    textLayout->addWidget(name);
    textLayout->addWidget(pathLabel);
    layout->addLayout(textLayout, 1);
    m_rowLabels.emplace_back(name);
    addContent(box);

    // The body (941:135727)
    m_bodyLabel = Helper::createLabel(PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this, true);
    m_bodyLabel->setLineHeight(16);
    m_bodyLabel->setProperty("noteKind", QStringLiteral("openBody"));
    addContent(m_bodyLabel);
}

void PDFAppMcpConfirmDialog::addCloseBody()
{
    using Helper = PDFAppMcpConfirmDialogHelper;

    m_bodyLabel = Helper::createLabel(PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this, true);
    m_bodyLabel->setLineHeight(16);
    m_bodyLabel->setProperty("noteKind", QStringLiteral("closeBody"));
    addContent(m_bodyLabel);
}

void PDFAppMcpConfirmDialog::build()
{
    using Helper = PDFAppMcpConfirmDialogHelper;

    setHeadWidget(createHead());

    switch (m_confirmation->kind)
    {
        case PDFAppMcpConfirmationKind::ReadDocuments:
            addReadBody();
            break;
        case PDFAppMcpConfirmationKind::Change:
            addChangeBody();
            break;
        case PDFAppMcpConfirmationKind::Save:
        case PDFAppMcpConfirmationKind::SaveCopy:
            addSaveBody();
            break;
        case PDFAppMcpConfirmationKind::Open:
            addOpenBody();
            break;
        case PDFAppMcpConfirmationKind::Close:
            addCloseBody();
            break;
    }

    // The reason row (955:143759: icon/info 16, Regular 12 text/secondary - the
    // document dialogs; the majority of the frames, Figma map 5.6)
    PDFLabel* reasonLabel = nullptr;
    QWidget* reasonRow = createInfoRow(QStringLiteral("info"), &reasonLabel);
    reasonRow->setObjectName(QStringLiteral("reason"));
    m_reasonLabel = reasonLabel;
    addContent(reasonRow);

    // The countdown at the leading side of the buttons (955:143764)
    QWidget* countdown = new QWidget(this);
    countdown->setObjectName(QStringLiteral("countdown"));
    QHBoxLayout* countdownLayout = new QHBoxLayout(countdown);
    countdownLayout->setContentsMargins(0, 0, 0, 0);
    countdownLayout->setSpacing(6);
    countdownLayout->addWidget(Helper::createIcon(QStringLiteral("hourglass"), 14, PDFThemeColors::IconMuted, countdown), 0, Qt::AlignVCenter);
    m_countdownLabel = Helper::createLabel(PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, countdown, false);
    countdownLayout->addWidget(m_countdownLabel, 1);
    addActionWidget(countdown);

    // Buttons (958:28030): Deny + the verb; safe = Primary (Allow, Open, Save
    // Copy), the save over the file is Secondary with Deny Primary (940:134248)
    const bool isSave = m_confirmation->kind == PDFAppMcpConfirmationKind::Save;
    if (isSave)
    {
        m_allowButton = addActionButton(QString(), ActionPosition::Trailing, false);
        m_denyButton = addActionButton(QString(), ActionPosition::Trailing, true);
    }
    else
    {
        m_denyButton = addActionButton(QString(), ActionPosition::Trailing, false);
        m_allowButton = addActionButton(QString(), ActionPosition::Trailing, true);
    }
    m_allowButton->setObjectName(QStringLiteral("allowButton"));
    m_denyButton->setObjectName(QStringLiteral("denyButton"));
    connect(m_allowButton, &QPushButton::clicked, this, &PDFAppMcpConfirmDialog::onAllowClicked);
    connect(m_denyButton, &QPushButton::clicked, this, &PDFAppMcpConfirmDialog::onDenyClicked);
}

void PDFAppMcpConfirmDialog::updateTexts()
{
    const PDFAppMcpConfirmation& confirmation = *m_confirmation;
    const QString client = sanitize(confirmation.clientName.isEmpty() ? tr("The assistant") : confirmation.clientName, 64);
    const QString document = confirmation.document ? sanitize(confirmation.document->getTitle()) : QString();
    const QString title = createTitle(confirmation);

    setTitle(title);
    if (m_titleLabel)
    {
        m_titleLabel->setText(title);
    }

    const QString time = PDFAppMcpConfirmDialogHelper::formatTime(confirmation.askedAt > 0 ? confirmation.askedAt : QDateTime::currentMSecsSinceEpoch());
    if (m_remoteTag)
    {
        m_remoteTag->setText(tr("Remote"));
    }
    if (m_originLabel)
    {
        m_originLabel->setText(confirmation.isRemote ? tr("%1 · asked at %2").arg(sanitize(confirmation.remoteAddress, 64), time)
                                                     : tr("This computer · asked at %1").arg(time));
    }

    if (m_reasonLabel)
    {
        const QString reason = createReason(confirmation);
        m_reasonLabel->setText(reason);
        if (QWidget* row = m_reasonLabel->parentWidget())
        {
            row->setVisible(!reason.isEmpty());
        }
    }

    if (m_scopeLabel)
    {
        m_scopeLabel->setText(tr("Applies until %1 disconnects").arg(client));
    }

    if (m_showOnPageLink)
    {
        m_showOnPageLink->setText(tr("Show on Page"));
    }

    const int changeCount = qMax(1, confirmation.details.value(QStringLiteral("changeCount")).toInt(1));
    const int signatureCount = qMax(0, confirmation.details.value(QStringLiteral("signatureCount")).toInt());
    const bool isAnnotationChange = confirmation.details.value(QStringLiteral("isAnnotationChange")).toBool();
    for (const QPointer<PDFLabel>& label : m_noteLabels)
    {
        if (!label)
        {
            continue;
        }

        const QString kind = label->property("noteKind").toString();
        if (kind == QLatin1String("undo"))
        {
            label->setText(changeCount == 1 ? tr("Undo removes the change in one step. Nothing is saved to the file.")
                                            : tr("Undo removes all %Ln changes in one step. Nothing is saved to the file.", nullptr, changeCount));
        }
        else if (kind == QLatin1String("overwrite"))
        {
            label->setText(tr("The previous content of the file is replaced. It cannot be restored from the disk."));
        }
        else if (kind == QLatin1String("signatureTitle"))
        {
            if (signatureCount == 1)
            {
                label->setText(isAnnotationChange ? tr("Adding annotations may invalidate the signature") : tr("This change may invalidate the signature"));
            }
            else
            {
                label->setText(isAnnotationChange ? tr("Adding annotations may invalidate %Ln signatures", nullptr, signatureCount)
                                                  : tr("This change may invalidate %Ln signatures", nullptr, signatureCount));
            }
        }
        else if (kind == QLatin1String("signatureText"))
        {
            label->setText(tr("%1 is signed. A signer may have allowed no changes after signing – the signatures panel then reports them as invalid.").arg(document));
        }
    }

    if (m_bodyLabel)
    {
        const QString kind = m_bodyLabel->property("noteKind").toString();
        if (kind == QLatin1String("readMeans"))
        {
            m_bodyLabel->setText(tr("%1 can read the text, properties, outline, annotations and page images of your open documents, switch between them and close them. "
                                    "Nothing in the documents is changed.").arg(client));
        }
        else if (kind == QLatin1String("openBody"))
        {
            m_bodyLabel->setText(tr("The document opens in a new tab. %1 can then work with it within the permissions you allow.").arg(client));
        }
        else if (kind == QLatin1String("closeBody"))
        {
            m_bodyLabel->setText(tr("The tab of %1 closes. The document has no unsaved changes.").arg(document));
        }
    }

    if (m_denyButton)
    {
        m_denyButton->setText(tr("Deny"));
    }
    if (m_allowButton)
    {
        switch (confirmation.kind)
        {
            case PDFAppMcpConfirmationKind::ReadDocuments:
            case PDFAppMcpConfirmationKind::Change:
                m_allowButton->setText(tr("Allow"));
                break;
            case PDFAppMcpConfirmationKind::Save:
                m_allowButton->setText(tr("Save"));
                break;
            case PDFAppMcpConfirmationKind::SaveCopy:
                m_allowButton->setText(tr("Save Copy"));
                break;
            case PDFAppMcpConfirmationKind::Open:
                m_allowButton->setText(tr("Open"));
                break;
            case PDFAppMcpConfirmationKind::Close:
                m_allowButton->setText(tr("Close"));
                break;
        }
    }

    setQueuePosition(m_queueIndex, m_queueCount);
    setRemainingTime(m_remainingTime);
}

void PDFAppMcpConfirmDialog::setQueuePosition(int index, int count)
{
    m_queueIndex = qMax(0, index);
    m_queueCount = qMax(1, count);
    if (m_queueTag)
    {
        m_queueTag->setText(getQueueText());
        m_queueTag->setVisible(m_queueCount > 1);
    }
}

QString PDFAppMcpConfirmDialog::getQueueText() const
{
    return m_queueCount > 1 ? tr("%1 of %2").arg(m_queueIndex + 1).arg(m_queueCount) : QString();
}

void PDFAppMcpConfirmDialog::setRemainingTime(qint64 milliseconds)
{
    m_remainingTime = qMax<qint64>(0, milliseconds);
    if (m_countdownLabel)
    {
        m_countdownLabel->setText(getCountdownText());
    }
}

QString PDFAppMcpConfirmDialog::getCountdownText() const
{
    return tr("Denied automatically in %1").arg(formatRemainingTime(m_remainingTime));
}

QString PDFAppMcpConfirmDialog::getTitleText() const
{
    return m_titleLabel ? m_titleLabel->text() : QString();
}

QString PDFAppMcpConfirmDialog::getOriginText() const
{
    QString text = m_originLabel ? m_originLabel->text() : QString();
    if (m_remoteTag)
    {
        text = m_remoteTag->text() + QStringLiteral(" · ") + text;
    }
    return text;
}

QString PDFAppMcpConfirmDialog::getReasonText() const
{
    return m_reasonLabel ? m_reasonLabel->text() : QString();
}

QStringList PDFAppMcpConfirmDialog::getNoteTexts() const
{
    QStringList texts;
    for (const QPointer<PDFLabel>& label : m_noteLabels)
    {
        if (label)
        {
            texts << label->text();
        }
    }
    if (m_scopeLabel)
    {
        texts << m_scopeLabel->text();
    }
    if (m_bodyLabel)
    {
        texts << m_bodyLabel->text();
    }
    return texts;
}

QStringList PDFAppMcpConfirmDialog::getRowTexts() const
{
    QStringList texts;
    for (const QPointer<PDFLabel>& label : m_rowLabels)
    {
        if (label)
        {
            texts << label->text();
        }
    }
    return texts;
}

void PDFAppMcpConfirmDialog::reject()
{
    // Escape (and closing) is Deny; the coordinator removes the dialog
    Q_EMIT denied(getConfirmationId());
}

void PDFAppMcpConfirmDialog::onAllowClicked()
{
    if (isInputBlocked())
    {
        return;
    }
    Q_EMIT allowed(getConfirmationId());
}

void PDFAppMcpConfirmDialog::onDenyClicked()
{
    if (isInputBlocked())
    {
        return;
    }
    Q_EMIT denied(getConfirmationId());
}

void PDFAppMcpConfirmDialog::onShowOnPageClicked()
{
    Q_EMIT showOnPageRequested(getConfirmationId());
}

bool PDFAppMcpConfirmDialog::focusNextPrevChild(bool next)
{
    // The focus stays inside of the dialog (modal for its tab): Tab cycles through
    // its own widgets and never reaches the tab below the scrim
    constexpr int MAXIMUM_STEPS = 10000;
    QList<QWidget*> chain;
    QWidget* widget = nextInFocusChain();
    for (int step = 0; widget && widget != this && step < MAXIMUM_STEPS; ++step, widget = widget->nextInFocusChain())
    {
        if (isAncestorOf(widget) && (widget->focusPolicy() & Qt::TabFocus) && widget->isEnabled() && widget->isVisibleTo(this))
        {
            chain.append(widget);
        }
    }

    if (chain.isEmpty())
    {
        setFocus(Qt::OtherFocusReason);
        return true;
    }

    QWidget* current = focusWidget();
    qsizetype index = chain.indexOf(current);
    if (index < 0)
    {
        index = next ? -1 : chain.size();
    }
    index = next ? (index + 1) % chain.size() : (index - 1 + chain.size()) % chain.size();
    chain.at(index)->setFocus(next ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    return true;
}

void PDFAppMcpConfirmDialog::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

}   // namespace pdf
