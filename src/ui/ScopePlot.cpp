// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "ScopePlot.h"

#include "ui/Theme.h"
#include "ui/TimeFormat.h"
#include "ui/ValueFormat.h"

#include <QContextMenuEvent>
#include <QFont>
#include <QFontMetrics>
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
    return ValueFormat::format(value);
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
    applyTheme();   // 此时游标/气泡已创建，统一按当前主题刷一遍
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
    // 右侧只是显示镜像：任何来源（自动缩放/滚轮/拖动）改动 yAxis 都要同步过去
    connect(yAxis, qOverload<const QCPRange &>(&QCPAxis::rangeChanged), this,
            [this](const QCPRange &range) { yAxis2->setRange(range); });

    // 边界气泡用绝对像素定位；窗口尺寸/坐标轴边距变化后必须重新贴边，
    // 否则首次 show()、最大化、拖动分隔条时气泡会留在旧位置。
    connect(this, &QCustomPlot::afterLayout, this, [this]() { syncMarkers(); });

    setChannelCount(0);
}

void ScopePlot::setupStyle()
{
    setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    setPlottingHint(QCP::phFastPolylines, true);
    setNotAntialiasedElements(QCP::aePlottables);
    axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
    axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
    axisRect()->setRangeZoomFactor(0.85);
    setNoAntialiasingOnDrag(true);
    setMouseTracking(true);
    // 交互说明不放在控件 tooltip 里（悬停即弹一大段文字太吵），
    // 统一收到“帮助 → 操作指南”。

    xAxis->setLabel(QStringLiteral("时间"));
    xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new TimeAxisTicker));
    xAxis->setRange(0, 1.0);
    yAxis->setRange(-1, 1);

    // Y 轴显示在右侧：yAxis 仍是图形的实际坐标轴（网格挂在它上面），
    // 但隐藏刻度/标签；右侧 yAxis2 只做镜像显示，范围随 yAxis 同步。
    yAxis->setTicks(false);
    yAxis->setTickLabels(false);
    yAxis->setLabel(QString());
    yAxis2->setVisible(true);
    yAxis2->setTickLabels(true);
    yAxis2->setLabel(QStringLiteral("数值"));
    yAxis2->setRange(yAxis->range());
    yAxis2->grid()->setVisible(false);
    yAxis2->grid()->setSubGridVisible(false);

    const QFont tickFont(QStringLiteral("Segoe UI"), 8);
    xAxis->setTickLabelFont(tickFont);
    yAxis2->setTickLabelFont(tickFont);
    xAxis->setLabelFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    yAxis2->setLabelFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));

    // 图例放左上角，避免与右侧 Y 轴刻度/标签打架
    legend->setVisible(true);
    legend->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8));
    legend->setRowSpacing(2);
    axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignLeft);
}

// QCustomPlot 不会跟随 QApplication 的 palette 变色：背景、坐标轴、网格、图例、
// 游标、悬停线、值标签都要在这里按当前主题重新着色。
void ScopePlot::applyTheme()
{
    const Theme::Colors &c = Theme::colors();

    setBackground(QBrush(c.plotCanvas));
    axisRect()->setBackground(QBrush(c.plotArea));

    xAxis->setTickLabelColor(c.tickText);
    yAxis->setTickLabelColor(c.tickText);
    yAxis2->setTickLabelColor(c.tickText);
    xAxis->setLabelColor(c.axisLabel);
    yAxis->setLabelColor(c.axisLabel);
    yAxis2->setLabelColor(c.axisLabel);
    xAxis->setBasePen(QPen(c.axisLine));
    yAxis->setBasePen(QPen(c.axisLine));
    yAxis2->setBasePen(QPen(c.axisLine));
    xAxis->setTickPen(QPen(c.axisTick));
    yAxis->setTickPen(QPen(c.axisTick));
    yAxis2->setTickPen(QPen(c.axisTick));
    xAxis->grid()->setPen(QPen(c.gridLine, 1));
    yAxis->grid()->setPen(QPen(c.gridLine, 1));
    yAxis2->grid()->setVisible(false);
    xAxis->grid()->setSubGridVisible(false);
    yAxis->grid()->setSubGridVisible(false);
    xAxis->grid()->setZeroLinePen(QPen(c.zeroLine, 1, Qt::DashLine));
    yAxis->grid()->setZeroLinePen(QPen(c.zeroLine, 1, Qt::DashLine));

    legend->setBrush(QBrush(c.legendBackground));
    legend->setBorderPen(QPen(c.legendBorder));
    legend->setTextColor(c.axisLabel);

    if (m_hint)
        m_hint->setColor(c.hint);

    if (m_hoverVertical)
        m_hoverVertical->setPen(QPen(c.hover, 1.0, Qt::DashLine));
    if (m_hoverHorizontal)
        m_hoverHorizontal->setPen(QPen(c.hover, 1.0, Qt::DashLine));
    for (QCPItemText *badge : {m_hoverXBadge, m_hoverYBadge}) {
        if (!badge)
            continue;
        badge->setColor(c.badgeText);
        badge->setPen(QPen(c.hover, 1));   // 边框也要跟随主题，否则浅色下仍是深色浅灰
        badge->setBrush(QBrush(c.badgeFill));
    }

    for (int channel = 0; channel < m_graphs.size(); ++channel)
        m_graphs.at(channel)->setPen(QPen(Theme::channelColor(channel), 1.0));

    for (int channel = 0; channel < m_valueLabels.size(); ++channel) {
        QCPItemText *label = m_valueLabels.at(channel);
        label->setColor(Theme::channelColor(channel));
        label->setBrush(QBrush(c.valueLabelFill));
    }

    for (Marker &marker : m_markers) {
        const bool isStart = marker.id == kStartMarkerId;
        const bool isEnd = marker.id == kEndMarkerId;
        if (isStart)
            marker.color = c.markerStart;
        else if (isEnd)
            marker.color = c.markerEnd;
        else if (marker.id >= 0)
            marker.color = c.markerDefault;

        if (marker.line)
            marker.line->setPen(QPen(marker.color, (isStart || isEnd) ? 2.0 : 1.4,
                                     Qt::SolidLine));
        if (marker.label) {
            marker.label->setColor(c.badgeText);
            marker.label->setBrush(QBrush(c.badgeFill));
        }
    }

    // 气泡边框的“测量中”高亮色由 syncMarkers 决定，刷完配色再同步一次
    syncMarkers();
    replot(QCustomPlot::rpQueuedReplot);
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
        graph->setName(channelDisplayName(channel));
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
    axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignLeft);
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

    // 缓冲区是硬上限：超出窗口的数据从曲线里删掉，不能再靠缩小 X 轴翻出来
    trimGraphData();
}

void ScopePlot::trimGraphData()
{
    for (QCPGraph *graph : m_graphs) {
        if (!m_bufferUnlimited && m_latestSample >= 0) {
            const double cutoff = m_latestTime - static_cast<double>(m_viewSpan) / 1000.0;
            graph->data()->removeBefore(cutoff);
        }

        // 点数上限兜底：极端高采样率下窗口内点数仍可能超过 kCapacity
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
    // 限制模式回填“最新时刻 - 缓冲时长”；导入模式回填整段已加载数据
    qint64 from = m_bufferUnlimited
                      ? store.firstStoredIndex()
                      : store.indexAtTime(latestTime
                                          - static_cast<double>(m_viewSpan) / 1000.0);
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
    else {
        if (m_mouseInside)
            syncHover(m_lastMousePos, false);
        replot(QCustomPlot::rpQueuedReplot);
    }
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
    // 悬停竖线按鼠标像素锚定：窗口滚动后重新按像素算一次时间与数值，
    // 竖线不会跟着波形左移，但穿过竖线的通道值会实时刷新。
    if (m_mouseInside)
        syncHover(m_lastMousePos, false);
    if (m_measureTimer.hasExpired(250)) {
        updateMeasurement();
        m_measureTimer.restart();
    }
    replot(QCustomPlot::rpQueuedReplot);
}

// 把跟随窗口对齐到“最新时刻 - 窗口长度”。无数据时 m_latestTime 为 0，
// 窗口即 [-span, 0]，这样左右边界在连接前就已经在画布上。
// 导入（无限制）模式下没有固定窗口长度，直接显示全部已加载数据。
void ScopePlot::applyWindow()
{
    double lower = 0.0;
    double upper = 0.0;
    bufferRange(lower, upper);
    setXRangeProgrammatic(lower, upper);
    syncMarkers();
}

// 缓冲区两端（左界/右界游标与缩放、平移的硬边界都用它）：
// 限制模式 = [最新时刻 - 缓冲时长, 最新时刻]；无限制模式 = 已加载数据的首尾。
void ScopePlot::bufferRange(double &lower, double &upper) const
{
    const double span = static_cast<double>(m_viewSpan) / 1000.0;
    if (m_latestSample < 0) {
        lower = -span;
        upper = 0.0;
        return;
    }

    upper = std::isfinite(m_latestTime) ? m_latestTime : 0.0;
    if (!m_bufferUnlimited) {
        lower = upper - span;
        return;
    }

    lower = upper;
    if (m_store && m_store->sampleCount() > 0) {
        const double first = m_store->timeAt(m_store->firstStoredIndex());
        if (std::isfinite(first) && first < lower)
            lower = first;
    }
    if (!(upper > lower))
        lower = upper - span;
}

// 用户缩放/拖动后调用：视窗宽度不超过缓冲区长度，且左右边界不越出缓冲区。
void ScopePlot::clampXRange()
{
    double lowerBound = 0.0;
    double upperBound = 0.0;
    bufferRange(lowerBound, upperBound);
    const double maxWidth = upperBound - lowerBound;
    if (!(maxWidth > 0.0) || !std::isfinite(maxWidth))
        return;

    const QCPRange range = xAxis->range();
    double width = range.size();
    if (!(width > 0.0) || !std::isfinite(width))
        return;

    if (width > maxWidth)
        width = maxWidth;

    // 先夹左端，再保证右端不越界；宽度等于窗口长度时视窗只能正好落在窗口上
    double lower = std::min(std::max(range.lower, lowerBound), upperBound - width);
    lower = std::max(lower, lowerBound);
    const double upper = lower + width;

    if (qAbs(lower - range.lower) > 1e-12 || qAbs(upper - range.upper) > 1e-12)
        setXRangeProgrammatic(lower, upper);
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
    const Theme::Colors &c = Theme::colors();

    m_hint = new QCPItemText(this);
    m_hint->setText(QStringLiteral("连接串口后开始接收波形"));
    m_hint->setPositionAlignment(Qt::AlignCenter);
    m_hint->position->setType(QCPItemPosition::ptAxisRectRatio);
    m_hint->position->setCoords(0.5, 0.5);
    m_hint->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11));
    m_hint->setColor(c.hint);
    m_hint->setSelectable(false);
    m_hint->setClipToAxisRect(false);

    m_hoverVertical = createVerticalLine(c.hover, true);
    m_hoverVertical->setVisible(false);
    m_hoverHorizontal = new QCPItemLine(this);
    m_hoverHorizontal->setSelectable(false);
    m_hoverHorizontal->setClipToAxisRect(true);
    m_hoverHorizontal->setPen(QPen(c.hover, 1, Qt::DashLine));
    for (QCPItemPosition *position : {m_hoverHorizontal->start, m_hoverHorizontal->end}) {
        position->setTypeX(QCPItemPosition::ptAxisRectRatio);
        position->setTypeY(QCPItemPosition::ptPlotCoords);
        position->setAxes(xAxis, yAxis);
        position->setAxisRect(axisRect());
    }
    m_hoverHorizontal->setVisible(false);
    m_hoverXBadge = createBadge(c.hover);
    m_hoverYBadge = createBadge(c.hover);

    Marker start;
    start.id = kStartMarkerId;
    start.color = c.markerStart;
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
    end.color = c.markerEnd;
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
    const Theme::Colors &theme = Theme::colors();
    badge->setColor(theme.badgeText);
    badge->setPen(QPen(color, 1));
    badge->setBrush(QBrush(theme.badgeFill));
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
        label->setBrush(QBrush(Theme::colors().valueLabelFill));
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
    // Y 轴已移到右侧，滚轮缩放带也跟着移到右边缘外
    return pos.y() >= rect.top() && pos.y() <= rect.bottom()
           && pos.x() > rect.right() && pos.x() <= rect.right() + 80;
}

bool ScopePlot::isPlotArea(const QPoint &pos) const
{
    return axisRect()->rect().contains(pos);
}

double ScopePlot::markerX(int id) const
{
    if (id == kStartMarkerId || id == kEndMarkerId) {
        double lower = 0.0;
        double upper = 0.0;
        bufferRange(lower, upper);
        return id == kStartMarkerId ? lower : upper;
    }

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
    // 图形数据按时间递增存放，二分定位到 time 附近的点
    auto it = data->findBegin(time, false);
    if (it == data->constEnd()) {
        if (it == data->constBegin())
            return std::numeric_limits<double>::quiet_NaN();
        --it;
    }

    double nearest = std::numeric_limits<double>::quiet_NaN();
    double distance = std::numeric_limits<double>::max();

    // 候选点：定位点、前后各一个；跳过 NaN（断流标记），
    // 否则断流点正好落在竖线附近时，该通道的值会整段消失。
    const auto consider = [&](auto candidate) {
        if (candidate == data->constEnd())
            return;
        const double value = candidate->value;
        if (!std::isfinite(value))
            return;
        const double candidateDistance = std::fabs(candidate->key - time);
        if (candidateDistance < distance) {
            distance = candidateDistance;
            nearest = value;
        }
    };

    consider(it);
    if (it != data->constBegin()) {
        auto previous = it;
        --previous;
        consider(previous);
        // 再往前看一个，避开紧邻竖线的 NaN 断点
        if (previous != data->constBegin()) {
            --previous;
            consider(previous);
        }
    }
    auto next = it;
    ++next;
    consider(next);

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

    for (Marker &marker : m_markers) {
        const bool isStart = marker.id == kStartMarkerId;
        const bool isEnd = marker.id == kEndMarkerId;
        // 左右界画在缓冲区的真实两端，不再固定贴视口边缘；
        // 放大 X 后它们会随数据边界一起移出画面。
        if (isStart || isEnd)
            marker.x = markerX(marker.id);
        setVerticalLineX(marker.line, marker.x);

        const double pixelX = xAxis->coordToPixel(marker.x);
        // 边界线在还没收到数据时也显示；用户固定的游标只在数据范围内显示
        const bool inView = (hasData || isStart || isEnd)
                            && pixelX >= rect.left() - 2
                            && pixelX <= rect.right() + 2;
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
        marker.label->setPen(
            QPen(measured ? Theme::colors().measureHighlight : marker.color, 1.2));

        // 气泡跟着竖线走；贴到左右边缘时改用左/右对齐，避免半个气泡跑出控件。
        // 关闭跟随后缓冲区继续滑动，左界会从画面内部经过，不能固定画在角落。
        if (pixelX <= rect.left() + 2.0) {
            marker.label->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
            marker.label->position->setCoords(rect.left() + 2, badgeY);
        } else if (pixelX >= rect.right() - 2.0) {
            marker.label->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
            marker.label->position->setCoords(rect.right() - 2, badgeY);
        } else {
            marker.label->setPositionAlignment(Qt::AlignHCenter | Qt::AlignTop);
            marker.label->position->setCoords(pixelX, badgeY);
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
    marker.color = Theme::colors().markerDefault;
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

void ScopePlot::syncHover(const QPoint &pos, bool requestReplot)
{
    // 记住鼠标位置：波形刷新导致 X 轴滚动时，竖线仍按这个像素锚定，
    // 不会被新数据“拉着往左走”，只有它对应的时间与数值随波形更新。
    m_lastMousePos = pos;
    m_mouseInside = true;

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
        if (requestReplot)
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

        // 先算出各可见通道在竖线处的数值，再统一排布标签：
        // 波形挨得很近时数值标签会叠在一起，只能看到其中一两个，
        // 这里按 y 排序后依次推开，保证每个通道的值都能显示出来。
        struct HoverValue {
            int channel = 0;
            double value = 0.0;
            double anchorX = 0.0;
            double anchorY = 0.0;
            double textHeight = 0.0;
            double textWidth = 0.0;
            QString text;
        };
        QVector<HoverValue> values;

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

            HoverValue entry;
            entry.channel = channel;
            entry.value = value;
            entry.anchorX = pos.x();
            entry.anchorY = yAxis->coordToPixel(value);
            entry.text = QStringLiteral("%1: %2").arg(channelDisplayName(channel),
                                                     channelValueText(channel, value));
            const QFontMetrics metrics(label->font());
            // 标签自带内边距（上下各 1、左右各 3），排布时算进去才不会贴住
            entry.textHeight = metrics.boundingRect(entry.text).height() + 2.0;
            entry.textWidth = metrics.horizontalAdvance(entry.text) + 6.0;
            values.append(entry);
        }

        std::sort(values.begin(), values.end(),
                  [](const HoverValue &a, const HoverValue &b) {
                      return a.anchorY < b.anchorY;
                  });

        // 标签底边对齐锚点，冲突时向下推开
        const double gap = 2.0;
        double lastBottom = -std::numeric_limits<double>::max();
        for (HoverValue &entry : values) {
            double top = entry.anchorY - entry.textHeight;
            if (top < lastBottom + gap)
                top = lastBottom + gap;
            entry.anchorY = top + entry.textHeight;
            lastBottom = entry.anchorY;
        }

        // 整体超出绘图区下边界时整体上移，避免最后一个标签被裁掉
        if (!values.isEmpty()) {
            const double overflow = lastBottom - rect.bottom();
            if (overflow > 0.0) {
                for (HoverValue &entry : values)
                    entry.anchorY -= overflow;
                const double minTop = values.first().anchorY - values.first().textHeight;
                if (minTop < rect.top()) {
                    const double shift = rect.top() - minTop;
                    for (HoverValue &entry : values)
                        entry.anchorY += shift;
                }
            }
        }

        for (const HoverValue &entry : values) {
            QCPItemText *label = m_valueLabels.at(entry.channel);
            // 贴着竖线显示，但文字不能被绘图区左右边缘裁掉
            const double halfWidth = entry.textWidth / 2.0 + 4.0;
            double x = entry.anchorX;
            if (rect.width() > 2.0 * halfWidth) {
                x = qBound<double>(rect.left() + halfWidth, x, rect.right() - halfWidth);
            } else {
                x = rect.center().x();
            }
            label->setText(entry.text);
            label->position->setPixelPosition(QPointF(x, entry.anchorY));
            label->setVisible(true);
        }
    }

    if (onY) {
        const double value = yAxis->pixelToCoord(pos.y());
        setHorizontalLineY(m_hoverHorizontal, value);
        m_hoverHorizontal->setVisible(true);
        m_hoverYBadge->setText(formatValueText(value));
        m_hoverYBadge->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_hoverYBadge->position->setCoords(
            rect.right() + 6,
            qBound<double>(rect.top(), pos.y(), rect.bottom()));
        m_hoverYBadge->setVisible(true);
    }

    if (requestReplot)
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
    MeasurementInfo info;
    info.from = markerX(m_measureA);
    info.to = markerX(m_measureB);
    info.hasRange = std::isfinite(info.from) && std::isfinite(info.to);
    info.hasData = m_latestSample >= 0;

    if (info.hasRange && info.hasData) {
        const double lower = std::min(info.from, info.to);
        const double upper = std::max(info.from, info.to);

        for (int channel = 0; channel < m_graphs.size(); ++channel) {
            if (!m_graphs.at(channel)->visible())
                continue;

            MeasureStat stat;
            stat.channel = channel;
            if (m_store && channel < m_store->channelCount()) {
                // 直接用环形缓冲统计，视窗外未加载到 QCPGraph 的点也能参与。
                // indexAtTime 返回的是“不大于目标时间的最后一个样本”，适合当右端；
                // 当 A 落在两个样本之间时，左端必须前进到第一个不早于 A 的样本，
                // 否则 A 之前的点会被算进区间（与按曲线统计的分支语义不一致）。
                qint64 fromIndex = m_store->indexAtTime(lower);
                const qint64 toIndex = m_store->indexAtTime(upper);
                if (fromIndex >= 0 && m_store->timeAt(fromIndex) < lower)
                    ++fromIndex;
                if (fromIndex >= 0 && toIndex >= fromIndex)
                    stat.hasData = m_store->rangeMinMax(channel, fromIndex, toIndex,
                                                        stat.minValue, stat.maxValue);
            } else {
                const auto data = m_graphs.at(channel)->data();
                const auto begin = data->findBegin(lower, false);
                const auto end = data->findEnd(upper, false);
                for (auto it = begin; it != end; ++it) {
                    if (!std::isfinite(it->value))
                        continue;
                    if (!stat.hasData) {
                        stat.minValue = stat.maxValue = it->value;
                        stat.hasData = true;
                    } else {
                        stat.minValue = std::min(stat.minValue, it->value);
                        stat.maxValue = std::max(stat.maxValue, it->value);
                    }
                }
            }
            info.stats.append(stat);
        }
    }

    // 只有内容变化才发信号：刷新期间可能 16 ms 一次，
    // 面板不必每帧重排表格。
    QString key = QStringLiteral("%1|%2|%3|%4")
                      .arg(info.hasRange)
                      .arg(info.hasData)
                      .arg(info.from, 0, 'g', 12)
                      .arg(info.to, 0, 'g', 12);
    for (const MeasureStat &stat : info.stats) {
        key += QLatin1Char('|');
        key += QString::number(stat.channel);
        key += QLatin1Char(':');
        key += stat.hasData ? QString::number(stat.minValue, 'g', 12) : QStringLiteral("-");
        key += QLatin1Char(',');
        key += stat.hasData ? QString::number(stat.maxValue, 'g', 12) : QStringLiteral("-");
    }
    if (key != m_lastMeasurement) {
        m_lastMeasurement = key;
        emit measurementChanged(info);
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
    // 无限制模式下即使数值没变，也要借这次显式设置恢复缓冲限制
    if (clamped == m_viewSpan && !m_bufferUnlimited)
        return;

    const bool spanChanged = clamped != m_viewSpan;
    m_viewSpan = clamped;
    m_bufferUnlimited = false;
    if (spanChanged)
        emit viewSpanChanged(m_viewSpan);

    // 缓冲变短时立即裁剪，变长也不能把已丢弃的数据找回来
    trimGraphData();

    if (m_followLatest) {
        // 跟随最新时窗口就是整个缓冲区；暂停或还没数据时右端是 0
        applyWindow();
    }
    // 非跟随状态保留当前缩放宽度与位置，只把它夹回新的缓冲窗口
    clampXRange();

    syncMarkers();
    updateMeasurement();
    if (!m_paused && m_latestSample >= 0 && m_autoY)
        refresh();
    else {
        if (m_mouseInside)
            syncHover(m_lastMousePos, false);
        replot(QCustomPlot::rpQueuedReplot);
    }
}

void ScopePlot::setBufferUnlimited(bool unlimited)
{
    if (m_bufferUnlimited == unlimited)
        return;

    m_bufferUnlimited = unlimited;
    if (!m_bufferUnlimited) {
        // 恢复实时限制：立即丢掉窗口外的数据，并把视窗夹回缓冲区内
        trimGraphData();
        clampXRange();
    }

    if (m_followLatest)
        applyWindow();
    syncMarkers();
    updateMeasurement();
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

void ScopePlot::setChannelConfig(const ChannelConfig *config)
{
    m_config = config;
    // 传 nullptr 表示解除引用（退出时），此时不要把图例刷回 CH{n}
    if (m_config)
        refreshChannelNames();
}

// 通道名称改动后同步图例（图例里显示的就是自定义名称）
void ScopePlot::refreshChannelNames()
{
    for (int channel = 0; channel < m_graphs.size(); ++channel)
        m_graphs.at(channel)->setName(channelDisplayName(channel));
    replot(QCustomPlot::rpQueuedReplot);
}

QString ScopePlot::channelDisplayName(int channel) const
{
    if (m_config) {
        const QString name = m_config->channelName(channel);
        if (!name.isEmpty())
            return name;
    }
    return QStringLiteral("CH%1").arg(channel);
}

// 命中码表时显示“码名：码值”，否则显示数值本身
QString ScopePlot::channelValueText(int channel, double value) const
{
    const QString number = formatValueText(value);
    if (!m_config)
        return number;
    const QString code = m_config->codeName(channel, value);
    if (code.isEmpty())
        return number;
    return QStringLiteral("%1：%2").arg(code, number);
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
        // 已经显示整个缓冲窗口时，继续缩小 X 只会误关“跟随最新”却看不到变化
        const double maxWidth = static_cast<double>(m_viewSpan) / 1000.0;
        const bool xAtMaxWidth = m_followLatest && !m_bufferUnlimited
                                 && xAxis->range().size() >= maxWidth - 1e-9;

        const double factor = std::pow(0.85, steps);
        if (onY) {
            yAxis->scaleRange(factor, yAxis->pixelToCoord(pos.y()));
        } else {
            if (!(factor > 1.0 && xAtMaxWidth))
                xAxis->scaleRange(factor, xAxis->pixelToCoord(pos.x()));
            if (onPlot)
                yAxis->scaleRange(factor, yAxis->pixelToCoord(pos.y()));
            // 缩放不能把视窗拉出缓冲区，也不能超过缓冲窗口长度
            clampXRange();
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
    // 拖动改变坐标范围时，QCPAxis::rangeChanged 会自动关闭跟随/自动 Y；
    // 平移不能越过缓冲区两端，否则画面会滑到没有数据的空白区
    if (event->buttons() & Qt::LeftButton)
        clampXRange();
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
    // 鼠标离开后不再按像素锚定竖线，等下一次 mouseMove 再恢复
    m_mouseInside = false;
    hideHover();
}
