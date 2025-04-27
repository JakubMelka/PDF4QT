// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "objectstatisticsdialog.h"
#include "ui_objectstatisticsdialog.h"

#include "pdfwidgetutils.h"

namespace pdfplugin
{

ObjectStatisticsDialog::ObjectStatisticsDialog(const pdf::PDFDocument* document, QWidget *parent) :
    QDialog(parent, Qt::Dialog | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint),
    ui(new Ui::ObjectStatisticsDialog),
    m_document(document),
    m_statisticsGraphWidget(new StatisticsGraphWidget(this))
{
    ui->setupUi(this);

    ui->dialogLayout->addWidget(m_statisticsGraphWidget);

    ui->comboBox->addItem(tr("Statistics by Object Function"), int(ByObjectClass));
    ui->comboBox->addItem(tr("Statistics by Object Type"), int(ByObjectType));
    ui->comboBox->setCurrentIndex(ui->comboBox->findData(int(ByObjectClass), Qt::UserRole, Qt::MatchExactly));
    connect(ui->comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ObjectStatisticsDialog::updateStatisticsWidget);

    pdf::PDFObjectClassifier classifier;
    classifier.classify(document);
    m_statistics = classifier.calculateStatistics(document);

    updateStatisticsWidget();
    pdf::PDFWidgetUtils::style(this);
}

ObjectStatisticsDialog::~ObjectStatisticsDialog()
{
    delete ui;
}

void ObjectStatisticsDialog::updateStatisticsWidget()
{
    StatisticsGraphWidget::Statistics statistics;

    QLocale locale;

    std::array colors = {
        Qt::green,
        Qt::red,
        Qt::blue,
        Qt::cyan,
        Qt::magenta,
        Qt::yellow,
        Qt::darkGreen,
        Qt::darkRed,
        Qt::darkBlue,
        Qt::darkCyan,
        Qt::darkMagenta,
        Qt::darkYellow
    };

    const StatisticsType statisticsType = static_cast<StatisticsType>(ui->comboBox->currentData().toInt());
    switch (statisticsType)
    {
        case pdfplugin::ObjectStatisticsDialog::ByObjectClass:
        {
            statistics.title = tr("Statistics by Object Class");
            statistics.headers = QStringList() << tr("Class") << tr("Percentage [%]") << tr("Count [#]") << tr("Space Usage [bytes]");

            size_t colorId = 0;
            qint64 totalBytesCount = 0;
            for (const auto& item : m_statistics.statistics)
            {
                totalBytesCount += item.second.bytes;
            }

            auto addItem = [&, this](pdf::PDFObjectClassifier::Type type, QString classText)
            {
                auto it = m_statistics.statistics.find(type);

                if (it == m_statistics.statistics.cend())
                {
                    // Jakub Melka: no type found
                    return;
                }

                const pdf::PDFObjectClassifier::StatisticsItem& statisticsItem = it->second;

                qreal percentage = qreal(100.0) * qreal(statisticsItem.bytes) / qreal(totalBytesCount);

                StatisticsGraphWidget::StatisticsItem item;
                item.portion = percentage / qreal(100.0);
                item.color = colors[colorId++ % colors.size()];
                item.texts = QStringList() << classText << locale.toString(percentage, 'f', 2) << locale.toString(statisticsItem.count) << locale.toString(statisticsItem.bytes);
                statistics.items.emplace_back(qMove(item));
            };

            addItem(pdf::PDFObjectClassifier::Page, tr("Page"));
            addItem(pdf::PDFObjectClassifier::ContentStream, tr("Content Stream"));
            addItem(pdf::PDFObjectClassifier::GraphicState, tr("Graphic State"));
            addItem(pdf::PDFObjectClassifier::ColorSpace, tr("Color Space"));
            addItem(pdf::PDFObjectClassifier::Pattern, tr("Pattern"));
            addItem(pdf::PDFObjectClassifier::Shading, tr("Shading"));
            addItem(pdf::PDFObjectClassifier::Image, tr("Image"));
            addItem(pdf::PDFObjectClassifier::Form, tr("Form"));
            addItem(pdf::PDFObjectClassifier::Font, tr("Font"));
            addItem(pdf::PDFObjectClassifier::Action, tr("Action"));
            addItem(pdf::PDFObjectClassifier::Annotation, tr("Annotation"));
            addItem(pdf::PDFObjectClassifier::None, tr("Other"));

            break;
        }

        case pdfplugin::ObjectStatisticsDialog::ByObjectType:
        {
            statistics.title = tr("Statistics by Object Type");
            statistics.headers = QStringList() << tr("Type") << tr("Percentage [%]") << tr("Count [#]");

            qint64 totalObjectCount = 0;
            for (pdf::PDFObject::Type type : pdf::PDFObject::getTypes())
            {
                const qint64 currentObjectCount = m_statistics.objectCountByType[size_t(type)];
                totalObjectCount += currentObjectCount;
            }

            if (totalObjectCount > 0)
            {
                size_t colorId = 0;
                for (pdf::PDFObject::Type type : pdf::PDFObject::getTypes())
                {
                    const qint64 currentObjectCount = m_statistics.objectCountByType[size_t(type)];

                    if (currentObjectCount == 0)
                    {
                        continue;
                    }

                    qreal percentage = qreal(100.0) * qreal(currentObjectCount) / qreal(totalObjectCount);

                    StatisticsGraphWidget::StatisticsItem item;
                    item.portion = percentage / qreal(100.0);
                    item.color = colors[colorId++ % colors.size()];
                    item.texts = QStringList() << pdf::PDFObjectUtils::getObjectTypeName(type) << locale.toString(percentage, 'f', 2) << locale.toString(currentObjectCount);
                    statistics.items.emplace_back(qMove(item));
                }
            }

            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    m_statisticsGraphWidget->setStatistics(qMove(statistics));
}

}   // namespace pdfplugin
