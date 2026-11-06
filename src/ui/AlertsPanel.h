#ifndef ALERTSPANEL_H
#define ALERTSPANEL_H

#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QVBoxLayout>

class AlertsPanel : public QWidget {
    Q_OBJECT
public:
    explicit AlertsPanel(QWidget *parent = nullptr);

    void addAlert(const QString &text, bool critical = false);
    void restoreAlert(const QString &text, bool critical = false);
    void clearAlerts();
    void setDarkTheme(bool darkTheme);

signals:
    void alertAdded(const QString &text, bool critical);
    void testBeepRequested();
    void stopBeepRequested();

private:
    void appendAlert(const QString &text, bool critical);
    void updateAlertColors();

    QLabel *m_title;
    QListWidget *m_list;
    bool m_darkTheme = true;
};

#endif // ALERTSPANEL_H
