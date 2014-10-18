#include "dashcoinwallet.h"
#include "ui_dashcoinwallet.h"
#include <QDir>
#include <QDebug>
#include <QNetworkRequest>
#include <QUrl>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QTimer>
#include <QPushButton>
#include <QVboxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCloseEvent>
#include <QJsonArray>
#include <QRegularExpression>

DashcoinWallet::DashcoinWallet(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::DashcoinWallet)
{
    ui->setupUi(this);
    tryingToClose = false;
    daemonRunning = false;
    walletRunning = false;
    synced = false;
    hideWallet();
    ui->sendconfirm_btn->hide();
    syncLabel = new QLabel(this);
    messageLabel = new QLabel(this);
    syncLabel->setContentsMargins(9,0,9,0);
    messageLabel->setContentsMargins(9,0,9,0);
    ui->statusBar->addPermanentWidget(syncLabel);
    ui->statusBar->addPermanentWidget(messageLabel,1);
    loadFile();
}

DashcoinWallet::~DashcoinWallet()
{
    delete ui;
}

void DashcoinWallet::loadFile()
{
    daemon = new QProcess(this);
    connect(daemon, SIGNAL(started()),this, SLOT(daemonStarted()));
    connect(daemon, SIGNAL(finished(int , QProcess::ExitStatus)),this, SLOT(daemonFinished()));
    daemon->start(QDir::currentPath ()+"/dashcoind", QStringList() << "");
}

void DashcoinWallet::daemonStarted(){
    daemonRunning = true;
    syncLabel->setText("Starting daemon");
    QTimer::singleShot(3000, this, SLOT(loadBlockHeight()));
    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(loadBlockHeight()));
    timer->start(10000);
}

void DashcoinWallet::daemonFinished()
{
    daemonRunning = false;
    qDebug() << "daemon finished";
    if(tryingToClose == true){
        qDebug() << "quitting";
        qApp->quit();
    }
}

void DashcoinWallet::loadBlockHeight(){
    if(daemonRunning){
        QNetworkAccessManager *manager = new QNetworkAccessManager(this);
        connect(manager, SIGNAL(finished(QNetworkReply*)),this, SLOT(replyFinished(QNetworkReply*)));
        QString dataStr = "{\"jsonrpc\": \"2.0\", \"method\":\"getblockcount\", \"id\": \"test\"}";
        QJsonDocument jsonData = QJsonDocument::fromJson(dataStr.toUtf8());
        QByteArray data = jsonData.toJson();
        QNetworkRequest request = QNetworkRequest(QUrl("http://127.0.0.1:29081/json_rpc"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
        manager->post(request, data);
    }

}

void DashcoinWallet::replyFinished(QNetworkReply *reply)
{
    if(tryingToClose == false){
        QByteArray bytes = reply->readAll();
        QString str = QString::fromUtf8(bytes.data(), bytes.size());
        QJsonDocument jsonResponse = QJsonDocument::fromJson(str.toUtf8());
        QJsonObject jsonObj = jsonResponse.object()["result"].toObject();
        QString status = jsonObj["status"].toString();
        QString height = QString::number(jsonObj["count"].toInt());
        if(status == "OK"){
            if(synced == false){
                messageLabel->setText("");
                synced = true;
            }
            syncLabel->setText("Synced with network. Height: "+height);
        }else{
            syncLabel->setText("Syncing with network");
        }
    }
}

void DashcoinWallet::on_openWallet_btn_clicked()
{
    //they clicked the open wallet button
    if(synced == true){
        pass = ui->password_txt->text();
        ui->password_txt->setText("");
        wallet = new QProcess(this);
        connect(wallet, SIGNAL(started()),this, SLOT(walletStarted()));
        connect(wallet, SIGNAL(finished(int , QProcess::ExitStatus)),this, SLOT(walletFinished()));
        QFile walletFile(QDir::currentPath ()+"/wallet.bin");
        if(!walletFile.exists()){
            messageLabel->setText("No wallet found. Generating new wallet...");
            walletGenerate = new QProcess(this);
            walletGenerate->start(QDir::currentPath ()+"/simplewallet", QStringList() << "--generate-new-wallet" << "wallet.bin" << "--password" << pass);
            QTimer::singleShot(2000, this, SLOT(killWalletGenerate()));
        }else{
            messageLabel->setText("Opening wallet...");
            wallet->start(QDir::currentPath ()+"/simplewallet", QStringList() << "--wallet-file=wallet.bin" << "--pass="+pass << "--rpc-bind-port=49253");
        }
    }else{
        messageLabel->setText("Please wait for the sync to complete");
    }
}

void DashcoinWallet::killWalletGenerate()
{
    messageLabel->setText("Generated wallet file wallet.bin. Now starting wallet server on port 49253.");
    walletGenerate->kill();
    wallet->start(QDir::currentPath ()+"/simplewallet", QStringList() << "--wallet-file=wallet.bin" << "--pass="+pass << "--rpc-bind-port=49253");
    pass = "";
}

void DashcoinWallet::walletStarted()
{
    walletRunning = true;
    showWallet();
}

void DashcoinWallet::walletFinished()
{
    walletRunning = false;
    hideWallet();
    showingWallet = false;
    disconnect(balanceLoad, SIGNAL(finished(QNetworkReply*)),this, SLOT(balanceReply(QNetworkReply*)));
    disconnect(transactionsLoad, SIGNAL(finished(QNetworkReply*)),this, SLOT(transactionsReply(QNetworkReply*)));
    messageLabel->setText("Wallet disconnected");
}

void DashcoinWallet::closeEvent(QCloseEvent *event)
 {
    qDebug() << "exiting now event";
    if(walletRunning == true){
        wallet->kill();
    }
    if(daemonRunning == true){
        if(tryingToClose == false){
            daemon->write("exit\n");
            tryingToClose = true;
            syncLabel->setText("Saving blockchain...");
        }
        event->ignore();
    }else{
        event->accept();
    }
 }

void DashcoinWallet::loadBalance()
{
    balanceLoad = new QNetworkAccessManager(this);
    connect(balanceLoad, SIGNAL(finished(QNetworkReply*)),this, SLOT(balanceReply(QNetworkReply*)));
    QString dataStr = "{\"jsonrpc\": \"2.0\", \"method\":\"getbalance\", \"id\": \"test\"}";
    QJsonDocument jsonData = QJsonDocument::fromJson(dataStr.toUtf8());
    QByteArray data = jsonData.toJson();
    QNetworkRequest request = QNetworkRequest(QUrl("http://127.0.0.1:49253/json_rpc"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    balanceLoad->post(request, data);

}

void DashcoinWallet::balanceReply(QNetworkReply *reply)
{
    QByteArray bytes = reply->readAll();
    QString str = QString::fromUtf8(bytes.data(), bytes.size()).simplified();
    str.replace(QRegularExpression("(?<=:)\\s()(?=\\d)"),"\"");
    str.replace(QRegularExpression("(?<=\\d)(?=[, ])"),"\"");
    QJsonDocument jsonResponse = QJsonDocument::fromJson(str.toUtf8());
    QJsonObject jsonObj = jsonResponse.object()["result"].toObject();
    QString balance = jsonObj["balance"].toString();
    QString unlocked_balance = jsonObj["unlocked_balance"].toString();
    balance = fixBalance(balance);
    unlocked_balance = fixBalance(unlocked_balance);
    ui->balance_txt->setText(balance+" DSH");
    ui->balance_unlocked_txt->setText(unlocked_balance+" DSH");
    if(showingWallet == true){
        showAllWallet();
        showingWallet = false;
    }
}

QString DashcoinWallet::fixBalance(QString str)
{
    if(str == "0"){
        return "0";
    }
    QString left = "";
    QString right = "";
    if(str.length() <= 8){
        str = QString((8-str.length()),'0')+str;
        left = "0";
    }else{
        left = str.left(str.length()-8);
    }
    right = str.right(8);
    right.remove(QRegularExpression("0+$"));
    if(right == ""){
        right = "0";
    }
    QString result = left+"."+right;
    return result;
}

void DashcoinWallet::hideWallet()
{
    ui->passwordBox->show();
    ui->balance->hide();
    ui->sendForm->hide();
    ui->in_address->hide();
    ui->transactions_table->hide();
}

void DashcoinWallet::showWallet()
{
    ui->passwordBox->hide();
    loadWalletData();
    showingWallet = true;
}

void DashcoinWallet::loadWalletData(){
    if(walletRunning == true){
        loadBalance();
        loadAddress();
        loadTransactions();
    }
}

void DashcoinWallet::showAllWallet()
{
    messageLabel->setText("Wallet connected");
    qDebug() << "Started wallet timer";
    ui->balance->show();
    ui->sendForm->show();
    ui->in_address->show();
    ui->transactions_table->show();
    QTimer *walletTimer = new QTimer(this);
    connect(walletTimer, SIGNAL(timeout()), this, SLOT(loadWalletData()));
    walletTimer->start(10000);
}

void DashcoinWallet::loadAddress()
{
    QFile addressFile(QDir::currentPath ()+"/wallet.bin.address.txt");
    if(!addressFile.exists() || !addressFile.open(QIODevice::ReadOnly)){
        ui->in_address_txt->setText("Cannot load address");
    }else{
        QTextStream in(&addressFile);
        ui->in_address_txt->setText(in.readLine());
    }

}

void DashcoinWallet::loadTransactions()
{
    transactionsLoad = new QNetworkAccessManager(this);
    connect(transactionsLoad, SIGNAL(finished(QNetworkReply*)),this, SLOT(transactionsReply(QNetworkReply*)));
    QString dataStr = "{\"jsonrpc\": \"2.0\", \"method\":\"get_transfers\", \"id\": \"test\"}";
    QJsonDocument jsonData = QJsonDocument::fromJson(dataStr.toUtf8());
    QByteArray data = jsonData.toJson();
    QNetworkRequest request = QNetworkRequest(QUrl("http://127.0.0.1:49253/json_rpc"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    transactionsLoad->post(request, data);
}

void DashcoinWallet::transactionsReply(QNetworkReply *reply)
{
    QByteArray bytes = reply->readAll();
    QString str = QString::fromUtf8(bytes.data(), bytes.size()).simplified();
    str.replace(QRegularExpression("(?<=:)\\s()(?=\\d)"),"\"");
    str.replace(QRegularExpression("(?<=\\d)(?=[, ])"),"\"");
    QJsonDocument jsonResponse = QJsonDocument::fromJson(str.toUtf8());
    QJsonArray jsonObj = jsonResponse.object()["result"].toObject()["transfers"].toArray();
    ui->transactions_table->setRowCount(jsonObj.size());
    int col = 0;
    for(int i=jsonObj.size()-1;i>=0;i--){
        QString address_str = jsonObj.at(i).toObject()["address"].toString();
        QTableWidgetItem *amount =  new QTableWidgetItem(fixBalance(jsonObj.at(i).toObject()["amount"].toString())+" DSH");
        QTableWidgetItem *address = new QTableWidgetItem(address_str);
        QTableWidgetItem *fee = new QTableWidgetItem(fixBalance(jsonObj.at(i).toObject()["fee"].toString())+ " DSH");
        QTableWidgetItem *txhash =  new QTableWidgetItem(jsonObj.at(i).toObject()["transactionHash"].toString());
        QTableWidgetItem *date = new QTableWidgetItem(QDateTime::fromTime_t(jsonObj.at(i).toObject()["time"].toString().toInt()).toUTC().toString("MMM d yyyy"));
        QString type_str = "Send";
        if(address_str == ""){
            type_str = "Receive";
        }
        QTableWidgetItem *type = new QTableWidgetItem(type_str);
        col = jsonObj.size()-i-1;
        ui->transactions_table->setItem(col,0,date);
        ui->transactions_table->setItem(col,1,type);
        ui->transactions_table->setItem(col,2,amount);
        ui->transactions_table->setItem(col, 3, fee);
        ui->transactions_table->setItem(col,4,txhash);
        ui->transactions_table->setItem(col,5,address);
    }
}

void DashcoinWallet::on_send_btn_clicked()
{
    ui->send_btn->setDisabled(true);
    ui->address_txt->setDisabled(true);
    ui->paymentid_txt->setDisabled(true);
    ui->amount_txt->setDisabled(true);
    ui->fee_txt->setDisabled(true);
    ui->mixin_txt->setDisabled(true);
    ui->sendconfirm_btn->show();
}

void DashcoinWallet::on_sendconfirm_btn_clicked()
{
    QString address = ui->address_txt->text();
    QString paymentid = ui->paymentid_txt->text();
    QString amount = fixamount(ui->amount_txt->cleanText());
    QString fee = fixamount(ui->fee_txt->cleanText());
    QString mixin = ui->mixin_txt->cleanText();
    qDebug() << "Amount: " << ui->amount_txt->cleanText();
    qDebug() << "Fixed amount: " << fixamount(ui->amount_txt->cleanText());
    ui->sendconfirm_btn->setText("Sending...");
    sendLoad = new QNetworkAccessManager(this);
    connect(sendLoad, SIGNAL(finished(QNetworkReply*)),this, SLOT(sendReply(QNetworkReply*)));
    QString dataStr = "{ \"jsonrpc\":\"2.0\", \"method\":\"transfer\", \"params\":{ \"destinations\":[ { \"amount\":"+amount+", \"address\":\""+address+"\" } ], \"payment_id\":\""+paymentid+"\", \"fee\":"+fee+", \"mixin\":"+mixin+", \"unlock_time\":0 } }";
    QJsonDocument jsonData = QJsonDocument::fromJson(dataStr.toUtf8());
    QByteArray data = jsonData.toJson();
    QNetworkRequest request = QNetworkRequest(QUrl("http://127.0.0.1:49253/json_rpc"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    qDebug() << "Request: " << data;
    sendLoad->post(request, data);
}

void DashcoinWallet::sendReply(QNetworkReply *reply)
{
    QByteArray bytes = reply->readAll();
    QString str = QString::fromUtf8(bytes.data(), bytes.size()).simplified();
    QJsonDocument jsonResponse = QJsonDocument::fromJson(str.toUtf8());
    if(jsonResponse.object().contains("error")){
        QString error = jsonResponse.object()["error"].toObject()["message"].toString();
        messageLabel->setText("Error sending transaction: "+error);
    }else{
        QString txhash = jsonResponse.object()["result"].toObject()["tx_hash"].toString();
        messageLabel->setText("Successfully sent transaction "+txhash);
    }

    qDebug() << str;

    ui->sendconfirm_btn->hide();
    ui->sendconfirm_btn->setText("Confirm");
    ui->send_btn->setDisabled(false);
    ui->address_txt->setDisabled(false);
    ui->paymentid_txt->setDisabled(false);
    ui->amount_txt->setDisabled(false);
    ui->fee_txt->setDisabled(false);
    ui->mixin_txt->setDisabled(false);
    ui->address_txt->clear();
    ui->paymentid_txt->clear();
    ui->amount_txt->setValue(0);
    ui->fee_txt->setValue(15);

}

QString DashcoinWallet::fixamount(QString str)
{
    if(str.contains(".")){
        QString right = str.mid(str.indexOf(".")+1);
        QString left = str.mid(0,str.length()-right.length()-1);
        QString moreZeros = "";
        if(right.length() < 8){
            moreZeros = QString(8-right.length(),'0');
        }
        right = right+moreZeros;
        right = right.mid(0,8);
        return left+right;
    }else{
        return str+"00000000";
    }
}
