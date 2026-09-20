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

#include "utils.h"
#include "settingsdockwidget.h"
#include "mainwindow.h"
#include "pdfdocumentbuilder.h"

#include <QtTest>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSettings>
#include <QTemporaryDir>
#include <QPainter>
#include <QAction>
#include <QLayout>

class DiffOverlayTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void mainWindowIntegration();
    void fitPageSizes_data();
    void fitPageSizes();
    void manualScaleAndShift();
    void originalLayoutAndOtherViews();
    void unpairedAndFilteredPages();
    void controlsAndReset();

private:
    QTemporaryDir m_settingsDirectory;
};

void DiffOverlayTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName("PDF4QT-Tests");
    QCoreApplication::setApplicationName("DiffOverlay");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

static pdf::PDFDocument createDrawing(double scale, QColor color)
{
    pdf::PDFDocumentBuilder builder;
    pdf::PDFPageContentStreamBuilder content(&builder);
    QPainter* painter = content.beginNewPage(QRectF(0, 0, 300 * scale, 400 * scale));
    painter->scale(scale, scale);
    painter->fillRect(QRectF(20, 20, 260, 360), color);
    painter->fillRect(QRectF(40, 40, 220, 320), Qt::white);
    content.end(painter);
    return builder.build();
}

void DiffOverlayTest::mainWindowIntegration()
{
    pdfdiff::MainWindow window(nullptr);
    window.resize(1200, 900);
    window.setLeftDocument(createDrawing(1.0, Qt::red));
    window.setRightDocument(createDrawing(2.0, Qt::blue));
    window.updateViewDocument();
    window.performOperation(pdfdiff::MainWindow::Operation::Compare);
    auto widget = window.findChild<pdf::PDFWidget*>();
    auto settings = window.findChild<pdfdiff::SettingsDockWidget*>();
    auto mode = window.findChild<QComboBox*>("overlayScaleModeCombo");
    auto overlayAction = window.findChild<QAction*>("actionView_Overlay");
    QVERIFY(widget && settings && mode && overlayAction);
    auto proxy = widget->getDrawWidgetProxy();
    QTRY_VERIFY_WITH_TIMEOUT(proxy->getDocument() && proxy->getDocument()->getCatalog()->getPageCount() == 2, 10000);
    overlayAction->trigger();
    QVERIFY(!settings->isHidden());
    QVERIFY(mode->isEnabled());
    window.layout()->activate();
    widget->resize(700, 700);
    widget->getDrawWidget()->getWidget()->resize(700, 700);
    proxy->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
    mode->setCurrentIndex(mode->findData(int(pdfdiff::OverlaySettings::ScaleMode::Fit)));
    const auto snapshot = proxy->getSnapshot();
    QCOMPARE(snapshot.items.size(), size_t(2));
    QCOMPARE(snapshot.items[0].rect, snapshot.items[1].rect);
    const QPointF leftCorner = snapshot.items[0].pageToDeviceMatrix.map(QPointF(20, 20));
    const QPointF rightCorner = snapshot.items[1].pageToDeviceMatrix.map(QPointF(40, 40));
    QVERIFY(QLineF(leftCorner, rightCorner).length() < 1.0);
    mode->setCurrentIndex(mode->findData(int(pdfdiff::OverlaySettings::ScaleMode::Manual)));
    auto leftScale = window.findChild<QDoubleSpinBox*>("leftScaleSpinBox");
    leftScale->setValue(200.0);
    window.findChild<QAction*>("actionView_Left")->trigger();
    QVERIFY(!mode->isEnabled());
    overlayAction->trigger();
    QCOMPARE(leftScale->value(), 200.0);
    QVERIFY(mode->isEnabled());
    window.setRightDocument(createDrawing(1.5, Qt::green));
    QCOMPARE(settings->getOverlaySettings().scaleMode, pdfdiff::OverlaySettings::ScaleMode::Original);
    QCOMPARE(leftScale->value(), 100.0);
}

struct OverlayFixture
{
    pdf::PDFDocument left;
    pdf::PDFDocument right;
    pdf::PDFDocument combined;
    pdf::PDFDiffResult diff;
    pdfdiff::ComparedDocumentMapper mapper;

    OverlayFixture(const QList<QSizeF>& leftSizes, const QList<QSizeF>& rightSizes,
                   pdf::PDFDiffResult::PageSequence sequence, bool rotateLeft = false)
    {
        pdf::PDFDocumentBuilder leftBuilder;
        pdf::PDFDocumentBuilder rightBuilder;
        pdf::PDFDocumentBuilder combinedBuilder;
        for (QSizeF size : leftSizes)
        {
            // Nonzero media box origins must not influence the overlay placement.
            QRectF box(QPointF(17.0, 23.0), size * (72.0 / 25.4));
            auto page = leftBuilder.appendPage(box);
            auto combinedPage = combinedBuilder.appendPage(box);
            if (rotateLeft)
            {
                leftBuilder.setPageRotation(page, pdf::PageRotation::Rotate90);
                combinedBuilder.setPageRotation(combinedPage, pdf::PageRotation::Rotate90);
            }
        }
        for (QSizeF size : rightSizes)
        {
            QRectF box(QPointF(), size * (72.0 / 25.4));
            rightBuilder.appendPage(box);
            combinedBuilder.appendPage(box);
        }
        left = leftBuilder.build();
        right = rightBuilder.build();
        combined = combinedBuilder.build();
        diff.setPageSequence(std::move(sequence));
    }

    void update(const pdfdiff::OverlaySettings& settings = {},
                pdfdiff::ComparedDocumentMapper::Mode mode = pdfdiff::ComparedDocumentMapper::Mode::Overlay,
                bool filter = false)
    {
        const pdf::PDFDocument* current = &combined;
        if (mode == pdfdiff::ComparedDocumentMapper::Mode::Left)
            current = &left;
        if (mode == pdfdiff::ComparedDocumentMapper::Mode::Right)
            current = &right;
        mapper.update(mode, filter, diff, &left, &right, current, settings);
    }
};

static bool closeSize(QSizeF actual, QSizeF expected)
{
    return qAbs(actual.width() - expected.width()) < 0.001 &&
           qAbs(actual.height() - expected.height()) < 0.001;
}

void DiffOverlayTest::fitPageSizes_data()
{
    QTest::addColumn<QSizeF>("left");
    QTest::addColumn<QSizeF>("right");
    QTest::addColumn<QSizeF>("expectedLeft");
    QTest::addColumn<QSizeF>("expectedRight");
    QTest::addColumn<bool>("rotateLeft");
    QTest::newRow("A4 to A3") << QSizeF(210, 297) << QSizeF(297, 420)
        << QSizeF(210.0 * 420.0 / 297.0, 420) << QSizeF(297, 420) << false;
    QTest::newRow("A3 to A4") << QSizeF(297, 420) << QSizeF(210, 297)
        << QSizeF(297, 420) << QSizeF(210.0 * 420.0 / 297.0, 420) << false;
    QTest::newRow("same size") << QSizeF(210, 297) << QSizeF(210, 297)
        << QSizeF(210, 297) << QSizeF(210, 297) << false;
    QTest::newRow("different proportions") << QSizeF(100, 100) << QSizeF(300, 200)
        << QSizeF(200, 200) << QSizeF(300, 200) << false;
    QTest::newRow("rotated page") << QSizeF(100, 200) << QSizeF(400, 200)
        << QSizeF(400, 200) << QSizeF(400, 200) << true;
}

void DiffOverlayTest::fitPageSizes()
{
    QFETCH(QSizeF, left);
    QFETCH(QSizeF, right);
    QFETCH(QSizeF, expectedLeft);
    QFETCH(QSizeF, expectedRight);
    QFETCH(bool, rotateLeft);
    OverlayFixture fixture({left}, {right}, {{0, 0}}, rotateLeft);
    pdfdiff::OverlaySettings settings;
    settings.scaleMode = pdfdiff::OverlaySettings::ScaleMode::Fit;
    fixture.update(settings);
    const auto& layout = fixture.mapper.getLayout();
    QCOMPARE(layout.size(), size_t(2));
    QVERIFY(closeSize(layout[0].pageRectMM.size(), expectedLeft));
    QVERIFY(closeSize(layout[1].pageRectMM.size(), expectedRight));
    QCOMPARE(layout[0].pageRectMM.center(), layout[1].pageRectMM.center());
    QCOMPARE(layout[0].groupIndex, 1);
    QCOMPARE(layout[1].groupIndex, 2);
    QCOMPARE(fixture.mapper.getLeftPageIndex(0), 0);
    QCOMPARE(fixture.mapper.getRightPageIndex(1), 0);
    // Scaling the view must never rewrite the source pages.
    QVERIFY(closeSize(fixture.right.getCatalog()->getPage(0)->getRotatedMediaBoxMM().size(), right));
}

void DiffOverlayTest::manualScaleAndShift()
{
    OverlayFixture fixture({{100, 200}, {100, 200}}, {{200, 400}, {200, 400}}, {{0, 0}, {1, 1}});
    pdfdiff::OverlaySettings settings;
    settings.scaleMode = pdfdiff::OverlaySettings::ScaleMode::Manual;
    settings.leftScale = 2.0;
    settings.rightScale = 0.75;
    settings.rightOffsetMM = QPointF(12.5, -160.0);
    fixture.update(settings);
    const auto& layout = fixture.mapper.getLayout();
    QCOMPARE(layout.size(), size_t(4));
    QVERIFY(closeSize(layout[0].pageRectMM.size(), QSizeF(200, 400)));
    QVERIFY(closeSize(layout[1].pageRectMM.size(), QSizeF(150, 300)));
    QCOMPARE(layout[1].pageRectMM.center() - layout[0].pageRectMM.center(), settings.rightOffsetMM);
    const QRectF firstPair = layout[0].pageRectMM.united(layout[1].pageRectMM);
    const QRectF secondPair = layout[2].pageRectMM.united(layout[3].pageRectMM);
    QCOMPARE(firstPair.top(), 0.0);
    QVERIFY(qAbs(secondPair.top() - firstPair.bottom() - 5.0) < 0.001);
    QCOMPARE(fixture.mapper.getPageIndexFromRightPageIndex(1), 3);
}

void DiffOverlayTest::originalLayoutAndOtherViews()
{
    OverlayFixture fixture({{100, 200}}, {{200, 400}}, {{0, 0}});
    fixture.update();
    const auto original = fixture.mapper.getLayout();
    QVERIFY(closeSize(original[0].pageRectMM.size(), QSizeF(100, 200)));
    QVERIFY(closeSize(original[1].pageRectMM.size(), QSizeF(200, 400)));
    QCOMPARE(original[0].pageRectMM.top(), original[1].pageRectMM.top());

    pdfdiff::OverlaySettings settings;
    settings.scaleMode = pdfdiff::OverlaySettings::ScaleMode::Manual;
    settings.leftScale = 3.0;
    settings.rightScale = 2.0;
    settings.rightOffsetMM = QPointF(100, 100);
    for (auto mode : {pdfdiff::ComparedDocumentMapper::Mode::Combined,
                      pdfdiff::ComparedDocumentMapper::Mode::Left,
                      pdfdiff::ComparedDocumentMapper::Mode::Right})
    {
        fixture.update({}, mode);
        const auto expected = fixture.mapper.getLayout();
        fixture.update(settings, mode);
        QVERIFY(fixture.mapper.getLayout() == expected);
    }
    fixture.update(settings);
    fixture.update();
    QVERIFY(fixture.mapper.getLayout() == original);
}

void DiffOverlayTest::unpairedAndFilteredPages()
{
    OverlayFixture fixture({{100, 200}, {150, 300}}, {{200, 400}, {300, 600}}, {{0, 0}, {1, -1}, {-1, 1}});
    pdfdiff::OverlaySettings settings;
    settings.scaleMode = pdfdiff::OverlaySettings::ScaleMode::Manual;
    settings.leftScale = 2.0;
    settings.rightScale = 2.0;
    settings.rightOffsetMM = QPointF(-20, 40);
    fixture.update(settings);
    const auto& layout = fixture.mapper.getLayout();
    QCOMPARE(layout.size(), size_t(4));
    QVERIFY(closeSize(layout[2].pageRectMM.size(), QSizeF(150, 300)));
    QVERIFY(closeSize(layout[3].pageRectMM.size(), QSizeF(300, 600)));
    QCOMPARE(layout[2].groupIndex, -1);
    QCOMPARE(layout[3].groupIndex, -1);
    fixture.update(settings, pdfdiff::ComparedDocumentMapper::Mode::Overlay, true);
    QVERIFY(fixture.mapper.getLayout().empty());
    fixture.mapper.update(pdfdiff::ComparedDocumentMapper::Mode::Overlay, false, fixture.diff,
                          &fixture.left, &fixture.right, nullptr, settings);
    QVERIFY(fixture.mapper.getLayout().empty());
}

void DiffOverlayTest::controlsAndReset()
{
    pdfdiff::Settings settings;
    pdfdiff::SettingsDockWidget widget(&settings, nullptr);
    auto mode = widget.findChild<QComboBox*>("overlayScaleModeCombo");
    auto left = widget.findChild<QDoubleSpinBox*>("leftScaleSpinBox");
    auto right = widget.findChild<QDoubleSpinBox*>("rightScaleSpinBox");
    auto horizontal = widget.findChild<QDoubleSpinBox*>("horizontalOffsetSpinBox");
    auto vertical = widget.findChild<QDoubleSpinBox*>("verticalOffsetSpinBox");
    auto blend = widget.findChild<QSlider*>("transparencySlider");
    auto reset = widget.findChild<QPushButton*>("resetOverlayButton");
    QVERIFY(mode && left && right && horizontal && vertical && blend && reset);
    QCOMPARE(mode->count(), 3); // The scale modes must not be populated with color names.
    QVERIFY(!mode->isEnabled());
    widget.setOverlayEnabled(true);
    QVERIFY(mode->isEnabled());
    QVERIFY(!left->isEnabled());
    mode->setCurrentIndex(mode->findData(int(pdfdiff::OverlaySettings::ScaleMode::Manual)));
    QVERIFY(left->isEnabled());
    QVERIFY(right->isEnabled());
    left->setValue(141.42);
    right->setValue(75.0);
    horizontal->setValue(-12.5);
    vertical->setValue(25.0);
    blend->setValue(80);
    QVERIFY(qAbs(widget.getOverlaySettings().leftScale - 1.4142) < 0.000001);
    QCOMPARE(widget.getOverlaySettings().rightScale, 0.75);
    QCOMPARE(widget.getOverlaySettings().rightOffsetMM, QPointF(-12.5, 25));
    widget.setOverlayEnabled(false);
    widget.setOverlayEnabled(true);
    QCOMPARE(left->value(), 141.42);
    QSignalSpy changes(&widget, &pdfdiff::SettingsDockWidget::overlaySettingsChanged);
    QSignalSpy blendChanges(&widget, &pdfdiff::SettingsDockWidget::transparencySliderChanged);
    reset->click();
    QCOMPARE(changes.count(), 1);
    QCOMPARE(blendChanges.count(), 1);
    QCOMPARE(widget.getOverlaySettings().scaleMode, pdfdiff::OverlaySettings::ScaleMode::Original);
    QCOMPARE(widget.getOverlaySettings().leftScale, 1.0);
    QCOMPARE(widget.getOverlaySettings().rightScale, 1.0);
    QCOMPARE(widget.getOverlaySettings().rightOffsetMM, QPointF());
    QCOMPARE(widget.getTransparencySliderValue(), 50);
    QVERIFY(!left->isEnabled());
    auto scrollArea = widget.findChild<QScrollArea*>();
    QVERIFY(scrollArea);
    QVERIFY(scrollArea->widgetResizable());
}

QTEST_MAIN(DiffOverlayTest)
#include "tst_diffoverlaytest.moc"
