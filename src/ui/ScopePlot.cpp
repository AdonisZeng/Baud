// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "ScopePlot.h"

#include "model/ChannelStore.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QFont>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>

ScopePlot::ScopePlot(QWidget *parent)
    : QCustomPlot(parent)
{
    setupStyle();

    connect(this, &QCustomPlot::mousePress, this, [this](QMouseEvent *event) {
        m_pressPosition = event->position().toPoint();
    });
    connect(this, &QCustomPlot::mouseMove, this, [this](QMouseEvent *event) {
        if ((event->buttons() & Qt::LeftButton)
            && (event->position().toPoint() - m_pressPosition).manhattanLength()
                   > QApplication::startDragDistance()) {
            m_followLatest = false;
        }
    });
    connect(this, &QCustomPlot::mouseWheel, this, [this](QWheelEvent *) {
        m_followLatest = false;
        m_autoY = false;
    });
    connect(this, &QCustomPlot::mouseDoubleClick, this, [this](QMouseEvent *) {
        m_followLatest = true;
        m_autoY = true;
        m_viewSpan = kDefaultViewSpan;
        refresh();
    });

    m_hint = new QCPItemText(this);
    m_hint->setText(QStringLiteral("连接串口后开始接收波形"));
    m_hint->setPositionAlignment(Qt::AlignCenter);
    m_hint->position->setType(QCPItemPosition::ptAxisRectRatio);
    m_hint->position->setCoords(0.5, 0.5);
    m_hint->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11));
    m_hint->setColor(QColor("#7E8DA0"));
    m_hint->setSelectable(false);
    m_hint->setClipToAxisRect(false);

    setChannelCount(0);
}

void ScopePlot::setupStyle()
{
    setBackground(QBrush(QColor("#0F1419")));
    axisRect()->setBackground(QBrush(QColor("#121820")));
    setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    axisRect()->setRangeDrag(Qt::Horizontal);
    axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
    axisRect()->setRangeZoomFactor(0.85);
    setNoAntialiasingOnDrag(true);
    setToolTip(QStringLiteral("滚轮缩放时间轴与数值轴，拖动平移，双击复位"));

    xAxis->setLabel(QStringLiteral("样本"));
    yAxis->setLabel(QStringLiteral("数值"));
    xAxis->setRange(0, kDefaultViewSpan);
    yAxis->setRange(-1, 1);

    const QColor tickColor("#A9B4C0");
    const QColor labelColor("#C8D2DC");
    const QColor gridColor(255, 255, 255, 28);
    const QColor zeroLineColor(255, 255, 255, 60);

    xAxis->setTickLabelColor(tickColor);
    yAxis->setTickLabelColor(tickColor);
    xAxis->setLabelColor(labelColor);
    yAxis->setLabelColor(labelColor);
    xAxis->setBasePen(QPen(QColor(255, 255, 255, 70)));
    yAxis->setBasePen(QPen(QColor(255, 255, 255, 70)));
    xAxis->setTickPen(QPen(QColor(255, 255, 255, 50)));
    yAxis->setTickPen(QPen(QColor(255, 255, 255, 50)));
    xAxis->grid()->setPen(QPen(gridColor, 1));
    yAxis->grid()->setPen(QPen(gridColor, 1));
    xAxis->grid()->setSubGridVisible(false);
    yAxis->grid()->setSubGridVisible(false);
    xAxis->grid()->setZeroLinePen(QPen(zeroLineColor, 1, Qt::DashLine));
    yAxis->grid()->setZeroLinePen(QPen(zeroLineColor, 1, Qt::DashLine));

    const QFont tickFont(QStringLiteral("Segoe UI"), 8);
    xAxis->setTickLabelFont(tickFont);
    yAxis->setTickLabelFont(tickFont);
    xAxis->setLabelFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    yAxis->setLabelFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));

    legend->setVisible(true);
    legend->setBrush(QBrush(QColor(18, 24, 32, 210)));
    legend->setBorderPen(QPen(QColor(255, 255, 255, 45)));
    legend->setTextColor(QColor("#C8D2DC"));
    legend->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8));
    legend->setRowSpacing(2);
    axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);
}

void ScopePlot::setChannelCount(int count)
{
    clearGraphs();
    m_graphs.clear();
    m_latestSample = -1;
    m_followLatest = true;

    for (int channel = 0; channel < count; ++channel) {
        QCPGraph *graph = addGraph();
        graph->setName(QStringLiteral("CH%1").arg(channel + 1));
        graph->setPen(QPen(Theme::channelColor(channel), 1.5));
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setScatterStyle(QCPScatterStyle::ssNone);
        graph->setAdaptiveSampling(true);
        m_graphs.append(graph);
    }

    legend->setVisible(count > 0);
    axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);
    if (m_hint)
        m_hint->setVisible(true);
    resetYRange();
}

void ScopePlot::appendFrames(const FrameBatch &frames, qint64 firstSampleIndex)
{
    if (frames.isEmpty() || m_graphs.isEmpty())
        return;
    if (frames.first().values.size() != m_graphs.size())
        return;

    const int frameCount = frames.size();
    QVector<double> keys(frameCount);
    for (int i = 0; i < frameCount; ++i)
        keys[i] = static_cast<double>(firstSampleIndex + i);

    for (int channel = 0; channel < m_graphs.size(); ++channel) {
        QVector<double> values(frameCount);
        for (int i = 0; i < frameCount; ++i)
            values[i] = frames.at(i).values.at(channel);
        m_graphs.at(channel)->addData(keys, values, true);
    }

    m_latestSample = firstSampleIndex + frameCount - 1;
    if (m_hint)
        m_hint->setVisible(false);

    for (QCPGraph *graph : m_graphs) {
        const qint64 keepFrom = m_latestSample - ChannelStore::kCapacity;
        if (keepFrom > 0)
            graph->data()->removeBefore(static_cast<double>(keepFrom));
    }
}

void ScopePlot::rebuildFromStore(const ChannelStore &store)
{
    if (m_graphs.size() != store.channelCount())
        setChannelCount(store.channelCount());

    clearData();

    if (store.channelCount() <= 0 || store.sampleCount() <= 0)
        return;

    // 只回填当前视窗，避免暂停恢复时把 20 万点/通道全部灌入 QCPGraph
    const qint64 to = store.sampleCount() - 1;
    const qint64 from = std::max(store.firstStoredIndex(), to - m_viewSpan + 1);
    const int sampleCount = static_cast<int>(to - from + 1);

    QVector<double> keys(sampleCount);
    for (int i = 0; i < sampleCount; ++i)
        keys[i] = static_cast<double>(from + i);

    for (int channel = 0; channel < store.channelCount(); ++channel) {
        QVector<double> values(sampleCount);
        for (int i = 0; i < sampleCount; ++i)
            values[i] = store.valueAt(channel, from + i);
        m_graphs.at(channel)->addData(keys, values, true);
    }

    m_latestSample = to;
    m_followLatest = true;
    if (m_hint)
        m_hint->setVisible(false);
    refresh();
}

void ScopePlot::setChannelVisible(int channel, bool visible)
{
    if (channel < 0 || channel >= m_graphs.size())
        return;

    QCPGraph *graph = m_graphs.at(channel);
    graph->setVisible(visible);
    if (QCPAbstractLegendItem *item = legend->itemWithPlottable(graph))
        item->setVisible(visible);

    if (m_paused)
        replot(QCustomPlot::rpQueuedReplot);
    else
        refresh();
}

void ScopePlot::setFollowLatest(bool follow)
{
    m_followLatest = follow;
}

void ScopePlot::setPaused(bool paused)
{
    m_paused = paused;
}

void ScopePlot::clearData()
{
    for (QCPGraph *graph : m_graphs)
        graph->data()->clear();

    m_latestSample = -1;
    m_followLatest = true;
    m_autoY = true;
    if (m_hint)
        m_hint->setVisible(true);
    xAxis->setRange(0, kDefaultViewSpan);
    yAxis->setRange(-1, 1);
    replot(QCustomPlot::rpQueuedReplot);
}

QColor ScopePlot::channelColor(int channel) const
{
    return Theme::channelColor(channel);
}

void ScopePlot::refresh()
{
    if (m_paused || m_latestSample < 0)
        return;

    if (m_followLatest) {
        const qint64 lower = std::max<qint64>(0, m_latestSample - m_viewSpan + 1);
        double upper = static_cast<double>(m_latestSample);
        if (upper <= static_cast<double>(lower))
            upper = static_cast<double>(lower) + 1.0;
        xAxis->setRange(static_cast<double>(lower), upper);
    }

    if (m_autoY) {
        const QCPRange xRange = xAxis->range();
        bool found = false;
        QCPRange yRange;

        for (QCPGraph *graph : m_graphs) {
            if (!graph->visible())
                continue;

            bool currentFound = false;
            const QCPRange range = graph->getValueRange(currentFound, QCP::sdBoth, xRange);
            if (!currentFound)
                continue;
            if (!found) {
                yRange = range;
                found = true;
            } else {
                yRange.expand(range);
            }
        }

        if (!found) {
            yAxis->setRange(-1, 1);
        } else {
            double lower = yRange.lower;
            double upper = yRange.upper;
            if (lower == upper) {
                lower -= 1.0;
                upper += 1.0;
            }
            const double pad = (upper - lower) * 0.05;
            yAxis->setRange(lower - pad, upper + pad);
        }
    }

    replot(QCustomPlot::rpQueuedReplot);
}

void ScopePlot::resetYRange()
{
    m_autoY = true;
    xAxis->setRange(0, m_viewSpan);
    yAxis->setRange(-1, 1);
    replot(QCustomPlot::rpQueuedReplot);
}
