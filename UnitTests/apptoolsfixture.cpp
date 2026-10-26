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

#include "apptoolsfixture.h"

#include "pdfappnavigator.h"
#include "pdfapppagenotfound.h"
#include "pdfapproute.h"
#include "pdfapptab.h"
#include "pdfapptoolpresentation.h"
#include "pdfdatabasenotifier.h"

#include "pdfcomponentrows.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QtMath>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

namespace pdftest
{

AppToolsTestPage::AppToolsTestPage(pdf::PDFAppContext* context, QWidget* parent) :
    pdf::PDFAppPage(context, parent)
{
    setObjectName(QStringLiteral("testToolPage"));
}

bool AppToolsTestPage::setRoute(const pdf::PDFAppRoute& route)
{
    m_toolId = pdf::PDFAppToolRoutePageFactory::getToolId(pdf::PDFAppToolPageKind::Start, route);
    if (m_toolId.isEmpty())
    {
        m_toolId = pdf::PDFAppToolRoutePageFactory::getToolId(pdf::PDFAppToolPageKind::Document, route);
    }
    return pdf::PDFAppPage::setRoute(route);
}

QString AppToolsTestPage::getTitle() const
{
    return QStringLiteral("Test tool %1").arg(m_toolId);
}

pdf::PDFAppPage* AppToolsTestPageFactory::createPage(pdf::PDFAppContext* context, QWidget* parent) const
{
    return new AppToolsTestPage(context, parent);
}

/// Thread running a write by its own connection (another instance)
class AppToolsWriterThread : public QThread
{
public:
    AppToolsWriterThread(pdf::PDFDatabase* database, std::function<pdf::PDFDatabaseError(pdf::PDFDatabase*)> write) :
        m_database(database),
        m_write(std::move(write))
    {
    }

    bool isSuccess() const { return m_isSuccess; }

protected:
    virtual void run() override
    {
        m_isSuccess = m_write && !m_write(m_database).isError();
    }

private:
    pdf::PDFDatabase* m_database;
    std::function<pdf::PDFDatabaseError(pdf::PDFDatabase*)> m_write;
    bool m_isSuccess = false;
};

AppToolsFixture::AppToolsFixture(QSize size) :
    AppDocumentFixture(size)
{

}

AppToolsFixture::~AppToolsFixture()
{
    for (const auto& item : m_registeredPages)
    {
        pdf::PDFAppToolPageRegistry::unregisterFactory(item.first, item.second);
    }
}

void AppToolsFixture::navigate(const QString& route)
{
    if (window && window->getNavigator())
    {
        window->getNavigator()->navigate(pdf::PDFAppRoute::parse(route), pdf::PDFAppNavigationMode::Push);
    }
    settle();
}

pdf::PDFAppPage* AppToolsFixture::openTools(const QString& route)
{
    navigate(route);
    return currentPage();
}

pdf::PDFAppTab* AppToolsFixture::currentTab() const
{
    return window ? window->getCurrentTab() : nullptr;
}

pdf::PDFAppPage* AppToolsFixture::currentPage() const
{
    pdf::PDFAppTab* tab = currentTab();
    return tab ? tab->getPage() : nullptr;
}

pdf::PDFPageNotFoundPage* AppToolsFixture::notFoundPage() const
{
    return qobject_cast<pdf::PDFPageNotFoundPage*>(currentPage());
}

QString AppToolsFixture::currentRoute() const
{
    pdf::PDFAppTab* tab = currentTab();
    const pdf::PDFAppHistoryEntry* entry = tab ? tab->getHistory().getCurrentEntry() : nullptr;
    return entry ? entry->route.toString() : QString();
}

QWidget* AppToolsFixture::findToolWidget(const QString& toolId) const
{
    pdf::PDFAppPage* page = currentPage();
    return page ? page->findChild<QWidget*>(pdf::PDFAppToolPresentation::getObjectName(toolId)) : nullptr;
}

pdf::PDFToolRow* AppToolsFixture::findToolRow(const QString& toolId) const
{
    pdf::PDFAppPage* page = currentPage();
    return page ? page->findChild<pdf::PDFToolRow*>(pdf::PDFAppToolPresentation::getObjectName(toolId)) : nullptr;
}

pdf::PDFCollectionRow* AppToolsFixture::findCollectionRow(const QString& routeId) const
{
    pdf::PDFAppPage* page = currentPage();
    return page ? page->findChild<pdf::PDFCollectionRow*>(QStringLiteral("collection-") + routeId) : nullptr;
}

pdf::PDFToolRepository AppToolsFixture::tools() const
{
    return context->getTools();
}

pdf::PDFToolCollectionRepository AppToolsFixture::collections() const
{
    return context->getToolCollections();
}

bool AppToolsFixture::writeFromOtherConnection(const std::function<pdf::PDFDatabaseError(pdf::PDFDatabase*)>& write, const QString& topic)
{
    if (!context || !context->getDatabase() || !context->getNotifier())
    {
        return false;
    }

    QSignalSpy spy(context->getNotifier(), &pdf::PDFDatabaseNotifier::changed);
    AppToolsWriterThread thread(context->getDatabase(), write);
    thread.start();
    if (!thread.wait(10000 * getTimeoutFactor()) || !thread.isSuccess())
    {
        return false;
    }

    // The notifier of this connection polls the change log
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 * getTimeoutFactor())
    {
        for (const QList<QVariant>& arguments : std::as_const(spy))
        {
            if (arguments.value(0).toString() == topic)
            {
                settle();
                return true;
            }
        }
        QTest::qWait(10);
    }
    return false;
}

bool AppToolsFixture::pinFromOtherConnection(const QString& toolId, bool isPinned)
{
    return writeFromOtherConnection([toolId, isPinned](pdf::PDFDatabase* database) { return pdf::PDFToolRepository(database).setPinned(toolId, isPinned); },
                                    pdf::PDFToolRepository::getPinTopic());
}

bool AppToolsFixture::recordUsageFromOtherConnection(const QString& toolId, qint64 time)
{
    return writeFromOtherConnection([toolId, time](pdf::PDFDatabase* database)
                                    {
                                        pdf::PDFToolRepository repository(database);
                                        repository.setCurrentTime(time);
                                        return repository.recordUsage(toolId);
                                    },
                                    pdf::PDFToolRepository::getUsageTopic());
}

bool AppToolsFixture::writeCollectionFromOtherConnection(const pdf::PDFToolCollectionDraft& draft, qint64* id)
{
    qint64 savedId = 0;
    const bool isSuccess = writeFromOtherConnection([draft, &savedId](pdf::PDFDatabase* database) { return pdf::PDFToolCollectionRepository(database).saveCollection(draft, &savedId); },
                                                    pdf::PDFToolCollectionRepository::getCollectionTopic());
    if (id)
    {
        *id = savedId;
    }
    return isSuccess;
}

bool AppToolsFixture::registerTestToolPage(const QString& toolId, pdf::PDFAppToolPageKind kind)
{
    if (!pdf::PDFAppToolPageRegistry::registerFactory(kind, toolId, std::make_shared<AppToolsTestPageFactory>()))
    {
        return false;
    }
    m_registeredPages.emplace_back(kind, toolId);
    return true;
}

pdf::PDFAppDocumentTab* AppToolsFixture::openDocument(int pageCount)
{
    static int s_documentCounter = 0;
    const QString name = QStringLiteral("Tools-%1.pdf").arg(++s_documentCounter);
    return open(AppDocumentSamples::simple(folder(), name, pageCount));
}

QImage AppToolsFixture::screenshot(const QString& name) const
{
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty() || !window)
    {
        return QImage();
    }

    QDir().mkpath(folder);
    const QImage image = window->grab().toImage();
    image.save(QDir(folder).absoluteFilePath(name + QStringLiteral(".png")));

    // 2x (review RF6): the window drawn at the device pixel ratio 2 with the popups
    // and dialogs, which are windows of their own, drawn over it at their places
    const qreal ratio = 2.0;
    QImage image2x(QSize(qCeil(window->width() * ratio), qCeil(window->height() * ratio)), QImage::Format_ARGB32_Premultiplied);
    image2x.setDevicePixelRatio(ratio);
    image2x.fill(Qt::transparent);
    QPainter painter(&image2x);
    window->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren | QWidget::DrawWindowBackground);
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (widget != window && widget->isVisible() && !widget->isAncestorOf(window) && (widget->windowType() == Qt::Popup || widget->windowType() == Qt::Dialog))
        {
            const QPoint position = window->mapFromGlobal(widget->mapToGlobal(QPoint(0, 0)));
            widget->render(&painter, position, QRegion(), QWidget::DrawChildren);
        }
    }
    painter.end();
    image2x.save(QDir(folder).absoluteFilePath(name + QStringLiteral("-2x.png")));
    return image;
}

}   // namespace pdftest
