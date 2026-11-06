#include "ClientListView.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

ClientListView::ClientListView(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    // Search bar
    auto *topRow = new QHBoxLayout();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Search clients by name, mobile or email...");
    m_search->setClearButtonEnabled(true);
    topRow->addWidget(m_search);
    m_addBtn = new QPushButton("Add Client", this);
    m_editBtn = new QPushButton("Edit", this);
    m_delBtn = new QPushButton("Delete", this);
    m_freeTimeBtn = new QPushButton("Add Free Time", this);
    topRow->addWidget(m_addBtn);
    topRow->addWidget(m_editBtn);
    topRow->addWidget(m_delBtn);
    topRow->addWidget(m_freeTimeBtn);
    layout->addLayout(topRow);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({"ID", "Name", "Mobile", "Email", "Time Bank", "Membership"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAlternatingRowColors(true);
    m_table->setColumnHidden(0, true);
    layout->addWidget(m_table);

    m_countLabel = new QLabel("0 clients", this);
    layout->addWidget(m_countLabel);

    connect(m_search, &QLineEdit::textChanged, this, &ClientListView::onSearchChanged);
    connect(m_addBtn, &QPushButton::clicked, this, &ClientListView::onAdd);
    connect(m_editBtn, &QPushButton::clicked, this, &ClientListView::onEdit);
    connect(m_delBtn, &QPushButton::clicked, this, &ClientListView::onDelete);
    connect(m_freeTimeBtn, &QPushButton::clicked, this, &ClientListView::onAddFreeTime);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ClientListView::onSelectionChanged);
}

void ClientListView::setClients(const QList<Client> &clients) {
    m_clients = clients;
    m_table->setRowCount(0);
    for (const Client &c : clients) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(QString::number(c.id)));
        m_table->setItem(row, 1, new QTableWidgetItem(c.name));
        m_table->setItem(row, 2, new QTableWidgetItem(c.mobile));
        m_table->setItem(row, 3, new QTableWidgetItem(c.email));
        m_table->setItem(row, 4, new QTableWidgetItem(c.timeBankString()));
        m_table->setItem(row, 5, new QTableWidgetItem(c.membershipType));
    }
    m_countLabel->setText(QString("%1 client(s)").arg(clients.size()));
}

qint64 ClientListView::selectedClientId() const {
    auto items = m_table->selectedItems();
    if (items.isEmpty()) return 0;
    int row = items.first()->row();
    auto *idItem = m_table->item(row, 0);
    return idItem ? idItem->text().toLongLong() : 0;
}

void ClientListView::onSearchChanged(const QString &text) {
    emit searchChanged(text);
}

void ClientListView::onAdd() {
    emit addRequested();
}

void ClientListView::onEdit() {
    qint64 id = selectedClientId();
    if (id == 0) {
        QMessageBox::information(this, "Select", "Please select a client to edit.");
        return;
    }
    emit editRequested(id);
}

void ClientListView::onDelete() {
    qint64 id = selectedClientId();
    if (id == 0) {
        QMessageBox::information(this, "Select", "Please select a client to delete.");
        return;
    }
    emit deleteRequested(id);
}

void ClientListView::onAddFreeTime() {
    const qint64 id = selectedClientId();
    if (id == 0) {
        QMessageBox::information(this, "Select", "Please select a client to add free time.");
        return;
    }
    emit addFreeTimeRequested(id);
}

void ClientListView::onSelectionChanged() {
    emit clientSelected(selectedClientId());
}
