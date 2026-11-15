// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "ScopePlot.h"

#include "ui/Theme.h"
#include "ui/TimeFormat.h"

#include <QContextMenuEvent>
#include <QFont>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
// 数据流长时间中断后，前后两批之间不应该画一条虚假的直线。
// 超过 max(200 ms, 20 个采样周期) 就插入一个 NaN 点断开折线。
constexpr double kGapBreakMinSeconds = 0.2;
constexpr double kGapBreakPeriods = 20.0;

// QCPAxis 的 rangeChanged 也会在程序内 setRange 时触发；用这个守卫区分
// “代码在跟随最新/自动缩放”和“用户拖动、滚轮缩放”，后者应关闭对应自动模式。
class ScopedBool {
public:
    explicit ScopedBool(bool &flag)
        : m_flag(flag)
        , m_previous(flag)
    {
        m_flag = true;
    }
    ~ScopedBool() { m_flag = m_previous; }

    ScopedBool(const ScopedBool &) = delete;
    ScopedBool &operator=(const ScopedBool &) = delete;

private:
    bool &m_flag;
    bool m_previous;
};

QString formatValueText(double value)
{
    return std::isfinite(value) ? QString::number(value, 'g', 6)
                                : QStringLiteral("--");
}

// 时间轴刻度：按当前跨度自动选择 ms / s / min 单位，
// 保证同一坐标轴上的所有刻度单位一致。
class TimeAxisTicker : public QCPAxisTicker {
public:
    TimeAxisTicker()
    {
        setTickCount(6);
        setTickStepStrategy(QCPAxisTicker::tssReadability);
    }

protected:
    void generate(const QCPRange &range, const QLocale &locale, QChar formatChar, int precision,
                  QVector<double> &ticks, QVector<double> *subTicks,
                  QVector<QString> *tickLabels) override
    {
        const double span = range.size();
        if (span < 0.02) {
            m_scale = 1000.0;
            m_suffix = QStringLiteral(" ms");
            m_decimals = 1;
        } else if (span < 120.0) {
            m_scale = 1.0;
            m_suffix = QStringLiteral(" s");
            m_decimals = span < 2.0 ? 3 : 2;
        } else {
            m_scale = 1.0 / 60.0;
            m_suffix = QStringLiteral(" min");
            m_decimals = 2;
        }

        QCPAxisTicker::generate(range, locale, formatChar, precision, ticks, subTicks, tickLabels);
    }

    QString getTickLabel(double tick, const QLocale &locale, QChar formatChar,
                         int precision) override
    {
        Q_UNUSED(locale);
        Q_UNUSED(formatChar);
        Q_UNUSED(precision);
        return QString::number(tick * m_scale, 'f', m_decimals) + m_suffix;
    }

private:
    double m_scale = 1.0;
    int m_decimals = 3;
    QString m_suffix;
};
} // namespace

ScopePlot::ScopePlot(QWidget *parent)
    : QCustomPlot(parent)
{
    setupStyle();
    setupItems();
    m_measureTimer.start();

    // 用户拖动/滚轮改变 X 范围时关闭跟随最新；改变 Y 范围时关闭自动 Y。
    // 程序内的跟随刷新通过 m_settingRange 排除，不会误关。
    connect(xAxis, qOverload<const QCPRange &>(&QCPAxis::rangeChanged), this,
            [this](const QCPRange &) {
                syncMarkers();
                if (m_settingRange)
                    return;
                if (m_followLatest) {
                    m_followLatest = false;
                    emit followLatestChanged(false);
                }
                if (m_measureTimer.hasExpired(250)) {
                    updateMeasurement();
                    m_measureTimer.restart();
                }
            });
    connect(yAxis, qOverload<const QCPRange &>(&QCPAxis::rangeChanged), this,
            [this](const QCPRange &) {
                if (!m_settingRange)
                    m_autoY = false;
            });

    // 边界气泡用绝对像素定位；窗口尺寸/坐标轴边距变化后必须重新贴边，
    // 否则首次 show()、最大化、拖动分隔条时气泡会留在旧位置。
    connect(this, &QCustomPlot::afterLayout, this, [this]() { syncMarkers(); });

    setChannelCount(0);
}

void ScopePlot::setupStyle()
{
    setBackground(QBrush(QColor("#0F1419")));
    axisRect()->setBackground(QBrush(QColor("#121820")));
    setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    setPlottingHint(QCP::phFastPolylines, true);
    setNotAntialiasedElements(QCP::aePlottables);
    axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
    axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
    axisRect()->setRangeZoomFactor(0.85);
    setNoAntialiasingOnDrag(true);
    setMouseTracking(true);
    setToolTip(QStringLiteral(
        "左右两条竖线是显示缓冲边界：跟随最新时，两边界间距 = 设置的缓冲长度；波形从右边界进入、到左边界移出\n"
        "绘图区滚轮：同时缩放 X/Y；X 轴带滚轮：只缩放 X；Y 轴带滚轮：只缩放 Y\n"
        "X 轴带 / 绘图区双击：固定竖线；双击已有竖线：移除；Y 轴带双击：复位视图\n"
        "左键点竖线设为 A，Shift+左键设为 B；A/B 是测量游标，显示真实时间差，不裁剪波形；右键菜单更多"));

    xAxis->setLabel(QStringLiteral("时间"));
    yAxis->setLabel(QStringLiteral("数值"));
    xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new TimeAxisTicker));
    xAxis->setRange(0, 1.0);
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
    m_latestTime = 0.0;
    setFollow(true, true);

    for (int channel = 0; channel < count; ++channel) {
        QCPGraph *graph = addGraph();
        graph->setName(QStringLiteral("CH%1").arg(channel + 1));
        graph->setPen(QPen(Theme::channelColor(channel), 1.0));
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setAntialiased(false);
        graph->setScatterStyle(QCPScatterStyle::ssNone);
        graph->setAdaptiveSampling(true);
        m_graphs.append(graph);
    }

    rebuildValueLabels(count);
    hideHover();
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
    for (const Frame &frame : frames) {
        if (frame.values.size() != m_graphs.size())
            return;
    }

    const int frameCount = frames.size();
    QVector<QCPGraphData> points(frameCount);
    for (int channel = 0; channel < m_graphs.size(); ++channel) {
        for (int i = 0; i < frameCount; ++i) {
            points[i].key = frames.at(i).t;
            points[i].value = frames.at(i).values.at(channel);
        }
        // 直接构造 QCPGraphData，省去 keys/values 到内部数据结构的二次拷贝
        m_graphs.at(channel)->data()->add(points, true);
    }

    // 断流后恢复时，用 NaN 点断开折线，避免把空闲区间画成一条斜线。
    // 周期未知（例如首批只有一个采样点）时不断线，宁可连起来也不要误伤低采样率数据。
    if (m_latestSample >= 0 && std::isfinite(m_latestTime)) {
        double period = m_samplePeriod;
        if (period <= 0.0 && frameCount > 1) {
            period = (frames.last().t - frames.first().t)
                     / static_cast<double>(frameCount - 1);
        }
        const double gapSeconds = frames.first().t - m_latestTime;
        const double threshold = std::max(kGapBreakMinSeconds, period * kGapBreakPeriods);
        if (period > 0.0 && gapSeconds > threshold) {
            const double gapKey = m_latestTime + gapSeconds * 0.5;
            for (QCPGraph *graph : m_graphs) {
                QCPGraphData gap;
                gap.key = gapKey;
                gap.value = std::numeric_limits<double>::quiet_NaN();
                graph->data()->add(gap);
            }
        }
    }

    m_latestSample = firstSampleIndex + frameCount - 1;
    m_latestTime = frames.last().t;
    if (m_hint)
        m_hint->setVisible(false);

    for (QCPGraph *graph : m_graphs) {
        // 图形数据以时间（秒）为 key，超出环形缓冲容量后按时间裁剪最旧的数据
        const int excess = graph->data()->size() - static_cast<int>(ChannelStore::kCapacity);
        if (excess > 0) {
            const double keepFrom = graph->data()->at(excess)->key;
            graph->data()->removeBefore(keepFrom);
        }
    }
}

void ScopePlot::rebuildFromStore(const ChannelStore &store)
{
    if (m_graphs.size() != store.channelCount()) {
        setChannelCount(store.channelCount());
        // 通道数变化通常意味着时间轴重来，旧游标不再有意义
        clearMarkers();
    }

    clearGraphData();

    if (store.channelCount() <= 0 || store.sampleCount() <= 0)
        return;

    // 只回填当前视窗，避免暂停恢复时把 20 万点/通道全部灌入 QCPGraph
    const qint64 to = store.sampleCount() - 1;
    const double latestTime = store.timeAt(to);
    // 视窗左端 = 最新时刻 - 窗口长度，按时间反查起始序号
    const double windowStart = latestTime - static_cast<double>(m_viewSpan) / 1000.0;
    qint64 from = store.indexAtTime(windowStart);
    if (from < 0)
        from = store.firstStoredIndex();
    from = std::max(from, store.firstStoredIndex());
    if (from > to)
        from = to;
    const int sampleCount = static_cast<int>(to - from + 1);

    QVector<double> keys(sampleCount);
    for (int i = 0; i < sampleCount; ++i)
        keys[i] = store.timeAt(from + i);

    // 重建窗口时也按同样的规则断开数据空洞；否则暂停/恢复后，
    // 实时刷新时已经断开的折线会在同一条记录上重新连起来。
    QVector<double> gapKeys;
    if (sampleCount > 1) {
        double period = m_samplePeriod;
        if (period <= 0.0)
            period = (keys.last() - keys.first()) / static_cast<double>(sampleCount - 1);
        const double threshold = std::max(kGapBreakMinSeconds, period * kGapBreakPeriods);
        if (period > 0.0 && std::isfinite(threshold)) {
            for (int i = 1; i < sampleCount; ++i) {
                const double gap = keys.at(i) - keys.at(i - 1);
                if (gap > threshold)
                    gapKeys.append(keys.at(i - 1) + gap * 0.5);
            }
        }
    }

    for (int channel = 0; channel < store.channelCount(); ++channel) {
        QVector<double> values(sampleCount);
        for (int i = 0; i < sampleCount; ++i)
            values[i] = store.valueAt(channel, from + i);
        m_graphs.at(channel)->addData(keys, values, true);
        for (double gapKey : gapKeys) {
            QCPGraphData gap;
            gap.key = gapKey;
            gap.value = std::numeric_limits<double>::quiet_NaN();
            m_graphs.at(channel)->data()->add(gap);
        }
    }

    m_latestSample = to;
    m_latestTime = store.timeAt(to);
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
    if (!follow)
        return;

    applyWindow();
    if (!m_paused && m_latestSample >= 0 && m_autoY)
        refresh();
    else
        replot(QCustomPlot::rpQueuedReplot);
}

void ScopePlot::setPaused(bool paused)
{
    m_paused = paused;
}

void ScopePlot::setSamplePeriod(double seconds)
{
    m_samplePeriod = (std::isfinite(seconds) && seconds > 0.0) ? seconds : 0.0;
}

void ScopePlot::clearData()
{
    clearGraphData();
    clearMarkers();
    replot(QCustomPlot::rpQueuedReplot);
}

// 清空曲线数据并把显示窗口复位到“无数据”状态，但保留用户固定的游标。
// 暂停恢复时用后者重建最近窗口，不能顺手把用户量好的 A/B 游标丢掉。
void ScopePlot::clearGraphData()
{
    for (QCPGraph *graph : m_graphs)
        graph->data()->clear();

    m_latestSample = -1;
    m_latestTime = 0.0;
    setFollow(true, true);
    if (m_hint)
        m_hint->setVisible(true);
    applyWindow();
    setYRangeProgrammatic(-1, 1);
    hideHover();
    syncMarkers();
    updateMeasurement();
}

QColor ScopePlot::channelColor(int channel) const
{
    return Theme::channelColor(channel);
}

void ScopePlot::refresh()
{
    if (m_paused || m_latestSample < 0)
        return;

    if (m_followLatest)
        applyWindow();

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
            setYRangeProgrammatic(-1, 1);
        } else {
            double lower = yRange.lower;
            double upper = yRange.upper;
            if (lower == upper) {
                lower -= 1.0;
                upper += 1.0;
            }
            const double pad = (upper - lower) * 0.05;
            setYRangeProgrammatic(lower - pad, upper + pad);
        }
    }

    syncMarkers();
    if (m_measureTimer.hasExpired(250)) {
        updateMeasurement();
        m_measureTimer.restart();
    }
    replot(QCustomPlot::rpQueuedReplot);
}

// 把跟随窗口对齐到“最新时刻 - 窗口长度”。无数据时 m_latestTime 为 0，
// 窗口即 [-span, 0]，这样左右边界在连接前就已经在画布上。
void ScopePlot::applyWindow()
{
    const double span = static_cast<double>(m_viewSpan) / 1000.0;
    const double upper = std::isfinite(m_latestTime) ? m_latestTime : 0.0;
    setXRangeProgrammatic(upper - span, upper);
    syncMarkers();
}

void ScopePlot::setXRangeProgrammatic(double lower, double upper)
{
    ScopedBool guard(m_settingRange);
    xAxis->setRange(lower, upper);
}

void ScopePlot::setYRangeProgrammatic(double lower, double upper)
{
    ScopedBool guard(m_settingRange);
    yAxis->setRange(lower, upper);
}

void ScopePlot::resetYRange()
{
    m_autoY = true;
    applyWindow();
    setYRangeProgrammatic(-1, 1);
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
    // 边界线正好落在坐标轴边框上，放到 axes 层并关闭裁剪，避免被坐标轴/边框盖住
    start.line->setClipToAxisRect(false);
    start.line->setLayer(QStringLiteral("axes"));
    start.line->setPen(QPen(start.color, 2.0));
    start.label->setLayer(QStringLiteral("axes"));
    m_markers.append(start);

    Marker end;
    end.id = kEndMarkerId;
    end.color = QColor("#D66BFF");
    end.line = createVerticalLine(end.color, false);
    end.label = createBadge(end.color);
    end.line->setClipToAxisRect(false);
    end.line->setLayer(QStringLiteral("axes"));
    end.line->setPen(QPen(end.color, 2.0));
    end.label->setLayer(QStringLiteral("axes"));
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

double ScopePlot::markerX(int id) const
{
    if (id == kStartMarkerId)
        return xAxis->range().lower;
    if (id == kEndMarkerId)
        return xAxis->range().upper;

    for (const Marker &marker : m_markers) {
        if (marker.id == id)
            return marker.x;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double ScopePlot::sampleValueAt(const QCPGraph *graph, double time) const
{
    if (!graph || graph->data()->size() <= 0)
        return std::numeric_limits<double>::quiet_NaN();

    const auto data = graph->data();
    // 图形数据按时间递增存放，二分定位到 time 附近的两个点
    auto it = data->findBegin(time, false);
    if (it == data->constEnd()) {
        if (it == data->constBegin())
            return std::numeric_limits<double>::quiet_NaN();
        --it;
        return it->value;
    }

    double nearest = it->value;
    double distance = std::fabs(it->key - time);
    if (it != data->constBegin()) {
        auto previous = it;
        --previous;
        const double previousDistance = std::fabs(previous->key - time);
        if (previousDistance < distance) {
            nearest = previous->value;
            distance = previousDistance;
        }
    }
    return nearest;
}

qint64 ScopePlot::indexAtTime(double time) const
{
    if (!m_store)
        return -1;
    return m_store->nearestIndexAtTime(time);
}

void ScopePlot::syncMarkers()
{
    const bool hasData = m_latestSample >= 0;
    const QRect rect = axisRect()->rect();
    const int badgeY = rect.bottom() + 24;
    const QCPRange xRange = xAxis->range();

    for (Marker &marker : m_markers) {
        const bool isStart = marker.id == kStartMarkerId;
        const bool isEnd = marker.id == kEndMarkerId;
        if (isStart)
            marker.x = xRange.lower;
        else if (isEnd)
            marker.x = xRange.upper;   // X 轴是时间轴（秒），不能用样本序号
        setVerticalLineX(marker.line, marker.x);

        const double pixelX = xAxis->coordToPixel(marker.x);
        // 左右边界永远可见；用户固定的游标只在数据范围内显示
        const bool inView = (isStart || isEnd)
                            || (hasData && pixelX >= rect.left() - 2
                                && pixelX <= rect.right() + 2);
        marker.line->setVisible(inView);
        marker.label->setVisible(inView);
        if (!inView)
            continue;

        const bool measured = marker.id == m_measureA || marker.id == m_measureB;
        QString text;
        if (isStart)
            text = (m_measureA == marker.id
                        ? QStringLiteral("A·左界 ")
                        : (m_measureB == marker.id ? QStringLiteral("B·左界 ")
                                                   : QStringLiteral("左界 ")));
        else if (isEnd)
            text = (m_measureB == marker.id
                        ? QStringLiteral("B·右界 ")
                        : (m_measureA == marker.id ? QStringLiteral("A·右界 ")
                                                   : QStringLiteral("右界 ")));
        else if (marker.id == m_measureA)
            text = QStringLiteral("A ");
        else if (marker.id == m_measureB)
            text = QStringLiteral("B ");
        text += TimeFormat::formatCompact(marker.x);

        marker.label->setText(text);
        marker.label->setPen(QPen(measured ? QColor("#F5C451") : marker.color, 1.2));

        // 边界气泡贴住左右两端，避免半个气泡跑出控件
        if (isStart) {
            marker.label->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
            marker.label->position->setCoords(rect.left() + 2, badgeY);
        } else if (isEnd) {
            marker.label->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
            marker.label->position->setCoords(rect.right() - 2, badgeY);
        } else {
            marker.label->setPositionAlignment(Qt::AlignHCenter | Qt::AlignTop);
            marker.label->position->setCoords(
                qBound<double>(rect.left(), pixelX, rect.right()), badgeY);
        }
    }
}

void ScopePlot::addFixedMarker(double x)
{
    Marker marker;
    marker.id = m_nextMarkerId++;
    // 吸附到最近的采样点，避免游标落在两个采样点之间
    const qint64 index = indexAtTime(x);
    if (index >= 0 && m_store) {
        const double time = m_store->timeAt(index);
        if (std::isfinite(time))
            marker.x = time;
        else
            marker.x = x;
    } else {
        marker.x = x;
    }
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
    replot(QCustomPlot::rpQueuedReplot);
}

void ScopePlot::syncHover(const QPoint &pos)
{
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
        const double time = xAxis->pixelToCoord(pos.x());
        setVerticalLineX(m_hoverVertical, time);
        m_hoverVertical->setVisible(true);
        m_hoverXBadge->setText(TimeFormat::formatCompact(time));
        m_hoverXBadge->position->setCoords(
            qBound<double>(rect.left(), pos.x(), rect.right()), rect.bottom() + 24);
        m_hoverXBadge->setVisible(true);

        for (int channel = 0; channel < m_graphs.size(); ++channel) {
            QCPGraph *graph = m_graphs.at(channel);
            QCPItemText *label = m_valueLabels.at(channel);
            if (!graph->visible())
                continue;
            const double value = sampleValueAt(graph, time);
            if (!std::isfinite(value) || value < yAxis->range().lower
                || value > yAxis->range().upper) {
                continue;
            }
            label->setText(QStringLiteral("CH%1: %2")
                               .arg(channel + 1)
                               .arg(formatValueText(value)));
            label->position->setCoords(time, value);
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

    if (!std::isfinite(fromX) || !std::isfinite(toX)) {
        text = QStringLiteral("测量游标：A / B 未定义（等待数据）");
    } else if (m_latestSample < 0) {
        // 边界在无数据时已经存在，先显示两个边界的时间，不显示极值统计
        text = QStringLiteral("测量游标：A 左边界 %1 → B 右边界 %2（等待数据）")
                   .arg(TimeFormat::format(fromX))
                   .arg(TimeFormat::format(toX));
    } else {
        const double lower = std::min(fromX, toX);
        const double upper = std::max(fromX, toX);
        text = QStringLiteral("A %1 → B %2    Δt %3")
                   .arg(TimeFormat::format(fromX))
                   .arg(TimeFormat::format(toX))
                   .arg(TimeFormat::formatDelta(upper - lower));

        QString stats;
        for (int channel = 0; channel < m_graphs.size(); ++channel) {
            if (!m_graphs.at(channel)->visible())
                continue;
            bool found = false;
            double minValue = 0.0;
            double maxValue = 0.0;
            if (m_store && channel < m_store->channelCount()) {
                // 直接用环形缓冲统计，视窗外未加载到 QCPGraph 的点也能参与
                const qint64 fromIndex = m_store->indexAtTime(lower);
                const qint64 toIndex = m_store->indexAtTime(upper);
                if (fromIndex >= 0 && toIndex >= fromIndex)
                    found = m_store->rangeMinMax(channel, fromIndex, toIndex,
                                                 minValue, maxValue);
            } else {
                const auto data = m_graphs.at(channel)->data();
                const auto begin = data->findBegin(lower, false);
                const auto end = data->findEnd(upper, false);
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
    const qint64 clamped = std::clamp(span, kMinViewSpanMs, kMaxViewSpanMs);
    if (clamped == m_viewSpan)
        return;
    m_viewSpan = clamped;
    emit viewSpanChanged(m_viewSpan);

    const double spanSeconds = static_cast<double>(m_viewSpan) / 1000.0;
    if (m_followLatest) {
        // 跟随最新时右端就是最新数据；暂停或还没数据时右端是 0
        applyWindow();
    } else {
        // 手动缩放/拖动后改窗口长度：保持右端不动，把左端拉开/收拢，
        // 这样边界间距始终等于设置的缓冲长度，而不是等复位后才生效。
        double upper = xAxis->range().upper;
        if (!std::isfinite(upper))
            upper = 0.0;
        setXRangeProgrammatic(upper - spanSeconds, upper);
    }

    syncMarkers();
    updateMeasurement();
    if (!m_paused && m_latestSample >= 0 && m_autoY)
        refresh();
    else
        replot(QCustomPlot::rpQueuedReplot);
}

void ScopePlot::resetView()
{
    setFollow(true, true);
    applyWindow();
    setYRangeProgrammatic(-1, 1);
    syncMarkers();
    updateMeasurement();
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
        // 跟随/自动 Y 的关闭由 rangeChanged 处理：
        // 只缩 Y 不会停掉 X 跟随，只缩 X 也不会冻结 Y 自动缩放。
        syncMarkers();
        updateMeasurement();
        if (m_autoY && !m_paused && m_latestSample >= 0)
            refresh();
        syncHover(pos);
    }
    event->accept();
}

void ScopePlot::mouseMoveEvent(QMouseEvent *event)
{
    QCustomPlot::mouseMoveEvent(event);
    // 拖动改变坐标范围时，QCPAxis::rangeChanged 会自动关闭跟随/自动 Y
    syncHover(event->position().toPoint());
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
    else
        updateMeasurement();
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
