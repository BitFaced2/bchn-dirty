// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2022 The Bitcoin Cash Node developers
// Copyright (c) 2017-2025 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/transactionrecord.h>

#include <cashaddrenc.h>
#include <chain.h>       // For MAX_BLOCK_TIME_GAP
#include <chainparams.h> // For Params()
#include <consensus/consensus.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <script/script.h>
#include <timedata.h>
#include <tinyformat.h>
#include <validation.h>

#include <QDateTime>

#include <cstdint>

/**
 * Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction() {
    // There are currently no cases where we hide transactions, but we may want
    // to use this in the future for things like RBF.
    return true;
}

/**
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord>
TransactionRecord::decomposeTransaction(const interfaces::WalletTx &wtx) {
    QList<TransactionRecord> parts;
    int64_t nTime = wtx.time;
    Amount nCredit = wtx.credit;
    Amount nDebit = wtx.debit;
    Amount nNet = nCredit - nDebit;
    const TxId &txid = wtx.tx->GetId();
    std::map<std::string, std::string> mapValue = wtx.value_map;

    if (nNet > Amount::zero() || wtx.is_coinbase) {
        //
        // Credit
        //
        for (size_t i = 0; i < wtx.tx->vout.size(); i++) {
            const CTxOut &txout = wtx.tx->vout[i];
            isminetype mine = wtx.txout_is_mine[i];
            if (mine) {
                TransactionRecord sub(txid, nTime);
                sub.idx = i; // vout index
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (wtx.txout_address_is_mine[i]) {
                    // Received by Bitcoin Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.address =
                        EncodeCashAddr(wtx.txout_address[i], Params());
                } else {
                    // Received by IP connection (deprecated features), or a
                    // multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.address = mapValue["from"];
                }
                if (wtx.is_coinbase) {
                    // Generated
                    sub.type = TransactionRecord::Generated;
                }

                parts.append(sub);
            }
        }
    } else {
        bool involvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (const isminetype mine : wtx.txin_is_mine) {
            if (mine & ISMINE_WATCH_ONLY) {
                involvesWatchAddress = true;
            }
            if (fAllFromMe > mine) {
                fAllFromMe = mine;
            }
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        for (const isminetype mine : wtx.txout_is_mine) {
            if (mine & ISMINE_WATCH_ONLY) {
                involvesWatchAddress = true;
            }
            if (fAllToMe > mine) {
                fAllToMe = mine;
            }
        }

        if (fAllFromMe && fAllToMe) {
            // Payment to self
            Amount nChange = wtx.change;

            parts.append(TransactionRecord(
                txid, nTime, TransactionRecord::SendToSelf, "",
                -1 * (nDebit - nChange), (nCredit - nChange)));
            // maybe pass to TransactionRecord as constructor argument
            parts.last().involvesWatchAddress = involvesWatchAddress;
        } else if (fAllFromMe) {
            //
            // Debit
            //
            Amount nTxFee = nDebit - wtx.tx->GetValueOut();

            for (size_t nOut = 0; nOut < wtx.tx->vout.size(); nOut++) {
                const CTxOut &txout = wtx.tx->vout[nOut];
                TransactionRecord sub(txid, nTime);
                sub.idx = nOut;
                sub.involvesWatchAddress = involvesWatchAddress;

                if (wtx.txout_is_mine[nOut]) {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                if (!std::get_if<CNoDestination>(&wtx.txout_address[nOut])) {
                    // Sent to Bitcoin Address
                    sub.type = TransactionRecord::SendToAddress;
                    sub.address =
                        EncodeCashAddr(wtx.txout_address[nOut], Params());
                } else {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.type = TransactionRecord::SendToOther;
                    sub.address = mapValue["to"];
                }

                Amount nValue = txout.nValue;
                /* Add fee to first output */
                if (nTxFee > Amount::zero()) {
                    nValue += nTxFee;
                    nTxFee = Amount::zero();
                }
                sub.debit = -1 * nValue;

                parts.append(sub);
            }
        } else {
            //
            // Mixed debit transaction, can't break down payees
            //
            parts.append(TransactionRecord(txid, nTime,
                                           TransactionRecord::Other, "", nNet,
                                           Amount::zero()));
            parts.last().involvesWatchAddress = involvesWatchAddress;
        }
    }
    parts.last().dsProof = wtx.dsProof;

    // ── Qube-aware tagging (BCHN Dirty) ─────────────────────────────────────
    // Recognize Qube protocol transactions by their on-chain shape so the
    // qubes-watch wallet's list reads as a life story instead of "(n/a)":
    // genesis and tombstone announce themselves in OP_RETURN; anchors and
    // level-ups re-emit the covenant (P2SH32 + mutable NFT, level in byte 57
    // of the 59-byte commitment); tidies and the melt's reserve reclaim are
    // pure self-sends. The tag rides on every part; the table model only
    // DISPLAYS it while the qubes-watch wallet is active.
    {
        std::string opret;
        bool covenantOut = false;
        uint8_t covenantLevel = 0;
        for (const CTxOut &txout : wtx.tx->vout) {
            const CScript &s = txout.scriptPubKey;
            if (s.size() > 2 && s[0] == OP_RETURN) {
                size_t off = 0, len = 0;
                const uint8_t push = s[1];
                if (push >= 1 && push <= 75) {
                    len = push;
                    off = 2;
                } else if (push == OP_PUSHDATA1 && s.size() > 3) {
                    len = s[2];
                    off = 3;
                }
                if (len > 0 && off + len <= s.size()) {
                    opret.assign(s.begin() + off, s.begin() + off + len);
                }
            }
            if (txout.tokenDataPtr && s.size() == 35 && s[0] == OP_HASH256 &&
                s[34] == OP_EQUAL) {
                covenantOut = true;
                const auto &c = txout.tokenDataPtr->GetCommitment();
                if (c.size() == 59) {
                    covenantLevel = c[57];
                }
            }
        }
        // The covenant's unlocking script names its function: the selector
        // rides as the second-to-last push, just before the (large) redeem
        // script — checkpointRoot=0, levelUp=1, addReserve=2, rotate=3,
        // sell=4, melt=5. Precise, straight from the spend itself.
        int covenantSelector = -1;
        for (const CTxIn &txin : wtx.tx->vin) {
            const CScript &u = txin.scriptSig;
            std::vector<std::pair<opcodetype, std::vector<uint8_t>>> ops;
            CScript::const_iterator it = u.begin();
            opcodetype op;
            std::vector<uint8_t> data;
            bool parsed = true;
            while (it < u.end()) {
                data.clear();
                if (!u.GetOp(it, op, data)) {
                    parsed = false;
                    break;
                }
                ops.emplace_back(op, data);
            }
            if (!parsed || ops.size() < 2 || ops.back().second.size() < 400) {
                continue; // not a covenant spend (redeem scripts are big)
            }
            const auto &sel = ops[ops.size() - 2];
            int v = -1;
            if (sel.first == OP_0) {
                v = 0;
            } else if (sel.first >= OP_1 && sel.first <= OP_16) {
                v = sel.first - OP_1 + 1;
            } else if (sel.second.size() == 1) {
                v = sel.second[0];
            }
            if (v >= 0) {
                covenantSelector = v;
                break;
            }
        }
        // A genesis mints its category from the spent outpoint-0: token
        // category == the first input's prevout txid. Catches hash-only
        // geneses (no readable OP_RETURN) too.
        bool genesisOut = false;
        if (covenantOut && !wtx.tx->vin.empty()) {
            const TxId &src = wtx.tx->vin[0].prevout.GetTxId();
            for (const CTxOut &txout : wtx.tx->vout) {
                if (txout.tokenDataPtr) {
                    const auto &cat = txout.tokenDataPtr->GetId();
                    if (std::equal(cat.begin(), cat.end(), src.begin())) {
                        genesisOut = true;
                    }
                    break;
                }
            }
        }
        // Geneses and tombstones carry the soul's NAME in their OP_RETURN
        // ("QUBE GENESIS Hal 404A2228: …") — embed it directly. Other rows
        // resolve their name in the table model via the qube's own labeled
        // address (qubeAddr below).
        auto opretName = [&opret](const char *prefix) -> std::string {
            std::string rest = opret.substr(std::string(prefix).size());
            const size_t sp = rest.find(' ');
            return sp == std::string::npos ? rest : rest.substr(0, sp);
        };
        std::string tag;
        if (opret.rfind("QUBE GENESIS ", 0) == 0) {
            const std::string n = opretName("QUBE GENESIS ");
            tag = "⬢ " + (n.empty() ? std::string("Qube") : n) + " · genesis · first words";
        } else if (opret.rfind("QUBE TOMBSTONE ", 0) == 0) {
            const std::string n = opretName("QUBE TOMBSTONE ");
            tag = "🪦 " + (n.empty() ? std::string("Qube") : n) + " · tombstone · last words";
        } else if (covenantSelector == 0) {
            tag = strprintf("⛓ Qube anchor · memory root (L%d)", covenantLevel);
        } else if (covenantSelector == 1) {
            tag = strprintf("⬆ Qube level-up → L%d", covenantLevel);
        } else if (covenantSelector == 5) {
            tag = "♻ Qube melt · reserve reclaimed";
        } else if (covenantSelector == 2) {
            tag = "💎 Qube reserve top-up";
        } else if (covenantSelector == 3) {
            tag = "🔑 Qube key rotation";
        } else if (covenantSelector == 4) {
            tag = "🤝 Qube sale";
        } else if (genesisOut) {
            tag = "⬢ Qube genesis";
        } else if (covenantOut) {
            tag = strprintf("⛓ Qube covenant (L%d)", covenantLevel);
        } else if (!parts.isEmpty() &&
                   parts.last().type == TransactionRecord::SendToSelf) {
            tag = !opret.empty() ? "⛓ Qube post to chain" : "♻ Qube tidy";
        } else if (!opret.empty() && nNet < Amount::zero()) {
            tag = "⛓ Qube post to chain";
        }
        if (!tag.empty()) {
            // The qube's own address in this tx — the model turns it into the
            // soul's name via the address book.
            std::string mineAddr;
            for (size_t i = 0; i < wtx.tx->vout.size(); i++) {
                if (wtx.txout_is_mine[i] &&
                    !std::get_if<CNoDestination>(&wtx.txout_address[i])) {
                    mineAddr = EncodeCashAddr(wtx.txout_address[i], Params());
                    break;
                }
            }
            for (auto &p : parts) {
                p.qubeTag = tag;
                p.qubeAddr = mineAddr;
            }
        }
    }

    return parts;
}

void TransactionRecord::updateStatus(const interfaces::WalletTxStatus &wtx,
                                     int numBlocks, int64_t block_time) {
    // Determine transaction status

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d", wtx.block_height,
                               wtx.is_coinbase ? 1 : 0, wtx.time_received, idx);
    status.countsForBalance = wtx.is_trusted && !(wtx.blocks_to_maturity > 0);
    status.depth = wtx.depth_in_main_chain;
    status.cur_num_blocks = numBlocks;

    const bool up_to_date =
        (int64_t(QDateTime::currentMSecsSinceEpoch()) / 1000 - block_time <
         MAX_BLOCK_TIME_GAP);
    if (wtx.is_double_spent) {
        status.status = TransactionStatus::DoubleSpent;
    } else if (up_to_date && !wtx.is_final) {
        if (wtx.lock_time < LOCKTIME_THRESHOLD) {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.lock_time - numBlocks;
        } else {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.lock_time;
        }
    } else if (type == TransactionRecord::Generated) {
        // For generated transactions, determine maturity
        if (wtx.blocks_to_maturity > 0) {
            status.status = TransactionStatus::Immature;

            if (wtx.is_in_main_chain) {
                status.matures_in = wtx.blocks_to_maturity;
            } else {
                status.status = TransactionStatus::NotAccepted;
            }
        } else {
            status.status = TransactionStatus::Confirmed;
        }
    } else {
        if (status.depth < 0) {
            status.status = TransactionStatus::Conflicted;
        } else if (status.depth == 0) {
            status.status = TransactionStatus::Unconfirmed;
            if (wtx.is_abandoned) {
                status.status = TransactionStatus::Abandoned;
            }
        } else if (status.depth < RecommendedNumConfirmations) {
            status.status = TransactionStatus::Confirming;
        } else {
            status.status = TransactionStatus::Confirmed;
        }
    }
}

bool TransactionRecord::statusUpdateNeeded(int numBlocks) const {
    return status.cur_num_blocks != numBlocks;
}

QString TransactionRecord::getTxID() const {
    return QString::fromStdString(txid.ToString());
}

int TransactionRecord::getOutputIndex() const {
    return idx;
}
