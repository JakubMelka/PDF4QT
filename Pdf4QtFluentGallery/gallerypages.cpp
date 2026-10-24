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

#include "gallerypages.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdftooltip.h"
#include "pdfwidgetstyleproperties.h"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDial>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRubberBand>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QStyleOptionHeader>
#include <QStyleOptionMenuItem>
#include <QStyleOptionProgressBar>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QStyleOptionTab>
#include <QStyleOptionToolButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolBox>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <array>
#include <utility>

// Widgets of Qt, which are shown only on the page of the leaks of the palette
#include <QCalendarWidget>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextEdit>

namespace pdffluentgallery
{

using Color = pdf::PDFThemeColors::Color;
using Font = pdf::PDFThemeFonts::Role;
using Painter = pdf::PDFStylePainter;
using Properties = pdf::PDFWidgetStyleProperties;
using StateFlags = GalleryPages::StateFlags;

GallerySpecimen::GallerySpecimen(QString name, QString figmaNode, QSize size, PaintFunction paintFunction, QWidget* parent) :
    QWidget(parent),
    m_name(std::move(name)),
    m_figmaNode(std::move(figmaNode)),
    m_size(size),
    m_paintFunction(std::move(paintFunction))
{
    setFixedSize(size + QSize(2 * MARGIN, 2 * MARGIN));
    setAccessibleName(m_name);
}

void GallerySpecimen::paintSpecimen(QPainter& painter) const
{
    if (m_paintFunction)
    {
        const pdf::PDFStylePainterGuard guard(&painter);
        m_paintFunction(painter, QRect(QPoint(MARGIN, MARGIN), m_size));
    }
}

void GallerySpecimen::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    paintSpecimen(painter);
}

GalleryBuilder::GalleryBuilder(QWidget* parent, QString id, QString title, QString iconName) :
    m_id(std::move(id)),
    m_title(std::move(title)),
    m_iconName(std::move(iconName)),
    m_page(new QWidget(parent)),
    m_pageLayout(new QVBoxLayout(m_page))
{
    m_pageLayout->setContentsMargins(24, 24, 24, 24);
    m_pageLayout->setSpacing(12);
    m_page->setObjectName(m_id);
    m_page->setProperty(Properties::CANVAS, true);

    pdf::PDFLabel* titleLabel = new pdf::PDFLabel(m_title, Font::DialogTitle, Color::TextPrimary, m_page);
    m_pageLayout->addWidget(titleLabel);
}

QStyle* GalleryBuilder::style() const
{
    return QApplication::style();
}

void GalleryBuilder::block(const QString& title, const QString& figmaNode)
{
    m_blockNode = figmaNode;
    m_pageLayout->addSpacing(8);
    m_pageLayout->addWidget(new pdf::PDFLabel(title, Font::BodySemiBold, Color::TextPrimary, m_page));
    row();
}

void GalleryBuilder::row()
{
    m_rowLayout = new QHBoxLayout();
    m_rowLayout->setContentsMargins(0, 0, 0, 0);
    m_rowLayout->setSpacing(20);
    m_rowLayout->addStretch(1);
    m_pageLayout->addLayout(m_rowLayout);
    m_countInRow = 0;
}

GallerySpecimen* GalleryBuilder::specimen(const QString& caption, QSize size, GallerySpecimen::PaintFunction paintFunction, const QString& figmaNode)
{
    GallerySpecimen* widget = new GallerySpecimen(caption, figmaNode.isEmpty() ? m_blockNode : figmaNode, size, std::move(paintFunction), m_page);
    addItem(widget, caption, 6 - GallerySpecimen::MARGIN);
    return widget;
}

void GalleryBuilder::wide(QWidget* widget, const QString& caption)
{
    if (!caption.isEmpty())
    {
        widget->setProperty("galleryName", caption);
        widget->setProperty("galleryNode", m_blockNode);
    }
    m_pageLayout->addWidget(widget);
}

void GalleryBuilder::note(const QString& text)
{
    pdf::PDFLabel* label = new pdf::PDFLabel(text, Font::Body, Color::TextSecondary, m_page);
    label->setWordWrap(true);
    m_pageLayout->addWidget(label);
}

GalleryPages::Page GalleryBuilder::finish()
{
    m_pageLayout->addStretch(1);

    GalleryPages::Page page;
    page.id = m_id;
    page.title = m_title;
    page.iconName = m_iconName;
    page.widget = m_page;
    return page;
}

void GalleryLifetimeHints::set(QWidget* widget, GalleryLifetimeHints hints)
{
    if (widget)
    {
        widget->setProperty("galleryLifetimeHints", QVariant::fromValue(std::move(hints)));
    }
}

GalleryLifetimeHints GalleryLifetimeHints::get(const QWidget* widget)
{
    const QVariant value = widget ? widget->property("galleryLifetimeHints") : QVariant();
    return value.canConvert<GalleryLifetimeHints>() ? value.value<GalleryLifetimeHints>() : GalleryLifetimeHints();
}

void GalleryLifetimeHints::giveMenus(QWidget* widget)
{
    if (!widget)
    {
        return;
    }

    QList<pdf::PDFMoreButton*> buttons = widget->findChildren<pdf::PDFMoreButton*>();
    if (pdf::PDFMoreButton* button = qobject_cast<pdf::PDFMoreButton*>(widget))
    {
        buttons.prepend(button);
    }

    int index = 0;
    for (pdf::PDFMoreButton* button : buttons)
    {
        if (!button->getMenu())
        {
            const bool isChildOfButton = (index++ % 2) == 1 || !button->parentWidget();
            QMenu* menu = new QMenu(isChildOfButton ? static_cast<QWidget*>(button) : button->parentWidget());
            menu->addAction(QStringLiteral("Open"));
            menu->addAction(QStringLiteral("Remove"));
            button->setMenu(menu);
        }
    }
}

void GalleryBuilder::addLiveDefaults(QWidget* widget)
{
    // A sample with its own hints keeps them
    if (widget->property("galleryLifetimeHints").isValid())
    {
        return;
    }

    GalleryLifetimeHints hints;
    hints.prepare = &GalleryLifetimeHints::giveMenus;
    GalleryLifetimeHints::set(widget, std::move(hints));
}

void GalleryBuilder::addItem(QWidget* widget, const QString& caption, int gap)
{
    if (m_countInRow >= 9)
    {
        row();
    }

    QVBoxLayout* itemLayout = new QVBoxLayout();
    itemLayout->setContentsMargins(0, 0, 0, 0);
    itemLayout->setSpacing(gap);
    itemLayout->addWidget(widget, 0, Qt::AlignLeft | Qt::AlignBottom);
    itemLayout->addWidget(new pdf::PDFLabel(caption, Font::Caption, Color::TextTertiary, m_page), 0, Qt::AlignLeft);
    m_rowLayout->insertLayout(m_rowLayout->count() - 1, itemLayout);
    ++m_countInRow;
}

/// Helper functions of the pages
class GalleryHelper
{
public:
    GalleryHelper() = delete;

    struct NamedState
    {
        const char* name;
        int flags;
    };

    static constexpr NamedState POINTER_STATES[] =
    {
        { "Default", GalleryPages::Default },
        { "Hover", GalleryPages::Hover },
        { "Pressed", GalleryPages::Pressed | GalleryPages::Hover },
        { "Focus", GalleryPages::Focus },
        { "Disabled", GalleryPages::Disabled }
    };

    static QIcon icon(const char* name)
    {
        return pdf::PDFThemeIcons::getIcon(QString::fromLatin1(name));
    }

    static void initOption(QStyleOption& option, const QRect& rect, StateFlags flags)
    {
        option.rect = rect;
        option.state = GalleryPages::toStyleState(flags);
        option.direction = QApplication::layoutDirection();
        option.fontMetrics = QFontMetrics(QApplication::font());
    }

    /// Draws a sample text of a field
    static void fieldText(QPainter& painter, const QRect& rect, const QString& text, Color color)
    {
        Painter::text(&painter, QRectF(rect.adjusted(13, 0, -13, 0)), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, text,
                      pdf::PDFThemeFonts::getFont(Font::Body), Painter::color(color));
    }

    static void fieldText(QPainter& painter, const QRect& rect, const QString& text, bool isMuted)
    {
        fieldText(painter, rect, text, isMuted ? Color::TextMuted : Color::TextPrimary);
    }

    static QStandardItemModel* createFilesModel(QObject* parent, bool hasCheckBoxes)
    {
        QStandardItemModel* model = new QStandardItemModel(parent);
        model->setHorizontalHeaderLabels({ QStringLiteral("Name"), QStringLiteral("Pages"), QStringLiteral("Size"), QStringLiteral("Modified") });

        // Columns aligned to the right have their header sections aligned in the same way
        for (int column = 1; column < 4; ++column)
        {
            model->horizontalHeaderItem(column)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }

        struct Row
        {
            const char* name;
            const char* icon;
            int pages;
            const char* size;
            const char* modified;
        };

        static constexpr Row rows[] =
        {
            { "Contract.pdf", "file-text", 6, "1.2 MB", "Today 10:42" },
            { "Annex_A.pdf", "file-text", 9, "3.8 MB", "Yesterday" },
            { "Tender_2026.pdf", "file-text", 17, "11 MB", "Sep 30" },
            { "Invoice.pdf", "file-text", 1, "84 kB", "Sep 14" },
            { "Scans", "folder", 240, "96 MB", "Aug 2" },
            { "Archive.pdf", "file", 3, "410 kB", "Jul 19" }
        };

        int index = 0;
        for (const Row& row : rows)
        {
            QStandardItem* name = new QStandardItem(icon(row.icon), QString::fromLatin1(row.name));
            name->setToolTip(QStringLiteral("%1\n%2 pages, %3").arg(QString::fromLatin1(row.name)).arg(row.pages).arg(QString::fromLatin1(row.size)));
            if (hasCheckBoxes)
            {
                name->setCheckable(true);
                name->setCheckState(index % 3 == 0 ? Qt::Checked : (index % 3 == 1 ? Qt::Unchecked : Qt::PartiallyChecked));
            }

            QStandardItem* pages = new QStandardItem();
            pages->setData(row.pages, Qt::DisplayRole);
            pages->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            QStandardItem* size = new QStandardItem(QString::fromLatin1(row.size));
            size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            QStandardItem* modified = new QStandardItem(QString::fromLatin1(row.modified));
            modified->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

            if (index == 5)
            {
                for (QStandardItem* item : { name, pages, size, modified })
                {
                    item->setEnabled(false);
                }
            }

            model->appendRow({ name, pages, size, modified });
            ++index;
        }

        return model;
    }

    static QStandardItemModel* createTreeModel(QObject* parent)
    {
        QStandardItemModel* model = new QStandardItemModel(parent);

        QStandardItem* layers = new QStandardItem(icon("layers"), QStringLiteral("Layers"));
        layers->setCheckable(true);
        layers->setCheckState(Qt::PartiallyChecked);

        for (const char* name : { "Text", "Images", "Annotations" })
        {
            QStandardItem* child = new QStandardItem(icon("file"), QString::fromLatin1(name));
            child->setCheckable(true);
            child->setCheckState(qstrcmp(name, "Images") == 0 ? Qt::Unchecked : Qt::Checked);
            layers->appendRow(child);

            if (qstrcmp(name, "Annotations") == 0)
            {
                child->setCheckState(Qt::PartiallyChecked);
                for (const char* leafName : { "Highlights", "Notes" })
                {
                    QStandardItem* leaf = new QStandardItem(icon("message-square"), QString::fromLatin1(leafName));
                    leaf->setCheckable(true);
                    leaf->setCheckState(qstrcmp(leafName, "Notes") == 0 ? Qt::Unchecked : Qt::Checked);
                    child->appendRow(leaf);
                }
            }
        }

        QStandardItem* bookmarks = new QStandardItem(icon("bookmark"), QStringLiteral("Bookmarks"));
        bookmarks->appendRow(new QStandardItem(icon("bookmark"), QStringLiteral("Chapter 1")));
        bookmarks->appendRow(new QStandardItem(icon("bookmark"), QStringLiteral("Chapter 2")));

        model->appendRow(layers);
        model->appendRow(bookmarks);
        return model;
    }

    static QMenu* createSampleMenu(QWidget* parent)
    {
        QMenu* menu = new QMenu(parent);
        menu->addSection(QStringLiteral("Document"));
        menu->addAction(icon("save"), QStringLiteral("&Save"))->setShortcut(QKeySequence::Save);
        menu->addAction(icon("save-as"), QStringLiteral("Save &As…"))->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
        menu->addAction(icon("printer"), QStringLiteral("&Print…"))->setShortcut(QKeySequence::Print);

        QMenu* exportMenu = menu->addMenu(QStringLiteral("&Export"));
        exportMenu->addAction(icon("image"), QStringLiteral("Pages as images…"));
        exportMenu->addAction(icon("file-text"), QStringLiteral("Text…"));
        exportMenu->addAction(icon("paperclip"), QStringLiteral("Attachments…"));
        exportMenu->addSeparator();
        exportMenu->addAction(icon("copy"), QStringLiteral("Copy page as image"));

        menu->addSeparator();

        QAction* sidebar = menu->addAction(icon("panel-left"), QStringLiteral("Show s&idebar"));
        sidebar->setCheckable(true);
        sidebar->setChecked(true);
        sidebar->setShortcut(QKeySequence(Qt::Key_F9));

        QActionGroup* group = new QActionGroup(menu);
        for (const char* name : { "Single page", "Continuous" })
        {
            QAction* action = menu->addAction(QString::fromLatin1(name));
            action->setCheckable(true);
            action->setChecked(qstrcmp(name, "Continuous") == 0);
            group->addAction(action);
        }

        menu->addSeparator();
        menu->addAction(icon("lock"), QStringLiteral("Encrypt…"))->setEnabled(false);
        menu->addSeparator();

        QAction* removeAction = menu->addAction(icon("trash"), QStringLiteral("Remove from list"));
        removeAction->setProperty(Properties::DESTRUCTIVE, true);
        return menu;
    }
};

QStyle::State GalleryPages::toStyleState(StateFlags flags)
{
    QStyle::State state = QStyle::State_Enabled | QStyle::State_Active;

    if (flags.testFlag(Disabled))
    {
        state &= ~QStyle::State_Enabled;
    }
    if (flags.testFlag(Inactive))
    {
        state &= ~QStyle::State_Active;
    }
    if (flags.testFlag(Hover))
    {
        state |= QStyle::State_MouseOver;
    }
    if (flags.testFlag(Pressed))
    {
        state |= QStyle::State_Sunken;
    }
    if (flags.testFlag(Focus))
    {
        state |= QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;
    }
    if (flags.testFlag(Selected))
    {
        state |= QStyle::State_Selected;
    }

    if (flags.testFlag(Partial))
    {
        state |= QStyle::State_NoChange;
    }
    else
    {
        state |= flags.testFlag(On) ? QStyle::State_On : QStyle::State_Off;
    }

    return state;
}

std::vector<GalleryPages::Page> GalleryPages::createPages(QWidget* parent)
{
    std::vector<Page> pages;
    const int count = getPageCount();
    pages.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        pages.push_back(createPage(i, parent));
    }
    return pages;
}

int GalleryPages::getPageCount()
{
    return static_cast<int>(getPageEntries().size());
}

QString GalleryPages::getPageId(int index)
{
    const std::vector<PageEntry>& entries = getPageEntries();
    if (index < 0 || index >= static_cast<int>(entries.size()))
    {
        return QString();
    }
    return QString::fromLatin1(entries[static_cast<size_t>(index)].id);
}

GalleryPages::Page GalleryPages::createPage(int index, QWidget* parent)
{
    const std::vector<PageEntry>& entries = getPageEntries();
    if (index < 0 || index >= static_cast<int>(entries.size()))
    {
        return Page();
    }
    return entries[static_cast<size_t>(index)].function(parent);
}

const std::vector<GalleryPages::PageEntry>& GalleryPages::getPageEntries()
{
    // The identifiers are known without creating the pages (data rows of the
    // tests); each one is the identifier, which its function gives to the page
    static const std::vector<PageEntry> entries =
    {
        { "00-overview", &GalleryPages::createOverviewPage },
        { "01-push-buttons", &GalleryPages::createButtonsPage },
        { "02-tool-buttons-toolbars", &GalleryPages::createToolButtonsPage },
        { "03-checkbox-radio", &GalleryPages::createCheckBoxesPage },
        { "04-text-input", &GalleryPages::createTextInputPage },
        { "05-spin-box-date-edit", &GalleryPages::createSpinBoxesPage },
        { "06-combo-box", &GalleryPages::createComboBoxesPage },
        { "07-slider-dial", &GalleryPages::createSlidersPage },
        { "08-progress-bar", &GalleryPages::createProgressBarsPage },
        { "09-scroll-bars", &GalleryPages::createScrollBarsPage },
        { "10-menus-menu-bar", &GalleryPages::createMenusPage },
        { "11-tabs", &GalleryPages::createTabsPage },
        { "12-item-views", &GalleryPages::createItemViewsPage },
        { "13-header-view", &GalleryPages::createHeadersPage },
        { "14-group-box-frames", &GalleryPages::createGroupBoxesPage },
        { "15-containers", &GalleryPages::createContainersPage },
        { "16-tooltips-dialogs", &GalleryPages::createDialogsPage },
        { "17-icons-metrics-hints", &GalleryPages::createIconsPage },
        { "18-components", &GalleryPages::createComponentsPage },
        { "19-shell-home", &GalleryPages::createShellHomePage },
        { "20-catalog", &GalleryPages::createCatalogPage },
        { "21-forms", &GalleryPages::createFormsPage },
        { "22-document", &GalleryPages::createDocumentPage },
        { "23-panels", &GalleryPages::createPanelsPage }
    };
    return entries;
}

GalleryPages::Page GalleryPages::createOverviewPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("00-overview"), QStringLiteral("Overview"), QStringLiteral("layout"));

    builder.block(QStringLiteral("Colors of the active theme (variables of the collection Color)"), QStringLiteral("605:1101"));
    for (int i = 0; i < pdf::PDFThemeColors::ColorCount; ++i)
    {
        const Color color = static_cast<Color>(i);
        const QString name = QString::fromLatin1(pdf::PDFThemeColors::getTokenName(color));

        builder.specimen(name, QSize(132, 28), [color](QPainter& painter, const QRect& rect)
        {
            // The color lies on the surface, so translucent colors are seen as they are used
            const QRectF swatch(rect.left(), rect.top(), 28.0, 28.0);
            Painter::fill(&painter, swatch, 6.0, Color::BgSurface);
            Painter::fill(&painter, swatch, 6.0, color);
            Painter::strokeInside(&painter, swatch, 6.0, Color::BorderDefault, 1.0);

            const QColor value = Painter::color(color);
            const QString text = value.alpha() < 255 ? QStringLiteral("%1 %2 %").arg(value.name(QColor::HexRgb)).arg(qRound(value.alphaF() * 100.0)) : value.name(QColor::HexRgb);
            Painter::text(&painter, QRectF(rect.adjusted(36, 0, 0, 0)), Qt::AlignVCenter | Qt::AlignLeft, text, pdf::PDFThemeFonts::getFont(Font::Small), Painter::color(Color::TextMuted));
        });
    }

    builder.block(QStringLiteral("Typography"), QStringLiteral("605:1178"));
    struct FontSample
    {
        const char* name;
        Font role;
    };
    static constexpr FontSample fontSamples[] =
    {
        { "Body - Regular 13", Font::Body }, { "Body Medium - Medium 13", Font::BodyMedium }, { "Body Semi Bold - Semi Bold 13", Font::BodySemiBold },
        { "Small - Regular 12", Font::Small }, { "Small Medium - Medium 12", Font::SmallMedium }, { "Small Semi Bold - Semi Bold 12", Font::SmallSemiBold },
        { "Caption - Medium 11", Font::Caption }, { "Caption Regular - Regular 11", Font::CaptionRegular }, { "Caption Semi Bold - Semi Bold 11", Font::CaptionSemiBold },
        { "Tiny - Regular 10", Font::Tiny }, { "Title - Bold 15", Font::Title }, { "Dialog title - Semi Bold 16", Font::DialogTitle },
        { "Tiny Semi Bold - Semi Bold 10", Font::TinySemiBold }, { "Body Bold - Bold 13", Font::BodyBold }, { "Tile title - Bold 14", Font::TileTitle },
        { "Display - Bold 22", Font::Display }
    };
    int fontIndex = 0;
    for (const FontSample& sample : fontSamples)
    {
        if (fontIndex++ % 3 == 0 && fontIndex > 1)
        {
            builder.row();
        }
        builder.live(QString::fromLatin1(sample.name), new pdf::PDFLabel(QStringLiteral("The quick brown fox 0123456789"), sample.role, Color::TextPrimary, builder.page()));
    }

    builder.block(QStringLiteral("Text colors"), QStringLiteral("605:1178"));
    for (const auto& item : { std::pair<const char*, Color>{ "text/primary", Color::TextPrimary }, { "text/secondary", Color::TextSecondary }, { "text/tertiary", Color::TextTertiary },
                              { "text/muted", Color::TextMuted }, { "text/accent", Color::TextAccent }, { "text/danger", Color::TextDanger } })
    {
        builder.live(QString::fromLatin1(item.first), new pdf::PDFLabel(QStringLiteral("Project_Report.pdf"), Font::Body, item.second, builder.page()));
    }

    builder.block(QStringLiteral("Focus ring - walk these controls by the Tab key"), QStringLiteral("636:1621"));
    QPushButton* focusButton = builder.live(QStringLiteral("Button - outside, r 8 -> 10"), new QPushButton(GalleryHelper::icon("folder-open"), QStringLiteral("Browse Files…"), builder.page()));
    focusButton->setAutoDefault(false);
    QToolButton* focusToolButton = builder.live(QStringLiteral("Tool button - outside, r 10 -> 12"), new QToolButton(builder.page()));
    focusToolButton->setIcon(GalleryHelper::icon("save"));
    focusToolButton->setIconSize(QSize(20, 20));
    focusToolButton->setAutoRaise(true);
    builder.live(QStringLiteral("Check box off - inside"), new QCheckBox(QStringLiteral("Enable"), builder.page()));
    QCheckBox* focusCheckBox = builder.live(QStringLiteral("Check box on - outside"), new QCheckBox(QStringLiteral("Enable"), builder.page()));
    focusCheckBox->setChecked(true);
    builder.live(QStringLiteral("Radio button"), new QRadioButton(QStringLiteral("Option"), builder.page()));
    builder.live(QStringLiteral("Text field - inside"), new pdf::PDFLineEdit(QStringLiteral("Text"), builder.page()))->setFixedWidth(160);
    QSlider* focusSlider = builder.live(QStringLiteral("Slider - around the handle"), new QSlider(Qt::Horizontal, builder.page()));
    focusSlider->setFixedWidth(140);
    focusSlider->setValue(50);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createButtonsPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("01-push-buttons"), QStringLiteral("Push buttons"), QStringLiteral("square-round"));
    QStyle* style = builder.style();

    // Prototype widgets carry the properties, by which the style selects a variant
    QPushButton* plainPrototype = new QPushButton(builder.page());
    plainPrototype->hide();
    QPushButton* primaryPrototype = new QPushButton(builder.page());
    primaryPrototype->setProperty(Properties::PRIMARY, true);
    primaryPrototype->hide();

    auto buttonSpecimen = [&builder, style](const QString& caption, const QString& node, const QString& text, const char* iconName, StateFlags flags,
                                             QStyleOptionButton::ButtonFeatures features, QPushButton* prototype)
    {
        QStyleOptionButton option;
        GalleryHelper::initOption(option, QRect(), flags);
        option.text = text;
        option.features = features;
        if (iconName)
        {
            option.icon = GalleryHelper::icon(iconName);
            option.iconSize = QSize(16, 16);
        }

        const QSize size = style->sizeFromContents(QStyle::CT_PushButton, &option, QSize(), prototype);
        const bool hasFocus = flags.testFlag(GalleryPages::Focus);

        builder.specimen(caption, size, [option, prototype = QPointer<QWidget>(prototype), hasFocus](QPainter& painter, const QRect& rect) mutable
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            option.rect = rect;
            style->drawControl(QStyle::CE_PushButton, &option, &painter, prototype);

            if (hasFocus)
            {
                // The ring, which the focus frame of the style draws around a button
                const bool isPrimary = option.features.testFlag(QStyleOptionButton::DefaultButton) || pdf::PDFWidgetStyleProperties::isSet(prototype, Properties::PRIMARY);
                Painter::focusRing(&painter, QRectF(rect), 8.0, isPrimary);
            }
        }, node);
    };

    builder.block(QStringLiteral("Existing - Components / Button: Primary"), QStringLiteral("606:1052"));
    const char* primaryNodes[] = { "606:1060", "606:1067", "606:1074", "606:1081", "606:1088" };
    int index = 0;
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        buttonSpecimen(QStringLiteral("Primary / %1").arg(QString::fromLatin1(state.name)), QString::fromLatin1(primaryNodes[index++]), QStringLiteral("Browse Files…"), "folder-open",
                       StateFlags(state.flags), QStyleOptionButton::None, primaryPrototype);
    }

    builder.block(QStringLiteral("Existing - Components / Button: Secondary"), QStringLiteral("606:1052"));
    const char* secondaryNodes[] = { "606:1095", "606:1102", "606:1109", "606:1116", "606:1123" };
    index = 0;
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        buttonSpecimen(QStringLiteral("Secondary / %1").arg(QString::fromLatin1(state.name)), QString::fromLatin1(secondaryNodes[index++]), QStringLiteral("Browse Files…"), "folder-open",
                       StateFlags(state.flags), QStyleOptionButton::None, plainPrototype);
    }

    builder.block(QStringLiteral("New - checkable button (State_On)"), QStringLiteral("606:1126"));
    const char* checkedNodes[] = { "606:1134", "606:1141", "606:1148", "606:1155", "606:1162" };
    index = 0;
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        buttonSpecimen(QStringLiteral("Checked / %1").arg(QString::fromLatin1(state.name)), QString::fromLatin1(checkedNodes[index++]), QStringLiteral("Thumbnails"), "folder-open",
                       StateFlags(state.flags) | On, QStyleOptionButton::None, plainPrototype);
    }

    builder.block(QStringLiteral("New - flat button"), QStringLiteral("606:1165"));
    const char* flatNodes[] = { "606:1173", "606:1180", "606:1187", "606:1194", "606:1201" };
    index = 0;
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        buttonSpecimen(QStringLiteral("Flat / %1").arg(QString::fromLatin1(state.name)), QString::fromLatin1(flatNodes[index++]), QStringLiteral("Restore defaults"), "rotate-ccw",
                       StateFlags(state.flags), QStyleOptionButton::Flat, plainPrototype);
    }

    builder.block(QStringLiteral("New - menu indicator, icon-only and text-only buttons"), QStringLiteral("606:1204"));
    buttonSpecimen(QStringLiteral("Secondary + menu"), QStringLiteral("606:1215"), QStringLiteral("Export"), "export", Default, QStyleOptionButton::HasMenu, plainPrototype);
    buttonSpecimen(QStringLiteral("Secondary + menu / open"), QStringLiteral("606:1225"), QStringLiteral("Export"), "export", Pressed, QStyleOptionButton::HasMenu, plainPrototype);
    buttonSpecimen(QStringLiteral("Primary + menu"), QStringLiteral("606:1232"), QStringLiteral("Save Copy"), nullptr, Default, QStyleOptionButton::HasMenu, primaryPrototype);
    buttonSpecimen(QStringLiteral("Icon-only / Default"), QStringLiteral("606:1238"), QString(), "more-horizontal", Default, QStyleOptionButton::None, plainPrototype);
    buttonSpecimen(QStringLiteral("Icon-only / Hover"), QStringLiteral("606:1244"), QString(), "more-horizontal", Hover, QStyleOptionButton::None, plainPrototype);
    buttonSpecimen(QStringLiteral("Icon-only / Pressed"), QStringLiteral("606:1250"), QString(), "more-horizontal", Pressed | Hover, QStyleOptionButton::None, plainPrototype);
    buttonSpecimen(QStringLiteral("Icon-only flat"), QStringLiteral("606:1256"), QString(), "x", Hover, QStyleOptionButton::Flat, plainPrototype);
    buttonSpecimen(QStringLiteral("Text-only Secondary"), QStringLiteral("606:1261"), QStringLiteral("Cancel"), nullptr, Default, QStyleOptionButton::None, plainPrototype);
    buttonSpecimen(QStringLiteral("Text-only Primary"), QStringLiteral("606:1266"), QStringLiteral("OK"), nullptr, Default, QStyleOptionButton::None, primaryPrototype);

    builder.block(QStringLiteral("Live buttons"), QStringLiteral("606:1052"));
    QPushButton* primary = builder.live(QStringLiteral("Primary (property)"), new QPushButton(GalleryHelper::icon("folder-open"), QStringLiteral("Browse Files…"), builder.page()));
    primary->setProperty(Properties::PRIMARY, true);
    primary->setAutoDefault(false);
    primary->setToolTip(QStringLiteral("Browse files\nOpens the dialog of the system and adds the chosen files to the list."));
    QPushButton* secondary = builder.live(QStringLiteral("Secondary"), new QPushButton(GalleryHelper::icon("folder-open"), QStringLiteral("Browse Files…"), builder.page()));
    secondary->setAutoDefault(false);
    secondary->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    secondary->setToolTip(QStringLiteral("Browse files"));
    QPushButton* checkable = builder.live(QStringLiteral("Checkable"), new QPushButton(GalleryHelper::icon("grid"), QStringLiteral("Thumbnails"), builder.page()));
    checkable->setCheckable(true);
    checkable->setChecked(true);
    checkable->setAutoDefault(false);
    QPushButton* flat = builder.live(QStringLiteral("Flat"), new QPushButton(GalleryHelper::icon("rotate-ccw"), QStringLiteral("Restore defaults"), builder.page()));
    flat->setFlat(true);
    flat->setAutoDefault(false);
    QPushButton* menuButton = builder.live(QStringLiteral("With a menu"), new QPushButton(GalleryHelper::icon("export"), QStringLiteral("Export"), builder.page()));
    menuButton->setMenu(GalleryHelper::createSampleMenu(menuButton));
    menuButton->setAutoDefault(false);
    QPushButton* iconOnly = builder.live(QStringLiteral("Icon-only"), new QPushButton(GalleryHelper::icon("more-horizontal"), QString(), builder.page()));
    iconOnly->setAutoDefault(false);
    iconOnly->setAccessibleName(QStringLiteral("More actions"));
    QPushButton* disabled = builder.live(QStringLiteral("Disabled"), new QPushButton(GalleryHelper::icon("folder-open"), QStringLiteral("Browse Files…"), builder.page()));
    disabled->setEnabled(false);
    QPushButton* mnemonic = builder.live(QStringLiteral("Mnemonic (press Alt)"), new QPushButton(QStringLiteral("&Apply"), builder.page()));
    mnemonic->setAutoDefault(false);

    builder.block(QStringLiteral("New - QDialogButtonBox layout (destructive left, Cancel before the accepting button)"), QStringLiteral("606:1274"));
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Discard | QDialogButtonBox::Cancel | QDialogButtonBox::Save, builder.page());
    buttonBox->button(QDialogButtonBox::Save)->setDefault(true);
    buttonBox->setFixedWidth(520);
    builder.live(QStringLiteral("Discard - Cancel - Save"), buttonBox, QStringLiteral("606:1290"));

    return builder.finish();
}

GalleryPages::Page GalleryPages::createToolButtonsPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("02-tool-buttons-toolbars"), QStringLiteral("Tool buttons and toolbars"), QStringLiteral("tools"));
    QStyle* style = builder.style();

    QToolBar* prototypeBar = new QToolBar(builder.page());
    prototypeBar->hide();
    QToolButton* toolbarPrototype = new QToolButton(prototypeBar);
    QToolButton* framedPrototype = new QToolButton(builder.page());
    framedPrototype->hide();
    QToolButton* sidebarPrototype = new QToolButton(builder.page());
    sidebarPrototype->setProperty(Properties::SIDEBAR_TAB, true);
    sidebarPrototype->hide();

    auto toolSpecimen = [&builder, style](const QString& caption, const QString& node, const char* iconName, const QString& text, Qt::ToolButtonStyle buttonStyle, StateFlags flags,
                                           QStyleOptionToolButton::ToolButtonFeatures features, QStyle::SubControls activeSubControls, QToolButton* prototype, int iconExtent, qreal radius)
    {
        QStyleOptionToolButton option;
        GalleryHelper::initOption(option, QRect(), flags);
        option.icon = GalleryHelper::icon(iconName);
        option.iconSize = QSize(iconExtent, iconExtent);
        option.text = text;
        option.toolButtonStyle = buttonStyle;
        option.features = features;
        option.subControls = QStyle::SC_ToolButton;
        option.activeSubControls = activeSubControls;
        option.state |= QStyle::State_AutoRaise;
        option.arrowType = Qt::NoArrow;

        if (features.testFlag(QStyleOptionToolButton::MenuButtonPopup))
        {
            option.subControls |= QStyle::SC_ToolButtonMenu;
        }

        const QSize size = style->sizeFromContents(QStyle::CT_ToolButton, &option, QSize(), prototype);
        const bool hasFocus = flags.testFlag(GalleryPages::Focus);

        builder.specimen(caption, size, [option, prototype = QPointer<QWidget>(prototype), hasFocus, radius](QPainter& painter, const QRect& rect) mutable
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            option.rect = rect;
            style->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, prototype);

            if (hasFocus)
            {
                Painter::focusRing(&painter, QRectF(rect), radius, false);
            }
        }, node);
    };

    const QStyleOptionToolButton::ToolButtonFeatures none = QStyleOptionToolButton::None;

    builder.block(QStringLiteral("Existing - Components / Toolbar button"), QStringLiteral("607:1114"));
    toolSpecimen(QStringLiteral("Default"), QStringLiteral("607:1122"), "save", QString(), Qt::ToolButtonIconOnly, Default, none, {}, toolbarPrototype, 20, 10.0);
    toolSpecimen(QStringLiteral("Hover"), QStringLiteral("607:1129"), "save", QString(), Qt::ToolButtonIconOnly, Hover, none, QStyle::SC_ToolButton, toolbarPrototype, 20, 10.0);
    toolSpecimen(QStringLiteral("Pressed"), QStringLiteral("607:1136"), "save", QString(), Qt::ToolButtonIconOnly, Pressed | Hover, none, QStyle::SC_ToolButton, toolbarPrototype, 20, 10.0);
    toolSpecimen(QStringLiteral("Focus"), QStringLiteral("607:1143"), "save", QString(), Qt::ToolButtonIconOnly, Focus, none, {}, toolbarPrototype, 20, 10.0);
    toolSpecimen(QStringLiteral("Disabled"), QStringLiteral("607:1150"), "save", QString(), Qt::ToolButtonIconOnly, Disabled, none, {}, toolbarPrototype, 20, 10.0);
    toolSpecimen(QStringLiteral("Active"), QStringLiteral("607:1157"), "highlighter", QString(), Qt::ToolButtonIconOnly, On, none, {}, toolbarPrototype, 20, 10.0);
    toolSpecimen(QStringLiteral("Active / Hover"), QStringLiteral("607:1164"), "highlighter", QString(), Qt::ToolButtonIconOnly, On | Hover, none, QStyle::SC_ToolButton, toolbarPrototype, 20, 10.0);
    toolSpecimen(QStringLiteral("Checked / Disabled"), QStringLiteral("607:1223"), "highlighter", QString(), Qt::ToolButtonIconOnly, On | Disabled, none, {}, toolbarPrototype, 20, 10.0);
    toolSpecimen(QStringLiteral("Menu indicator"), QStringLiteral("607:1173"), "rotate-cw", QString(), Qt::ToolButtonIconOnly, Default, QStyleOptionToolButton::HasMenu, {}, toolbarPrototype, 20, 10.0);

    builder.block(QStringLiteral("Existing - Toolbar menu button, Action tile, Sidebar tab"), QStringLiteral("607:1114"));
    toolSpecimen(QStringLiteral("Menu button / closed"), QStringLiteral("607:1180"), "file", QStringLiteral("File"), Qt::ToolButtonTextBesideIcon, Default, none, {}, toolbarPrototype, 18, 10.0);
    toolSpecimen(QStringLiteral("Menu button / open"), QStringLiteral("607:1187"), "file", QStringLiteral("File"), Qt::ToolButtonTextBesideIcon, On, none, {}, toolbarPrototype, 18, 10.0);
    toolSpecimen(QStringLiteral("Action tile / Default"), QStringLiteral("607:1201"), "highlighter", QStringLiteral("Highlight"), Qt::ToolButtonTextUnderIcon, Default, none, {}, toolbarPrototype, 18, 10.0);
    toolSpecimen(QStringLiteral("Action tile / Active"), QStringLiteral("607:1194"), "highlighter", QStringLiteral("Highlight"), Qt::ToolButtonTextUnderIcon, On, none, {}, toolbarPrototype, 18, 10.0);
    toolSpecimen(QStringLiteral("Sidebar tab / Default"), QStringLiteral("607:1207"), "pages", QString(), Qt::ToolButtonIconOnly, Default, none, {}, sidebarPrototype, 18, 8.0);
    toolSpecimen(QStringLiteral("Sidebar tab / Selected"), QStringLiteral("607:1207"), "pages", QString(), Qt::ToolButtonIconOnly, On, none, {}, sidebarPrototype, 18, 8.0);
    toolSpecimen(QStringLiteral("Sidebar tab / Hover"), QStringLiteral("607:1213"), "pages", QString(), Qt::ToolButtonIconOnly, Hover, none, QStyle::SC_ToolButton, sidebarPrototype, 18, 8.0);

    builder.block(QStringLiteral("New - split menu button (MenuButtonPopup), framed button"), QStringLiteral("607:1216"));
    const QStyleOptionToolButton::ToolButtonFeatures split = QStyleOptionToolButton::MenuButtonPopup | QStyleOptionToolButton::HasMenu;
    toolSpecimen(QStringLiteral("Split / Default"), QStringLiteral("607:1233"), "rotate-cw", QString(), Qt::ToolButtonIconOnly, Default, split, {}, toolbarPrototype, 20, 10.0);
    toolSpecimen(QStringLiteral("Split / main hovered"), QStringLiteral("607:1244"), "rotate-cw", QString(), Qt::ToolButtonIconOnly, Hover, split, QStyle::SC_ToolButton, toolbarPrototype, 20, 10.0);
    toolSpecimen(QStringLiteral("Split / arrow hovered"), QStringLiteral("607:1255"), "rotate-cw", QString(), Qt::ToolButtonIconOnly, Hover, split, QStyle::SC_ToolButtonMenu, toolbarPrototype, 20, 10.0);
    toolSpecimen(QStringLiteral("Split / main pressed"), QStringLiteral("607:1266"), "rotate-cw", QString(), Qt::ToolButtonIconOnly, Pressed | Hover, split, QStyle::SC_ToolButton, toolbarPrototype, 20, 10.0);
    toolSpecimen(QStringLiteral("Split / menu open"), QStringLiteral("607:1277"), "rotate-cw", QString(), Qt::ToolButtonIconOnly, Pressed | Hover, split, QStyle::SC_ToolButtonMenu, toolbarPrototype, 20, 10.0);

    {
        // A tool button outside of a toolbar, which is not raised automatically
        QStyleOptionToolButton option;
        GalleryHelper::initOption(option, QRect(), Default);
        option.icon = GalleryHelper::icon("folder-open");
        option.iconSize = QSize(16, 16);
        option.toolButtonStyle = Qt::ToolButtonIconOnly;
        option.subControls = QStyle::SC_ToolButton;

        builder.specimen(QStringLiteral("Framed (autoRaise = false)"), QSize(36, 36), [option, framedPrototype = QPointer<QWidget>(framedPrototype)](QPainter& painter, const QRect& rect) mutable
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            option.rect = rect;
            style->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, framedPrototype);
        }, QStringLiteral("607:1289"));
    }

    builder.block(QStringLiteral("New - toolbar panel, separator, handle, extension button (live)"), QStringLiteral("607:1292"));
    QToolBar* toolBar = new QToolBar(builder.page());
    toolBar->setMovable(true);
    toolBar->setIconSize(QSize(20, 20));
    QAction* saveAction = toolBar->addAction(GalleryHelper::icon("save"), QStringLiteral("Save"));
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setToolTip(QStringLiteral("Save"));
    toolBar->addAction(GalleryHelper::icon("printer"), QStringLiteral("Print"));
    toolBar->addSeparator();
    toolBar->addAction(GalleryHelper::icon("undo"), QStringLiteral("Undo"));
    toolBar->addAction(GalleryHelper::icon("redo"), QStringLiteral("Redo"))->setEnabled(false);
    toolBar->addSeparator();

    QToolButton* fileButton = new QToolButton(toolBar);
    fileButton->setIcon(GalleryHelper::icon("file"));
    fileButton->setText(QStringLiteral("File"));
    fileButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    fileButton->setPopupMode(QToolButton::InstantPopup);
    fileButton->setMenu(GalleryHelper::createSampleMenu(fileButton));
    toolBar->addWidget(fileButton);

    QToolButton* rotateButton = new QToolButton(toolBar);
    rotateButton->setIcon(GalleryHelper::icon("rotate-cw"));
    rotateButton->setToolTip(QStringLiteral("Rotate right"));
    rotateButton->setPopupMode(QToolButton::MenuButtonPopup);
    rotateButton->setMenu(GalleryHelper::createSampleMenu(rotateButton));
    toolBar->addWidget(rotateButton);

    QToolButton* zoomButton = new QToolButton(toolBar);
    zoomButton->setIcon(GalleryHelper::icon("zoom-in"));
    zoomButton->setToolTip(QStringLiteral("Zoom"));
    zoomButton->setPopupMode(QToolButton::InstantPopup);
    zoomButton->setMenu(GalleryHelper::createSampleMenu(zoomButton));
    toolBar->addWidget(zoomButton);

    QAction* highlightAction = toolBar->addAction(GalleryHelper::icon("highlighter"), QStringLiteral("Highlight"));
    highlightAction->setCheckable(true);
    highlightAction->setChecked(true);
    for (const char* name : { "underline", "strikethrough", "squiggly", "sticky-note", "stamp", "link" })
    {
        toolBar->addAction(GalleryHelper::icon(name), QString::fromLatin1(name));
    }
    toolBar->setFixedWidth(560);
    builder.live(QStringLiteral("QToolBar (narrow, with the extension button)"), toolBar, QStringLiteral("607:1348"));

    builder.row();
    QToolBar* sidebarTabs = new QToolBar(builder.page());
    sidebarTabs->setProperty(Properties::SIDEBAR_TABS, true);
    sidebarTabs->setMovable(false);
    QActionGroup* sidebarGroup = new QActionGroup(sidebarTabs);
    for (const char* name : { "pages", "bookmark", "list", "paperclip", "search" })
    {
        QAction* action = sidebarTabs->addAction(GalleryHelper::icon(name), QString::fromLatin1(name));
        action->setCheckable(true);
        action->setChecked(qstrcmp(name, "bookmark") == 0);
        sidebarGroup->addAction(action);
    }
    builder.live(QStringLiteral("Toolbar of the icon tabs of a sidebar"), sidebarTabs, QStringLiteral("636:1791"));

    QToolButton* tile = new QToolButton(builder.page());
    tile->setIcon(GalleryHelper::icon("highlighter"));
    tile->setText(QStringLiteral("Highlight"));
    tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tile->setCheckable(true);
    tile->setAutoRaise(true);
    builder.live(QStringLiteral("Action tile (checkable)"), tile, QStringLiteral("607:1201"));

    QToolButton* framed = new QToolButton(builder.page());
    framed->setIcon(GalleryHelper::icon("folder-open"));
    builder.live(QStringLiteral("Framed"), framed, QStringLiteral("607:1289"));

    return builder.finish();
}

GalleryPages::Page GalleryPages::createCheckBoxesPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("03-checkbox-radio"), QStringLiteral("Check boxes and radio buttons"), QStringLiteral("check-circle"));
    QStyle* style = builder.style();

    auto indicatorSpecimen = [&builder, style](const QString& caption, const QString& text, StateFlags flags, bool isRadio, const QString& node)
    {
        QStyleOptionButton option;
        GalleryHelper::initOption(option, QRect(), flags);
        option.text = text;

        const QSize size = style->sizeFromContents(isRadio ? QStyle::CT_RadioButton : QStyle::CT_CheckBox, &option, QSize(), nullptr);
        const bool isChecked = flags.testFlag(GalleryPages::On) || flags.testFlag(GalleryPages::Partial);
        const bool hasFocus = flags.testFlag(GalleryPages::Focus);

        builder.specimen(caption, size, [option, isRadio, isChecked, hasFocus](QPainter& painter, const QRect& rect) mutable
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            option.rect = rect;
            style->drawControl(isRadio ? QStyle::CE_RadioButton : QStyle::CE_CheckBox, &option, &painter, nullptr);

            if (hasFocus && isChecked)
            {
                // Checked indicator has its ring outside, the focus frame of the style draws it
                const QRect indicator = style->subElementRect(isRadio ? QStyle::SE_RadioButtonIndicator : QStyle::SE_CheckBoxIndicator, &option, nullptr);
                Painter::focusRing(&painter, QRectF(indicator), isRadio ? 9.0 : 5.0, true);
            }
        }, node);
    };

    builder.block(QStringLiteral("Existing - Components / Checkbox: unchecked"), QStringLiteral("608:1181"));
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        indicatorSpecimen(QStringLiteral("Checked=False / %1").arg(QString::fromLatin1(state.name)), QStringLiteral("Enable"), StateFlags(state.flags), false, QStringLiteral("608:1181"));
    }

    builder.block(QStringLiteral("Existing - Components / Checkbox: checked"), QStringLiteral("608:1181"));
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        indicatorSpecimen(QStringLiteral("Checked=True / %1").arg(QString::fromLatin1(state.name)), QStringLiteral("Enable"), StateFlags(state.flags) | On, false, QStringLiteral("608:1181"));
    }

    builder.block(QStringLiteral("New - partially checked (State_NoChange)"), QStringLiteral("608:1268"));
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        indicatorSpecimen(QStringLiteral("Partial / %1").arg(QString::fromLatin1(state.name)), QStringLiteral("Include"), StateFlags(state.flags) | Partial, false, QStringLiteral("608:1268"));
    }

    builder.block(QStringLiteral("New - radio button states: off"), QStringLiteral("608:1312"));
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        indicatorSpecimen(QStringLiteral("Off / %1").arg(QString::fromLatin1(state.name)), QStringLiteral("Option"), StateFlags(state.flags), true, QStringLiteral("608:1312"));
    }

    builder.block(QStringLiteral("New - radio button states: on"), QStringLiteral("608:1312"));
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        indicatorSpecimen(QStringLiteral("On / %1").arg(QString::fromLatin1(state.name)), QStringLiteral("Option"), StateFlags(state.flags) | On, true, QStringLiteral("608:1312"));
    }

    builder.block(QStringLiteral("New - stand-alone indicators"), QStringLiteral("608:1368"));
    struct Indicator
    {
        const char* name;
        QStyle::PrimitiveElement element;
        int flags;
        bool isExclusive;
    };
    static constexpr Indicator indicators[] =
    {
        { "Item view check / off", QStyle::PE_IndicatorItemViewItemCheck, Default, false },
        { "Item view check / on", QStyle::PE_IndicatorItemViewItemCheck, On, false },
        { "Item view check / partial", QStyle::PE_IndicatorItemViewItemCheck, Partial, false },
        { "Item view check / hover", QStyle::PE_IndicatorItemViewItemCheck, Hover, false },
        { "Menu check mark (16)", QStyle::PE_IndicatorMenuCheckMark, On, false },
        { "Menu exclusive mark (6 px dot)", QStyle::PE_IndicatorMenuCheckMark, On, true }
    };
    for (const Indicator& indicator : indicators)
    {
        const bool isMenuMark = indicator.element == QStyle::PE_IndicatorMenuCheckMark;
        const QStyle::PrimitiveElement element = indicator.element;
        const StateFlags flags(indicator.flags);
        const bool isExclusive = indicator.isExclusive;

        builder.specimen(QString::fromLatin1(indicator.name), isMenuMark ? QSize(16, 16) : QSize(18, 18), [element, flags, isExclusive](QPainter& painter, const QRect& rect)
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            QStyleOptionMenuItem option;
            GalleryHelper::initOption(option, rect, flags);
            option.checkType = isExclusive ? QStyleOptionMenuItem::Exclusive : QStyleOptionMenuItem::NonExclusive;
            option.checked = true;
            style->drawPrimitive(element, &option, &painter, nullptr);
        });
    }

    builder.block(QStringLiteral("Live controls"), QStringLiteral("608:1181"));
    builder.live(QStringLiteral("QCheckBox"), new QCheckBox(QStringLiteral("&Enable"), builder.page()));
    QCheckBox* checked = builder.live(QStringLiteral("QCheckBox checked"), new QCheckBox(QStringLiteral("Enable"), builder.page()));
    checked->setChecked(true);
    QCheckBox* tristate = builder.live(QStringLiteral("QCheckBox tristate"), new QCheckBox(QStringLiteral("Include"), builder.page()));
    tristate->setTristate(true);
    tristate->setCheckState(Qt::PartiallyChecked);
    QCheckBox* disabledCheckBox = builder.live(QStringLiteral("QCheckBox disabled"), new QCheckBox(QStringLiteral("Enable"), builder.page()));
    disabledCheckBox->setChecked(true);
    disabledCheckBox->setEnabled(false);

    QButtonGroup* radioGroup = new QButtonGroup(builder.page());
    for (const char* name : { "Option A", "Option B", "Option C" })
    {
        QRadioButton* radioButton = builder.live(QStringLiteral("QRadioButton"), new QRadioButton(QString::fromLatin1(name), builder.page()));
        radioButton->setChecked(qstrcmp(name, "Option B") == 0);
        radioGroup->addButton(radioButton);
    }
    QRadioButton* disabledRadio = builder.live(QStringLiteral("QRadioButton disabled"), new QRadioButton(QStringLiteral("Option"), builder.page()));
    disabledRadio->setEnabled(false);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createTextInputPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("04-text-input"), QStringLiteral("Text input"), QStringLiteral("text-cursor"));
    QStyle* style = builder.style();

    QLineEdit* prototype = new pdf::PDFLineEdit(builder.page());
    prototype->hide();
    QLineEdit* invalidPrototype = new pdf::PDFLineEdit(builder.page());
    invalidPrototype->setProperty(Properties::INVALID, true);
    invalidPrototype->hide();
    QLineEdit* readOnlyPrototype = new pdf::PDFLineEdit(builder.page());
    readOnlyPrototype->setReadOnly(true);
    readOnlyPrototype->hide();

    auto fieldSpecimen = [&builder, style](const QString& caption, const QString& text, StateFlags flags, QLineEdit* fieldPrototype, bool isMuted)
    {
        builder.specimen(caption, QSize(200, 34), [text, flags, fieldPrototype = QPointer<QLineEdit>(fieldPrototype), isMuted](QPainter& painter, const QRect& rect)
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            QStyleOptionFrame option;
            GalleryHelper::initOption(option, rect, flags);
            option.lineWidth = 1;
            style->drawPrimitive(QStyle::PE_PanelLineEdit, &option, &painter, fieldPrototype);

            // The text is drawn by the widget from its own palette: muted in a
            // disabled field, secondary in a read-only one
            Color textColor = Color::TextPrimary;
            if (isMuted || flags.testFlag(GalleryPages::Disabled))
            {
                textColor = Color::TextMuted;
            }
            else if (fieldPrototype && fieldPrototype->isReadOnly())
            {
                textColor = Color::TextSecondary;
            }
            GalleryHelper::fieldText(painter, rect, text, textColor);
        });
    };

    builder.block(QStringLiteral("Existing - Components / Text field (frame drawn by the style)"), QStringLiteral("609:1230"));
    fieldSpecimen(QStringLiteral("Text field / Default"), QStringLiteral("Text"), Default, prototype, false);
    fieldSpecimen(QStringLiteral("Text field / Hover"), QStringLiteral("Text"), Hover, prototype, false);
    fieldSpecimen(QStringLiteral("Text field / Focus"), QStringLiteral("Text"), Focus, prototype, false);
    fieldSpecimen(QStringLiteral("Text field / Disabled"), QStringLiteral("Text"), Disabled, prototype, false);

    builder.block(QStringLiteral("New - read-only, error (frame drawn by the style)"), QStringLiteral("609:1346"));
    fieldSpecimen(QStringLiteral("Read-only"), QStringLiteral("C:\\Users\\Jakub\\Documents"), Default, readOnlyPrototype, false);
    fieldSpecimen(QStringLiteral("Error"), QStringLiteral("12..5"), Default, invalidPrototype, false);
    fieldSpecimen(QStringLiteral("Error / Focus"), QStringLiteral("12..5"), Focus, invalidPrototype, false);
    fieldSpecimen(QStringLiteral("Placeholder"), QStringLiteral("Search in document…"), Default, prototype, true);

    builder.block(QStringLiteral("Live fields (PDFLineEdit)"), QStringLiteral("609:1287"));
    auto lineEdit = [&builder](const QString& caption, const QString& text, int width)
    {
        pdf::PDFLineEdit* edit = builder.live(caption, new pdf::PDFLineEdit(text, builder.page()));
        edit->setFixedWidth(width);
        return edit;
    };

    lineEdit(QStringLiteral("Text field"), QStringLiteral("Project_Report"), 260);
    lineEdit(QStringLiteral("Placeholder"), QString(), 260)->setPlaceholderText(QStringLiteral("Search in document…"));
    lineEdit(QStringLiteral("Clear button"), QStringLiteral("Project_Report.pdf"), 260)->setClearButtonEnabled(true);
    builder.row();
    lineEdit(QStringLiteral("Leading icon"), QStringLiteral("C:\\Users\\Jakub\\Documents"), 260)->addIconAction(QStringLiteral("folder"), QLineEdit::LeadingPosition);
    lineEdit(QStringLiteral("Trailing icon"), QStringLiteral("Scale"), 260)->addIconAction(QStringLiteral("ruler"), QLineEdit::TrailingPosition);
    lineEdit(QStringLiteral("Read-only"), QStringLiteral("C:\\Users\\Jakub\\Documents\\Project_Report.pdf"), 260)->setReadOnly(true);
    builder.row();
    lineEdit(QStringLiteral("Error"), QStringLiteral("12..5"), 260)->setInvalid(true);
    lineEdit(QStringLiteral("Frameless"), QStringLiteral("Untitled document"), 260)->setFrame(false);
    lineEdit(QStringLiteral("Disabled"), QStringLiteral("Text"), 260)->setEnabled(false);
    builder.row();
    pdf::PDFLineEdit* searchField = lineEdit(QStringLiteral("Search field (pill)"), QString(), 320);
    searchField->setSearchField();
    searchField->setPlaceholderText(QStringLiteral("Search tools, documents, or help..."));
    pdf::PDFLineEdit* passwordField = lineEdit(QStringLiteral("Password field"), QStringLiteral("correct horse"), 320);
    passwordField->setPasswordField();

    builder.block(QStringLiteral("New - multi-line editors (PDFTextEdit, PDFPlainTextEdit)"), QStringLiteral("609:1377"));
    pdf::PDFTextEdit* textEdit = builder.live(QStringLiteral("PDFTextEdit"), new pdf::PDFTextEdit(builder.page()));
    textEdit->setPlainText(QStringLiteral("The document was scanned at 300 dpi. Text recognition found 1,592 pages of body text and 38 pages with figures only; the review marks are kept in the project file.\n\nSecond paragraph, which makes the editor scroll.\n\nThird paragraph."));
    textEdit->setFixedSize(340, 112);
    pdf::PDFPlainTextEdit* logEdit = builder.live(QStringLiteral("PDFPlainTextEdit / read-only (log)"), new pdf::PDFPlainTextEdit(builder.page()));
    logEdit->setPlainText(QStringLiteral("Checking fonts… 14 embedded\nChecking images… 412 objects\nStructure: 3 issues found"));
    logEdit->setReadOnly(true);
    logEdit->setFixedSize(340, 112);
    pdf::PDFTextEdit* disabledEdit = builder.live(QStringLiteral("Disabled"), new pdf::PDFTextEdit(builder.page()));
    disabledEdit->setPlainText(QStringLiteral("Not available for encrypted documents."));
    disabledEdit->setEnabled(false);
    disabledEdit->setFixedSize(340, 112);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createSpinBoxesPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("05-spin-box-date-edit"), QStringLiteral("Spin boxes and date / time edits"), QStringLiteral("hash"));
    QStyle* style = builder.style();

    auto spinSpecimen = [&builder, style](const QString& caption, StateFlags flags, QStyle::SubControls activeSubControls, QAbstractSpinBox::StepEnabled stepEnabled, QAbstractSpinBox::ButtonSymbols symbols)
    {
        builder.specimen(caption, QSize(140, 34), [flags, activeSubControls, stepEnabled, symbols](QPainter& painter, const QRect& rect)
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            QStyleOptionSpinBox option;
            GalleryHelper::initOption(option, rect, flags);
            option.frame = true;
            option.buttonSymbols = symbols;
            option.stepEnabled = stepEnabled;
            option.subControls = QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField | QStyle::SC_SpinBoxUp | QStyle::SC_SpinBoxDown;
            option.activeSubControls = activeSubControls;
            style->drawComplexControl(QStyle::CC_SpinBox, &option, &painter, nullptr);
            GalleryHelper::fieldText(painter, rect, QStringLiteral("50"), flags.testFlag(GalleryPages::Disabled));
        });
    };

    const QAbstractSpinBox::StepEnabled both = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;

    builder.block(QStringLiteral("Existing - Components / Spin box"), QStringLiteral("610:1257"));
    spinSpecimen(QStringLiteral("Spin box / Default"), Default, {}, both, QAbstractSpinBox::UpDownArrows);
    spinSpecimen(QStringLiteral("Spin box / Hover"), Hover, {}, both, QAbstractSpinBox::UpDownArrows);
    spinSpecimen(QStringLiteral("Spin box / Focus"), Focus, {}, both, QAbstractSpinBox::UpDownArrows);
    spinSpecimen(QStringLiteral("Spin box / Disabled"), Disabled, {}, both, QAbstractSpinBox::UpDownArrows);

    builder.block(QStringLiteral("New - stepper halves and button symbols"), QStringLiteral("610:1305"));
    spinSpecimen(QStringLiteral("Up hovered"), Hover, QStyle::SC_SpinBoxUp, both, QAbstractSpinBox::UpDownArrows);
    spinSpecimen(QStringLiteral("Up pressed"), Hover | Pressed, QStyle::SC_SpinBoxUp, both, QAbstractSpinBox::UpDownArrows);
    spinSpecimen(QStringLiteral("Down hovered"), Hover, QStyle::SC_SpinBoxDown, both, QAbstractSpinBox::UpDownArrows);
    spinSpecimen(QStringLiteral("At maximum (up disabled)"), Default, {}, QAbstractSpinBox::StepDownEnabled, QAbstractSpinBox::UpDownArrows);
    spinSpecimen(QStringLiteral("At minimum (down disabled)"), Default, {}, QAbstractSpinBox::StepUpEnabled, QAbstractSpinBox::UpDownArrows);
    spinSpecimen(QStringLiteral("PlusMinus symbols"), Default, {}, both, QAbstractSpinBox::PlusMinus);
    spinSpecimen(QStringLiteral("NoButtons"), Default, {}, both, QAbstractSpinBox::NoButtons);

    builder.block(QStringLiteral("Live spin boxes (PDFSpinBox, PDFDoubleSpinBox)"), QStringLiteral("610:1305"));
    pdf::PDFSpinBox* spinBox = builder.live(QStringLiteral("Value with a suffix"), new pdf::PDFSpinBox(builder.page()));
    spinBox->setRange(0, 100);
    spinBox->setValue(50);
    spinBox->setDisplaySuffix(QStringLiteral("px"));
    spinBox->setFixedWidth(140);
    pdf::PDFSpinBox* plusMinus = builder.live(QStringLiteral("PlusMinus"), new pdf::PDFSpinBox(builder.page()));
    plusMinus->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    plusMinus->setValue(3);
    plusMinus->setDisplaySuffix(QStringLiteral("pt"));
    plusMinus->setFixedWidth(140);
    pdf::PDFSpinBox* noButtons = builder.live(QStringLiteral("NoButtons"), new pdf::PDFSpinBox(builder.page()));
    noButtons->setButtonSymbols(QAbstractSpinBox::NoButtons);
    noButtons->setRange(0, 400);
    noButtons->setValue(150);
    noButtons->setDisplaySuffix(QStringLiteral("%"));
    noButtons->setFixedWidth(140);
    pdf::PDFDoubleSpinBox* doubleSpinBox = builder.live(QStringLiteral("Prefix + suffix (double)"), new pdf::PDFDoubleSpinBox(builder.page()));
    doubleSpinBox->setValue(1.25);
    doubleSpinBox->setDisplayPrefix(QStringLiteral("×"));
    doubleSpinBox->setDisplaySuffix(QStringLiteral("mm"));
    doubleSpinBox->setFixedWidth(170);
    pdf::PDFSpinBox* disabledSpinBox = builder.live(QStringLiteral("Disabled"), new pdf::PDFSpinBox(builder.page()));
    disabledSpinBox->setValue(50);
    disabledSpinBox->setEnabled(false);
    disabledSpinBox->setFixedWidth(140);

    builder.block(QStringLiteral("New - QDateTimeEdit with calendar popup (PDFDateTimeEdit, PDFCalendarWidget)"), QStringLiteral("610:1415"));
    pdf::PDFDateTimeEdit* dateEdit = builder.live(QStringLiteral("QDateEdit (calendar popup)"), new pdf::PDFDateTimeEdit(QDate(2026, 10, 14), builder.page()));
    dateEdit->setCalendarPopupEnabled(true);
    dateEdit->setDisplayFormat(QStringLiteral("d. M. yyyy"));
    dateEdit->setFixedWidth(170);
    pdf::PDFDateTimeEdit* timeEdit = builder.live(QStringLiteral("QTimeEdit (stepper)"), new pdf::PDFDateTimeEdit(QTime(10, 42), builder.page()));
    timeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    timeEdit->setFixedWidth(120);
    pdf::PDFDateTimeEdit* disabledDateEdit = builder.live(QStringLiteral("QDateEdit / Disabled"), new pdf::PDFDateTimeEdit(QDate(2026, 10, 14), builder.page()));
    disabledDateEdit->setCalendarPopupEnabled(true);
    disabledDateEdit->setDisplayFormat(QStringLiteral("d. M. yyyy"));
    disabledDateEdit->setEnabled(false);
    disabledDateEdit->setFixedWidth(170);

    pdf::PDFCalendarWidget* calendar = new pdf::PDFCalendarWidget(builder.page());
    calendar->setSelectedDate(QDate(2026, 10, 14));
    calendar->setFirstDayOfWeek(Qt::Monday);

    QFrame* calendarPanel = new QFrame(builder.page());
    calendarPanel->setProperty(Properties::CARD, true);
    QVBoxLayout* calendarLayout = new QVBoxLayout(calendarPanel);
    calendarLayout->setContentsMargins(9, 9, 9, 9);
    calendarLayout->addWidget(calendar);
    builder.live(QStringLiteral("PDFCalendarWidget in a panel"), calendarPanel, QStringLiteral("610:1456"));

    return builder.finish();
}

GalleryPages::Page GalleryPages::createComboBoxesPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("06-combo-box"), QStringLiteral("Combo boxes"), QStringLiteral("chevron-down"));
    QStyle* style = builder.style();

    auto comboSpecimen = [&builder, style](const QString& caption, StateFlags flags, bool isEditable, QStyle::SubControls activeSubControls)
    {
        builder.specimen(caption, QSize(220, 34), [flags, isEditable, activeSubControls](QPainter& painter, const QRect& rect)
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            QStyleOptionComboBox option;
            GalleryHelper::initOption(option, rect, flags);
            option.frame = true;
            option.editable = isEditable;
            option.currentText = isEditable ? QStringLiteral("150 %") : QStringLiteral("Automatic detection");
            option.subControls = QStyle::SC_ComboBoxFrame | QStyle::SC_ComboBoxArrow | QStyle::SC_ComboBoxEditField;
            option.activeSubControls = activeSubControls;
            style->drawComplexControl(QStyle::CC_ComboBox, &option, &painter, nullptr);

            if (isEditable)
            {
                GalleryHelper::fieldText(painter, rect, option.currentText, flags.testFlag(GalleryPages::Disabled));
            }
            else
            {
                style->drawControl(QStyle::CE_ComboBoxLabel, &option, &painter, nullptr);
            }
        });
    };

    builder.block(QStringLiteral("Existing - Components / Combo box"), QStringLiteral("612:1310"));
    comboSpecimen(QStringLiteral("Combo box / Default"), Default, false, {});
    comboSpecimen(QStringLiteral("Combo box / Hover"), Hover, false, {});
    comboSpecimen(QStringLiteral("Combo box / Pressed"), Pressed | Hover, false, {});
    comboSpecimen(QStringLiteral("Combo box / Focus"), Focus, false, {});
    comboSpecimen(QStringLiteral("Combo box / Disabled"), Disabled, false, {});

    builder.block(QStringLiteral("New - editable combo box"), QStringLiteral("612:1409"));
    comboSpecimen(QStringLiteral("Editable / Default"), Default, true, {});
    comboSpecimen(QStringLiteral("Editable / arrow hovered"), Hover, true, QStyle::SC_ComboBoxArrow);
    comboSpecimen(QStringLiteral("Editable / arrow pressed"), Hover | Pressed, true, QStyle::SC_ComboBoxArrow);
    comboSpecimen(QStringLiteral("Editable / Disabled"), Disabled, true, {});

    builder.block(QStringLiteral("Live combo boxes (PDFComboBox) - open them to see the popup list"), QStringLiteral("612:1357"));
    pdf::PDFComboBox* comboBox = builder.live(QStringLiteral("Items, separator, disabled item"), new pdf::PDFComboBox(builder.page()));
    comboBox->addItems({ QStringLiteral("Automatic detection"), QStringLiteral("English"), QStringLiteral("Czech"), QStringLiteral("German") });
    comboBox->insertSeparator(4);
    comboBox->addItem(QStringLiteral("Download more languages…"));
    if (QStandardItemModel* model = qobject_cast<QStandardItemModel*>(comboBox->model()))
    {
        model->item(5)->setEnabled(false);
    }
    comboBox->setFixedWidth(260);

    pdf::PDFComboBox* iconCombo = builder.live(QStringLiteral("Items with icons"), new pdf::PDFComboBox(builder.page()));
    iconCombo->addItem(GalleryHelper::icon("fit-width"), QStringLiteral("Fit width"));
    iconCombo->addItem(GalleryHelper::icon("maximize"), QStringLiteral("Fit page"));
    iconCombo->addItem(GalleryHelper::icon("actual-size"), QStringLiteral("Actual size"));
    iconCombo->setFixedWidth(220);

    pdf::PDFComboBox* editableCombo = builder.live(QStringLiteral("Editable"), new pdf::PDFComboBox(builder.page()));
    editableCombo->setEditable(true);
    editableCombo->addItems({ QStringLiteral("50 %"), QStringLiteral("100 %"), QStringLiteral("150 %"), QStringLiteral("200 %") });
    editableCombo->setCurrentIndex(2);
    editableCombo->setFixedWidth(260);

    builder.row();
    pdf::PDFComboBox* placeholderCombo = builder.live(QStringLiteral("Placeholder"), new pdf::PDFComboBox(builder.page()));
    placeholderCombo->addItems({ QStringLiteral("Jakub Melka"), QStringLiteral("Test certificate") });
    placeholderCombo->setPlaceholderText(QStringLiteral("Choose a certificate…"));
    placeholderCombo->setCurrentIndex(-1);
    placeholderCombo->setFixedWidth(260);

    pdf::PDFComboBox* compactCombo = builder.live(QStringLiteral("Compact (28 px)"), new pdf::PDFComboBox(builder.page()));
    compactCombo->addItems({ QStringLiteral("Low"), QStringLiteral("Medium"), QStringLiteral("High") });
    compactCombo->setCurrentIndex(1);
    compactCombo->setCompact(true);
    compactCombo->setFixedWidth(140);

    pdf::PDFComboBox* selectCombo = builder.live(QStringLiteral("Select with a prefix"), new pdf::PDFComboBox(builder.page()));
    selectCombo->addItems({ QStringLiteral("Last opened"), QStringLiteral("Name"), QStringLiteral("Size") });
    selectCombo->setSelectPrefix(QStringLiteral("Sort:"));

    pdf::PDFComboBox* disabledCombo = builder.live(QStringLiteral("Disabled"), new pdf::PDFComboBox(builder.page()));
    disabledCombo->addItem(QStringLiteral("Automatic detection"));
    disabledCombo->setEnabled(false);
    disabledCombo->setFixedWidth(220);

    pdf::PDFComboBox* longCombo = builder.live(QStringLiteral("More than 10 rows"), new pdf::PDFComboBox(builder.page()));
    for (int i = 1; i <= 30; ++i)
    {
        longCombo->addItem(QStringLiteral("Page %1").arg(i));
    }
    longCombo->setFixedWidth(160);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createSlidersPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("07-slider-dial"), QStringLiteral("Sliders and dial"), QStringLiteral("sliders"));
    QStyle* style = builder.style();

    auto sliderSpecimen = [&builder, style](const QString& caption, int value, StateFlags flags, QStyle::SubControls activeSubControls, Qt::Orientation orientation, QSlider::TickPosition ticks)
    {
        const bool hasTicks = ticks != QSlider::NoTicks;
        const QSize size = orientation == Qt::Horizontal ? QSize(180, hasTicks ? 28 : 20) : QSize(hasTicks ? 28 : 20, 140);

        builder.specimen(caption, size, [value, flags, activeSubControls, orientation, ticks](QPainter& painter, const QRect& rect)
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            QStyleOptionSlider option;
            GalleryHelper::initOption(option, rect, flags);
            option.orientation = orientation;
            option.minimum = 0;
            option.maximum = 100;
            option.sliderPosition = value;
            option.sliderValue = value;
            option.tickPosition = ticks;
            option.tickInterval = 25;
            option.pageStep = 10;
            option.upsideDown = orientation == Qt::Vertical;
            option.subControls = QStyle::SC_SliderGroove | QStyle::SC_SliderHandle | (ticks != QSlider::NoTicks ? QStyle::SC_SliderTickmarks : QStyle::SC_None);
            option.activeSubControls = activeSubControls;
            if (orientation == Qt::Horizontal)
            {
                option.state |= QStyle::State_Horizontal;
            }
            style->drawComplexControl(QStyle::CC_Slider, &option, &painter, nullptr);
        });
    };

    builder.block(QStringLiteral("Existing - Components / Slider"), QStringLiteral("613:1345"));
    for (int value : { 0, 25, 50, 75, 100 })
    {
        sliderSpecimen(QStringLiteral("Slider / Value=%1").arg(value), value, Default, {}, Qt::Horizontal, QSlider::NoTicks);
    }

    builder.block(QStringLiteral("New - handle states, disabled, tick marks"), QStringLiteral("613:1383"));
    sliderSpecimen(QStringLiteral("Handle / Hover"), 50, Hover, QStyle::SC_SliderHandle, Qt::Horizontal, QSlider::NoTicks);
    sliderSpecimen(QStringLiteral("Handle / Pressed"), 50, Hover | Pressed, QStyle::SC_SliderHandle, Qt::Horizontal, QSlider::NoTicks);
    sliderSpecimen(QStringLiteral("Handle / Focus"), 50, Focus, {}, Qt::Horizontal, QSlider::NoTicks);
    sliderSpecimen(QStringLiteral("Handle / Disabled"), 50, Disabled, {}, Qt::Horizontal, QSlider::NoTicks);
    sliderSpecimen(QStringLiteral("TicksBelow, interval 25"), 50, Default, {}, Qt::Horizontal, QSlider::TicksBelow);

    builder.block(QStringLiteral("New - vertical slider"), QStringLiteral("613:1428"));
    sliderSpecimen(QStringLiteral("Vertical / 60"), 60, Default, {}, Qt::Vertical, QSlider::NoTicks);
    sliderSpecimen(QStringLiteral("Vertical / Hover"), 60, Hover, QStyle::SC_SliderHandle, Qt::Vertical, QSlider::NoTicks);
    sliderSpecimen(QStringLiteral("Vertical / ticks"), 60, Default, {}, Qt::Vertical, QSlider::TicksBelow);
    sliderSpecimen(QStringLiteral("Vertical / Disabled"), 60, Disabled, {}, Qt::Vertical, QSlider::NoTicks);

    auto dialSpecimen = [&builder, style](const QString& caption, int value, StateFlags flags)
    {
        builder.specimen(caption, QSize(64, 64), [value, flags](QPainter& painter, const QRect& rect)
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            QStyleOptionSlider option;
            GalleryHelper::initOption(option, rect, flags);
            option.minimum = 0;
            option.maximum = 100;
            option.sliderPosition = value;
            option.sliderValue = value;
            option.upsideDown = true;
            option.dialWrapping = false;
            option.subControls = QStyle::SC_DialGroove | QStyle::SC_DialHandle;
            style->drawComplexControl(QStyle::CC_Dial, &option, &painter, nullptr);
        });
    };

    builder.block(QStringLiteral("New - QDial"), QStringLiteral("613:1465"));
    dialSpecimen(QStringLiteral("Dial / 60"), 60, Default);
    dialSpecimen(QStringLiteral("Dial / 0"), 0, Default);
    dialSpecimen(QStringLiteral("Dial / 100 Hover"), 100, Hover);
    dialSpecimen(QStringLiteral("Dial / Pressed"), 35, Hover | Pressed);
    dialSpecimen(QStringLiteral("Dial / Disabled"), 60, Disabled);

    builder.block(QStringLiteral("Live sliders"), QStringLiteral("613:1345"));
    QSlider* slider = builder.live(QStringLiteral("QSlider"), new QSlider(Qt::Horizontal, builder.page()));
    slider->setValue(50);
    slider->setFixedWidth(180);
    QSlider* tickSlider = builder.live(QStringLiteral("QSlider with ticks"), new QSlider(Qt::Horizontal, builder.page()));
    tickSlider->setTickPosition(QSlider::TicksBelow);
    tickSlider->setTickInterval(25);
    tickSlider->setValue(25);
    tickSlider->setFixedWidth(180);
    QSlider* disabledSlider = builder.live(QStringLiteral("Disabled"), new QSlider(Qt::Horizontal, builder.page()));
    disabledSlider->setValue(50);
    disabledSlider->setEnabled(false);
    disabledSlider->setFixedWidth(180);
    QSlider* verticalSlider = builder.live(QStringLiteral("Vertical"), new QSlider(Qt::Vertical, builder.page()));
    verticalSlider->setValue(60);
    verticalSlider->setFixedHeight(140);
    QDial* dial = builder.live(QStringLiteral("PDFDial"), new pdf::PDFDial(builder.page()));
    dial->setValue(60);
    dial->setFixedSize(64, 64);
    QDial* notchDial = builder.live(QStringLiteral("PDFDial with notches"), new pdf::PDFDial(builder.page()));
    notchDial->setNotchesVisible(true);
    notchDial->setValue(30);
    notchDial->setFixedSize(64, 64);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createProgressBarsPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("08-progress-bar"), QStringLiteral("Progress bars"), QStringLiteral("refresh-cw"));
    QStyle* style = builder.style();

    QProgressBar* thinPrototype = new QProgressBar(builder.page());
    thinPrototype->setProperty(Properties::THIN, true);
    thinPrototype->hide();

    auto progressSpecimen = [&builder, style](const QString& caption, int value, int maximum, const QString& text, StateFlags flags, bool isHorizontal, QProgressBar* prototype)
    {
        QStyleOptionProgressBar option;
        GalleryHelper::initOption(option, QRect(), flags);
        option.minimum = 0;
        option.maximum = maximum;
        option.progress = value;
        option.text = text;
        option.textVisible = !text.isEmpty();
        if (isHorizontal)
        {
            option.state |= QStyle::State_Horizontal;
        }

        const QSize thickness = style->sizeFromContents(QStyle::CT_ProgressBar, &option, QSize(180, 120), prototype);
        const int labelWidth = text.isEmpty() ? 0 : 8 + pdf::PDFStylePainter::textWidth(text, pdf::PDFThemeFonts::getFont(Font::Small)) + 1;
        const QSize size = isHorizontal ? QSize(180 + labelWidth, thickness.height()) : QSize(thickness.width(), 120);

        builder.specimen(caption, size, [option, prototype = QPointer<QWidget>(prototype)](QPainter& painter, const QRect& rect) mutable
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            option.rect = rect;
            style->drawControl(QStyle::CE_ProgressBar, &option, &painter, prototype);
        });
    };

    builder.block(QStringLiteral("Existing - Components / Progress bar"), QStringLiteral("615:1355"));
    for (int value : { 0, 25, 50, 75, 100 })
    {
        progressSpecimen(QStringLiteral("Value=%1").arg(value), value, 100, QString(), Default, true, nullptr);
    }

    builder.block(QStringLiteral("New - busy, label, vertical, disabled, thin"), QStringLiteral("615:1384"));
    progressSpecimen(QStringLiteral("Busy (not animated here)"), 0, 0, QString(), Default, true, nullptr);
    progressSpecimen(QStringLiteral("45 % with label"), 45, 100, QStringLiteral("45 %"), Default, true, nullptr);
    progressSpecimen(QStringLiteral("Custom format"), 987, 1592, QStringLiteral("987 / 1,592"), Default, true, nullptr);
    progressSpecimen(QStringLiteral("Disabled 45 %"), 45, 100, QStringLiteral("45 %"), Disabled, true, nullptr);
    progressSpecimen(QStringLiteral("Vertical 60 %"), 60, 100, QString(), Default, false, nullptr);
    progressSpecimen(QStringLiteral("Thin 2 px"), 55, 100, QString(), Default, true, thinPrototype);

    builder.block(QStringLiteral("Live progress bars"), QStringLiteral("615:1384"));
    QProgressBar* progressBar = builder.live(QStringLiteral("QProgressBar with label"), new QProgressBar(builder.page()));
    progressBar->setRange(0, 100);
    progressBar->setValue(45);
    progressBar->setFormat(QStringLiteral("%p %"));
    progressBar->setFixedWidth(220);
    QProgressBar* busyBar = builder.live(QStringLiteral("Busy (animated)"), new QProgressBar(builder.page()));
    busyBar->setRange(0, 0);
    busyBar->setTextVisible(false);
    busyBar->setFixedWidth(180);
    QProgressBar* thinBar = builder.live(QStringLiteral("Thin"), new QProgressBar(builder.page()));
    thinBar->setProperty(Properties::THIN, true);
    thinBar->setValue(55);
    thinBar->setTextVisible(false);
    thinBar->setFixedWidth(195);
    QProgressBar* verticalBar = builder.live(QStringLiteral("Vertical"), new QProgressBar(builder.page()));
    verticalBar->setOrientation(Qt::Vertical);
    verticalBar->setValue(60);
    verticalBar->setTextVisible(false);
    verticalBar->setFixedHeight(120);
    QProgressBar* growingBar = builder.live(QStringLiteral("Running"), new QProgressBar(builder.page()));
    growingBar->setRange(0, 100);
    growingBar->setFormat(QStringLiteral("%p %"));
    growingBar->setFixedWidth(220);
    QTimer* timer = new QTimer(growingBar);
    QObject::connect(timer, &QTimer::timeout, growingBar, [growingBar]() { growingBar->setValue((growingBar->value() + 1) % 101); });
    timer->start(120);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createScrollBarsPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("09-scroll-bars"), QStringLiteral("Scroll bars and scroll areas"), QStringLiteral("arrow-up-down"));
    QStyle* style = builder.style();

    auto scrollSpecimen = [&builder, style](const QString& caption, StateFlags flags, QStyle::SubControls activeSubControls, Qt::Orientation orientation, int maximum, int pageStep)
    {
        const QSize size = orientation == Qt::Vertical ? QSize(12, 200) : QSize(200, 12);

        builder.specimen(caption, size, [flags, activeSubControls, orientation, maximum, pageStep](QPainter& painter, const QRect& rect)
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            QStyleOptionSlider option;
            GalleryHelper::initOption(option, rect, flags);
            option.orientation = orientation;
            option.minimum = 0;
            option.maximum = maximum;
            option.pageStep = pageStep;
            option.singleStep = 1;
            option.sliderPosition = maximum / 6;
            option.sliderValue = option.sliderPosition;
            option.subControls = QStyle::SC_ScrollBarGroove | QStyle::SC_ScrollBarSlider | QStyle::SC_ScrollBarAddPage | QStyle::SC_ScrollBarSubPage;
            option.activeSubControls = activeSubControls;
            if (orientation == Qt::Horizontal)
            {
                option.state |= QStyle::State_Horizontal;
            }
            style->drawComplexControl(QStyle::CC_ScrollBar, &option, &painter, nullptr);
        });
    };

    builder.block(QStringLiteral("New - states, horizontal bar, reserved extent"), QStringLiteral("616:1379"));
    scrollSpecimen(QStringLiteral("Vertical / Default"), Default, {}, Qt::Vertical, 150, 100);
    scrollSpecimen(QStringLiteral("Vertical / Hover"), Hover, {}, Qt::Vertical, 150, 100);
    scrollSpecimen(QStringLiteral("Vertical / Pressed"), Hover | Pressed, QStyle::SC_ScrollBarSlider, Qt::Vertical, 150, 100);
    scrollSpecimen(QStringLiteral("Vertical / Disabled"), Disabled, {}, Qt::Vertical, 150, 100);
    scrollSpecimen(QStringLiteral("Nothing to scroll"), Default, {}, Qt::Vertical, 0, 100);
    scrollSpecimen(QStringLiteral("Minimum slider 32"), Default, {}, Qt::Vertical, 100000, 10);
    scrollSpecimen(QStringLiteral("Horizontal / Default"), Default, {}, Qt::Horizontal, 150, 100);
    scrollSpecimen(QStringLiteral("Horizontal / Hover"), Hover, {}, Qt::Horizontal, 150, 100);
    scrollSpecimen(QStringLiteral("Horizontal / Pressed"), Hover | Pressed, QStyle::SC_ScrollBarSlider, Qt::Horizontal, 150, 100);

    builder.block(QStringLiteral("New - scroll area with both bars and the corner (live)"), QStringLiteral("616:1436"));
    auto createArea = [&builder](const QString& caption, QFrame::Shape shape, const char* property)
    {
        QWidget* content = new QWidget();
        content->setFixedSize(520, 360);
        QVBoxLayout* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(12, 10, 12, 10);
        for (int i = 0; i < 12; ++i)
        {
            contentLayout->addWidget(new pdf::PDFLabel(QStringLiteral("Line %1 of a content, which is larger than the scroll area").arg(i + 1), Font::Body, Color::TextSecondary, content));
        }
        contentLayout->addStretch(1);

        QScrollArea* area = new QScrollArea(builder.page());
        area->setFrameShape(shape);
        area->setWidget(content);
        area->setFixedSize(300, 180);

        if (property)
        {
            QFrame* holder = new QFrame(builder.page());
            holder->setProperty(property, true);
            QVBoxLayout* holderLayout = new QVBoxLayout(holder);
            holderLayout->setContentsMargins(0, 0, 1, 0);
            holderLayout->addWidget(area);
            builder.live(caption, holder);
        }
        else
        {
            builder.live(caption, area);
        }
    };

    createArea(QStringLiteral("QScrollArea with frame (r 8)"), QFrame::StyledPanel, nullptr);
    createArea(QStringLiteral("Frameless view inside the sidebar"), QFrame::NoFrame, Properties::PANEL);

    QScrollBar* scrollBar = builder.live(QStringLiteral("QScrollBar"), new QScrollBar(Qt::Vertical, builder.page()));
    scrollBar->setRange(0, 150);
    scrollBar->setPageStep(100);
    scrollBar->setFixedHeight(180);
    QScrollBar* disabledScrollBar = builder.live(QStringLiteral("Disabled"), new QScrollBar(Qt::Vertical, builder.page()));
    disabledScrollBar->setRange(0, 150);
    disabledScrollBar->setPageStep(100);
    disabledScrollBar->setEnabled(false);
    disabledScrollBar->setFixedHeight(180);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createMenusPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("10-menus-menu-bar"), QStringLiteral("Menus and menu bar"), QStringLiteral("list"));

    builder.block(QStringLiteral("New - complete menu (rendered popup, the row \"Print\" is hovered)"), QStringLiteral("619:1467"));
    {
        QMenu* menu = GalleryHelper::createSampleMenu(builder.page());
        menu->ensurePolished();
        const QSize size = menu->sizeHint();

        builder.specimen(QStringLiteral("Section title, shortcuts, submenu, checked, exclusive, disabled, destructive"), size, [menu = QPointer<QMenu>(menu)](QPainter& painter, const QRect& rect)
        {
            // The menu is a child of the page, it may have been destroyed
            if (!menu)
            {
                return;
            }
            menu->ensurePolished();
            menu->resize(menu->sizeHint());
            const QList<QAction*> actions = menu->actions();
            if (actions.size() > 3)
            {
                menu->setActiveAction(actions[3]);
            }
            // The menu is a window of its own, it is rendered into an image first
            const qreal scale = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
            QImage image(menu->size() * scale, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(scale);
            image.fill(Qt::transparent);
            {
                QPainter menuPainter(&image);
                menu->render(&menuPainter, QPoint(), QRegion(), QWidget::DrawChildren);
            }
            painter.drawImage(rect.topLeft(), image);
        }, QStringLiteral("619:1471"));
    }

    builder.block(QStringLiteral("New - menu bar (live): open the menus by the mouse or by Alt and the arrows"), QStringLiteral("619:1601"));
    QMenuBar* menuBar = new QMenuBar(builder.page());
    for (const char* title : { "&File", "&Edit", "&View", "&Tools", "&Help" })
    {
        QMenu* menu = GalleryHelper::createSampleMenu(menuBar);
        menu->setTitle(QString::fromLatin1(title));
        menuBar->addMenu(menu);
    }
    menuBar->setFixedWidth(720);
    builder.live(QStringLiteral("QMenuBar"), menuBar, QStringLiteral("619:1616"));

    builder.block(QStringLiteral("Live menus"), QStringLiteral("619:1385"));
    QPushButton* contextButton = builder.live(QStringLiteral("Context menu"), new QPushButton(QStringLiteral("Open the menu"), builder.page()));
    contextButton->setAutoDefault(false);
    contextButton->setMenu(GalleryHelper::createSampleMenu(contextButton));

    QPushButton* longButton = builder.live(QStringLiteral("Menu with scrollers"), new QPushButton(QStringLiteral("Languages"), builder.page()));
    longButton->setAutoDefault(false);
    QMenu* longMenu = new QMenu(longButton);
    for (int i = 0; i < 80; ++i)
    {
        longMenu->addAction(QStringLiteral("Language %1").arg(i + 1));
    }
    longButton->setMenu(longMenu);

    QPushButton* popoverButton = builder.live(QStringLiteral("Popover menu (property)"), new QPushButton(QStringLiteral("Popover"), builder.page()));
    popoverButton->setAutoDefault(false);
    QMenu* popoverMenu = GalleryHelper::createSampleMenu(popoverButton);
    popoverMenu->setProperty(Properties::POPOVER, true);
    popoverButton->setMenu(popoverMenu);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createTabsPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("11-tabs"), QStringLiteral("Tabs"), QStringLiteral("tab"));
    QStyle* style = builder.style();

    QTabBar* chipPrototype = new QTabBar(builder.page());
    chipPrototype->hide();
    QTabBar* navPrototype = new QTabBar(builder.page());
    navPrototype->setShape(QTabBar::RoundedWest);
    navPrototype->hide();
    QTabBar* documentPrototype = new QTabBar(builder.page());
    documentPrototype->setProperty(Properties::DOCUMENT_TABS, true);
    documentPrototype->hide();

    auto tabSpecimen = [&builder, style](const QString& caption, const QString& text, const char* iconName, StateFlags flags, QTabBar::Shape shape, QTabBar* prototype, const QString& node)
    {
        QStyleOptionTab option;
        GalleryHelper::initOption(option, QRect(), flags);
        option.shape = shape;
        option.text = text;
        option.tabIndex = -1;
        if (iconName)
        {
            option.icon = GalleryHelper::icon(iconName);
            option.iconSize = QSize(14, 14);
        }

        const QSize size = style->sizeFromContents(QStyle::CT_TabBarTab, &option, QSize(), prototype);

        builder.specimen(caption, size, [option, prototype = QPointer<QWidget>(prototype)](QPainter& painter, const QRect& rect) mutable
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            option.rect = rect;
            style->drawControl(QStyle::CE_TabBarTab, &option, &painter, prototype);
        }, node);
    };

    builder.block(QStringLiteral("New - horizontal tab bar = Filter chip (tab rectangle with the margin of the focus ring)"), QStringLiteral("636:1833"));
    tabSpecimen(QStringLiteral("Default"), QStringLiteral("Details"), nullptr, Default, QTabBar::RoundedNorth, chipPrototype, QStringLiteral("636:1844"));
    tabSpecimen(QStringLiteral("Hover"), QStringLiteral("Warnings"), nullptr, Hover, QTabBar::RoundedNorth, chipPrototype, QStringLiteral("636:1850"));
    tabSpecimen(QStringLiteral("Pressed"), QStringLiteral("Details"), nullptr, Pressed | Hover, QTabBar::RoundedNorth, chipPrototype, QStringLiteral("636:1864"));
    tabSpecimen(QStringLiteral("Focus (keyboard)"), QStringLiteral("Details"), nullptr, Focus, QTabBar::RoundedNorth, chipPrototype, QStringLiteral("636:1873"));
    tabSpecimen(QStringLiteral("Selected"), QStringLiteral("Summary"), nullptr, Selected, QTabBar::RoundedNorth, chipPrototype, QStringLiteral("636:1838"));
    tabSpecimen(QStringLiteral("Selected / Hover"), QStringLiteral("Summary"), nullptr, Selected | Hover, QTabBar::RoundedNorth, chipPrototype, QStringLiteral("636:1881"));
    tabSpecimen(QStringLiteral("Selected / Focus"), QStringLiteral("Summary"), nullptr, Selected | Focus, QTabBar::RoundedNorth, chipPrototype, QStringLiteral("636:1890"));
    tabSpecimen(QStringLiteral("With icon 14"), QStringLiteral("Signatures"), "signature", Default, QTabBar::RoundedNorth, chipPrototype, QStringLiteral("636:1899"));
    tabSpecimen(QStringLiteral("Disabled"), QStringLiteral("Details"), nullptr, Disabled, QTabBar::RoundedNorth, chipPrototype, QStringLiteral("636:1916"));

    builder.block(QStringLiteral("New - vertical tab bar = Nav item"), QStringLiteral("636:1926"));
    tabSpecimen(QStringLiteral("Selected"), QStringLiteral("Properties"), "info", Selected, QTabBar::RoundedWest, navPrototype, QStringLiteral("636:1931"));
    tabSpecimen(QStringLiteral("Default"), QStringLiteral("Security"), "lock", Default, QTabBar::RoundedWest, navPrototype, QStringLiteral("636:1937"));
    tabSpecimen(QStringLiteral("Hover"), QStringLiteral("Fonts"), "type", Hover, QTabBar::RoundedWest, navPrototype, QStringLiteral("636:1943"));
    builder.row();
    tabSpecimen(QStringLiteral("Pressed"), QStringLiteral("Display and print"), "printer", Pressed | Hover, QTabBar::RoundedWest, navPrototype, QStringLiteral("636:1949"));
    tabSpecimen(QStringLiteral("Focus"), QStringLiteral("XMP metadata"), "code", Focus, QTabBar::RoundedWest, navPrototype, QStringLiteral("636:1955"));
    tabSpecimen(QStringLiteral("Disabled"), QStringLiteral("Layers"), "layers", Disabled, QTabBar::RoundedWest, navPrototype, QStringLiteral("636:1961"));

    builder.block(QStringLiteral("Existing - Components / Tab (document tabs), Tab close button"), QStringLiteral("620:1455"));
    tabSpecimen(QStringLiteral("Tab / Selected"), QStringLiteral("Project_Report.pdf"), "file-text", Selected, QTabBar::RoundedNorth, documentPrototype, QStringLiteral("620:1459"));
    tabSpecimen(QStringLiteral("Tab / Default"), QStringLiteral("Invoice.pdf"), "file-text", Default, QTabBar::RoundedNorth, documentPrototype, QStringLiteral("620:1474"));
    tabSpecimen(QStringLiteral("Tab / Hover"), QStringLiteral("Invoice.pdf"), "file-text", Hover, QTabBar::RoundedNorth, documentPrototype, QStringLiteral("620:1489"));
    tabSpecimen(QStringLiteral("Tab / Focus"), QStringLiteral("Invoice.pdf"), "file-text", Focus, QTabBar::RoundedNorth, documentPrototype, QStringLiteral("620:1455"));
    for (const GalleryHelper::NamedState& state : { GalleryHelper::POINTER_STATES[0], GalleryHelper::POINTER_STATES[1], GalleryHelper::POINTER_STATES[2] })
    {
        const StateFlags flags(state.flags);
        builder.specimen(QStringLiteral("Tab close button / %1").arg(QString::fromLatin1(state.name)), QSize(20, 20), [flags](QPainter& painter, const QRect& rect)
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            QStyleOption option;
            GalleryHelper::initOption(option, rect, flags);
            style->drawPrimitive(QStyle::PE_IndicatorTabClose, &option, &painter, nullptr);
        });
    }

    builder.block(QStringLiteral("Live tab bars"), QStringLiteral("636:1968"));

    // Top bar of the application (app-top-bar of the document view): the home
    // tab, the tabs of the documents and the button of a new tab, which follows
    // the last tab. The tabs switch the document shown below the bar.
    QWidget* application = new QWidget(builder.page());
    QVBoxLayout* applicationLayout = new QVBoxLayout(application);
    applicationLayout->setContentsMargins(0, 0, 0, 0);
    applicationLayout->setSpacing(0);

    QWidget* topBar = new QWidget(application);
    topBar->setProperty(Properties::DOCUMENT_TABS, true);
    topBar->setFixedHeight(38);
    QHBoxLayout* topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(16, 6, 16, 0);
    topBarLayout->setSpacing(8);

    QTabBar* documentTabs = new QTabBar(topBar);
    documentTabs->setProperty(Properties::DOCUMENT_TABS, true);
    documentTabs->setAccessibleName(QStringLiteral("Documents"));
    documentTabs->setTabsClosable(true);
    documentTabs->setMovable(true);

    // The tab bar is not wider than its tabs, the button of a new tab follows the last tab
    documentTabs->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    documentTabs->addTab(GalleryHelper::icon("home"), QStringLiteral("Home"));
    for (const char* name : { "Project_Report.pdf", "Invoice.pdf", "Tender_2026.pdf", "Annex_A.pdf", "Contract_final_signed.pdf", "Minutes_2026-10-14.pdf" })
    {
        documentTabs->addTab(GalleryHelper::icon("file-text"), QString::fromLatin1(name));
    }

    // The home tab can not be closed
    if (QWidget* closeButton = documentTabs->tabButton(0, QTabBar::RightSide))
    {
        documentTabs->setTabButton(0, QTabBar::RightSide, nullptr);
        closeButton->deleteLater();
    }
    documentTabs->setCurrentIndex(1);
    topBarLayout->addWidget(documentTabs, 0, Qt::AlignBottom);

    QToolButton* newTabButton = new QToolButton(topBar);
    newTabButton->setProperty(Properties::NEW_TAB, true);
    newTabButton->setIcon(GalleryHelper::icon("plus"));
    newTabButton->setAccessibleName(QStringLiteral("New tab"));
    newTabButton->setToolTip(QStringLiteral("New tab"));
    topBarLayout->addWidget(newTabButton, 0, Qt::AlignBottom);
    topBarLayout->addStretch(1);
    applicationLayout->addWidget(topBar);

    QFrame* documentView = new QFrame(application);
    documentView->setProperty(Properties::CANVAS, true);
    QVBoxLayout* documentLayout = new QVBoxLayout(documentView);
    documentLayout->setContentsMargins(24, 20, 24, 20);
    documentLayout->setSpacing(4);
    pdf::PDFLabel* documentTitle = new pdf::PDFLabel(QString(), Font::Title, Color::TextPrimary, documentView);
    pdf::PDFLabel* documentHint = new pdf::PDFLabel(QStringLiteral("Switch the documents by the tabs, drag the tabs, close them or open a new tab by the plus button. Tabs, which do not fit, are scrolled."),
                                                    Font::Body, Color::TextSecondary, documentView);
    documentHint->setWordWrap(true);
    documentLayout->addWidget(documentTitle);
    documentLayout->addWidget(documentHint);
    documentLayout->addStretch(1);
    applicationLayout->addWidget(documentView, 1);

    auto showDocument = [documentTabs, documentTitle]()
    {
        documentTitle->setText(documentTabs->tabText(documentTabs->currentIndex()));
    };
    QObject::connect(documentTabs, &QTabBar::currentChanged, documentTitle, showDocument);
    QObject::connect(documentTabs, &QTabBar::tabCloseRequested, documentTabs, [documentTabs](int index) { documentTabs->removeTab(index); });
    QObject::connect(newTabButton, &QToolButton::clicked, documentTabs, [documentTabs, counter = 0]() mutable
    {
        documentTabs->setCurrentIndex(documentTabs->addTab(GalleryHelper::icon("file-text"), QStringLiteral("Untitled %1.pdf").arg(++counter)));
    });
    showDocument();

    application->setFixedSize(860, 170);
    builder.live(QStringLiteral("Top bar of the application - Home, document tabs (closable, movable, scrolled) and the New tab button"), application, QStringLiteral("620:1455"));

    builder.row();
    QTabWidget* chipsWidget = new QTabWidget(builder.page());
    for (const char* name : { "Summary", "Details", "Warnings", "Certificates" })
    {
        QWidget* page = new QWidget(chipsWidget);
        QVBoxLayout* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pdf::PDFLabel* label = new pdf::PDFLabel(QStringLiteral("The document contains 3 signatures. Two are valid, one needs attention: the certificate of “Jakub Melka” expired on 12 March 2026. (%1)").arg(QString::fromLatin1(name)), Font::Body, Color::TextSecondary, page);
        label->setWordWrap(true);
        pageLayout->addWidget(label);
        pageLayout->addStretch(1);
        chipsWidget->addTab(page, QString::fromLatin1(name));
    }
    chipsWidget->tabBar()->setTabData(2, 3);
    chipsWidget->setTabIcon(3, GalleryHelper::icon("signature"));
    chipsWidget->setTabEnabled(1, true);
    chipsWidget->setFixedSize(460, 150);
    builder.live(QStringLiteral("QTabWidget - frameless pane, count badge, icon"), chipsWidget, QStringLiteral("636:1972"));

    QTabWidget* cardWidget = new QTabWidget(builder.page());
    cardWidget->setProperty(Properties::CARD, true);
    for (const char* name : { "Light", "Dark", "Custom" })
    {
        QWidget* page = new QWidget(cardWidget);
        QVBoxLayout* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->addWidget(new pdf::PDFLabel(QStringLiteral("Settings of the theme %1").arg(QString::fromLatin1(name)), Font::Body, Color::TextSecondary, page));
        pageLayout->addStretch(1);
        cardWidget->addTab(page, QString::fromLatin1(name));
    }
    cardWidget->setTabEnabled(2, false);
    cardWidget->setFixedSize(420, 150);
    builder.live(QStringLiteral("QTabWidget with property \"card\", disabled tab"), cardWidget, QStringLiteral("636:1998"));

    builder.row();
    QTabWidget* westWidget = new QTabWidget(builder.page());
    westWidget->setTabPosition(QTabWidget::West);
    struct NavTab
    {
        const char* name;
        const char* icon;
    };
    for (const NavTab& tab : { NavTab{ "Properties", "info" }, NavTab{ "Security", "lock" }, NavTab{ "Fonts", "type" }, NavTab{ "Display and print", "printer" }, NavTab{ "Layers", "layers" } })
    {
        QWidget* page = new QWidget(westWidget);
        QVBoxLayout* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->addWidget(new pdf::PDFLabel(QString::fromLatin1(tab.name), Font::Title, Color::TextPrimary, page));
        pageLayout->addStretch(1);
        westWidget->addTab(page, GalleryHelper::icon(tab.icon), QString::fromLatin1(tab.name));
    }
    westWidget->setTabEnabled(4, false);
    westWidget->setFixedSize(520, 270);
    builder.live(QStringLiteral("QTabWidget West - use the up and down arrows"), westWidget, QStringLiteral("636:1930"));

    QTabWidget* southWidget = new QTabWidget(builder.page());
    southWidget->setTabPosition(QTabWidget::South);
    for (const char* name : { "Pages", "Thumbnails" })
    {
        QWidget* page = new QWidget(southWidget);
        QVBoxLayout* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->addWidget(new pdf::PDFLabel(QStringLiteral("Content above the chips"), Font::Body, Color::TextSecondary, page));
        pageLayout->addStretch(1);
        southWidget->addTab(page, QString::fromLatin1(name));
    }
    southWidget->setFixedSize(300, 150);
    builder.live(QStringLiteral("QTabWidget South"), southWidget, QStringLiteral("636:1968"));

    builder.row();
    // The theme toggle of the design: segments Light / Dark / Auto of equal width,
    // the sun of the Light segment has the color icon/sun in every state
    pdf::PDFSegmentedControl* segmented = new pdf::PDFSegmentedControl(builder.page());
    segmented->setAccessibleName(QStringLiteral("Theme"));
    const int lightSegment = segmented->addSegment(QStringLiteral("Light"), QStringLiteral("sun"));
    segmented->addSegment(QStringLiteral("Dark"), QStringLiteral("moon"));
    const int autoSegment = segmented->addSegment(QStringLiteral("Auto"), QStringLiteral("monitor"));
    segmented->setSegmentIconColor(lightSegment, Color::IconSun);
    segmented->setCurrentIndex(autoSegment);
    segmented->setFixedWidth(182);
    builder.live(QStringLiteral("Segmented control (theme toggle)"), segmented, QStringLiteral("47:157"));

    pdf::PDFSegmentedControl* twoSegments = new pdf::PDFSegmentedControl(builder.page());
    twoSegments->setAccessibleName(QStringLiteral("Theme, light selected"));
    const int selectedLight = twoSegments->addSegment(QStringLiteral("Light"), QStringLiteral("sun"));
    twoSegments->addSegment(QStringLiteral("Dark"), QStringLiteral("moon"));
    twoSegments->setSegmentIconColor(selectedLight, Color::IconSun);
    twoSegments->setFixedWidth(182);
    builder.live(QStringLiteral("Segmented control with two segments"), twoSegments, QStringLiteral("620:1544"));

    // A single choice of a few options is a segmented control of segments without icons,
    // all as wide as the widest one (Compress presets of the Optimize tool)
    pdf::PDFSegmentedControl* presets = new pdf::PDFSegmentedControl(builder.page());
    presets->setAccessibleName(QStringLiteral("Preset"));
    for (const char* name : { "Smaller file", "Balanced", "Best quality", "Custom\xE2\x80\xA6" })
    {
        presets->addSegment(QString::fromUtf8(name));
    }
    presets->setCurrentIndex(1);
    builder.live(QStringLiteral("Segmented control of options (presets)"), presets, QStringLiteral("47:157"));

    QWidget* chipRow = new QWidget(builder.page());
    QHBoxLayout* chipLayout = new QHBoxLayout(chipRow);
    chipLayout->setContentsMargins(2, 2, 2, 2);
    chipLayout->setSpacing(6);
    QButtonGroup* chipGroup = new QButtonGroup(chipRow);
    for (const char* name : { "All", "Files", "Folders" })
    {
        QPushButton* chip = new QPushButton(QString::fromLatin1(name), chipRow);
        chip->setProperty(Properties::CHIP, true);
        chip->setCheckable(true);
        chip->setAutoDefault(false);
        chip->setChecked(qstrcmp(name, "All") == 0);
        chipGroup->addButton(chip);
        chipLayout->addWidget(chip);
    }
    builder.live(QStringLiteral("Filter chips (checkable buttons with property \"chip\")"), chipRow, QStringLiteral("620:1556"));

    // On / off options: chips with the property "option" show the check mark instead
    // of their icon, when they are on
    QWidget* optionRow = new QWidget(builder.page());
    QHBoxLayout* optionLayout = new QHBoxLayout(optionRow);
    optionLayout->setContentsMargins(2, 2, 2, 2);
    optionLayout->setSpacing(6);
    for (const auto& option : { std::pair<const char*, const char*>{ "Match case", "match-case" }, { "Whole words", "whole-word" } })
    {
        QPushButton* chip = new QPushButton(QString::fromLatin1(option.first), optionRow);
        chip->setProperty(Properties::CHIP, true);
        chip->setProperty(Properties::OPTION, true);
        chip->setCheckable(true);
        chip->setAutoDefault(false);
        chip->setIcon(pdf::PDFThemeIcons::getIcon(QString::fromLatin1(option.second)));
        chip->setChecked(qstrcmp(option.first, "Whole words") == 0);
        optionLayout->addWidget(chip);
    }
    builder.live(QStringLiteral("Option chips (Whole words on)"), optionRow, QStringLiteral("198:4934"));

    return builder.finish();
}

GalleryPages::Page GalleryPages::createItemViewsPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("12-item-views"), QStringLiteral("Item views"), QStringLiteral("table"));

    builder.block(QStringLiteral("New - generic rows of a list view"), QStringLiteral("621:1521"));
    pdf::PDFListView* listView = new pdf::PDFListView(builder.page());
    listView->setModel(GalleryHelper::createFilesModel(listView, false));
    listView->setItemDelegate(new pdf::PDFItemDelegate(listView));
    listView->setCurrentIndex(listView->model()->index(2, 0));
    listView->setFixedSize(288, 206);
    builder.live(QStringLiteral("PDFListView - hover, selected, current, disabled, editing by F2"), listView, QStringLiteral("621:1525"));

    pdf::PDFListView* alternateView = new pdf::PDFListView(builder.page());
    alternateView->setModel(GalleryHelper::createFilesModel(alternateView, true));
    alternateView->setAlternatingRowColors(true);
    alternateView->setDragDropMode(QAbstractItemView::InternalMove);
    alternateView->setFixedSize(288, 206);
    builder.live(QStringLiteral("Alternating rows, check boxes, drag and drop"), alternateView, QStringLiteral("621:1559"));

    QFrame* sidebar = new QFrame(builder.page());
    sidebar->setProperty(Properties::PANEL, true);
    QVBoxLayout* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 1, 0);
    pdf::PDFListView* sidebarView = new pdf::PDFListView(sidebar);
    sidebarView->setFrameShape(QFrame::NoFrame);
    sidebarView->setModel(GalleryHelper::createFilesModel(sidebarView, false));
    sidebarLayout->addWidget(sidebarView);
    sidebar->setFixedSize(288, 206);
    builder.live(QStringLiteral("Frameless view in a sidebar (bg/sidebar)"), sidebar, QStringLiteral("621:1574"));

    builder.block(QStringLiteral("New - tree view branches and check column"), QStringLiteral("621:1590"));
    pdf::PDFTreeView* treeView = new pdf::PDFTreeView(builder.page());
    treeView->setModel(GalleryHelper::createTreeModel(treeView));
    treeView->setHeaderHidden(true);
    treeView->expandAll();
    treeView->setFixedSize(300, 236);
    builder.live(QStringLiteral("PDFTreeView"), treeView, QStringLiteral("621:1594"));

    QListView* iconView = new pdf::PDFListView(builder.page());
    iconView->setViewMode(QListView::IconMode);
    iconView->setIconSize(QSize(48, 48));
    iconView->setResizeMode(QListView::Adjust);
    iconView->setMovement(QListView::Static);
    iconView->setModel(GalleryHelper::createFilesModel(iconView, false));
    iconView->setFixedSize(380, 236);
    builder.live(QStringLiteral("Icon mode"), iconView, QStringLiteral("621:1714"));

    builder.block(QStringLiteral("New - table view: cells, grid, header, editing"), QStringLiteral("621:1656"));
    QTableView* tableView = new QTableView(builder.page());
    tableView->setModel(GalleryHelper::createFilesModel(tableView, false));
    tableView->setItemDelegate(new pdf::PDFItemDelegate(tableView));
    tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView->setSortingEnabled(true);
    tableView->sortByColumn(0, Qt::AscendingOrder);
    tableView->verticalHeader()->hide();
    tableView->setColumnWidth(0, 220);
    tableView->horizontalHeader()->setStretchLastSection(true);
    tableView->selectRow(2);
    tableView->setFixedSize(540, 220);
    builder.live(QStringLiteral("QTableView - sortable, rows selected, cells edited by PDFItemDelegate"), tableView, QStringLiteral("621:1660"));

    QTableView* cellTable = new QTableView(builder.page());
    cellTable->setModel(GalleryHelper::createFilesModel(cellTable, false));
    cellTable->setColumnWidth(0, 160);
    cellTable->setFixedSize(480, 220);
    builder.live(QStringLiteral("QTableView with the vertical header and the corner button"), cellTable, QStringLiteral("622:1603"));

    // A cell of each type, which PDFItemEditorFactory edits by a widget of the
    // library (a boolean, numbers, a date, a time, a date and time, a text)
    builder.row();
    QStandardItemModel* typedModel = new QStandardItemModel(0, 8);
    typedModel->setHorizontalHeaderLabels({ QStringLiteral("Bool"), QStringLiteral("Int"), QStringLiteral("UInt"), QStringLiteral("Double"),
                                            QStringLiteral("Date"), QStringLiteral("Time"), QStringLiteral("Date and time"), QStringLiteral("Text") });
    for (int row = 0; row < 2; ++row)
    {
        const QList<QVariant> values = { QVariant(row == 0), QVariant(42 + row), QVariant(uint(7 + row)), QVariant(2.5 + row),
                                         QVariant(QDate(2026, 10, 18 + row)), QVariant(QTime(9, 30 + row)),
                                         QVariant(QDateTime(QDate(2026, 10, 18 + row), QTime(9, 30))), QVariant(QStringLiteral("Report %1").arg(row + 1)) };
        QList<QStandardItem*> items;
        for (const QVariant& value : values)
        {
            QStandardItem* item = new QStandardItem();
            item->setData(value, Qt::EditRole);
            items << item;
        }
        typedModel->appendRow(items);
    }
    QTableView* typedTable = new QTableView(builder.page());
    typedTable->setModel(typedModel);
    typedModel->setParent(typedTable);
    typedTable->setItemDelegate(new pdf::PDFItemDelegate(typedTable));
    typedTable->verticalHeader()->hide();
    typedTable->setFixedSize(900, 120);
    builder.live(QStringLiteral("QTableView - an editor of each type of PDFItemEditorFactory (F2, double click)"), typedTable, QStringLiteral("621:1660"));

    return builder.finish();
}

GalleryPages::Page GalleryPages::createHeadersPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("13-header-view"), QStringLiteral("Header view"), QStringLiteral("columns"));
    QStyle* style = builder.style();

    auto headerSpecimen = [&builder, style](const QString& caption, const QString& text, StateFlags flags, QStyleOptionHeader::SortIndicator sortIndicator, Qt::Alignment alignment,
                                             Qt::Orientation orientation, QStyleOptionHeader::SectionPosition position, int width)
    {
        const QSize size = orientation == Qt::Horizontal ? QSize(width, 28) : QSize(40, 30);

        builder.specimen(caption, size, [text, flags, sortIndicator, alignment, orientation, position](QPainter& painter, const QRect& rect)
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            QStyleOptionHeader option;
            GalleryHelper::initOption(option, rect, flags);
            option.state &= ~QStyle::State_Off;
            option.text = text;
            option.sortIndicator = sortIndicator;
            option.textAlignment = alignment;
            option.orientation = orientation;
            option.position = position;
            if (orientation == Qt::Horizontal)
            {
                option.state |= QStyle::State_Horizontal;
            }
            style->drawControl(QStyle::CE_Header, &option, &painter, nullptr);
        });
    };

    const Qt::Alignment left = Qt::AlignLeft | Qt::AlignVCenter;
    const Qt::Alignment right = Qt::AlignRight | Qt::AlignVCenter;

    builder.block(QStringLiteral("New - horizontal header"), QStringLiteral("622:1562"));
    headerSpecimen(QStringLiteral("Sorted ascending"), QStringLiteral("Name"), Default, QStyleOptionHeader::SortDown, left, Qt::Horizontal, QStyleOptionHeader::Beginning, 200);
    headerSpecimen(QStringLiteral("Hover, right-aligned"), QStringLiteral("Pages"), Hover, QStyleOptionHeader::None, right, Qt::Horizontal, QStyleOptionHeader::Middle, 90);
    headerSpecimen(QStringLiteral("Pressed, right-aligned"), QStringLiteral("Size"), Hover | Pressed, QStyleOptionHeader::None, right, Qt::Horizontal, QStyleOptionHeader::Middle, 90);
    headerSpecimen(QStringLiteral("Default (last)"), QStringLiteral("Modified"), Default, QStyleOptionHeader::None, left, Qt::Horizontal, QStyleOptionHeader::End, 120);
    headerSpecimen(QStringLiteral("Sorted descending"), QStringLiteral("Pages"), Default, QStyleOptionHeader::SortUp, left, Qt::Horizontal, QStyleOptionHeader::Middle, 120);
    headerSpecimen(QStringLiteral("Selected column"), QStringLiteral("Size"), On | Pressed, QStyleOptionHeader::None, left, Qt::Horizontal, QStyleOptionHeader::End, 110);
    headerSpecimen(QStringLiteral("Sorted, right-aligned"), QStringLiteral("Pages"), Default, QStyleOptionHeader::SortDown, right, Qt::Horizontal, QStyleOptionHeader::Middle, 110);

    builder.block(QStringLiteral("New - vertical header"), QStringLiteral("622:1603"));
    headerSpecimen(QStringLiteral("Default"), QStringLiteral("1"), Default, QStyleOptionHeader::None, Qt::AlignCenter, Qt::Vertical, QStyleOptionHeader::Beginning, 40);
    headerSpecimen(QStringLiteral("Hover"), QStringLiteral("2"), Hover, QStyleOptionHeader::None, Qt::AlignCenter, Qt::Vertical, QStyleOptionHeader::Middle, 40);
    headerSpecimen(QStringLiteral("Selected"), QStringLiteral("3"), On | Pressed, QStyleOptionHeader::None, Qt::AlignCenter, Qt::Vertical, QStyleOptionHeader::End, 40);

    builder.block(QStringLiteral("Live table with both headers (sortable, movable sections)"), QStringLiteral("622:1603"));
    QTableView* tableView = new QTableView(builder.page());
    tableView->setModel(GalleryHelper::createFilesModel(tableView, false));
    tableView->setSortingEnabled(true);
    tableView->horizontalHeader()->setSectionsMovable(true);
    tableView->setColumnWidth(0, 180);
    tableView->setFixedSize(560, 230);
    builder.live(QStringLiteral("QTableView"), tableView);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createGroupBoxesPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("14-group-box-frames"), QStringLiteral("Group boxes and frames"), QStringLiteral("group"));

    auto createGroupBox = [&builder](const QString& caption, const QString& title, const QString& node)
    {
        QGroupBox* groupBox = new QGroupBox(title, builder.page());
        QVBoxLayout* layout = new QVBoxLayout(groupBox);
        layout->addWidget(new pdf::PDFLabel(QStringLiteral("Text"), Font::Body, Color::TextPrimary, groupBox));
        pdf::PDFLineEdit* edit = new pdf::PDFLineEdit(QStringLiteral("DRAFT"), groupBox);
        layout->addWidget(edit);
        layout->addWidget(new QCheckBox(QStringLiteral("Also on the first page"), groupBox));
        groupBox->setFixedWidth(320);
        builder.live(caption, groupBox, node);
        return groupBox;
    };

    builder.block(QStringLiteral("New - QGroupBox variants"), QStringLiteral("623:1569"));
    createGroupBox(QStringLiteral("Plain"), QStringLiteral("Output"), QStringLiteral("623:1569"));
    QGroupBox* checkedBox = createGroupBox(QStringLiteral("Checkable / checked"), QStringLiteral("Add watermark"), QStringLiteral("623:1569"));
    checkedBox->setCheckable(true);
    checkedBox->setChecked(true);
    QGroupBox* uncheckedBox = createGroupBox(QStringLiteral("Checkable / unchecked (content disabled)"), QStringLiteral("Add watermark"), QStringLiteral("623:1569"));
    uncheckedBox->setCheckable(true);
    uncheckedBox->setChecked(false);
    builder.row();
    QGroupBox* flatBox = createGroupBox(QStringLiteral("Flat"), QStringLiteral("Advanced"), QStringLiteral("623:1569"));
    flatBox->setFlat(true);

    QGroupBox* cardBox = new QGroupBox(QStringLiteral("Documents"), builder.page());
    cardBox->setProperty(Properties::CARD, true);
    QVBoxLayout* cardLayout = new QVBoxLayout(cardBox);
    cardLayout->setSpacing(0);
    for (const char* text : { "Open the last documents at start", "Remember the last page of a document" })
    {
        QWidget* row = new QWidget(cardBox);
        QHBoxLayout* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(16, 11, 16, 11);
        rowLayout->addWidget(new pdf::PDFLabel(QString::fromLatin1(text), Font::Body, Color::TextPrimary, row), 1);
        QCheckBox* checkBox = new QCheckBox(row);
        checkBox->setChecked(true);
        checkBox->setAccessibleName(QString::fromLatin1(text));
        rowLayout->addWidget(checkBox);
        cardLayout->addWidget(row);

        if (qstrcmp(text, "Open the last documents at start") == 0)
        {
            QFrame* divider = new QFrame(cardBox);
            divider->setFrameShape(QFrame::HLine);
            divider->setFixedHeight(1);
            cardLayout->addWidget(divider);
        }
    }
    cardBox->setFixedWidth(420);
    builder.live(QStringLiteral("Card (property \"card\")"), cardBox, QStringLiteral("623:1569"));

    QGroupBox* disabledBox = createGroupBox(QStringLiteral("Disabled"), QStringLiteral("Output"), QStringLiteral("623:1569"));
    disabledBox->setEnabled(false);

    builder.block(QStringLiteral("New - QFrame shapes and boxes"), QStringLiteral("623:1666"));
    auto createFrame = [&builder](const QString& caption, const QString& text, QFrame::Shape shape, const char* property)
    {
        QFrame* frame = new QFrame(builder.page());
        frame->setFrameShape(shape);
        if (property)
        {
            frame->setProperty(property, true);
        }
        QVBoxLayout* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->addWidget(new pdf::PDFLabel(text, Font::Small, Color::TextMuted, frame), 0, Qt::AlignCenter);
        frame->setFixedSize(200, 72);
        builder.live(caption, frame);
    };

    createFrame(QStringLiteral("StyledPanel"), QStringLiteral("1 px border/default, r 8"), QFrame::StyledPanel, nullptr);
    createFrame(QStringLiteral("Box"), QStringLiteral("1 px border/default, r 8"), QFrame::Box, nullptr);
    createFrame(QStringLiteral("Property \"card\""), QStringLiteral("bg/surface, border, r 12"), QFrame::NoFrame, Properties::CARD);
    createFrame(QStringLiteral("Property \"note\""), QStringLiteral("bg/subtle, r 10"), QFrame::NoFrame, Properties::NOTE);
    createFrame(QStringLiteral("Property \"panel\""), QStringLiteral("bg/sidebar, border right"), QFrame::NoFrame, Properties::PANEL);

    builder.row();
    QFrame* horizontalLine = new QFrame(builder.page());
    horizontalLine->setFrameShape(QFrame::HLine);
    horizontalLine->setFixedSize(160, 9);
    builder.live(QStringLiteral("HLine (border/divider)"), horizontalLine);
    QFrame* verticalLine = new QFrame(builder.page());
    verticalLine->setFrameShape(QFrame::VLine);
    verticalLine->setFixedSize(9, 40);
    builder.live(QStringLiteral("VLine (border/separator)"), verticalLine);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createContainersPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("15-containers"), QStringLiteral("Splitter, docks, status bar"), QStringLiteral("panel-left"));

    builder.block(QStringLiteral("New - splitter handle: rest the pointer over the handle or drag it"), QStringLiteral("625:1582"));
    auto createSplitter = [&builder](const QString& caption, Qt::Orientation orientation)
    {
        QSplitter* splitter = new QSplitter(orientation, builder.page());
        QFrame* sidebar = new QFrame(splitter);
        sidebar->setProperty(Properties::PANEL, true);
        QFrame* canvas = new QFrame(splitter);
        canvas->setProperty(Properties::NOTE, true);
        splitter->addWidget(sidebar);
        splitter->addWidget(canvas);
        splitter->setFixedSize(220, 130);
        builder.live(caption, splitter);
    };
    createSplitter(QStringLiteral("Horizontal"), Qt::Horizontal);
    createSplitter(QStringLiteral("Vertical"), Qt::Vertical);

    builder.block(QStringLiteral("New - dock widget title bar and floating frame"), QStringLiteral("625:1617"));
    QMainWindow* dockHost = new QMainWindow(builder.page());
    dockHost->setWindowFlags(Qt::Widget);
    QDockWidget* dockWidget = new QDockWidget(QStringLiteral("Object inspector"), dockHost);
    QWidget* dockContent = new QWidget(dockWidget);
    QVBoxLayout* dockLayout = new QVBoxLayout(dockContent);
    dockLayout->addWidget(new pdf::PDFLabel(QStringLiteral("Content of the dock widget"), Font::Body, Color::TextSecondary, dockContent));
    dockLayout->addStretch(1);
    dockWidget->setWidget(dockContent);
    dockHost->addDockWidget(Qt::LeftDockWidgetArea, dockWidget);
    QFrame* dockCanvas = new QFrame(dockHost);
    dockCanvas->setProperty(Properties::NOTE, true);
    dockHost->setCentralWidget(dockCanvas);
    dockHost->setFixedSize(520, 170);
    builder.live(QStringLiteral("QDockWidget in a QMainWindow (float it by its button)"), dockHost, QStringLiteral("625:1621"));

    builder.block(QStringLiteral("New - status bar, items and size grip"), QStringLiteral("625:1686"));
    pdf::PDFStatusBar* statusBar = new pdf::PDFStatusBar(builder.page());
    statusBar->showMessage(QStringLiteral("Ready"));
    statusBar->createItem(QStringLiteral("308 / 1,592"));
    statusBar->createItem(QStringLiteral("151 %"));
    statusBar->createItem(QStringLiteral("3 rendering issues"), QStringLiteral("info"), true);
    statusBar->setFixedWidth(760);
    builder.live(QStringLiteral("PDFStatusBar - message, items (one Warning), size grip area"), statusBar, QStringLiteral("625:1690"));

    builder.block(QStringLiteral("New - rubber band"), QStringLiteral("625:1708"));
    QFrame* rubberHost = new QFrame(builder.page());
    rubberHost->setProperty(Properties::NOTE, true);
    rubberHost->setFixedSize(260, 150);
    QRubberBand* rubberBand = new QRubberBand(QRubberBand::Rectangle, rubberHost);
    rubberBand->setGeometry(40, 48, 120, 64);
    rubberBand->show();
    QRubberBand* lineBand = new QRubberBand(QRubberBand::Line, rubberHost);
    lineBand->setGeometry(200, 0, 2, 150);
    lineBand->show();
    builder.live(QStringLiteral("Rectangle and line rubber bands"), rubberHost, QStringLiteral("625:1719"));

    builder.block(QStringLiteral("New - tool box (QToolBox)"), QStringLiteral("625:1730"));
    QToolBox* toolBox = new QToolBox(builder.page());
    struct ToolBoxPage
    {
        const char* name;
        const char* icon;
    };
    for (const ToolBoxPage& toolBoxPage : { ToolBoxPage{ "Document", "file-text" }, ToolBoxPage{ "Fonts", "font" }, ToolBoxPage{ "Security", "lock" } })
    {
        QWidget* page = new QWidget(toolBox);
        QVBoxLayout* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(12, 12, 12, 12);
        pageLayout->addWidget(new pdf::PDFLabel(QStringLiteral("Page %1").arg(QString::fromLatin1(toolBoxPage.name)), Font::Body, Color::TextSecondary, page));
        pageLayout->addStretch(1);
        toolBox->addItem(page, GalleryHelper::icon(toolBoxPage.icon), QString::fromLatin1(toolBoxPage.name));
    }
    toolBox->setCurrentIndex(1);
    toolBox->setFixedSize(280, 200);
    builder.live(QStringLiteral("QToolBox - Fonts open"), toolBox, QStringLiteral("625:1734"));

    return builder.finish();
}

GalleryPages::Page GalleryPages::createDialogsPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("16-tooltips-dialogs"), QStringLiteral("Tooltips, focus frame, dialogs"), QStringLiteral("message-square"));

    builder.block(QStringLiteral("New - tooltips: rest the pointer over the buttons"), QStringLiteral("626:1604"));
    QPushButton* plainTip = builder.live(QStringLiteral("Plain"), new QPushButton(GalleryHelper::icon("rotate-cw"), QStringLiteral("Rotate"), builder.page()));
    plainTip->setAutoDefault(false);
    plainTip->setToolTip(QStringLiteral("Rotate right"));
    QPushButton* shortcutTip = builder.live(QStringLiteral("With shortcut"), new QPushButton(GalleryHelper::icon("rotate-cw"), QStringLiteral("Rotate"), builder.page()));
    shortcutTip->setAutoDefault(false);
    shortcutTip->setShortcut(QKeySequence(Qt::Key_F4));
    shortcutTip->setToolTip(QStringLiteral("Rotate right"));
    QPushButton* richTip = builder.live(QStringLiteral("Rich - description"), new QPushButton(GalleryHelper::icon("save"), QStringLiteral("Checkpoint"), builder.page()));
    richTip->setAutoDefault(false);
    richTip->setToolTip(QStringLiteral("Save checkpoint\nKeeps the current arrangement for later. Checkpoints are kept until the assembly is closed."));

    builder.block(QStringLiteral("New - message box icons"), QStringLiteral("626:1710"));
    struct BadgeSample
    {
        const char* name;
        pdf::PDFThemeIcons::Badge badge;
    };
    static constexpr BadgeSample badges[] =
    {
        { "Information", pdf::PDFThemeIcons::Badge::Information },
        { "Question", pdf::PDFThemeIcons::Badge::Question },
        { "Warning", pdf::PDFThemeIcons::Badge::Warning },
        { "Critical", pdf::PDFThemeIcons::Badge::Critical },
        { "Success", pdf::PDFThemeIcons::Badge::Success },
        { "Neutral", pdf::PDFThemeIcons::Badge::Neutral }
    };
    for (const BadgeSample& sample : badges)
    {
        const pdf::PDFThemeIcons::Badge badge = sample.badge;
        builder.specimen(QString::fromLatin1(sample.name), QSize(36, 36), [badge](QPainter& painter, const QRect& rect)
        {
            pdf::PDFThemeIcons::paintBadge(&painter, QRectF(rect), badge);
        });
    }

    builder.block(QStringLiteral("New - message boxes (PDFMessageBox)"), QStringLiteral("626:1710"));
    pdf::PDFMessageBox* embedded = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Critical, QStringLiteral("The file could not be written"),
                                                          QStringLiteral("C:\\Users\\Jakub\\Documents\\Project_Report.pdf is open in another application. Close it and try again."),
                                                          QDialogButtonBox::Cancel | QDialogButtonBox::Retry, builder.page());
    embedded->setWindowFlags(Qt::Widget);
    embedded->setProperty(Properties::OVERLAY, true);
    embedded->setDetailedText(QStringLiteral("Error 32: The process cannot access the file because it is being used by another process."));
    embedded->setCheckBoxText(QStringLiteral("Don’t show again"));
    embedded->setDefaultButton(QDialogButtonBox::Retry);
    builder.live(QStringLiteral("Message box shown inside of a tab (property \"overlay\")"), embedded, QStringLiteral("626:1744"));

    // A dialog of the design built on the message box: no badge, own content,
    // own row of the buttons in the order of the design, the width of the frame
    pdf::PDFMessageBox* form = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Information, QStringLiteral("Rename workspace"),
                                                      QStringLiteral("The workspace keeps its documents."), QDialogButtonBox::NoButton, builder.page());
    form->setWindowFlags(Qt::Widget);
    form->setProperty(Properties::OVERLAY, true);
    form->setBadgeVisible(false);
    form->setCardWidth(440);
    pdf::PDFLineEdit* formField = new pdf::PDFLineEdit(QStringLiteral("Thesis review"));
    formField->setAccessibleName(QStringLiteral("Name"));
    form->addContent(formField);
    QPushButton* formDelete = form->addActionButton(QStringLiteral("Delete"), pdf::PDFMessageBox::ActionPosition::Leading, false);
    formDelete->setProperty(Properties::DESTRUCTIVE, true);
    QPushButton* formCancel = form->addActionButton(QStringLiteral("Cancel"), pdf::PDFMessageBox::ActionPosition::Trailing, false);
    form->addActionButton(QStringLiteral("Rename"), pdf::PDFMessageBox::ActionPosition::Trailing, true);
    QObject::connect(formCancel, &QPushButton::clicked, form, &pdf::PDFMessageBox::reject);
    builder.live(QStringLiteral("Dialog of the design: no badge, own content and buttons, width 440"), form, QStringLiteral("955:150171"));

    builder.row();
    struct MessageSample
    {
        const char* name;
        pdf::PDFThemeIcons::Badge badge;
        const char* title;
        const char* text;
    };
    static constexpr MessageSample messages[] =
    {
        { "Information…", pdf::PDFThemeIcons::Badge::Information, "The document was saved", "A copy is stored next to the original file." },
        { "Question…", pdf::PDFThemeIcons::Badge::Question, "Remove 12 pages?", "The pages are removed from the document, the file on the disk is not changed until you save it." },
        { "Warning…", pdf::PDFThemeIcons::Badge::Warning, "Some fonts are not embedded", "The document may look different on another computer." },
        { "Critical…", pdf::PDFThemeIcons::Badge::Critical, "The file could not be written", "The file is open in another application. Close it and try again." }
    };
    for (const MessageSample& sample : messages)
    {
        QPushButton* button = builder.live(QString::fromUtf8(sample.name).remove(QChar(0x2026)), new QPushButton(QString::fromUtf8(sample.name), builder.page()));
        button->setAutoDefault(false);
        const pdf::PDFThemeIcons::Badge badge = sample.badge;
        const QString title = QString::fromLatin1(sample.title);
        const QString text = QString::fromLatin1(sample.text);

        QObject::connect(button, &QPushButton::clicked, button, [button, badge, title, text]()
        {
            // The static functions of the library, as an application calls them (the
            // message box is a child of the window and is deleted with it, when the
            // window is deleted while the message box is shown)
            QWidget* window = button->window();
            switch (badge)
            {
                case pdf::PDFThemeIcons::Badge::Question:
                    pdf::PDFMessageBox::question(window, title, text, QDialogButtonBox::Yes | QDialogButtonBox::No, QDialogButtonBox::Yes);
                    break;
                case pdf::PDFThemeIcons::Badge::Warning:
                    pdf::PDFMessageBox::warning(window, title, text, QDialogButtonBox::Ok, QDialogButtonBox::Ok);
                    break;
                case pdf::PDFThemeIcons::Badge::Critical:
                    pdf::PDFMessageBox::critical(window, title, text, QDialogButtonBox::Ok, QDialogButtonBox::Ok);
                    break;
                default:
                    pdf::PDFMessageBox::information(window, title, text, QDialogButtonBox::Ok, QDialogButtonBox::Ok);
                    break;
            }
        });
    }

    return builder.finish();
}

GalleryPages::Page GalleryPages::createIconsPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("17-icons-metrics-hints"), QStringLiteral("Icons, metrics and hints"), QStringLiteral("grid"));

    builder.block(QStringLiteral("Standard icons (SP_*)"), QStringLiteral("627:1625"));
    struct StandardSample
    {
        const char* name;
        QStyle::StandardPixmap pixmap;
    };
    static constexpr StandardSample standardSamples[] =
    {
        { "SP_MessageBoxInformation", QStyle::SP_MessageBoxInformation }, { "SP_MessageBoxWarning", QStyle::SP_MessageBoxWarning },
        { "SP_MessageBoxCritical", QStyle::SP_MessageBoxCritical }, { "SP_MessageBoxQuestion", QStyle::SP_MessageBoxQuestion },
        { "SP_DialogOkButton", QStyle::SP_DialogOkButton }, { "SP_DialogCancelButton", QStyle::SP_DialogCancelButton },
        { "SP_ArrowDown", QStyle::SP_ArrowDown }, { "SP_DirIcon", QStyle::SP_DirIcon }, { "SP_FileIcon", QStyle::SP_FileIcon },
        { "SP_TrashIcon", QStyle::SP_TrashIcon }, { "SP_BrowserReload", QStyle::SP_BrowserReload }, { "SP_MediaPlay", QStyle::SP_MediaPlay },
        { "SP_LineEditClearButton", QStyle::SP_LineEditClearButton }, { "SP_TitleBarNormalButton", QStyle::SP_TitleBarNormalButton },
        { "SP_ToolBarHorizontalExtensionButton", QStyle::SP_ToolBarHorizontalExtensionButton }, { "SP_VistaShield", QStyle::SP_VistaShield },
        { "SP_DialogSaveButton", QStyle::SP_DialogSaveButton }, { "SP_DriveHDIcon", QStyle::SP_DriveHDIcon }
    };
    for (const StandardSample& sample : standardSamples)
    {
        const QStyle::StandardPixmap pixmap = sample.pixmap;
        builder.specimen(QString::fromLatin1(sample.name), QSize(36, 36), [pixmap](QPainter& painter, const QRect& rect)
        {
            // The style of the application at the time of painting (it can be replaced)
            QStyle* style = QApplication::style();
            Painter::fill(&painter, QRectF(rect), 8.0, Color::BgSubtle);
            const QIcon icon = style->standardIcon(pixmap);
            const bool isBadge = pixmap == QStyle::SP_MessageBoxInformation || pixmap == QStyle::SP_MessageBoxWarning || pixmap == QStyle::SP_MessageBoxCritical || pixmap == QStyle::SP_MessageBoxQuestion;
            const int extent = isBadge ? 36 : 20;
            icon.paint(&painter, QRect(rect.left() + (rect.width() - extent) / 2, rect.top() + (rect.height() - extent) / 2, extent, extent));
        });
    }

    builder.block(QStringLiteral("Icon modes of the icon engine (16 px: normal, disabled, active, selected)"), QStringLiteral("627:1625"));
    for (const auto& mode : { std::pair<const char*, QIcon::Mode>{ "Normal", QIcon::Normal }, { "Disabled", QIcon::Disabled }, { "Active", QIcon::Active }, { "Selected", QIcon::Selected } })
    {
        const QIcon::Mode iconMode = mode.second;
        builder.specimen(QString::fromLatin1(mode.first), QSize(96, 24), [iconMode](QPainter& painter, const QRect& rect)
        {
            int x = rect.left();
            for (const char* name : { "save", "printer", "search", "settings" })
            {
                GalleryHelper::icon(name).paint(&painter, QRect(x, rect.top() + 4, 16, 16), Qt::AlignCenter, iconMode);
                x += 24;
            }
        });
    }

    builder.block(QStringLiteral("Icons of the design (Components / Icons)"), QStringLiteral("46:3"));
    const QStringList names = pdf::PDFThemeIcons::getIconNames();
    for (const QString& name : names)
    {
        builder.specimen(name, QSize(92, 28), [name](QPainter& painter, const QRect& rect)
        {
            Painter::icon(&painter, QRectF(rect.left() + 2, rect.top() + 2, 24, 24), name, Color::IconDefault);
        });
    }

    return builder.finish();
}

GalleryPages::Page GalleryPages::createComponentsPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("18-components"), QStringLiteral("Components"), QStringLiteral("layers"));

    // Hover and press of a live widget are forced for the samples
    auto setState = [](QAbstractButton* button, const GalleryHelper::NamedState& state)
    {
        button->setAttribute(Qt::WA_UnderMouse, (state.flags & GalleryPages::Hover) != 0);
        button->setDown((state.flags & GalleryPages::Pressed) != 0);
        button->setEnabled((state.flags & GalleryPages::Disabled) == 0);
    };

    builder.block(QStringLiteral("Icon button (PDFIconButton) - Size 24 and 28"), QStringLiteral("706:147674"));
    for (const pdf::PDFIconButton::Size size : { pdf::PDFIconButton::Size::Small, pdf::PDFIconButton::Size::Medium })
    {
        for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
        {
            if (state.flags == GalleryPages::Focus)
            {
                continue;
            }

            pdf::PDFIconButton* button = new pdf::PDFIconButton(QStringLiteral("more-horizontal"), size, builder.page());
            button->setAccessibleName(QStringLiteral("More"));
            setState(button, state);
            builder.live(QStringLiteral("%1 %2").arg(pdf::PDFIconButton::getExtent(size)).arg(QString::fromLatin1(state.name)), button);
        }
        builder.row();
    }

    pdf::PDFIconButton* confirmButton = new pdf::PDFIconButton(QStringLiteral("check"), pdf::PDFIconButton::Size::Small, builder.page());
    confirmButton->setAccessibleName(QStringLiteral("Confirm"));
    confirmButton->setIconColor(Color::IconAccent);
    builder.live(QStringLiteral("Own color icon/accent (confirm)"), confirmButton);

    pdf::PDFIconButton* hoveredConfirmButton = new pdf::PDFIconButton(QStringLiteral("check"), pdf::PDFIconButton::Size::Small, builder.page());
    hoveredConfirmButton->setAccessibleName(QStringLiteral("Confirm"));
    hoveredConfirmButton->setIconColor(Color::IconAccent);
    hoveredConfirmButton->setAttribute(Qt::WA_UnderMouse, true);
    builder.live(QStringLiteral("Own color, Hover"), hoveredConfirmButton);
    builder.row();

    // The focus ring outside is drawn by the focus frame of the style for a
    // widget; the variants Focus are drawn by the painter
    struct FocusVariant
    {
        pdf::PDFIconButton::Size size;
        const char* node;
    };
    static constexpr FocusVariant iconButtonFocusVariants[] = { { pdf::PDFIconButton::Size::Small, "706:147653" }, { pdf::PDFIconButton::Size::Medium, "706:147668" } };
    for (const FocusVariant& variant : iconButtonFocusVariants)
    {
        pdf::PDFComponentPainter::IconButton content;
        content.iconName = QStringLiteral("more-horizontal");
        content.extent = pdf::PDFIconButton::getExtent(variant.size);
        content.iconExtent = pdf::PDFIconButton::getIconExtent(variant.size);
        content.radius = pdf::PDFIconButton::getRadius(variant.size);
        builder.specimen(QStringLiteral("%1 Focus").arg(content.extent), QSize(content.extent, content.extent), [content](QPainter& painter, const QRect& rect)
        {
            pdf::PDFComponentPainter::paintIconButton(&painter, QRectF(rect), content, pdf::PDFComponentPainter::State(QStyle::State_Enabled | QStyle::State_HasFocus));
        }, QString::fromLatin1(variant.node));
    }

    builder.block(QStringLiteral("Menu button (PDFMoreButton) - icon more-vertical 16 of rows and cards, focus ring outside (radius 4)"));
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        const QStyle::State flags = toStyleState(StateFlags(state.flags));
        builder.specimen(QString::fromLatin1(state.name), pdf::PDFComponentPainter::getMoreButtonSize(), [flags](QPainter& painter, const QRect& rect)
        {
            pdf::PDFComponentPainter::paintMoreButton(&painter, QRectF(rect), pdf::PDFComponentPainter::State(flags));
        });
    }
    builder.row();
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        if (state.flags == GalleryPages::Focus)
        {
            continue;
        }

        pdf::PDFMoreButton* moreButton = new pdf::PDFMoreButton(builder.page());
        setState(moreButton, state);
        builder.live(QStringLiteral("Live %1").arg(QString::fromLatin1(state.name)), moreButton);
    }

    builder.block(QStringLiteral("Tab close button (PDFIconButton, Size TabClose)"), QStringLiteral("48:110"));
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        if (state.flags == GalleryPages::Focus || state.flags == GalleryPages::Disabled)
        {
            continue;
        }

        pdf::PDFIconButton* button = new pdf::PDFIconButton(QStringLiteral("x"), pdf::PDFIconButton::Size::TabClose, builder.page());
        button->setAccessibleName(QStringLiteral("Close tab"));
        setState(button, state);
        builder.live(QString::fromLatin1(state.name), button);
    }

    builder.block(QStringLiteral("Stamp chip (PDFStampChip)"), QStringLiteral("190:779"));
    struct Stamp
    {
        const char* text;
        pdf::PDFAccentHue hue;
    };
    static constexpr Stamp stamps[] =
    {
        { "Approved", pdf::PDFAccentHue::Green }, { "Confidential", pdf::PDFAccentHue::Red },
        { "Draft", pdf::PDFAccentHue::Blue }, { "Experimental", pdf::PDFAccentHue::Amber }
    };
    for (const Stamp& stamp : stamps)
    {
        builder.live(QString::fromLatin1(stamp.text), new pdf::PDFStampChip(QString::fromLatin1(stamp.text), stamp.hue, builder.page()));
    }
    builder.row();
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        if (state.flags == GalleryPages::Focus || state.flags == GalleryPages::Disabled)
        {
            continue;
        }

        pdf::PDFStampChip* chip = new pdf::PDFStampChip(QStringLiteral("Approved"), pdf::PDFAccentHue::Green, builder.page());
        setState(chip, state);
        builder.live(QStringLiteral("Green %1").arg(QString::fromLatin1(state.name)), chip);
    }

    builder.block(QStringLiteral("Filter chip with a count and an icon (QPushButton, properties chip and chipCount)"), QStringLiteral("78:836"));
    for (const bool isSelected : { false, true })
    {
        for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
        {
            if (state.flags == GalleryPages::Focus)
            {
                continue;
            }

            QPushButton* chip = new QPushButton(QStringLiteral("All"), builder.page());
            chip->setProperty(Properties::CHIP, true);
            chip->setProperty(Properties::CHIP_COUNT, 12);
            chip->setCheckable(true);
            chip->setChecked(isSelected);
            chip->setAutoDefault(false);
            setState(chip, state);
            builder.live(QStringLiteral("%1 %2").arg(isSelected ? QStringLiteral("Selected") : QStringLiteral("Unselected"), QString::fromLatin1(state.name)), chip);
        }
        builder.row();
    }
    QPushButton* iconChip = new QPushButton(GalleryHelper::icon("match-case"), QStringLiteral("Match case"), builder.page());
    iconChip->setProperty(Properties::CHIP, true);
    iconChip->setCheckable(true);
    iconChip->setAutoDefault(false);
    builder.live(QStringLiteral("With an icon"), iconChip);
    QPushButton* selectedIconChip = new QPushButton(GalleryHelper::icon("match-case"), QStringLiteral("Match case"), builder.page());
    selectedIconChip->setProperty(Properties::CHIP, true);
    selectedIconChip->setCheckable(true);
    selectedIconChip->setChecked(true);
    selectedIconChip->setAutoDefault(false);
    builder.live(QStringLiteral("With an icon, selected"), selectedIconChip);

    builder.block(QStringLiteral("Navigation of Home (PDFNavList, Nav item)"), QStringLiteral("47:123"));
    pdf::PDFNavList* homeNavigation = new pdf::PDFNavList(pdf::PDFNavItem::Kind::Home, builder.page());
    homeNavigation->setAccessibleName(QStringLiteral("Navigation"));
    homeNavigation->addItem(QStringLiteral("Home"), QStringLiteral("home"));
    homeNavigation->addItem(QStringLiteral("Open"), QStringLiteral("folder"));
    homeNavigation->addItem(QStringLiteral("Recent"), QStringLiteral("clock"));
    homeNavigation->addItem(QStringLiteral("Tools"), QStringLiteral("tools"));
    homeNavigation->addItem(QStringLiteral("AI"), QStringLiteral("ai"));
    homeNavigation->addGroup();
    homeNavigation->addItem(QStringLiteral("What's New"), QStringLiteral("gift"));
    homeNavigation->addItem(QStringLiteral("About"), QStringLiteral("info"));
    homeNavigation->addItem(QStringLiteral("Support"), QStringLiteral("headphones"));
    homeNavigation->addItem(QStringLiteral("Sponsor"), QStringLiteral("heart"));
    homeNavigation->addGroup();
    homeNavigation->addItem(QStringLiteral("Settings"), QStringLiteral("settings"));
    homeNavigation->setFixedWidth(190);
    builder.live(QStringLiteral("Three groups, Home selected"), homeNavigation, QStringLiteral("28:4"));

    QWidget* itemColumn = new QWidget(builder.page());
    QVBoxLayout* itemLayout = new QVBoxLayout(itemColumn);
    itemLayout->setContentsMargins(0, 0, 0, 0);
    itemLayout->setSpacing(6);
    for (const GalleryHelper::NamedState& state : GalleryHelper::POINTER_STATES)
    {
        if (state.flags == GalleryPages::Focus)
        {
            continue;
        }

        for (const bool isSelected : { false, true })
        {
            if (isSelected && state.flags == GalleryPages::Disabled)
            {
                continue;
            }

            pdf::PDFNavItem* item = new pdf::PDFNavItem(QStringLiteral("%1 %2").arg(isSelected ? QStringLiteral("Selected") : QStringLiteral("Open"), QString::fromLatin1(state.name)),
                                                        QStringLiteral("folder"), pdf::PDFNavItem::Kind::Home, itemColumn);
            item->setChecked(isSelected);
            setState(item, state);
            item->setFixedWidth(190);
            itemLayout->addWidget(item);
        }
    }
    builder.live(QStringLiteral("Nav item - states"), itemColumn);

    pdf::PDFNavList* settingsNavigation = new pdf::PDFNavList(pdf::PDFNavItem::Kind::Settings, builder.page());
    settingsNavigation->setAccessibleName(QStringLiteral("Settings"));
    settingsNavigation->addItem(QStringLiteral("General"), QStringLiteral("settings"), QStringLiteral("Language, theme, documents"));
    settingsNavigation->addItem(QStringLiteral("Viewing"), QStringLiteral("eye"), QStringLiteral("Page display, scrolling, magnifier"));
    settingsNavigation->addItem(QStringLiteral("Color modes"), QStringLiteral("droplet"), QStringLiteral("Reading colors, contrast"));
    settingsNavigation->addItem(QStringLiteral("Editing"), QStringLiteral("edit"), QStringLiteral("Undo, author, forms"));
    settingsNavigation->addItem(QStringLiteral("Security"), QStringLiteral("lock"), QStringLiteral("Links, signatures, certificates"));
    settingsNavigation->setFixedWidth(300);
    builder.live(QStringLiteral("Settings nav item"), settingsNavigation, QStringLiteral("145:2879"));

    builder.block(QStringLiteral("Toolbar field (PDFToolBarField)"), QStringLiteral("190:593"));
    builder.live(QStringLiteral("Default"), new pdf::PDFToolBarField(QStringLiteral("308"), QStringLiteral("/ 1592"), builder.page()));
    pdf::PDFToolBarField* hoveredField = new pdf::PDFToolBarField(QStringLiteral("308"), QStringLiteral("/ 1592"), builder.page());
    hoveredField->setAttribute(Qt::WA_UnderMouse, true);
    builder.live(QStringLiteral("Hover"), hoveredField);
    pdf::PDFToolBarField* disabledField = new pdf::PDFToolBarField(QStringLiteral("308"), QStringLiteral("/ 1592"), builder.page());
    disabledField->setEnabled(false);
    builder.live(QStringLiteral("Disabled"), disabledField);
    builder.live(QStringLiteral("Zoom"), new pdf::PDFToolBarField(QStringLiteral("151"), QStringLiteral("%"), builder.page()));
    builder.live(QStringLiteral("Without a suffix (min. 64)"), new pdf::PDFToolBarField(QStringLiteral("12"), QString(), builder.page()));
    builder.row();

    // The field and an Icon button in a toolbar, as the application places them
    // (QToolBar::addWidget wraps them into actions, which the toolbar lays out)
    QToolBar* fieldToolBar = new QToolBar(builder.page());
    fieldToolBar->addAction(GalleryHelper::icon("chevron-left"), QStringLiteral("Previous page"));
    fieldToolBar->addWidget(new pdf::PDFToolBarField(QStringLiteral("308"), QStringLiteral("/ 1592"), fieldToolBar));
    fieldToolBar->addAction(GalleryHelper::icon("chevron-right"), QStringLiteral("Next page"));
    fieldToolBar->addSeparator();
    fieldToolBar->addWidget(new pdf::PDFToolBarField(QStringLiteral("151"), QStringLiteral("%"), fieldToolBar));
    pdf::PDFIconButton* toolBarIconButton = new pdf::PDFIconButton(QStringLiteral("more-horizontal"), pdf::PDFIconButton::Size::Small, fieldToolBar);
    toolBarIconButton->setToolTip(QStringLiteral("More"));
    fieldToolBar->addWidget(toolBarIconButton);
    builder.live(QStringLiteral("In a toolbar (QToolBar::addWidget)"), fieldToolBar);

    builder.block(QStringLiteral("Password field (PDFLineEdit::setPasswordField)"), QStringLiteral("225:870"));
    auto passwordField = [&builder](const QString& caption, const QString& text)
    {
        pdf::PDFLineEdit* field = builder.live(caption, new pdf::PDFLineEdit(text, builder.page()));
        field->setPasswordField();
        field->setPlaceholderText(QStringLiteral("Password"));
        field->setFixedWidth(320);
        return field;
    };
    passwordField(QStringLiteral("Empty"), QString());
    passwordField(QStringLiteral("Masked"), QStringLiteral("Report-2025!"));
    passwordField(QStringLiteral("Visible"), QStringLiteral("Report-2025!"))->getPasswordButton()->setChecked(true);
    builder.row();
    passwordField(QStringLiteral("Hover"), QStringLiteral("Report-2025!"))->setAttribute(Qt::WA_UnderMouse, true);
    passwordField(QStringLiteral("Error"), QStringLiteral("Report-2025!"))->setInvalid(true, QStringLiteral("Wrong password"));
    passwordField(QStringLiteral("Disabled"), QStringLiteral("Report-2025!"))->setEnabled(false);

    builder.block(QStringLiteral("Document tabs (PDFDocumentTabBar)"), QStringLiteral("48:170"));
    QWidget* topBar = new QWidget(builder.page());
    topBar->setProperty(Properties::DOCUMENT_TABS, true);
    QHBoxLayout* topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(8, 0, 8, 0);
    pdf::PDFDocumentTabBar* tabBar = new pdf::PDFDocumentTabBar(topBar);
    tabBar->setAccessibleName(QStringLiteral("Documents"));
    const int homeTab = tabBar->addTab(GalleryHelper::icon("home"), QStringLiteral("Home"));
    const int reportTab = tabBar->addTab(GalleryHelper::icon("file"), QStringLiteral("Project_Report.pdf"));
    const int invoiceTab = tabBar->addTab(GalleryHelper::icon("file"), QStringLiteral("Invoice.pdf"));
    const int scanTab = tabBar->addTab(GalleryHelper::icon("file"), QStringLiteral("Scan_0042.pdf"));
    tabBar->setTabClosable(homeTab, false);
    tabBar->setTabClosable(reportTab, true);
    tabBar->setTabClosable(invoiceTab, true);
    tabBar->setTabClosable(scanTab, true);
    tabBar->setTabUnsaved(reportTab, true);
    tabBar->setTabLocked(invoiceTab, true);
    tabBar->setTabUnsaved(invoiceTab, true);
    tabBar->setTabProgress(scanTab, 0.55);
    tabBar->setCurrentIndex(reportTab);
    topBarLayout->addWidget(tabBar);
    topBarLayout->addStretch(1);
    topBar->setFixedSize(860, 38);
    builder.live(QStringLiteral("Home, unsaved, locked and unsaved, progress"), topBar);
    builder.row();

    // Document protected by a password (the icon password replaces the icon of
    // the tab, View Document 228:8709) and the tab New tab (pdf4qt-new-tab 955:152339)
    QWidget* passwordBar = new QWidget(builder.page());
    passwordBar->setProperty(Properties::DOCUMENT_TABS, true);
    QHBoxLayout* passwordBarLayout = new QHBoxLayout(passwordBar);
    passwordBarLayout->setContentsMargins(8, 0, 8, 0);
    pdf::PDFDocumentTabBar* passwordTabs = new pdf::PDFDocumentTabBar(passwordBar);
    passwordTabs->setAccessibleName(QStringLiteral("Documents"));
    const int passwordHomeTab = passwordTabs->addTab(GalleryHelper::icon("home"), QStringLiteral("Home"));
    const int protectedTab = passwordTabs->addTab(GalleryHelper::icon("file"), QStringLiteral("Project_Report.pdf"));
    const int protectedLockedTab = passwordTabs->addTab(GalleryHelper::icon("file"), QStringLiteral("Contract.pdf"));
    const int newTab = passwordTabs->addTab(GalleryHelper::icon("folder"), QStringLiteral("New tab"));
    passwordTabs->setTabClosable(passwordHomeTab, false);
    passwordTabs->setTabClosable(protectedTab, true);
    passwordTabs->setTabClosable(protectedLockedTab, true);
    passwordTabs->setTabClosable(newTab, true);
    passwordTabs->setTabPasswordProtected(protectedTab, true);
    passwordTabs->setTabPasswordProtected(protectedLockedTab, true);
    passwordTabs->setTabLocked(protectedLockedTab, true);
    passwordTabs->setCurrentIndex(protectedTab);
    passwordBarLayout->addWidget(passwordTabs);
    passwordBarLayout->addStretch(1);
    passwordBar->setFixedSize(860, 38);
    builder.live(QStringLiteral("Password protected, password protected and locked, New tab"), passwordBar, QStringLiteral("228:8709"));

    builder.block(QStringLiteral("Rows of views (PDFTreeView::setRowStyle)"), QStringLiteral("191:840"));
    QStandardItemModel* outlineModel = new QStandardItemModel(builder.page());
    QStandardItem* chapter = new QStandardItem(QStringLiteral("Market Analysis"));
    QStandardItem* section = new QStandardItem(QStringLiteral("4.1 Regional revenue"));
    section->appendRow(new QStandardItem(QStringLiteral("Central Europe")));
    section->appendRow(new QStandardItem(QStringLiteral("Nordic countries")));
    chapter->appendRow(section);
    chapter->appendRow(new QStandardItem(QStringLiteral("4.2 Customer segments")));
    outlineModel->appendRow(new QStandardItem(QStringLiteral("Introduction")));
    outlineModel->appendRow(chapter);
    outlineModel->appendRow(new QStandardItem(QStringLiteral("Conclusion")));
    pdf::PDFTreeView* outlineView = new pdf::PDFTreeView(builder.page());
    outlineView->setModel(outlineModel);
    outlineView->setHeaderHidden(true);
    outlineView->setRowStyle(pdf::PDFTreeView::RowStyle::Outline);
    outlineView->expandAll();
    outlineView->setCurrentIndex(outlineModel->index(0, 0, chapter->index()));
    outlineView->setFixedSize(300, 230);
    builder.live(QStringLiteral("Outline row"), outlineView, QStringLiteral("191:840"));

    QStandardItemModel* certificateModel = new QStandardItemModel(builder.page());
    certificateModel->setHorizontalHeaderLabels({ QStringLiteral("Type"), QStringLiteral("Certificate"), QStringLiteral("Organization"), QStringLiteral("Valid from"), QStringLiteral("Valid to") });
    struct Certificate
    {
        const char* type;
        const char* name;
        const char* organization;
        const char* from;
        const char* to;
    };
    static constexpr Certificate certificates[] =
    {
        { "System", "ISRG Root X1", "Internet Security Research Group", "2015-06-04", "2035-06-04" },
        { "System", "DigiCert Global Root G2", "DigiCert Inc", "2013-08-01", "2038-01-15" },
        { "User", "Jakub Melka", "PDF4QT", "2026-01-10", "2028-01-10" }
    };
    for (const Certificate& certificate : certificates)
    {
        QList<QStandardItem*> row;
        for (const char* text : { certificate.type, certificate.name, certificate.organization, certificate.from, certificate.to })
        {
            QStandardItem* item = new QStandardItem(QString::fromLatin1(text));
            row << item;
        }
        QFont nameFont = pdf::PDFThemeFonts::getFont(Font::SmallSemiBold);
        row[1]->setFont(nameFont);
        certificateModel->appendRow(row);
    }
    pdf::PDFTreeView* certificateView = new pdf::PDFTreeView(builder.page());
    certificateView->setModel(certificateModel);
    certificateView->setRootIsDecorated(false);
    certificateView->setRowStyle(pdf::PDFTreeView::RowStyle::Certificate);
    certificateView->setCurrentIndex(certificateModel->index(1, 0));
    certificateView->header()->resizeSection(0, 84);
    certificateView->header()->resizeSection(1, 200);
    certificateView->header()->resizeSection(2, 240);
    certificateView->setFixedSize(720, 150);
    builder.live(QStringLiteral("Certificate row"), certificateView, QStringLiteral("145:2929"));

    builder.row();
    QStandardItemModel* libraryModel = new QStandardItemModel(builder.page());
    libraryModel->setHorizontalHeaderLabels({ QStringLiteral("Library"), QStringLiteral("Version"), QStringLiteral("Purpose"), QStringLiteral("License") });
    for (const auto& library : { std::array<const char*, 4>{ "Qt", "6.9.0", "Application framework", "LGPL v3" }, { "OpenSSL", "3.4.0", "Cryptography", "Apache 2.0" }, { "Blend2D", "0.12", "2D rendering", "Zlib" } })
    {
        QList<QStandardItem*> row;
        for (const char* text : library)
        {
            row << new QStandardItem(QString::fromLatin1(text));
        }
        row[0]->setFont(pdf::PDFThemeFonts::getFont(Font::SmallSemiBold));
        libraryModel->appendRow(row);
    }
    pdf::PDFTreeView* libraryView = new pdf::PDFTreeView(builder.page());
    libraryView->setModel(libraryModel);
    libraryView->setRootIsDecorated(false);
    libraryView->setSelectionMode(QAbstractItemView::NoSelection);
    libraryView->setRowStyle(pdf::PDFTreeView::RowStyle::Library);
    libraryView->setFixedSize(620, 140);
    builder.live(QStringLiteral("Library row"), libraryView, QStringLiteral("110:2158"));

    builder.block(QStringLiteral("Key value rows (PDFKeyValueList)"), QStringLiteral("110:2155"));
    pdf::PDFKeyValueList* facts = new pdf::PDFKeyValueList(builder.page());
    facts->addRow(QStringLiteral("Input"), QStringLiteral("One open PDF"));
    facts->addRow(QStringLiteral("Result"), QStringLiteral("A smaller PDF"));
    facts->addRow(QStringLiteral("Signatures"), QStringLiteral("Kept (incremental save)"));
    facts->addRow(QStringLiteral("Command line"), QStringLiteral("pdftool optimize"));
    facts->addRow(QStringLiteral("Build date"), QStringLiteral("Sep 14, 2026 14:32 (a long value, which is elided)"));
    facts->addRow(QStringLiteral("Program"), QStringLiteral("C:\\Users\\Jane\\Downloads\\Quarterly reports 2026\\invoice.pdf.exe"), pdf::PDFComponentPainter::TextKind::FileName);
    facts->setFixedWidth(360);
    builder.live(QStringLiteral("Facts of a card, 360 px"), facts);

    return builder.finish();
}

GalleryPages::Page GalleryPages::createLeakPage(QWidget* parent)
{
    GalleryBuilder builder(parent, QStringLiteral("99-palette-leak"), QStringLiteral("Palette leak"), QStringLiteral("alert-triangle"));

    builder.block(QStringLiteral("Widgets of Qt, which draw themselves from the palette - their texts are expected to have the leak color"));
    QLineEdit* lineEdit = builder.live(QStringLiteral("QLineEdit"), new QLineEdit(QStringLiteral("Text of a QLineEdit"), builder.page()));
    lineEdit->setFixedWidth(220);
    QSpinBox* spinBox = builder.live(QStringLiteral("QSpinBox"), new QSpinBox(builder.page()));
    spinBox->setValue(50);
    QLabel* richLabel = builder.live(QStringLiteral("QLabel with a rich text"), new QLabel(QStringLiteral("<b>Rich</b> text with a <a href=\"#\">link</a>"), builder.page()));
    richLabel->setTextFormat(Qt::RichText);
    QStatusBar* statusBar = builder.live(QStringLiteral("QStatusBar"), new QStatusBar(builder.page()));
    statusBar->showMessage(QStringLiteral("Message of a QStatusBar"));
    statusBar->setFixedWidth(260);

    builder.row();
    QTextEdit* textEdit = builder.live(QStringLiteral("QTextEdit"), new QTextEdit(builder.page()));
    textEdit->setPlainText(QStringLiteral("Text of a QTextEdit"));
    textEdit->setFixedSize(260, 80);
    QCalendarWidget* calendar = builder.live(QStringLiteral("QCalendarWidget"), new QCalendarWidget(builder.page()));
    calendar->setFixedSize(320, 220);

    return builder.finish();
}

QImage GalleryPages::renderWidget(QWidget* widget, qreal scale)
{
    const QSize size = widget->size();
    QImage image(size * scale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    if (const GallerySpecimen* specimen = qobject_cast<const GallerySpecimen*>(widget))
    {
        specimen->paintSpecimen(painter);
    }
    else
    {
        widget->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    }
    painter.end();

    return image;
}

void GalleryPages::exportPage(const Page& page, const QString& directory, const QString& theme, qreal scale, QJsonArray& manifest)
{
    QDir().mkpath(directory);

    auto fileName = [&](const QString& name, int index)
    {
        QString safe;
        for (const QChar character : name)
        {
            safe += (character.isLetterOrNumber() && character.unicode() < 128) ? character.toLower() : QLatin1Char('-');
        }
        while (safe.contains(QLatin1String("--")))
        {
            safe.replace(QLatin1String("--"), QLatin1String("-"));
        }
        return QStringLiteral("%1__%2__%3-%4@%5x.png").arg(page.id, theme).arg(index, 3, 10, QLatin1Char('0')).arg(safe.left(60)).arg(scale);
    };

    auto append = [&](const QString& file, const QString& name, const QString& node, const QString& kind, const QSize& size, int margin)
    {
        QJsonObject object;
        object.insert(QStringLiteral("file"), file);
        object.insert(QStringLiteral("page"), page.id);
        object.insert(QStringLiteral("theme"), theme);
        object.insert(QStringLiteral("name"), name);
        object.insert(QStringLiteral("figmaNode"), node);
        object.insert(QStringLiteral("kind"), kind);
        object.insert(QStringLiteral("width"), size.width());
        object.insert(QStringLiteral("height"), size.height());
        object.insert(QStringLiteral("margin"), margin);
        object.insert(QStringLiteral("scale"), scale);
        manifest.append(object);
    };

    // The whole page. Live widgets are cut out of it, so they are exported as they are seen.
    QImage pageImage(page.widget->size() * scale, QImage::Format_ARGB32_Premultiplied);
    pageImage.setDevicePixelRatio(scale);
    pageImage.fill(pdf::PDFStylePainter::color(Color::BgCanvas));
    {
        QPainter painter(&pageImage);
        page.widget->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    }
    {
        const QString file = QStringLiteral("%1__%2__page@%3x.png").arg(page.id, theme).arg(scale);
        pageImage.save(directory + QLatin1Char('/') + file);
        append(file, page.title, QString(), QStringLiteral("page"), page.widget->size(), 0);
    }

    if (qEnvironmentVariableIsSet("PDF4QT_GALLERY_DEBUG"))
    {
        // Widgets filled from the palette by Qt itself
        const QList<QWidget*> allWidgets = page.widget->findChildren<QWidget*>();
        for (const QWidget* widget : allWidgets)
        {
            if (widget->autoFillBackground())
            {
                qWarning("autofill: %s / %s '%s' in %s styled=%d saved=%d polished=%d visible=%d", qPrintable(page.id), widget->metaObject()->className(), qPrintable(widget->objectName()), widget->parentWidget()->metaObject()->className(), int(widget->testAttribute(Qt::WA_StyledBackground)), int(widget->property("_pdf4qt_styled").isValid()), int(widget->testAttribute(Qt::WA_WState_Polished)), int(widget->isVisible()));
            }
        }
    }

    int index = 0;
    const QList<QWidget*> widgets = page.widget->findChildren<QWidget*>();
    for (QWidget* widget : widgets)
    {
        const GallerySpecimen* specimen = qobject_cast<const GallerySpecimen*>(widget);
        const QString liveName = widget->property("galleryName").toString();

        if (!specimen && liveName.isEmpty())
        {
            continue;
        }

        const QString name = specimen ? specimen->getName() : liveName;
        const QString node = specimen ? specimen->getFigmaNode() : widget->property("galleryNode").toString();
        const QString file = fileName(name, index++);

        // Samples are exported over the canvas, as they are seen
        const QRect geometry(widget->mapTo(page.widget, QPoint(0, 0)), widget->size());
        const QRect pixels(qRound(geometry.left() * scale), qRound(geometry.top() * scale), qRound(geometry.width() * scale), qRound(geometry.height() * scale));
        QImage image = pageImage.copy(pixels);
        image.setDevicePixelRatio(scale);
        image.save(directory + QLatin1Char('/') + file);

        append(file, name, node, specimen ? QStringLiteral("specimen") : QStringLiteral("live"), specimen ? specimen->getControlSize() : widget->size(), specimen ? GallerySpecimen::MARGIN : 0);
    }
}

}   // namespace pdffluentgallery
