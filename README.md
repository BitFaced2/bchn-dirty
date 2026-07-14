# BCHN Dirty

> ⚠️ **This is an unofficial UI mod of Bitcoin Cash Node.** Not developed, endorsed, or supported by the BCHN team. For the canonical, audited node client visit **[bitcoincashnode.org](https://bitcoincashnode.org)**.

An unofficial UI mod of [Bitcoin Cash Node](https://bitcoincashnode.org/) v29.0.0 by [@Bit_Faced](https://x.com/Bit_Faced).

**Same consensus, wallet, and networking code as mainline BCHN — only the Qt GUI is different.**

## Download

Grab `bchn_dirty.exe` from the [Releases page](https://github.com/BitFaced2/bchn-dirty/releases). Verify the SHA256 checksum listed in the release notes before running.

```powershell
Get-FileHash bchn_dirty.exe -Algorithm SHA256
```

## What changed vs mainline BCHN

Only files under `src/qt/` (plus one `depends/` build fix) — no changes to consensus, wallet, mempool, RPC, P2P, or any code that touches money or the chain.

- Global QSS theme: near-black background, green primary (`#0AC18E`), purple accent (`#9D4EDD`)
- Bundled fonts (Rajdhani / JetBrains Mono / Orbitron) so no system install needed
- Overview page rebuilt as a single dashboard: Balances + Node cockpit on top, Send + Receive side-by-side, full transaction history below
- Frameless custom title bar
- Cockpit block-count widget with a huge Orbitron block number
- Modernized 'Request payment' (QR) modal with theme fonts + copy/save actions
- Sync overlay reduced from full-window dim to a compact top-right toast that lets you keep using the dashboard while resyncing
- Custom app icon + `BCHN Dirty` branding (splash, window title, About, `.exe` metadata)

See the full commit list on the [`bchn-dirty` branch](https://github.com/BitFaced2/bchn-dirty/commits/bchn-dirty).

## Verify what actually changed

Upstream base commit: **`89a591f`** (BCHN `master` at v29.0.0 release, from [gitlab.com/bitcoin-cash-node/bitcoin-cash-node](https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node)).

```
git clone https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node upstream
cd upstream && git checkout 89a591f
cd ..
git clone https://github.com/BitFaced2/bchn-dirty
diff -r upstream/src/ bchn-dirty/src/ | less
```

Or use the pre-generated patch attached to each GitHub release (`bchn-dirty.patch`).

## Windows SmartScreen

The binary is not code-signed. On first launch Windows will show *'Windows protected your PC'* — click **More info → Run anyway**.

## License

MIT, matching upstream BCHN. All original copyright notices preserved.

---

# Upstream BCHN README

Bitcoin Cash Node
=================

The goal of Bitcoin Cash Node is to create sound money that is usable by everyone
in the world. We believe this is a civilization-changing technology which will
dramatically increase human flourishing, freedom, and prosperity. The project
aims to achieve this goal by implementing a series of optimizations and
protocol upgrades that will enable peer-to-peer digital cash to scale many
orders of magnitude beyond current limits.

What is Bitcoin Cash?
---------------------

Bitcoin Cash is a digital currency that enables instant payments to anyone,
anywhere in the world. It uses peer-to-peer technology to operate with no
central authority: managing transactions and issuing money are carried out
collectively by the network. Bitcoin Cash is a descendant of Bitcoin. It became
a separate currency from the version supported by Bitcoin Core when the two
split on August 1, 2017. Bitcoin Cash and the Bitcoin Core version of Bitcoin
share the same transaction history up until the split.

What is Bitcoin Cash Node?
--------------------------

[Bitcoin Cash Node](https://www.bitcoincashnode.org) is the name of open-source
software which enables the use of Bitcoin Cash. It is a descendant of the
[Bitcoin Core](https://bitcoincore.org) and [Bitcoin ABC](https://www.bitcoinabc.org)
software projects.

License
-------

Bitcoin Cash Node is released under the terms of the MIT license. See
[COPYING](COPYING) for more information or see
[https://opensource.org/licenses/MIT](https://opensource.org/licenses/MIT).

This product includes software developed by the OpenSSL Project for use in the
[OpenSSL Toolkit](https://www.openssl.org/), cryptographic software written by
[Eric Young](mailto:eay@cryptsoft.com), and UPnP software written by Thomas
Bernard.

Development Process
-------------------

Bitcoin Cash Node development takes place at [https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node](https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node)

There is also a source-only mirror of Bitcoin Cash Node on Github at [https://github.com/bitcoin-cash-node/bitcoin-cash-node](https://github.com/bitcoin-cash-node/bitcoin-cash-node).

If you would like to contribute, please read our [contribution guide](https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node/-/blob/master/CONTRIBUTING.md) and feel
free to contact us directly at [bitcoincashnode.slack.com](https://bitcoincashnode.slack.com) or [t.me/bitcoincashnode](https://t.me/bitcoincashnode).

Disclosure Policy
-----------------

We have a [Disclosure Policy](DISCLOSURE_POLICY.md) for responsible disclosure
of security issues.

Further info
------------

See [doc/README.md](doc/README.md) for further info on installation, building,
development and more.
