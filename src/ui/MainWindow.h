#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QTabWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QList>
#include <QSet>

#include "models/Client.h"
#include "models/Room.h"
#include "models/Session.h"
#include "models/Transaction.h"
#include "ui/LobbyView.h"
#include "ui/ClientListView.h"
#include "ui/AlertsPanel.h"
#include "services/ClientManager.h"
#include "services/SessionTimerService.h"

class QAudioOutput;
class QMediaPlayer;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void refreshClients();
    void refreshDebtClients();
    void refreshPayments();
    void onAddClient();
    void onAddFreeTime(qint64 clientId);
    void onEditClient(qint64 clientId);
    void onDeleteClient(qint64 clientId);
    void onSearchClients(const QString &text);
    void onClientSelected(qint64 clientId);

    void onCheckIn();
    void onCheckOut();
    void onPauseSession();
    void onResumeSession();
    void onUpgradeReservation();
    void onMoveSession();

    void refreshSessions();
    void onSessionsUpdated();
    void testTimeoutAlert();

    void onLobbyRoomClicked(qint64 roomId);
    void onLobbySessionClicked(qint64 sessionId);

private:
    void setupUi();
    void loadRooms();
    void populateRoomCombo(QComboBox *combo);
    void updateSelectedSessionActions();
    void syncTimeoutAlertPlayback(const QList<ActiveSessionInfo> &sessions);
    void playTimeoutAlert();
    void stopTimeoutAlert();
    void applyTheme(bool darkTheme);
    qint64 selectedSessionId() const;
    QString clientName(qint64 id) const;
    QString roomName(qint64 id) const;

    ClientManager *m_clientManager;
    SessionTimerService *m_sessionService;
    QMediaPlayer *m_timeoutPlayer;
    QAudioOutput *m_timeoutAudioOutput;
    QTimer *m_timeoutRepeatTimer;
    QSet<qint64> m_timeoutAlertSessionIds;
    QSet<qint64> m_dismissedTimeoutAlertSessionIds;
    bool m_isDarkTheme = true;

    // UI
    QSplitter *m_splitter;
    QPushButton *m_themeToggleBtn;
    QTabWidget *m_managementTabs;
    LobbyView *m_lobbyView;
    AlertsPanel *m_alertsPanel;
    ClientListView *m_clientListView;

    // check-in controls
    QComboBox *m_checkinClientCombo;
    QComboBox *m_checkinRoomCombo;
    QComboBox *m_billingModeCombo;
    QComboBox *m_reservationPlanCombo;
    QPushButton *m_checkinBtn;

    // session action buttons
    QPushButton *m_checkoutBtn;
    QPushButton *m_pauseBtn;
    QPushButton *m_resumeBtn;
    QPushButton *m_upgradeReservationBtn;
    QPushButton *m_moveBtn;
    QComboBox *m_moveRoomCombo;

    // sessions table
    QTableWidget *m_sessionsTable;
    QTableWidget *m_debtClientsTable;
    QTableWidget *m_paymentsTable;
    QLabel *m_paymentsTotalLabel;

    QList<Room> m_rooms;
    QList<Client> m_clients;
    QList<Session> m_sessions; // active
};

#endif // MAINWINDOW_H
