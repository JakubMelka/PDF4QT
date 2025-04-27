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

#include "inkcoveragedialog.h"
#include "ui_inkcoveragedialog.h"

#include "pdfdrawspacecontroller.h"
#include "pdfwidgetutils.h"

#include <QCloseEvent>
#include <QtConcurrent/QtConcurrent>

namespace pdfplugin
{

InkCoverageDialog::InkCoverageDialog(const pdf::PDFDocument* document, pdf::PDFWidget* widget, QWidget* parent) :
    QDialog(parent, Qt::Dialog | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint),
    ui(new Ui::InkCoverageDialog),
    m_inkMapper(widget->getCMSManager(), document),
    m_document(document),
    m_widget(widget),
    m_model(nullptr),
    m_futureWatcher(nullptr)
{
    ui->setupUi(this);

    m_inkMapper.createSpotColors(true);

    m_model = new InkCoverageStatisticsModel(this);
    ui->coverageTableView->setModel(m_model);
    ui->coverageTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(800, 600)));
    updateInkCoverage();
    pdf::PDFWidgetUtils::style(this);
}

InkCoverageDialog::~InkCoverageDialog()
{
    delete ui;
}

void InkCoverageDialog::updateInkCoverage()
{
    Q_ASSERT(!m_future.isRunning());
    Q_ASSERT(!m_futureWatcher);

    auto calculateInkCoverage = [this]() -> InkCoverageResults
    {
        InkCoverageResults results;

        pdf::PDFTransparencyRendererSettings settings;

        // Jakub Melka: debug is very slow, use multithreading
#ifdef QT_DEBUG
        settings.flags.setFlag(pdf::PDFTransparencyRendererSettings::MultithreadedPathSampler, true);
#endif

        settings.flags.setFlag(pdf::PDFTransparencyRendererSettings::ActiveColorMask, false);
        settings.flags.setFlag(pdf::PDFTransparencyRendererSettings::SeparationSimulation, m_inkMapper.getActiveSpotColorCount() > 0);
        settings.flags.setFlag(pdf::PDFTransparencyRendererSettings::SaveOriginalProcessImage, true);

        pdf::PDFInkCoverageCalculator calculator(m_document,
                                                 m_widget->getDrawWidgetProxy()->getFontCache(),
                                                 m_widget->getCMSManager(),
                                                 m_widget->getDrawWidgetProxy()->getOptionalContentActivity(),
                                                 &m_inkMapper,
                                                 m_widget->getDrawWidgetProxy()->getProgress(),
                                                 settings);

        std::vector<pdf::PDFInteger> pageIndices;
        pageIndices.resize(m_document->getCatalog()->getPageCount(), 0);
        std::iota(pageIndices.begin(), pageIndices.end(), 0);
        calculator.perform(QSize(RESOLUTION, RESOLUTION), pageIndices);

        CoverageInfo coverageInfo;

        for (const pdf::PDFInteger pageIndex : pageIndices)
        {
            const std::vector<pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo>* coverage = calculator.getInkCoverage(pageIndex);

            if (!coverage)
            {
                // Jakub Melka: Something bad happened?
                continue;
            }

            for (const auto& info : *coverage)
            {
                if (!pdf::PDFInkCoverageCalculator::findCoverageInfoByName(coverageInfo,  info.name))
                {
                    coverageInfo.push_back(info);
                    coverageInfo.back().coveredArea = 0.0;
                    coverageInfo.back().ratio = 0.0;
                }
            }
        }

        CoverageInfo templateInfo = coverageInfo;
        CoverageInfo sumInfo = coverageInfo;

        for (const pdf::PDFInteger pageIndex : pageIndices)
        {
            const std::vector<pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo>* coverage = calculator.getInkCoverage(pageIndex);
            results.pageInfo.push_back(templateInfo);

            if (!coverage)
            {
                // Jakub Melka: Something bad happened?
                continue;
            }

            CoverageInfo& currentInfo = results.pageInfo.back();

            for (const auto& info : *coverage)
            {
                pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo* channelInfo = pdf::PDFInkCoverageCalculator::findCoverageInfoByName(currentInfo, info.name);
                pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo* sumChannelInfo = pdf::PDFInkCoverageCalculator::findCoverageInfoByName(sumInfo, info.name);
                channelInfo->coveredArea = info.coveredArea;
                channelInfo->ratio = info.ratio;
                sumChannelInfo->coveredArea += info.coveredArea;
            }
        }

        results.sumInfo = qMove(sumInfo);

        return results;
    };

    m_future = QtConcurrent::run(calculateInkCoverage);
    m_futureWatcher = new QFutureWatcher<InkCoverageResults>();
    connect(m_futureWatcher, &QFutureWatcher<InkCoverageResults>::finished, this, &InkCoverageDialog::onInkCoverageCalculated);
    m_futureWatcher->setFuture(m_future);
}

void InkCoverageDialog::closeEvent(QCloseEvent* event)
{
    if (!isInkCoverageCalculated())
    {
        event->ignore();
        return;
    }

    BaseClass::closeEvent(event);
}

void InkCoverageDialog::accept()
{
    if (!isInkCoverageCalculated())
    {
        return;
    }

    BaseClass::accept();
}

void InkCoverageDialog::reject()
{
    if (!isInkCoverageCalculated())
    {
        return;
    }

    BaseClass::reject();
}

void InkCoverageDialog::onInkCoverageCalculated()
{
    Q_ASSERT(m_future.isFinished());
    InkCoverageResults results = m_future.result();
    m_model->setInkCoverageResults(qMove(results));
}

bool InkCoverageDialog::isInkCoverageCalculated() const
{
    return !(m_futureWatcher && m_futureWatcher->isRunning());
}

InkCoverageStatisticsModel::InkCoverageStatisticsModel(QObject* parent) :
    BaseClass(parent)
{

}

QModelIndex InkCoverageStatisticsModel::index(int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return createIndex(row, column, nullptr);
}

QModelIndex InkCoverageStatisticsModel::parent(const QModelIndex& child) const
{
    Q_UNUSED(child);

    return QModelIndex();
}

int InkCoverageStatisticsModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);

    if (m_inkCoverageResults.pageInfo.empty())
    {
        return 0;
    }

    return int(m_inkCoverageResults.pageInfo.size() + 1);
}

int InkCoverageStatisticsModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);

    return LastStandardColumn + getChannelCount() * LastChannelColumn;
}

QVariant InkCoverageStatisticsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    switch (role)
    {
        case Qt::DisplayRole:
        {
            const int row = index.row();
            QLocale locale;

            const bool isTotalRow = row == rowCount(index.parent()) - 1;
            if (index.column() == StandardColumnPageIndex)
            {
                if (isTotalRow)
                {
                    return tr("Total");
                }
                return locale.toString(row + 1);
            }

            Q_ASSERT(index.column() >= LastStandardColumn);

            const CoverageInfo* info = nullptr;
            if (row < m_inkCoverageResults.pageInfo.size())
            {
                info = &m_inkCoverageResults.pageInfo[row];
            }
            else
            {
                info = &m_inkCoverageResults.sumInfo;
            }

            const int channelIndex = getChannelIndex(index.column());
            EChannelColumn channelColumn = getChannelColumn(index.column());
            ChannelCoverageInfo channelInfo = info->at(channelIndex);

            switch (channelColumn)
            {
                case pdfplugin::InkCoverageStatisticsModel::ChannelColumnColorant:
                    return QString();
                case pdfplugin::InkCoverageStatisticsModel::ChannelColumnCoverageArea:
                    return locale.toString(channelInfo.coveredArea, 'f', 2);
                case pdfplugin::InkCoverageStatisticsModel::ChannelColumnCoverageRatio:
                    return !isTotalRow ? locale.toString(channelInfo.ratio * 100.0, 'f', 0) : QString();

                default:
                    Q_ASSERT(false);
                    break;
            }

            break;
        }

        case Qt::TextAlignmentRole:
        {
            if (index.column() == StandardColumnPageIndex)
            {
                return int(Qt::AlignCenter);
            }

            Q_ASSERT(index.column() >= LastStandardColumn);

            EChannelColumn channelColumn = getChannelColumn(index.column());
            switch (channelColumn)
            {
                case pdfplugin::InkCoverageStatisticsModel::ChannelColumnColorant:
                    return int(Qt::AlignLeft | Qt::AlignVCenter);

                case pdfplugin::InkCoverageStatisticsModel::ChannelColumnCoverageArea:
                case pdfplugin::InkCoverageStatisticsModel::ChannelColumnCoverageRatio:
                    return int(Qt::AlignRight | Qt::AlignVCenter);

                default:
                    Q_ASSERT(false);
                    break;
            }

            break;
        }

        case Qt::BackgroundRole:
        {
            if (index.column() >= LastStandardColumn && getChannelColumn(index.column()) == ChannelColumnColorant)
            {
                const int channelIndex = getChannelIndex(index.column());
                return QBrush(m_inkCoverageResults.sumInfo[channelIndex].color);
            }

            break;
        }

        default:
            break;
    }

    return QVariant();
}

QVariant InkCoverageStatisticsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
    {
        return QVariant();
    }

    if (section < LastStandardColumn)
    {
        Q_ASSERT(section == StandardColumnPageIndex);
        return tr("Page Index");
    }

    const EChannelColumn channelColumn = getChannelColumn(section);

    switch (channelColumn)
    {
        case ChannelColumnColorant:
        {
            const int channelIndex = getChannelIndex(section);
            return m_inkCoverageResults.sumInfo[channelIndex].textName;
        }

        case ChannelColumnCoverageArea:
            return tr("[ mm² ]");

        case ChannelColumnCoverageRatio:
            return tr("[ % ]");

        case LastChannelColumn:
            Q_ASSERT(false);
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    return QVariant();
}

void InkCoverageStatisticsModel::setInkCoverageResults(InkCoverageResults inkCoverageResults)
{
    beginResetModel();
    m_inkCoverageResults = qMove(inkCoverageResults);
    endResetModel();
}

int InkCoverageStatisticsModel::getChannelCount() const
{
    return int(m_inkCoverageResults.sumInfo.size());
}

InkCoverageStatisticsModel::EChannelColumn InkCoverageStatisticsModel::getChannelColumn(int index) const
{
    Q_ASSERT(index >= LastStandardColumn);

    index -= LastStandardColumn;
    return static_cast<EChannelColumn>(index % LastChannelColumn);
}

int InkCoverageStatisticsModel::getChannelIndex(int index) const
{
    Q_ASSERT(index >= LastStandardColumn);

    index -= LastStandardColumn;
    return index / LastChannelColumn;
}

}   // pdfplugin
