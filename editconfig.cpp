#include "editconfig.h"
#include "ui_editconfig.h"

#include "preferences.h"

EditConfig::EditConfig(const QString &path) :
    QDialog(),
    m_ui(new Ui::EditConfig),
    path (path)
{
    m_ui->setupUi(this);    
    this->setWindowFlags(Qt::WindowCloseButtonHint);
}

void EditConfig::changeEvent(QEvent *e)
{
    QDialog::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        m_ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

void EditConfig::showEvent(QShowEvent *e) {
    m_ui->memConfigPath->setPlainText(this->path.replace("\\", "/"));
    // Content holen und setzen
    m_ui->memConfigContent->setPlainText(this->getContent());
    // Dialog �ffnen
    // Mittig ausrichten
    int winW = this->width();
    int winH = this->height();

    int left (0);
    if (Preferences::instance()->isVisible()) {
        // Wenn das Hauptfenster offen ist mittig �ber diesem plazieren
        left = Preferences::instance()->geometry().x();
        left = left + (Preferences::instance()->geometry().width() - winW) / 2;
    } else {
        // Desktop auswerten
        left = qApp->desktop()->width();
        // Die Breite bei virtuellen Desktops vierteln
        if (left > 2000 && qApp->desktop()->isVirtualDesktop()) {
            left /= 4;
        }
    }
    // Nun die neuen setzen
    this->setGeometry(left, (qApp->desktop()->height() / 2) - (winH / 2), winW, winH);

    // �ffnen
    e->accept();
    this->setWindowState(Qt::WindowActive);
}

void EditConfig::on_cmdSave_clicked()
{
    // Speichern
    QFile saveFile (this->path);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
         return;
    }

     QTextStream out(&saveFile);
     out << m_ui->memConfigContent->toPlainText();
     saveFile.close();
     // Reload File
     m_ui->memConfigContent->setPlainText(this->getContent());
     this->close();
}

void EditConfig::on_cmdClose_clicked()
{
    this->close();
}

QString EditConfig::getContent() {
    QString fileContent ("");
    QFile cf (this->path);

    if (!cf.open(QIODevice::ReadOnly | QIODevice::Text))
         return QLatin1String("");

    QTextStream in(&cf);
    while (!in.atEnd()) {
        QString line = in.readLine();
        fileContent += line + QLatin1String("\n");
    }
    cf.close();

    return fileContent;
}
