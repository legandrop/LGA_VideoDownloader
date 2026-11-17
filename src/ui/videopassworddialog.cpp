#include "videodownloader/videopassworddialog.h"
#include <QApplication>
#include <QUrl>

VideoPasswordDialog::VideoPasswordDialog(const QString &videoUrl, QWidget *parent)
    : QDialog(parent)
    , m_messageLabel(nullptr)
    , m_passwordInput(nullptr)
    , m_okButton(nullptr)
    , m_cancelButton(nullptr)
    , m_videoPassword("")
{
    setWindowTitle("Video Password Required");
    setModal(true);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // Extract video ID or title from URL for display
    QString displayUrl = videoUrl;
    QUrl url(videoUrl);
    if (url.isValid()) {
        QString path = url.path();
        if (!path.isEmpty() && path != "/") {
            // Try to extract video ID from Vimeo URL
            QStringList parts = path.split('/');
            if (!parts.isEmpty()) {
                QString lastPart = parts.last();
                if (!lastPart.isEmpty()) {
                    displayUrl = QString("vimeo.com/%1").arg(lastPart);
                }
            }
        }
    }

    // Create layout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // Message label
    m_messageLabel = new QLabel(this);
    m_messageLabel->setText(QString("The video <span style=\"font-weight:600;\">%1</span> is protected by a password.\n\nPlease enter the video password:").arg(displayUrl));
    m_messageLabel->setWordWrap(true);
    mainLayout->addWidget(m_messageLabel);

    // Password input
    m_passwordInput = new QLineEdit(this);
    m_passwordInput->setEchoMode(QLineEdit::Password);
    m_passwordInput->setPlaceholderText("Enter video password...");
    mainLayout->addWidget(m_passwordInput);

    // Buttons layout
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_cancelButton = new QPushButton("Cancel", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &VideoPasswordDialog::onCancelClicked);
    buttonLayout->addWidget(m_cancelButton);

    m_okButton = new QPushButton("OK", this);
    m_okButton->setDefault(true);
    connect(m_okButton, &QPushButton::clicked, this, &VideoPasswordDialog::onOkClicked);
    buttonLayout->addWidget(m_okButton);

    mainLayout->addLayout(buttonLayout);

    // Connect enter key to OK button
    connect(m_passwordInput, &QLineEdit::returnPressed, this, &VideoPasswordDialog::onOkClicked);

    // Set focus to password input
    m_passwordInput->setFocus();

    // Set fixed size
    setFixedSize(400, 180);
}

VideoPasswordDialog::~VideoPasswordDialog()
{
    // Widgets are automatically deleted by Qt parent-child system
}

QString VideoPasswordDialog::getVideoPassword() const
{
    return m_videoPassword;
}

void VideoPasswordDialog::onOkClicked()
{
    m_videoPassword = m_passwordInput->text().trimmed();
    if (m_videoPassword.isEmpty()) {
        // Don't accept empty password
        m_passwordInput->setFocus();
        m_passwordInput->selectAll();
        return;
    }
    accept();
}

void VideoPasswordDialog::onCancelClicked()
{
    m_videoPassword = "";
    reject();
}
