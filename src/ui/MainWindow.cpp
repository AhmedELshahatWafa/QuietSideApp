#include "MainWindow.h"

#include "database/DatabaseManager.h"
#include "ui/ClientFormDialog.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QApplication>
#include <QMessageBox>
#include <QInputDialog>
#include <QStringList>
#include <QSpinBox>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QTimer>
#include <QUrl>
#include <QAction>
#include <QKeySequence>
#include <QSettings>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Coworking Space Manager");
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    resize(1280, 800);

    m_clientManager = new ClientManager(this);
    m_sessionService = new SessionTimerService(this);
    m_timeoutAudioOutput = new QAudioOutput(this);
    m_timeoutPlayer = new QMediaPlayer(this);
    m_timeoutPlayer->setAudioOutput(m_timeoutAudioOutput);
    m_timeoutPlayer->setSource(QUrl("qrc:/Sounds/TimeOutBeeb.mp3"));
    m_timeoutRepeatTimer = new QTimer(this);
    m_timeoutRepeatTimer->setInterval(10000);
    connect(m_timeoutRepeatTimer, &QTimer::timeout, this, &MainWindow::playTimeoutAlert);
    connect(m_timeoutPlayer, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &error) {
        m_alertsPanel->addAlert(QString("Timeout alert sound error: %1").arg(error), true);
    });

    setupUi();
    QSettings settings;
    applyTheme(settings.value("appearance/darkTheme", true).toBool());

    connect(m_alertsPanel, &AlertsPanel::alertAdded, this, [this](const QString &text, bool critical) {
        if (!DatabaseManager::instance().addActiveAlert(text, critical)) {
            QMessageBox::warning(this, "Alert Not Saved",
                                 DatabaseManager::instance().lastError());
        }
    });

    QList<QPair<QString, bool>> savedAlerts;
    if (DatabaseManager::instance().loadActiveAlerts(savedAlerts)) {
        for (const auto &alert : savedAlerts) {
            m_alertsPanel->restoreAlert(alert.first, alert.second);
        }
    } else {
        QMessageBox::warning(this, "Alerts Not Loaded",
                             DatabaseManager::instance().lastError());
    }
    connect(m_alertsPanel, &AlertsPanel::testBeepRequested,
            this, &MainWindow::testTimeoutAlert);
    connect(m_alertsPanel, &AlertsPanel::stopBeepRequested,
            this, &MainWindow::stopTimeoutAlert);
    auto *testBeepAction = new QAction(this);
    testBeepAction->setShortcut(QKeySequence("Ctrl+Shift+B"));
    testBeepAction->setShortcutContext(Qt::WindowShortcut);
    addAction(testBeepAction);
    connect(testBeepAction, &QAction::triggered, this, &MainWindow::testTimeoutAlert);

    // Load data
    loadRooms();
    refreshClients();
    refreshSessions();

    // Connections: client manager
    connect(m_clientManager, &ClientManager::clientsChanged, this, &MainWindow::refreshClients);
    connect(m_clientManager, &ClientManager::error, this, [this](const QString &msg) {
        QMessageBox::warning(this, "Client Error", msg);
    });
    connect(m_clientListView, &ClientListView::addRequested, this, &MainWindow::onAddClient);
    connect(m_clientListView, &ClientListView::addFreeTimeRequested,
            this, &MainWindow::onAddFreeTime);
    connect(m_clientListView, &ClientListView::editRequested, this, &MainWindow::onEditClient);
    connect(m_clientListView, &ClientListView::deleteRequested, this, &MainWindow::onDeleteClient);
    connect(m_clientListView, &ClientListView::searchChanged, this, &MainWindow::onSearchClients);

    // Sessions
    connect(m_sessionService, &SessionTimerService::sessionsUpdated, this, &MainWindow::onSessionsUpdated);
    connect(m_sessionService, &SessionTimerService::error, this, [this](const QString &msg) {
        m_alertsPanel->addAlert(QString("Session timer error: %1").arg(msg), true);
    });
    connect(m_sessionService, &SessionTimerService::reservationTimeExpired, this,
            [this](const Session &session, const QString &clientName, const QString &roomName) {
        m_alertsPanel->addAlert(
            QString("Reserved time is up: %1 in %2 is paused for checkout or an upgrade (session #%3).")
                .arg(clientName, roomName)
                .arg(session.id),
            true);
    });
    connect(m_sessionService, &SessionTimerService::sessionsUpdated, this, &MainWindow::refreshPayments);
    refreshPayments();

    // Restore persisted sessions and account for time elapsed while the app was closed.
    m_sessionService->start(5000);

    // Lobby
    connect(m_lobbyView, &LobbyView::roomClicked, this, &MainWindow::onLobbyRoomClicked);
    connect(m_lobbyView, &LobbyView::sessionClicked, this, &MainWindow::onLobbySessionClicked);

    // Buttons
    connect(m_checkinBtn, &QPushButton::clicked, this, &MainWindow::onCheckIn);
    connect(m_checkoutBtn, &QPushButton::clicked, this, &MainWindow::onCheckOut);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onPauseSession);
    connect(m_resumeBtn, &QPushButton::clicked, this, &MainWindow::onResumeSession);
    connect(m_upgradeReservationBtn, &QPushButton::clicked,
            this, &MainWindow::onUpgradeReservation);
    connect(m_moveBtn, &QPushButton::clicked, this, &MainWindow::onMoveSession);
}

MainWindow::~MainWindow() {
    m_sessionService->stop();
    delete m_sessionService;
}


void MainWindow::setupUi() {
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    auto *topBar = new QWidget(central);
    auto *topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(4, 0, 4, 4);
    auto *appTitle = new QLabel("QuietSide Workspace", topBar);
    appTitle->setStyleSheet("font-size:18px; font-weight:bold;");
    m_themeToggleBtn = new QPushButton(topBar);
    m_themeToggleBtn->setObjectName("themeToggleButton");
    topBarLayout->addWidget(appTitle);
    topBarLayout->addStretch();
    topBarLayout->addWidget(m_themeToggleBtn);
    mainLayout->addWidget(topBar);

    m_splitter = new QSplitter(Qt::Horizontal, central);

    // ---- Left panel: Lobby + Alerts ----
    auto *leftPanel = new QWidget(m_splitter);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    auto *lobbyLabel = new QLabel("Floor Plan / Lobby", leftPanel);
    lobbyLabel->setStyleSheet("font-weight:bold; font-size:14px; padding:4px;");
    leftLayout->addWidget(lobbyLabel);

    m_lobbyView = new LobbyView(leftPanel);
    leftLayout->addWidget(m_lobbyView, 1);

    m_alertsPanel = new AlertsPanel(leftPanel);
    m_alertsPanel->setMinimumHeight(160);
    m_alertsPanel->setMaximumHeight(260);
    leftLayout->addWidget(m_alertsPanel);

    m_splitter->addWidget(leftPanel);

    // ---- Right panel: tabs ----
    auto *rightPanel = new QWidget(m_splitter);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_managementTabs = new QTabWidget(rightPanel);
    m_managementTabs->setObjectName("managementTabs");

    // Tab 1: Clients
    m_clientListView = new ClientListView(rightPanel);
    m_managementTabs->addTab(m_clientListView, "Clients");

    // Tab 2: Sessions / Check-in
    auto *sessionTab = new QWidget(m_managementTabs);
    auto *sessionLayout = new QVBoxLayout(sessionTab);

    // Check-in group
    auto *checkinGroup = new QGroupBox("Check In Client", sessionTab);
    auto *checkinLayout = new QVBoxLayout(checkinGroup);
    auto *checkinClientRoomRow = new QHBoxLayout();
    m_checkinClientCombo = new QComboBox(checkinGroup);
    m_checkinClientCombo->setMinimumWidth(180);
    m_checkinRoomCombo = new QComboBox(checkinGroup);
    m_checkinRoomCombo->setMinimumWidth(160);
    checkinClientRoomRow->addWidget(new QLabel("Client:", checkinGroup));
    checkinClientRoomRow->addWidget(m_checkinClientCombo, 1);
    checkinClientRoomRow->addWidget(new QLabel("Room:", checkinGroup));
    checkinClientRoomRow->addWidget(m_checkinRoomCombo, 1);
    checkinLayout->addLayout(checkinClientRoomRow);

    auto *checkinBillingRow = new QHBoxLayout();
    m_billingModeCombo = new QComboBox(checkinGroup);
    m_billingModeCombo->addItem("Pay as you go", sessionBillingModeToString(SessionBillingMode::PayAsYouGo));
    m_billingModeCombo->addItem("Reserve hours", sessionBillingModeToString(SessionBillingMode::Reserved));
    m_reservationPlanCombo = new QComboBox(checkinGroup);
    m_reservationPlanCombo->addItem("1 hour - 30 EGP", QVariantList{60, 30.0});
    m_reservationPlanCombo->addItem("2 hours - 55 EGP", QVariantList{120, 55.0});
    m_reservationPlanCombo->addItem("4 hours - 90 EGP", QVariantList{240, 90.0});
    m_reservationPlanCombo->addItem("8 hours - 150 EGP", QVariantList{480, 150.0});
    m_reservationPlanCombo->setEnabled(false);
    m_checkinBtn = new QPushButton("Check In", checkinGroup);
    checkinBillingRow->addWidget(new QLabel("Billing:", checkinGroup));
    checkinBillingRow->addWidget(m_billingModeCombo);
    checkinBillingRow->addWidget(new QLabel("Package:", checkinGroup));
    checkinBillingRow->addWidget(m_reservationPlanCombo);
    checkinBillingRow->addWidget(m_checkinBtn);
    checkinLayout->addLayout(checkinBillingRow);
    connect(m_billingModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) {
        m_reservationPlanCombo->setEnabled(
            m_billingModeCombo->currentData().toString()
                == sessionBillingModeToString(SessionBillingMode::Reserved));
    });
    sessionLayout->addWidget(checkinGroup);

    // Active sessions table
    auto *sessionsLabel = new QLabel("Active Sessions", sessionTab);
    sessionsLabel->setStyleSheet("font-weight:bold; font-size:13px; padding:4px;");
    sessionLayout->addWidget(sessionsLabel);

    m_sessionsTable = new QTableWidget(sessionTab);
    m_sessionsTable->setColumnCount(6);
    m_sessionsTable->setHorizontalHeaderLabels({"ID", "Client", "Room", "Check-in", "Time Left / Used", "Status"});
    m_sessionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sessionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sessionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sessionsTable->horizontalHeader()->setStretchLastSection(true);
    m_sessionsTable->setAlternatingRowColors(true);
    m_sessionsTable->setColumnHidden(0, true);
    sessionLayout->addWidget(m_sessionsTable, 1);

    // Session actions
    auto *actionsLayout = new QHBoxLayout();
    m_checkoutBtn = new QPushButton("Check Out", sessionTab);
    m_pauseBtn = new QPushButton("Pause", sessionTab);
    m_resumeBtn = new QPushButton("Resume", sessionTab);
    m_upgradeReservationBtn = new QPushButton("Upgrade Reservation", sessionTab);
    m_upgradeReservationBtn->setEnabled(false);
    m_resumeBtn->setEnabled(false);
    m_moveBtn = new QPushButton("Move to Room", sessionTab);
    m_moveRoomCombo = new QComboBox(sessionTab);
    actionsLayout->addWidget(m_checkoutBtn);
    actionsLayout->addWidget(m_pauseBtn);
    actionsLayout->addWidget(m_resumeBtn);
    actionsLayout->addWidget(m_upgradeReservationBtn);
    actionsLayout->addWidget(m_moveRoomCombo);
    actionsLayout->addWidget(m_moveBtn);
    actionsLayout->addStretch();
    sessionLayout->addLayout(actionsLayout);
    connect(m_sessionsTable, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::updateSelectedSessionActions);

    m_managementTabs->addTab(sessionTab, "Sessions");

    auto *debtTab = new QWidget(m_managementTabs);
    auto *debtLayout = new QVBoxLayout(debtTab);
    auto *debtLabel = new QLabel("Checked-out clients with unpaid visit charges or legacy balances", debtTab);
    debtLabel->setStyleSheet("font-weight:bold; font-size:13px; padding:4px;");
    debtLayout->addWidget(debtLabel);

    m_debtClientsTable = new QTableWidget(debtTab);
    m_debtClientsTable->setColumnCount(5);
    m_debtClientsTable->setHorizontalHeaderLabels({"Client", "Mobile", "Email", "Total Debt (EGP)", "Action"});
    m_debtClientsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_debtClientsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_debtClientsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_debtClientsTable->horizontalHeader()->setStretchLastSection(true);
    m_debtClientsTable->setAlternatingRowColors(true);
    debtLayout->addWidget(m_debtClientsTable);
    m_managementTabs->addTab(debtTab, "In Debt");

    auto *paymentsTab = new QWidget(m_managementTabs);
    auto *paymentsLayout = new QVBoxLayout(paymentsTab);
    auto *paymentsLabel = new QLabel(
        "Checkout payments: 0.50 EGP per started minute, 30 EGP minimum per visit. "
        "Legacy debt settlements are charged at 0.50 EGP per owed minute.",
        paymentsTab);
    paymentsLayout->addWidget(paymentsLabel);
    m_paymentsTotalLabel = new QLabel("Total paid: 0.00 EGP", paymentsTab);
    m_paymentsTotalLabel->setStyleSheet("font-weight:bold; padding:4px;");
    paymentsLayout->addWidget(m_paymentsTotalLabel);

    m_paymentsTable = new QTableWidget(paymentsTab);
    m_paymentsTable->setColumnCount(5);
    m_paymentsTable->setHorizontalHeaderLabels({"Date", "Client", "Visit", "Amount (EGP)", "Method"});
    m_paymentsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_paymentsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_paymentsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_paymentsTable->horizontalHeader()->setStretchLastSection(true);
    m_paymentsTable->setAlternatingRowColors(true);
    paymentsLayout->addWidget(m_paymentsTable, 1);
    m_managementTabs->addTab(paymentsTab, "Payments");

    rightLayout->addWidget(m_managementTabs);
    m_splitter->addWidget(rightPanel);

    m_splitter->setSizes({640, 720});
    mainLayout->addWidget(m_splitter, 1);
    connect(m_themeToggleBtn, &QPushButton::clicked, this,
            [this]() { applyTheme(!m_isDarkTheme); });

}

void MainWindow::loadRooms() {
    m_rooms = DatabaseManager::instance().getAllRooms();
    populateRoomCombo(m_checkinRoomCombo);
    populateRoomCombo(m_moveRoomCombo);
    m_lobbyView->setRooms(m_rooms);
}

void MainWindow::populateRoomCombo(QComboBox *combo) {
    combo->clear();
    for (const Room &r : m_rooms) {
        combo->addItem(r.roomName, r.id);
    }
}

qint64 MainWindow::selectedSessionId() const {
    auto items = m_sessionsTable->selectedItems();
    if (items.isEmpty()) return 0;
    int row = items.first()->row();
    auto *idItem = m_sessionsTable->item(row, 0);
    return idItem ? idItem->text().toLongLong() : 0;
}

QString MainWindow::clientName(qint64 id) const {
    for (const Client &c : m_clients) if (c.id == id) return c.name;
    Client c = DatabaseManager::instance().getClient(id);
    return c.name;
}

QString MainWindow::roomName(qint64 id) const {
    for (const Room &r : m_rooms) if (r.id == id) return r.roomName;
    Room r = DatabaseManager::instance().getRoom(id);
    return r.roomName;
}

void MainWindow::refreshClients() {
    const qint64 selectedClientId = m_checkinClientCombo->currentData().toLongLong();
    m_clients = m_clientManager->getAllClients();
    m_clientListView->setClients(m_clientManager->searchClients(m_clientListView->searchText()));

    m_checkinClientCombo->clear();
    int selectedIndex = -1;
    for (const Client &c : m_clients) {
        m_checkinClientCombo->addItem(c.name, c.id);
        if (c.id == selectedClientId) {
            selectedIndex = m_checkinClientCombo->count() - 1;
        }
    }
    if (selectedIndex >= 0) m_checkinClientCombo->setCurrentIndex(selectedIndex);
    refreshDebtClients();
}

void MainWindow::refreshPayments() {
    const QList<Transaction> transactions = DatabaseManager::instance().getAllTransactions();
    m_paymentsTable->setRowCount(0);
    double totalPaid = 0.0;
    for (const Transaction &payment : transactions) {
        if (payment.type != TransactionType::Payment) continue;
        totalPaid += payment.amount;
        const int row = m_paymentsTable->rowCount();
        m_paymentsTable->insertRow(row);
        m_paymentsTable->setItem(row, 0,
            new QTableWidgetItem(payment.timestamp.toLocalTime().toString("yyyy-MM-dd hh:mm AP")));
        m_paymentsTable->setItem(row, 1, new QTableWidgetItem(payment.clientName));
        m_paymentsTable->setItem(row, 2, new QTableWidgetItem(payment.description));
        m_paymentsTable->setItem(row, 3,
            new QTableWidgetItem(QString::number(payment.amount, 'f', 2)));
        m_paymentsTable->setItem(row, 4, new QTableWidgetItem(payment.paymentMethod));
    }
    m_paymentsTotalLabel->setText(QString("Total paid: %1 EGP").arg(totalPaid, 0, 'f', 2));
}

void MainWindow::refreshDebtClients() {
    const QList<ActiveSessionInfo> activeSessions = m_sessionService->activeSessions();
    m_debtClientsTable->setRowCount(0);

    for (const Client &client : m_clientManager->getAllClients()) {
        double outstandingDebt = 0.0;
        if (!DatabaseManager::instance().getClientOutstandingDebt(client.id, outstandingDebt)) {
            m_alertsPanel->addAlert(
                QString("Could not load debt for %1: %2")
                    .arg(client.name, DatabaseManager::instance().lastError()),
                true);
            continue;
        }
        if (outstandingDebt <= 0.0) continue;

        bool checkedIn = false;
        for (const ActiveSessionInfo &info : activeSessions) {
            if (info.session.clientId == client.id
                && (info.session.status == SessionStatus::Active || info.session.isPaused)) {
                checkedIn = true;
                break;
            }
        }
        if (checkedIn) continue;

        const int row = m_debtClientsTable->rowCount();
        m_debtClientsTable->insertRow(row);
        m_debtClientsTable->setItem(row, 0, new QTableWidgetItem(client.name));
        m_debtClientsTable->setItem(row, 1, new QTableWidgetItem(client.mobile));
        m_debtClientsTable->setItem(row, 2, new QTableWidgetItem(client.email));
        m_debtClientsTable->setItem(
            row, 3, new QTableWidgetItem(QString("%1 EGP").arg(outstandingDebt, 0, 'f', 2)));
        auto *paymentButton = new QPushButton("View Payment", m_debtClientsTable);
        m_debtClientsTable->setCellWidget(row, 4, paymentButton);
        connect(paymentButton, &QPushButton::clicked, this, [this, client, outstandingDebt]() {
            const double amountDue = outstandingDebt;

            QDialog dialog(this);
            dialog.setWindowTitle("Review Outstanding Payment");
            dialog.setModal(true);
            auto *layout = new QVBoxLayout(&dialog);
            auto *summary = new QLabel(
                QString("%1 has an outstanding balance from an unpaid visit or legacy time debt.\n"
                        "Total payment due: %2 EGP.")
                    .arg(client.name)
                    .arg(amountDue, 0, 'f', 2),
                &dialog);
            summary->setWordWrap(true);
            layout->addWidget(summary);

            auto *methodCombo = new QComboBox(&dialog);
            methodCombo->addItems({"Cash", "Card", "Bank transfer", "Other"});
            auto *form = new QFormLayout();
            form->addRow("Payment method", methodCombo);
            layout->addLayout(form);

            auto *buttonsLayout = new QHBoxLayout();
            auto *confirmButton = new QPushButton("Confirm Payment Received", &dialog);
            auto *cancelButton = new QPushButton("Cancel", &dialog);
            buttonsLayout->addStretch();
            buttonsLayout->addWidget(cancelButton);
            buttonsLayout->addWidget(confirmButton);
            layout->addLayout(buttonsLayout);
            connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
            connect(confirmButton, &QPushButton::clicked, &dialog,
                    [this, &dialog, client, outstandingDebt]() {
                auto *method = dialog.findChild<QComboBox *>();
                if (!method) {
                    QMessageBox::critical(&dialog, "Payment Error", "Could not read the payment method.");
                    return;
                }

                Transaction payment;
                if (!DatabaseManager::instance().recordClientDebtPayment(
                        client.id, outstandingDebt, method->currentText(), payment)) {
                    QMessageBox::warning(&dialog, "Payment Not Recorded",
                                         DatabaseManager::instance().lastError());
                    return;
                }

                refreshClients();
                refreshPayments();
                QMessageBox::information(
                    &dialog, "Payment Confirmed",
                    QString("%1 EGP received from %2.\nThe outstanding debt has been cleared.")
                        .arg(payment.amount, 0, 'f', 2)
                        .arg(client.name));
                dialog.accept();
                m_managementTabs->setCurrentIndex(m_managementTabs->indexOf(m_paymentsTable->parentWidget()));
            });
            dialog.exec();
        });
    }
}


void MainWindow::onAddClient() {
    ClientFormDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        Client c = dlg.client();
        if (m_clientManager->addClient(c)) {
            refreshClients();
        }
    }
}

void MainWindow::onAddFreeTime(qint64 clientId) {
    const Client client = m_clientManager->getClient(clientId);
    if (client.id == 0) {
        QMessageBox::warning(this, "Add Free Time", "The selected client could not be found.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Add Free Time");
    dialog.setModal(true);
    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel(
        QString("Add complimentary time to %1's time bank.").arg(client.name), &dialog);
    layout->addWidget(description);

    auto *hours = new QSpinBox(&dialog);
    hours->setRange(0, 999);
    hours->setSuffix(" hours");
    auto *minutes = new QSpinBox(&dialog);
    minutes->setRange(0, 59);
    minutes->setSuffix(" minutes");
    auto *form = new QFormLayout();
    form->addRow("Hours", hours);
    form->addRow("Minutes", minutes);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Add Free Time");
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return;
    const int totalMinutes = hours->value() * 60 + minutes->value();
    if (totalMinutes <= 0) {
        QMessageBox::warning(this, "Add Free Time", "Enter a duration greater than zero.");
        return;
    }
    if (m_clientManager->addFreeTime(clientId, totalMinutes)) {
        m_alertsPanel->addAlert(
            QString("Added %1h %2m of free time to %3's time bank.")
                .arg(totalMinutes / 60)
                .arg(totalMinutes % 60, 2, 10, QChar('0'))
                .arg(client.name),
            false);
    }
}

void MainWindow::onEditClient(qint64 clientId) {
    Client c = m_clientManager->getClient(clientId);
    ClientFormDialog dlg(this, c);
    if (dlg.exec() == QDialog::Accepted) {
        Client updated = dlg.client();
        updated.id = clientId;
        if (m_clientManager->updateClient(updated)) {
            refreshClients();
        }
    }
}

void MainWindow::onDeleteClient(qint64 clientId) {
    auto reply = QMessageBox::question(this, "Delete Client",
        "Delete this client and all their sessions?", QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        if (m_clientManager->deleteClient(clientId)) {
            refreshClients();
        }
    }
}

void MainWindow::onSearchClients(const QString &text) {
    m_clients = m_clientManager->searchClients(text);
    m_clientListView->setClients(m_clients);
}
void MainWindow::onCheckIn() {
    qint64 clientId = m_checkinClientCombo->currentData().toLongLong();
    qint64 roomId = m_checkinRoomCombo->currentData().toLongLong();
    if (clientId == 0 || roomId == 0) {
        QMessageBox::warning(this, "Check In", "Please select a client and a room.");
        return;
    }

    // Prevent duplicate active check-in for the same client
    for (const ActiveSessionInfo &info : m_sessionService->activeSessions()) {
        if (info.session.clientId == clientId
            && (info.session.status == SessionStatus::Active || info.session.isPaused)) {
            QMessageBox::warning(this, "Check In",
                QString("Client \"%1\" is already checked in to %2.")
                .arg(info.clientName, info.roomName));
            return;
        }
    }

    const SessionBillingMode billingMode =
        sessionBillingModeFromString(m_billingModeCombo->currentData().toString());
    int reservedMinutes = 0;
    double reservedPrice = 0.0;
    if (billingMode == SessionBillingMode::Reserved) {
        const QVariantList package = m_reservationPlanCombo->currentData().toList();
        if (package.size() != 2) {
            QMessageBox::warning(this, "Check In", "Select a valid reservation package.");
            return;
        }
        reservedMinutes = package[0].toInt();
        reservedPrice = package[1].toDouble();
    }

    Session out;
    if (m_sessionService->checkIn(clientId, roomId, billingMode,
                                  reservedMinutes, reservedPrice, out)) {
        const QString billingDescription = billingMode == SessionBillingMode::Reserved
            ? QString("reserved for %1 hour(s) at %2 EGP")
                  .arg(reservedMinutes / 60.0, 0, 'f', 2)
                  .arg(reservedPrice, 0, 'f', 2)
            : "pay-as-you-go";
        m_alertsPanel->addAlert(
            QString("Checked in: %1 -> %2 (%3)")
                .arg(clientName(clientId), roomName(roomId), billingDescription),
            false);
    }
}

void MainWindow::onCheckOut() {
    qint64 sid = selectedSessionId();
    if (sid == 0) {
        QMessageBox::information(this, "Select", "Select a session to check out.");
        return;
    }
    int billableMinutes = 0;
    double amount = 0.0;
    if (!m_sessionService->checkoutQuote(sid, billableMinutes, amount)) {
        QMessageBox::warning(this, "Check Out", "This session is no longer active.");
        return;
    }

    QMessageBox decision(this);
    decision.setWindowTitle("Complete Checkout");
    decision.setIcon(QMessageBox::Question);
    decision.setText(QString("Total due: %1 EGP\nTime used: %2 started minute(s).\n"
                             "Was the payment received?")
                         .arg(amount, 0, 'f', 2)
                         .arg(billableMinutes));
    QPushButton *paidButton = decision.addButton("Confirm Payment Received", QMessageBox::AcceptRole);
    QPushButton *debtButton = decision.addButton("Add to Debt", QMessageBox::DestructiveRole);
    decision.addButton("Cancel", QMessageBox::RejectRole);
    decision.exec();
    const bool paymentReceived = decision.clickedButton() == paidButton;
    if (!paymentReceived && decision.clickedButton() != debtButton) return;

    QString method;
    if (paymentReceived) {
        const QStringList methods = {"Cash", "Card", "Bank transfer", "Other"};
        bool methodSelected = false;
        method = QInputDialog::getItem(
            this, "Checkout Payment", "Payment method received:", methods, 0, false, &methodSelected);
        if (!methodSelected) return;
    }

    Transaction payment;
    int freeTimeCreditedMinutes = 0;
    if (m_sessionService->checkOut(
            sid, paymentReceived, method, payment, freeTimeCreditedMinutes)) {
        const int finalBillableMinutes = DatabaseManager::instance().getSession(sid).billableStartedMinutes();
        if (freeTimeCreditedMinutes > 0) {
            m_alertsPanel->addAlert(
                QString("Credited %1h %2m of unused reserved time to %3's time bank.")
                    .arg(freeTimeCreditedMinutes / 60)
                    .arg(freeTimeCreditedMinutes % 60, 2, 10, QChar('0'))
                    .arg(clientName(payment.clientId)),
                false);
        }
        if (paymentReceived) {
            m_alertsPanel->addAlert(
                QString("Payment received: %1 EGP (%2, %3).")
                    .arg(payment.amount, 0, 'f', 2).arg(finalBillableMinutes).arg(method),
                false);
            QMessageBox::information(
                this, "Checkout Complete",
                QString("Payment recorded: %1 EGP\nTime: %2 started minute(s)\nMethod: %3")
                    .arg(payment.amount, 0, 'f', 2).arg(finalBillableMinutes).arg(method));
        } else {
            m_alertsPanel->addAlert(
                QString("Unpaid checkout recorded as debt: %1 EGP for %2.")
                    .arg(payment.amount, 0, 'f', 2)
                    .arg(clientName(payment.clientId)),
                true);
            QMessageBox::information(
                this, "Debt Recorded",
                QString("%1 EGP is now due from %2 and will appear in the In Debt tab.")
                    .arg(payment.amount, 0, 'f', 2)
                    .arg(clientName(payment.clientId)));
        }
        m_pauseBtn->setEnabled(false);
        m_resumeBtn->setEnabled(false);
    }
}

void MainWindow::onPauseSession() {
    qint64 sid = selectedSessionId();
    if (sid == 0) return;
    if (m_sessionService->pauseSession(sid)) {
        m_alertsPanel->addAlert("Session paused.", false);
        m_pauseBtn->setEnabled(false);
        m_resumeBtn->setEnabled(true);
    }
}

void MainWindow::onResumeSession() {
    qint64 sid = selectedSessionId();
    if (sid == 0) return;
    if (m_sessionService->resumeSession(sid)) {
        m_alertsPanel->addAlert("Session resumed.", false);
        m_pauseBtn->setEnabled(true);
        m_resumeBtn->setEnabled(false);
    }
}

void MainWindow::onUpgradeReservation() {
    const qint64 sid = selectedSessionId();
    Session selected;
    for (const ActiveSessionInfo &info : m_sessionService->activeSessions()) {
        if (info.session.id == sid) {
            selected = info.session;
            break;
        }
    }
    if (sid == 0 || !selected.limitReached
        || selected.billingMode != SessionBillingMode::Reserved) {
        QMessageBox::information(this, "Upgrade Reservation",
                                 "Select a reservation that has reached its time limit.");
        return;
    }

    const QList<int> planMinutes = {60, 120, 240, 480};
    const QList<double> planPrices = {30.0, 55.0, 90.0, 150.0};
    QStringList planLabels;
    QList<int> availablePlanIndexes;
    for (int i = 0; i < planMinutes.size(); ++i) {
        if (planMinutes[i] <= selected.reservedDurationMinutes) continue;
        planLabels.append(QString("%1 hours - %2 EGP")
                              .arg(planMinutes[i] / 60)
                              .arg(planPrices[i], 0, 'f', 2));
        availablePlanIndexes.append(i);
    }
    if (planLabels.isEmpty()) {
        QMessageBox::information(this, "Upgrade Reservation",
                                 "This reservation is already at the longest available package.");
        return;
    }

    bool accepted = false;
    const QString selectedPlan = QInputDialog::getItem(
        this, "Upgrade Reservation", "Choose the new total reservation:", planLabels, 0, false, &accepted);
    if (!accepted) return;
    const int selectedPlanIndex = planLabels.indexOf(selectedPlan);
    if (selectedPlanIndex < 0) return;
    const int planIndex = availablePlanIndexes[selectedPlanIndex];
    if (!m_sessionService->upgradeReservation(sid, planMinutes[planIndex], planPrices[planIndex])) return;

    m_alertsPanel->addAlert(
        QString("Reservation upgraded to %1 hours for %2 EGP.")
            .arg(planMinutes[planIndex] / 60)
            .arg(planPrices[planIndex], 0, 'f', 2),
        false);
}

void MainWindow::onMoveSession() {
    qint64 sid = selectedSessionId();
    if (sid == 0) {
        QMessageBox::information(this, "Select", "Select a session to move.");
        return;
    }
    qint64 newRoom = m_moveRoomCombo->currentData().toLongLong();
    if (newRoom == 0) return;
    m_sessionService->moveClient(sid, newRoom);
    m_alertsPanel->addAlert(QString("Moved session to %1.").arg(roomName(newRoom)), false);
}

void MainWindow::refreshSessions() {
    onSessionsUpdated();
}

void MainWindow::onSessionsUpdated() {
    QList<ActiveSessionInfo> infos = m_sessionService->activeSessions();
    syncTimeoutAlertPlayback(infos);
    refreshDebtClients();
    const qint64 selectedSid = selectedSessionId();
    m_sessionsTable->setRowCount(0);

    for (const ActiveSessionInfo &info : infos) {
        int row = m_sessionsTable->rowCount();
        m_sessionsTable->insertRow(row);
        m_sessionsTable->setItem(row, 0, new QTableWidgetItem(QString::number(info.session.id)));
        m_sessionsTable->setItem(row, 1, new QTableWidgetItem(info.clientName));
        m_sessionsTable->setItem(row, 2, new QTableWidgetItem(info.roomName));
        m_sessionsTable->setItem(row, 3,
            new QTableWidgetItem(info.session.checkInTime.toLocalTime().toString("hh:mm AP")));
        const QString timeDisplay = info.session.billingMode == SessionBillingMode::Reserved
            ? info.session.remainingTimeString()
            : QString("%1 min used").arg(info.session.billableStartedMinutes());
        m_sessionsTable->setItem(row, 4, new QTableWidgetItem(timeDisplay));
        QString status = sessionStatusToString(info.session.status);
        if (info.session.limitReached) status = "TIME LIMIT - ACTION";
        else if (info.session.isPaused) status = "PAUSED";
        auto *statusItem = new QTableWidgetItem(status);
        if (info.session.limitReached) {
            statusItem->setBackground(m_isDarkTheme ? QColor("#512f2a") : QColor("#f2d5c9"));
            statusItem->setForeground(m_isDarkTheme ? QColor("#ffd7cf") : QColor("#632b1d"));
        } else if (info.session.isPaused) {
            statusItem->setBackground(m_isDarkTheme ? QColor("#51402b") : QColor("#f2e3c8"));
            statusItem->setForeground(m_isDarkTheme ? QColor("#f6d79e") : QColor("#68451f"));
        }
        m_sessionsTable->setItem(row, 5, statusItem);
        if (info.session.id == selectedSid) {
            m_sessionsTable->selectRow(row);
        }

    }

    updateSelectedSessionActions();

    QList<LobbySession> lobbySessions;
    for (const ActiveSessionInfo &info : infos) {
        LobbySession ls;
        ls.session = info.session;
        ls.clientName = info.clientName;
        lobbySessions.append(ls);
    }
    m_lobbyView->setSessions(lobbySessions);
}

void MainWindow::syncTimeoutAlertPlayback(const QList<ActiveSessionInfo> &sessions) {
    QSet<qint64> expiredSessionIds;
    for (const ActiveSessionInfo &info : sessions) {
        if (info.session.limitReached
            && (info.session.status == SessionStatus::Active
                || info.session.status == SessionStatus::Paused)) {
            expiredSessionIds.insert(info.session.id);
        }
    }

    bool newlyExpired = false;
    for (qint64 sessionId : expiredSessionIds) {
        if (!m_timeoutAlertSessionIds.contains(sessionId)) {
            newlyExpired = true;
        }
    }
    for (qint64 sessionId : m_timeoutAlertSessionIds) {
        if (!expiredSessionIds.contains(sessionId)) {
            m_dismissedTimeoutAlertSessionIds.remove(sessionId);
        }
    }
    m_timeoutAlertSessionIds = expiredSessionIds;

    QSet<qint64> audibleSessionIds = m_timeoutAlertSessionIds;
    for (qint64 sessionId : m_dismissedTimeoutAlertSessionIds) {
        audibleSessionIds.remove(sessionId);
    }

    if (audibleSessionIds.isEmpty()) {
        m_timeoutRepeatTimer->stop();
        if (m_timeoutAlertSessionIds.isEmpty()) {
            m_timeoutPlayer->stop();
        }
    } else {
        if (!m_timeoutRepeatTimer->isActive()) {
            m_timeoutRepeatTimer->start();
        }
        if (newlyExpired) {
            playTimeoutAlert();
        }
    }
}

void MainWindow::playTimeoutAlert() {
    if (m_timeoutAlertSessionIds.isEmpty()) return;
    for (qint64 sessionId : m_timeoutAlertSessionIds) {
        if (!m_dismissedTimeoutAlertSessionIds.contains(sessionId)) {
            m_timeoutPlayer->stop();
            m_timeoutPlayer->setPosition(0);
            m_timeoutPlayer->play();
            return;
        }
    }
}

void MainWindow::testTimeoutAlert() {
    m_timeoutPlayer->stop();
    m_timeoutPlayer->setPosition(0);
    m_timeoutPlayer->play();
}

void MainWindow::stopTimeoutAlert() {
    m_dismissedTimeoutAlertSessionIds.unite(m_timeoutAlertSessionIds);
    m_timeoutRepeatTimer->stop();
    m_timeoutPlayer->stop();
}

void MainWindow::applyTheme(bool darkTheme) {
    m_isDarkTheme = darkTheme;
    const QString darkStyle = R"(
        QWidget { background-color:#211d19; color:#f3e8d8; }
        QMainWindow, QDialog { background-color:#211d19; }
        QGroupBox { background-color:#2b2520; border:1px solid #594735; border-radius:8px; margin-top:10px; padding:10px 6px 6px; font-weight:bold; }
        QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 5px; background-color:#2b2520; }
        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox, QDoubleSpinBox, QListWidget, QTableWidget {
            background-color:#2b2520; color:#f3e8d8; border:1px solid #594735; border-radius:5px; padding:5px; selection-background-color:#75502d; selection-color:#fff8ee;
        }
        QComboBox QAbstractItemView { background-color:#2b2520; color:#f3e8d8; selection-background-color:#75502d; }
        QHeaderView::section { background-color:#393027; color:#ead3b3; border:0; border-bottom:1px solid #594735; padding:7px; font-weight:bold; }
        QTableWidget { gridline-color:#473b30; alternate-background-color:#302922; }
        QTableWidget::item:selected { background-color:#75502d; color:#fff8ee; }
        QListWidget::item:selected { background-color:#75502d; color:#fff8ee; }
        QTabWidget::pane { border:1px solid #594735; border-radius:6px; top:-1px; }
        QTabBar::tab { background-color:#302922; color:#d5c3aa; border:1px solid #594735; padding:8px 14px; margin-right:3px; border-top-left-radius:5px; border-top-right-radius:5px; }
        QTabBar::tab:selected { background-color:#493726; color:#ffe2b5; border-bottom-color:#493726; }
        QPushButton { background-color:#493726; color:#f7ecdc; border:1px solid #765637; border-radius:6px; padding:7px 12px; }
        QPushButton:hover { background-color:#60472f; border-color:#c28d50; }
        QPushButton:pressed { background-color:#35271b; }
        QPushButton:disabled { background-color:#302a24; color:#8f8273; border-color:#493f35; }
        QScrollBar:vertical { background:#211d19; width:12px; margin:0; }
        QScrollBar::handle:vertical { background:#66513a; min-height:24px; border-radius:5px; }
        QScrollBar:horizontal { background:#211d19; height:12px; margin:0; }
        QScrollBar::handle:horizontal { background:#66513a; min-width:24px; border-radius:5px; }
        QScrollBar::add-line, QScrollBar::sub-line { width:0; height:0; }
        QToolTip { background-color:#3b2c1f; color:#fff3e1; border:1px solid #c28d50; padding:4px; }
        QSplitter::handle { background-color:#473b30; }
    )";
    const QString lightStyle = R"(
        QWidget { background-color:#f4e7d1; color:#3b2415; }
        QMainWindow, QDialog { background-color:#f4e7d1; }
        QGroupBox { background-color:#fff9ef; border:1px solid #d6b98d; border-radius:8px; margin-top:10px; padding:10px 6px 6px; font-weight:bold; }
        QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 5px; background-color:#fff9ef; }
        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox, QDoubleSpinBox, QListWidget, QTableWidget {
            background-color:#fffaf2; color:#3b2415; border:1px solid #d6b98d; border-radius:5px; padding:5px; selection-background-color:#e4c99d; selection-color:#3b2415;
        }
        QComboBox QAbstractItemView { background-color:#fffaf2; color:#3b2415; selection-background-color:#e4c99d; }
        QHeaderView::section { background-color:#ead9bb; color:#4b2b16; border:0; border-bottom:1px solid #d6b98d; padding:7px; font-weight:bold; }
        QTableWidget { gridline-color:#e1cfad; alternate-background-color:#f8efdf; }
        QTableWidget::item:selected { background-color:#e4c99d; color:#3b2415; }
        QListWidget::item:selected { background-color:#e4c99d; color:#3b2415; }
        QTabWidget::pane { border:1px solid #d6b98d; border-radius:6px; top:-1px; }
        QTabBar::tab { background-color:#ead9bb; color:#68452b; border:1px solid #d6b98d; padding:8px 14px; margin-right:3px; border-top-left-radius:5px; border-top-right-radius:5px; }
        QTabBar::tab:selected { background-color:#fff9ef; color:#4b2b16; border-bottom-color:#fff9ef; }
        QPushButton { background-color:#70401f; color:#fff8ed; border:1px solid #70401f; border-radius:6px; padding:7px 12px; }
        QPushButton:hover { background-color:#8b552e; border-color:#bd8950; }
        QPushButton:pressed { background-color:#552d16; }
        QPushButton:disabled { background-color:#e8ddca; color:#a3947d; border-color:#d6c7ad; }
        QScrollBar:vertical { background:#f4e7d1; width:12px; margin:0; }
        QScrollBar::handle:vertical { background:#c9a36b; min-height:24px; border-radius:5px; }
        QScrollBar:horizontal { background:#f4e7d1; height:12px; margin:0; }
        QScrollBar::handle:horizontal { background:#c9a36b; min-width:24px; border-radius:5px; }
        QScrollBar::add-line, QScrollBar::sub-line { width:0; height:0; }
        QToolTip { background-color:#fff3df; color:#3b2415; border:1px solid #bd8950; padding:4px; }
        QSplitter::handle { background-color:#d6b98d; }
    )";

    qApp->setStyleSheet(darkTheme ? darkStyle : lightStyle);
    if (m_lobbyView) m_lobbyView->setDarkTheme(darkTheme);
    if (m_alertsPanel) m_alertsPanel->setDarkTheme(darkTheme);
    if (m_themeToggleBtn) {
        m_themeToggleBtn->setText(darkTheme ? "☀ Light theme" : "☾ Dark theme");
    }
    QSettings settings;
    settings.setValue("appearance/darkTheme", darkTheme);
}

void MainWindow::updateSelectedSessionActions() {
    const qint64 sid = selectedSessionId();
    Session selected;
    for (const ActiveSessionInfo &info : m_sessionService->activeSessions()) {
        if (info.session.id == sid) {
            selected = info.session;
            break;
        }
    }
    const bool hasSelection = selected.id != 0;
    m_checkoutBtn->setEnabled(hasSelection);
    m_pauseBtn->setEnabled(hasSelection && selected.status == SessionStatus::Active
                           && !selected.isPaused && !selected.limitReached);
    m_resumeBtn->setEnabled(hasSelection && selected.status == SessionStatus::Paused
                            && selected.isPaused && !selected.limitReached);
    m_upgradeReservationBtn->setEnabled(hasSelection && selected.limitReached
                                        && selected.billingMode == SessionBillingMode::Reserved);
}

void MainWindow::onLobbyRoomClicked(qint64 roomId) {
    for (int i = 0; i < m_checkinRoomCombo->count(); ++i) {
        if (m_checkinRoomCombo->itemData(i).toLongLong() == roomId) {
            m_checkinRoomCombo->setCurrentIndex(i);
            break;
        }
    }
}

void MainWindow::onLobbySessionClicked(qint64 sessionId) {
    for (int r = 0; r < m_sessionsTable->rowCount(); ++r) {
        if (m_sessionsTable->item(r, 0) && m_sessionsTable->item(r, 0)->text().toLongLong() == sessionId) {
            m_sessionsTable->selectRow(r);
            break;
        }
    }
}

void MainWindow::onClientSelected(qint64 /*clientId*/) {
    // Placeholder: client preview can be added here later
}
