#ifndef CLIENTFORMDIALOG_H
#define CLIENTFORMDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>

#include "models/Client.h"

class ClientFormDialog : public QDialog {
    Q_OBJECT
public:
    explicit ClientFormDialog(QWidget *parent = nullptr, const Client &client = Client());

    Client client() const;

private:
    QLineEdit *m_name;
    QLineEdit *m_mobile;
    QLineEdit *m_email;
    QComboBox *m_membership;
    QTextEdit *m_notes;
    qint64 m_id;
    QDateTime m_createdAt;
};

#endif // CLIENTFORMDIALOG_H
