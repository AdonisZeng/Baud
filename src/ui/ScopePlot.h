// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "model/ChannelStore.h"
#include "protocol/ProtocolTypes.h"

#include "qcustomplot.h"

#include <QColor>
#include <QElapsedTimer>
#include <QPoint>
#include <QString>
#include <QVector>

class QContextMenuEvent;
class QEvent;
class QMouseEvent;
class QWheelEvent;

// 基于 QCustomPlot 的实时波形控件
class ScopePlot : public QCustomPlot {
    Q_OBJECT

public:
    explicit ScopePlot(QWidget *parent = nullptr);

    // 视图窗口长度（毫秒）
    static constexpr qint64 kMinViewSpanMs = 10;
    static constexpr qint64 kMaxViewSpanMs = 600000;
    static constexpr qint64 kDefaultViewSpanMs = 5000;

    void setChannelCount(int count);
    void appendFrames(const FrameBatch &frames, qint64 firstSampleIndex);
    void rebuildFromStore(const ChannelStore &store);
    void setChannelVisible(int channel, bool visible);
    void setFollowLatest(bool follow);
    void setPaused(bool paused);
    // 实测采样周期（秒），用于判断数据流中断并断开折线
    void setSamplePeriod(double seconds);
    void setStore(const ChannelStore *store);
    void clearData();
    QColor channelColor(int channel) const;
    qint64 viewSpan() const;

signals:
    void viewSpanChanged(qint64 spanMs);
    void followLatestChanged(bool follow);
    void measurementChanged(const QString &text);

public slots:
    void refresh();
    void setViewSpan(qint64 span);
    void resetView();
    void clearMarkers();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // 特殊游标 id 为负数，用户固定的游标 id >= 0
    static constexpr int kStartMarkerId = -2;
    static constexpr int kEndMarkerId = -1;

    struct Marker {
        int id = 0;
        double x = 0.0;   // X 坐标（秒）
        QColor color;
        QCPItemLine *line = nullptr;
        QCPItemText *label = nullptr;
    };

    void setupStyle();
    void resetYRange();
    void setupItems();
    void rebuildValueLabels(int count);

    // 以“最新时刻 - 窗口长度”为显示窗口；无数据时窗口为 [-span, 0]，
    // 让左右边界在连接前就可见。
    void applyWindow();
    void setXRangeProgrammatic(double lower, double upper);
    void setYRangeProgrammatic(double lower, double upper);
    // 只清曲线数据，保留用户固定的游标（暂停恢复时使用）
    void clearGraphData();

    QCPItemLine *createVerticalLine(const QColor &color, bool dashed);
    QCPItemText *createBadge(const QColor &color);
    void setVerticalLineX(QCPItemLine *line, double x);
    void setHorizontalLineY(QCPItemLine *line, double y);

    void addFixedMarker(double x);
    int markerAt(const QPoint &pos) const;
    void setMeasureEnd(bool start, const QPoint &pos);
    void removeMarkerAt(int index);
    void syncMarkers();
    void syncHover(const QPoint &pos);
    void hideHover();
    void updateMeasurement();
    void setFollow(bool follow, bool autoY);

    bool isXBand(const QPoint &pos) const;
    bool isYBand(const QPoint &pos) const;
    bool isPlotArea(const QPoint &pos) const;
    double markerX(int id) const;
    // 按时间坐标取最近的采样点值
    double sampleValueAt(const QCPGraph *graph, double time) const;
    // 时间坐标 -> 最接近的采样序号
    qint64 indexAtTime(double time) const;

    const ChannelStore *m_store = nullptr;
    QVector<QCPGraph *> m_graphs;
    QVector<QCPItemText *> m_valueLabels;
    QVector<Marker> m_markers;

    QCPItemText *m_hint = nullptr;
    QCPItemLine *m_hoverVertical = nullptr;
    QCPItemLine *m_hoverHorizontal = nullptr;
    QCPItemText *m_hoverXBadge = nullptr;
    QCPItemText *m_hoverYBadge = nullptr;

    QPoint m_pressPosition;
    qint64 m_latestSample = -1;    // 最新帧的序号（查环形缓冲用）
    double m_latestTime = 0.0;      // 最新帧的时间坐标（秒）
    qint64 m_viewSpan = kDefaultViewSpanMs;  // 视图窗口长度（毫秒）
    double m_samplePeriod = 0.0;             // 实测采样周期（秒），0 表示未知
    int m_nextMarkerId = 0;
    int m_measureA = kStartMarkerId;
    int m_measureB = kEndMarkerId;
    bool m_followLatest = true;
    bool m_autoY = true;
    bool m_paused = false;
    bool m_pressWasClick = false;
    bool m_hoverVerticalVisible = false;
    // 程序内调整坐标范围时置位，避免被当成用户缩放而关闭“跟随最新”/自动 Y
    bool m_settingRange = false;
    QString m_lastMeasurement;
    QElapsedTimer m_measureTimer;
};
