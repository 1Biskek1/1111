#include "dashcoinwallet.h"
#include "ui_dashcoinwallet.h"
#include <QDir>
#include <QDebug>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QPushButton>
#include <QVboxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCloseEvent>
#include <QJsonArray>
#include <QRegularExpression>
#include <QMessageBox>
#include <QRegularExpressionValidator>
#include <QtCore/qmath.h>
#include <QtNetwork>
#include <QFileInfo>

DashcoinWallet::DashcoinWallet(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::DashcoinWallet)
{
    ui->setupUi(this);
    init_ui();
    init_wallet();
}

DashcoinWallet::~DashcoinWallet()
{
    delete ui;
}

/*
 * ==============================================
 *               Main Functions
 * ==============================================
 */

void DashcoinWallet::init_wallet()
{
    load_wallets();

    synced = false;
    tryingToClose = false;
    daemon_is_running = false;

    start_daemon();
}

void DashcoinWallet::start_daemon()
{
    syncLabel->setText("Starting daemon");

    QString daemon_path = "./dashcoind.exe";
    QFileInfo check_file(daemon_path);
    if (!check_file.exists() || !check_file.isFile()) {
        syncLabel->setText("Error: Please make sure dashcoind.exe is in the same directory as the wallet GUI");
        return;
    }

    daemon = new QProcess(this);

    connect(daemon, SIGNAL(started()),this, SLOT(daemon_started()));
    connect(daemon, SIGNAL(finished(int , QProcess::ExitStatus)), this, SLOT(daemon_finished()));

    daemon->start(daemon_path, QStringList() << "");
}

void DashcoinWallet::daemon_started()
{
    daemon_is_running = true;

    // Every 7 seconds, parse the daemon log
    parse_daemon_timer = new QTimer(this);
    connect(parse_daemon_timer, SIGNAL(timeout()), this, SLOT(parse_daemon_log()));
    parse_daemon_timer->start(7000);
}

void DashcoinWallet::daemon_finished()
{
    daemon_is_running = false;
    if(tryingToClose == true){
        qApp->quit();
    }
}

/*
 * ==============================================
 *               Display Functions
 * ==============================================
 */

void DashcoinWallet::init_ui()
{
    ui->panel_send_confirm->hide();
    show_wallet(false);

    // Add sync status and general message labels to the status bar
    syncLabel = new QLabel(this);
    messageLabel = new QLabel(this);
    syncLabel->setContentsMargins(9,0,9,0);
    messageLabel->setContentsMargins(9,0,9,0);
    ui->bar_status->addPermanentWidget(syncLabel);
    ui->bar_status->addPermanentWidget(messageLabel, 1);

    // Validate send address
    QRegularExpression rx1("[a-zA-Z0-9]*");
    QValidator *alphaNum = new QRegularExpressionValidator(rx1, this);
    ui->txt_send_address->setValidator(alphaNum);

    // Validate payment id
    QRegularExpression rx2("[a-fA-F0-9]*");
    QValidator *hexOnly = new QRegularExpressionValidator(rx2, this);
    ui->txt_send_paymentid->setValidator(hexOnly);
}

void DashcoinWallet::show_wallet(bool b)
{
    // If showing wallet, show these items
    ui->panel_generate->setHidden(!b);
    ui->panel_balance->setHidden(!b);

    // hide these items
    ui->panel_password->setHidden(b);
    ui->btn_close_wallet->setHidden(!b);

    // enable these items
    ui->tab_send->setEnabled(b);
    ui->tab_receive->setEnabled(b);
    ui->tab_transactions->setEnabled(b);

    // When hiding wallet, reset transactions and address
    if(!b){
        ui->table_transactions->setRowCount(0);
        ui->txt_receive_address->clear();
    }
}

/*
 * ==============================================
 *               Button Presses
 * ==============================================
 */

void DashcoinWallet::on_btn_open_clicked()
{
    if(!synced){
        messageLabel->setText("Please wait for the sync to complete");
        return;
    }

    QString wallet_path = "./simplewallet";
    QFileInfo check_file(wallet_path);
    if (!check_file.exists() || !check_file.isFile()) {
        syncLabel->setText("Error: Please make sure simplewallet.exe is in the same directory as the wallet GUI");
        return;
    }

    ui->btn_open->setDisabled(true);
    ui->btn_open->setText("Opening wallet...");
    current_wallet = ui->select_wallet->currentText();
    QString pass = ui->txt_password_open->text();
    ui->txt_password_open->setText("");

    wallet = new QProcess(this);
    connect(wallet, SIGNAL(started()),this, SLOT(wallet_started()));
    connect(wallet, SIGNAL(finished(int , QProcess::ExitStatus)),this, SLOT(wallet_finished()));

    messageLabel->setText("Opening wallet...");

    QStringList params;
    params << "--wallet-file=wallets/"+current_wallet+".bin" << "--password" << pass << "--rpc-bind-port=49253";
    wallet->start(wallet_path, params);
}

void DashcoinWallet::on_btn_generate_clicked()
{
    if(!synced){
        messageLabel->setText("Please wait for the sync to complete");
        return;
    }

    if(ui->panel_generate->isHidden()){
        QRegularExpression rx("[a-zA-Z0-9]*");
        QValidator *validator = new QRegularExpressionValidator(rx, this);
        ui->txt_name_generate->setValidator(validator);
        ui->btn_generate->setText("Generate wallet");
        ui->panel_generate->show();
        return;
    }

    if(ui->txt_name_generate->text() == ""){
        messageLabel->setText("Wallet name cannot be blank");
        return;
    }

    current_wallet = ui->txt_name_generate->text();
    QString pass1 = ui->txt_password_generate->text();
    QString pass2 = ui->txt_password_generate_confirm->text();
    if(pass1 != pass2){
        messageLabel->setText("Passwords do not match");
        return;
    }

    QFile file_wallet(QDir::currentPath ()+"/wallets/"+current_wallet+".bin.keys");
    if(file_wallet.exists()){
        messageLabel->setText("A wallet with the name "+current_wallet+" already exists");
        return;
    }

    // TODO: Generate wallets folder if doesn't exist

    messageLabel->setText("Generating wallet "+current_wallet+"...");
    wallet_generate = new QProcess(this);
    wallet_generate->start(QDir::currentPath ()+"/simplewallet", QStringList() << "--generate-new-wallet" << "wallets/"+current_wallet+".bin" << "--password" << pass1);
    QTimer::singleShot(2000, this, SLOT(done_generating()));

}

void DashcoinWallet::on_btn_send_clicked()
{
    bool b = true;
    ui->btn_send->setHidden(b);
    ui->txt_send_address->setDisabled(b);
    ui->txt_send_paymentid->setDisabled(b);
    ui->txt_send_amount->setDisabled(b);
    ui->txt_send_fee->setDisabled(b);
    ui->txt_send_mixin->setDisabled(b);
    ui->panel_send_confirm->setHidden(!b);
}

void DashcoinWallet::on_btn_send_confirm_clicked()
{
    if(ui->txt_send_amount->value() <= 0){
        messageLabel->setText("Invalid amount");
        return;
    }

    QString address = ui->txt_send_address->text();
    QString paymentid = ui->txt_send_paymentid->text();
    QString amount = fix_amount(ui->txt_send_amount->cleanText());
    QString fee = fix_amount(ui->txt_send_fee->cleanText());
    QString mixin = ui->txt_send_mixin->cleanText();

    ui->txt_send_address->clear();
    ui->txt_send_paymentid->clear();
    ui->txt_send_amount->setValue(0);
    ui->txt_send_fee->setValue(15);
    ui->txt_send_mixin->setValue(0);
    ui->btn_send_confirm->setText("Sending...");

    loader_send = new QNetworkAccessManager(this);
    connect(loader_send, SIGNAL(finished(QNetworkReply*)),this, SLOT(rpcReply(QNetworkReply*)));

    QString dataStr = "{ \"jsonrpc\":\"2.0\", \"method\":\"transfer\", \"id\":\"send\", \"params\":{ \"destinations\":[ { \"amount\":"+amount+", \"address\":\""+address+"\" } ], \"payment_id\":\""+paymentid+"\", \"fee\":"+fee+", \"mixin\":"+mixin+", \"unlock_time\":0 } }";
    QJsonDocument jsonData = QJsonDocument::fromJson(dataStr.toUtf8());
    QByteArray data = jsonData.toJson();

    QNetworkRequest request = QNetworkRequest(QUrl("http://127.0.0.1:49253/json_rpc"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");

    loader_send->post(request, data);

    qDebug() << "Sent send request " << dataStr ;
}

void DashcoinWallet::on_btn_send_cancel_clicked()
{
    bool b = false;

    ui->btn_send->setHidden(b);

    ui->txt_send_address->setDisabled(b);
    ui->txt_send_paymentid->setDisabled(b);
    ui->txt_send_amount->setDisabled(b);
    ui->txt_send_fee->setDisabled(b);
    ui->txt_send_mixin->setDisabled(b);

    ui->panel_send_confirm->setHidden(!b);
}

void DashcoinWallet::on_btn_close_wallet_clicked()
{
    if(wallet_is_running){
        wallet->kill();
    }
}

/*
 * ==============================================
 *               Data Population
 * ==============================================
 */

void DashcoinWallet::load_wallets()
{
    ui->select_wallet->clear();

    QStringList filter_name;
    filter_name << "*.bin.wallet";
    filter_name << "*.bin.keys";
    QDir directory_wallets(QDir::currentPath ()+"/wallets");
    QStringList list_wallets = directory_wallets.entryList(filter_name);

    for(int i=0;i<list_wallets.length();i++){
        QString cur = list_wallets[i];
        ui->select_wallet->addItem(cur.mid(0,cur.lastIndexOf(".bin")), cur);
    }
}

/*
 * ==============================================
 *                    Slots
 * ==============================================
 */

void DashcoinWallet::done_generating(){
    wallet_generate->kill();
    load_wallets();
    messageLabel->setText("Generated wallet "+current_wallet);
}

void DashcoinWallet::closeEvent(QCloseEvent *event)
 {
    /*
    if(walletRunning == true){
        wallet->kill();
    }*/
    if(daemon_is_running){
        daemon->write("exit\n");
        if(tryingToClose == false){
            tryingToClose = true;
            syncLabel->setText("Saving blockchain...");
        }else{
            QMessageBox msgBox;
            msgBox.setText("The blockchain is not done saving.");
            msgBox.setInformativeText("Would you like to force quit?");
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Cancel);
            int ret = msgBox.exec();
            if(ret == QMessageBox::Yes){
                event->accept();
                return;
            }
        }
        event->ignore();
    }else{
        event->accept();
    }
 }

void DashcoinWallet::wallet_started()
{
    messageLabel->setText("Opening wallet...");
    opening_wallet = true;
    wallet_is_running = true;

    load_wallet_data();
    QTimer *wallet_timer = new QTimer(this);
    connect(wallet_timer, SIGNAL(timeout()), this, SLOT(load_wallet_data()));
    wallet_timer->start(10000);
}

void DashcoinWallet::wallet_finished()
{
    disconnect(loader_balance, SIGNAL(finished(QNetworkReply*)),this, SLOT(rpcReply(QNetworkReply*)));
    disconnect(loader_transactions, SIGNAL(finished(QNetworkReply*)),this, SLOT(rpcReply(QNetworkReply*)));
    show_wallet(false);
    messageLabel->setText("Wallet disconnected");
    wallet_is_running = false;
    ui->btn_open->setDisabled(false);
    ui->btn_open->setText("Open wallet");
}

void DashcoinWallet::load_wallet_data(){
    if(!wallet_is_running){
        return;
    }

    load_balance();
    load_address();
    load_transactions();
}

void DashcoinWallet::rpcReply(QNetworkReply *reply)
{
    if(!wallet_is_running){
        return;
    }
    QByteArray bytes = reply->readAll();
    QString str = QString::fromUtf8(bytes.data(), bytes.size()).simplified();
    if(!str.contains("error")){
        str.replace(QRegularExpression("(?<=:)\\s()(?=\\d)"),"\"");
        str.replace(QRegularExpression("(?<=\\d)(?=[, ])"),"\"");
    }
    QJsonDocument jsonResponse = QJsonDocument::fromJson(str.toUtf8());
    QJsonObject jsonObj;
    if(jsonResponse.object().contains("error")){
        jsonObj = jsonResponse.object()["error"].toObject();
    }else{
        jsonObj = jsonResponse.object()["result"].toObject();
    }
    QString id = jsonResponse.object()["id"].toString();
    if(str == ""){
        messageLabel->setText("Syncing wallet "+current_wallet);
        return;
    }
    if(opening_wallet == true){
        opening_wallet = false;
        messageLabel->setText("Opened wallet "+current_wallet);
        ui->btn_open->setDisabled(false);
        ui->btn_open->setText("Open wallet");
        show_wallet(true);
    }
    if(id == "balance"){
        QString balance = jsonObj["balance"].toString();
        QString unlocked_balance = jsonObj["unlocked_balance"].toString();
        balance = fix_balance(balance);
        unlocked_balance = fix_balance(unlocked_balance);
        ui->txt_balance->setText(balance+" DSH");
        ui->txt_balance_unlocked->setText(unlocked_balance+" DSH");
        return;
    }
    if(id == "transactions"){
        QJsonArray jsonArray = jsonObj["transfers"].toArray();
        ui->table_transactions->setRowCount(jsonArray.size());
        int col = 0;
        for(int i=jsonArray.size()-1;i>=0;i--){
            QString address_str = jsonArray.at(i).toObject()["address"].toString();
            QTableWidgetItem *amount =  new QTableWidgetItem(fix_balance(jsonArray.at(i).toObject()["amount"].toString())+" DSH");
            QTableWidgetItem *address = new QTableWidgetItem(address_str);
            QTableWidgetItem *fee = new QTableWidgetItem(fix_balance(jsonArray.at(i).toObject()["fee"].toString())+ " DSH");
            QTableWidgetItem *txhash =  new QTableWidgetItem(jsonArray.at(i).toObject()["transactionHash"].toString());
            QTableWidgetItem *date = new QTableWidgetItem(QDateTime::fromTime_t(jsonArray.at(i).toObject()["time"].toString().toInt()).toUTC().toString("MMM d yyyy hh:mm:ss"));
            QString type_str = "Send";
            if(address_str == ""){
                type_str = "Receive";
            }
            QTableWidgetItem *type = new QTableWidgetItem(type_str);
            col = jsonArray.size()-i-1;
            ui->table_transactions->setItem(col,0,date);
            ui->table_transactions->setItem(col,1,type);
            ui->table_transactions->setItem(col,2,amount);
            ui->table_transactions->setItem(col, 3, fee);
            ui->table_transactions->setItem(col,4,txhash);
            ui->table_transactions->setItem(col,5,address);
        }
        return;
    }
    if(id == "send"){
        if(jsonResponse.object().contains("error")){
            QString error = jsonObj["message"].toString();
            messageLabel->setText("Error sending transaction: "+error);
        }else{
            QString txhash = jsonResponse.object()["result"].toObject()["tx_hash"].toString();
            messageLabel->setText("Successfully sent transaction "+txhash);
        }
        bool b = false;
        ui->btn_send_confirm->setText("Confirm");
        ui->txt_send_address->setDisabled(b);
        ui->txt_send_paymentid->setDisabled(b);
        ui->txt_send_amount->setDisabled(b);
        ui->txt_send_fee->setDisabled(b);
        ui->txt_send_mixin->setDisabled(b);
        ui->panel_send_confirm->setHidden(!b);
        ui->btn_send->setHidden(b);
    }

}

/*
 * ==============================================
 *              General Functions
 * ==============================================
 */

QString DashcoinWallet::load_daemon_log()
{
    QFile file(QDir::currentPath ()+"/dashcoind.log");
    if(!file.exists() || !file.open(QIODevice::ReadOnly)){
        return "";
    }
    file.seek(file.size()-1);
    int count = 0;
    int lines = 30;
    while ( (count < lines) && (file.pos() > 0) ) {
        QString ch = file.read(1);
        file.seek(file.pos()-2);
        if (ch == "\n")
            count++;
    }
    QByteArray bytes = file.readAll();
    file.close();
    QString log_text =  QString::fromUtf8(bytes.data(), bytes.size()).simplified();
    log_text.replace(QRegularExpression(".*dashcoin v(?!.*dashcoin v)"),"");
    return log_text;
}

void DashcoinWallet::parse_daemon_log()
{
    if(!daemon_is_running || tryingToClose){
        qDebug() << "Daemon is closing, skipping log parse";
        return;
    }

    QString log = load_daemon_log();

    bool syncedFound = log.contains("SYNCHRONIZED OK") || log.contains("now synchronized with the network");
    bool syncedLast = log.lastIndexOf("SYNCHRONIZED OK") > log.lastIndexOf("days) behind");
    syncedLast = syncedLast || log.lastIndexOf("now synchronized with the network") > log.lastIndexOf("days) behind");

    if(syncedFound && syncedLast){
        synced = true;
        syncLabel->setText("Synced with network");
        messageLabel->setText("");
        parse_daemon_timer->stop();
    }else if(log.contains(QRegularExpression("(?<=Sync data(?!.*Sync data)).*(?=\\[)"))){
        log.replace(QRegularExpression(".*Sync data returned unknown top block: \\d* -> \\d* \\[\\d* blocks \\("), "");
        log.replace(QRegularExpression(" days.*"), "");
        syncLabel->setText("Syncing with network (" + log + " days behind)");
    }else{
        qDebug() << "No sync status found";
    }
}

void DashcoinWallet::load_balance()
{
    loader_balance = new QNetworkAccessManager(this);
    connect(loader_balance, SIGNAL(finished(QNetworkReply*)),this, SLOT(rpcReply(QNetworkReply*)));
    QString dataStr = "{\"jsonrpc\": \"2.0\", \"method\":\"getbalance\", \"id\": \"balance\"}";
    QJsonDocument jsonData = QJsonDocument::fromJson(dataStr.toUtf8());
    QByteArray data = jsonData.toJson();
    QNetworkRequest request = QNetworkRequest(QUrl("http://127.0.0.1:49253/json_rpc"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    loader_balance->post(request, data);
}

void DashcoinWallet::load_address()
{
    QFile addressFile(QDir::currentPath ()+"/wallets/"+current_wallet+".bin.address.txt");
    if(!addressFile.exists() || !addressFile.open(QIODevice::ReadOnly)){
        ui->txt_receive_address->setText("Cannot load address");
    }else{
        QTextStream in(&addressFile);
        ui->txt_receive_address->setText(in.readLine());
    }
}

void DashcoinWallet::load_transactions()
{
    loader_transactions = new QNetworkAccessManager(this);
    connect(loader_transactions, SIGNAL(finished(QNetworkReply*)),this, SLOT(rpcReply(QNetworkReply*)));
    QString dataStr = "{\"jsonrpc\": \"2.0\", \"method\":\"get_transfers\", \"id\": \"transactions\"}";
    QJsonDocument jsonData = QJsonDocument::fromJson(dataStr.toUtf8());
    QByteArray data = jsonData.toJson();
    QNetworkRequest request = QNetworkRequest(QUrl("http://127.0.0.1:49253/json_rpc"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    loader_transactions->post(request, data);
}

QString DashcoinWallet::fix_balance(QString str)
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

QString DashcoinWallet::fix_amount(QString str)
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

void DashcoinWallet::on_txt_password_open_returnPressed()
{
    on_btn_open_clicked();
}
