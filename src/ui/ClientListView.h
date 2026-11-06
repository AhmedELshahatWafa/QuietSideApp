#ifndef CLIENTLISTVIEW_H
#define CLIENTLISTVIEW_H

#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

#include "models/Client.h"

class ClientListView : public QWidget {
    Q_OBJECT
public:
    explicit ClientListView(QWidget *parent = nullptr);

    void setClients(const QList<Client> &clients);
    qint64 selectedClientId() const;
    QString searchText() const { return m_search->text(); }

signals:
    void addRequested();
    void editRequested(qint64 clientId);
    void deleteRequested(qint64 clientId);
    void addFreeTimeRequested(qint64 clientId);
    void searchChanged(const QString &text);
    void clientSelected(qint64 clientId);

private slots:
    void onSearchChanged(const QString &text);
    void onAdd();
    void onEdit();
    void onDelete();
    void onAddFreeTime();
    void onSelectionChanged();

private:
    QLineEdit *m_search;
    QTableWidget *m_table;
    QPushButton *m_addBtn;
    QPushButton *m_editBtn;
    QPushButton *m_delBtn;
    QPushButton *m_freeTimeBtn;
    QLabel *m_countLabel;
    QList<Client> m_clients;
};

#endif // CLIENTLISTVIEW_H
