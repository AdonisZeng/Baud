// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "io/DataExporter.h"
#include "model/ChannelConfig.h"
#include "model/ChannelStore.h"
#include "protocol/ProtocolTypes.h"
#include "serial/SerialSettings.h"
#include "ui_Baud.h"

#include <QMainWindow>

class QAction;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QThread;
class QTimer;
class CsvImporter;
class ScopePlot;
class SerialWorker;
class UpdateChecker;
class UpdateDialog;

class Baud : public QMainWindow {
    Q_OBJECT

public:
    explicit Baud(QWidget *parent = nullptr);
    ~Baud() override;

signals:
    void openPortRequested(const SerialSettings &settings, ProtocolMode mode);
    void closePortRequested();
    void protocolChangeRequested(ProtocolMode mode);
    void writeRequested(const QByteArray &bytes);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onConnectClicked();
    void onPauseClicked();
    void onClearClicked();
    void onProtocolChanged(int index);
    void onRefreshPortsClicked();
    void onPortRefreshTimer();
    void onWorkerOpened();
    void onWorkerClosed();
    void onWorkerProtocolApplied();
    void onWorkerError(const QString &message);
    void onRawBytesReceived(const QByteArray &bytes);
    void onFramesReceived(const FrameBatch &frames);
    void onUiTimer();
    void onRateTimer();
    void onRenderTimer();
    void onSendClicked();
    void onRawModeChanged(int index);
    void onSendModeChanged(int index);
    void onImportCsv();
    void onExportNeedMore();
    void onExportFinished(bool ok, const QString &message);
    void onImportFrames(const FrameBatch &frames);
    void onImportFinished(bool ok, const QString &message, qint64 rowCount, int channelCount);
    void onTimingUpdated(double period, double jitter);
    void onThemeToggled();

private:
    void buildUi();
    void setupSerialThread();
    void setupTimers();
    void refreshPorts();
    void updateConnectionUi();
    void updateStatus();
    void rebuildChannelTable(int channelCount);
    void updateChannelTable();
    void refreshChannelSwatches();
    // 通道配置：自定义名称、通道类型（状态码/错误码）与码表
    void showChannelConfigDialog();
    // 配置改动后同步到图例、通道表与测量面板
    void applyChannelConfigToUi();
    // 测量面板的通道名（改名后同步）
    void updateMeasureChannelNames();
    // 通道名称单元格的提示文字
    QString channelTypeTooltip(int channel) const;
    void updateThemeButton();
    void showUserGuide();
    void startExport(DataExporter::Format format);
    // 导出/导入线程的收尾：超时则脱离父子关系（运行中的 QThread 不能析构）
    bool stopIoThread(QThread *&thread);
    void abortExport(const QString &message);
    void appendRawData(const QByteArray &bytes);
    void resetSession();
    void stopSerialThread();
    void stopIoThreads();
    void checkForUpdates(bool automatic);

    SerialSettings currentSettings() const;
    ProtocolMode currentProtocol() const;

    Ui::BaudClass ui;

    SerialWorker *m_worker = nullptr;
    QThread *m_serialThread = nullptr;
    ChannelStore m_store;
    ChannelConfig m_channelConfig;
    ScopePlot *m_scope = nullptr;
    QStackedWidget *m_stack = nullptr;
    QSpinBox *m_viewSpanSpin = nullptr;
    QPushButton *m_followButton = nullptr;
    QPushButton *m_clearMarkersButton = nullptr;
    QLabel *m_measureSummary = nullptr;
    QLabel *m_measureDelta = nullptr;
    QTableWidget *m_measureTable = nullptr;
    QPushButton *m_themeButton = nullptr;

    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QComboBox *m_dataBitsCombo = nullptr;
    QComboBox *m_parityCombo = nullptr;
    QComboBox *m_stopBitsCombo = nullptr;
    QComboBox *m_flowCombo = nullptr;
    QPushButton *m_refreshPortsButton = nullptr;
    QPushButton *m_connectButton = nullptr;

    QComboBox *m_protocolCombo = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QPushButton *m_clearButton = nullptr;

    // 导入/导出期间要禁用的菜单项
    QAction *m_exportCsvAction = nullptr;
    QAction *m_exportMarkdownAction = nullptr;
    QAction *m_exportCompactAction = nullptr;
    QAction *m_importAction = nullptr;

    QTableWidget *m_channelTable = nullptr;
    QPushButton *m_channelConfigButton = nullptr;
    QPlainTextEdit *m_rawMonitor = nullptr;
    QComboBox *m_rawModeCombo = nullptr;

    QLineEdit *m_sendEdit = nullptr;
    QComboBox *m_sendModeCombo = nullptr;
    QComboBox *m_lineEndingCombo = nullptr;
    QPushButton *m_sendButton = nullptr;

    QLabel *m_statusState = nullptr;
    QLabel *m_statusRxRate = nullptr;
    QLabel *m_statusFrameRate = nullptr;
    QLabel *m_statusTiming = nullptr;
    QLabel *m_statusSamples = nullptr;
    QLabel *m_statusChannels = nullptr;

    QTimer *m_renderTimer = nullptr;
    QTimer *m_uiTimer = nullptr;
    QTimer *m_rateTimer = nullptr;
    QTimer *m_portTimer = nullptr;

    UpdateChecker *m_updateChecker = nullptr;
    UpdateDialog *m_updateDialog = nullptr;

    // 数据导出（CSV / Markdown）与 CSV 导入
    QThread *m_exportThread = nullptr;
    DataExporter *m_exporter = nullptr;
    QThread *m_importThread = nullptr;
    CsvImporter *m_importer = nullptr;
    qint64 m_exportFrom = 0;
    qint64 m_exportTo = 0;
    qint64 m_exportCursor = 0;

    bool m_connected = false;
    bool m_connecting = false;
    bool m_paused = false;
    bool m_shuttingDown = false;
    bool m_acceptData = true;
    bool m_plotDirty = false;
    // 导出期间冻结接收：保证导出区间对应的缓冲数据不被新数据覆盖
    bool m_exporting = false;
    // 导入期间丢弃串口数据：避免串口帧与导入帧混进同一份缓冲
    bool m_importing = false;
    int m_lastChannelCount = 0;
    int m_hexColumn = 0;

    qint64 m_rxBytes = 0;
    qint64 m_acceptedFrames = 0;
    qint64 m_lastRxBytes = 0;
    qint64 m_lastFrames = 0;
    qint64 m_rxRate = 0;
    qint64 m_frameRate = 0;

    // 由串口线程实测的采样周期与抖动（秒）
    double m_measuredPeriod = 0.0;
    double m_measuredJitter = 0.0;
};
