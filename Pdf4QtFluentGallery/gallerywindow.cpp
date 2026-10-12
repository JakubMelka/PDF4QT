#include "gallerywindow.h"

#include "pdffluentwidgets.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>
#include <QScrollArea>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace pdffluentgallery
{

GalleryWindow::GalleryWindow(QWidget* parent) :
    QMainWindow(parent),
    m_tabWidget(new QTabWidget(this)),
    m_themeControl(new pdf::PDFSegmentedControl(this)),
    m_themeItem(nullptr)
{
    setWindowTitle(tr("PDF4QT widget gallery"));
    resize(1500, 900);

    QWidget* central = new QWidget(this);
    QVBoxLayout* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    // Top bar: theme, direction of the layout, state of the pages
    QFrame* topBar = new QFrame(central);
    topBar->setProperty(pdf::PDFWidgetStyleProperties::NOTE, true);
    QHBoxLayout* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(16, 8, 16, 8);
    topLayout->setSpacing(16);
    topLayout->addWidget(new pdf::PDFLabel(tr("Theme"), pdf::PDFThemeFonts::Role::BodySemiBold, pdf::PDFThemeColors::TextPrimary, topBar));

    pdf::PDFThemeManager* themeManager = pdf::PDFThemeManager::getInstance();
    int currentTheme = 0;
    for (const pdf::PDFThemeColors& theme : themeManager->getThemes())
    {
        const int index = m_themeControl->addSegment(theme.getName(), theme.isDark() ? QStringLiteral("moon") : QStringLiteral("sun"));
        if (theme.getId() == themeManager->getTheme().getId())
        {
            currentTheme = index;
        }
    }
    m_themeControl->setCurrentIndex(currentTheme);
    m_themeControl->setAccessibleName(tr("Theme"));
    topLayout->addWidget(m_themeControl);

    QCheckBox* rightToLeft = new QCheckBox(tr("Right to left"), topBar);
    QCheckBox* disablePages = new QCheckBox(tr("Disable the pages"), topBar);
    QCheckBox* reduceMotion = new QCheckBox(tr("Reduce motion"), topBar);
    topLayout->addWidget(rightToLeft);
    topLayout->addWidget(disablePages);
    topLayout->addWidget(reduceMotion);
    topLayout->addStretch(1);
    centralLayout->addWidget(topBar);

    // One tab for each section of the design
    m_tabWidget->setTabPosition(QTabWidget::West);
    m_pages = GalleryPages::createPages(this);
    m_pages.push_back(GalleryPages::createLeakPage(this));

    for (const GalleryPages::Page& page : m_pages)
    {
        QScrollArea* scrollArea = new QScrollArea(m_tabWidget);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setWidgetResizable(true);
        scrollArea->setWidget(page.widget);
        m_tabWidget->addTab(scrollArea, pdf::PDFThemeIcons::getIcon(page.iconName), page.title);
    }
    centralLayout->addWidget(m_tabWidget, 1);
    setCentralWidget(central);

    pdf::PDFStatusBar* statusBar = new pdf::PDFStatusBar(this);
    m_themeItem = statusBar->createItem(QString());
    setStatusBar(statusBar);

    connect(m_themeControl, &pdf::PDFSegmentedControl::currentIndexChanged, this, [this](int index)
    {
        const std::vector<pdf::PDFThemeColors>& themes = pdf::PDFThemeManager::getInstance()->getThemes();
        if (index >= 0 && index < static_cast<int>(themes.size()))
        {
            pdf::PDFThemeManager::getInstance()->setTheme(themes[static_cast<size_t>(index)].getId());
            updateStatus();
        }
    });

    connect(rightToLeft, &QCheckBox::toggled, this, [](bool isRightToLeft)
    {
        QApplication::setLayoutDirection(isRightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
    });

    connect(disablePages, &QCheckBox::toggled, this, [this](bool isDisabled)
    {
        for (const GalleryPages::Page& page : m_pages)
        {
            page.widget->setEnabled(!isDisabled);
        }
    });

    connect(reduceMotion, &QCheckBox::toggled, this, [](bool isReduced)
    {
        pdf::PDFThemeManager::getInstance()->setReducedMotion(isReduced);
    });

    updateStatus();
}

void GalleryWindow::setCurrentPage(const QString& id)
{
    for (size_t i = 0; i < m_pages.size(); ++i)
    {
        if (m_pages[i].id == id || m_pages[i].id.startsWith(id + QLatin1Char('-')))
        {
            m_tabWidget->setCurrentIndex(static_cast<int>(i));
            return;
        }
    }
}

int GalleryWindow::exportPages(const QString& directory, qreal scale)
{
    QJsonArray manifest;
    const QString theme = pdf::PDFThemeManager::getInstance()->getTheme().getId();

    for (size_t i = 0; i < m_pages.size(); ++i)
    {
        // A page is laid out and prepared by the style, when it is shown
        m_tabWidget->setCurrentIndex(static_cast<int>(i));
        QApplication::processEvents();
        GalleryPages::exportPage(m_pages[i], directory, theme, scale, manifest);
    }

    QFile file(QStringLiteral("%1/manifest__%2@%3x.json").arg(directory, theme).arg(scale));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    }

    return static_cast<int>(manifest.size());
}

void GalleryWindow::updateStatus()
{
    const pdf::PDFThemeColors& theme = pdf::PDFThemeManager::getInstance()->getTheme();
    const qreal devicePixelRatio = screen() ? screen()->devicePixelRatio() : 1.0;
    m_themeItem->setText(tr("%1 theme · %2 colors · scale %3 %").arg(theme.getName()).arg(static_cast<int>(pdf::PDFThemeColors::ColorCount)).arg(qRound(devicePixelRatio * 100.0)));
    statusBar()->showMessage(tr("Style %1 · platform %2 · font %3").arg(QApplication::style()->objectName(), QGuiApplication::platformName(), QApplication::font().family()));
}

}   // namespace pdffluentgallery
