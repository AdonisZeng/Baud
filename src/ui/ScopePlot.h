// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "protocol/ProtocolTypes.h"

#include "qcustomplot.h"

#include <QColor>
#include <QPoint>
#include <QVector>

class ChannelStore;

// 基于 QCustomPlot 的实时波形控件
class ScopePlot : public QCustomPlot {
    Q_OBJECT

public:
    explicit ScopePlot(QWidget *parent = nullptr);

    void setChannelCount(int count);
    void appendFrames(const FrameBatch &frames, qint64 firstSampleIndex);
    void rebuildFromStore(const ChannelStore &store);
    void setChannelVisible(int channel, bool visible);
    void setFollowLatest(bool follow);
    void setPaused(bool paused);
    void clearData();
    QColor channelColor(int channel) const;

public slots:
    void refresh();

private:
    void setupStyle();
    void resetYRange();

    static constexpr qint64 kDefaultViewSpan = 5000;

    QVector<QCPGraph *> m_graphs;
    QCPItemText *m_hint = nullptr;
    QPoint m_pressPosition;
    qint64 m_latestSample = -1;
    qint64 m_viewSpan = kDefaultViewSpan;
    bool m_followLatest = true;
    bool m_autoY = true;
    bool m_paused = false;
};
