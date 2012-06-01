#include "importconfig.h"
#include "ui_importconfig.h"

#include "preferences.h"
#include "appfunc.h"
#include "crypt.h"
#include "zip.h"

#include "message.h"

ImportConfig::ImportConfig(QWidget *parent) :
    QDialog(parent),
    m_ui(new Ui::ImportConfig)
{
    m_ui->setupUi(this);
    m_ui->txtPassword->setEchoMode(QLineEdit::Password);
    this->setWindowFlags(Qt::WindowCloseButtonHint);
}

void ImportConfig::changeEvent(QEvent *e)
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

void ImportConfig::showEvent(QShowEvent *e) {
    m_ui->txtImportPath->clear();
    m_ui->txtNewName->clear();
    m_ui->txtPassword->clear();
    m_ui->txtNewName->setEnabled(false);
    m_ui->rbSaveAsFile->setChecked(true);
    // Mittig ausrichten
    int winW = this->width();
    int winH = this->height();

    int left = Preferences::instance()->geometry().x();
    left = left + (Preferences::instance()->geometry().width() - winW) / 2;

    // Nun die neuen setzen
    this->setGeometry(left, (qApp->desktop()->height() / 2) - (winH / 2), winW, winH);
    // �ffnen
    e->accept();
    this->setWindowState(Qt::WindowActive);
}

void ImportConfig::on_cmdOpenCryptFile_clicked()
{
    QFileDialog certFileDialog;
    QString filename = certFileDialog.getOpenFileName(this, tr("Find import file"), QApplication::applicationDirPath(), tr("Import files (*.crypt)"));
    if (!filename.isEmpty()) {
        m_ui->txtImportPath->setText(filename);
    }
}

void ImportConfig::on_cmdCancel_clicked()
{
    this->close();
}

void ImportConfig::on_rbSaveAsName_toggled(bool checked)
{
    if (checked) {
        m_ui->txtNewName->setEnabled(true);
    } else {
        m_ui->txtNewName->setText("");
        m_ui->txtNewName->setEnabled(false);
    }
}

void ImportConfig::on_cmdImport_clicked()
{
    if (!m_ui->txtExistingOvpn->text().isEmpty()) {
        // Import existing file
        QFile linkOvpn (AppFunc::getAppSavePath() + QString ("/configs.txt"));
        if (!linkOvpn.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
            Message::error(QObject::tr("Unable to open configs.txt!"), QObject::tr("Import Configuration"));
            return;
        }
        // Datei offen, Config schreiben
        QTextStream out (&linkOvpn);
        out << m_ui->txtExistingOvpn->text() + QString ("\n");
        // Liste aktualisieren
        linkOvpn.close();

        Preferences::instance()->refreshConfigList();
        Preferences::instance()->setConnectionStatus();
        Preferences::instance()->setIcon();

        // Fertig
        Message::information(QObject::tr("Import successfully ended!"), QObject::tr("Import Configuration"));
        Preferences::instance()->refreshDialog();
        this->close();
    } else {
        // Import crypt file
        if (m_ui->txtPassword->text().isEmpty()) {
            Message::error(QObject::tr("No password specified!"), QObject::tr("Import Configuration"));
            return;
        }
        if (!m_ui->txtImportPath->text().isEmpty()) {
            if (m_ui->rbSaveAsName->isChecked() && m_ui->txtNewName->text().isEmpty()) {
                Message::error(QObject::tr("No import name specified!"), QObject::tr("Import Configuration"));
                return;
            }

            // Portale oder install
            QString dirPath;
            QString configName;

            if (!m_ui->rbSaveAsName->isChecked()) {
                configName = m_ui->txtImportPath->text().right(m_ui->txtImportPath->text().size() - m_ui->txtImportPath->text().lastIndexOf("/") -1);
                configName = configName.left(configName.size()-6);
            } else {
                configName = m_ui->txtNewName->text().trimmed();
            }

            dirPath = AppFunc::getAppSavePath() + QString ("/") + configName;

            // Verzeichnis da?
            QDir dirobj (dirPath);
            if (!dirobj.exists(dirPath)){
                //Verzeichnis existiert nicht
                // Pfad erstellen
                if (!dirobj.mkpath(dirPath)) {
                    // Pfad konnte nicht erstellt werden
                    Message::error(QObject::tr("Unable to create directory!"), QObject::tr("Import Configuration"));
                    return;
                }
            } else {
                // Verzeichnis existiert
                Message::error(QObject::tr("A diretory with this name already exists!"), QObject::tr("Import Configuration"));
                return;
            }
            // Datei ins neue Verzeichnis kopieren
            //QFile importFileCrypt (m_ui->txtImportPath->text());
            QString packFile = dirPath + QString("/") + configName + QString(".zip");

            // Erstmal entschl�sseln
            if(QFile::exists(packFile)) {
                QFile::remove(packFile);
            }

            // Die Daten einlesen
            {
                QFile crypted (m_ui->txtImportPath->text());
                if (crypted.open(QIODevice::ReadOnly)) {
                    // Nun das Ziel �fffnen
                    QFile targetFile (packFile);
                    if (targetFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        // Alles offen, kann losgehen
                        QByteArray data = crypted.readAll();
                        Crypt crypt;
                        crypt.setSecretKey(m_ui->txtPassword->text());
                        targetFile.write(crypt.cryptToPlainTextExt(data));
                        targetFile.waitForBytesWritten(3000);
                        targetFile.flush();
                        targetFile.waitForBytesWritten(3000);

                        // Alles Ok
                        targetFile.close();
                        crypted.close();
                    }
                } else {
                    Message::error(QObject::tr("Can't open crypted file"));
                    return;
                }
            }

            // Nun die Datei entpacken
            if (!Zip::extract(packFile, dirPath)) {
                Message::error(QObject::tr("Can't open zip file"));
                return;
            }

            // Nun ist alles entpackt, das Ziparchiv nun l�schen
            QFile configZip (packFile);
            if (!configZip.remove()) {
                Message::error(configZip.errorString(), QObject::tr("Import Configuration"));
            }

            if (m_ui->rbSaveAsName->isChecked()) {
                // ovpn umbennen
                QString ovpnFilePath = m_ui->txtImportPath->text().right(m_ui->txtImportPath->text().size() - m_ui->txtImportPath->text().lastIndexOf("/") -1);
                        ovpnFilePath = dirPath + QString("/") + ovpnFilePath.left(ovpnFilePath.size()-6) + QString(".ovpn");
                QFile ovpnFile (ovpnFilePath);
                if (ovpnFile.exists()) {
                    // umbenennen
                    ovpnFile.rename(dirPath + QString("/") + configName + QString(".ovpn"));
                }
            }

            QFile ovpnFile (dirPath + QString("/") + configName + QString(".ovpn"));
            if (!ovpnFile.exists()) {
                Message::error(QObject::tr("Import failed! Removing empty directory."), QObject::tr("Import Configuration"));
                dirobj.rmdir(dirPath);
            } else {
                Preferences::instance()->refreshConfigList();
                Preferences::instance()->setConnectionStatus();
                Message::information(QObject::tr("Import successfully ended!"), QObject::tr("Import Configuration"));
                Preferences::instance()->refreshDialog();
                Preferences::instance()->setIcon();
                this->close();
            }
        } else {
            Message::error(QObject::tr("No import file selected!"), QObject::tr("Import Configuration"));            
        }
    }
}

void ImportConfig::showProcessError (QProcess::ProcessError err) {
    QString errMessage;
    switch (err) {
        case QProcess::FailedToStart:
            errMessage = QLatin1String ("The process failed to start. Either the invoked program is missing, or you may have insufficient permissions to invoke the program.");
            break;
        case QProcess::Crashed:
            errMessage = QLatin1String ("The process crashed some time after starting successfully.");
            break;
        case QProcess::Timedout:
            errMessage = QLatin1String ("The last waitFor...() function timed out. The state of QProcess is unchanged, and you can try calling waitFor...() again.");
            break;
        case QProcess::WriteError:
            errMessage = QLatin1String ("An error occurred when attempting to write to the process. For example, the process may not be running, or it may have closed its input channel.");
            break;
        case QProcess::ReadError:
            errMessage = QLatin1String ("An error occurred when attempting to read from the process. For example, the process may not be running.");
            break;
        case QProcess::UnknownError:
            errMessage = QLatin1String ("An unknown error occurred. This is the default return value of error().");
            break;
        default:
            errMessage = QLatin1String ("No valid error code!");
            break;
    }

    // Daten ausgeben
    Message::error(errMessage, QObject::tr("Import Configuration"));
}


void ImportConfig::on_cmdOpenOvpnFile_clicked()
{
    QFileDialog certFileDialog;
    QString filename (certFileDialog.getOpenFileName(this, QObject::tr("Find ovpn file"), AppFunc::getAppSavePath(), QObject::tr("OpenVPN configs (*.ovpn)")));

    if (!filename.isEmpty()) {
        m_ui->txtExistingOvpn->setText(filename);
    }
}
