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

    static constexpr qint64 kMinViewSpan = 100;
    static constexpr qint64 kMaxViewSpan = ChannelStore::kCapacity;
    static constexpr qint64 kDefaultViewSpan = 5000;

    void setChannelCount(int count);
    void appendFrames(const FrameBatch &frames, qint64 firstSampleIndex);
    void rebuildFromStore(const ChannelStore &store);
    void setChannelVisible(int channel, bool visible);
    void setFollowLatest(bool follow);
    void setPaused(bool paused);
    void setStore(const ChannelStore *store);
    void clearData();
    QColor channelColor(int channel) const;
    qint64 viewSpan() const;

signals:
    void viewSpanChanged(qint64 span);
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
        double x = 0.0;
        QColor color;
        QCPItemLine *line = nullptr;
        QCPItemText *label = nullptr;
    };

    void setupStyle();
    void resetYRange();
    void setupItems();
    void rebuildValueLabels(int count);

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
    double boundaryStart() const;
    double markerX(int id) const;
    double sampleValueAt(const QCPGraph *graph, double key) const;

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
    QPoint m_lastHoverPosition;
    qint64 m_latestSample = -1;
    qint64 m_viewSpan = kDefaultViewSpan;
    int m_nextMarkerId = 0;
    int m_measureA = kStartMarkerId;
    int m_measureB = kEndMarkerId;
    bool m_followLatest = true;
    bool m_autoY = true;
    bool m_paused = false;
    bool m_pressWasClick = false;
    bool m_hoverVerticalVisible = false;
    QString m_lastMeasurement;
    QElapsedTimer m_measureTimer;
};
