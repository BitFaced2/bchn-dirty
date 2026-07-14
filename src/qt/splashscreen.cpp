// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2020-2021 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <qt/splashscreen.h>

#include <clientversion.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <qt/guiutil.h>
#include <qt/networkstyle.h>
#include <ui_interface.h>
#include <util/system.h>
#include <version.h>

#include <QApplication>
#include <QCloseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QScreen>

#include <memory>

SplashScreen::SplashScreen(interfaces::Node &node,
                           const NetworkStyle *networkStyle)
    : QWidget(nullptr), curAlignment(0), m_node(node) {
    float devicePixelRatio = 1.0;
#if QT_VERSION > 0x050100
    devicePixelRatio = static_cast<QGuiApplication *>(QCoreApplication::instance())->devicePixelRatio();
#endif

    QString titleText = PACKAGE_NAME;
    QString versionText = QString::fromStdString(FormatFullVersion());
    QString titleAddText = networkStyle->getTitleAddText();

    const QString uiFontName = QStringLiteral("Rajdhani");
    const QString monoFontName = QStringLiteral("JetBrains Mono");

    // Square canvas so the logo (which is square) fills nicely.
    const int splashPx = 480;
    QSize splashSize(splashPx * devicePixelRatio, splashPx * devicePixelRatio);
    pixmap = QPixmap(splashSize);

#if QT_VERSION > 0x050100
    pixmap.setDevicePixelRatio(devicePixelRatio);
#endif

    QPainter pixPaint(&pixmap);
    pixPaint.setRenderHint(QPainter::Antialiasing, true);
    pixPaint.setRenderHint(QPainter::SmoothPixmapTransform, true);
    pixPaint.setRenderHint(QPainter::TextAntialiasing, true);

    // Fill with the logo scaled to cover the entire canvas.
    QPixmap logo(QStringLiteral(":/icons/bitcoin_splash"));
    QPixmap scaled = logo.scaled(splashSize, Qt::KeepAspectRatioByExpanding,
                                 Qt::SmoothTransformation);
    // center-crop if needed
    QRect crop((scaled.width() - splashSize.width()) / 2,
               (scaled.height() - splashSize.height()) / 2,
               splashSize.width(), splashSize.height());
    pixPaint.drawPixmap(QRect(0, 0, splashPx, splashPx), scaled, crop);

    // Version text at top-right, over the logo's dark top area.
    QFont versionFont(monoFontName);
    versionFont.setPointSizeF(10);
    versionFont.setWeight(QFont::Medium);
    versionFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    pixPaint.setFont(versionFont);
    pixPaint.setPen(QColor(0x7B, 0x85, 0x7F, 220));
    QFontMetrics fm = pixPaint.fontMetrics();
    int versionWidth = GUIUtil::TextWidth(fm, versionText);
    pixPaint.drawText(splashPx - versionWidth - 16, 24, versionText);

    // Network label (testnet, chipnet, etc.) — top-left, purple.
    if (!titleAddText.isEmpty()) {
        QFont addFont(uiFontName);
        addFont.setPointSizeF(11);
        addFont.setWeight(QFont::Bold);
        addFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
        pixPaint.setFont(addFont);
        pixPaint.setPen(QColor(0x9D, 0x4E, 0xDD));
        pixPaint.drawText(16, 24, titleAddText);
    }

    pixPaint.end();

    setWindowTitle(titleAddText.isEmpty() ? titleText : titleText + " " + titleAddText);

    // Resize window and move to center of desktop, disallow resizing
    QRect r(QPoint(), QSize(pixmap.size().width() / devicePixelRatio, pixmap.size().height() / devicePixelRatio));
    resize(r.size());
    setFixedSize(r.size());
    move(QGuiApplication::primaryScreen()->geometry().center() - r.center());

    subscribeToCoreSignals();
    installEventFilter(this);
}

SplashScreen::~SplashScreen() {
    unsubscribeFromCoreSignals();
}

bool SplashScreen::eventFilter(QObject *obj, QEvent *ev) {
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
        if (keyEvent->key() == Qt::Key_Q) {
            m_node.startShutdown();
        }
    }
    return QObject::eventFilter(obj, ev);
}

void SplashScreen::slotFinish(QWidget *mainWin) {
    Q_UNUSED(mainWin);

    /* If the window is minimized, hide() will be ignored. */
    /* Make sure we de-minimize the splashscreen window before hiding */
    if (isMinimized()) {
        showNormal();
    }
    hide();
    // No more need for this
    deleteLater();
}

static void InitMessage(SplashScreen *splash, const std::string &message) {
    QMetaObject::invokeMethod(splash, "showMessage", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(message)),
                              Q_ARG(int, Qt::AlignBottom | Qt::AlignHCenter),
                              Q_ARG(QColor, QColor(0xD9, 0xD9, 0xD9)));
}

static void ShowProgress(SplashScreen *splash, const std::string &title,
                         int nProgress, bool resume_possible) {
    InitMessage(splash, title + std::string("\n") +
                            (resume_possible
                                 ? _("(press q to shutdown and continue later)")
                                 : _("press q to shutdown")) +
                            strprintf("\n%d", nProgress) + "%");
}
#ifdef ENABLE_WALLET
void SplashScreen::ConnectWallet(std::unique_ptr<interfaces::Wallet> wallet) {
    m_connected_wallet_handlers.emplace_back(wallet->handleShowProgress(
        std::bind(ShowProgress, this, std::placeholders::_1,
                  std::placeholders::_2, false)));
    m_connected_wallets.emplace_back(std::move(wallet));
}
#endif

void SplashScreen::subscribeToCoreSignals() {
    // Connect signals to client
    m_handler_init_message = m_node.handleInitMessage(
        std::bind(InitMessage, this, std::placeholders::_1));
    m_handler_show_progress = m_node.handleShowProgress(
        std::bind(ShowProgress, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3));
#ifdef ENABLE_WALLET
    m_handler_load_wallet = m_node.handleLoadWallet(
        [this](std::unique_ptr<interfaces::Wallet> wallet) {
            ConnectWallet(std::move(wallet));
        });
#endif
}

void SplashScreen::unsubscribeFromCoreSignals() {
    // Disconnect signals from client
    m_handler_init_message->disconnect();
    m_handler_show_progress->disconnect();
    for (const auto &handler : m_connected_wallet_handlers) {
        handler->disconnect();
    }
    m_connected_wallet_handlers.clear();
    m_connected_wallets.clear();
}

void SplashScreen::showMessage(const QString &message, int alignment,
                               const QColor &color) {
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(20, 5, -20, -18);
    QFont msgFont(QStringLiteral("Rajdhani"));
    msgFont.setPointSize(10);
    msgFont.setWeight(QFont::Medium);
    msgFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    painter.setFont(msgFont);
    painter.setPen(QColor(0x0A, 0xC1, 0x8E));
    painter.drawText(r, Qt::AlignHCenter | Qt::AlignBottom, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event) {
    // allows an "emergency" shutdown during startup
    m_node.startShutdown();
    event->ignore();
}
