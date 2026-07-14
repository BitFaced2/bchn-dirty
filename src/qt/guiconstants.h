// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

/* Milliseconds between model updates */
static const int MODEL_UPDATE_DELAY = 250;

/* AskPassphraseDialog -- Maximum passphrase length */
static const int MAX_PASSPHRASE_SIZE = 1024;

/* BitcoinGUI -- Size of icons in status bar */
static const int STATUSBAR_ICONSIZE = 16;

/** Default for -splash */
static constexpr bool DEFAULT_SPLASHSCREEN = true;

/* Invalid field background style */
#define STYLE_INVALID "background:#3A1E24; color:#E85D6B; border:1px solid #E85D6B;"
/* Intermediate state field background style */
#define STYLE_INTERMEDIATE "background:#332A15; color:#E8B547; border:1px solid #E8B547;"

/* Transaction list -- unconfirmed transaction */
#define COLOR_UNCONFIRMED QColor(123, 133, 127)
/* Transaction list -- negative amount (spend) — amber, not harsh red */
#define COLOR_NEGATIVE QColor(232, 181, 71)
/* Transaction list -- bare address (without label) */
#define COLOR_BAREADDRESS QColor(74, 84, 79)
/* Transaction list -- TX status decoration - open until date */
#define COLOR_TX_STATUS_OPENUNTILDATE QColor(157, 78, 221)
/* Transaction list -- TX status decoration - danger, tx needs attention */
#define COLOR_TX_STATUS_DANGER QColor(232, 93, 107)
/* Transaction list -- TX status decoration - default color (was black; now theme text) */
#define COLOR_BLACK QColor(232, 236, 234)

/* Tooltips longer than this (in characters) are converted into rich text,
   so that they can be word-wrapped.
 */
static const int TOOLTIP_WRAP_THRESHOLD = 80;

/* Maximum allowed URI length */
static const int MAX_URI_LENGTH = 255;

/* QRCodeDialog -- size of exported QR Code image */
#define QR_IMAGE_SIZE 350

/* Number of frames in spinner animation */
#define SPINNER_FRAMES 36

#define QAPP_ORG_NAME "BitcoinCashNode"
#define QAPP_ORG_DOMAIN "bitcoincashnode.org"
#define QAPP_APP_NAME_DEFAULT "BitcoinCashNode-Qt"
#define QAPP_APP_NAME_TESTNET "BitcoinCashNode-Qt-testnet"
#define QAPP_APP_NAME_TESTNET4 "BitcoinCashNode-Qt-testnet4"
#define QAPP_APP_NAME_SCALENET "BitcoinCashNode-Qt-scalenet"
#define QAPP_APP_NAME_CHIPNET "BitcoinCashNode-Qt-chipnet"
