#include "ClientFormDialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QDateTime>

ClientFormDialog::ClientFormDialog(QWidget *parent, const Client &client)
    : QDialog(parent), m_id(client.id), m_createdAt(client.createdAt) {
    setWindowTitle(client.id > 0 ? "Edit Client" : "Add Client");
    setModal(true);
    resize(420, 360);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_name = new QLineEdit(client.name, this);
    m_mobile = new QLineEdit(client.mobile, this);
    m_email = new QLineEdit(client.email, this);
    m_membership = new QComboBox(this);
    m_membership->setEditable(true);
    m_membership->addItems({"Basic", "Premium", "Day Pass", "Corporate", ""});
    m_membership->setCurrentText(client.membershipType);
    m_notes = new QTextEdit(this);
    m_notes->setPlainText(client.notes);
    m_notes->setMaximumHeight(100);

    form->addRow("Full Name *", m_name);
    form->addRow("Mobile", m_mobile);
    form->addRow("Email", m_email);
    form->addRow("Membership", m_membership);
    if (client.id > 0) {
        form->addRow("Legacy time balance", new QLabel(client.timeBankString(), this));
    } else {
        form->addRow("Billing", new QLabel("Pay-as-you-go at checkout; no time balance is required.", this));
    }
    form->addRow("Notes", m_notes);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

Client ClientFormDialog::client() const {
    Client c;
    c.id = m_id;
    c.name = m_name->text().trimmed();
    c.mobile = m_mobile->text().trimmed();
    c.email = m_email->text().trimmed();
    c.membershipType = m_membership->currentText().trimmed();
    c.notes = m_notes->toPlainText().trimmed();
    c.createdAt = m_createdAt.isValid() ? m_createdAt : QDateTime::currentDateTime();
    return c;
}
