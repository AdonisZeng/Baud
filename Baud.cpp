// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "Baud.h"

#include "serial/SerialWorker.h"
#include "ui/ScopePlot.h"
#include "ui/Theme.h"
#include "update/UpdateChecker.h"
#include "update/UpdateDialog.h"

#include <QApplication>

#include <QAction>
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
QString protocolDisplayName(ProtocolMode mode)
{
    switch (mode) {
    case ProtocolMode::JustFloat:
        return QStringLiteral("JustFloat");
    case ProtocolMode::FireWater:
        return QStringLiteral("FireWater");
    case ProtocolMode::RawData:
        return QStringLiteral("RawData");
    }
    return QStringLiteral("Unknown");
}

QString formatValue(double value)
{
    if (!std::isfinite(value))
        return QStringLiteral("--");
    return QString::number(value, 'g', 4);
}
} // namespace

Baud::Baud(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);
    buildUi();
    setupSerialThread();
    setupTimers();
    refreshPorts();
    updateConnectionUi();
    updateStatus();
}

Baud::~Baud()
{
    stopSerialThread();
}

void Baud::buildUi()
{
    setWindowTitle(QStringLiteral("Baud · VOFA 串口上位机"));
    setMinimumSize(900, 600);
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        const int width = std::min(1280, available.width() - 40);
        const int height = std::min(800, available.height() - 80);
        resize(width, height);
        move(available.x() + (available.width() - width) / 2,
             available.y() + (available.height() - height) / 2);
    } else {
        resize(1280, 800);
    }
    ui.mainToolBar->hide();

    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    QAction *quitAction = fileMenu->addAction(QStringLiteral("退出"));
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
    QAction *pauseAction = viewMenu->addAction(QStringLiteral("暂停 / 继续"));
    connect(pauseAction, &QAction::triggered, this, [this]() {
        if (!m_pauseButton->isEnabled())
            return;
        m_pauseButton->setChecked(!m_pauseButton->isChecked());
        onPauseClicked();
    });
    QAction *clearAction = viewMenu->addAction(QStringLiteral("清空"));
    connect(clearAction, &QAction::triggered, this, &Baud::onClearClicked);

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
    QAction *updateAction = helpMenu->addAction(QStringLiteral("检查更新"));
    connect(updateAction, &QAction::triggered, this, [this]() {
        checkForUpdates(false);
    });
    helpMenu->addSeparator();
    QAction *aboutAction = helpMenu->addAction(QStringLiteral("关于"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, QStringLiteral("关于 Baud"),
                           QStringLiteral("Baud · VOFA 串口上位机\n"
                                          "版本 %1\n"
                                          "支持 RawData / JustFloat / FireWater\n"
                                          "波形渲染：QCustomPlot 2.1.1 (GPLv3)")
                               .arg(QApplication::applicationVersion()));
    });

    QWidget *central = ui.centralWidget;
    QVBoxLayout *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(8, 8, 8, 8);
    centralLayout->setSpacing(0);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);
    centralLayout->addWidget(splitter);

    // 左侧控制面板
    QWidget *left = new QWidget(splitter);
    left->setMinimumWidth(320);
    left->setMaximumWidth(430);
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    QGroupBox *serialGroup = new QGroupBox(QStringLiteral("串口"), left);
    QGridLayout *serialGrid = new QGridLayout(serialGroup);
    serialGrid->setHorizontalSpacing(8);
    serialGrid->setVerticalSpacing(8);
    serialGrid->setContentsMargins(10, 14, 10, 10);

    m_portCombo = new QComboBox(serialGroup);
    m_refreshPortsButton = new QPushButton(serialGroup);
    m_refreshPortsButton->setFixedSize(28, 28);
    m_refreshPortsButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshPortsButton->setToolTip(QStringLiteral("刷新串口列表"));
    m_baudCombo = new QComboBox(serialGroup);
    const QStringList baudRates = {QStringLiteral("3000000"), QStringLiteral("2500000"),
                                   QStringLiteral("2000000"), QStringLiteral("1500000"),
                                   QStringLiteral("1000000"), QStringLiteral("921600"),
                                   QStringLiteral("500000"),  QStringLiteral("460800"),
                                   QStringLiteral("256000"),  QStringLiteral("230400"),
                                   QStringLiteral("115200"),  QStringLiteral("57600"),
                                   QStringLiteral("38400"),   QStringLiteral("19200"),
                                   QStringLiteral("9600"),    QStringLiteral("4800"),
                                   QStringLiteral("2400"),    QStringLiteral("1200")};
    m_baudCombo->addItems(baudRates);
    m_baudCombo->setCurrentText(QStringLiteral("460800"));

    m_dataBitsCombo = new QComboBox(serialGroup);
    m_dataBitsCombo->addItem(QStringLiteral("8 位"), static_cast<int>(QSerialPort::Data8));
    m_dataBitsCombo->addItem(QStringLiteral("7 位"), static_cast<int>(QSerialPort::Data7));
    m_dataBitsCombo->addItem(QStringLiteral("6 位"), static_cast<int>(QSerialPort::Data6));
    m_dataBitsCombo->addItem(QStringLiteral("5 位"), static_cast<int>(QSerialPort::Data5));

    m_parityCombo = new QComboBox(serialGroup);
    m_parityCombo->addItem(QStringLiteral("无校验"), static_cast<int>(QSerialPort::NoParity));
    m_parityCombo->addItem(QStringLiteral("偶校验"), static_cast<int>(QSerialPort::EvenParity));
    m_parityCombo->addItem(QStringLiteral("奇校验"), static_cast<int>(QSerialPort::OddParity));
    m_parityCombo->addItem(QStringLiteral("标记"), static_cast<int>(QSerialPort::MarkParity));
    m_parityCombo->addItem(QStringLiteral("空格"), static_cast<int>(QSerialPort::SpaceParity));

    m_stopBitsCombo = new QComboBox(serialGroup);
    m_stopBitsCombo->addItem(QStringLiteral("1 位"), static_cast<int>(QSerialPort::OneStop));
    m_stopBitsCombo->addItem(QStringLiteral("1.5 位"), static_cast<int>(QSerialPort::OneAndHalfStop));
    m_stopBitsCombo->addItem(QStringLiteral("2 位"), static_cast<int>(QSerialPort::TwoStop));

    m_flowCombo = new QComboBox(serialGroup);
    m_flowCombo->addItem(QStringLiteral("无流控"), static_cast<int>(QSerialPort::NoFlowControl));
    m_flowCombo->addItem(QStringLiteral("硬件 RTS/CTS"),
                         static_cast<int>(QSerialPort::HardwareControl));
    m_flowCombo->addItem(QStringLiteral("软件 XON/XOFF"),
                         static_cast<int>(QSerialPort::SoftwareControl));

    m_connectButton = new QPushButton(QStringLiteral("连接"), serialGroup);
    m_connectButton->setObjectName(QStringLiteral("primaryButton"));
    m_connectButton->setMinimumHeight(32);

    serialGrid->addWidget(new QLabel(QStringLiteral("端口"), serialGroup), 0, 0);
    serialGrid->addWidget(m_portCombo, 0, 1);
    serialGrid->addWidget(m_refreshPortsButton, 0, 2);
    serialGrid->addWidget(new QLabel(QStringLiteral("波特率"), serialGroup), 1, 0);
    serialGrid->addWidget(m_baudCombo, 1, 1, 1, 2);
    serialGrid->addWidget(new QLabel(QStringLiteral("数据位"), serialGroup), 2, 0);
    serialGrid->addWidget(m_dataBitsCombo, 2, 1, 1, 2);
    serialGrid->addWidget(new QLabel(QStringLiteral("校验"), serialGroup), 3, 0);
    serialGrid->addWidget(m_parityCombo, 3, 1, 1, 2);
    serialGrid->addWidget(new QLabel(QStringLiteral("停止位"), serialGroup), 4, 0);
    serialGrid->addWidget(m_stopBitsCombo, 4, 1, 1, 2);
    serialGrid->addWidget(new QLabel(QStringLiteral("流控"), serialGroup), 5, 0);
    serialGrid->addWidget(m_flowCombo, 5, 1, 1, 2);
    serialGrid->addWidget(m_connectButton, 6, 0, 1, 3);
    serialGrid->setColumnStretch(1, 1);
    leftLayout->addWidget(serialGroup, 0);

    QGroupBox *protocolGroup = new QGroupBox(QStringLiteral("协议"), left);
    QGridLayout *protocolGrid = new QGridLayout(protocolGroup);
    protocolGrid->setHorizontalSpacing(8);
    protocolGrid->setVerticalSpacing(8);
    protocolGrid->setContentsMargins(10, 14, 10, 10);

    m_protocolCombo = new QComboBox(protocolGroup);
    m_protocolCombo->addItem(QStringLiteral("JustFloat（浮点波形）"),
                             static_cast<int>(ProtocolMode::JustFloat));
    m_protocolCombo->addItem(QStringLiteral("FireWater（CSV 文本）"),
                             static_cast<int>(ProtocolMode::FireWater));
    m_protocolCombo->addItem(QStringLiteral("RawData（原始字节）"),
                             static_cast<int>(ProtocolMode::RawData));

    m_pauseButton = new QPushButton(QStringLiteral("暂停"), protocolGroup);
    m_pauseButton->setCheckable(true);
    m_clearButton = new QPushButton(QStringLiteral("清空"), protocolGroup);

    QHBoxLayout *protocolButtons = new QHBoxLayout;
    protocolButtons->setContentsMargins(0, 0, 0, 0);
    protocolButtons->setSpacing(8);
    protocolButtons->addWidget(m_pauseButton);
    protocolButtons->addWidget(m_clearButton);

    protocolGrid->addWidget(new QLabel(QStringLiteral("协议"), protocolGroup), 0, 0);
    protocolGrid->addWidget(m_protocolCombo, 0, 1, 1, 2);
    protocolGrid->addLayout(protocolButtons, 1, 1, 1, 2);
    protocolGrid->setColumnStretch(1, 1);
    leftLayout->addWidget(protocolGroup, 0);

    QGroupBox *channelGroup = new QGroupBox(QStringLiteral("通道"), left);
    QVBoxLayout *channelLayout = new QVBoxLayout(channelGroup);
    channelLayout->setContentsMargins(10, 14, 10, 10);

    m_channelTable = new QTableWidget(0, 5, channelGroup);
    m_channelTable->setHorizontalHeaderLabels(
        {QStringLiteral("显示"), QStringLiteral("通道"), QStringLiteral("当前值"),
         QStringLiteral("最小"), QStringLiteral("最大")});
    m_channelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_channelTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_channelTable->setAlternatingRowColors(true);
    m_channelTable->setShowGrid(false);
    m_channelTable->verticalHeader()->setVisible(false);
    m_channelTable->verticalHeader()->setDefaultSectionSize(24);
    QHeaderView *channelHeader = m_channelTable->horizontalHeader();
    channelHeader->setSectionResizeMode(0, QHeaderView::Fixed);
    channelHeader->setSectionResizeMode(1, QHeaderView::Fixed);
    channelHeader->setSectionResizeMode(2, QHeaderView::Stretch);
    channelHeader->setSectionResizeMode(3, QHeaderView::Fixed);
    channelHeader->setSectionResizeMode(4, QHeaderView::Fixed);
    m_channelTable->setColumnWidth(0, 42);
    m_channelTable->setColumnWidth(1, 60);
    m_channelTable->setColumnWidth(3, 58);
    m_channelTable->setColumnWidth(4, 58);
    channelLayout->addWidget(m_channelTable);
    leftLayout->addWidget(channelGroup, 1);

    // 右侧波形 / 原始数据区
    QWidget *right = new QWidget(splitter);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    m_stack = new QStackedWidget(right);

    QWidget *scopePage = new QWidget(m_stack);
    QVBoxLayout *scopeLayout = new QVBoxLayout(scopePage);
    scopeLayout->setContentsMargins(0, 0, 0, 0);
    scopeLayout->setSpacing(6);

    QWidget *scopeBar = new QWidget(scopePage);
    QHBoxLayout *scopeBarLayout = new QHBoxLayout(scopeBar);
    scopeBarLayout->setContentsMargins(2, 0, 2, 0);
    scopeBarLayout->setSpacing(8);
    scopeBarLayout->addWidget(new QLabel(QStringLiteral("缓冲区上限(点)"), scopeBar));
    m_viewSpanSpin = new QSpinBox(scopeBar);
    m_viewSpanSpin->setRange(static_cast<int>(ScopePlot::kMinViewSpan),
                             static_cast<int>(ScopePlot::kMaxViewSpan));
    m_viewSpanSpin->setSingleStep(1000);
    m_viewSpanSpin->setValue(static_cast<int>(ScopePlot::kDefaultViewSpan));
    m_viewSpanSpin->setToolTip(QStringLiteral(
        "X 轴默认窗口长度，也决定红色起始线与紫色结束线之间的数据范围；"
        "底层环形缓冲固定为 200000 点/通道"));
    scopeBarLayout->addWidget(m_viewSpanSpin);
    m_followButton = new QPushButton(QStringLiteral("跟随最新"), scopeBar);
    m_followButton->setCheckable(true);
    m_followButton->setChecked(true);
    m_followButton->setToolTip(QStringLiteral("跟随最新并自动缩放 Y 轴"));
    scopeBarLayout->addWidget(m_followButton);
    m_clearMarkersButton = new QPushButton(QStringLiteral("清除游标"), scopeBar);
    scopeBarLayout->addWidget(m_clearMarkersButton);
    QLabel *scopeHint = new QLabel(
        QStringLiteral("轴带滚轮单轴缩放 · 绘图区 XY 同缩 · 双击固定/移除竖线"
                       " · 左键选 A / Shift+左键选 B · 右键更多"),
        scopeBar);
    scopeHint->setWordWrap(true);
    scopeHint->setStyleSheet(QStringLiteral("color: #7E8DA0;"));
    scopeBarLayout->addWidget(scopeHint, 1);
    scopeLayout->addWidget(scopeBar);

    m_scope = new ScopePlot(scopePage);
    m_scope->setStore(&m_store);
    scopeLayout->addWidget(m_scope, 1);

    m_measureLabel = new QLabel(
        QStringLiteral("测量区间：A 起始线 → B 结束线（等待数据）"), scopePage);
    m_measureLabel->setWordWrap(true);
    m_measureLabel->setMinimumHeight(34);
    m_measureLabel->setFont(QFont(QStringLiteral("Consolas"), 9));
    m_measureLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    scopeLayout->addWidget(m_measureLabel);
    m_stack->addWidget(scopePage);

    QWidget *rawPage = new QWidget(m_stack);
    QVBoxLayout *rawLayout = new QVBoxLayout(rawPage);
    rawLayout->setContentsMargins(0, 0, 0, 0);
    rawLayout->setSpacing(6);
    QHBoxLayout *rawToolbar = new QHBoxLayout;
    rawToolbar->setContentsMargins(0, 0, 0, 0);
    rawToolbar->setSpacing(8);
    rawToolbar->addWidget(new QLabel(QStringLiteral("显示模式"), rawPage));
    m_rawModeCombo = new QComboBox(rawPage);
    m_rawModeCombo->addItem(QStringLiteral("ASCII"), 0);
    m_rawModeCombo->addItem(QStringLiteral("HEX"), 1);
    rawToolbar->addWidget(m_rawModeCombo);
    rawToolbar->addStretch(1);
    rawLayout->addLayout(rawToolbar);
    m_rawMonitor = new QPlainTextEdit(rawPage);
    m_rawMonitor->setReadOnly(true);
    m_rawMonitor->setMaximumBlockCount(10000);
    m_rawMonitor->setFont(QFont(QStringLiteral("Consolas"), 9));
    m_rawMonitor->setPlaceholderText(QStringLiteral("RawData 模式下显示收到的原始字节"));
    rawLayout->addWidget(m_rawMonitor, 1);
    m_stack->addWidget(rawPage);
    rightLayout->addWidget(m_stack, 1);

    QWidget *sendBar = new QWidget(right);
    QHBoxLayout *sendLayout = new QHBoxLayout(sendBar);
    sendLayout->setContentsMargins(0, 0, 0, 0);
    sendLayout->setSpacing(8);
    m_sendModeCombo = new QComboBox(sendBar);
    m_sendModeCombo->addItem(QStringLiteral("ASCII"), 0);
    m_sendModeCombo->addItem(QStringLiteral("HEX"), 1);
    m_sendEdit = new QLineEdit(sendBar);
    m_sendEdit->setPlaceholderText(QStringLiteral("输入要发送的内容"));
    m_lineEndingCombo = new QComboBox(sendBar);
    m_lineEndingCombo->addItem(QStringLiteral("无行尾"), QString());
    m_lineEndingCombo->addItem(QStringLiteral("CR"), QStringLiteral("\r"));
    m_lineEndingCombo->addItem(QStringLiteral("LF"), QStringLiteral("\n"));
    m_lineEndingCombo->addItem(QStringLiteral("CRLF"), QStringLiteral("\r\n"));
    m_sendButton = new QPushButton(QStringLiteral("发送"), sendBar);
    m_sendButton->setObjectName(QStringLiteral("primaryButton"));
    sendLayout->addWidget(m_sendModeCombo);
    sendLayout->addWidget(m_sendEdit, 1);
    sendLayout->addWidget(m_lineEndingCombo);
    sendLayout->addWidget(m_sendButton);
    rightLayout->addWidget(sendBar, 0);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>() << 340 << 940);

    // 状态栏
    m_statusState = new QLabel(QStringLiteral("未连接"), this);
    m_statusState->setMinimumWidth(250);
    m_statusRxRate = new QLabel(this);
    m_statusFrameRate = new QLabel(this);
    m_statusSamples = new QLabel(this);
    m_statusChannels = new QLabel(this);
    statusBar()->addWidget(m_statusState, 1);
    statusBar()->addPermanentWidget(m_statusRxRate);
    statusBar()->addPermanentWidget(m_statusFrameRate);
    statusBar()->addPermanentWidget(m_statusSamples);
    statusBar()->addPermanentWidget(m_statusChannels);

    connect(m_refreshPortsButton, &QPushButton::clicked, this, &Baud::onRefreshPortsClicked);
    connect(m_connectButton, &QPushButton::clicked, this, &Baud::onConnectClicked);
    connect(m_protocolCombo, &QComboBox::currentIndexChanged,
            this, &Baud::onProtocolChanged);
    connect(m_pauseButton, &QPushButton::clicked, this, &Baud::onPauseClicked);
    connect(m_clearButton, &QPushButton::clicked, this, &Baud::onClearClicked);
    connect(m_viewSpanSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_scope)
            m_scope->setViewSpan(value);
    });
    connect(m_scope, &ScopePlot::viewSpanChanged, this, [this](qint64 span) {
        QSignalBlocker blocker(m_viewSpanSpin);
        m_viewSpanSpin->setValue(static_cast<int>(span));
    });
    connect(m_scope, &ScopePlot::followLatestChanged, this, [this](bool follow) {
        QSignalBlocker blocker(m_followButton);
        m_followButton->setChecked(follow);
    });
    connect(m_followButton, &QPushButton::toggled, this, [this](bool checked) {
        if (!m_scope)
            return;
        if (checked)
            m_scope->resetView();
        else
            m_scope->setFollowLatest(false);
    });
    connect(m_clearMarkersButton, &QPushButton::clicked,
            m_scope, &ScopePlot::clearMarkers);
    connect(m_scope, &ScopePlot::measurementChanged,
            m_measureLabel, &QLabel::setText);
    connect(m_sendButton, &QPushButton::clicked, this, &Baud::onSendClicked);
    connect(m_sendEdit, &QLineEdit::returnPressed, this, &Baud::onSendClicked);
    connect(m_rawModeCombo, &QComboBox::currentIndexChanged,
            this, &Baud::onRawModeChanged);
    connect(m_sendModeCombo, &QComboBox::currentIndexChanged,
            this, &Baud::onSendModeChanged);

    connect(m_channelTable, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *item) {
                if (!item || item->column() != 0 || !m_scope)
                    return;
                m_scope->setChannelVisible(item->row(),
                                           item->checkState() == Qt::Checked);
            });

    m_protocolCombo->setCurrentIndex(0);
    m_stack->setCurrentIndex(0);
}

void Baud::setupSerialThread()
{
    m_serialThread = new QThread(this);
    m_worker = new SerialWorker;
    m_worker->moveToThread(m_serialThread);

    connect(m_serialThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &Baud::openPortRequested, m_worker, &SerialWorker::openPort);
    connect(this, &Baud::closePortRequested, m_worker, &SerialWorker::closePort);
    connect(this, &Baud::protocolChangeRequested, m_worker, &SerialWorker::changeProtocol);
    connect(this, &Baud::writeRequested, m_worker, &SerialWorker::writeBytes);
    connect(m_worker, &SerialWorker::opened, this, &Baud::onWorkerOpened);
    connect(m_worker, &SerialWorker::closed, this, &Baud::onWorkerClosed);
    connect(m_worker, &SerialWorker::protocolApplied, this, &Baud::onWorkerProtocolApplied);
    connect(m_worker, &SerialWorker::errorOccurred, this, &Baud::onWorkerError);
    connect(m_worker, &SerialWorker::rawBytesReceived, this, &Baud::onRawBytesReceived);
    connect(m_worker, &SerialWorker::framesReceived, this, &Baud::onFramesReceived);

    m_serialThread->start();
}

void Baud::setupTimers()
{
    m_renderTimer = new QTimer(this);
    m_renderTimer->setInterval(33);
    connect(m_renderTimer, &QTimer::timeout, this, &Baud::onRenderTimer);

    m_uiTimer = new QTimer(this);
    m_uiTimer->setInterval(250);
    connect(m_uiTimer, &QTimer::timeout, this, &Baud::onUiTimer);

    m_rateTimer = new QTimer(this);
    m_rateTimer->setInterval(1000);
    connect(m_rateTimer, &QTimer::timeout, this, &Baud::onRateTimer);

    m_portTimer = new QTimer(this);
    m_portTimer->setInterval(2000);
    connect(m_portTimer, &QTimer::timeout, this, &Baud::onPortRefreshTimer);

    m_renderTimer->start();
    m_uiTimer->start();
    m_rateTimer->start();
    m_portTimer->start();

    if (UpdateChecker::shouldAutoCheck()) {
        QTimer::singleShot(8000, this, [this]() {
            checkForUpdates(true);
        });
    }
}

void Baud::checkForUpdates(bool automatic)
{
    if (automatic && (!UpdateChecker::shouldAutoCheck()
                      || (m_updateDialog && m_updateDialog->isVisible())))
        return;

    if (!m_updateChecker) {
        m_updateChecker = new UpdateChecker(this);
        m_updateDialog = new UpdateDialog(m_updateChecker, this);
        connect(m_updateDialog, &UpdateDialog::installLaunched,
                this, []() { QApplication::closeAllWindows(); });
    }

    if (m_updateChecker->isBusy())
        return;

    m_updateDialog->startCheck(automatic);
}

void Baud::refreshPorts()
{
    if (m_connected || m_connecting)
        return;
    if (m_portCombo->view() && m_portCombo->view()->isVisible())
        return;

    const QString previousPort = m_portCombo->currentData().toString();
    QSignalBlocker blocker(m_portCombo);
    m_portCombo->clear();

    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        const QString description = info.description().trimmed();
        const QString text = description.isEmpty()
                                 ? info.portName()
                                 : QStringLiteral("%1  ·  %2").arg(info.portName(), description);
        m_portCombo->addItem(text, info.portName());
    }

    if (m_portCombo->count() == 0)
        m_portCombo->addItem(QStringLiteral("未发现串口"), QString());

    const int previousIndex = m_portCombo->findData(previousPort);
    m_portCombo->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
}

void Baud::updateConnectionUi()
{
    const bool configEnabled = !m_connected && !m_connecting;
    const bool rawMode = currentProtocol() == ProtocolMode::RawData;

    m_portCombo->setEnabled(configEnabled);
    m_baudCombo->setEnabled(configEnabled);
    m_dataBitsCombo->setEnabled(configEnabled);
    m_parityCombo->setEnabled(configEnabled);
    m_stopBitsCombo->setEnabled(configEnabled);
    m_flowCombo->setEnabled(configEnabled);
    m_refreshPortsButton->setEnabled(configEnabled);
    m_protocolCombo->setEnabled(!m_connecting);

    m_connectButton->setEnabled(!m_connecting);
    m_connectButton->setText(m_connecting ? QStringLiteral("连接中…")
                                          : (m_connected ? QStringLiteral("断开")
                                                         : QStringLiteral("连接")));

    m_sendEdit->setEnabled(m_connected);
    m_sendModeCombo->setEnabled(m_connected);
    m_lineEndingCombo->setEnabled(m_connected);
    m_sendButton->setEnabled(m_connected);

    m_pauseButton->setEnabled(m_connected && !rawMode);
    m_clearButton->setEnabled(true);

    if (rawMode && m_pauseButton->isChecked()) {
        m_pauseButton->setChecked(false);
        m_paused = false;
        if (m_scope)
            m_scope->setPaused(false);
    }
    m_pauseButton->setText(m_paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    m_stack->setCurrentIndex(rawMode ? 1 : 0);

    if (m_connecting) {
        m_statusState->setText(QStringLiteral("连接中…"));
    } else if (m_connected) {
        m_statusState->setText(QStringLiteral("已连接 %1 @ %2 bps")
                                   .arg(m_portCombo->currentData().toString(),
                                        m_baudCombo->currentText()));
    } else {
        m_statusState->setText(QStringLiteral("未连接"));
    }
}

SerialSettings Baud::currentSettings() const
{
    SerialSettings settings;
    settings.portName = m_portCombo->currentData().toString();

    settings.baudRate = m_baudCombo->currentText().toInt();

    settings.dataBits =
        static_cast<QSerialPort::DataBits>(m_dataBitsCombo->currentData().toInt());
    settings.parity =
        static_cast<QSerialPort::Parity>(m_parityCombo->currentData().toInt());
    settings.stopBits =
        static_cast<QSerialPort::StopBits>(m_stopBitsCombo->currentData().toInt());
    settings.flowControl =
        static_cast<QSerialPort::FlowControl>(m_flowCombo->currentData().toInt());
    return settings;
}

ProtocolMode Baud::currentProtocol() const
{
    return static_cast<ProtocolMode>(m_protocolCombo->currentData().toInt());
}

void Baud::onConnectClicked()
{
    if (m_connected || m_connecting) {
        emit closePortRequested();
        return;
    }

    const SerialSettings settings = currentSettings();
    if (settings.portName.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("串口"),
                                 QStringLiteral("没有可用串口，请插入设备后刷新。"));
        return;
    }

    resetSession();
    m_connecting = true;
    updateConnectionUi();
    emit openPortRequested(settings, currentProtocol());
}

void Baud::onPauseClicked()
{
    if (!m_connected)
        return;

    m_paused = m_pauseButton->isChecked();
    m_pauseButton->setText(m_paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    m_scope->setPaused(m_paused);

    if (!m_paused) {
        m_scope->rebuildFromStore(m_store);
        m_scope->setFollowLatest(true);
    }
}

void Baud::onClearClicked()
{
    resetSession();
    statusBar()->showMessage(QStringLiteral("已清空当前会话数据"), 3000);
}

void Baud::onProtocolChanged(int index)
{
    Q_UNUSED(index);

    const ProtocolMode mode = currentProtocol();
    // 切换协议时先暂停接收，等 worker 回执后再恢复，
    // 避免旧协议已排队的批次被新会话接收
    m_acceptData = false;
    if (m_worker)
        emit protocolChangeRequested(mode);
    else
        m_acceptData = true;

    resetSession();
    updateConnectionUi();
    statusBar()->showMessage(
        QStringLiteral("已切换协议：%1").arg(protocolDisplayName(mode)), 3000);
}

void Baud::onWorkerProtocolApplied()
{
    m_acceptData = true;
}

void Baud::onRefreshPortsClicked()
{
    refreshPorts();
}

void Baud::onPortRefreshTimer()
{
    refreshPorts();
}

void Baud::onWorkerOpened()
{
    if (m_shuttingDown)
        return;

    m_acceptData = true;
    m_connected = true;
    m_connecting = false;
    updateConnectionUi();
    statusBar()->showMessage(QStringLiteral("串口已打开"), 3000);
}

void Baud::onWorkerClosed()
{
    m_acceptData = true;
    m_connected = false;
    m_connecting = false;
    m_paused = false;
    m_pauseButton->setChecked(false);
    if (m_scope)
        m_scope->setPaused(false);
    updateConnectionUi();

    if (!m_shuttingDown) {
        statusBar()->showMessage(QStringLiteral("串口已关闭"), 3000);
        refreshPorts();
    }
}

void Baud::onWorkerError(const QString &message)
{
    if (m_shuttingDown)
        return;

    if (m_connecting) {
        m_connecting = false;
        m_connected = false;
        updateConnectionUi();
        QMessageBox::warning(this, QStringLiteral("串口错误"), message);
        return;
    }

    statusBar()->showMessage(message, 6000);
}

void Baud::onRawBytesReceived(const QByteArray &bytes)
{
    if (!m_acceptData)
        return;

    m_rxBytes += bytes.size();
    if (currentProtocol() == ProtocolMode::RawData)
        appendRawData(bytes);
}

void Baud::onFramesReceived(const FrameBatch &frames)
{
    if (!m_acceptData || frames.isEmpty())
        return;

    const qint64 firstSampleIndex = m_store.sampleCount();
    const FrameBatch accepted = m_store.appendFrames(frames);
    if (accepted.isEmpty())
        return;

    const int channelCount = m_store.channelCount();
    if (channelCount != m_lastChannelCount) {
        m_scope->setChannelCount(channelCount);
        rebuildChannelTable(channelCount);
        m_lastChannelCount = channelCount;
    }

    if (!m_paused)
        m_scope->appendFrames(accepted, firstSampleIndex);

    m_acceptedFrames += accepted.size();
}

void Baud::onUiTimer()
{
    if (m_store.channelCount() > 0)
        updateChannelTable();
    updateStatus();
}

void Baud::onRateTimer()
{
    m_rxRate = m_rxBytes - m_lastRxBytes;
    m_frameRate = m_acceptedFrames - m_lastFrames;
    m_lastRxBytes = m_rxBytes;
    m_lastFrames = m_acceptedFrames;
    updateStatus();
}

void Baud::onRenderTimer()
{
    if (m_connected && !m_paused)
        m_scope->refresh();
}

void Baud::onSendClicked()
{
    if (!m_connected) {
        statusBar()->showMessage(QStringLiteral("串口未连接，无法发送"), 4000);
        return;
    }

    QByteArray data;
    if (m_sendModeCombo->currentData().toInt() == 1) {
        QString hex = m_sendEdit->text();
        hex.remove(QRegularExpression(QStringLiteral("0[xX]")));
        hex.remove(QRegularExpression(QStringLiteral("[\\s,]")));
        if (hex.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("发送内容为空"), 4000);
            return;
        }
        if ((hex.size() % 2) != 0
            || hex.contains(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")))) {
            statusBar()->showMessage(QStringLiteral("HEX 格式无效，请输入成对十六进制字节"), 4000);
            return;
        }
        data = QByteArray::fromHex(hex.toLatin1());
    } else {
        data = m_sendEdit->text().toUtf8();
    }

    data.append(m_lineEndingCombo->currentData().toString().toUtf8());
    if (data.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("发送内容为空"), 4000);
        return;
    }

    emit writeRequested(data);
    statusBar()->showMessage(QStringLiteral("已发送 %1 字节").arg(data.size()), 3000);
}

void Baud::onRawModeChanged(int index)
{
    Q_UNUSED(index);
    m_hexColumn = 0;
    m_rawMonitor->clear();
}

void Baud::onSendModeChanged(int index)
{
    Q_UNUSED(index);
    if (m_sendModeCombo->currentData().toInt() == 1)
        m_sendEdit->setPlaceholderText(QStringLiteral("输入 HEX 字节，如 01 02 AF FA"));
    else
        m_sendEdit->setPlaceholderText(QStringLiteral("输入要发送的 ASCII 文本"));
}

void Baud::updateStatus()
{
    m_statusRxRate->setText(QStringLiteral("接收: %1 B/s").arg(m_rxRate));
    m_statusFrameRate->setText(QStringLiteral("帧率: %1 fps").arg(m_frameRate));
    m_statusSamples->setText(QStringLiteral("样本: %1").arg(m_store.sampleCount()));
    m_statusChannels->setText(QStringLiteral("通道: %1").arg(m_store.channelCount()));
}

void Baud::rebuildChannelTable(int channelCount)
{
    QSignalBlocker blocker(m_channelTable);
    m_channelTable->setRowCount(0);
    m_channelTable->setRowCount(channelCount);

    for (int channel = 0; channel < channelCount; ++channel) {
        QTableWidgetItem *checkItem = new QTableWidgetItem;
        checkItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        checkItem->setCheckState(Qt::Checked);
        m_channelTable->setItem(channel, 0, checkItem);

        QPixmap swatch(12, 12);
        swatch.fill(Theme::channelColor(channel));
        QTableWidgetItem *nameItem =
            new QTableWidgetItem(QIcon(swatch), QStringLiteral("CH%1").arg(channel + 1));
        nameItem->setFlags(Qt::ItemIsEnabled);
        m_channelTable->setItem(channel, 1, nameItem);

        for (int column = 2; column <= 4; ++column) {
            QTableWidgetItem *valueItem = new QTableWidgetItem(QStringLiteral("--"));
            valueItem->setFlags(Qt::ItemIsEnabled);
            valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_channelTable->setItem(channel, column, valueItem);
        }
    }
}

void Baud::updateChannelTable()
{
    const int channelCount = m_store.channelCount();
    if (channelCount <= 0 || m_channelTable->rowCount() != channelCount)
        return;

    const QVector<double> latest = m_store.latestValues();
    if (latest.size() != channelCount)
        return;

    const qint64 to = m_store.sampleCount() - 1;
    const qint64 from = std::max(m_store.firstStoredIndex(), to - 999);

    QSignalBlocker blocker(m_channelTable);
    for (int channel = 0; channel < channelCount; ++channel) {
        m_channelTable->item(channel, 2)->setText(formatValue(latest.at(channel)));

        double minValue = 0.0;
        double maxValue = 0.0;
        const bool hasRange = m_store.rangeMinMax(channel, from, to, minValue, maxValue);
        m_channelTable->item(channel, 3)->setText(hasRange ? formatValue(minValue)
                                                           : QStringLiteral("--"));
        m_channelTable->item(channel, 4)->setText(hasRange ? formatValue(maxValue)
                                                           : QStringLiteral("--"));
    }
}

void Baud::appendRawData(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return;

    QString text;
    if (m_rawModeCombo->currentData().toInt() == 1) {
        text.reserve(bytes.size() * 3);
        for (const char byte : bytes) {
            const unsigned char value = static_cast<unsigned char>(byte);
            text.append(QString::number(value, 16).toUpper().rightJustified(2, QLatin1Char('0')));
            text.append(QLatin1Char(' '));
            ++m_hexColumn;
            if (m_hexColumn >= 16) {
                text.append(QLatin1Char('\n'));
                m_hexColumn = 0;
            }
        }
    } else {
        text.reserve(bytes.size());
        for (const char byte : bytes) {
            const unsigned char value = static_cast<unsigned char>(byte);
            if (value == 0x0A || value == 0x0D || value == 0x09
                || (value >= 0x20 && value <= 0x7E)) {
                text.append(QChar(static_cast<ushort>(value)));
            } else {
                text.append(QLatin1Char('.'));
            }
        }
    }

    if (text.isEmpty())
        return;

    m_rawMonitor->moveCursor(QTextCursor::End);
    m_rawMonitor->insertPlainText(text);
}

void Baud::resetSession()
{
    m_store.clear();
    if (m_scope) {
        m_scope->setChannelCount(0);
        m_scope->clearData();
    }
    m_channelTable->setRowCount(0);
    m_lastChannelCount = 0;
    m_rawMonitor->clear();
    m_hexColumn = 0;

    m_paused = false;
    m_pauseButton->setChecked(false);
    m_pauseButton->setText(QStringLiteral("暂停"));
    if (m_scope)
        m_scope->setPaused(false);

    m_rxBytes = 0;
    m_acceptedFrames = 0;
    m_lastRxBytes = 0;
    m_lastFrames = 0;
    m_rxRate = 0;
    m_frameRate = 0;
    updateStatus();
}

void Baud::stopSerialThread()
{
    if (!m_serialThread)
        return;

    if (m_serialThread->isRunning()) {
        if (m_worker)
            QMetaObject::invokeMethod(m_worker, "closePort", Qt::BlockingQueuedConnection);
        m_serialThread->quit();
        m_serialThread->wait(3000);
    }
    m_worker = nullptr;
}

void Baud::closeEvent(QCloseEvent *event)
{
    m_shuttingDown = true;

    if (m_renderTimer)
        m_renderTimer->stop();
    if (m_uiTimer)
        m_uiTimer->stop();
    if (m_rateTimer)
        m_rateTimer->stop();
    if (m_portTimer)
        m_portTimer->stop();

    stopSerialThread();
    event->accept();
}
