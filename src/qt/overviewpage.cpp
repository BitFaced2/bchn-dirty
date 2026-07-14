// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/forms/ui_overviewpage.h>
#include <qt/overviewpage.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/receivecoinsdialog.h>
#include <qt/sendcoinsdialog.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>

#ifdef USE_QRCODE
#include <qrencode.h>
#endif

#include <QAbstractItemDelegate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QVector>
#include <QPainter>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#define DECORATION_SIZE 32
#define ROW_HEIGHT 62
#define NUM_ITEMS 15

Q_DECLARE_METATYPE(interfaces::WalletBalances)

class TxViewDelegate : public QAbstractItemDelegate {
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle *_platformStyle,
                            QObject *parent = nullptr)
        : QAbstractItemDelegate(parent), unit(BitcoinUnits::BCH),
          platformStyle(_platformStyle) {}

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index) const {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setRenderHint(QPainter::TextAntialiasing, true);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRect fullRect = option.rect;
        const QRect cardRect = fullRect.adjusted(4, 3, -4, -3);

        // Hover / selection: subtle purple-tinted rounded card
        if (option.state & QStyle::State_Selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(10, 193, 142, 34));
            painter->drawRoundedRect(cardRect, 10, 10);
        } else if (option.state & QStyle::State_MouseOver) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(157, 78, 221, 28));
            painter->drawRoundedRect(cardRect, 10, 10);
        }

        // Icon on the left, vertically centered
        const int iconSize = DECORATION_SIZE;
        QRect iconRect(cardRect.left() + 12,
                       cardRect.top() + (cardRect.height() - iconSize) / 2,
                       iconSize, iconSize);
        QIcon icon = qvariant_cast<QIcon>(
            index.data(TransactionTableModel::RawDecorationRole));
        icon = platformStyle->SingleColorIcon(icon);
        icon.paint(painter, iconRect);

        // Text content area
        const int xleft = iconRect.right() + 14;
        const int xright = cardRect.right() - 12;
        const int textAreaHeight = cardRect.height() - 12;
        const int topY = cardRect.top() + 6;
        const int lineH = textAreaHeight / 2;
        QRect topLine(xleft, topY, xright - xleft, lineH);
        QRect botLine(xleft, topY + lineH, xright - xleft, lineH);

        // Data
        QDateTime date =
            index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        Amount amount(int64_t(
                          index.data(TransactionTableModel::AmountRole)
                              .toLongLong()) *
                      SATOSHI);
        bool confirmed =
            index.data(TransactionTableModel::ConfirmedRole).toBool();

        // Top line: date (left, Rajdhani muted) + amount (right, JB Mono)
        QFont dateFont(QStringLiteral("Rajdhani"));
        dateFont.setPointSize(9);
        dateFont.setWeight(QFont::Medium);
        dateFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        painter->setFont(dateFont);
        painter->setPen(QColor(0x7B, 0x85, 0x7F));
        painter->drawText(topLine, Qt::AlignLeft | Qt::AlignVCenter,
                          GUIUtil::dateTimeStr(date));

        QColor amountColor;
        if (amount < Amount::zero()) {
            amountColor = COLOR_NEGATIVE;
        } else if (!confirmed) {
            amountColor = COLOR_UNCONFIRMED;
        } else {
            amountColor = QColor(0x0A, 0xC1, 0x8E);
        }
        QFont amountFont(QStringLiteral("JetBrains Mono"));
        amountFont.setPointSize(10);
        amountFont.setWeight(QFont::Medium);
        painter->setFont(amountFont);
        painter->setPen(amountColor);
        QString amountText = BitcoinUnits::formatWithUnit(
            unit, amount, true, BitcoinUnits::separatorAlways);
        if (!confirmed) {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(topLine, Qt::AlignRight | Qt::AlignVCenter,
                          amountText);

        // Bottom line: address (JB Mono, purple, elided)
        QFont addrFont(QStringLiteral("JetBrains Mono"));
        addrFont.setPointSize(8);
        painter->setFont(addrFont);
        painter->setPen(QColor(0x9D, 0x4E, 0xDD));
        QFontMetrics fm(addrFont);
        QRect addrDrawRect = botLine;
        // Reserve room for the watch-only icon on the right if present
        bool isWatch = index.data(TransactionTableModel::WatchonlyRole).toBool();
        if (isWatch) addrDrawRect.setRight(xright - 22);
        QString elided = fm.elidedText(address, Qt::ElideMiddle,
                                       addrDrawRect.width());
        painter->drawText(addrDrawRect, Qt::AlignLeft | Qt::AlignVCenter,
                          elided);

        if (isWatch) {
            QIcon iconWatchonly = qvariant_cast<QIcon>(
                index.data(
                    TransactionTableModel::WatchonlyDecorationRole));
            const int wIconSize = 14;
            QRect watchonlyRect(xright - wIconSize,
                                botLine.top() + (botLine.height() - wIconSize) / 2,
                                wIconSize, wIconSize);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option,
                          const QModelIndex &index) const {
        return QSize(DECORATION_SIZE, ROW_HEIGHT);
    }

    int unit;
    const PlatformStyle *platformStyle;
};
#include <qt/overviewpage.moc>

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent)
    : QWidget(parent), ui(new Ui::OverviewPage), clientModel(nullptr),
      walletModel(nullptr),
      txdelegate(new TxViewDelegate(platformStyle, this)) {
    ui->setupUi(this);

    // Elevated glass-card shadow on the two overview panels — subtle deep
    // black lift + a faint purple tint for the "neon under the glass" look.
    auto makeCardShadow = [](QWidget *w) {
        auto *shadow = new QGraphicsDropShadowEffect(w);
        shadow->setBlurRadius(28.0);
        shadow->setOffset(0, 10);
        shadow->setColor(QColor(0, 0, 0, 180));
        w->setGraphicsEffect(shadow);
    };
    makeCardShadow(ui->frame);
    makeCardShadow(ui->frame_2);

    // Neon glow behind the hero total balance number.
    auto *totalGlow = new QGraphicsDropShadowEffect(ui->labelTotal);
    totalGlow->setBlurRadius(24.0);
    totalGlow->setOffset(0, 0);
    totalGlow->setColor(QColor(10, 193, 142, 160));
    ui->labelTotal->setGraphicsEffect(totalGlow);

    // Softer purple halo on the watch-only total.
    auto *watchGlow = new QGraphicsDropShadowEffect(ui->labelWatchTotal);
    watchGlow->setBlurRadius(18.0);
    watchGlow->setOffset(0, 0);
    watchGlow->setColor(QColor(157, 78, 221, 130));
    ui->labelWatchTotal->setGraphicsEffect(watchGlow);

    m_balances.balance = -SATOSHI;

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    // also set the disabled icon because we are using a disabled QPushButton to
    // work around missing HiDPI support of QLabel
    // (https://bugreports.qt.io/browse/QTBUG-42503)
    icon.addPixmap(icon.pixmap(QSize(64, 64), QIcon::Normal), QIcon::Disabled);
    ui->labelTransactionsStatus->setIcon(icon);
    ui->labelWalletStatus->setIcon(icon);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(5 * (ROW_HEIGHT + 2));
    ui->listTransactions->setMouseTracking(true);
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, &QListView::clicked, this,
            &OverviewPage::handleTransactionClicked);

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, &QPushButton::clicked, this,
            &OverviewPage::handleOutOfSyncWarningClicks);
    connect(ui->labelTransactionsStatus, &QPushButton::clicked, this,
            &OverviewPage::handleOutOfSyncWarningClicks);
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index) {
    if (filter) {
        Q_EMIT transactionClicked(filter->mapToSource(index));
    }
}

void OverviewPage::handleOutOfSyncWarningClicks() {
    Q_EMIT outOfSyncWarningClicked();
}

void OverviewPage::refreshReceiveAddress() {
    if (!walletModel || !walletModel->getAddressTableModel()) {
        return;
    }
    const OutputType address_type =
        walletModel->wallet().getDefaultAddressType();
    const QString address = walletModel->getAddressTableModel()->addRow(
        AddressTableModel::Receive, QString(), QString(), address_type);
    if (address.isEmpty()) {
        return;
    }
    if (m_receiveAddressLabel) {
        m_receiveAddressLabel->setText(address);
    }
#ifdef USE_QRCODE
    if (m_receiveQrLabel) {
        const QString uri = QStringLiteral("bitcoincash:") + address;
        QRcode *code = QRcode_encodeString(uri.toUtf8().constData(), 0,
                                            QR_ECLEVEL_L, QR_MODE_8, 1);
        if (code) {
            const int qrPixSize = m_receiveQrLabel->width();
            QImage qrImage(code->width + 8, code->width + 8,
                           QImage::Format_RGB32);
            qrImage.fill(0xffffff);
            uint8_t *p = code->data;
            for (int y = 0; y < code->width; ++y) {
                for (int x = 0; x < code->width; ++x) {
                    qrImage.setPixel(x + 4, y + 4,
                                     ((*p & 1) ? 0x000000 : 0xffffff));
                    ++p;
                }
            }
            QRcode_free(code);
            m_receiveQrLabel->setPixmap(
                QPixmap::fromImage(qrImage.scaled(qrPixSize, qrPixSize,
                                                  Qt::KeepAspectRatio,
                                                  Qt::FastTransformation)));
        }
    }
#endif
}

void OverviewPage::embedSubWidgets(SendCoinsDialog *send,
                                   ReceiveCoinsDialog *receive,
                                   TransactionView *history,
                                   QPushButton *exportButton) {
    // Hide the compact "Recent transactions" panel — the full history view
    // takes its place below.
    ui->frame_2->hide();

    // Prune advanced/rarely-used sub-widgets from Send. Kept alive as
    // children so signals stay wired; surfaced on demand via the modal.
    auto hideChild = [](QWidget *parent, const char *name) {
        if (auto *w = parent->findChild<QWidget *>(QLatin1String(name))) {
            w->hide();
        }
    };
    hideChild(send, "addButton");
    hideChild(send, "clearButton");
    hideChild(send, "labelBalance");
    // Removing the entry causes SendCoinsDialog to spawn a fresh replacement
    // that never runs through our restack — its labels come back to the
    // side of the fields. Hiding the delete button keeps the restacked
    // entry pinned for the whole session.
    hideChild(send, "deleteButton");
    // The QScrollArea shipped inside SendCoinsDialog draws a scroll bar as
    // soon as content sizes even a pixel over its viewport. Force it off —
    // our compact form always fits, and the scroll bar was cosmetic clutter.
    if (auto *sa = send->findChild<QScrollArea *>()) {
        sa->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // QSS `border: none` on QScrollArea doesn't always clear the
        // beveled frame — kill it explicitly at the widget level.
        sa->setFrameShape(QFrame::NoFrame);
        // scrollAreaWidgetContents ships with a trailing verticalSpacer
        // (Expanding) that swallows every pixel of extra height, so the
        // grid inside SendCoins never sees space for its row stretches.
        // Strip the spacer and let the entry chain expand.
        if (auto *contents = sa->widget()) {
            if (auto *cl = qobject_cast<QVBoxLayout *>(contents->layout())) {
                for (int i = cl->count() - 1; i >= 0; --i) {
                    if (cl->itemAt(i)->spacerItem()) {
                        auto *si = cl->takeAt(i);
                        delete si;
                    }
                }
                if (cl->count() > 0) cl->setStretch(0, 1);
            }
        }
    }
    for (auto *entry :
         send->findChildren<QWidget *>(QStringLiteral("SendCoinsEntry"))) {
        entry->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    }
    if (auto *sendCoinsFrame =
            send->findChild<QFrame *>(QStringLiteral("SendCoins"))) {
        sendCoinsFrame->setSizePolicy(QSizePolicy::Preferred,
                                       QSizePolicy::Expanding);
    }
    // Also stretch the SendCoinsDialog widget itself so the entire chain
    // (send → scrollArea → SendCoinsEntry → SendCoins) can grow to fill
    // the card. Without this, `send` stays at its layout's sizeHint and
    // the row stretches inside the grid have nothing extra to work with.
    send->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    // The QFrames tucked away beneath the entries (frameFee, frameCoinControl,
    // frameFeeSelection) are hidden by us, but their sibling widgets
    // (labels, spacers, entire horizontalLayout_2 with "Balance:") still
    // live in the outer SendCoinsDialog layout. Kill any lingering frame
    // shapes on all QFrames so no purple hairline can leak through.
    for (auto *f : send->findChildren<QFrame *>()) {
        f->setFrameShape(QFrame::NoFrame);
        f->setFrameShadow(QFrame::Plain);
        f->setLineWidth(0);
        f->setMidLineWidth(0);
    }
    // The QScrollArea's scroll bars — even under AlwaysOff policy — leave
    // a styled track background at the bottom edge that renders as a
    // faint horizontal line. Explicitly hide the QScrollBar widgets.
    if (auto *sa = send->findChild<QScrollArea *>()) {
        if (auto *hb = sa->horizontalScrollBar()) hb->hide();
        if (auto *vb = sa->verticalScrollBar()) vb->hide();
        // Kill viewport background painting too.
        if (auto *vp = sa->viewport()) vp->setAutoFillBackground(false);
    }
    // As a last resort, kill autoFillBackground on every child of send so
    // no stray widget can paint its own background layer that shows as a
    // hairline where its edge lands.
    for (auto *w : send->findChildren<QWidget *>()) {
        w->setAutoFillBackground(false);
    }
    // Advanced fee toggle — most users leave defaults. Reachable later
    // via the gear button in the Send header if we surface a proper
    // "advanced" modal for it.
    hideChild(send, "checkboxSubtractFeeFromAmount");

    // Force a uniform fixed height on all input fields across both cards
    // so their rows sum to identical envelopes. Use setFixedHeight (not a
    // min/max pair — the amount spinbox's internal padding pushes back
    // against a too-tight max, clipping the border).
    const int kFieldHeight = 40;
    auto stampFieldHeights = [kFieldHeight](QWidget *root) {
        for (auto *le : root->findChildren<QLineEdit *>()) {
            le->setFixedHeight(kFieldHeight);
        }
        for (auto *sb : root->findChildren<QAbstractSpinBox *>()) {
            sb->setFixedHeight(kFieldHeight);
        }
        for (auto *cb : root->findChildren<QComboBox *>()) {
            cb->setFixedHeight(kFieldHeight);
        }
        // Stamp inline row buttons too — QToolButton/QPushButton with QSS
        // padding otherwise render taller than 40px and dominate their
        // HBoxLayout row height, sinking the line edit inside. Uniform
        // 40 across all row-inline widgets keeps captions and field
        // tops perfectly aligned across both panels.
        for (auto *tb : root->findChildren<QToolButton *>()) {
            tb->setFixedHeight(kFieldHeight);
        }
        for (auto *pb : root->findChildren<QPushButton *>()) {
            pb->setFixedHeight(kFieldHeight);
        }
    };
    stampFieldHeights(send);
    stampFieldHeights(receive);

    // Restack Send/Receive form rows vertically: caption label sitting
    // *above* its field, spanning full width, instead of the .ui's two-
    // column [label | field] table. This gives the tall cards enough
    // vertical breathing room and reads as a modern form.
    auto restackFormGrid = [](QGridLayout *grid, int fieldCol, bool spread) {
        if (!grid) return;
        struct Pair {
            QWidget *label = nullptr;
            QWidget *fieldWidget = nullptr;
            QLayout *fieldLayout = nullptr;
        };
        QVector<Pair> pairs;
        const int rows = grid->rowCount();
        for (int r = 0; r < rows; ++r) {
            QLayoutItem *lbl = grid->itemAtPosition(r, 0);
            QLayoutItem *field = grid->itemAtPosition(r, fieldCol);
            if (!lbl || !field || !lbl->widget()) continue;
            auto *lblAsLabel = qobject_cast<QLabel *>(lbl->widget());
            // Skip rows whose label is empty/whitespace or hidden — those
            // are either separator stubs (label_7) or already-hidden bits
            // (label_5 instructional) that would leave a blank caption.
            if (lblAsLabel) {
                QString t = lblAsLabel->text().trimmed();
                t.remove(QChar('&'));
                if (t.isEmpty()) continue;
            }
            if (lbl->widget()->isHidden()) continue;
            Pair p;
            p.label = lbl->widget();
            p.fieldWidget = field->widget();
            p.fieldLayout = field->layout();
            pairs.append(p);
        }
        // Drain the grid.
        while (grid->count() > 0) {
            grid->takeAt(0);
        }
        grid->setVerticalSpacing(spread ? 6 : 18);
        grid->setHorizontalSpacing(0);
        grid->setContentsMargins(0, 0, 0, 0);
        // Clear any lingering row/column stretch factors from the
        // original .ui grid. Without this, rows above the AMOUNT/SEND
        // row end up inflated with leftover stretch and push everything
        // below them down.
        for (int r = 0; r < 24; ++r) grid->setRowStretch(r, 0);
        for (int c = 0; c < 8; ++c) grid->setColumnStretch(c, 0);
        for (int i = 0; i < pairs.size(); ++i) {
            Pair &p = pairs[i];
            // Caption styling — small Rajdhani caps in muted gray.
            p.label->setStyleSheet(QStringLiteral(
                "QLabel { color: #7B857F; background: transparent; "
                "font-family: 'Rajdhani', sans-serif; font-size: 9pt; "
                "font-weight: 600; letter-spacing: 2px; }"));
            if (auto *lblAsLabel = qobject_cast<QLabel *>(p.label)) {
                lblAsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                QString text = lblAsLabel->text();
                if (text.endsWith(QChar(':'))) text.chop(1);
                // Ampersand (&) is a Qt mnemonic marker — strip it before
                // uppercasing so it doesn't render literally.
                text.remove(QChar('&'));
                lblAsLabel->setText(text.toUpper());
            }
            const int rLbl = i * 2;
            const int rField = i * 2 + 1;
            grid->addWidget(p.label, rLbl, 0, 1, fieldCol + 1);
            if (p.fieldWidget) {
                grid->addWidget(p.fieldWidget, rField, 0, 1, fieldCol + 1);
            } else if (p.fieldLayout) {
                grid->addLayout(p.fieldLayout, rField, 0, 1, fieldCol + 1);
            }
        }
        (void)spread;
    };

    // Send: SendCoinsEntry's inner "SendCoins" frame owns the form grid
    // — labels at col 0, fields at col 1.
    if (auto *sendCoinsFrame =
            send->findChild<QFrame *>(QStringLiteral("SendCoins"))) {
        // Swap PAY TO (row 0) with LABEL (row 1) so LABEL sits at the top
        // and PAY TO takes the middle slot, per the user's requested order.
        if (auto *grid = qobject_cast<QGridLayout *>(sendCoinsFrame->layout())) {
            auto swap = [grid](int r0, int r1, int c) {
                auto *ia = grid->itemAtPosition(r0, c);
                auto *ib = grid->itemAtPosition(r1, c);
                QWidget *wa = ia ? ia->widget() : nullptr;
                QWidget *wb = ib ? ib->widget() : nullptr;
                QLayout *la = ia ? ia->layout() : nullptr;
                QLayout *lb = ib ? ib->layout() : nullptr;
                if (wa) grid->removeWidget(wa);
                if (wb) grid->removeWidget(wb);
                if (la) grid->removeItem(ia);
                if (lb) grid->removeItem(ib);
                if (wa) grid->addWidget(wa, r1, c);
                else if (la) grid->addLayout(la, r1, c);
                if (wb) grid->addWidget(wb, r0, c);
                else if (lb) grid->addLayout(lb, r0, c);
            };
            swap(0, 1, 0); // labels
            swap(0, 1, 1); // fields/layouts
        }
        restackFormGrid(qobject_cast<QGridLayout *>(sendCoinsFrame->layout()),
                        1, /*spread=*/true);
        // Spread the 3 label/field pairs vertically through the frame:
        // give each FIELD row an equal stretch factor so extra vertical
        // space is shared across all 3 rows, then re-add each field cell
        // with Qt::AlignTop so widgets sit at cell tops with clean empty
        // space below within each cell. This puts natural clearance
        // below the SEND/BCH/UseAvailable widgets (AMOUNT row) without
        // opening a gap between the last row and the frame border — so
        // no hairline can show up, and no card inflation is needed.
        if (auto *grid = qobject_cast<QGridLayout *>(sendCoinsFrame->layout())) {
            const int rows = grid->rowCount();
            // restackFormGrid emits 2 rows per pair; iterate every odd
            // index (field rows) and normalise them.
            for (int rField = 1; rField < rows; rField += 2) {
                grid->setRowStretch(rField, 1);
                QLayoutItem *item = grid->itemAtPosition(rField, 0);
                if (!item) continue;
                QWidget *w = item->widget();
                QLayout *l = item->layout();
                grid->removeItem(item);
                if (w) grid->addWidget(w, rField, 0, 1, 2, Qt::AlignTop);
                else if (l) grid->addLayout(l, rField, 0, 1, 2, Qt::AlignTop);
            }
        }
        // Zero out the frame's internal contentsMargins so the LABEL row
        // sits flush against the frame top — matches Receive's frame
        // padding.
        sendCoinsFrame->setContentsMargins(14, 0, 14, 0);
        // Same treatment we apply to receive below: the send widget's
        // outer verticalLayout plus the scrollArea's contents VBox both
        // carry Qt's default ~9-11px margins that push LABEL down. Zero
        // every layout inside `send` so LABEL sits flush against the top
        // of `send` — matches Receive's LABEL Y position.
        for (auto *l : send->findChildren<QLayout *>()) {
            l->setContentsMargins(0, 0, 0, 0);
        }
        if (auto *sl = send->layout()) {
            sl->setContentsMargins(0, 0, 0, 0);
        }
    }
    // Receive: ReceiveCoinsDialog's outer form is `frame2`. Labels at
    // col 0, fields at col 2. Spread the 3 pairs vertically so Label is at
    // the top, Amount is centered, Message anchors the bottom.
    if (auto *receiveFrame =
            receive->findChild<QFrame *>(QStringLiteral("frame2"))) {
        // Reorder the .ui rows so the visual order becomes Label · Message
        // · Amount (Amount anchors the bottom, Label sits at top, Message
        // takes the centered slot).
        auto *reqAmount =
            receive->findChild<QWidget *>(QStringLiteral("reqAmount"));
        auto *reqMessage =
            receive->findChild<QWidget *>(QStringLiteral("reqMessage"));
        if (auto *grid = receiveFrame->findChild<QGridLayout *>()) {
            int rAmount = -1, rMessage = -1;
            for (int r = 0; r < grid->rowCount(); ++r) {
                auto *fld = grid->itemAtPosition(r, 2);
                if (!fld || !fld->widget()) continue;
                if (fld->widget() == reqAmount) rAmount = r;
                else if (fld->widget() == reqMessage) rMessage = r;
            }
            if (rAmount >= 0 && rMessage >= 0) {
                auto swapCell = [grid](int rA, int rB, int c) {
                    auto *ia = grid->itemAtPosition(rA, c);
                    auto *ib = grid->itemAtPosition(rB, c);
                    QWidget *wa = ia ? ia->widget() : nullptr;
                    QWidget *wb = ib ? ib->widget() : nullptr;
                    if (wa) grid->removeWidget(wa);
                    if (wb) grid->removeWidget(wb);
                    if (wa) grid->addWidget(wa, rB, c);
                    if (wb) grid->addWidget(wb, rA, c);
                };
                swapCell(rAmount, rMessage, 0);
                swapCell(rAmount, rMessage, 2);
            }
        }
        restackFormGrid(receiveFrame->findChild<QGridLayout *>(), 2,
                        /*spread=*/true);
        if (auto *outer = qobject_cast<QVBoxLayout *>(receive->layout())) {
            const int idx = outer->indexOf(receiveFrame);
            if (idx >= 0) outer->setStretch(idx, 0);
        }
        // Force Receive content compact at top: add a phantom trailing
        // row with stretch=1 so extra vertical space accumulates BELOW
        // the AMOUNT row instead of stretching its cell (which was
        // centering the amount input further down). This matches Send's
        // compact top-anchored form.
        if (auto *grid = receiveFrame->findChild<QGridLayout *>()) {
            grid->setRowStretch(grid->rowCount(), 1);
        }
        // Zero the frame's contentsMargins so the LABEL row sits flush
        // against the frame top — matches sendCoinsFrame.
        receiveFrame->setContentsMargins(14, 0, 14, 0);
        // frame2 wraps its grid in a verticalLayout_3 with default (~9-11px)
        // margins that push the LABEL row down. The .ui doesn't override
        // them, so setting the grid's margins wasn't enough. Zero every
        // layout in the receive widget so the LABEL sits flush against
        // the top of `receive`.
        for (auto *l : receive->findChildren<QLayout *>()) {
            l->setContentsMargins(0, 0, 0, 0);
        }
        if (auto *rl = receive->layout()) {
            rl->setContentsMargins(0, 0, 0, 0);
        }
    }

    // Consolidate the Send bottom row: close the gap between the amount
    // field and "Use available balance", then float the big SEND button
    // to the far right of the same row so it visually anchors the action.
    if (auto *amountRow =
            send->findChild<QHBoxLayout *>(QStringLiteral("horizontalLayoutAmount"))) {
        // Kill the .ui's stretch="0,1,0" — the middle slot held the now-hidden
        // "Subtract fee" checkbox and was still eating extra horizontal space.
        for (int i = 0; i < amountRow->count(); ++i) {
            amountRow->setStretch(i, 0);
        }
        if (auto *sendBtn =
                send->findChild<QPushButton *>(QStringLiteral("sendButton"))) {
            amountRow->addStretch(1);
            // addWidget handles reparenting on its own; setParent(nullptr)
            // briefly promoted sendBtn to a top-level window whose
            // Qt::Window flag wasn't always cleared on re-attach, so it
            // rendered as a floating widget beside the panel.
            amountRow->addWidget(sendBtn);
        }
    }
    // The "Balance:" caption is a QLabel with the generic objectName
    // "label" — the only reliable way to target it is by matching text.
    for (auto *l : send->findChildren<QLabel *>()) {
        if (l->text() == QStringLiteral("Balance:")) {
            l->hide();
            break;
        }
    }
    if (auto *f = send->findChild<QWidget *>(QStringLiteral("frameFee"))) {
        f->hide();
        m_sendFeeWidget = f;
    }

    // Prune Receive: the instructional label + the payment-history table.
    hideChild(receive, "label_5");
    // Keep receiveButton + clearButton visible — the form is the primary
    // content of the Receive card, so users need those buttons to
    // generate/clear a labeled payment request.
    if (auto *f = receive->findChild<QWidget *>(QStringLiteral("frame"))) {
        f->hide();
        m_receiveHistoryWidget = f;
    }

    QFont sectionFont(QStringLiteral("Rajdhani"));
    sectionFont.setPointSize(11);
    sectionFont.setWeight(QFont::Bold);
    sectionFont.setCapitalization(QFont::AllUppercase);
    sectionFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);

    auto makeCard = [this](const QString &objectName) {
        auto *card = new QFrame(this);
        card->setObjectName(objectName);
        card->setFrameShape(QFrame::StyledPanel);
        card->setFrameShadow(QFrame::Raised);
        return card;
    };
    auto makeHeader = [&sectionFont](const QString &text, QWidget *parent) {
        auto *label = new QLabel(text, parent);
        label->setFont(sectionFont);
        label->setStyleSheet(QStringLiteral(
            "QLabel { color: #7B857F; background: transparent; border: none; }"));
        return label;
    };
    auto makeExpanderButton = [](const QString &tooltip, QWidget *parent) {
        auto *btn = new QPushButton(QStringLiteral("\u2699"), parent); // gear
        btn->setObjectName(QStringLiteral("cardExpander"));
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(tooltip);
        btn->setFixedSize(28, 24);
        btn->setFocusPolicy(Qt::NoFocus);
        return btn;
    };
    auto makeCardShadow = [](QWidget *w) {
        auto *shadow = new QGraphicsDropShadowEffect(w);
        shadow->setBlurRadius(28.0);
        shadow->setOffset(0, 10);
        shadow->setColor(QColor(0, 0, 0, 180));
        w->setGraphicsEffect(shadow);
    };
    // Build a chevron toggle button. Clicking it hides/shows `content`.
    auto makeChevron = [](QWidget *parent) {
        auto *btn = new QPushButton(QStringLiteral("\u25BE"), parent); // ▾
        btn->setObjectName(QStringLiteral("cardChevron"));
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedSize(24, 24);
        btn->setFocusPolicy(Qt::NoFocus);
        return btn;
    };
    // Wire chevron to hide/show content and shrink the card via size policy.
    // For cards flagged as fillingHost, expanded state grants vertical
    // Expanding so the card grabs unused window space; collapsed reverts to
    // Preferred so the frame hugs the header row. After the toggle we ask
    // the top-level window to shrink to fit — otherwise the window keeps
    // whatever height the user last stretched it to and nothing appears
    // to change visually when all sections collapse.
    auto wireCollapse = [this](QPushButton *chev, QWidget *content,
                                QFrame *card, bool fillingHost) {
        QObject::connect(chev, &QPushButton::clicked, content,
                         [this, chev, content, card, fillingHost]() {
            const bool showing = !content->isVisible();
            content->setVisible(showing);
            chev->setText(showing ? QStringLiteral("\u25BE")
                                   : QStringLiteral("\u25B8")); // ▾ / ▸
            card->setSizePolicy(QSizePolicy::Preferred,
                                (showing && fillingHost)
                                    ? QSizePolicy::Expanding
                                    : QSizePolicy::Preferred);
            // Defer to the next event loop tick so layout invalidation
            // from setVisible has flushed, then shrink the window to
            // sizeHint. Without the defer the resize sees the *old*
            // hint and leaves dead space at the bottom.
            QTimer::singleShot(0, this, [this]() {
                if (QWidget *top = window()) {
                    if (!top->isMaximized() && !top->isFullScreen()) {
                        top->layout()->activate();
                        top->resize(top->width(),
                                    top->layout()->minimumSize().height());
                    }
                }
            });
        });
    };
    // Header row with: title, stretch, optional right-side button.
    // (Collapse chevrons were removed — the layout side effects were
    // too fragile and the feature didn't justify the complexity.)
    auto makeHeaderRow = [&](const QString &title, QPushButton *chevron,
                             QPushButton *extraBtn, QWidget *parent) {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        if (chevron) row->addWidget(chevron);
        row->addWidget(makeHeader(title, parent));
        row->addStretch();
        if (extraBtn) row->addWidget(extraBtn);
        return row;
    };

    // Send card
    auto *sendCard = makeCard(QStringLiteral("frameSend"));
    auto *sendLayout = new QVBoxLayout(sendCard);
    sendLayout->setAlignment(Qt::AlignTop);
    QPushButton *sendAdvBtn = m_sendFeeWidget
        ? makeExpanderButton(tr("Fee options"), sendCard) : nullptr;
    sendLayout->addLayout(
        makeHeaderRow(tr("Send"), nullptr, sendAdvBtn, sendCard));
    auto *sendContent = new QWidget(sendCard);
    auto *sendContentLayout = new QVBoxLayout(sendContent);
    sendContentLayout->setContentsMargins(0, 0, 0, 0);
    send->setParent(sendContent);
    sendContentLayout->addWidget(send);
    sendLayout->addWidget(sendContent);
    makeCardShadow(sendCard);

    if (sendAdvBtn) {
        connect(sendAdvBtn, &QPushButton::clicked, this, [this]() {
            if (!m_sendAdvancedDialog) {
                m_sendAdvancedDialog = new QDialog(this);
                m_sendAdvancedDialog->setWindowTitle(tr("Fee Options"));
                m_sendAdvancedDialog->setModal(true);
                auto *dlgLayout = new QVBoxLayout(m_sendAdvancedDialog);
                m_sendFeeWidget->setParent(m_sendAdvancedDialog);
                m_sendFeeWidget->show();
                dlgLayout->addWidget(m_sendFeeWidget);
                auto *bb = new QDialogButtonBox(QDialogButtonBox::Close,
                                                m_sendAdvancedDialog);
                connect(bb, &QDialogButtonBox::rejected, m_sendAdvancedDialog,
                        &QDialog::accept);
                dlgLayout->addWidget(bb);
            }
            m_sendAdvancedDialog->exec();
        });
    }

    // Receive card — label/message/amount form is the primary content.
    // The QR + address + New Address panel lives behind a gear button
    // in the header, mirroring Send's fee-options modal pattern.
    auto *receiveCard = makeCard(QStringLiteral("frameReceive"));
    auto *receiveLayout = new QVBoxLayout(receiveCard);
    receiveLayout->setAlignment(Qt::AlignTop);

    // "QR CODE" text button — parked here for later injection into the
    // AMOUNT row (next to the BCH unit combobox), not the header.
    // Clicking it programmatically triggers the hidden receiveButton,
    // which pops the stock ReceiveRequestDialog (QR + URI + address +
    // Copy URI + Copy Address + Save Image + Close).
    auto *receiveAdvBtn = new QPushButton(tr("QR CODE"), receiveCard);
    receiveAdvBtn->setObjectName(QStringLiteral("receiveQrCodeButton"));
    receiveAdvBtn->setCursor(Qt::PointingHandCursor);
    receiveAdvBtn->setFocusPolicy(Qt::NoFocus);

    QPushButton *receiveHistBtn = m_receiveHistoryWidget
        ? makeExpanderButton(tr("Payment history"), receiveCard) : nullptr;
    if (receiveHistBtn) {
        receiveHistBtn->setText(QStringLiteral("\U0001F551")); // clock face
    }

    // Header row: title + stretch + clock (payment history) only.
    auto *receiveHeaderRow = new QHBoxLayout();
    receiveHeaderRow->setContentsMargins(0, 0, 0, 0);
    receiveHeaderRow->setSpacing(8);
    receiveHeaderRow->addWidget(makeHeader(tr("Receive"), receiveCard));
    receiveHeaderRow->addStretch();
    if (receiveHistBtn) receiveHeaderRow->addWidget(receiveHistBtn);
    receiveLayout->addLayout(receiveHeaderRow);

    auto *receiveContent = new QWidget(receiveCard);
    auto *receiveContentLayout = new QVBoxLayout(receiveContent);
    receiveContentLayout->setContentsMargins(0, 0, 0, 0);
    receive->setParent(receiveContent);
    receiveContentLayout->addWidget(receive, 1);
    receiveLayout->addWidget(receiveContent, 1);
    makeCardShadow(receiveCard);

    // Inject QR CODE button into the AMOUNT row of the receive form,
    // right after reqAmount (whose BCH unit combobox is its last inline
    // child). Wrap the existing reqAmount cell in a QHBoxLayout so we
    // can put the button beside it without touching BitcoinAmountField
    // internals.
    if (auto *frame2 =
            receive->findChild<QFrame *>(QStringLiteral("frame2"))) {
        if (auto *grid = frame2->findChild<QGridLayout *>()) {
            if (auto *reqAmount =
                    receive->findChild<QWidget *>(QStringLiteral("reqAmount"))) {
                const int idx = grid->indexOf(reqAmount);
                if (idx >= 0) {
                    int r, c, rs, cs;
                    grid->getItemPosition(idx, &r, &c, &rs, &cs);
                    grid->removeWidget(reqAmount);
                    auto *amountRow = new QHBoxLayout();
                    amountRow->setContentsMargins(0, 0, 0, 0);
                    amountRow->setSpacing(8);
                    amountRow->addWidget(reqAmount);
                    receiveAdvBtn->setParent(frame2);
                    amountRow->addWidget(receiveAdvBtn);
                    amountRow->addStretch(1);
                    grid->addLayout(amountRow, r, c, rs, cs);
                }
            }
        }
    }

    // Hide the stock Request payment / Clear buttons and route the
    // QR CODE button to trigger the stock Request payment action.
    if (auto *reqBtn =
            receive->findChild<QPushButton *>(QStringLiteral("receiveButton"))) {
        reqBtn->hide();
        connect(receiveAdvBtn, &QPushButton::clicked, reqBtn,
                &QPushButton::click);
    }
    if (auto *clrBtn =
            receive->findChild<QPushButton *>(QStringLiteral("clearButton"))) {
        clrBtn->hide();
    }

    if (receiveHistBtn) {
        connect(receiveHistBtn, &QPushButton::clicked, this, [this]() {
            if (!m_receiveHistoryDialog) {
                m_receiveHistoryDialog = new QDialog(this);
                m_receiveHistoryDialog->setWindowTitle(tr("Payment History"));
                m_receiveHistoryDialog->setModal(true);
                m_receiveHistoryDialog->resize(680, 380);
                auto *dlgLayout = new QVBoxLayout(m_receiveHistoryDialog);
                m_receiveHistoryWidget->setParent(m_receiveHistoryDialog);
                m_receiveHistoryWidget->show();
                dlgLayout->addWidget(m_receiveHistoryWidget);
                auto *bb = new QDialogButtonBox(QDialogButtonBox::Close,
                                                m_receiveHistoryDialog);
                connect(bb, &QDialogButtonBox::rejected, m_receiveHistoryDialog,
                        &QDialog::accept);
                dlgLayout->addWidget(bb);
            }
            m_receiveHistoryDialog->exec();
        });
    }

    // Side-by-side row
    auto *actionsRow = new QHBoxLayout();
    actionsRow->setSpacing(12);
    actionsRow->addWidget(sendCard, 1);
    actionsRow->addWidget(receiveCard, 1);

    // History card (full TransactionView with filter row + export button)
    auto *historyCard = makeCard(QStringLiteral("frameHistory"));
    auto *historyLayout = new QVBoxLayout(historyCard);
    historyLayout->setAlignment(Qt::AlignTop);
    historyLayout->addLayout(
        makeHeaderRow(tr("Transactions"), nullptr, nullptr, historyCard));
    auto *historyContent = new QWidget(historyCard);
    auto *historyContentLayout = new QVBoxLayout(historyContent);
    historyContentLayout->setContentsMargins(0, 0, 0, 0);
    history->setParent(historyContent);
    history->setMinimumHeight(280);
    historyContentLayout->addWidget(history);
    if (exportButton) {
        // Slot the Export button onto the far right of the transaction
        // filter row (right after Min amount) rather than under the table
        // — that reclaims a full row of vertical space for the tx list.
        if (auto *filterRow = history->findChild<QHBoxLayout *>()) {
            exportButton->setParent(history);
            filterRow->addWidget(exportButton);
        } else {
            // Fallback: original bottom-row placement.
            auto *btnRow = new QHBoxLayout();
            btnRow->addStretch();
            exportButton->setParent(historyContent);
            btnRow->addWidget(exportButton);
            historyContentLayout->addLayout(btnRow);
        }
    }
    historyLayout->addWidget(historyContent);
    makeCardShadow(historyCard);

    // Balances (+ Node status) grow to fill the vertical slack freed by
    // capping the Send/Receive cards. Send/Receive shrink to their
    // content sizeHint (roughly header + 3 form rows = ~260px) via a
    // hard maximum. History stays Expanding so it still claims most of
    // the window; the top row's stretch (set below on outerLayout)
    // lets Balances/Node share the freed slack instead of all going to
    // History.
    ui->frame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    sendCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    receiveCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    historyCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    // Append into the outer vertical layout (was ui->horizontalLayout, now QVBoxLayout).
    auto *outerLayout = qobject_cast<QVBoxLayout *>(ui->horizontalLayout);
    if (outerLayout) {
        // Rip out verticalLayout_3 — it wrapped the now-hidden frame_2 and
        // was silently claiming vertical space between Balances and Send.
        if (outerLayout->count() > 1) {
            QLayoutItem *dead = outerLayout->takeAt(1);
            delete dead; // deletes the empty layout item wrapper, not children
        }

        // Wrap the balances card in a horizontal top row so the Node
        // status card can sit next to it as a side-by-side sibling. Detach
        // ui->frame from its .ui wrapper (verticalLayout_2) instead of
        // pulling the wrapper out — deleting the wrapper item was killing
        // the balance frame's layout ownership and blanking the card.
        ui->frame->setParent(this);
        m_topRow = new QHBoxLayout();
        m_topRow->setSpacing(12);
        m_topRow->addWidget(ui->frame, 3);
        outerLayout->insertLayout(0, m_topRow);

        outerLayout->addLayout(actionsRow);
        // History card claims a share of remaining vertical space so
        // the tx table fills the bottom of the window instead of
        // leaving dead padding.
        outerLayout->addWidget(historyCard, 2);
        // Top row (Balances + Node status) also grows — user wants the
        // slack freed by Send/Receive capping to go here, not to
        // History.
        outerLayout->setStretch(0, 1);
    }

    // Balance section trim: hide "Immature" and the .ui's bottom-row
    // Spendable/Watch-only column labels. The two labels are re-added into
    // the header row (below) so they read as part of the section heading,
    // level with "BALANCES", instead of hanging under the value grid.
    ui->labelImmature->hide();
    ui->labelImmatureText->hide();
    ui->labelWatchImmature->hide();
    ui->labelSpendable->hide();
    ui->labelWatchonly->hide();

    // Tighten the balance card interior — the header→value gap was too
    // roomy because the .ui shipped with default QVBoxLayout spacing plus a
    // trailing expanding spacer.
    ui->verticalLayout_4->setSpacing(2);
    ui->verticalLayout_4->setContentsMargins(0, 0, 0, 0);
    ui->gridLayout->setVerticalSpacing(4);
    if (auto *sp = ui->verticalSpacerInsideCard) {
        sp->changeSize(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    }

    // Re-title in the section style used by the other cards.
    ui->label_5->setFont(sectionFont);
    ui->label_5->setStyleSheet(QStringLiteral(
        "QLabel { color: #7B857F; background: transparent; border: none; }"));
    ui->label_5->setText(tr("Balances"));

    // Put Spendable / Watch-only labels into the value grid itself so
    // they right-align perfectly to the two value columns. We shift every
    // existing grid item down one row and drop the header labels into
    // row 0. Combined with tight verticalLayout_4 spacing this reads as
    // "right under BALANCES" — same visual line, correct columns.
    auto makeColChip = [](const QString &text, const QColor &color,
                          QWidget *parent) {
        auto *l = new QLabel(text, parent);
        QFont f(QStringLiteral("Rajdhani"));
        f.setPointSize(9);
        f.setWeight(QFont::DemiBold);
        f.setCapitalization(QFont::AllUppercase);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
        l->setFont(f);
        l->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; background: transparent; border: none; }")
            .arg(color.name()));
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return l;
    };

    struct GridCell {
        QLayoutItem *item;
        int row, col, rowSpan, colSpan;
    };
    QVector<GridCell> cells;
    for (int i = ui->gridLayout->count() - 1; i >= 0; --i) {
        int r, c, rs, cs;
        ui->gridLayout->getItemPosition(i, &r, &c, &rs, &cs);
        cells.prepend({ui->gridLayout->takeAt(i), r, c, rs, cs});
    }
    for (const auto &cell : cells) {
        if (auto *w = cell.item->widget()) {
            ui->gridLayout->addWidget(w, cell.row + 1, cell.col, cell.rowSpan,
                                      cell.colSpan);
            delete cell.item;
        } else {
            ui->gridLayout->addItem(cell.item, cell.row + 1, cell.col,
                                    cell.rowSpan, cell.colSpan);
        }
    }
    ui->gridLayout->addWidget(
        makeColChip(tr("Spendable"), QColor(0x0A, 0xC1, 0x8E), ui->frame),
        0, 1, 1, 1);
    ui->gridLayout->addWidget(
        makeColChip(tr("Watch-only"), QColor(0x9D, 0x4E, 0xDD), ui->frame),
        0, 2, 1, 1);
    // Wrap the balance card's contents with a big BCH logo on the left.
    // Drain verticalLayout_4 into savedItems, swap ui->frame's layout to a
    // QHBoxLayout, then rehang the drained items in a QVBox on the right.
    // Widget/spacer identities survive the swap so downstream references
    // to ui->labelTotal, ui->verticalSpacerInsideCard, etc. stay valid.
    QVector<QLayoutItem *> savedBalanceItems;
    while (ui->verticalLayout_4->count() > 0) {
        savedBalanceItems.append(ui->verticalLayout_4->takeAt(0));
    }
    delete ui->frame->layout();

    auto *balanceHBox = new QHBoxLayout(ui->frame);
    balanceHBox->setContentsMargins(10, 10, 10, 10);
    balanceHBox->setSpacing(20);

    auto *bchLogo = new QLabel(ui->frame);
    QIcon logoIcon(QStringLiteral(":/icons/bitcoin_cash_circle"));
    bchLogo->setPixmap(logoIcon.pixmap(QSize(180, 180)));
    bchLogo->setAlignment(Qt::AlignCenter);
    bchLogo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    bchLogo->setFixedSize(180, 180);
    balanceHBox->addWidget(bchLogo, 0, Qt::AlignVCenter);

    auto *rightBox = new QVBoxLayout();
    rightBox->setContentsMargins(0, 0, 0, 0);
    rightBox->setSpacing(2);
    for (QLayoutItem *item : savedBalanceItems) {
        if (item->widget()) {
            rightBox->addWidget(item->widget());
        } else if (item->layout()) {
            rightBox->addLayout(item->layout());
        } else if (item->spacerItem()) {
            rightBox->addItem(item);
        }
    }
    // rightBox fills the space to the right of the logo. Only col 2
    // (watch-only) stretches — SPENDABLE / labels / green values keep
    // their natural positions on the left, watch-only slides right.
    balanceHBox->addLayout(rightBox, 1);
    ui->gridLayout->setColumnStretch(0, 0);
    ui->gridLayout->setColumnStretch(1, 0);
    ui->gridLayout->setColumnStretch(2, 1);
    ui->gridLayout->setColumnStretch(3, 0);
}

void OverviewPage::installNetworkStatusWidget(QWidget *w) {
    if (!w) return;
    w->setParent(this);
    // Let the node status card grow vertically with the top row so it
    // and the Balances card together absorb the slack freed by the
    // capped Send/Receive cards.
    w->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    // Slot into the top row as a sibling of the Balances card.
    if (m_topRow) {
        m_topRow->addWidget(w, 1);
    }
    w->show();
}

OverviewPage::~OverviewPage() {
    delete ui;
}

void OverviewPage::setBalance(const interfaces::WalletBalances &balances) {
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    m_balances = balances;
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(
        unit, balances.balance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(
        BitcoinUnits::formatWithUnit(unit, balances.unconfirmed_balance, false,
                                     BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(
        unit, balances.immature_balance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::formatWithUnit(
        unit,
        balances.balance + balances.unconfirmed_balance +
            balances.immature_balance,
        false, BitcoinUnits::separatorAlways));
    ui->labelWatchAvailable->setText(
        BitcoinUnits::formatWithUnit(unit, balances.watch_only_balance, false,
                                     BitcoinUnits::separatorAlways));
    ui->labelWatchPending->setText(BitcoinUnits::formatWithUnit(
        unit, balances.unconfirmed_watch_only_balance, false,
        BitcoinUnits::separatorAlways));
    ui->labelWatchImmature->setText(
        BitcoinUnits::formatWithUnit(unit, balances.immature_watch_only_balance,
                                     false, BitcoinUnits::separatorAlways));
    ui->labelWatchTotal->setText(BitcoinUnits::formatWithUnit(
        unit,
        balances.watch_only_balance + balances.unconfirmed_watch_only_balance +
            balances.immature_watch_only_balance,
        false, BitcoinUnits::separatorAlways));

    // Immature (mining coinbase-not-matured) is meaningless for
    // non-mining users — always hidden in this dashboard.
    ui->labelImmature->setVisible(false);
    ui->labelImmatureText->setVisible(false);
    ui->labelWatchImmature->setVisible(false);
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly) {
    // labelSpendable + labelWatchonly are hidden in the dashboard trim and
    // replaced with column headers laid out in embedSubWidgets().
    ui->lineWatchBalance->setVisible(showWatchOnly);
    ui->labelWatchAvailable->setVisible(showWatchOnly);
    ui->labelWatchPending->setVisible(showWatchOnly);
    ui->labelWatchTotal->setVisible(showWatchOnly);
    ui->labelWatchImmature->setVisible(false);
}

void OverviewPage::setClientModel(ClientModel *model) {
    this->clientModel = model;
    if (model) {
        // Show warning, for example if this is a prerelease version
        connect(model, &ClientModel::alertsChanged, this,
                &OverviewPage::updateAlerts);
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model) {
    this->walletModel = model;
    if (model && model->getOptionsModel()) {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        interfaces::Wallet &wallet = model->wallet();
        interfaces::WalletBalances balances = wallet.getBalances();
        setBalance(balances);
        connect(model, &WalletModel::balanceChanged, this,
                &OverviewPage::setBalance);

        connect(model->getOptionsModel(), &OptionsModel::displayUnitChanged,
                this, &OverviewPage::updateDisplayUnit);

        updateWatchOnlyLabels(wallet.haveWatchOnly());
        connect(model, &WalletModel::notifyWatchonlyChanged, this,
                &OverviewPage::updateWatchOnlyLabels);
    }

    // update the display unit, to not use the default ("BCH")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit() {
    if (walletModel && walletModel->getOptionsModel()) {
        if (m_balances.balance != -SATOSHI) {
            setBalance(m_balances);
        }

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings) {
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow) {
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}
