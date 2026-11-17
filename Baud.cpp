// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "Baud.h"

#include "io/DataExporter.h"
#include "io/CsvImporter.h"
#include "serial/SerialWorker.h"
#include "ui/ScopePlot.h"
#include "ui/Theme.h"
#include "ui/TimeFormat.h"
#include "ui/ValueFormat.h"
#include "update/UpdateChecker.h"
#include "update/UpdateDialog.h"

#include <QApplication>

#include <QAction>
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
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
#include <QTextBrowser>
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

// 操作指南正文：原先塞在波形控件的 tooltip 里，悬停即弹一大段文字太吵，
// 现在收进“帮助 → 操作指南”。不写死颜色，跟随当前主题的前景色。
QString userGuideHtml()
{
    // 用 fromUtf8 而不是 QStringLiteral(R"(...)" )：后者展开成 u"" R"(...)"，
    // 窄/宽字面量拼接是 MSVC 扩展，换 GCC/Clang 编不过。
    return QString::fromUtf8(R"(<!DOCTYPE html>
<html><body>
<h2>操作指南</h2>

<h3>连接与协议</h3>
<ul>
<li>选择端口和波特率后点“连接”；数据位 / 校验 / 停止位 / 流控按设备配置填写。</li>
<li><b>JustFloat / FireWater</b> 会解析成波形；<b>RawData</b> 不解析数值，右侧显示原始字节（ASCII 或 HEX）。</li>
<li>切换协议会清空当前会话，避免两种协议的数据混在同一份缓冲里。</li>
<li>串口列表每 2 秒自动刷新，也可以点端口输入框右侧的刷新按钮。</li>
</ul>

<h3>波形区交互</h3>
<ul>
<li>滚轮：绘图区内同时缩放 X/Y；在 X 轴刻度带只缩放 X；在 Y 轴刻度带只缩放 Y。</li>
<li>左键拖动：平移视图（会关闭“跟随最新”和自动 Y 缩放）。</li>
<li>双击：绘图区或 X 轴带固定一根竖线；双击已有竖线将其移除；Y 轴带双击复位视图。</li>
<li>单击竖线设为测量起点 A，Shift+单击设为终点 B；A/B 只用于测量，不裁剪波形。</li>
<li>右键菜单：固定 / 移除竖线、设为 A / B、清除手动游标、复位视图。</li>
<li>左右两条彩色竖线是显示缓冲边界：跟随最新时，两边界间距等于“显示缓冲(ms)”；波形从右边界进入、到左边界移出。</li>
<li>“跟随最新”恢复自动跟随与自动 Y；暂停时波形停止刷新，继续后自动补齐。</li>
</ul>

<h3>通道与测量</h3>
<ul>
<li>通道编号从 <b>CH0</b> 开始（与协议里的 ch0 对应）。通道面板可勾选显示 / 隐藏。</li>
<li>通道面板的当前值、最小值、最大值为最近 1000 点统计；鼠标悬停单元格可查看完整精度。</li>
<li>测量区间（A→B）面板显示 Δt 以及每个可见通道在该区间内的最小 / 最大值。</li>
</ul>

<h3>发送</h3>
<ul>
<li>ASCII 或 HEX 发送，可选 CR / LF / CRLF 行尾；HEX 允许空格、逗号和 0x 前缀。</li>
<li>USB-TTL 把 TX 与 RX 短接即可做回环自测。</li>
</ul>

<h3>导入与导出</h3>
<ul>
<li><b>文件 → 导出数据 → CSV</b>：宽表 <code>time_s,ch0,…</code>，带 # 注释元数据，Excel / pandas 可直接打开。</li>
<li><b>Markdown</b>：同样的逐点数据写成 Markdown 表格，便于粘贴到文档或对话里。</li>
<li><b>精简 Markdown</b>：连续不变的采样合并成时间段（<code>t_start_s</code> / <code>t_end_s</code> / 各通道值），文件更小，也便于给 AI 阅读。</li>
<li><b>文件 → 导入 CSV</b>：按时间列重建时间轴并回放波形。</li>
<li>导出期间冻结接收、导入期间丢弃串口帧，避免环形缓冲被覆盖或两份数据混在一起。</li>
</ul>

<h3>显示</h3>
<ul>
<li>菜单栏右上角按钮切换深色 / 浅色主题，选择会被记住。</li>
<li>“显示缓冲(ms)”只改变可见时间窗口；底层环形缓冲固定 200000 点 / 通道。</li>
</ul>
</body></html>)");
}

// 导出文件的元数据头：CSV 用 # 注释行，Markdown 用标题 + 列表
QString buildExportMeta(DataExporter::Format format, const QString &protocolName,
                        double periodSeconds, int channelCount, qint64 rowCount)
{
    const QString created = QDateTime::currentDateTime().toString(Qt::ISODate);
    const QString sampleRate =
        periodSeconds > 0.0 ? QString::number(1.0 / periodSeconds, 'f', 3)
                            : QStringLiteral("0");
    const QString dt = periodSeconds > 0.0 ? QString::number(periodSeconds, 'f', 9)
                                           : QStringLiteral("0");

    if (format == DataExporter::Format::Csv) {
        QStringList meta;
        meta << QStringLiteral("# Baud Export v1");
        meta << QStringLiteral("# created=%1").arg(created);
        meta << QStringLiteral("# protocol=%1").arg(protocolName);
        meta << QStringLiteral("# time_base=pc_arrival");
        meta << QStringLiteral("# sample_rate=%1").arg(sampleRate);
        meta << QStringLiteral("# dt_auto=%1").arg(dt);
        meta << QStringLiteral("# channels=%1").arg(channelCount);
        meta << QStringLiteral("# rows=%1").arg(rowCount);
        return meta.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }

    const bool compact = format == DataExporter::Format::CompactMarkdown;
    QStringList meta;
    meta << (compact ? QStringLiteral("# Baud 数据导出（精简）")
                     : QStringLiteral("# Baud 数据导出"));
    meta << QString();
    meta << (compact
                 ? QStringLiteral("> 每行是一个时间段：区间内所有通道的值保持不变；"
                                  "`t_start_s` / `t_end_s` 是相对会话起点的时间（秒）。")
                 : QStringLiteral("> 每行是一个采样点：`time_s` 是相对会话起点的时间（秒），"
                                  "`ch0`… 为各通道数值。"));
    meta << QString();
    meta << QStringLiteral("- 创建时间：%1").arg(created);
    meta << QStringLiteral("- 协议：%1").arg(protocolName);
    meta << QStringLiteral("- 时间基准：pc_arrival（上位机接收时刻）");
    meta << QStringLiteral("- 采样率：%1 Hz").arg(sampleRate);
    meta << QStringLiteral("- 自动估计 Δt：%1 s").arg(dt);
    meta << QStringLiteral("- 通道数：%1").arg(channelCount);
    meta << (compact ? QStringLiteral("- 原始采样点数：%1").arg(rowCount)
                     : QStringLiteral("- 采样行数：%1").arg(rowCount));
    meta << QString();
    return meta.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// 通道色块随主题变化，统一从这里生成，避免波形、通道表、测量面板各画一套
QIcon channelSwatch(int channel)
{
    QPixmap swatch(12, 12);
    swatch.fill(Theme::channelColor(channel));
    return QIcon(swatch);
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
    stopIoThreads();
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
    QMenu *exportMenu = fileMenu->addMenu(QStringLiteral("导出数据"));
    m_exportCsvAction = exportMenu->addAction(QStringLiteral("CSV…"));
    m_exportCsvAction->setToolTip(QStringLiteral(
        "宽表 time_s,ch0,…，带 # 注释元数据，Excel / pandas 可直接打开"));
    connect(m_exportCsvAction, &QAction::triggered, this, [this]() {
        startExport(DataExporter::Format::Csv);
    });
    m_exportMarkdownAction = exportMenu->addAction(QStringLiteral("Markdown…"));
    m_exportMarkdownAction->setToolTip(QStringLiteral(
        "同样的逐点数据写成 Markdown 表格，便于粘贴到文档或对话里"));
    connect(m_exportMarkdownAction, &QAction::triggered, this, [this]() {
        startExport(DataExporter::Format::Markdown);
    });
    m_exportCompactAction = exportMenu->addAction(QStringLiteral("精简 Markdown…"));
    m_exportCompactAction->setToolTip(QStringLiteral(
        "连续不变的采样合并成时间段，文件更小，便于给 AI 阅读"));
    connect(m_exportCompactAction, &QAction::triggered, this, [this]() {
        startExport(DataExporter::Format::CompactMarkdown);
    });
    m_importAction = fileMenu->addAction(QStringLiteral("导入 CSV…"));
    m_importAction->setToolTip(QStringLiteral("读取 CSV 文件并按时间轴回放波形"));
    connect(m_importAction, &QAction::triggered, this, &Baud::onImportCsv);
    fileMenu->addSeparator();
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
    QAction *guideAction = helpMenu->addAction(QStringLiteral("操作指南"));
    connect(guideAction, &QAction::triggered, this, &Baud::showUserGuide);
    helpMenu->addSeparator();
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

    // 主题切换放在菜单栏右上角：不占波形工具栏，RawData 页也能用
    m_themeButton = new QPushButton(menuBar());
    m_themeButton->setObjectName(QStringLiteral("themeToggle"));
    m_themeButton->setCursor(Qt::PointingHandCursor);
    m_themeButton->setFocusPolicy(Qt::NoFocus);
    menuBar()->setCornerWidget(m_themeButton, Qt::TopRightCorner);
    connect(m_themeButton, &QPushButton::clicked, this, &Baud::onThemeToggled);
    updateThemeButton();

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
    // 串口描述（"COM9 · USB-SERIAL CH340"）可能很长，sizeHint 不要跟着最长项变宽
    m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_portCombo->setMinimumContentsLength(12);
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
    m_flowCombo->addItem(QStringLiteral("RTS/CTS"), static_cast<int>(QSerialPort::HardwareControl));
    m_flowCombo->addItem(QStringLiteral("XON/XOFF"),
                         static_cast<int>(QSerialPort::SoftwareControl));
    m_flowCombo->setToolTip(QStringLiteral("硬件流控 RTS/CTS，或软件流控 XON/XOFF"));

    // 两列并排后每个下拉框只有一半宽度：统一按内容长度策略，
    // 避免某个长选项（如 RTS/CTS）把整块面板顶宽。
    for (QComboBox *combo : {m_dataBitsCombo, m_parityCombo, m_stopBitsCombo, m_flowCombo}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(6);
    }

    m_connectButton = new QPushButton(QStringLiteral("连接"), serialGroup);
    m_connectButton->setObjectName(QStringLiteral("primaryButton"));
    m_connectButton->setMinimumHeight(32);

    // 端口行：下拉框和刷新按钮放进同一个横向布局并整行铺满，
    // 右边缘与下面的波特率等字段对齐（此前按钮单独占一列，端口框比下面窄一截）
    QWidget *portRow = new QWidget(serialGroup);
    QHBoxLayout *portRowLayout = new QHBoxLayout(portRow);
    portRowLayout->setContentsMargins(0, 0, 0, 0);
    portRowLayout->setSpacing(6);
    portRowLayout->addWidget(m_portCombo, 1);
    portRowLayout->addWidget(m_refreshPortsButton, 0);

    // 端口/波特率各占一行；数据位/停止位、校验/流控两两一行，少占纵向空间
    serialGrid->addWidget(new QLabel(QStringLiteral("端口"), serialGroup), 0, 0);
    serialGrid->addWidget(portRow, 0, 1, 1, 3);
    serialGrid->addWidget(new QLabel(QStringLiteral("波特率"), serialGroup), 1, 0);
    serialGrid->addWidget(m_baudCombo, 1, 1, 1, 3);
    serialGrid->addWidget(new QLabel(QStringLiteral("数据位"), serialGroup), 2, 0);
    serialGrid->addWidget(m_dataBitsCombo, 2, 1);
    serialGrid->addWidget(new QLabel(QStringLiteral("停止位"), serialGroup), 2, 2);
    serialGrid->addWidget(m_stopBitsCombo, 2, 3);
    serialGrid->addWidget(new QLabel(QStringLiteral("校验"), serialGroup), 3, 0);
    serialGrid->addWidget(m_parityCombo, 3, 1);
    serialGrid->addWidget(new QLabel(QStringLiteral("流控"), serialGroup), 3, 2);
    serialGrid->addWidget(m_flowCombo, 3, 3);
    serialGrid->addWidget(m_connectButton, 4, 0, 1, 4);
    serialGrid->setColumnStretch(1, 1);
    serialGrid->setColumnStretch(3, 1);
    serialGrid->setVerticalSpacing(6);
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

    // 测量区间面板：A/B 之间的每通道极值。原先挤在波形下方的小字标签看不清，
    // 这里单独开一块区域放在通道表下面。
    QGroupBox *measureGroup = new QGroupBox(QStringLiteral("测量区间（A→B）"), left);
    QVBoxLayout *measureLayout = new QVBoxLayout(measureGroup);
    measureLayout->setContentsMargins(10, 14, 10, 10);
    measureLayout->setSpacing(6);

    // 摘要与 Δt 并排：Δt 是这里最主要的信息，用更大字号，摘要让出宽度
    QHBoxLayout *measureHeaderLayout = new QHBoxLayout;
    measureHeaderLayout->setContentsMargins(0, 0, 0, 0);
    measureHeaderLayout->setSpacing(8);

    m_measureSummary = new QLabel(
        QStringLiteral("A 左边界  →  B 右边界"), measureGroup);
    m_measureSummary->setFont(QFont(QStringLiteral("Consolas"), 9));
    m_measureSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    measureHeaderLayout->addWidget(m_measureSummary, 1);

    m_measureDelta = new QLabel(QStringLiteral("Δt --"), measureGroup);
    QFont deltaFont(QStringLiteral("Consolas"), 12);
    deltaFont.setBold(true);
    m_measureDelta->setFont(deltaFont);
    m_measureDelta->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_measureDelta->setTextInteractionFlags(Qt::TextSelectableByMouse);
    measureHeaderLayout->addWidget(m_measureDelta, 0);

    measureLayout->addLayout(measureHeaderLayout);

    m_measureTable = new QTableWidget(0, 3, measureGroup);
    m_measureTable->setHorizontalHeaderLabels(
        {QStringLiteral("通道"), QStringLiteral("区间最小"), QStringLiteral("区间最大")});
    m_measureTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_measureTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_measureTable->setAlternatingRowColors(true);
    m_measureTable->setShowGrid(false);
    m_measureTable->verticalHeader()->setVisible(false);
    m_measureTable->verticalHeader()->setDefaultSectionSize(24);
    m_measureTable->setFont(QFont(QStringLiteral("Consolas"), 10));
    QHeaderView *measureHeader = m_measureTable->horizontalHeader();
    measureHeader->setSectionResizeMode(0, QHeaderView::Fixed);
    measureHeader->setSectionResizeMode(1, QHeaderView::Stretch);
    measureHeader->setSectionResizeMode(2, QHeaderView::Stretch);
    m_measureTable->setColumnWidth(0, 58);
    // 通道多时表格内部滚动，别把左侧面板顶爆
    m_measureTable->setMaximumHeight(120);
    measureLayout->addWidget(m_measureTable);
    leftLayout->addWidget(measureGroup, 0);

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
    scopeBarLayout->addWidget(new QLabel(QStringLiteral("显示缓冲(ms)"), scopeBar));
    m_viewSpanSpin = new QSpinBox(scopeBar);
    m_viewSpanSpin->setRange(static_cast<int>(ScopePlot::kMinViewSpanMs),
                             static_cast<int>(ScopePlot::kMaxViewSpanMs));
    m_viewSpanSpin->setSingleStep(100);
    m_viewSpanSpin->setValue(static_cast<int>(ScopePlot::kDefaultViewSpanMs));
    m_viewSpanSpin->setToolTip(QStringLiteral(
        "左右两条边界之间的 X 轴长度。跟随最新时，波形从右边界进入、"
        "到左边界移出；滚轮/拖动缩放后可点“跟随最新”恢复。"
        "底层环形缓冲固定为 200000 点/通道"));
    scopeBarLayout->addWidget(m_viewSpanSpin);
    m_followButton = new QPushButton(QStringLiteral("跟随最新"), scopeBar);
    m_followButton->setCheckable(true);
    m_followButton->setChecked(true);
    m_followButton->setToolTip(QStringLiteral("跟随最新并自动缩放 Y 轴"));
    scopeBarLayout->addWidget(m_followButton);
    m_clearMarkersButton = new QPushButton(QStringLiteral("清除游标"), scopeBar);
    scopeBarLayout->addWidget(m_clearMarkersButton);
    // 交互说明统一放在“帮助 → 操作指南”，工具栏不再挂长提示：
    // 窄窗口下 QLabel 自动换行会把波形区顶下去一大截。
    scopeBarLayout->addStretch(1);
    scopeLayout->addWidget(scopeBar);

    m_scope = new ScopePlot(scopePage);
    m_scope->setStore(&m_store);
    scopeLayout->addWidget(m_scope, 1);
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
    m_statusFrameRate->setToolTip(QStringLiteral(
        "接收帧率由串口数据决定；绘制耗时为波形最近平均重绘耗时，"
        "界面刷新目标约 60 fps"));
    m_statusSamples = new QLabel(this);
    m_statusChannels = new QLabel(this);
    m_statusTiming = new QLabel(this);
    m_statusTiming->setToolTip(QStringLiteral(
        "由接收节奏自动推算的采样周期与抖动，无需手工填写 Δt；"
        "它是上位机侧的估计值，不等于下位机内部定时器周期"));
    statusBar()->addWidget(m_statusState, 1);
    statusBar()->addPermanentWidget(m_statusRxRate);
    statusBar()->addPermanentWidget(m_statusFrameRate);
    statusBar()->addPermanentWidget(m_statusTiming);
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
    // 测量结果直接渲染到左下的独立面板：A/B 摘要 + 大字 Δt + 每通道极值表
    connect(m_scope, &ScopePlot::measurementChanged, this,
            [this](const MeasurementInfo &info) {
                if (!info.hasRange) {
                    m_measureSummary->setText(QStringLiteral("测量游标：A / B 未定义"));
                    m_measureDelta->setText(QStringLiteral("Δt --"));
                } else {
                    m_measureSummary->setText(QStringLiteral("A %1  →  B %2")
                                                  .arg(TimeFormat::format(info.from))
                                                  .arg(TimeFormat::format(info.to)));
                    // 区间极值按 [min, max] 统计，Δt 也取绝对值，
                    // 否则 A 设在 B 右侧时会显示负值，与统计方向矛盾
                    m_measureDelta->setText(
                        info.hasData ? QStringLiteral("Δt %1").arg(TimeFormat::formatDelta(
                                           std::fabs(info.to - info.from)))
                                     : QStringLiteral("Δt 等待数据"));
                }

                const auto formatStat = [](bool hasData, double value) {
                    return hasData && std::isfinite(value) ? ValueFormat::format(value)
                                                           : QStringLiteral("--");
                };

                m_measureTable->setRowCount(info.stats.size());
                for (int row = 0; row < info.stats.size(); ++row) {
                    const MeasureStat &stat = info.stats.at(row);
                    const auto ensureItem = [this, row](int column) {
                        QTableWidgetItem *item = m_measureTable->item(row, column);
                        if (!item) {
                            item = new QTableWidgetItem;
                            item->setFlags(Qt::ItemIsEnabled);
                            m_measureTable->setItem(row, column, item);
                        }
                        return item;
                    };

                    QTableWidgetItem *nameItem = ensureItem(0);
                    nameItem->setText(QStringLiteral("CH%1").arg(stat.channel));
                    nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                    nameItem->setForeground(Theme::channelColor(stat.channel));
                    // 行的顺序只包含可见通道，主题切换要按真实通道号重新取色
                    nameItem->setData(Qt::UserRole, stat.channel);

                    QTableWidgetItem *minItem = ensureItem(1);
                    minItem->setText(formatStat(stat.hasData, stat.minValue));
                    minItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

                    QTableWidgetItem *maxItem = ensureItem(2);
                    maxItem->setText(formatStat(stat.hasData, stat.maxValue));
                    maxItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                }
            });
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
    connect(m_worker, &SerialWorker::timingUpdated, this, &Baud::onTimingUpdated);

    m_serialThread->start();
}

void Baud::setupTimers()
{
    m_renderTimer = new QTimer(this);
    m_renderTimer->setInterval(16);
    m_renderTimer->setTimerType(Qt::PreciseTimer);
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
    // 导入/导出期间串口、协议、清空等会改动 ChannelStore 的控件全部锁住，
    // 否则导出区间会被清空/重连/切协议破坏，或导入数据与串口会话混在一起。
    const bool ioBusy = m_exporting || m_importing;
    const bool configEnabled = !m_connected && !m_connecting && !ioBusy;
    const bool rawMode = currentProtocol() == ProtocolMode::RawData;

    m_portCombo->setEnabled(configEnabled);
    m_baudCombo->setEnabled(configEnabled);
    m_dataBitsCombo->setEnabled(configEnabled);
    m_parityCombo->setEnabled(configEnabled);
    m_stopBitsCombo->setEnabled(configEnabled);
    m_flowCombo->setEnabled(configEnabled);
    m_refreshPortsButton->setEnabled(configEnabled);
    m_protocolCombo->setEnabled(!m_connecting && !ioBusy);

    m_connectButton->setEnabled(!m_connecting && !ioBusy);
    m_connectButton->setText(m_connecting ? QStringLiteral("连接中…")
                                          : (m_connected ? QStringLiteral("断开")
                                                         : QStringLiteral("连接")));

    m_sendEdit->setEnabled(m_connected && !ioBusy);
    m_sendModeCombo->setEnabled(m_connected && !ioBusy);
    m_lineEndingCombo->setEnabled(m_connected && !ioBusy);
    m_sendButton->setEnabled(m_connected && !ioBusy);

    m_pauseButton->setEnabled(m_connected && !rawMode && !ioBusy);
    m_clearButton->setEnabled(!ioBusy);

    // 导入/导出期间菜单项一并禁用：只靠 startExport/onImportCsv 里的判空兜底，
    // 会把状态栏的“正在导出…”换成一条 3 秒后就消失的提示。
    for (QAction *action : {m_exportCsvAction, m_exportMarkdownAction,
                            m_exportCompactAction, m_importAction}) {
        if (action)
            action->setEnabled(!ioBusy);
    }

    if (rawMode && m_pauseButton->isChecked()) {
        m_pauseButton->setChecked(false);
        m_paused = false;
        if (m_scope)
            m_scope->setPaused(false);
    }
    m_pauseButton->setText(m_paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    // RawData 下默认显示原始监视页；但 CSV 导入的是数值波形，
    // 导入期间/缓冲区已有通道数据时仍显示波形页，避免导入后看不到数据。
    const bool showScope = !rawMode || m_importing || m_store.channelCount() > 0;
    m_stack->setCurrentIndex(showScope ? 0 : 1);

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
        // 断开前先停止接收：worker 在关闭前还会 flush 一次残留批次，
        // 不挡住的话断开后波形仍会“冒”出一截旧数据。
        m_acceptData = false;
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
        m_plotDirty = false;
        m_scope->rebuildFromStore(m_store);
        m_scope->setFollowLatest(true);
        m_lastChannelCount = m_store.channelCount();
    }
}

void Baud::onClearClicked()
{
    if (m_exporting || m_importing) {
        statusBar()->showMessage(QStringLiteral("正在导入/导出，请稍候…"), 0);
        return;
    }
    resetSession();
    updateConnectionUi();
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
    const bool wasPaused = m_paused;
    m_paused = false;
    m_pauseButton->setChecked(false);
    if (m_scope) {
        m_scope->setPaused(false);
        // 断开时如果还停在暂停状态，把暂停期间进入缓冲区、但没画出来的数据补上
        if (wasPaused && !m_shuttingDown) {
            m_scope->rebuildFromStore(m_store);
            m_lastChannelCount = m_store.channelCount();
        }
        // 把最后 16 ms 内还没等到渲染定时器的新数据刷出来
        if (!m_shuttingDown) {
            m_scope->refresh();
            m_plotDirty = false;
        }
    }
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

    // 导出期间冻结接收，保证导出区间对应的缓冲数据不被新数据覆盖；
    // 导入期间丢弃串口帧，避免与导入帧混进同一份缓冲。
    if (m_exporting || m_importing)
        return;

    const FrameBatch accepted = m_store.appendFrames(frames);
    if (accepted.isEmpty())
        return;

    // 校准通道数时 ChannelStore 可能丢弃残帧并从零重新编号
    const qint64 firstSampleIndex = m_store.sampleCount() - accepted.size();
    const int channelCount = m_store.channelCount();
    if (channelCount != m_lastChannelCount) {
        m_scope->setChannelCount(channelCount);
        // 校准阶段改写通道数时时间轴会从零重来，旧游标已无意义
        m_scope->clearMarkers();
        rebuildChannelTable(channelCount);
        m_lastChannelCount = channelCount;
    } else if (firstSampleIndex == 0 && m_store.sampleCount() == accepted.size()) {
        // 校准过程中可能绕了一圈又回到相同通道数，此时图形也须从零重建
        m_scope->clearData();
    }

    if (!m_paused) {
        m_scope->appendFrames(accepted, firstSampleIndex);
        m_plotDirty = true;
    }

    m_acceptedFrames += accepted.size();
}

void Baud::onTimingUpdated(double period, double jitter)
{
    m_measuredPeriod = period;
    m_measuredJitter = jitter;
    if (m_scope)
        m_scope->setSamplePeriod(period);
    updateStatus();
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
    // 导入期间串口是断开的，但导入的数据同样要实时画出来
    if ((m_connected || m_importing) && !m_paused && m_plotDirty) {
        m_plotDirty = false;
        m_scope->refresh();
    }
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

void Baud::startExport(DataExporter::Format format)
{
    if (m_exportThread) {
        // 用 0 超时：否则这条“请稍候”3 秒后过期，状态栏变空，反而看不出还在导出
        statusBar()->showMessage(QStringLiteral("正在导出，请稍候…（导出期间暂停接收）"), 0);
        return;
    }
    if (m_importThread) {
        statusBar()->showMessage(QStringLiteral("正在导入，请稍候…"), 0);
        return;
    }
    if (m_store.channelCount() <= 0 || m_store.sampleCount() <= 0) {
        QMessageBox::information(this, QStringLiteral("导出数据"),
                                 QStringLiteral("当前没有可导出的数据。"));
        return;
    }

    QString caption;
    QString filter;
    QString suffix;
    QString busyText;
    switch (format) {
    case DataExporter::Format::Markdown:
        caption = QStringLiteral("导出 Markdown");
        filter = QStringLiteral("Markdown 文件 (*.md)");
        suffix = QStringLiteral("md");
        busyText = QStringLiteral("正在导出 Markdown（导出期间暂停接收）…");
        break;
    case DataExporter::Format::CompactMarkdown:
        caption = QStringLiteral("导出精简 Markdown");
        filter = QStringLiteral("Markdown 文件 (*.md)");
        suffix = QStringLiteral("compact.md");
        busyText = QStringLiteral("正在导出精简 Markdown（导出期间暂停接收）…");
        break;
    case DataExporter::Format::Csv:
    default:
        caption = QStringLiteral("导出 CSV");
        filter = QStringLiteral("CSV 文件 (*.csv)");
        suffix = QStringLiteral("csv");
        busyText = QStringLiteral("正在导出 CSV（导出期间暂停接收）…");
        break;
    }

    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString path = QFileDialog::getSaveFileName(
        this, caption, QStringLiteral("Baud_%1.%2").arg(stamp, suffix), filter);
    if (path.isEmpty())
        return;

    m_exportThread = new QThread(this);
    m_exporter = new DataExporter;
    m_exporter->moveToThread(m_exportThread);
    connect(m_exportThread, &QThread::finished, m_exporter, &QObject::deleteLater);

    connect(m_exporter, &DataExporter::needMore, this, &Baud::onExportNeedMore);
    connect(m_exporter, &DataExporter::finished, this, &Baud::onExportFinished);

    // 导出区间在开始时就固定下来：导出期间冻结接收，
    // 否则环形缓冲回绕会把已导出的区间覆盖掉，导致时间列回退、行数对不上。
    m_exportFrom = m_store.firstStoredIndex();
    m_exportTo = m_store.sampleCount() - 1;
    m_exportCursor = m_exportFrom;
    m_exporting = true;
    statusBar()->showMessage(busyText, 0);
    updateConnectionUi();

    const QString meta = buildExportMeta(format, protocolDisplayName(currentProtocol()),
                                         m_store.estimatedPeriod(), m_store.channelCount(),
                                         m_exportTo - m_exportFrom + 1);

    m_exportThread->start();
    // invokeMethod 找不到槽或参数类型未注册时只返回 false（并往 stderr 打一行），
    // 不处理的话 open 永不执行，needMore/finished 都不会来，m_exporting 永远为 true。
    const bool invoked = QMetaObject::invokeMethod(
        m_exporter, "open", Qt::QueuedConnection,
        Q_ARG(QString, path),
        Q_ARG(QString, meta),
        Q_ARG(int, m_store.channelCount()),
        Q_ARG(int, static_cast<int>(format)));
    if (!invoked)
        abortExport(QStringLiteral("无法启动导出任务（导出线程未接受请求）。"));
}

void Baud::onExportNeedMore()
{
    if (!m_exporter)
        return;

    // 区间已取完（或缓冲被清空）：通知导出线程写完收尾内容并关闭文件
    if (m_exportCursor > m_exportTo) {
        if (!QMetaObject::invokeMethod(m_exporter, "close", Qt::QueuedConnection))
            abortExport(QStringLiteral("导出收尾失败（导出线程未响应）。"));
        return;
    }

    // 每次取一块，避免一次性拷贝整个缓冲区
    constexpr qint64 kChunkRows = 8192;
    const qint64 to = std::min(m_exportTo, m_exportCursor + kChunkRows - 1);

    QVector<double> times;
    QVector<double> values;
    m_store.readRange(m_exportCursor, to, times, values);
    m_exportCursor = to + 1;

    // 区间被清空/越界时立刻收尾，避免空块在两侧来回空转
    if (times.isEmpty()) {
        if (!QMetaObject::invokeMethod(m_exporter, "close", Qt::QueuedConnection))
            abortExport(QStringLiteral("导出收尾失败（导出线程未响应）。"));
        return;
    }

    if (!QMetaObject::invokeMethod(m_exporter, "appendChunk", Qt::QueuedConnection,
                                   Q_ARG(QVector<double>, times),
                                   Q_ARG(QVector<double>, values))) {
        abortExport(QStringLiteral("导出数据块失败（导出线程未响应）。"));
    }
}

// 导出请求发不出去（槽签名/参数类型不匹配、线程已退出）时的兜底：
// 不复位 m_exporting 的话 updateConnectionUi() 会把串口/协议/清空/发送永久禁用。
void Baud::abortExport(const QString &message)
{
    m_exporting = false;
    if (m_exportThread)
        stopIoThread(m_exportThread);
    m_exporter = nullptr;
    m_exportCursor = 0;
    m_exportFrom = 0;
    m_exportTo = 0;
    updateConnectionUi();
    statusBar()->clearMessage();
    QMessageBox::warning(this, QStringLiteral("导出数据"), message);
}

void Baud::onExportFinished(bool ok, const QString &message)
{
    m_exporting = false;

    if (ok) {
        statusBar()->showMessage(message, 5000);
    } else {
        statusBar()->clearMessage();
        QMessageBox::warning(this, QStringLiteral("导出数据"), message);
    }

    stopIoThread(m_exportThread);
    m_exporter = nullptr;
    m_exportCursor = 0;
    m_exportFrom = 0;
    m_exportTo = 0;
    updateConnectionUi();
}

void Baud::onImportCsv()
{
    if (m_importThread) {
        statusBar()->showMessage(QStringLiteral("正在导入，请稍候…"), 0);
        return;
    }
    if (m_exportThread) {
        statusBar()->showMessage(QStringLiteral("正在导出，请稍候…（导出期间暂停接收）"), 0);
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入 CSV"), QString(),
        QStringLiteral("CSV 文件 (*.csv);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    // 导入会替换当前会话，先断开串口避免两路数据混在一起。
    // m_importing 让 onFramesReceived 直接丢弃串口帧：closePortRequested 是异步的，
    // 关闭前 worker 还会把残留批次发过来，若不丢弃会与导入帧混进同一份缓冲，
    // 甚至触发通道数重校准把已导入的数据整体清空。
    m_importing = true;
    m_acceptData = false;
    if (m_connected) {
        emit closePortRequested();
    }
    resetSession();
    updateConnectionUi();
    statusBar()->showMessage(QStringLiteral("正在导入 CSV…"), 0);

    m_importThread = new QThread(this);
    m_importer = new CsvImporter;
    m_importer->moveToThread(m_importThread);
    connect(m_importThread, &QThread::finished, m_importer, &QObject::deleteLater);

    connect(m_importer, &CsvImporter::framesParsed, this, &Baud::onImportFrames);
    connect(m_importer, &CsvImporter::imported, this, &Baud::onImportFinished);
    connect(m_importer, &CsvImporter::progressChanged, this, [this](int percent) {
        if (!m_importing)
            return;
        statusBar()->showMessage(QStringLiteral("正在导入 CSV… %1%").arg(percent));
    });

    m_importThread->start();
    // 同导出：请求投递失败时 importFile 不会执行，m_importing 会永久卡住界面
    if (!QMetaObject::invokeMethod(m_importer, "importFile", Qt::QueuedConnection,
                                   Q_ARG(QString, path))) {
        m_importing = false;
        m_acceptData = true;
        stopIoThread(m_importThread);
        m_importer = nullptr;
        updateConnectionUi();
        statusBar()->clearMessage();
        QMessageBox::warning(this, QStringLiteral("导入 CSV"),
                             QStringLiteral("无法启动导入任务（导入线程未接受请求）。"));
    }
}

void Baud::onImportFrames(const FrameBatch &frames)
{
    if (frames.isEmpty())
        return;

    const FrameBatch accepted = m_store.appendFrames(frames);
    if (accepted.isEmpty())
        return;

    const qint64 firstSampleIndex = m_store.sampleCount() - accepted.size();
    const int channelCount = m_store.channelCount();
    if (channelCount != m_lastChannelCount) {
        m_scope->setChannelCount(channelCount);
        rebuildChannelTable(channelCount);
        m_lastChannelCount = channelCount;
    } else if (firstSampleIndex == 0 && m_store.sampleCount() == accepted.size()) {
        m_scope->clearData();
    }

    m_scope->appendFrames(accepted, firstSampleIndex);
    m_plotDirty = true;
}

void Baud::onImportFinished(bool ok, const QString &message, qint64 rowCount, int channelCount)
{
    m_importing = false;
    // 串口若已经关闭，onWorkerClosed 会恢复接收，这里补一次；
    // 串口仍开着则保持关闭，等 onWorkerClosed 再恢复，避免残留帧混入导入结果。
    if (!m_connected)
        m_acceptData = true;

    if (ok) {
        // 导入数据的周期/抖动直接由文件里的时间列统计
        m_measuredPeriod = m_store.estimatedPeriod();
        m_measuredJitter = m_store.periodJitter();
        if (m_scope)
            m_scope->setSamplePeriod(m_measuredPeriod);
        statusBar()->showMessage(QStringLiteral("%1，%2 个通道").arg(message).arg(channelCount), 5000);
        m_scope->resetView();
    } else {
        QMessageBox::warning(this, QStringLiteral("导入 CSV"), message);
    }

    stopIoThread(m_importThread);
    m_importer = nullptr;
    updateConnectionUi();
}

void Baud::updateStatus()
{
    m_statusRxRate->setText(QStringLiteral("接收: %1 B/s").arg(m_rxRate));
    const QString replotMs = m_scope
                                 ? QString::number(m_scope->replotTime(true), 'f', 1)
                                 : QStringLiteral("0.0");
    m_statusFrameRate->setText(QStringLiteral("接收帧率: %1 fps · 绘制: %2 ms")
                                   .arg(m_frameRate)
                                   .arg(replotMs));
    m_statusSamples->setText(QStringLiteral("样本: %1").arg(m_store.sampleCount()));
    m_statusChannels->setText(QStringLiteral("通道: %1").arg(m_store.channelCount()));

    // 实测采样周期与抖动：由接收节奏自动推算，无需用户填写 Δt
    if (m_measuredPeriod > 0.0) {
        m_statusTiming->setText(QStringLiteral("实测 Δt: %1 · 抖动 ±%2")
                                    .arg(TimeFormat::format(m_measuredPeriod))
                                    .arg(TimeFormat::format(m_measuredJitter)));
    } else {
        m_statusTiming->setText(QStringLiteral("实测 Δt: --"));
    }
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

        QTableWidgetItem *nameItem =
            new QTableWidgetItem(channelSwatch(channel), QStringLiteral("CH%1").arg(channel));
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

    // 单元格窄，定点显示可能被裁：悬停时给出完整精度
    const auto setValueCell = [this](int row, int column, bool hasValue, double value) {
        QTableWidgetItem *item = m_channelTable->item(row, column);
        const QString text = hasValue ? ValueFormat::format(value) : QStringLiteral("--");
        const QString tip = hasValue ? ValueFormat::formatExact(value) : QString();
        if (item->text() != text)
            item->setText(text);
        if (item->toolTip() != tip)
            item->setToolTip(tip);
    };

    QSignalBlocker blocker(m_channelTable);
    for (int channel = 0; channel < channelCount; ++channel) {
        setValueCell(channel, 2, true, latest.at(channel));

        double minValue = 0.0;
        double maxValue = 0.0;
        const bool hasRange = m_store.rangeMinMax(channel, from, to, minValue, maxValue);
        setValueCell(channel, 3, hasRange, minValue);
        setValueCell(channel, 4, hasRange, maxValue);
    }
}

// 主题切换后只换色块图标，不能 rebuildChannelTable：
// 那会把用户取消勾选的通道可见性重置回全选。
void Baud::refreshChannelSwatches()
{
    if (!m_channelTable || m_channelTable->rowCount() <= 0)
        return;

    QSignalBlocker blocker(m_channelTable);
    for (int channel = 0; channel < m_channelTable->rowCount(); ++channel) {
        if (QTableWidgetItem *item = m_channelTable->item(channel, 1))
            item->setIcon(channelSwatch(channel));
    }
}

void Baud::updateThemeButton()
{
    if (!m_themeButton)
        return;

    const bool dark = Theme::mode() == Theme::Mode::Dark;
    m_themeButton->setText(dark ? QStringLiteral("浅色模式") : QStringLiteral("深色模式"));
    m_themeButton->setToolTip(dark
                                  ? QStringLiteral("当前：深色主题，点击切换到浅色")
                                  : QStringLiteral("当前：浅色主题，点击切换到深色"));
}

// 操作指南：波形区的交互说明原先挂在控件 tooltip 上，鼠标一进绘图区就弹一大段
// 文字，干扰读数；现在改成按需打开的对话框。
void Baud::showUserGuide()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("操作指南"));
    dialog.resize(640, 620);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QTextBrowser *browser = new QTextBrowser(&dialog);
    browser->setOpenExternalLinks(false);
    browser->setHtml(userGuideHtml());
    layout->addWidget(browser, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    // 应用没有加载 Qt 翻译文件，标准按钮默认是英文 "Close"，这里改成中文
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

void Baud::onThemeToggled()
{
    const Theme::Mode next = Theme::mode() == Theme::Mode::Dark ? Theme::Mode::Light
                                                               : Theme::Mode::Dark;
    Theme::apply(*qApp, next);
    Theme::saveMode(next);
    updateThemeButton();

    if (m_scope)
        m_scope->applyTheme();

    refreshChannelSwatches();

    // 通道色块是图标，表内文字靠全局样式表刷新；测量面板的通道色需要重画一次
    if (m_measureTable) {
        for (int row = 0; row < m_measureTable->rowCount(); ++row) {
            QTableWidgetItem *item = m_measureTable->item(row, 0);
            if (!item)
                continue;
            const int channel = item->data(Qt::UserRole).toInt();
            item->setForeground(Theme::channelColor(channel));
        }
    }

    if (m_updateDialog)
        m_updateDialog->applyTheme();

    statusBar()->showMessage(next == Theme::Mode::Dark ? QStringLiteral("已切换到深色主题")
                                                       : QStringLiteral("已切换到浅色主题"),
                             2000);
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
    m_plotDirty = false;
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
    m_measuredPeriod = 0.0;
    m_measuredJitter = 0.0;
    if (m_scope)
        m_scope->setSamplePeriod(0.0);
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
        if (!m_serialThread->wait(3000)) {
            // 线程仍未退出时不能让它随父对象析构（QThread 运行中被销毁会 qFatal），
            // 脱离父子关系，宁可泄漏也不崩。
            m_serialThread->setParent(nullptr);
        }
    }
    m_worker = nullptr;
}

// 导入/导出线程的收尾：quit + wait；等待超时说明线程仍卡在写盘/解析里，
// 此时绝不能让它随父对象析构（QThread 运行中被销毁会 qFatal），
// 脱离父子关系，宁可泄漏一个 QThread 对象也不能让程序崩掉。
bool Baud::stopIoThread(QThread *&thread)
{
    if (!thread)
        return true;

    thread->quit();
    const bool stopped = thread->wait(3000);
    if (stopped)
        thread->deleteLater();
    else
        thread->setParent(nullptr);
    thread = nullptr;
    return stopped;
}

// 退出前停掉 CSV 导入/导出线程（原因见 stopIoThread）
void Baud::stopIoThreads()
{
    stopIoThread(m_exportThread);
    m_exporter = nullptr;
    stopIoThread(m_importThread);
    m_importer = nullptr;
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
    stopIoThreads();
    event->accept();
}
