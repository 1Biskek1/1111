#ifndef DASHCOINWALLET_H
#define DASHCOINWALLET_H

#include <QMainWindow>
#include <QProcess>
#include <QNetworkReply>
#include <QLabel>

namespace Ui {
class DashcoinWallet;
}

class DashcoinWallet : public QMainWindow
{
    Q_OBJECT

public:
    explicit DashcoinWallet(QWidget *parent = 0);
    ~DashcoinWallet();

protected:
    void closeEvent(QCloseEvent *event);

private slots:
    void replyFinished(QNetworkReply *reply);
    void daemonStarted();
    void daemonFinished();
    void loadBlockHeight();
    void loadWalletData();
    void killWalletGenerate();
    void on_openWallet_btn_clicked();
    void walletStarted();
    void walletFinished();
    void balanceReply(QNetworkReply *reply);

    void on_send_btn_clicked();

private:
    Ui::DashcoinWallet *ui;
    void loadFile();
    void loadBalance();
    void hideWallet();
    void showWallet();
    void showAllWallet();
    QString fixBalance(QString str);
    QProcess *daemon;
    QProcess *wallet;
    QProcess *walletGenerate;
    QString pass;
    QLabel *syncLabel;
    QLabel *messageLabel;
    QNetworkAccessManager *balanceLoad;
    bool tryingToClose;
    bool daemonRunning;
    bool walletRunning;
    bool synced;
    bool showingWallet;
};

#endif // DASHCOINWALLET_H
