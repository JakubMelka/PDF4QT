//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "objectstatisticsdialog.h"
#include "ui_objectstatisticsdialog.h"

namespace pdfplugin
{

ObjectStatisticsDialog::ObjectStatisticsDialog(const pdf::PDFDocument* document, QWidget *parent) :
    QDialog(parent, Qt::Dialog | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint),
    ui(new Ui::ObjectStatisticsDialog),
    m_document(document)
{
    ui->setupUi(this);

    ui->comboBox->addItem(tr("Statistics by Object Function"), int(ByObjectClass));
    ui->comboBox->addItem(tr("Statistics by Object Type"), int(ByObjectType));
    ui->comboBox->setCurrentIndex(ui->comboBox->findData(int(ByObjectClass), Qt::UserRole, Qt::MatchExactly));
    connect(ui->comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ObjectStatisticsDialog::updateStatisticsWidget);

    pdf::PDFObjectClassifier classifier;
    classifier.classify(document);
    m_statistics = classifier.calculateStatistics(document);

    updateStatisticsWidget();
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

    ui->graphWidget->setStatistics(qMove(statistics));
}

}   // namespace pdfplugin
