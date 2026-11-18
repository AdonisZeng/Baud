// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "ui/ChannelConfigDialog.h"

#include "ui/ValueFormat.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
// 码表编辑对话框：码名 + 码值，可增删行
bool editCodeTable(QWidget *parent, const QString &typeId, QVector<ChannelCode> &codes)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("码表 · %1").arg(typeId));
    dialog.resize(380, 340);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *hint = new QLabel(
        QStringLiteral("码名与码值一一对应，例如 RUN = 6。"
                       "波形上该通道会直接显示“码名：码值”。"),
        &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    QTableWidget *table = new QTableWidget(0, 2, &dialog);
    table->setHorizontalHeaderLabels({QStringLiteral("码名"), QStringLiteral("码值")});
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table->setColumnWidth(1, 90);
    layout->addWidget(table, 1);

    const auto fillRow = [table](int row, const QString &name, const QString &value) {
        QTableWidgetItem *nameItem = new QTableWidgetItem(name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable);
        table->setItem(row, 0, nameItem);

        QTableWidgetItem *valueItem = new QTableWidgetItem(value);
        valueItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable);
        valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(row, 1, valueItem);
    };

    for (const ChannelCode &code : codes)
        fillRow(table->rowCount(), code.name, ValueFormat::format(code.value));

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    QPushButton *addButton = new QPushButton(QStringLiteral("添加码值"), &dialog);
    QPushButton *removeButton = new QPushButton(QStringLiteral("删除选中"), &dialog);
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(removeButton);
    buttonLayout->addStretch(1);
    layout->addLayout(buttonLayout);

    // 自由函数里要用 QObject::connect（Qt 6 已移除全局 connect 重载）
    QObject::connect(addButton, &QPushButton::clicked, table, [table, fillRow]() {
        const int row = table->rowCount();
        table->insertRow(row);
        fillRow(row, QString(), QStringLiteral("0"));
        table->setCurrentCell(row, 0);
        table->editItem(table->item(row, 0));
    });
    QObject::connect(removeButton, &QPushButton::clicked, table, [table]() {
        const int row = table->currentRow();
        if (row >= 0)
            table->removeRow(row);
    });

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // 码值非法时提示后重新打开，保留用户已经填好的内容
    while (true) {
        if (dialog.exec() != QDialog::Accepted)
            return false;

        QVector<ChannelCode> result;
        bool valid = true;
        for (int row = 0; row < table->rowCount(); ++row) {
            const QTableWidgetItem *nameItem = table->item(row, 0);
            const QTableWidgetItem *valueItem = table->item(row, 1);
            const QString name = nameItem ? nameItem->text().trimmed() : QString();
            if (name.isEmpty())
                continue;
            const QString valueText = valueItem ? valueItem->text().trimmed() : QString();
            bool ok = false;
            const double value = valueText.toDouble(&ok);
            if (!ok) {
                QMessageBox::warning(&dialog, QStringLiteral("码表"),
                                     QStringLiteral("码值必须是数字：%1").arg(valueText));
                valid = false;
                break;
            }
            result.append({name, value});
        }
        if (!valid)
            continue;

        codes = result;
        return true;
    }
}
} // namespace

ChannelConfigDialog::ChannelConfigDialog(const ChannelConfig &config, int channelCount,
                                         QWidget *parent)
    : QDialog(parent)
    , m_config(config)
    , m_channelCount(channelCount)
{
    setWindowTitle(QStringLiteral("通道配置"));
    resize(560, 520);
    buildUi();
    reloadChannelTable();
    reloadTypeTable();
}

void ChannelConfigDialog::buildUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    QTabWidget *tabs = new QTabWidget(this);

    // ---- 通道页 ----
    QWidget *channelPage = new QWidget(tabs);
    QVBoxLayout *channelLayout = new QVBoxLayout(channelPage);
    m_channelHint = new QLabel(
        QStringLiteral("通道名称会显示在波形图例与悬停提示里；"
                       "给通道指定类型后，波形上该通道会显示“码名：码值”。"),
        channelPage);
    m_channelHint->setWordWrap(true);
    channelLayout->addWidget(m_channelHint);

    m_channelTable = new QTableWidget(0, 3, channelPage);
    m_channelTable->setHorizontalHeaderLabels(
        {QStringLiteral("通道"), QStringLiteral("名称"), QStringLiteral("类型")});
    m_channelTable->verticalHeader()->setVisible(false);
    m_channelTable->verticalHeader()->setDefaultSectionSize(26);
    m_channelTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_channelTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_channelTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_channelTable->setColumnWidth(0, 60);
    m_channelTable->setColumnWidth(2, 190);
    channelLayout->addWidget(m_channelTable, 1);
    tabs->addTab(channelPage, QStringLiteral("通道"));

    // ---- 类型页 ----
    QWidget *typePage = new QWidget(tabs);
    QVBoxLayout *typeLayout = new QVBoxLayout(typePage);
    QLabel *typeHint = new QLabel(
        QStringLiteral("状态码 / 错误码在不同工程里定义不同，"
                       "这里可以新增自己的通道类型，并给每种类型定义码名与码值。"),
        typePage);
    typeHint->setWordWrap(true);
    typeLayout->addWidget(typeHint);

    m_typeTable = new QTableWidget(0, 4, typePage);
    m_typeTable->setHorizontalHeaderLabels({QStringLiteral("类型 ID"), QStringLiteral("名称"),
                                            QStringLiteral("码值"), QStringLiteral("操作")});
    m_typeTable->verticalHeader()->setVisible(false);
    m_typeTable->verticalHeader()->setDefaultSectionSize(26);
    m_typeTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_typeTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_typeTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_typeTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_typeTable->setColumnWidth(0, 120);
    m_typeTable->setColumnWidth(3, 100);
    typeLayout->addWidget(m_typeTable, 1);

    QHBoxLayout *typeButtons = new QHBoxLayout;
    QPushButton *addTypeButton = new QPushButton(QStringLiteral("新增类型"), typePage);
    QPushButton *removeTypeButton = new QPushButton(QStringLiteral("删除类型"), typePage);
    typeButtons->addWidget(addTypeButton);
    typeButtons->addWidget(removeTypeButton);
    typeButtons->addStretch(1);
    typeLayout->addLayout(typeButtons);

    connect(addTypeButton, &QPushButton::clicked, this, &ChannelConfigDialog::onAddType);
    connect(removeTypeButton, &QPushButton::clicked, this, &ChannelConfigDialog::onRemoveType);

    tabs->addTab(typePage, QStringLiteral("通道类型"));
    layout->addWidget(tabs, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        commitChannelTable();
        commitTypeTable();

        // 类型 ID 必须唯一且非空，否则通道无法正确引用
        for (int index = 0; index < m_config.typeCount(); ++index) {
            const ChannelType *type = m_config.typeAt(index);
            if (!type || type->id.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("通道配置"),
                                     QStringLiteral("类型 ID 不能为空。"));
                return;
            }
            if (m_config.indexOfType(type->id) != index) {
                QMessageBox::warning(this, QStringLiteral("通道配置"),
                                     QStringLiteral("类型 ID 重复：%1").arg(type->id));
                return;
            }
        }

        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ChannelConfigDialog::reloadChannelTable()
{
    // 至少给出 8 行，方便还没收到数据时先给通道起名
    const int rows = std::max(m_channelCount, 8);
    m_channelTable->setRowCount(rows);

    for (int channel = 0; channel < rows; ++channel) {
        QTableWidgetItem *idItem = new QTableWidgetItem(QStringLiteral("CH%1").arg(channel));
        idItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_channelTable->setItem(channel, 0, idItem);

        // 名称列显示 CH{n} 或自定义名；提交时把与默认名相同的文本视为“未自定义”，
        // 这样点一次“保存”不会给所有通道写上显式名称 CH0…CH7。
        QTableWidgetItem *nameItem = new QTableWidgetItem(m_config.channelName(channel));
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsSelectable);
        nameItem->setToolTip(QStringLiteral("留空则使用默认名称 CH%1").arg(channel));
        m_channelTable->setItem(channel, 1, nameItem);

        QComboBox *combo = new QComboBox(m_channelTable);
        combo->addItem(QStringLiteral("数值（不解释）"), QString());
        for (int index = 0; index < m_config.typeCount(); ++index) {
            const ChannelType *type = m_config.typeAt(index);
            if (!type)
                continue;
            combo->addItem(type->name.isEmpty()
                               ? type->id
                               : QStringLiteral("%1 · %2").arg(type->id, type->name),
                           type->id);
        }
        const int typeIndex = combo->findData(m_config.channelTypeId(channel));
        if (typeIndex >= 0)
            combo->setCurrentIndex(typeIndex);
        m_channelTable->setCellWidget(channel, 2, combo);
    }
}

void ChannelConfigDialog::reloadTypeTable()
{
    m_typeTable->setRowCount(m_config.typeCount());

    for (int row = 0; row < m_config.typeCount(); ++row) {
        const ChannelType *type = m_config.typeAt(row);
        if (!type)
            continue;

        QTableWidgetItem *idItem = new QTableWidgetItem(type->id);
        idItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable);
        idItem->setToolTip(QStringLiteral("通道引用这个 ID，修改后请确认没有重复"));
        m_typeTable->setItem(row, 0, idItem);

        QTableWidgetItem *nameItem = new QTableWidgetItem(type->name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable);
        m_typeTable->setItem(row, 1, nameItem);

        QStringList summary;
        for (const ChannelCode &code : type->codes) {
            summary << QStringLiteral("%1 = %2").arg(code.name, ValueFormat::format(code.value));
        }
        QTableWidgetItem *codesItem = new QTableWidgetItem(summary.join(QStringLiteral("，")));
        codesItem->setFlags(Qt::ItemIsEnabled);
        codesItem->setToolTip(summary.join(QStringLiteral("\n")));
        m_typeTable->setItem(row, 2, codesItem);

        QPushButton *editButton = new QPushButton(QStringLiteral("编辑码值…"), m_typeTable);
        connect(editButton, &QPushButton::clicked, this, [this, row]() { onEditCodes(row); });
        m_typeTable->setCellWidget(row, 3, editButton);
    }
}

void ChannelConfigDialog::commitChannelTable()
{
    for (int row = 0; row < m_channelTable->rowCount(); ++row) {
        if (QTableWidgetItem *item = m_channelTable->item(row, 1)) {
            const QString text = item->text().trimmed();
            // 与默认名相同视为“未自定义”，不写进配置（配置里保持为空，
            // 显示时再回退成 CH{n}）
            const QString fallback = QStringLiteral("CH%1").arg(row);
            m_config.setChannelName(row, text == fallback ? QString() : text);
        }
        if (auto *combo = qobject_cast<QComboBox *>(m_channelTable->cellWidget(row, 2)))
            m_config.setChannelType(row, combo->currentData().toString());
    }
}

void ChannelConfigDialog::commitTypeTable()
{
    for (int row = 0; row < m_typeTable->rowCount(); ++row) {
        const ChannelType *type = m_config.typeAt(row);
        if (!type)
            continue;
        const QTableWidgetItem *idItem = m_typeTable->item(row, 0);
        const QTableWidgetItem *nameItem = m_typeTable->item(row, 1);
        const QString id = idItem ? idItem->text().trimmed() : type->id;
        const QString name = nameItem ? nameItem->text() : type->name;
        if (id.isEmpty())
            continue;
        m_config.setTypeInfo(row, id, name);
    }
}

void ChannelConfigDialog::onAddType()
{
    // 固定顺序：先提交通道（引用的是当前类型 ID），再提交类型（改 ID 时会重映射引用）。
    // 顺序反了会把通道引用覆盖回旧 ID，变成悬空引用。
    commitChannelTable();
    commitTypeTable();

    QString id = QStringLiteral("custom.type");
    int suffix = 2;
    while (m_config.indexOfType(id) >= 0)
        id = QStringLiteral("custom.type%1").arg(suffix++);

    const int index = m_config.addType(id, QStringLiteral("自定义类型"));
    if (index < 0)
        return;

    reloadTypeTable();
    reloadChannelTable();
    m_typeTable->setCurrentCell(index, 0);
    if (QTableWidgetItem *item = m_typeTable->item(index, 0))
        m_typeTable->editItem(item);
}

void ChannelConfigDialog::onRemoveType()
{
    const int row = m_typeTable->currentRow();
    if (row < 0)
        return;

    commitChannelTable();
    commitTypeTable();
    const ChannelType *type = m_config.typeAt(row);
    const QString typeId = type ? type->id : QString();
    if (QMessageBox::question(this, QStringLiteral("通道配置"),
                              QStringLiteral("删除类型 %1？引用它的通道会改回数值显示。")
                                  .arg(typeId),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;

    m_config.removeType(row);
    reloadTypeTable();
    reloadChannelTable();
}

void ChannelConfigDialog::onEditCodes(int row)
{
    commitChannelTable();
    commitTypeTable();
    const ChannelType *type = m_config.typeAt(row);
    if (!type)
        return;

    QVector<ChannelCode> codes = type->codes;
    if (!editCodeTable(this, type->id, codes))
        return;

    m_config.setCodes(row, codes);
    reloadTypeTable();
}
