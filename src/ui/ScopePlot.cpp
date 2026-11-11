// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "ScopePlot.h"

#include "ui/Theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFont>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QString formatValueText(double value)
{
    return std::isfinite(value) ? QString::number(value, 'g', 6)
                                : QStringLiteral("--");
}

QString formatIndexText(double x)
{
    return QString::number(qRound64(x));
}
} // namespace

ScopePlot::ScopePlot(QWidget *parent)
    : QCustomPlot(parent)
{
    setupStyle();
    setupItems();
    m_measureTimer.start();
    setChannelCount(0);
}

void ScopePlot::setupStyle()
{
    setBackground(QBrush(QColor("#0F1419")));
    axisRect()->setBackground(QBrush(QColor("#121820")));
    setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
    axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
    axisRect()->setRangeZoomFactor(0.85);
    setNoAntialiasingOnDrag(true);
    setMouseTracking(true);
    setToolTip(QStringLiteral(
        "绘图区滚轮：同时缩放 X/Y；X 轴带滚轮：只缩放 X；Y 轴带滚轮：只缩放 Y\n"
        "X 轴带 / 绘图区双击：固定竖线；双击已有竖线：移除；Y 轴带双击：复位视图\n"
        "左键点竖线设为 A，Shift+左键设为 B；右键菜单更多"));

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
    setFollow(true, true);

    for (int channel = 0; channel < count; ++channel) {
        QCPGraph *graph = addGraph();
        graph->setName(QStringLiteral("CH%1").arg(channel + 1));
        graph->setPen(QPen(Theme::channelColor(channel), 1.5));
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setScatterStyle(QCPScatterStyle::ssNone);
        graph->setAdaptiveSampling(true);
        m_graphs.append(graph);
    }

    rebuildValueLabels(count);
    legend->setVisible(count > 0);
    axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);
    if (m_hint)
        m_hint->setVisible(true);
    resetYRange();
    updateMeasurement();
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
    setFollow(true, true);
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
    updateMeasurement();
}

void ScopePlot::setFollowLatest(bool follow)
{
    setFollow(follow, follow);
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
    setFollow(true, true);
    if (m_hint)
        m_hint->setVisible(true);
    xAxis->setRange(0, m_viewSpan);
    yAxis->setRange(-1, 1);
    clearMarkers();
    hideHover();
    updateMeasurement();
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

    syncMarkers();
    if (m_measureTimer.hasExpired(250)) {
        updateMeasurement();
        m_measureTimer.restart();
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

void ScopePlot::setupItems()
{
    m_hint = new QCPItemText(this);
    m_hint->setText(QStringLiteral("连接串口后开始接收波形"));
    m_hint->setPositionAlignment(Qt::AlignCenter);
    m_hint->position->setType(QCPItemPosition::ptAxisRectRatio);
    m_hint->position->setCoords(0.5, 0.5);
    m_hint->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11));
    m_hint->setColor(QColor("#7E8DA0"));
    m_hint->setSelectable(false);
    m_hint->setClipToAxisRect(false);

    m_hoverVertical = createVerticalLine(QColor("#9AA7B4"), true);
    m_hoverVertical->setVisible(false);
    m_hoverHorizontal = new QCPItemLine(this);
    m_hoverHorizontal->setSelectable(false);
    m_hoverHorizontal->setClipToAxisRect(true);
    m_hoverHorizontal->setPen(QPen(QColor("#9AA7B4"), 1, Qt::DashLine));
    for (QCPItemPosition *position : {m_hoverHorizontal->start, m_hoverHorizontal->end}) {
        position->setTypeX(QCPItemPosition::ptAxisRectRatio);
        position->setTypeY(QCPItemPosition::ptPlotCoords);
        position->setAxes(xAxis, yAxis);
        position->setAxisRect(axisRect());
    }
    m_hoverHorizontal->setVisible(false);
    m_hoverXBadge = createBadge(QColor("#9AA7B4"));
    m_hoverYBadge = createBadge(QColor("#9AA7B4"));

    Marker start;
    start.id = kStartMarkerId;
    start.color = QColor("#FF4D5A");
    start.line = createVerticalLine(start.color, false);
    start.label = createBadge(start.color);
    m_markers.append(start);

    Marker end;
    end.id = kEndMarkerId;
    end.color = QColor("#D66BFF");
    end.line = createVerticalLine(end.color, false);
    end.label = createBadge(end.color);
    m_markers.append(end);

    syncMarkers();
}

QCPItemLine *ScopePlot::createVerticalLine(const QColor &color, bool dashed)
{
    QCPItemLine *line = new QCPItemLine(this);
    line->setSelectable(false);
    line->setClipToAxisRect(true);
    line->setPen(QPen(color, dashed ? 1.0 : 1.4,
                      dashed ? Qt::DashLine : Qt::SolidLine));
    for (QCPItemPosition *position : {line->start, line->end}) {
        position->setTypeX(QCPItemPosition::ptPlotCoords);
        position->setTypeY(QCPItemPosition::ptAxisRectRatio);
        position->setAxes(xAxis, yAxis);
        position->setAxisRect(axisRect());
    }
    return line;
}

QCPItemText *ScopePlot::createBadge(const QColor &color)
{
    QCPItemText *badge = new QCPItemText(this);
    badge->setSelectable(false);
    badge->setClipToAxisRect(false);
    badge->position->setType(QCPItemPosition::ptAbsolute);
    badge->setFont(QFont(QStringLiteral("Segoe UI"), 8));
    badge->setColor(QColor("#EDF2F7"));
    badge->setPen(QPen(color, 1));
    badge->setBrush(QBrush(QColor(15, 20, 25, 235)));
    badge->setPadding(QMargins(4, 2, 4, 2));
    badge->setPositionAlignment(Qt::AlignHCenter | Qt::AlignTop);
    badge->setVisible(false);
    return badge;
}

void ScopePlot::setVerticalLineX(QCPItemLine *line, double x)
{
    line->start->setCoords(x, 0.0);
    line->end->setCoords(x, 1.0);
}

void ScopePlot::setHorizontalLineY(QCPItemLine *line, double y)
{
    line->start->setCoords(0.0, y);
    line->end->setCoords(1.0, y);
}

void ScopePlot::rebuildValueLabels(int count)
{
    for (QCPItemText *label : m_valueLabels)
        removeItem(label);
    m_valueLabels.clear();

    for (int channel = 0; channel < count; ++channel) {
        QCPItemText *label = new QCPItemText(this);
        label->setSelectable(false);
        label->setClipToAxisRect(true);
        label->position->setType(QCPItemPosition::ptPlotCoords);
        label->position->setAxes(xAxis, yAxis);
        label->setFont(QFont(QStringLiteral("Segoe UI"), 8));
        label->setColor(Theme::channelColor(channel));
        label->setPen(Qt::NoPen);
        label->setBrush(QBrush(QColor(15, 20, 25, 210)));
        label->setPadding(QMargins(3, 1, 3, 1));
        label->setPositionAlignment(Qt::AlignBottom | Qt::AlignHCenter);
        label->setVisible(false);
        m_valueLabels.append(label);
    }
}

bool ScopePlot::isXBand(const QPoint &pos) const
{
    const QRect rect = axisRect()->rect();
    return pos.x() >= rect.left() && pos.x() <= rect.right()
           && pos.y() > rect.bottom() && pos.y() <= rect.bottom() + 60;
}

bool ScopePlot::isYBand(const QPoint &pos) const
{
    const QRect rect = axisRect()->rect();
    return pos.y() >= rect.top() && pos.y() <= rect.bottom()
           && pos.x() < rect.left() && pos.x() >= rect.left() - 80;
}

bool ScopePlot::isPlotArea(const QPoint &pos) const
{
    return axisRect()->rect().contains(pos);
}

double ScopePlot::boundaryStart() const
{
    if (m_latestSample < 0)
        return 0.0;
    return static_cast<double>(std::max<qint64>(0, m_latestSample - m_viewSpan + 1));
}

double ScopePlot::markerX(int id) const
{
    if (id == kStartMarkerId)
        return boundaryStart();
    if (id == kEndMarkerId)
        return static_cast<double>(m_latestSample);

    for (const Marker &marker : m_markers) {
        if (marker.id == id)
            return marker.x;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double ScopePlot::sampleValueAt(const QCPGraph *graph, double key) const
{
    if (!graph || graph->data()->size() <= 0)
        return std::numeric_limits<double>::quiet_NaN();

    const auto data = graph->data();
    const double sample = std::floor(key + 0.5);
    auto it = data->findBegin(sample, false);
    auto candidate = it;
    if (candidate == data->constEnd() || candidate->key > sample) {
        if (candidate == data->constBegin())
            return std::numeric_limits<double>::quiet_NaN();
        --candidate;
    }

    double nearest = candidate->value;
    double distance = qAbs(candidate->key - sample);
    if (it != data->constEnd() && qAbs(it->key - sample) < distance) {
        nearest = it->value;
        distance = qAbs(it->key - sample);
    }
    return distance <= 1.0 ? nearest
                           : std::numeric_limits<double>::quiet_NaN();
}

void ScopePlot::syncMarkers()
{
    const bool hasData = m_latestSample >= 0;
    const QRect rect = axisRect()->rect();
    const int badgeY = rect.bottom() + 24;

    for (Marker &marker : m_markers) {
        if (marker.id == kStartMarkerId)
            marker.x = boundaryStart();
        else if (marker.id == kEndMarkerId)
            marker.x = static_cast<double>(m_latestSample);
        setVerticalLineX(marker.line, marker.x);

        const double pixelX = xAxis->coordToPixel(marker.x);
        const bool inView = hasData && pixelX >= rect.left() - 2
                            && pixelX <= rect.right() + 2;
        marker.line->setVisible(inView);
        marker.label->setVisible(inView);
        if (!inView)
            continue;

        const bool measured = marker.id == m_measureA || marker.id == m_measureB;
        QString text = formatIndexText(marker.x);
        if (marker.id == m_measureA)
            text.prepend(QStringLiteral("A "));
        else if (marker.id == m_measureB)
            text.prepend(QStringLiteral("B "));
        marker.label->setText(text);
        marker.label->setPen(QPen(measured ? QColor("#F5C451") : marker.color, 1.2));
        marker.label->position->setCoords(
            qBound<double>(rect.left(), pixelX, rect.right()), badgeY);
    }
}

void ScopePlot::addFixedMarker(double x)
{
    Marker marker;
    marker.id = m_nextMarkerId++;
    marker.x = std::floor(x + 0.5);
    marker.color = QColor("#E6EDF5");
    marker.line = createVerticalLine(marker.color, false);
    marker.label = createBadge(marker.color);
    m_markers.append(marker);
    syncMarkers();
    replot(QCustomPlot::rpQueuedReplot);
}

int ScopePlot::markerAt(const QPoint &pos) const
{
    if (!isXBand(pos) && !isPlotArea(pos))
        return -1;

    int best = -1;
    double bestDistance = 9.0;
    for (int i = 0; i < m_markers.size(); ++i) {
        const Marker &marker = m_markers.at(i);
        if (!marker.line->visible())
            continue;
        const double distance = qAbs(xAxis->coordToPixel(marker.x) - pos.x());
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

void ScopePlot::setMeasureEnd(bool start, const QPoint &pos)
{
    if (!isXBand(pos) && !isPlotArea(pos))
        return;

    int index = markerAt(pos);
    if (index < 0) {
        addFixedMarker(xAxis->pixelToCoord(pos.x()));
        index = m_markers.size() - 1;
    }

    const int id = m_markers.at(index).id;
    if (start) {
        m_measureA = id;
        if (m_measureB == id)
            m_measureB = kEndMarkerId;
    } else {
        m_measureB = id;
        if (m_measureA == id)
            m_measureA = kStartMarkerId;
    }
    syncMarkers();
    updateMeasurement();
    replot(QCustomPlot::rpQueuedReplot);
}

void ScopePlot::removeMarkerAt(int index)
{
    if (index < 0 || index >= m_markers.size() || m_markers.at(index).id < 0)
        return;

    const int id = m_markers.at(index).id;
    removeItem(m_markers.at(index).line);
    removeItem(m_markers.at(index).label);
    m_markers.removeAt(index);
    if (m_measureA == id)
        m_measureA = kStartMarkerId;
    if (m_measureB == id)
        m_measureB = kEndMarkerId;

    syncMarkers();
    updateMeasurement();
    replot(QCustomPlot::rpQueuedReplot);
}

void ScopePlot::clearMarkers()
{
    for (int i = m_markers.size() - 1; i >= 0; --i) {
        if (m_markers.at(i).id < 0)
            continue;
        removeItem(m_markers.at(i).line);
        removeItem(m_markers.at(i).label);
        m_markers.removeAt(i);
    }
    m_measureA = kStartMarkerId;
    m_measureB = kEndMarkerId;
    syncMarkers();
    updateMeasurement();
}

void ScopePlot::syncHover(const QPoint &pos)
{
    m_lastHoverPosition = pos;
    const bool onX = isXBand(pos);
    const bool onY = isYBand(pos);
    const bool onPlot = isPlotArea(pos);
    m_hoverVerticalVisible = onX || onPlot;

    m_hoverVertical->setVisible(false);
    m_hoverHorizontal->setVisible(false);
    m_hoverXBadge->setVisible(false);
    m_hoverYBadge->setVisible(false);
    for (QCPItemText *label : m_valueLabels)
        label->setVisible(false);

    if (!m_hoverVerticalVisible && !onY) {
        replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    const QRect rect = axisRect()->rect();
    if (m_hoverVerticalVisible) {
        const double sample = std::floor(xAxis->pixelToCoord(pos.x()) + 0.5);
        setVerticalLineX(m_hoverVertical, sample);
        m_hoverVertical->setVisible(true);
        m_hoverXBadge->setText(formatIndexText(sample));
        m_hoverXBadge->position->setCoords(
            qBound<double>(rect.left(), pos.x(), rect.right()), rect.bottom() + 24);
        m_hoverXBadge->setVisible(true);

        for (int channel = 0; channel < m_graphs.size(); ++channel) {
            QCPGraph *graph = m_graphs.at(channel);
            QCPItemText *label = m_valueLabels.at(channel);
            if (!graph->visible())
                continue;
            const double value = sampleValueAt(graph, sample);
            if (!std::isfinite(value) || value < yAxis->range().lower
                || value > yAxis->range().upper) {
                continue;
            }
            label->setText(QStringLiteral("CH%1: %2")
                               .arg(channel + 1)
                               .arg(formatValueText(value)));
            label->position->setCoords(sample, value);
            label->setVisible(true);
        }
    }

    if (onY) {
        const double value = yAxis->pixelToCoord(pos.y());
        setHorizontalLineY(m_hoverHorizontal, value);
        m_hoverHorizontal->setVisible(true);
        m_hoverYBadge->setText(formatValueText(value));
        m_hoverYBadge->setPositionAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_hoverYBadge->position->setCoords(
            rect.left() - 6,
            qBound<double>(rect.top(), pos.y(), rect.bottom()));
        m_hoverYBadge->setVisible(true);
    }
    replot(QCustomPlot::rpQueuedReplot);
}

void ScopePlot::hideHover()
{
    m_hoverVerticalVisible = false;
    m_hoverVertical->setVisible(false);
    m_hoverHorizontal->setVisible(false);
    m_hoverXBadge->setVisible(false);
    m_hoverYBadge->setVisible(false);
    for (QCPItemText *label : m_valueLabels)
        label->setVisible(false);
    replot(QCustomPlot::rpQueuedReplot);
}

void ScopePlot::updateMeasurement()
{
    const double fromX = markerX(m_measureA);
    const double toX = markerX(m_measureB);
    QString text;

    if (m_latestSample < 0 || !std::isfinite(fromX) || !std::isfinite(toX)) {
        text = QStringLiteral("测量区间：A 起始线 → B 结束线（等待数据）");
    } else {
        const double lower = std::min(fromX, toX);
        const double upper = std::max(fromX, toX);
        text = QStringLiteral("A %1 → B %2    ΔX %3")
                   .arg(formatIndexText(fromX))
                   .arg(formatIndexText(toX))
                   .arg(formatIndexText(upper - lower));

        QString stats;
        for (int channel = 0; channel < m_graphs.size(); ++channel) {
            if (!m_graphs.at(channel)->visible())
                continue;
            bool found = false;
            double minValue = 0.0;
            double maxValue = 0.0;
            if (m_store && channel < m_store->channelCount()) {
                // 直接用环形缓冲统计，视窗外未加载到 QCPGraph 的点也能参与
                found = m_store->rangeMinMax(channel, qRound64(lower), qRound64(upper),
                                             minValue, maxValue);
            } else {
                const auto data = m_graphs.at(channel)->data();
                const auto begin = data->findBegin(lower - 0.5, false);
                const auto end = data->findEnd(upper + 0.5, false);
                for (auto it = begin; it != end; ++it) {
                    if (!std::isfinite(it->value))
                        continue;
                    if (!found) {
                        minValue = maxValue = it->value;
                        found = true;
                    } else {
                        minValue = std::min(minValue, it->value);
                        maxValue = std::max(maxValue, it->value);
                    }
                }
            }
            if (!stats.isEmpty())
                stats.append(QStringLiteral("   "));
            stats.append(found ? QStringLiteral("CH%1 [%2, %3]")
                                     .arg(channel + 1)
                                     .arg(formatValueText(minValue))
                                     .arg(formatValueText(maxValue))
                               : QStringLiteral("CH%1 [--]").arg(channel + 1));
        }
        text.append(QLatin1Char('\n'));
        text.append(stats.isEmpty() ? QStringLiteral("当前无可见通道") : stats);
    }

    if (text != m_lastMeasurement) {
        m_lastMeasurement = text;
        emit measurementChanged(text);
    }
}

void ScopePlot::setFollow(bool follow, bool autoY)
{
    const bool changed = m_followLatest != follow;
    m_followLatest = follow;
    m_autoY = autoY;
    if (changed)
        emit followLatestChanged(follow);
}

void ScopePlot::setViewSpan(qint64 span)
{
    const qint64 clamped = std::clamp(span, kMinViewSpan, kMaxViewSpan);
    if (clamped == m_viewSpan)
        return;
    m_viewSpan = clamped;
    emit viewSpanChanged(m_viewSpan);
    syncMarkers();
    updateMeasurement();
    if (m_followLatest && !m_paused && m_latestSample >= 0)
        refresh();
    replot(QCustomPlot::rpQueuedReplot);
}

void ScopePlot::resetView()
{
    setFollow(true, true);
    xAxis->setRange(0, m_viewSpan);
    yAxis->setRange(-1, 1);
    refresh();
    replot(QCustomPlot::rpQueuedReplot);
}

qint64 ScopePlot::viewSpan() const
{
    return m_viewSpan;
}

void ScopePlot::setStore(const ChannelStore *store)
{
    m_store = store;
    updateMeasurement();
}

void ScopePlot::wheelEvent(QWheelEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const bool onX = isXBand(pos);
    const bool onY = isYBand(pos);
    const bool onPlot = isPlotArea(pos);
    if (!onX && !onY && !onPlot) {
        event->accept();
        return;
    }

    const double steps = event->angleDelta().y() / 120.0;
    if (!qFuzzyIsNull(steps)) {
        const double factor = std::pow(0.85, steps);
        if (onY) {
            yAxis->scaleRange(factor, yAxis->pixelToCoord(pos.y()));
        } else {
            xAxis->scaleRange(factor, xAxis->pixelToCoord(pos.x()));
            if (onPlot)
                yAxis->scaleRange(factor, yAxis->pixelToCoord(pos.y()));
        }
        setFollow(false, false);
        syncMarkers();
        syncHover(pos);
        updateMeasurement();
    }
    event->accept();
}

void ScopePlot::mouseMoveEvent(QMouseEvent *event)
{
    QCustomPlot::mouseMoveEvent(event);
    const QPoint pos = event->position().toPoint();
    if (event->buttons() & Qt::LeftButton) {
        const QPoint delta = pos - m_pressPosition;
        if (delta.manhattanLength() > QApplication::startDragDistance()
            && qAbs(delta.x()) >= qAbs(delta.y())) {
            setFollow(false, m_autoY);
        }
    }
    syncHover(pos);
}

void ScopePlot::mousePressEvent(QMouseEvent *event)
{
    m_pressPosition = event->position().toPoint();
    m_pressWasClick = event->button() == Qt::LeftButton;
    QCustomPlot::mousePressEvent(event);
}

void ScopePlot::mouseReleaseEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const bool isClick = m_pressWasClick && event->button() == Qt::LeftButton
                         && (pos - m_pressPosition).manhattanLength() <= 4;
    m_pressWasClick = false;
    QCustomPlot::mouseReleaseEvent(event);
    if (isClick && markerAt(pos) >= 0)
        setMeasureEnd(!(event->modifiers() & Qt::ShiftModifier), pos);
}

void ScopePlot::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    m_pressWasClick = false;
    QCustomPlot::mouseDoubleClickEvent(event);

    const int index = markerAt(pos);
    if (isXBand(pos) || isPlotArea(pos)) {
        // 双击已有固定竖线可移除，其余位置固定一根新竖线
        if (index >= 0 && m_markers.at(index).id >= 0)
            removeMarkerAt(index);
        else
            addFixedMarker(xAxis->pixelToCoord(pos.x()));
    } else {
        // Y 轴带与角落保持原来的复位行为
        resetView();
    }
    updateMeasurement();
    replot(QCustomPlot::rpQueuedReplot);
}

void ScopePlot::contextMenuEvent(QContextMenuEvent *event)
{
    const QPoint pos = event->pos();
    const bool canMark = isXBand(pos) || isPlotArea(pos);
    const int index = markerAt(pos);

    QMenu menu(this);
    QAction *fixAction = canMark ? menu.addAction(QStringLiteral("在此固定竖线"))
                                 : nullptr;
    QAction *startAction = canMark ? menu.addAction(QStringLiteral("设为测量起点 A"))
                                   : nullptr;
    QAction *endAction = canMark ? menu.addAction(QStringLiteral("设为测量终点 B"))
                                 : nullptr;
    QAction *removeAction = (index >= 0 && m_markers.at(index).id >= 0)
                                ? menu.addAction(QStringLiteral("移除该竖线"))
                                : nullptr;
    menu.addSeparator();
    QAction *clearAction = menu.addAction(QStringLiteral("清除手动游标"));
    QAction *resetAction = menu.addAction(QStringLiteral("复位视图"));

    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;
    if (chosen == fixAction) {
        addFixedMarker(xAxis->pixelToCoord(pos.x()));
    } else if (chosen == startAction) {
        setMeasureEnd(true, pos);
    } else if (chosen == endAction) {
        setMeasureEnd(false, pos);
    } else if (chosen == removeAction) {
        removeMarkerAt(index);
    } else if (chosen == clearAction) {
        clearMarkers();
    } else if (chosen == resetAction) {
        resetView();
    }
    event->accept();
}

void ScopePlot::leaveEvent(QEvent *event)
{
    QCustomPlot::leaveEvent(event);
    hideHover();
}
