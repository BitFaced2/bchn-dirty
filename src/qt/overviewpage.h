// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <interfaces/wallet.h>

#include <QWidget>
#include <memory>

class ClientModel;
class TransactionFilterProxy;
class TxViewDelegate;
class PlatformStyle;
class WalletModel;
class SendCoinsDialog;
class ReceiveCoinsDialog;
class TransactionView;

QT_BEGIN_NAMESPACE
class QDialog;
class QPushButton;
QT_END_NAMESPACE

namespace Ui {
class OverviewPage;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Overview ("home") page widget */
class OverviewPage : public QWidget {
    Q_OBJECT

public:
    explicit OverviewPage(const PlatformStyle *platformStyle,
                          QWidget *parent = nullptr);
    ~OverviewPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    void showOutOfSyncWarning(bool fShow);
    void embedSubWidgets(SendCoinsDialog *send, ReceiveCoinsDialog *receive,
                        TransactionView *history, QPushButton *exportButton);
    /** Reparent an external status/network widget into the top-right of
     *  the Balances card. Ownership is not transferred; caller retains
     *  responsibility for lifetime. */
    void installNetworkStatusWidget(QWidget *w);

public Q_SLOTS:
    void setBalance(const interfaces::WalletBalances &balances);

Q_SIGNALS:
    void transactionClicked(const QModelIndex &index);
    void outOfSyncWarningClicked();

private:
    Ui::OverviewPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    interfaces::WalletBalances m_balances;

    TxViewDelegate *txdelegate;
    std::unique_ptr<TransactionFilterProxy> filter;

    QDialog *m_sendAdvancedDialog = nullptr;
    QDialog *m_receiveAdvancedDialog = nullptr;
    QDialog *m_receiveHistoryDialog = nullptr;
    QWidget *m_sendFeeWidget = nullptr;
    QWidget *m_receiveHistoryWidget = nullptr;
    class QHBoxLayout *m_topRow = nullptr;
    QLabel *m_receiveQrLabel = nullptr;
    QLabel *m_receiveAddressLabel = nullptr;

private Q_SLOTS:
    void updateDisplayUnit();
    void handleTransactionClicked(const QModelIndex &index);
    void updateAlerts(const QString &warnings);
    void updateWatchOnlyLabels(bool showWatchOnly);
    void handleOutOfSyncWarningClicks();
    void refreshReceiveAddress();
};
