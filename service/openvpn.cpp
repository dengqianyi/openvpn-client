#include "openvpn.h"
#include <windows.h>

#include "debug.h"
#include "qthreadexec.h"
#include "srvcli.h"

OpenVpn::OpenVpn ()
    : onDisconnect (false),      
      useNoInteract (false),
      connectionStable (false),      
      _id(-1),
      configName (""),
      configPath (""),      
      configPrivKey (""),
      challengeKey (""),
      connectionIP (""),
      connectionLineBak (""),      
      procScripts (0),
      proxyString ("")
{

}

OpenVpn::~OpenVpn()
{
    Debug::log(QLatin1String("Destroy openvpn"), DebugLevel::Destruktor);
}

void OpenVpn::connectToVpn()
{
    //
    // Die OpenVpn.exe mit der Config starten, um die Verbindung aufzubauen
    //

    // Haben wir eine ID?
    if (this->id() == -1) {
        // Das hier d�rfte nie passieren!
        Debug::error(QLatin1String("OpenVpn: No valid id to connect."));
        return;
    }

    // Pfad f�r die Config bauen
    QString cFile (this->configPath + QLatin1String("/") + this->configName + QLatin1String(".ovpn"));

    // Die Parameter f�r OpenVpn bauen
    QStringList arguments;
    arguments << QString ("--service");
    arguments << QString ("openvpngui_exit_%1").arg(this->id());
    arguments << QString ("0");
    arguments << QString ("--config");
    arguments << cFile;

    // Wurde ein Proxy �bergeben
    if (!this->proxyString.trimmed().isEmpty()) {
        // Proxy wurde �bergeben
        arguments << this->proxyString.split(" ");
    }

    // Interact setzen -> wegen OTP
    if (!this->useNoInteract) {
        arguments << QString ("--auth-retry");
        arguments << QString ("interact");
    }

    // Prozesssignale zur �berwachung an die Slots binden
    if (!QObject::connect(&this->proc, SIGNAL(error ( QProcess::ProcessError)), this, SLOT(showProcessError (QProcess::ProcessError)))) {
        Debug::error(QLatin1String("OpenVPN: Can't connect process error signal"));
    }

    if(!QObject::connect(&this->proc, SIGNAL(readyReadStandardOutput()), this, SLOT(readProcessData()))) {
        Debug::error(QLatin1String("OpenVPN: Can't connect read std signal"));
    }

    if(!QObject::connect(&this->proc, SIGNAL(readyReadStandardError()), this, SLOT(readProcessData()))) {
        Debug::error(QLatin1String("OpenVPN: Can't connect read err signal"));
    }

    if (!QObject::connect(&this->proc, SIGNAL(finished (int, QProcess::ExitStatus)), this, SLOT(processFinished(int, QProcess::ExitStatus)))) {
        Debug::error(QLatin1String("OpenVPN: Can't connect finished signal"));
    }

    // Programm starten im Config Verzeichnis, sonst findet OpenVPN keine Zertifikate
    this->proc.setWorkingDirectory(this->configPath + QLatin1String("/"));

    QString program (QCoreApplication::applicationDirPath() + QLatin1String("/app/bin/openvpn.exe"));

    Debug::log(QLatin1String("OpenVpn path: ") + program);
    Debug::log(QLatin1String("Start OpenVpn with: ") + arguments.join(QLatin1String(" ")));

    // Ist die exe da
    if (!QFile::exists(program)) {
        // Leider nicht
        Debug::error(QLatin1String("OpenVPN.exe is missing"));
        emit removeItemFromList(this->id());
        return;
    }

    // Programm starten
    this->proc.start(program, arguments);
    //
    Debug::log(QLatin1String("wait for started"));
    if (!this->proc.waitForStarted()) {
        Debug::error(QLatin1String("Start failed"));
        Debug::log(this->proc.errorString());
        emit removeItemFromList(this->id());
    }

    Debug::log(QLatin1String("End of connect vpn"));
}

void OpenVpn::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(exitStatus)

    // OpenVpn beenden
    QObject::disconnect(&this->proc, 0, 0, 0);

    emit removeItemFromList(this->id());

    Debug::log(QLatin1String("OpenVpn: Process finished"));

    this->connectionStable = false;    
}

bool OpenVpn::isConnectionStable () const
{
    return this->connectionStable;
}

void OpenVpn::setProxyString(const QString &proxy)
{
    this->proxyString = proxy;
}

void OpenVpn::sendStatus()
{
    SrvCLI::instance()->send(QString::number(this->id()) + QLatin1String(";") + (this->isConnectionStable() ? QLatin1String("true") : QLatin1String("false")) + QLatin1String(";") + this->configPath + QLatin1String(";") + this->configName, QLatin1String("STATUS"));
}

void OpenVpn::showProcessError(QProcess::ProcessError error)
{
    this->connectionStable = false;
    QString errMessage;
    switch (error) {
        case QProcess::FailedToStart:
            errMessage = QString ("The process failed to start. Either the invoked program is missing, or you may have insufficient permissions to invoke the program.");
            break;
        case QProcess::Crashed:
            errMessage = QString ("The process crashed some time after starting successfully.");
            break;
        case QProcess::Timedout:
            errMessage = QString ("The last waitFor...() function timed out. The state of QProcess is unchanged, and you can try calling waitFor...() again.");
            break;
        case QProcess::WriteError:
            errMessage = QString ("An error occurred when attempting to write to the process. For example, the process may not be running, or it may have closed its input channel.");
            break;
        case QProcess::ReadError:
            errMessage = QString ("An error occurred when attempting to read from the process. For example, the process may not be running.");
            break;
        case QProcess::UnknownError:
            errMessage = QString ("An unknown error occurred. This is the default return value of error().");
            break;
        default:
            errMessage = QString ("No valid error code!");
            break;
    }

    Debug::error(QLatin1String("OpenVpn: ") + errMessage);
    if (!this->onDisconnect) {
        SrvCLI::instance()->send(QString::number(this->id()) + QLatin1String(";") + errMessage, QLatin1String("ERROR"));
        // Wenn der Process nicht gestartet werden kann, gibt es auch kein finished Signal
        if (error == QProcess::FailedToStart) {
            // Element aus der Liste l�schen
            emit removeItemFromList(this->id());
        }
    }
}

void OpenVpn::disconnectVpn()
{

    Debug::log(QLatin1String("Disconnect OpenVPN.  Id: ") + QString::number(this->id()));

    // OpenVpn beenden
    QObject::disconnect(&this->proc, 0, 0, 0);

    this->onDisconnect = true;

    this->configName = "";
    this->configPath = "";
    this->connectionStable = false;
    // Per Winapi killen
    HANDLE exitEvent;
    QString eventName (QLatin1String("openvpngui_exit_") + QString::number(this->id()));
    exitEvent = CreateEvent(NULL, TRUE, FALSE, (LPCTSTR)eventName.utf16());
    if (!exitEvent) {
        return;
    }
    SetEvent(exitEvent);
    CloseHandle (exitEvent);

    if(!this->proc.waitForFinished(5000)) {
        Debug::log(QLatin1String("OpenVpn do kill"));
        this->proc.kill();
        this->proc.waitForFinished(5000);
    }

    //
    emit removeItemFromList(this->id());

    SrvCLI::instance()->send(QString::number(this->id()), QLatin1String("DISCONNECTED"));

}

void OpenVpn::readProcessData()
{
    if (this->onDisconnect) {
        return;
    }

    Debug::log (QLatin1String("Read process data"));

    QByteArray line;    
    bool showLine = true;
    line = this->proc.readAllStandardError();
    if (line == "")
        line = this->proc.readAllStandardOutput();
    if (line != "") {
        Debug::log (line);
        //Enter Private Key Password:
        // 0 - Username
        // 1 - Pwd
        // 2 - OTP
        // 3 - PKCS12
        // 4 - Private Key f�r Crypted User Data
        QString pkkey (line);
        if (pkkey.contains("Enter Private Key Password:", Qt::CaseInsensitive)) {
            Debug::log(QLatin1String("Wait for private key"));
            SrvCLI::instance()->send(QString("%1;3").arg(this->id()), QLatin1String("INPUT"));
            showLine = false;
        } else if (line == "Enter Auth Username:" || line == "Enter Challenge Username:" ) { // Enter Username?
            Debug::log(QLatin1String("Wait for User"));
            SrvCLI::instance()->send(QString("%1;0").arg(this->id()), QLatin1String("INPUT"));
            showLine = false;
        } else if (line == "Enter Auth Password:") { // Enter Password
            Debug::log(QLatin1String("Wait for pass"));
            SrvCLI::instance()->send(QString("%1;1").arg(this->id()), QLatin1String("INPUT"));
            showLine = false;
        } else if (line == "Enter Challenge Password:") { // Challenge Key
            Debug::log(QLatin1String("Wait for challenge"));
            SrvCLI::instance()->send(QString("%1;2").arg(this->id()), QLatin1String("INPUT"));
            showLine = false;
        } else if (line.contains("Enter HTTP Proxy Username:")) {
            Debug::log(QLatin1String("Wait for http user"));
            SrvCLI::instance()->send(QString("%1;5").arg(this->id()), QLatin1String("INPUT"));
            showLine = false;
        } else if (line.contains("Enter HTTP Proxy Password:")) {
            Debug::log(QLatin1String("Wait for http pass"));
            SrvCLI::instance()->send(QString("%1;6").arg(this->id()), QLatin1String("INPUT"));
            showLine = false;
        }
        // QByteArray in QString
        QString lineOut(line);
        // Verbindung da?
        if (lineOut.contains("Notified TAP-Win32 driver to set a DHCP IP", Qt::CaseInsensitive)) {
            // IP Suchen und speichern
            QString connIP = lineOut.mid(lineOut.indexOf("Notified TAP-Win32 driver to set a DHCP IP")+54,15);
            // IP durchlaufen und / und spaces entfernen
            int indexOfChar = 0;
            indexOfChar = connIP.indexOf("/",0);
            if (indexOfChar != 0) {
                // Maskeabschneiden
                connIP = connIP.left(indexOfChar);
            } else {
                // Lerrzeichen da?
                indexOfChar = connIP.indexOf(" ",0);
                if (indexOfChar != 0) {
                    // Leerzeichen abschneiden
                    connIP = connIP.left(indexOfChar);
                }
            }

            // Meldung zeigen connected
            this->connectionIP = connIP;            
            // Status speichern und Tray Icon setzen
            this->connectionStable = true;
            // Line speichern
            this->connectionLineBak = lineOut;
        }
        
        // Fehler abfangen
        bool errorOcurred = false;
        bool _tlsHandshakeFailed (false);
        QString errorMessage;
        //"All TAP-Win32 adapters on this system are currently in use"
        if (lineOut.contains("All TAP-Win32 adapters on this system are currently in use", Qt::CaseInsensitive)) {
            SrvCLI::instance()->send(QLatin1String("NEEDED"), QLatin1String("TAPINSTALL"));
            errorMessage = QLatin1String ("All TAP-Win32 adapters on this system are currently in use");
            errorOcurred = true;
        } else if (lineOut.contains("There are no TAP-Win32 adapters on this system", Qt::CaseInsensitive)) {
            SrvCLI::instance()->send(QLatin1String("NEEDED"), QLatin1String("TAPINSTALL"));
            errorMessage = QLatin1String ("There are no TAP-Win32 adapters on this system");
            errorOcurred = true;
        } else if (lineOut.contains("TLS Error: Need PEM pass phrase for private key", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("TLS Error: Need PEM pass phrase for private key");
            errorOcurred = true;
        } else if (lineOut.contains("TLS Error: TLS handshake failed", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("TLS error! See log for details");
            errorOcurred = true;
            _tlsHandshakeFailed = true;
        } else if (lineOut.contains("RESOLVE: Cannot resolve host address:", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("Connection error! See log for details");
            errorOcurred = true;
            _tlsHandshakeFailed = true;
        } else if (lineOut.contains("EVP_DecryptFinal:bad decrypt", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("EVP_DecryptFinal:bad decrypt");
            errorOcurred = true;
        } else if (lineOut.contains("PKCS12_parse:mac verify failure", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("PKCS12_parse:mac verify failure");
            errorOcurred = true;
        } else if (lineOut.contains("Received AUTH_FAILED control message", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("Received AUTH_FAILED control message");
            errorOcurred = true;
        } else if (lineOut.contains("Auth username is empty", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("Auth username is empty");
            errorOcurred = true;
        } else if (lineOut.contains("error=certificate has expired", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("error=certificate has expired");
            errorOcurred = true;
        } else if (lineOut.contains("error=certificate is not yet valid", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("error=certificate is not yet valid");
            errorOcurred = true;
        } else if (lineOut.contains("Proxy Authentication Required", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("Proxy Authentication Required");
            errorOcurred = true;
        } else if (lineOut.contains("Cannot load certificate file", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("Cannot load certificate file");
            errorOcurred = true;
        } else if (lineOut.contains("Exiting", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("Application Exiting!");
            errorOcurred = true;
        } else if (lineOut.contains("Use --help for more information.", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("OpenVPN parameter error!\nSee log for details");
            errorOcurred = true;
        } else if (lineOut.contains("will try again in 5 seconds", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("OpenVPN connection error!\nSee log for details");
        } else if (lineOut.contains("No Route to Host", Qt::CaseInsensitive)) {
            errorMessage = QLatin1String ("No Route to Host!\nSee log for details");
            SrvCLI::instance()->send(QString("%1;%2")
                             .arg(this->id())
                             .arg(errorMessage), QLatin1String("ERROR"));

            this->onDisconnect = true;
            this->connectionStable = false;
            this->proc.kill();
        }

        if (lineOut.contains("Proxy requires authentication") && !lineOut.contains("process exiting")) {
            // Kein Fehler
            errorOcurred = false;
            errorMessage = "";
        }        

        // Fehler durch normalen Disconnect verhindern
        if (lineOut.contains("SIGTERM[hard,] received, process exiting", Qt::CaseInsensitive)) {
            errorOcurred = false;
        }

        if (errorOcurred) {
            this->connectionStable = false;
            SrvCLI::instance()->send(QString("%1;%2")
                             .arg(this->id())
                             .arg(errorMessage), QLatin1String("ERROR"));
        }

        // Die Ausgabe von OpenVPN immer senden, sofern es keine Useraufforderungen sind
        if (showLine) {
            SrvCLI::instance()->send(QString("%1;%2")
                             .arg(this->id())
                             .arg(QString(line)), QLatin1String("LOG"));
        }


        if (_tlsHandshakeFailed) {
            this->disconnectVpn();
        }

        if (lineOut.contains("Restart pause", Qt::CaseInsensitive)) {
            // Bei Restart Pause befinden wir uns immer noch im Connect auch wenn vorher ein Fehler aufgetreten ist!
            // Status speichern und Tray Icon setzen
            if (!_tlsHandshakeFailed) {
                SrvCLI::instance()->send(QString("%1").arg(this->id()), QLatin1String("RESTART"));
            } else {
                SrvCLI::instance()->send(QString ("%1;%2")
                                 .arg(this->id())
                                 .arg(QLatin1String("Timeout[Maybe your cetificates are not valid. Please check if it is revoked], restart pause will be ignored! Shuting down OpenVPN ...")), QLatin1String("LOG"));
            }
        }

        //Initialization Sequence Completed
        if (lineOut.contains("Initialization Sequence Completed", Qt::CaseInsensitive)) {
            this->connectionStable = true;
            SrvCLI::instance()->send(QString ("%1;%2")
                             .arg(this->id())
                             .arg(connectionLineBak), QLatin1String("LOG"));

            // Nun die Ip senden, damit kann der Client auf gr�n gehen
            SrvCLI::instance()->send(QString("%1;%2").arg(this->id()).arg(this->connectionIP), QLatin1String("RECEIVEDIP"));
        }

    }
}

void OpenVpn::errorSocket(QAbstractSocket::SocketError err)
{
    Q_UNUSED(err)
}

void OpenVpn::setUsername(const QString &username)
{
    Debug::log(QLatin1String("Set username: ") + username);
    if (this->proc.isWritable()) {
        QByteArray ba;
        ba.append(username + "\n");
        this->proc.write(ba);
    } else {
        Debug::error(QLatin1String("Process is not writable"));
    }
}


void OpenVpn::setPassword(const QString &pwd)
{
    Debug::log(QLatin1String("Set password: ") + pwd);
    if (this->proc.isWritable()) {
        QByteArray ba;
        ba.append(pwd + "\n");
        this->proc.write(ba);
    } else {
        Debug::error(QLatin1String("Process is not writable"));
    }
}

void OpenVpn::setPrivateKey(const QString &key)
{
    Debug::log(QLatin1String("Set private key: ") + key);
    if (this->proc.isWritable()) {
        QByteArray ba;
        ba.append(key + "\n");
        this->proc.write(ba);
    } else {
        Debug::error(QLatin1String("Process is not writable"));
    }
}

void OpenVpn::setChallengeKey(const QString &key)
{
    Debug::log(QLatin1String("Set challenge key: ") + key);
    if (this->proc.isWritable()) {
        QByteArray ba;
        ba.append(key + "\n");
        this->proc.write(ba);
    } else {
        Debug::error(QLatin1String("Process is not writable"));
    }
}

int OpenVpn::id() const
{
    return this->_id;
}

void OpenVpn::setId(const int &ident)
{
    this->_id = ident;
}

void OpenVpn::setConfigName(const QString &name)
{
    this->configName = name;
}

void OpenVpn::setConfigPath(const QString &path)
{
    this->configPath = path;
}

void OpenVpn::setUseInteract(const QString &interact)
{
    this->useNoInteract = (interact == QLatin1String("1") ? true : false);
}
