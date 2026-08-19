// Copyright (c) 2011-2019 The Bitcoin Core developers
// Copyright (c) 2020-2025 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <qt/bitcoingui.h>

#include <chain.h>
#include <chainparams.h>
#include <config.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#ifdef Q_OS_MAC
#include <qt/macdockiconhandler.h>
#endif
#include <qt/modaloverlay.h>
#include <qt/networkstyle.h>
#include <qt/notificator.h>
#include <qt/openuridialog.h>
#include <qt/optionsdialog.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/rpcconsole.h>
#include <qt/titlebar.h>
#include <qt/utilitydialog.h>

#include <QGraphicsDropShadowEffect>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#endif
#ifdef ENABLE_WALLET
#include <qt/walletcontroller.h>
#include <qt/overviewpage.h>
#include <qt/walletframe.h>
#include <qt/walletmodel.h>
#include <qt/walletview.h>
#endif // ENABLE_WALLET
#include <ui_interface.h>
#include <util/system.h>

#include <algorithm>
#include <memory>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QActionGroup>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QDesktopServices>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QUrl>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QVariantAnimation>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWindow>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

const std::string BitcoinGUI::DEFAULT_UIPLATFORM =
#if defined(Q_OS_MAC)
    "macosx"
#elif defined(Q_OS_WIN)
    "windows"
#else
    "other"
#endif
    ;

namespace {
// A QLabel that paints its text so the actual visible ink is centred inside
// the widget rect. Bypasses QLabel's default AlignCenter (which centres by
// advance width and inherits font side-bearings + trailing letter-space
// bias). Guarantees two OpticalCenterLabels stacked in a centred layout
// share the same optical centre regardless of font, size, or letter-spacing.
class OpticalCenterLabel : public QLabel {
public:
    explicit OpticalCenterLabel(QWidget *parent = nullptr) : QLabel(parent) {
        // Expand horizontally so the layout gives us the full column
        // width. Ink is then centred inside our wide widget rect, which
        // matches the column centre — guarantees perfect alignment with
        // any other OpticalCenterLabel stacked above or below.
        setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    }
    void setTextColor(const QColor &c) { m_color = c; update(); }
    QSize sizeHint() const override {
        const QFontMetrics fm(font());
        return QSize(0, fm.height() + 6);
    }
    QSize minimumSizeHint() const override {
        const QFontMetrics fm(font());
        return QSize(fm.horizontalAdvance(text()), fm.height() + 6);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        if (text().isEmpty()) return;
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setFont(font());
        p.setPen(m_color.isValid() ? m_color
                                   : palette().color(foregroundRole()));
        const QFontMetrics fm(font());
        const QRect tight = fm.tightBoundingRect(text());
        // Centre the ink rectangle inside the widget rect.
        const int x = (width() - tight.width()) / 2 - tight.left();
        const int y = (height() + fm.ascent() - fm.descent()) / 2;
        p.drawText(x, y, text());
    }
private:
    QColor m_color;
};
}  // namespace

BitcoinGUI::BitcoinGUI(interfaces::Node &node, const Config *configIn,
                       const PlatformStyle *_platformStyle,
                       const NetworkStyle *networkStyle, QWidget *parent)
    : QMainWindow(parent), m_node(node), trayIconMenu{new QMenu()},
      config(configIn), platformStyle(_platformStyle),
      m_network_style(networkStyle) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    QSettings settings;
    if (!restoreGeometry(settings.value("MainWindowGeometry").toByteArray())) {
        // Restore failed (perhaps missing setting), center the window
        move(QGuiApplication::primaryScreen()->availableGeometry().center() -
             frameGeometry().center());
    }

#ifdef ENABLE_WALLET
    enableWallet = WalletModel::isWalletEnabled();
#endif // ENABLE_WALLET
    QApplication::setWindowIcon(m_network_style->getTrayAndWindowIcon());
    setWindowIcon(m_network_style->getTrayAndWindowIcon());
    updateWindowTitle();

    rpcConsole = new RPCConsole(node, _platformStyle, nullptr);
    helpMessageDialog = new HelpMessageDialog(node, this, false);
#ifdef ENABLE_WALLET
    if (enableWallet) {
        /** Create wallet frame and make it the central widget */
        walletFrame = new WalletFrame(_platformStyle, this);
        setCentralWidget(walletFrame);
    } else
#endif // ENABLE_WALLET
    {
        /**
         * When compiled without wallet or -disablewallet is provided,  the
         * central widget is the rpc console.
         */
        setCentralWidget(rpcConsole);
        Q_EMIT consoleShown(rpcConsole);
    }

    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    // Needs walletFrame to be initialized
    createActions();

    // Create application menu bar
    createMenuBar();

    // Wrap the native menu bar in our custom frameless-window title bar and
    // install it as the QMainWindow menu widget.
#ifndef Q_OS_MAC
    m_titleBar = new CustomTitleBar(this, appMenuBar,
                                    m_network_style->getTrayAndWindowIcon(),
                                    windowTitle(), this);
    setMenuWidget(m_titleBar);
#endif

    // Create the toolbars
    createToolBars();

    // Create system tray icon and notification
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        createTrayIcon();
    }
    notificator =
        new Notificator(QApplication::applicationName(), trayIcon, this);

    // Create status bar
    statusBar();

    // Frameless window has no native resize edges; enable size grip so the
    // user can still resize from the bottom-right corner.
    statusBar()->setSizeGripEnabled(true);

    // Status bar notification icons
    QFrame *frameBlocks = new QFrame();
    m_frameBlocks = frameBlocks;
    frameBlocks->setObjectName(QStringLiteral("networkStatusPanel"));
    frameBlocks->setCursor(Qt::PointingHandCursor);
    frameBlocks->setToolTip(tr("Double-click for full-screen block counter"));
    frameBlocks->installEventFilter(this);
    frameBlocks->setContentsMargins(0, 0, 0, 0);
    frameBlocks->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    QVBoxLayout *frameBlocksLayout = new QVBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(14, 10, 14, 12);
    frameBlocksLayout->setSpacing(6);
    unitDisplayControl = new UnitDisplayStatusBarControl(platformStyle);
    labelWalletEncryptionIcon = new QLabel();
    labelWalletHDStatusIcon = new QLabel();
    labelProxyIcon = new GUIUtil::ClickableLabel();
    connectionsControl = new GUIUtil::ClickableLabel();
    labelBlocksIcon = new GUIUtil::ClickableLabel();

    auto makeCockpitLabel = [](const QString &initial) {
        auto *l = new QLabel(initial);
        QFont f(QStringLiteral("JetBrains Mono"));
        f.setPointSize(10);
        f.setWeight(QFont::Medium);
        l->setFont(f);
        l->setStyleSheet(QStringLiteral(
            "QLabel { color: #E8ECEA; background: transparent; }"));
        return l;
    };
    auto makeCockpitSep = [] {
        auto *sep = new QFrame();
        sep->setObjectName(QStringLiteral("cockpitSep"));
        sep->setFrameShape(QFrame::VLine);
        sep->setFixedWidth(1);
        sep->setStyleSheet(QStringLiteral(
            "QFrame { color: rgba(157, 78, 221, 60); background: rgba(157, 78, 221, 60); }"));
        return sep;
    };

    m_lblWalletText = makeCockpitLabel(tr("Wallet"));
    m_lblPeersText  = makeCockpitLabel(tr("0 peers"));
    m_lblBlocksText = makeCockpitLabel(tr("—"));

    // Top: centered horizontal chips row — unit, wallet, peers.
    auto *chipsRow = new QHBoxLayout();
    chipsRow->setSpacing(10);
    chipsRow->setContentsMargins(0, 0, 0, 0);
    chipsRow->addStretch(1);
    if (enableWallet) {
        chipsRow->addWidget(unitDisplayControl);
        chipsRow->addWidget(makeCockpitSep());
        chipsRow->addWidget(labelWalletEncryptionIcon);
        chipsRow->addWidget(labelWalletHDStatusIcon);
        chipsRow->addWidget(m_lblWalletText);
        chipsRow->addWidget(makeCockpitSep());
    }
    if (labelProxyIcon) {
        chipsRow->addWidget(labelProxyIcon);
    }
    chipsRow->addWidget(connectionsControl);
    chipsRow->addWidget(m_lblPeersText);
    chipsRow->addStretch(1);
    frameBlocksLayout->addLayout(chipsRow);
    // Vertical centering: block group sits between two equal stretches so
    // the huge number lands in the middle of the card, chips stay pinned
    // to the top.
    frameBlocksLayout->addStretch(1);

    // Middle: huge Orbitron block number (no "Block" prefix, no
    // sync-status checkmark — those live elsewhere or are implied).
    labelBlocksIcon->hide();
    m_lblBlocksText->setObjectName(QStringLiteral("cockpitBlockNumber"));
    QFont blockFont(QStringLiteral("Orbitron"));
    blockFont.setPointSize(64);
    blockFont.setWeight(QFont::Black);
    m_lblBlocksText->setFont(blockFont);
    m_lblBlocksText->setStyleSheet(QStringLiteral(
        "QLabel { color: #0AC18E; background: transparent; letter-spacing: 3px; }"));
    m_lblBlocksText->setAlignment(Qt::AlignHCenter);
    auto *blockRow = new QHBoxLayout();
    blockRow->setSpacing(8);
    blockRow->setContentsMargins(0, 0, 0, 0);
    blockRow->addStretch(1);
    blockRow->addWidget(m_lblBlocksText);
    blockRow->addStretch(1);
    frameBlocksLayout->addLayout(blockRow);

    // Bottom: "BLOCK" caption sits under the number. Kept as a member so the
    // Qube memory clock can relabel it MEMORY when the qubes-watch wallet is up.
    m_blockCaption = new QLabel(tr("BLOCK"));
    {
        QFont cf(QStringLiteral("Rajdhani"));
        cf.setPointSize(9);
        cf.setWeight(QFont::DemiBold);
        cf.setCapitalization(QFont::AllUppercase);
        cf.setLetterSpacing(QFont::AbsoluteSpacing, 4.0);
        m_blockCaption->setFont(cf);
        m_blockCaption->setStyleSheet(QStringLiteral(
            "QLabel { color: #7B857F; background: transparent; }"));
        m_blockCaption->setAlignment(Qt::AlignHCenter);
    }
    frameBlocksLayout->addWidget(m_blockCaption);
    frameBlocksLayout->addStretch(1);

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);
    progressBar = new GUIUtil::ProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    // Override style sheet for progress bar for styles that have a segmented
    // progress bar, as they make the text unreadable (workaround for issue
    // #1071)
    // See https://doc.qt.io/qt-5/gallery.html
    QString curStyle = QApplication::style()->metaObject()->className();
    if (curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle") {
        progressBar->setStyleSheet(
            "QProgressBar { background-color: #e8e8e8; border: 1px solid grey; "
            "border-radius: 7px; padding: 1px; text-align: center; } "
            "QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, "
            "x2: 1, y2: 0, stop: 0 #FF8000, stop: 1 orange); border-radius: "
            "7px; margin: 0px; }");
    }

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);

    // Qube memory clock: a network manager + a 2s poll of the local qubed,
    // started only while the qubes-watch wallet is active (applyQubeWatchMode).
    m_qubeMemTimer = new QTimer(this);
    m_qubeMemTimer->setInterval(2000);
    connect(m_qubeMemTimer, &QTimer::timeout, this,
            &BitcoinGUI::pollQubeMemory);

    // Install event filter to be able to catch status tip events
    // (QEvent::StatusTip)
    this->installEventFilter(this);

    // Initially wallet actions should be disabled
    setWalletActionsEnabled(false);

    // Subscribe to notifications from core
    subscribeToCoreSignals();

    connect(connectionsControl, &GUIUtil::ClickableLabel::clicked,
            [this] { m_node.setNetworkActive(!m_node.getNetworkActive()); });
    connect(labelProxyIcon, &GUIUtil::ClickableLabel::clicked,
            [this] { openOptionsDialogWithTab(OptionsDialog::TAB_NETWORK); });

    modalOverlay = new ModalOverlay(this->centralWidget());
#ifdef ENABLE_WALLET
    if (enableWallet) {
        connect(walletFrame, &WalletFrame::requestedSyncWarningInfo, this,
                &BitcoinGUI::showModalOverlay);
        connect(labelBlocksIcon, &GUIUtil::ClickableLabel::clicked, this,
                &BitcoinGUI::showModalOverlay);
        connect(progressBar, &GUIUtil::ClickableProgressBar::clicked, this,
                &BitcoinGUI::showModalOverlay);
    }
#endif
}

BitcoinGUI::~BitcoinGUI() {
    // Unsubscribe from notifications from core
    unsubscribeFromCoreSignals();

    QSettings settings;
    settings.setValue("MainWindowGeometry", saveGeometry());
    // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
    if (trayIcon) {
        trayIcon->hide();
    }
#ifdef Q_OS_MAC
    delete appMenuBar;
    MacDockIconHandler::cleanup();
#endif

    delete rpcConsole;
}

void BitcoinGUI::createActions() {
    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction =
        new QAction(platformStyle->SingleColorIcon(":/icons/overview"),
                    tr("&Overview"), this);
    overviewAction->setStatusTip(tr("Show general overview of wallet"));
    overviewAction->setToolTip(overviewAction->statusTip());
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + static_cast<int>(Qt::Key_1)));
    tabGroup->addAction(overviewAction);

    sendCoinsAction = new QAction(
        platformStyle->SingleColorIcon(":/icons/send"), tr("&Send"), this);
    sendCoinsAction->setStatusTip(tr("Send coins to a Bitcoin Cash address"));
    sendCoinsAction->setToolTip(sendCoinsAction->statusTip());
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + static_cast<int>(Qt::Key_2)));
    tabGroup->addAction(sendCoinsAction);

    sendCoinsMenuAction =
        new QAction(platformStyle->TextColorIcon(":/icons/send"),
                    sendCoinsAction->text() + "...", this);
    sendCoinsMenuAction->setStatusTip(sendCoinsAction->statusTip());
    sendCoinsMenuAction->setToolTip(sendCoinsMenuAction->statusTip());

    receiveCoinsAction = new QAction(
        platformStyle->SingleColorIcon(":/icons/receiving_addresses"),
        tr("&Receive"), this);
    receiveCoinsAction->setStatusTip(
        tr("Request payments (generates QR codes and %1: URIs)")
            .arg(QString::fromStdString(
                config->GetChainParams().CashAddrPrefix())));
    receiveCoinsAction->setToolTip(receiveCoinsAction->statusTip());
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + static_cast<int>(Qt::Key_3)));
    tabGroup->addAction(receiveCoinsAction);

    receiveCoinsMenuAction =
        new QAction(platformStyle->TextColorIcon(":/icons/receiving_addresses"),
                    receiveCoinsAction->text() + "...", this);
    receiveCoinsMenuAction->setStatusTip(receiveCoinsAction->statusTip());
    receiveCoinsMenuAction->setToolTip(receiveCoinsMenuAction->statusTip());

    historyAction =
        new QAction(platformStyle->SingleColorIcon(":/icons/history"),
                    tr("&Transactions"), this);
    historyAction->setStatusTip(tr("Browse transaction history"));
    historyAction->setToolTip(historyAction->statusTip());
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + static_cast<int>(Qt::Key_4)));
    tabGroup->addAction(historyAction);

#ifdef ENABLE_WALLET
    // These showNormalIfMinimized are needed because Send Coins and Receive
    // Coins can be triggered from the tray menu, and need to show the GUI to be
    // useful.
    connect(overviewAction, &QAction::triggered,
            [this] { showNormalIfMinimized(); });
    connect(overviewAction, &QAction::triggered, this,
            &BitcoinGUI::gotoOverviewPage);
    connect(sendCoinsAction, &QAction::triggered,
            [this] { showNormalIfMinimized(); });
    connect(sendCoinsAction, &QAction::triggered,
            [this] { gotoSendCoinsPage(); });
    connect(sendCoinsMenuAction, &QAction::triggered,
            [this] { showNormalIfMinimized(); });
    connect(sendCoinsMenuAction, &QAction::triggered,
            [this] { gotoSendCoinsPage(); });
    connect(receiveCoinsAction, &QAction::triggered,
            [this] { showNormalIfMinimized(); });
    connect(receiveCoinsAction, &QAction::triggered, this,
            &BitcoinGUI::gotoReceiveCoinsPage);
    connect(receiveCoinsMenuAction, &QAction::triggered,
            [this] { showNormalIfMinimized(); });
    connect(receiveCoinsMenuAction, &QAction::triggered, this,
            &BitcoinGUI::gotoReceiveCoinsPage);
    connect(historyAction, &QAction::triggered,
            [this] { showNormalIfMinimized(); });
    connect(historyAction, &QAction::triggered, this,
            &BitcoinGUI::gotoHistoryPage);
#endif // ENABLE_WALLET

    quitAction = new QAction(platformStyle->TextColorIcon(":/icons/quit"),
                             tr("E&xit"), this);
    quitAction->setStatusTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + static_cast<int>(Qt::Key_Q)));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(platformStyle->TextColorIcon(":/icons/about"),
                              tr("&About %1").arg(PACKAGE_NAME), this);
    aboutAction->setStatusTip(
        tr("Show information about %1").arg(PACKAGE_NAME));
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutAction->setEnabled(false);
    aboutQtAction =
        new QAction(platformStyle->TextColorIcon(":/icons/about_qt"),
                    tr("About &Qt"), this);
    aboutQtAction->setStatusTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(platformStyle->TextColorIcon(":/icons/options"),
                                tr("&Options..."), this);
    optionsAction->setStatusTip(
        tr("Modify configuration options for %1").arg(PACKAGE_NAME));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    optionsAction->setEnabled(false);
    toggleHideAction =
        new QAction(platformStyle->TextColorIcon(":/icons/about"),
                    tr("&Show / Hide"), this);
    toggleHideAction->setStatusTip(tr("Show or hide the main Window"));

    encryptWalletAction =
        new QAction(platformStyle->TextColorIcon(":/icons/lock_closed"),
                    tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setStatusTip(
        tr("Encrypt the private keys that belong to your wallet"));
    encryptWalletAction->setCheckable(true);
    backupWalletAction =
        new QAction(platformStyle->TextColorIcon(":/icons/filesave"),
                    tr("&Backup Wallet..."), this);
    backupWalletAction->setStatusTip(tr("Backup wallet to another location"));
    changePassphraseAction =
        new QAction(platformStyle->TextColorIcon(":/icons/key"),
                    tr("&Change Passphrase..."), this);
    changePassphraseAction->setStatusTip(
        tr("Change the passphrase used for wallet encryption"));
    signMessageAction =
        new QAction(platformStyle->TextColorIcon(":/icons/edit"),
                    tr("Sign &Message..."), this);
    signMessageAction->setStatusTip(
        tr("Sign messages with your Bitcoin Cash addresses to prove you own them"));
    verifyMessageAction =
        new QAction(platformStyle->TextColorIcon(":/icons/verify"),
                    tr("&Verify Message..."), this);
    verifyMessageAction->setStatusTip(
        tr("Verify messages to ensure they were signed with specified Bitcoin Cash addresses"));

    openRPCConsoleAction =
        new QAction(platformStyle->TextColorIcon(":/icons/debugwindow"),
                    tr("No&de Window"), this);
    openRPCConsoleAction->setStatusTip(
        tr("Open node debugging and diagnostic console"));
    // initially disable the debug window menu item
    openRPCConsoleAction->setEnabled(false);
    openRPCConsoleAction->setObjectName("openRPCConsoleAction");

    usedSendingAddressesAction =
        new QAction(platformStyle->TextColorIcon(":/icons/address-book"),
                    tr("&Sending Addresses"), this);
    usedSendingAddressesAction->setStatusTip(
        tr("Show the list of used sending addresses and labels"));
    usedReceivingAddressesAction =
        new QAction(platformStyle->TextColorIcon(":/icons/address-book"),
                    tr("&Receiving Addresses"), this);
    usedReceivingAddressesAction->setStatusTip(
        tr("Show the list of used receiving addresses and labels"));

    openAction = new QAction(platformStyle->TextColorIcon(":/icons/open"),
                             tr("Open &URI..."), this);
    openAction->setStatusTip(
        tr("Open a %1: URI")
            .arg(QString::fromStdString(
                config->GetChainParams().CashAddrPrefix())));

    showHelpMessageAction =
        new QAction(platformStyle->TextColorIcon(":/icons/info"),
                    tr("&Command-line options"), this);
    showHelpMessageAction->setMenuRole(QAction::NoRole);
    showHelpMessageAction->setStatusTip(
        tr("Show the %1 help message to get a list with possible command-line options")
            .arg(PACKAGE_NAME));

    connect(quitAction, &QAction::triggered, qApp, QApplication::quit);
    connect(aboutAction, &QAction::triggered, this, &BitcoinGUI::aboutClicked);
    connect(aboutQtAction, &QAction::triggered, qApp, QApplication::aboutQt);
    connect(optionsAction, &QAction::triggered, this,
            &BitcoinGUI::optionsClicked);
    connect(toggleHideAction, &QAction::triggered, this,
            &BitcoinGUI::toggleHidden);
    connect(showHelpMessageAction, &QAction::triggered, this,
            &BitcoinGUI::showHelpMessageClicked);
    connect(openRPCConsoleAction, &QAction::triggered, this,
            &BitcoinGUI::showDebugWindow);
    // prevents an open debug window from becoming stuck/unusable on client
    // shutdown
    connect(quitAction, &QAction::triggered, rpcConsole, &QWidget::hide);

#ifdef ENABLE_WALLET
    if (walletFrame) {
        connect(encryptWalletAction, &QAction::triggered, walletFrame,
                &WalletFrame::encryptWallet);
        connect(backupWalletAction, &QAction::triggered, walletFrame,
                &WalletFrame::backupWallet);
        connect(changePassphraseAction, &QAction::triggered, walletFrame,
                &WalletFrame::changePassphrase);
        connect(signMessageAction, &QAction::triggered,
                [this] { showNormalIfMinimized(); });
        connect(signMessageAction, &QAction::triggered,
                [this] { gotoSignMessageTab(); });
        connect(verifyMessageAction, &QAction::triggered,
                [this] { showNormalIfMinimized(); });
        connect(verifyMessageAction, &QAction::triggered,
                [this] { gotoVerifyMessageTab(); });
        connect(usedSendingAddressesAction, &QAction::triggered, walletFrame,
                &WalletFrame::usedSendingAddresses);
        connect(usedReceivingAddressesAction, &QAction::triggered, walletFrame,
                &WalletFrame::usedReceivingAddresses);
        connect(openAction, &QAction::triggered, this,
                &BitcoinGUI::openClicked);
    }
#endif // ENABLE_WALLET

    connect(new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_C), this),
            &QShortcut::activated, this,
            &BitcoinGUI::showDebugWindowActivateConsole);
    connect(new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_D), this),
            &QShortcut::activated, this, &BitcoinGUI::showDebugWindow);
}

void BitcoinGUI::createMenuBar() {
#ifdef Q_OS_MAC
    // Create a decoupled menu bar on Mac which stays even if the window is
    // closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    if (walletFrame) {
        file->addAction(openAction);
        file->addAction(backupWalletAction);
        file->addAction(signMessageAction);
        file->addAction(verifyMessageAction);
        file->addSeparator();
    }
    file->addAction(quitAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Settings"));
    if (walletFrame) {
        settings->addAction(encryptWalletAction);
        settings->addAction(changePassphraseAction);
        settings->addSeparator();
    }
    settings->addAction(optionsAction);

    QMenu *window_menu = appMenuBar->addMenu(tr("&Window"));

    QAction *minimize_action = window_menu->addAction(tr("Minimize"));
    minimize_action->setStatusTip(tr("Minimize the Main Window"));
    minimize_action->setToolTip(minimize_action->statusTip());
    minimize_action->setShortcut(QKeySequence(Qt::CTRL + static_cast<int>(Qt::Key_M)));
    connect(minimize_action, &QAction::triggered,
            [] { QApplication::activeWindow()->showMinimized(); });
    connect(qApp, &QApplication::focusWindowChanged,
            [minimize_action](QWindow *window) {
                minimize_action->setEnabled(
                    window != nullptr &&
                    (window->flags() & Qt::Dialog) != Qt::Dialog &&
                    window->windowState() != Qt::WindowMinimized);
            });

#ifdef Q_OS_MAC
    QAction *zoom_action = window_menu->addAction(tr("Zoom"));
    // No setStatusTip+setToolTip here because these don't work on the MacOS menu bar.
    connect(zoom_action, &QAction::triggered, [] {
        QWindow *window = qApp->focusWindow();
        if (window->windowState() != Qt::WindowMaximized) {
            window->showMaximized();
        } else {
            window->showNormal();
        }
    });

    connect(qApp, &QApplication::focusWindowChanged,
            [zoom_action](QWindow *window) {
                zoom_action->setEnabled(window != nullptr);
            });
#endif

    if (walletFrame) {
#ifdef Q_OS_MAC
        window_menu->addSeparator();
        m_main_window_action = window_menu->addAction(tr("Main Window"));
        // No setStatusTip+setToolTip here because these don't work on the MacOS menu bar.
        connect(m_main_window_action, &QAction::triggered, this,
                [this] { GUIUtil::bringToFront(this); });
#endif
        window_menu->addSeparator();
        window_menu->addAction(usedSendingAddressesAction);
        window_menu->addAction(usedReceivingAddressesAction);
    }

    window_menu->addSeparator();
    for (RPCConsole::TabTypes tab_type : rpcConsole->tabs()) {
        QString title = rpcConsole->tabTitle(tab_type);
        QAction *tab_action = window_menu->addAction(platformStyle->TextColorIcon(":/icons/debugwindow"), title);
        int shortcutKeyPosition = title.indexOf('&');
        if (shortcutKeyPosition != -1) {
            // Strip first ampersand from the title (keyboard shortcut) before putting it in the status tip.
            title.remove(shortcutKeyPosition, 1);
        }
        tab_action->setStatusTip(tr("Show the %1 tab of the Node Window").arg(title));
        tab_action->setToolTip(tab_action->statusTip());
        tab_action->setShortcut(rpcConsole->tabShortcut(tab_type));
        connect(tab_action, &QAction::triggered, this, [this, tab_type] {
            rpcConsole->setTabFocus(tab_type);
            showDebugWindow();
        });

        m_node_actions.append(tab_action);
    }

    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    help->addAction(showHelpMessageAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);

    setWindowActionsEnabled(false);
}

void BitcoinGUI::createToolBars() {
    if (walletFrame) {
        QToolBar *toolbar = addToolBar(tr("Tabs toolbar"));
        appToolBar = toolbar;
        toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
        toolbar->setMovable(false);
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        // Single-page dashboard: no tab bar. The Send / Receive / History
        // sections live inside the Overview page. Actions are still kept
        // alive for tray/menu access and keyboard shortcuts.
        overviewAction->setChecked(true);
        toolbar->hide();

#ifdef ENABLE_WALLET
        // QUBES launcher (BCHN Dirty): top-left twin of the wallet selector.
        // Probes the local qubed (127.0.0.1:8787); if it's awake, opens its
        // UI — otherwise runs the configured launch command (QSettings
        // "qubedLaunchCmd", else env QUBED_LAUNCH) and then opens it.
        auto *qubesBtn = new QPushButton(QStringLiteral("QUBES"));
        {
            QFont qf(QStringLiteral("Orbitron"));
            qf.setPointSize(11);
            qf.setWeight(QFont::Bold);
            qf.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
            qubesBtn->setFont(qf);
        }
        qubesBtn->setCursor(Qt::PointingHandCursor);
        qubesBtn->setToolTip(
            tr("Wake the Qubes daemon and open its interface"));
        qubesBtn->setStyleSheet(QStringLiteral(
            "QPushButton { color: #9D4EDD; background: transparent; "
            "border: 1px solid #9D4EDD; border-radius: 8px; "
            "padding: 5px 16px; }"
            "QPushButton:hover { background: rgba(157,78,221,0.16); }"
            "QPushButton:pressed { background: rgba(157,78,221,0.32); }"));
        connect(qubesBtn, &QPushButton::clicked, this, [this]() {
            const QUrl url(QStringLiteral("http://127.0.0.1:8787"));
            auto *probe = new QTcpSocket(this);
            auto *decide = new QTimer(probe);
            decide->setSingleShot(true);
            // One decision point (no error/timeout double-fire): after the
            // grace period, connected = open; anything else = launch + open.
            connect(decide, &QTimer::timeout, this, [this, probe, url]() {
                const bool awake =
                    probe->state() == QAbstractSocket::ConnectedState;
                probe->abort();
                probe->deleteLater();
                if (awake) {
                    QDesktopServices::openUrl(url);
                    return;
                }
                QSettings settings;
                QString cmd =
                    settings.value(QStringLiteral("qubedLaunchCmd"))
                        .toString();
                if (cmd.isEmpty()) {
                    cmd = qEnvironmentVariable("QUBED_LAUNCH");
                }
                if (!cmd.isEmpty()) {
#ifdef WIN32
                    QProcess::startDetached(
                        QStringLiteral("cmd.exe"),
                        {QStringLiteral("/c"), cmd});
#else
                    QProcess::startDetached(cmd, {});
#endif
                    QTimer::singleShot(2200, this, [url]() {
                        QDesktopServices::openUrl(url);
                    });
                } else {
                    QDesktopServices::openUrl(url); // best effort
                }
            });
            probe->connectToHost(QStringLiteral("127.0.0.1"), 8787);
            decide->start(900);
        });
        toolbar->addWidget(qubesBtn);

        QWidget *spacer = new QWidget();
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        toolbar->addWidget(spacer);

        m_wallet_selector = new QComboBox();
        connect(m_wallet_selector,
                static_cast<void (QComboBox::*)(int)>(
                    &QComboBox::currentIndexChanged),
                this, &BitcoinGUI::setCurrentWalletBySelectorIndex);

        m_wallet_selector_label = new QLabel();
        m_wallet_selector_label->setText(tr("Wallet:") + " ");
        m_wallet_selector_label->setBuddy(m_wallet_selector);

        m_wallet_selector_label_action =
            appToolBar->addWidget(m_wallet_selector_label);
        m_wallet_selector_action = appToolBar->addWidget(m_wallet_selector);

        m_wallet_selector_label_action->setVisible(false);
        m_wallet_selector_action->setVisible(false);
#endif
    }
}

void BitcoinGUI::setClientModel(ClientModel *_clientModel) {
    this->clientModel = _clientModel;
    if (_clientModel) {
        // Create system tray menu (or setup the dock menu) that late to prevent
        // users from calling actions, while the client has not yet fully loaded
        createTrayIconMenu();

        // Keep up to date with client
        updateNetworkState();
        connect(_clientModel, &ClientModel::numConnectionsChanged, this,
                &BitcoinGUI::setNumConnections);
        connect(_clientModel, &ClientModel::networkActiveChanged, this,
                &BitcoinGUI::setNetworkActive);

        modalOverlay->setKnownBestHeight(
            _clientModel->getHeaderTipHeight(),
            GUIUtil::dateTimeFromTime(_clientModel->getHeaderTipTime()));
        setNumBlocks(m_node.getNumBlocks(), GUIUtil::dateTimeFromTime(m_node.getLastBlockTime()),
                     QString::fromStdString(m_node.getLastBlockHash().ToString()), m_node.getVerificationProgress(), false);
        connect(_clientModel, &ClientModel::numBlocksChanged, this,
                &BitcoinGUI::setNumBlocks);

        // Receive and report messages from client model
        connect(_clientModel, &ClientModel::message,
                [this](const QString &title, const QString &message,
                       unsigned int style) {
                    this->message(title, message, style);
                });

        // Show progress dialog
        connect(_clientModel, &ClientModel::showProgress, this,
                &BitcoinGUI::showProgress);

        rpcConsole->setClientModel(_clientModel);

        updateProxyIcon();

#ifdef ENABLE_WALLET
        if (walletFrame) {
            walletFrame->setClientModel(_clientModel);
        }
#endif // ENABLE_WALLET
        unitDisplayControl->setOptionsModel(_clientModel->getOptionsModel());

        OptionsModel *optionsModel = _clientModel->getOptionsModel();
        if (optionsModel && trayIcon) {
            // be aware of the tray icon disable state change reported by the
            // OptionsModel object.
            connect(optionsModel, &OptionsModel::hideTrayIconChanged, this,
                    &BitcoinGUI::setTrayIconVisible);

            // initialize the disable state of the tray icon with the current
            // value in the model.
            setTrayIconVisible(optionsModel->getHideTrayIcon());
        }
    } else {
        // Disable possibility to show main window via action
        toggleHideAction->setEnabled(false);
        if (trayIconMenu) {
            // Disable context menu on tray icon
            trayIconMenu->clear();
        }
        // Propagate cleared model to child objects
        rpcConsole->setClientModel(nullptr);
#ifdef ENABLE_WALLET
        if (walletFrame) {
            walletFrame->setClientModel(nullptr);
        }
#endif // ENABLE_WALLET
        unitDisplayControl->setOptionsModel(nullptr);
    }
}

#ifdef ENABLE_WALLET
void BitcoinGUI::setWalletController(WalletController *wallet_controller) {
    assert(!m_wallet_controller);
    assert(wallet_controller);

    m_wallet_controller = wallet_controller;

    connect(wallet_controller, &WalletController::walletAdded, this,
            &BitcoinGUI::addWallet);
    connect(wallet_controller, &WalletController::walletRemoved, this,
            &BitcoinGUI::removeWallet);

    for (WalletModel *wallet_model : m_wallet_controller->getWallets()) {
        addWallet(wallet_model);
    }
}

void BitcoinGUI::addWallet(WalletModel *walletModel) {
    if (!walletFrame) {
        return;
    }
    const QString display_name = walletModel->getDisplayName();
    setWalletActionsEnabled(true);
    rpcConsole->addWallet(walletModel);
    walletFrame->addWallet(walletModel);
    m_wallet_selector->addItem(display_name, QVariant::fromValue(walletModel));
    if (m_wallet_selector->count() == 2) {
        m_wallet_selector_label_action->setVisible(true);
        m_wallet_selector_action->setVisible(true);
        if (appToolBar) appToolBar->show();
    }

    // Move the network/status panel out of the bottom status bar and into
    // the top-right of the first wallet's Balances card. Done once per app
    // lifetime — the widget is a singleton and belongs to whichever card
    // installs it.
    if (!m_networkStatusInstalled && m_frameBlocks) {
        if (auto *wv = walletFrame->currentWalletView()) {
            if (auto *op = wv->getOverviewPage()) {
                op->installNetworkStatusWidget(m_frameBlocks);
                statusBar()->hide();
                m_networkStatusInstalled = true;
            }
        }
    }
}

void BitcoinGUI::removeWallet(WalletModel *walletModel) {
    if (!walletFrame) {
        return;
    }
    int index = m_wallet_selector->findData(QVariant::fromValue(walletModel));
    m_wallet_selector->removeItem(index);
    if (m_wallet_selector->count() == 0) {
        setWalletActionsEnabled(false);
    } else if (m_wallet_selector->count() == 1) {
        m_wallet_selector_label_action->setVisible(false);
        m_wallet_selector_action->setVisible(false);
        if (appToolBar) appToolBar->hide();
    }
    rpcConsole->removeWallet(walletModel);
    walletFrame->removeWallet(walletModel);
    updateWindowTitle();
}

void BitcoinGUI::setCurrentWallet(WalletModel *wallet_model) {
    if (!walletFrame) {
        return;
    }
    walletFrame->setCurrentWallet(wallet_model);
    // The cockpit clock card is a singleton widget installed into one overview
    // page. Each wallet has its own overview page, so move the card into the
    // now-active wallet's page on every switch — otherwise it only shows on the
    // first wallet it was installed into (and vanishes on all the others).
    // installNetworkStatusWidget reparents + re-adds, so this is safe to repeat.
    if (m_frameBlocks) {
        if (auto *wv = walletFrame->currentWalletView()) {
            if (auto *op = wv->getOverviewPage()) {
                op->installNetworkStatusWidget(m_frameBlocks);
                statusBar()->hide();
            }
        }
    }
    for (int index = 0; index < m_wallet_selector->count(); ++index) {
        if (m_wallet_selector->itemData(index).value<WalletModel *>() ==
            wallet_model) {
            m_wallet_selector->setCurrentIndex(index);
            break;
        }
    }
    updateWindowTitle();
}

void BitcoinGUI::setCurrentWalletBySelectorIndex(int index) {
    WalletModel *wallet_model =
        m_wallet_selector->itemData(index).value<WalletModel *>();
    if (wallet_model) {
        setCurrentWallet(wallet_model);
    }
}

void BitcoinGUI::removeAllWallets() {
    if (!walletFrame) {
        return;
    }
    setWalletActionsEnabled(false);
    walletFrame->removeAllWallets();
}
#endif // ENABLE_WALLET

void BitcoinGUI::setWalletActionsEnabled(bool enabled) {
    overviewAction->setEnabled(enabled);
    sendCoinsAction->setEnabled(enabled);
    sendCoinsMenuAction->setEnabled(enabled);
    receiveCoinsAction->setEnabled(enabled);
    receiveCoinsMenuAction->setEnabled(enabled);
    historyAction->setEnabled(enabled);
    encryptWalletAction->setEnabled(enabled);
    backupWalletAction->setEnabled(enabled);
    changePassphraseAction->setEnabled(enabled);
    signMessageAction->setEnabled(enabled);
    verifyMessageAction->setEnabled(enabled);
    usedSendingAddressesAction->setEnabled(enabled);
    usedReceivingAddressesAction->setEnabled(enabled);
    openAction->setEnabled(enabled);
}

void BitcoinGUI::createTrayIcon() {
    assert(QSystemTrayIcon::isSystemTrayAvailable());

#ifndef Q_OS_MAC
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        trayIcon =
            new QSystemTrayIcon(m_network_style->getTrayAndWindowIcon(), this);
        QString toolTip = tr("%1 client").arg(PACKAGE_NAME) + " " +
                          m_network_style->getTitleAddText();
        trayIcon->setToolTip(toolTip);
    }
#endif
}

void BitcoinGUI::createTrayIconMenu() {
#ifndef Q_OS_MAC
    // Return if trayIcon is unset (only on non-macOSes)
    if (!trayIcon) {
        return;
    }

    trayIcon->setContextMenu(trayIconMenu.get());
    connect(trayIcon, &QSystemTrayIcon::activated, this,
            &BitcoinGUI::trayIconActivated);
#else
    // Note: On macOS, the Dock icon is used to provide the tray's
    // functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    connect(dockIconHandler, &MacDockIconHandler::dockIconClicked, this,
            &BitcoinGUI::macosDockIconActivated);
    trayIconMenu->setAsDockMenu();
#endif

    // Configuration of the tray icon (or Dock icon) menu
#ifndef Q_OS_MAC
    // Note: On macOS, the Dock icon's menu already has Show / Hide action.
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
#endif
    if (enableWallet) {
        trayIconMenu->addAction(sendCoinsMenuAction);
        trayIconMenu->addAction(receiveCoinsMenuAction);
        trayIconMenu->addSeparator();
        trayIconMenu->addAction(signMessageAction);
        trayIconMenu->addAction(verifyMessageAction);
        trayIconMenu->addSeparator();
    }
    trayIconMenu->addAction(optionsAction);
    if (enableWallet) {
        trayIconMenu->addAction(openRPCConsoleAction);
    }
#ifndef Q_OS_MAC
    // This is built-in on macOS
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif
}

#ifndef Q_OS_MAC
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        // Click on system tray icon triggers show/hide of the main window
        toggleHidden();
    }
}
#else
void BitcoinGUI::macosDockIconActivated() {
    show();
    activateWindow();
}
#endif

void BitcoinGUI::optionsClicked() {
    openOptionsDialogWithTab(OptionsDialog::TAB_MAIN);
}

void BitcoinGUI::aboutClicked() {
    if (!clientModel) {
        return;
    }

    HelpMessageDialog dlg(m_node, this, true);
    dlg.exec();
}

void BitcoinGUI::showDebugWindow() {
    GUIUtil::bringToFront(rpcConsole);
    Q_EMIT consoleShown(rpcConsole);
}

void BitcoinGUI::showDebugWindowActivateConsole() {
    rpcConsole->setTabFocus(RPCConsole::TAB_CONSOLE);
    showDebugWindow();
}

void BitcoinGUI::showHelpMessageClicked() {
    helpMessageDialog->show();
}

#ifdef ENABLE_WALLET
void BitcoinGUI::openClicked() {
    OpenURIDialog dlg(config->GetChainParams(), this);
    if (dlg.exec()) {
        Q_EMIT receivedURI(dlg.getURI());
    }
}

void BitcoinGUI::gotoOverviewPage() {
    overviewAction->setChecked(true);
    if (walletFrame) {
        walletFrame->gotoOverviewPage();
    }
}

void BitcoinGUI::gotoHistoryPage() {
    historyAction->setChecked(true);
    if (walletFrame) {
        walletFrame->gotoHistoryPage();
    }
}

void BitcoinGUI::gotoReceiveCoinsPage() {
    receiveCoinsAction->setChecked(true);
    if (walletFrame) {
        walletFrame->gotoReceiveCoinsPage();
    }
}

void BitcoinGUI::gotoSendCoinsPage(QString addr) {
    sendCoinsAction->setChecked(true);
    if (walletFrame) {
        walletFrame->gotoSendCoinsPage(addr);
    }
}

void BitcoinGUI::gotoSignMessageTab(QString addr) {
    if (walletFrame) {
        walletFrame->gotoSignMessageTab(addr);
    }
}

void BitcoinGUI::gotoVerifyMessageTab(QString addr) {
    if (walletFrame) {
        walletFrame->gotoVerifyMessageTab(addr);
    }
}
#endif // ENABLE_WALLET

void BitcoinGUI::updateNetworkState() {
    int count = clientModel->getNumConnections();
    QString icon;
    switch (count) {
        case 0:
            icon = ":/icons/connect_0";
            break;
        case 1:
        case 2:
        case 3:
            icon = ":/icons/connect_1";
            break;
        case 4:
        case 5:
        case 6:
            icon = ":/icons/connect_2";
            break;
        case 7:
        case 8:
        case 9:
            icon = ":/icons/connect_3";
            break;
        default:
            icon = ":/icons/connect_4";
            break;
    }

    QString tooltip;

    if (m_node.getNetworkActive()) {
        tooltip = tr("%n active connection(s) to Bitcoin network", "", count) +
                  QString(".<br>") + tr("Click to disable network activity.");
    } else {
        tooltip = tr("Network activity disabled.") + QString("<br>") +
                  tr("Click to enable network activity again.");
        icon = ":/icons/network_disabled";
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");
    connectionsControl->setToolTip(tooltip);

    connectionsControl->setPixmap(platformStyle->SingleColorIcon(icon).pixmap(
        STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));

    if (m_lblPeersText) {
        m_lblPeersText->setText(
            m_node.getNetworkActive()
                ? tr("%n peer(s)", "", count)
                : tr("Offline"));
    }
}

void BitcoinGUI::setNumConnections(int count) {
    updateNetworkState();
}

void BitcoinGUI::setNetworkActive(bool networkActive) {
    updateNetworkState();
}

void BitcoinGUI::updateHeadersSyncProgressLabel() {
    int64_t headersTipTime = clientModel->getHeaderTipTime();
    int headersTipHeight = clientModel->getHeaderTipHeight();
    int estHeadersLeft =
        (GetTime() - headersTipTime) /
        config->GetChainParams().GetConsensus().nPowTargetSpacing;
    if (estHeadersLeft > HEADER_HEIGHT_DELTA_SYNC) {
        progressBarLabel->setText(
            tr("Syncing Headers (%1%)...")
                .arg(QLocale().toString(100.0 * headersTipHeight / (headersTipHeight + estHeadersLeft), 'f', 1)));
    }
}

void BitcoinGUI::openOptionsDialogWithTab(OptionsDialog::Tab tab) {
    if (!clientModel || !clientModel->getOptionsModel()) {
        return;
    }

    OptionsDialog dlg(this, enableWallet);
    dlg.setCurrentTab(tab);
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void BitcoinGUI::setNumBlocks(int count, const QDateTime &blockDate, const QString &,
                              double nVerificationProgress, bool header) {
    if (modalOverlay) {
        if (header) {
            modalOverlay->setKnownBestHeight(count, blockDate);
        } else {
            modalOverlay->tipUpdate(count, blockDate, nVerificationProgress);
        }
    }
    if (!clientModel) {
        return;
    }

    // Prevent orphan statusbar messages (e.g. hover Quit in main menu, wait
    // until chain-sync starts -> garbled text)
    statusBar()->clearMessage();

    // Acquire current block source
    enum BlockSource blockSource = clientModel->getBlockSource();
    switch (blockSource) {
        case BlockSource::NETWORK:
            if (header) {
                updateHeadersSyncProgressLabel();
                return;
            }
            progressBarLabel->setText(tr("Synchronizing with network..."));
            updateHeadersSyncProgressLabel();
            break;
        case BlockSource::DISK:
            if (header) {
                progressBarLabel->setText(tr("Indexing blocks on disk..."));
            } else {
                progressBarLabel->setText(tr("Processing blocks on disk..."));
            }
            break;
        case BlockSource::REINDEX:
            progressBarLabel->setText(tr("Reindexing blocks on disk..."));
            break;
        case BlockSource::NONE:
            if (header) {
                return;
            }
            progressBarLabel->setText(tr("Connecting to peers..."));
            break;
    }

    QString tooltip;

    QDateTime currentDate = QDateTime::currentDateTime();
    qint64 secs = blockDate.secsTo(currentDate);

    tooltip = tr("Processed %n block(s) of transaction history.", "", count);

    // Set icon state: spinning if catching up, tick otherwise
    if (secs < MAX_BLOCK_TIME_GAP) {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setPixmap(
            platformStyle->SingleColorIcon(":/icons/synced")
                .pixmap(STATUSBAR_ICONSIZE * 2, STATUSBAR_ICONSIZE * 2));

#ifdef ENABLE_WALLET
        if (walletFrame) {
            walletFrame->showOutOfSyncWarning(false);
            modalOverlay->showHide(true, true);
        }
#endif // ENABLE_WALLET

        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);
    } else {
        QString timeBehindText = GUIUtil::formatNiceTimeOffset(secs);

        progressBarLabel->setVisible(true);
        progressBar->setFormat(tr("%1 behind").arg(timeBehindText));
        progressBar->setMaximum(1000000000);
        progressBar->setValue(nVerificationProgress * 1000000000.0 + 0.5);
        progressBar->setVisible(true);

        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        if (count != prevBlocks) {
            labelBlocksIcon->setPixmap(
                platformStyle
                    ->SingleColorIcon(QString(":/movies/spinner-%1")
                                          .arg(spinnerFrame, 3, 10, QChar('0')))
                    .pixmap(STATUSBAR_ICONSIZE * 2, STATUSBAR_ICONSIZE * 2));
            spinnerFrame = (spinnerFrame + 1) % SPINNER_FRAMES;
        }
        prevBlocks = count;

#ifdef ENABLE_WALLET
        if (walletFrame) {
            walletFrame->showOutOfSyncWarning(true);
            modalOverlay->showHide();
        }
#endif // ENABLE_WALLET

        tooltip += QString("<br>");
        tooltip +=
            tr("Last received block was generated %1 ago.").arg(timeBehindText);
        tooltip += QString("<br>");
        tooltip += tr("Transactions after this will not yet be visible.");
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);

    if (m_lblBlocksText) {
        const QString formatted =
            QLocale::system().toString(qulonglong(count));
        // While the qubes-watch wallet is active the cockpit number belongs to
        // the Qube's memory height (driven by pollQubeMemory); don't clobber it
        // with the chain height. The fullscreen block clock still tracks chain.
        if (!m_qubeWatchActive) {
            m_lblBlocksText->setText(formatted);
        }
        if (m_blockFullscreenLabel) {
            m_blockFullscreenLabel->setText(formatted);
        }
        if (m_blockFullscreenLabelRed) {
            m_blockFullscreenLabelRed->setText(formatted);
        }
        if (m_blockFullscreenLabelBlue) {
            m_blockFullscreenLabelBlue->setText(formatted);
        }
        // Block-clock reaction: fire sound + shockwave animation on a
        // real +1 tip update when the user is watching the full-screen
        // counter. Filter out IBD spam (headers-only, <99.9% verified)
        // and the initial startup count.
        if (!header && nVerificationProgress > 0.999 &&
            m_lastBlockCount >= 0 && count > m_lastBlockCount) {
            // Record timestamp for the elapsed-since-last-block ticker
            // + colour gradient — regardless of whether the fullscreen
            // viewer is currently open, so it reflects reality on next
            // open.
            m_lastBlockTimeMs = QDateTime::currentMSecsSinceEpoch();
            if (m_blockFullscreen && m_blockFullscreen->isVisible()) {
                playRandomBlockSound();
                playBlockAnimation();
                updateBlockClockTick();
            }
        }
        m_lastBlockCount = count;
    }
}

void BitcoinGUI::message(const QString &title, const QString &message,
                         unsigned int style, bool *ret) {
    // default title
    QString strTitle = tr("Bitcoin");
    // Default to information icon
    int nMBoxIcon = QMessageBox::Information;
    int nNotifyIcon = Notificator::Information;

    QString msgType;

    // Prefer supplied title over style based title
    if (!title.isEmpty()) {
        msgType = title;
    } else {
        switch (style) {
            case CClientUIInterface::MSG_ERROR:
                msgType = tr("Error");
                break;
            case CClientUIInterface::MSG_WARNING:
                msgType = tr("Warning");
                break;
            case CClientUIInterface::MSG_INFORMATION:
                msgType = tr("Information");
                break;
            default:
                break;
        }
    }
    // Append title to "Bitcoin - "
    if (!msgType.isEmpty()) {
        strTitle += " - " + msgType;
    }

    // Check for error/warning icon
    if (style & CClientUIInterface::ICON_ERROR) {
        nMBoxIcon = QMessageBox::Critical;
        nNotifyIcon = Notificator::Critical;
    } else if (style & CClientUIInterface::ICON_WARNING) {
        nMBoxIcon = QMessageBox::Warning;
        nNotifyIcon = Notificator::Warning;
    }

    // Display message
    if (style & CClientUIInterface::MODAL) {
        // Check for buttons, use OK as default, if none was supplied
        QMessageBox::StandardButton buttons;
        if (!(buttons = (QMessageBox::StandardButton)(
                  style & CClientUIInterface::BTN_MASK))) {
            buttons = QMessageBox::Ok;
        }

        showNormalIfMinimized();
        QMessageBox mBox(static_cast<QMessageBox::Icon>(nMBoxIcon), strTitle,
                         message, buttons, this);
        mBox.setTextFormat(Qt::PlainText);
        int r = mBox.exec();
        if (ret != nullptr) {
            *ret = r == QMessageBox::Ok;
        }
    } else {
        notificator->notify(static_cast<Notificator::Class>(nNotifyIcon),
                            strTitle, message);
    }
}

void BitcoinGUI::changeEvent(QEvent *e) {
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if (e->type() == QEvent::WindowStateChange) {
        if (clientModel && clientModel->getOptionsModel() &&
            clientModel->getOptionsModel()->getMinimizeToTray()) {
            QWindowStateChangeEvent *wsevt =
                static_cast<QWindowStateChangeEvent *>(e);
            if (!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized()) {
                QTimer::singleShot(0, this, &BitcoinGUI::hide);
                e->ignore();
            } else if ((wsevt->oldState() & Qt::WindowMinimized) &&
                       !isMinimized()) {
                QTimer::singleShot(0, this, &BitcoinGUI::show);
                e->ignore();
            }
        }
    }
#endif
}

void BitcoinGUI::closeEvent(QCloseEvent *event) {
#ifndef Q_OS_MAC // Ignored on Mac
    if (clientModel && clientModel->getOptionsModel()) {
        if (!clientModel->getOptionsModel()->getMinimizeOnClose()) {
            // close rpcConsole in case it was open to make some space for the
            // shutdown window
            rpcConsole->close();

            QApplication::quit();
        } else {
            QMainWindow::showMinimized();
            event->ignore();
        }
    }
#else
    QMainWindow::closeEvent(event);
#endif
}

void BitcoinGUI::showEvent(QShowEvent *event) {
    // enable the debug window when the main window shows up
    openRPCConsoleAction->setEnabled(true);
    aboutAction->setEnabled(true);
    optionsAction->setEnabled(true);

    setWindowActionsEnabled(true);
}

#ifdef ENABLE_WALLET
void BitcoinGUI::incomingTransaction(const QString &date, int unit,
                                     const Amount amount, const QString &type,
                                     const QString &address,
                                     const QString &label,
                                     const QString &walletName) {
    // On new transaction, make an info balloon
    QString msg = tr("Date: %1\n").arg(date) +
                  tr("Amount: %1\n")
                      .arg(BitcoinUnits::formatWithUnit(unit, amount, true));
    if (m_node.getWallets().size() > 1 && !walletName.isEmpty()) {
        msg += tr("Wallet: %1\n").arg(walletName);
    }
    msg += tr("Type: %1\n").arg(type);
    if (!label.isEmpty()) {
        msg += tr("Label: %1\n").arg(label);
    } else if (!address.isEmpty()) {
        msg += tr("Address: %1\n").arg(address);
    }
    message(amount < Amount::zero() ? tr("Sent transaction")
                                    : tr("Incoming transaction"),
            msg, CClientUIInterface::MSG_INFORMATION);
}
#endif // ENABLE_WALLET

void BitcoinGUI::dragEnterEvent(QDragEnterEvent *event) {
    // Accept only URIs
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void BitcoinGUI::dropEvent(QDropEvent *event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &uri : event->mimeData()->urls()) {
            Q_EMIT receivedURI(uri.toString());
        }
    }
    event->acceptProposedAction();
}

bool BitcoinGUI::eventFilter(QObject *object, QEvent *event) {
    // Catch status tip events
    if (event->type() == QEvent::StatusTip) {
        // Prevent adding text from setStatusTip(), if we currently use the
        // status bar for displaying other stuff
        if (progressBarLabel->isVisible() || progressBar->isVisible()) {
            return true;
        }
    }
    // Double-click the network status panel → toggle the full-screen
    // block-count view (a "watch the blocks tick" mode).
    if (object == m_frameBlocks &&
        event->type() == QEvent::MouseButtonDblClick) {
        showBlockCountFullscreen();
        return true;
    }
    // Close the fullscreen viewer on left-click or Escape; right-click
    // opens the block-clock context menu (sounds toggle / folder / test).
    if (object == m_blockFullscreen) {
        auto hideAndStop = [this]() {
            if (m_blockClockTimer) m_blockClockTimer->stop();
            m_blockFullscreen->hide();
        };
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::RightButton) {
                showBlockClockContextMenu(me->globalPos());
                return true;
            }
            hideAndStop();
            return true;
        }
        if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Escape ||
                ke->key() == Qt::Key_Q ||
                ke->key() == Qt::Key_F11) {
                hideAndStop();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(object, event);
}

// Full-screen "pond ripple" overlay: transparent, mouse-through widget
// that paints one or more expanding rings from center outward. Driven
// externally by QVariantAnimation ticks calling setRadius().
class BlockShockwave : public QWidget {
public:
    explicit BlockShockwave(QWidget *parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
    }
    void setRadius(int r) { m_radius = r; update(); }
    void setMaxRadius(int r) { m_maxRadius = r; }

protected:
    void paintEvent(QPaintEvent *) override {
        if (m_radius <= 0) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        const QPoint center(width() / 2, height() / 2);
        const int maxR = m_maxRadius > 0 ? m_maxRadius
                                          : qMax(width(), height());
        auto alphaAt = [maxR](int r) {
            const qreal t = qreal(r) / qreal(maxR);
            return int(qBound(0.0, 220.0 * (1.0 - t), 220.0));
        };
        // Leading green ring — the primary shockwave.
        QPen pen1(QColor(10, 193, 142, alphaAt(m_radius)));
        pen1.setWidth(6);
        p.setPen(pen1);
        p.drawEllipse(center, m_radius, m_radius);
        // Purple trailing ring, offset behind by ~60px.
        if (m_radius > 60) {
            QPen pen2(QColor(157, 78, 221, alphaAt(m_radius - 60) / 2));
            pen2.setWidth(3);
            p.setPen(pen2);
            p.drawEllipse(center, m_radius - 60, m_radius - 60);
        }
        // Second green ring even further back, fainter — pond-ripple layering.
        if (m_radius > 130) {
            QPen pen3(QColor(10, 193, 142, alphaAt(m_radius - 130) / 3));
            pen3.setWidth(2);
            p.setPen(pen3);
            p.drawEllipse(center, m_radius - 130, m_radius - 130);
        }
    }

private:
    int m_radius = 0;
    int m_maxRadius = 0;
};

void BitcoinGUI::showBlockCountFullscreen() {
    if (!m_blockFullscreen) {
        m_blockFullscreen = new QWidget(nullptr);
        m_blockFullscreen->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        m_blockFullscreen->setStyleSheet(
            QStringLiteral("background: #000000;"));
        m_blockFullscreen->setCursor(Qt::PointingHandCursor);
        m_blockFullscreen->setAttribute(Qt::WA_DeleteOnClose, false);

        auto *v = new QVBoxLayout(m_blockFullscreen);
        v->setContentsMargins(0, 0, 0, 0);
        v->addStretch(1);

        // 1) Elapsed ticker on top. Uses OpticalCenterLabel which
        //    paints its ink centred inside its own widget rect, so
        //    it always aligns perfectly with the BLOCK caption below.
        {
            auto *l = new OpticalCenterLabel(m_blockFullscreen);
            QFont f;
            f.setFamilies({QStringLiteral("JetBrains Mono"),
                           QStringLiteral("Consolas"),
                           QStringLiteral("Courier New")});
            f.setPixelSize(18);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 8);
            l->setFont(f);
            l->setTextColor(QColor(0x4A, 0x54, 0x4F));
            v->addWidget(l);
            m_blockElapsedLabel = l;
        }

        // 2) BLOCK caption sits directly beneath the ticker.
        {
            auto *l = new OpticalCenterLabel(m_blockFullscreen);
            l->setText(tr("BLOCK"));
            QFont f;
            f.setFamilies({QStringLiteral("Rajdhani"),
                           QStringLiteral("Inter")});
            f.setPixelSize(32);
            f.setWeight(QFont::DemiBold);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 8);
            l->setFont(f);
            l->setTextColor(QColor(0x7B, 0x85, 0x7F));
            v->addSpacing(6);
            v->addWidget(l);
        }

        // 3) The huge block number.
        m_blockFullscreenLabel =
            new QLabel(m_lblBlocksText ? m_lblBlocksText->text() : QString(),
                       m_blockFullscreen);
        m_blockFullscreenLabel->setAlignment(Qt::AlignCenter);
        setBlockLabelScale(1.0);
        v->addSpacing(8);
        v->addWidget(m_blockFullscreenLabel, 0, Qt::AlignCenter);

        // RGB-split "chromatic aberration" clones — hidden by default,
        // faded in briefly by playBlockAnimation() for the digital
        // distortion beat. Children of the main label so they inherit
        // its position (including screen shake).
        auto makeRgbClone = [this](const QColor &c, int dx) -> QLabel * {
            auto *l = new QLabel(m_blockFullscreenLabel->text(),
                                 m_blockFullscreenLabel);
            l->setAlignment(Qt::AlignCenter);
            l->setStyleSheet(QStringLiteral(
                "QLabel { color: rgba(%1,%2,%3,255); background: transparent; "
                "font-family: 'Orbitron','JetBrains Mono',monospace; "
                "font-weight: 900; letter-spacing: 10px; }")
                    .arg(c.red()).arg(c.green()).arg(c.blue()));
            auto *op = new QGraphicsOpacityEffect(l);
            op->setOpacity(0.0);
            l->setGraphicsEffect(op);
            l->setAttribute(Qt::WA_TransparentForMouseEvents);
            (void)dx; // Positioned in playBlockAnimation() so it can shake.
            return l;
        };
        m_blockFullscreenLabelRed  = makeRgbClone(QColor(255, 60, 60), -8);
        m_blockFullscreenLabelBlue = makeRgbClone(QColor(60, 120, 255), 8);

        auto *hint = new QLabel(
            tr("Esc or click to exit  ·  Right-click for sounds + animations"),
            m_blockFullscreen);
        hint->setAlignment(Qt::AlignCenter);
        hint->setStyleSheet(QStringLiteral(
            "QLabel { color: #4A544F; background: transparent; "
            "font-family: 'Rajdhani','Inter',sans-serif; "
            "font-size: 14px; letter-spacing: 6px; }"));
        v->addSpacing(40);
        v->addWidget(hint, 0, Qt::AlignCenter);
        v->addStretch(1);

        // Full-screen shockwave overlay — sits above the layout, sized to
        // fill the widget. Raised to top so its rings paint over the number.
        m_blockShockwave = new BlockShockwave(m_blockFullscreen);
        m_blockShockwave->setGeometry(m_blockFullscreen->rect());
        m_blockShockwave->raise();

        // Timer drives the elapsed-time ticker + colour gradient. 500ms
        // is fast enough that the colour fade looks smooth and cheap
        // enough that per-tick stylesheet churn doesn't matter.
        m_blockClockTimer = new QTimer(m_blockFullscreen);
        m_blockClockTimer->setInterval(500);
        connect(m_blockClockTimer, &QTimer::timeout, this,
                &BitcoinGUI::updateBlockClockTick);

        // Click anywhere on the widget to close.
        m_blockFullscreen->installEventFilter(this);
    }
    // Sync all three label copies in case blocks arrived between opens.
    if (m_blockFullscreenLabel && m_lblBlocksText) {
        const QString t = m_lblBlocksText->text();
        m_blockFullscreenLabel->setText(t);
        if (m_blockFullscreenLabelRed)  m_blockFullscreenLabelRed->setText(t);
        if (m_blockFullscreenLabelBlue) m_blockFullscreenLabelBlue->setText(t);
    }
    // Multi-monitor: pick the screen saved in settings if it still
    // exists; otherwise fall back to the screen containing the parent
    // window, then primary.
    QScreen *chosenScreen = nullptr;
    {
        QSettings s;
        const QString wanted = s.value("blockClock/screenName").toString();
        for (auto *scr : QApplication::screens()) {
            if (scr->name() == wanted) { chosenScreen = scr; break; }
        }
        if (!chosenScreen) {
            chosenScreen = QApplication::screenAt(pos()) ?
                           QApplication::screenAt(pos()) :
                           QApplication::primaryScreen();
        }
    }
    if (chosenScreen) {
        m_blockFullscreen->setGeometry(chosenScreen->geometry());
        m_blockFullscreen->windowHandle()
            ? m_blockFullscreen->windowHandle()->setScreen(chosenScreen)
            : (void)0;
    }
    // Compute font size for the current chosen screen (recompute each
    // show so switching monitors resizes correctly).
    {
        QScreen *sizeScr = chosenScreen ? chosenScreen
                                        : QApplication::primaryScreen();
        if (sizeScr) {
            const QRect geo = sizeScr->geometry();
            int fsPx = int(geo.height() * 0.30);
            QFont probe(QStringLiteral("Orbitron"));
            probe.setPixelSize(fsPx);
            probe.setWeight(QFont::Black);
            probe.setLetterSpacing(QFont::AbsoluteSpacing, 10.0);
            QFontMetrics fm(probe);
            const int actualWidth = fm.horizontalAdvance("999,999");
            const int maxWidth = int(geo.width() * 0.85);
            if (actualWidth > maxWidth) {
                fsPx = int(qreal(fsPx) * qreal(maxWidth) / qreal(actualWidth));
            }
            m_baseBlockFontSize = fsPx;
            setBlockLabelScale(1.0);
        }
    }
    m_blockFullscreen->showFullScreen();
    // On some Qt/Windows combos setScreen only takes after the window
    // is shown — re-apply post-show to guarantee the correct display.
    if (chosenScreen && m_blockFullscreen->windowHandle()) {
        m_blockFullscreen->windowHandle()->setScreen(chosenScreen);
        m_blockFullscreen->setGeometry(chosenScreen->geometry());
    }
    if (m_blockClockTimer) m_blockClockTimer->start();
    updateBlockClockTick();
    // Now that the window has its final geometry, size the shockwave
    // overlay to match and align the RGB clones with the main label.
    if (m_blockShockwave) {
        m_blockShockwave->setGeometry(m_blockFullscreen->rect());
        m_blockShockwave->setMaxRadius(
            int(std::hypot(m_blockFullscreen->width(),
                            m_blockFullscreen->height()) / 2));
    }
    if (m_blockFullscreenLabel) {
        const QRect r = m_blockFullscreenLabel->rect();
        if (m_blockFullscreenLabelRed) {
            m_blockFullscreenLabelRed->setGeometry(r.translated(-8, 0));
        }
        if (m_blockFullscreenLabelBlue) {
            m_blockFullscreenLabelBlue->setGeometry(r.translated(8, 0));
        }
    }
    m_blockFullscreen->raise();
    m_blockFullscreen->activateWindow();
    m_blockFullscreen->setFocus();
}

void BitcoinGUI::setBlockLabelScale(qreal scale) {
    if (!m_blockFullscreenLabel) return;
    const int sz = int(m_baseBlockFontSize * scale);
    const QColor &c = m_currentBlockColor;
    m_blockFullscreenLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: rgb(%1,%2,%3); background: transparent; "
        "font-family: 'Orbitron','JetBrains Mono',monospace; "
        "font-size: %4px; font-weight: 900; letter-spacing: 10px; }")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(sz));
    if (m_blockFullscreenLabelRed) {
        m_blockFullscreenLabelRed->setStyleSheet(QStringLiteral(
            "QLabel { color: rgb(255,60,60); background: transparent; "
            "font-family: 'Orbitron','JetBrains Mono',monospace; "
            "font-size: %1px; font-weight: 900; letter-spacing: 10px; }")
                .arg(sz));
        m_blockFullscreenLabelRed->setGeometry(
            m_blockFullscreenLabel->rect().translated(-8, 0));
    }
    if (m_blockFullscreenLabelBlue) {
        m_blockFullscreenLabelBlue->setStyleSheet(QStringLiteral(
            "QLabel { color: rgb(60,120,255); background: transparent; "
            "font-family: 'Orbitron','JetBrains Mono',monospace; "
            "font-size: %1px; font-weight: 900; letter-spacing: 10px; }")
                .arg(sz));
        m_blockFullscreenLabelBlue->setGeometry(
            m_blockFullscreenLabel->rect().translated(8, 0));
    }
}

void BitcoinGUI::updateBlockClockTick() {
    if (!m_blockFullscreen || !m_blockFullscreen->isVisible()) return;
    // Elapsed since last observed new-block-with-tip event.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsedMs = m_lastBlockTimeMs > 0 ? (now - m_lastBlockTimeMs)
                                                   : 0;
    if (m_blockElapsedLabel) {
        QString txt;
        if (m_lastBlockTimeMs == 0) {
            txt = tr("— waiting for tip update —");
        } else {
            const qint64 s = elapsedMs / 1000;
            if (s < 60) {
                txt = tr("%1s SINCE").arg(s);
            } else if (s < 3600) {
                txt = tr("%1m %2s SINCE").arg(s / 60).arg(s % 60);
            } else {
                txt = tr("%1h %2m SINCE").arg(s / 3600).arg((s / 60) % 60);
            }
        }
        m_blockElapsedLabel->setText(txt);
    }
    // Colour gradient: green (#0AC18E) → yellow (#FFDC00) at 10min,
    // yellow → red (#E83C3C) at 20min. Beyond 20min stays solid red.
    const qint64 tenMin = 10 * 60 * 1000;
    QColor c;
    if (m_lastBlockTimeMs == 0 || elapsedMs <= 0) {
        c = QColor(10, 193, 142);
    } else if (elapsedMs <= tenMin) {
        const qreal t = qreal(elapsedMs) / qreal(tenMin);
        c = QColor(int(10 + (255 - 10) * t),
                   int(193 + (220 - 193) * t),
                   int(142 * (1.0 - t)));
    } else if (elapsedMs <= 2 * tenMin) {
        const qreal t = qreal(elapsedMs - tenMin) / qreal(tenMin);
        c = QColor(int(255 - (255 - 232) * t),
                   int(220 - 220 * t),
                   int(60 * t));
    } else {
        c = QColor(232, 60, 60);
    }
    if (c != m_currentBlockColor) {
        m_currentBlockColor = c;
        setBlockLabelScale(1.0);
    }
}

void BitcoinGUI::playBlockAnimation() {
    QSettings s;
    if (!s.value("blockClock/animationsEnabled", true).toBool()) {
        return;
    }
    if (!m_blockFullscreenLabel || !m_blockFullscreen->isVisible()) return;

    // 1. Screen shake — decaying wobble on the label position.
    {
        auto *ka = new QPropertyAnimation(m_blockFullscreenLabel, "pos",
                                          m_blockFullscreenLabel);
        const QPoint p0 = m_blockFullscreenLabel->pos();
        ka->setDuration(500);
        ka->setKeyValueAt(0.00, p0);
        ka->setKeyValueAt(0.08, p0 + QPoint( 18, -12));
        ka->setKeyValueAt(0.18, p0 + QPoint(-14,  10));
        ka->setKeyValueAt(0.30, p0 + QPoint( 10,  -8));
        ka->setKeyValueAt(0.45, p0 + QPoint( -7,   6));
        ka->setKeyValueAt(0.62, p0 + QPoint(  5,  -4));
        ka->setKeyValueAt(0.80, p0 + QPoint( -3,   2));
        ka->setEndValue(p0);
        ka->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // 2. Scale bump — grow to 112% then ease back with a slight bounce.
    {
        auto *sa = new QVariantAnimation(this);
        sa->setDuration(650);
        sa->setStartValue(1.0);
        sa->setKeyValueAt(0.30, 1.12);
        sa->setEndValue(1.0);
        sa->setEasingCurve(QEasingCurve::OutBack);
        connect(sa, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &v) { setBlockLabelScale(v.toReal()); });
        sa->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // 3. Glow flare — install a drop shadow on the label, animate blur
    //    radius up and down. Re-installed each time so it's fresh.
    {
        auto *glow = new QGraphicsDropShadowEffect(m_blockFullscreenLabel);
        glow->setColor(QColor(10, 193, 142, 220));
        glow->setOffset(0, 0);
        glow->setBlurRadius(0);
        m_blockFullscreenLabel->setGraphicsEffect(glow);
        auto *ga = new QVariantAnimation(m_blockFullscreenLabel);
        ga->setDuration(700);
        ga->setStartValue(0.0);
        ga->setKeyValueAt(0.25, 80.0);
        ga->setEndValue(0.0);
        connect(ga, &QVariantAnimation::valueChanged, this,
                [glow](const QVariant &v) {
                    glow->setBlurRadius(v.toReal());
                });
        ga->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // 4. Pond-ripple shockwave — expanding concentric rings from center.
    if (m_blockShockwave) {
        const int maxR = int(std::hypot(m_blockFullscreen->width(),
                                         m_blockFullscreen->height()) / 2);
        m_blockShockwave->setMaxRadius(maxR);
        m_blockShockwave->raise();
        auto *ra = new QVariantAnimation(m_blockShockwave);
        ra->setDuration(900);
        ra->setStartValue(0);
        ra->setEndValue(maxR);
        ra->setEasingCurve(QEasingCurve::OutQuart);
        connect(ra, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &v) {
                    if (m_blockShockwave) m_blockShockwave->setRadius(v.toInt());
                });
        connect(ra, &QVariantAnimation::finished, this, [this]() {
            if (m_blockShockwave) m_blockShockwave->setRadius(0);
        });
        ra->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // 5. Chromatic aberration flash — RGB split on the number.
    auto flashClone = [this](QLabel *l) {
        if (!l) return;
        auto *op = qobject_cast<QGraphicsOpacityEffect *>(l->graphicsEffect());
        if (!op) return;
        auto *oa = new QVariantAnimation(l);
        oa->setDuration(280);
        oa->setStartValue(0.0);
        oa->setKeyValueAt(0.35, 0.75);
        oa->setEndValue(0.0);
        connect(oa, &QVariantAnimation::valueChanged, l,
                [op](const QVariant &v) { op->setOpacity(v.toReal()); });
        oa->start(QAbstractAnimation::DeleteWhenStopped);
    };
    flashClone(m_blockFullscreenLabelRed);
    flashClone(m_blockFullscreenLabelBlue);
}

void BitcoinGUI::playRandomBlockSound() {
    QSettings s;
    if (!s.value("blockClock/soundsEnabled", false).toBool()) {
        return;
    }
    const QString folder = s.value("blockClock/soundsFolder").toString();
    if (folder.isEmpty()) {
        QApplication::beep();
        return;
    }
    QDir d(folder);
    QStringList files = d.entryList({"*.wav", "*.WAV"}, QDir::Files);
    if (files.isEmpty()) {
        QApplication::beep();
        return;
    }
    // Avoid playing the same file twice in a row when we have more than
    // one to choose from.
    if (files.size() > 1 && !m_lastBlockSoundFile.isEmpty()) {
        files.removeAll(m_lastBlockSoundFile);
    }
    const QString picked =
        files.at(QRandomGenerator::global()->bounded(files.size()));
    m_lastBlockSoundFile = picked;
    const QString path = d.absoluteFilePath(picked);
#if defined(Q_OS_WIN)
    // Windows: PlaySound with SND_ASYNC | SND_FILENAME. Path must be a
    // wide-char null-terminated string.
    PlaySoundW(reinterpret_cast<LPCWSTR>(path.utf16()), NULL,
               SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
#elif defined(Q_OS_MAC)
    QProcess::startDetached("/usr/bin/afplay", {path});
#else
    // Linux: try paplay (PulseAudio) then aplay (ALSA) as fallback.
    if (!QProcess::startDetached("paplay", {path})) {
        QProcess::startDetached("aplay", {"-q", path});
    }
#endif
}

void BitcoinGUI::showBlockClockContextMenu(const QPoint &globalPos) {
    if (!m_blockFullscreen) return;
    QSettings s;
    const bool enabled = s.value("blockClock/soundsEnabled", false).toBool();
    const QString folder = s.value("blockClock/soundsFolder").toString();

    QMenu menu(m_blockFullscreen);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: #17221D; color: #E8ECEA; "
        "border: 1px solid rgba(157,78,221,90); padding: 6px; } "
        "QMenu::item { padding: 8px 20px; } "
        "QMenu::item:selected { background: rgba(10,193,142,40); }"));

    QAction *actEnable = menu.addAction(tr("Enable block-found sounds"));
    actEnable->setCheckable(true);
    actEnable->setChecked(enabled);

    QAction *actFolder = menu.addAction(
        folder.isEmpty() ? tr("Choose sounds folder…")
                         : tr("Sounds folder: %1").arg(QDir(folder).dirName()));

    QAction *actTest = menu.addAction(tr("Test random sound"));
    actTest->setEnabled(!folder.isEmpty());

    menu.addSeparator();
    const bool animEnabled =
        s.value("blockClock/animationsEnabled", true).toBool();
    QAction *actAnim = menu.addAction(tr("Enable block-found animations"));
    actAnim->setCheckable(true);
    actAnim->setChecked(animEnabled);

    QAction *actTestAnim = menu.addAction(tr("Test animation"));

    // Multi-monitor selector — only add when 2+ screens are attached.
    menu.addSeparator();
    QList<QScreen *> screens = QApplication::screens();
    const QString currentScreenName =
        s.value("blockClock/screenName",
                m_blockFullscreen->screen()
                    ? m_blockFullscreen->screen()->name()
                    : QString()).toString();
    QMenu *screenMenu = nullptr;
    QList<QAction *> screenActions;
    if (screens.size() > 1) {
        screenMenu = menu.addMenu(tr("Show on display"));
        auto *grp = new QActionGroup(screenMenu);
        grp->setExclusive(true);
        for (auto *scr : screens) {
            const QRect g = scr->geometry();
            QAction *a = screenMenu->addAction(
                QStringLiteral("%1  (%2×%3)")
                    .arg(scr->name()).arg(g.width()).arg(g.height()));
            a->setCheckable(true);
            a->setChecked(scr->name() == currentScreenName);
            grp->addAction(a);
            screenActions.append(a);
        }
    }

    QAction *chosen = menu.exec(globalPos);
    if (!chosen) return;

    // Multi-monitor: move fullscreen to the picked display.
    for (int i = 0; i < screenActions.size(); ++i) {
        if (chosen == screenActions[i]) {
            QScreen *scr = screens.at(i);
            s.setValue("blockClock/screenName", scr->name());
            m_blockFullscreen->setGeometry(scr->geometry());
            if (m_blockFullscreen->windowHandle()) {
                m_blockFullscreen->windowHandle()->setScreen(scr);
            }
            m_blockFullscreen->showFullScreen();
            m_blockFullscreen->raise();
            return;
        }
    }

    if (chosen == actAnim) {
        s.setValue("blockClock/animationsEnabled", actAnim->isChecked());
    } else if (chosen == actTestAnim) {
        const bool wasEnabled = animEnabled;
        if (!wasEnabled) s.setValue("blockClock/animationsEnabled", true);
        playBlockAnimation();
        if (!wasEnabled) s.setValue("blockClock/animationsEnabled", false);
    } else if (chosen == actEnable) {
        s.setValue("blockClock/soundsEnabled", actEnable->isChecked());
    } else if (chosen == actFolder) {
        const QString start = folder.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
            : folder;
        const QString picked = QFileDialog::getExistingDirectory(
            m_blockFullscreen, tr("Choose block sounds folder"), start);
        if (!picked.isEmpty()) {
            s.setValue("blockClock/soundsFolder", picked);
            // Auto-enable when the user picks a folder for the first time.
            if (!enabled) s.setValue("blockClock/soundsEnabled", true);
        }
    } else if (chosen == actTest) {
        // Temporarily force-enable so playRandomBlockSound() plays.
        const bool wasEnabled = enabled;
        if (!wasEnabled) s.setValue("blockClock/soundsEnabled", true);
        playRandomBlockSound();
        if (!wasEnabled) s.setValue("blockClock/soundsEnabled", false);
    }
}

#ifdef Q_OS_WIN
bool BitcoinGUI::nativeEvent(const QByteArray &eventType, void *message,
                             long *result) {
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_NCHITTEST && !isMaximized() && !isFullScreen()) {
            RECT winRect;
            GetWindowRect(reinterpret_cast<HWND>(winId()), &winRect);
            LONG x = GET_X_LPARAM(msg->lParam);
            LONG y = GET_Y_LPARAM(msg->lParam);
            const int margin = int(6 * devicePixelRatioF());
            const bool left  = x >= winRect.left && x < winRect.left + margin;
            const bool right = x >= winRect.right - margin && x < winRect.right;
            const bool top   = y >= winRect.top && y < winRect.top + margin;
            const bool bot   = y >= winRect.bottom - margin && y < winRect.bottom;
            if (top && left)   { *result = HTTOPLEFT;    return true; }
            if (top && right)  { *result = HTTOPRIGHT;   return true; }
            if (bot && left)   { *result = HTBOTTOMLEFT; return true; }
            if (bot && right)  { *result = HTBOTTOMRIGHT;return true; }
            if (left)          { *result = HTLEFT;       return true; }
            if (right)         { *result = HTRIGHT;      return true; }
            if (top)           { *result = HTTOP;        return true; }
            if (bot)           { *result = HTBOTTOM;     return true; }
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

#ifdef ENABLE_WALLET
bool BitcoinGUI::handlePaymentRequest(const SendCoinsRecipient &recipient) {
    // URI has to be valid
    if (walletFrame && walletFrame->handlePaymentRequest(recipient)) {
        showNormalIfMinimized();
        gotoSendCoinsPage();
        return true;
    }
    return false;
}

void BitcoinGUI::setHDStatus(int hdEnabled) {
    labelWalletHDStatusIcon->setPixmap(
        platformStyle
            ->SingleColorIcon(hdEnabled ? ":/icons/hd_enabled"
                                        : ":/icons/hd_disabled")
            .pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
    labelWalletHDStatusIcon->setToolTip(
        hdEnabled ? tr("HD key generation is <b>enabled</b>")
                  : tr("HD key generation is <b>disabled</b>"));

    // eventually disable the QLabel to set its opacity to 50%
    labelWalletHDStatusIcon->setEnabled(hdEnabled);
}

void BitcoinGUI::setEncryptionStatus(int status) {
    switch (status) {
        case WalletModel::Unencrypted:
            labelWalletEncryptionIcon->hide();
            encryptWalletAction->setChecked(false);
            changePassphraseAction->setEnabled(false);
            encryptWalletAction->setEnabled(true);
            break;
        case WalletModel::Unlocked:
            labelWalletEncryptionIcon->show();
            labelWalletEncryptionIcon->setPixmap(
                platformStyle->SingleColorIcon(":/icons/lock_open")
                    .pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
            labelWalletEncryptionIcon->setToolTip(
                tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
            encryptWalletAction->setChecked(true);
            changePassphraseAction->setEnabled(true);
            encryptWalletAction->setEnabled(
                false); // TODO: decrypt currently not supported
            break;
        case WalletModel::Locked:
            labelWalletEncryptionIcon->show();
            labelWalletEncryptionIcon->setPixmap(
                platformStyle->SingleColorIcon(":/icons/lock_closed")
                    .pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
            labelWalletEncryptionIcon->setToolTip(
                tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
            encryptWalletAction->setChecked(true);
            changePassphraseAction->setEnabled(true);
            encryptWalletAction->setEnabled(
                false); // TODO: decrypt currently not supported
            break;
    }
}

void BitcoinGUI::updateWalletStatus() {
    if (!walletFrame) {
        return;
    }
    WalletView *const walletView = walletFrame->currentWalletView();
    if (!walletView) {
        return;
    }
    WalletModel *const walletModel = walletView->getWalletModel();
    setEncryptionStatus(walletModel->getEncryptionStatus());
    setHDStatus(walletModel->wallet().hdEnabled());
    if (m_lblWalletText) {
        QString label;
        switch (walletModel->getEncryptionStatus()) {
            case WalletModel::Unencrypted: label = tr("Unencrypted"); break;
            case WalletModel::Unlocked:    label = tr("Unlocked");    break;
            case WalletModel::Locked:      label = tr("Locked");      break;
        }
        if (walletModel->wallet().hdEnabled()) {
            label += QStringLiteral(" \u00B7 HD");
        }
        m_lblWalletText->setText(label);
    }
}
#endif // ENABLE_WALLET

void BitcoinGUI::updateProxyIcon() {
    std::string ip_port;
    bool proxy_enabled = clientModel->getProxyInfo(ip_port);

    if (proxy_enabled) {
        if (!labelProxyIcon->hasPixmap()) {
            QString ip_port_q = QString::fromStdString(ip_port);
            labelProxyIcon->setPixmap(
                platformStyle->SingleColorIcon(":/icons/proxy")
                    .pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
            labelProxyIcon->setToolTip(
                tr("Proxy is <b>enabled</b>: %1").arg(ip_port_q));
        } else {
            labelProxyIcon->show();
        }
    } else {
        labelProxyIcon->hide();
    }
}

void BitcoinGUI::updateWindowTitle() {
    QString window_title = PACKAGE_NAME;
    const QString &titleAddText = m_network_style->getTitleAddText();
    if (!titleAddText.isEmpty()) {
        window_title += " " + titleAddText;
    }
#ifdef ENABLE_WALLET
    if (walletFrame) {
        WalletModel *const wallet_model = walletFrame->currentWalletModel();
        const QString walletName =
            wallet_model ? wallet_model->getWalletName() : QString();
        if (wallet_model && !wallet_model->getWalletName().isEmpty()) {
            window_title += " - " + wallet_model->getDisplayName();
        }
        // The active wallet drives whether the cockpit clock shows the chain
        // height or the living Qube's memory height.
        applyQubeWatchMode(walletName);
    }
#endif
    setWindowTitle(window_title);
    if (m_titleBar) {
        m_titleBar->setTitle(window_title);
    }
}

// Switch the cockpit clock between chain height (BLOCK) and the active Qube's
// memory height (MEMORY), driven by whether the qubes-watch wallet is selected.
void BitcoinGUI::applyQubeWatchMode(const QString &walletName) {
    const bool active = (walletName == QStringLiteral("qubes-watch"));
    if (active == m_qubeWatchActive) {
        return; // no change
    }
    m_qubeWatchActive = active;
    if (active) {
        if (m_blockCaption) {
            m_blockCaption->setText(tr("MEMORY"));
        }
        if (m_lblBlocksText) {
            m_lblBlocksText->setText(QStringLiteral("…"));
        }
        pollQubeMemory(); // immediate first read, then on the timer
        if (m_qubeMemTimer) {
            m_qubeMemTimer->start();
        }
    } else {
        if (m_qubeMemTimer) {
            m_qubeMemTimer->stop();
        }
        if (m_blockCaption) {
            m_blockCaption->setText(tr("BLOCK"));
        }
        // Restore the chain height we last saw (setNumBlocks kept tracking it).
        if (m_lblBlocksText && m_lastBlockCount >= 0) {
            m_lblBlocksText->setText(
                QLocale::system().toString(qulonglong(m_lastBlockCount)));
        }
    }
}

// Poll the local qubed for the active Qube's memory-chain height. Uses a raw
// QTcpSocket (not QNetworkAccessManager, which on this static Windows build
// fails even a listening-localhost request via the system proxy/bearer layer):
// a one-shot HTTP/1.0 GET, parsed when the server closes the connection.
// `alive` shows the height; `locked` (up, vault sealed) shows LOCKED; a refused
// or timed-out connection (qubed not running) shows a dash.
void BitcoinGUI::pollQubeMemory() {
    auto *sock = new QTcpSocket(this);
    // Fail-safe: if nothing resolves in 1.5s (e.g. connection refused), show a
    // dash and tear down. Parented to the socket so it dies with it.
    auto *deadline = new QTimer(sock);
    deadline->setSingleShot(true);

    auto apply = [this](const QString &text) {
        if (m_qubeWatchActive && m_lblBlocksText) {
            m_lblBlocksText->setText(text);
        }
    };

    connect(sock, &QTcpSocket::connected, this, [sock]() {
        sock->write("GET /v1/blockclock HTTP/1.0\r\n"
                    "Host: 127.0.0.1\r\n"
                    "Connection: close\r\n\r\n");
    });
    // HTTP/1.0 + Connection: close → the server closes when done; read it all.
    connect(sock, &QTcpSocket::disconnected, this, [this, sock, apply]() {
        const QByteArray resp = sock->readAll();
        const int sep = resp.indexOf("\r\n\r\n");
        QString out = QStringLiteral("—");
        if (sep >= 0) {
            const QJsonObject obj =
                QJsonDocument::fromJson(resp.mid(sep + 4)).object();
            const QString state = obj.value(QStringLiteral("state")).toString();
            if (state == QStringLiteral("alive") &&
                obj.contains(QStringLiteral("height"))) {
                out = QLocale::system().toString(static_cast<qulonglong>(
                    obj.value(QStringLiteral("height")).toDouble()));
            } else {
                out = tr("LOCKED"); // up but vault sealed
            }
        }
        apply(out);
        sock->deleteLater();
    });
    connect(deadline, &QTimer::timeout, this, [sock, apply]() {
        apply(QStringLiteral("—")); // qubed not answering
        sock->abort();
        sock->deleteLater();
    });

    sock->connectToHost(QStringLiteral("127.0.0.1"), 8787);
    deadline->start(1500);
}

void BitcoinGUI::showNormalIfMinimized(bool fToggleHidden) {
    if (!clientModel) {
        return;
    }

    if (!isHidden() && !isMinimized() && !GUIUtil::isObscured(this) &&
        fToggleHidden) {
        hide();
    } else {
        GUIUtil::bringToFront(this);
    }
}

void BitcoinGUI::toggleHidden() {
    showNormalIfMinimized(true);
}

void BitcoinGUI::detectShutdown() {
    if (m_node.shutdownRequested()) {
        if (rpcConsole) {
            rpcConsole->hide();
        }
        qApp->quit();
    }
}

void BitcoinGUI::showProgress(const QString &title, int nProgress) {
    nProgress = std::clamp(nProgress, 0, 100); // ensure valid range
    if (nProgress < 100) { // creation & normal usage for any value <100
        if (!progressDialog) {
            progressDialog = new QProgressDialog(title, "", 0, 100);
            progressDialog->setWindowModality(Qt::ApplicationModal);
            progressDialog->setMinimumDuration(0);
            progressDialog->setCancelButton(nullptr);
            progressDialog->setAutoClose(false);
        }
        progressDialog->setValue(nProgress);
    } else if (progressDialog) { // nProgress >= 100, delete progressDialog
        progressDialog->close();
        progressDialog->deleteLater();
        progressDialog = nullptr;
    }
}

void BitcoinGUI::setTrayIconVisible(bool fHideTrayIcon) {
    if (trayIcon) {
        trayIcon->setVisible(!fHideTrayIcon);
    }
}

void BitcoinGUI::showModalOverlay() {
    if (modalOverlay &&
        (progressBar->isVisible() || modalOverlay->isLayerVisible())) {
        modalOverlay->toggleVisibility();
    }
}

void BitcoinGUI::setWindowActionsEnabled(bool enabled) {
    if (m_main_window_action != nullptr) {
        m_main_window_action->setEnabled(enabled);
    }

    for (QAction *action : m_node_actions) {
         action->setEnabled(enabled);
    }
}

static bool ThreadSafeMessageBox(BitcoinGUI *gui, const std::string &message,
                                 const std::string &caption,
                                 unsigned int style) {
    bool modal = (style & CClientUIInterface::MODAL);
    // The SECURE flag has no effect in the Qt GUI.
    // bool secure = (style & CClientUIInterface::SECURE);
    style &= ~CClientUIInterface::SECURE;
    bool ret = false;
    // In case of modal message, use blocking connection to wait for user to
    // click a button
    QMetaObject::invokeMethod(gui, "message",
                              modal ? GUIUtil::blockingGUIThreadConnection()
                                    : Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(caption)),
                              Q_ARG(QString, QString::fromStdString(message)),
                              Q_ARG(unsigned int, style), Q_ARG(bool *, &ret));
    return ret;
}

void BitcoinGUI::subscribeToCoreSignals() {
    // Connect signals to client
    m_handler_message_box = m_node.handleMessageBox(
        std::bind(ThreadSafeMessageBox, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3));
    m_handler_question = m_node.handleQuestion(
        std::bind(ThreadSafeMessageBox, this, std::placeholders::_1,
                  std::placeholders::_3, std::placeholders::_4));
}

void BitcoinGUI::unsubscribeFromCoreSignals() {
    // Disconnect signals from client
    m_handler_message_box->disconnect();
    m_handler_question->disconnect();
}

UnitDisplayStatusBarControl::UnitDisplayStatusBarControl(
    const PlatformStyle *platformStyle)
    : optionsModel(nullptr), menu(nullptr) {
    createContextMenu();
    setToolTip(tr("Unit to show amounts in. Click to select another unit."));
    QList<BitcoinUnits::Unit> units = BitcoinUnits::availableUnits();
    int max_width = 0;
    const QFontMetrics fm(font());
    for (const BitcoinUnits::Unit unit : units) {
        max_width = qMax(max_width, GUIUtil::TextWidth(fm, BitcoinUnits::ticker(unit)));
    }
    setMinimumSize(max_width, 0);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setStyleSheet(QString("QLabel { color : %1 }")
                      .arg(platformStyle->SingleColor().name()));
}

/** So that it responds to button clicks */
void UnitDisplayStatusBarControl::mousePressEvent(QMouseEvent *event) {
    onDisplayUnitsClicked(event->pos());
}

/** Creates context menu, its actions, and wires up all the relevant signals for
 * mouse events. */
void UnitDisplayStatusBarControl::createContextMenu() {
    menu = new QMenu(this);
    for (const BitcoinUnits::Unit u : BitcoinUnits::availableUnits()) {
        QAction *menuAction = new QAction(BitcoinUnits::ticker(u), this);
        menuAction->setStatusTip(tr("Change unit to %1").arg(BitcoinUnits::description(u)));
        menuAction->setToolTip(menuAction->statusTip());
        menuAction->setData(QVariant(u));
        menu->addAction(menuAction);
    }
    connect(menu, &QMenu::triggered, this,
            &UnitDisplayStatusBarControl::onMenuSelection);
}

/** Lets the control know about the Options Model (and its signals) */
void UnitDisplayStatusBarControl::setOptionsModel(OptionsModel *_optionsModel) {
    if (_optionsModel) {
        this->optionsModel = _optionsModel;

        // be aware of a display unit change reported by the OptionsModel
        // object.
        connect(_optionsModel, &OptionsModel::displayUnitChanged, this,
                &UnitDisplayStatusBarControl::updateDisplayUnit);

        // initialize the display units label with the current value in the
        // model.
        updateDisplayUnit(_optionsModel->getDisplayUnit());
    }
}

/** When Display Units are changed on OptionsModel it will refresh the display
 * text of the control on the status bar */
void UnitDisplayStatusBarControl::updateDisplayUnit(int newUnits) {
    setText(BitcoinUnits::ticker(newUnits));
}

/** Shows context menu with Display Unit options by the mouse coordinates */
void UnitDisplayStatusBarControl::onDisplayUnitsClicked(const QPoint &point) {
    QPoint globalPos = mapToGlobal(point);
    menu->exec(globalPos);
}

/** Tells underlying optionsModel to update its current display unit. */
void UnitDisplayStatusBarControl::onMenuSelection(QAction *action) {
    if (action) {
        optionsModel->setDisplayUnit(action->data());
    }
}
