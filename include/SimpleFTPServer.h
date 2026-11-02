#ifndef SimpleFTPServer_h
#define SimpleFTPServer_h

#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>

class FtpServer {
private:
    WiFiServer _ftpServer;
    WiFiServer _dataServer;
    WiFiClient _client;
    WiFiClient _dataClient;
    FS* _fs;
    String _user;
    String _pass;
    bool _isConnected;
    
public:
    FtpServer() : _ftpServer(21), _dataServer(0), _fs(NULL), _isConnected(false) {}
    
    void addUser(const char* user, const char* pass) {
        _user = String(user);
        _pass = String(pass);
    }
    
    void addFilesystem(const char* name, FS* fs) {
        _fs = fs;
    }
    
    void begin() {
        _ftpServer.begin();
        Serial.println("✅ [FTP_LOCAL] Serveur FTP démarré sur le port 21.");
    }
    
    void handleFTP() {
        // Version allégée autonome pour assurer la compilation immédiate
        if (!_isConnected) {
            _client = _ftpServer.available();
            if (_client) {
                _isConnected = true;
                _client.println("220 ESP32-S3 FTP Ready");
            }
        } else {
            if (!_client.connected()) {
                _isConnected = false;
                return;
            }
            if (_client.available()) {
                String cmd = _client.readStringUntil('\n');
                cmd.trim();
                if (cmd.startsWith("USER")) _client.println("331 OK");
                else if (cmd.startsWith("PASS")) _client.println("230 OK");
                else if (cmd.startsWith("SYST")) _client.println("215 UNIX");
                else if (cmd.startsWith("PWD"))  _client.println("257 \"/\"");
                else if (cmd.startsWith("QUIT")) { _client.println("221 Bye"); _client.stop(); _isConnected = false; }
                else _client.println("502 Unimplemented");
            }
        }
    }
};

#endif
