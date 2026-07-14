// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2021 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/forms/ui_modaloverlay.h>
#include <qt/modaloverlay.h>

#include <chainparams.h>
#include <qt/guiutil.h>

#include <QPropertyAnimation>
#include <QResizeEvent>

ModalOverlay::ModalOverlay(QWidget *parent)
    : QWidget(parent), ui(new Ui::ModalOverlay), bestHeaderHeight(0),
      bestHeaderDate(QDateTime()), layerIsVisible(false), userClosed(false) {
    ui->setupUi(this);
    connect(ui->closeButton, &QPushButton::clicked, this,
            &ModalOverlay::closeClicked);
    if (parent) {
        parent->installEventFilter(this);
        raise();
    }

    // Compact toast-style overlay: skip the full-window dim, tuck the
    // sync card into the top-right corner, and let mouse events pass
    // through the empty areas so the dashboard stays interactive
    // during a resync.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    ui->bgWidget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    ui->bgWidget->setStyleSheet(
        QStringLiteral("#bgWidget { background: transparent; }"));
    if (auto *l = qobject_cast<QVBoxLayout *>(ui->bgWidget->layout())) {
        l->setContentsMargins(0, 12, 12, 0);
        l->setAlignment(ui->contentWidget, Qt::AlignTop | Qt::AlignRight);
    }
    ui->contentWidget->setStyleSheet(QStringLiteral(
        "#contentWidget { background: rgba(23,34,29,240); "
        "border: 1px solid rgba(157,78,221,90); border-radius: 12px; "
        "padding: 8px; } "
        "QLabel { color: #E8ECEA; background: transparent; "
        "font-family: 'Rajdhani','Inter',sans-serif; font-size: 10pt; }"));
    ui->contentWidget->setFixedWidth(420);

    // Kill the noisy warning icon + long info paragraphs; the compact
    // card only needs the live sync stats and a Hide button.
    ui->warningIcon->hide();
    ui->infoText->hide();
    ui->infoTextStrong->hide();

    blockProcessTime.clear();
    setVisible(false);
}

ModalOverlay::~ModalOverlay() {
    delete ui;
}

bool ModalOverlay::eventFilter(QObject *obj, QEvent *ev) {
    if (obj == parent()) {
        if (ev->type() == QEvent::Resize) {
            QResizeEvent *rev = static_cast<QResizeEvent *>(ev);
            resize(rev->size());
            if (!layerIsVisible) {
                setGeometry(0, height(), width(), height());
            }

        } else if (ev->type() == QEvent::ChildAdded) {
            raise();
        }
    }
    return QWidget::eventFilter(obj, ev);
}

//! Tracks parent widget changes
bool ModalOverlay::event(QEvent *ev) {
    if (ev->type() == QEvent::ParentAboutToChange) {
        if (parent()) {
            parent()->removeEventFilter(this);
        }
    } else if (ev->type() == QEvent::ParentChange) {
        if (parent()) {
            parent()->installEventFilter(this);
            raise();
        }
    }
    return QWidget::event(ev);
}

void ModalOverlay::setKnownBestHeight(int count, const QDateTime &blockDate) {
    if (count > bestHeaderHeight) {
        bestHeaderHeight = count;
        bestHeaderDate = blockDate;
    }
}

void ModalOverlay::tipUpdate(int count, const QDateTime &blockDate,
                             double nVerificationProgress) {
    QDateTime currentDate = QDateTime::currentDateTime();

    // keep a vector of samples of verification progress at height
    blockProcessTime.push_front(
        qMakePair(currentDate.toMSecsSinceEpoch(), nVerificationProgress));

    // show progress speed if we have more than one sample
    if (blockProcessTime.size() >= 2) {
        double progressDelta = 0;
        double progressPerHour = 0;
        qint64 timeDelta = 0;
        qint64 remainingMSecs = 0;
        double remainingProgress = 1.0 - nVerificationProgress;
        for (int i = 1; i < blockProcessTime.size(); i++) {
            QPair<qint64, double> sample = blockProcessTime[i];

            // take first sample after 500 seconds or last available one
            if (sample.first < (currentDate.toMSecsSinceEpoch() - 500 * 1000) ||
                i == blockProcessTime.size() - 1) {
                progressDelta = blockProcessTime[0].second - sample.second;
                timeDelta = blockProcessTime[0].first - sample.first;
                progressPerHour =
                    progressDelta / (double)timeDelta * 1000 * 3600;
                remainingMSecs =
                    (progressDelta > 0)
                        ? remainingProgress / progressDelta * timeDelta
                        : -1;
                break;
            }
        }
        // show progress increase per hour
        ui->progressIncreasePerH->setText(QLocale().toString(progressPerHour * 100, 'f', 2) + "%");

        // show expected remaining time
        if (remainingMSecs >= 0) {
            ui->expectedTimeLeft->setText(
                GUIUtil::formatNiceTimeOffset(remainingMSecs / 1000.0));
        } else {
            ui->expectedTimeLeft->setText(QObject::tr("unknown"));
        }

        static const int MAX_SAMPLES = 5000;
        if (blockProcessTime.count() > MAX_SAMPLES) {
            blockProcessTime.remove(MAX_SAMPLES,
                                    blockProcessTime.count() - MAX_SAMPLES);
        }
    }

    // show the last block date
    ui->newestBlockDate->setText(GUIUtil::dateTimeStrLong(blockDate));

    // show the percentage done according to nVerificationProgress
    ui->percentageProgress->setText(QLocale().toString(nVerificationProgress * 100, 'f', 2) + "%");
    ui->progressBar->setValue(nVerificationProgress * 100);

    if (!bestHeaderDate.isValid()) {
        // not syncing
        return;
    }

    // estimate the number of headers left based on nPowTargetSpacing
    // and check if the gui is not aware of the best header (happens rarely)
    int estimateNumHeadersLeft = bestHeaderDate.secsTo(currentDate) /
                                 Params().GetConsensus().nPowTargetSpacing;
    bool hasBestHeader = bestHeaderHeight >= count;

    // show remaining number of blocks
    if (estimateNumHeadersLeft < HEADER_HEIGHT_DELTA_SYNC && hasBestHeader) {
        ui->numberOfBlocksLeft->setText(
            QString::number(bestHeaderHeight - count));
    } else {
        ui->numberOfBlocksLeft->setText(
            tr("Unknown. Syncing Headers (%1)...").arg(bestHeaderHeight));
        ui->expectedTimeLeft->setText(tr("Unknown..."));
    }
}

void ModalOverlay::toggleVisibility() {
    showHide(layerIsVisible, true);
    if (!layerIsVisible) {
        userClosed = true;
    }
}

void ModalOverlay::showHide(bool hide, bool userRequested) {
    if ((layerIsVisible && !hide) || (!layerIsVisible && hide) ||
        (!hide && userClosed && !userRequested)) {
        return;
    }

    if (!isVisible() && !hide) {
        setVisible(true);
    }

    setGeometry(0, hide ? 0 : height(), width(), height());

    QPropertyAnimation *animation = new QPropertyAnimation(this, "pos");
    animation->setDuration(300);
    animation->setStartValue(QPoint(0, hide ? 0 : this->height()));
    animation->setEndValue(QPoint(0, hide ? this->height() : 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    layerIsVisible = !hide;
}

void ModalOverlay::closeClicked() {
    showHide(true);
    userClosed = true;
}
