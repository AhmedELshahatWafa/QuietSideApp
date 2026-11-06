#include "AlertsPanel.h"
#include <QListWidgetItem>
#include <QHBoxLayout>
#include <QPushButton>

AlertsPanel::AlertsPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    auto *headerLayout = new QHBoxLayout();
    m_title = new QLabel("Active Alerts", this);
    m_title->setStyleSheet("font-weight:bold; font-size:14px;");
    auto *testBeepButton = new QPushButton("Test Beep", this);
    testBeepButton->setVisible(false);
    auto *stopBeepButton = new QPushButton("Stop Beep", this);
    headerLayout->addWidget(m_title);
    headerLayout->addStretch();
    headerLayout->addWidget(testBeepButton);
    headerLayout->addWidget(stopBeepButton);
    layout->addLayout(headerLayout);

    connect(testBeepButton, &QPushButton::clicked, this, &AlertsPanel::testBeepRequested);
    connect(stopBeepButton, &QPushButton::clicked, this, &AlertsPanel::stopBeepRequested);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    layout->addWidget(m_list);
}

void AlertsPanel::addAlert(const QString &text, bool critical) {
    appendAlert(text, critical);
    emit alertAdded(text, critical);
}

void AlertsPanel::restoreAlert(const QString &text, bool critical) {
    appendAlert(text, critical);
}

void AlertsPanel::appendAlert(const QString &text, bool critical) {
    auto *item = new QListWidgetItem(text, m_list);
    item->setData(Qt::UserRole, critical);
    updateAlertColors();
    m_list->scrollToBottom();
}

void AlertsPanel::setDarkTheme(bool darkTheme) {
    m_darkTheme = darkTheme;
    updateAlertColors();
}

void AlertsPanel::updateAlertColors() {
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        const bool critical = item->data(Qt::UserRole).toBool();
        if (m_darkTheme) {
            item->setBackground(critical ? QColor("#512f2a") : QColor("#51402b"));
            item->setForeground(critical ? QColor("#ffd7cf") : QColor("#f6d79e"));
        } else {
            item->setBackground(critical ? QColor("#f2d5c9") : QColor("#f2e3c8"));
            item->setForeground(critical ? QColor("#632b1d") : QColor("#68451f"));
        }
    }
}

void AlertsPanel::clearAlerts() {
    m_list->clear();
}
