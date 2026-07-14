# BCHN Dirty

An unofficial UI mod of [Bitcoin Cash Node](https://bitcoincashnode.org/) v29.0.0.

**Same consensus, wallet, and networking code as mainline BCHN — only the Qt GUI is different.**

Built as a personal cypherpunk / futuristic redesign of the BCHN dashboard.

---

## What changed vs mainline BCHN

Only files under `src/qt/` (plus one `depends/` build fix) — no changes to consensus, wallet, mempool, RPC, P2P, or any code that touches money or the chain.

- Global QSS theme: near-black background, green primary (`#0AC18E`), purple accent (`#9D4EDD`)
- Bundled fonts (Rajdhani / JetBrains Mono / Orbitron) so no system install needed
- Overview page rebuilt as a single dashboard: Balances + Node cockpit on top, Send + Receive side-by-side, full transaction history below
- Frameless custom title bar
- Cockpit block-count widget with a huge Orbitron block number
- Modernized "Request payment" (QR) modal with theme fonts + copy/save actions
- Sync overlay reduced from full-window dim to a compact top-right toast that lets you keep using the dashboard while resyncing
- Custom app icon + `BCHN Dirty` branding (splash, window title, About, `.exe` metadata)

See the full commit list on the `bchn-dirty` branch.

## Verify what actually changed

Upstream base commit: **`89a591f`** (BCHN `master` at v29.0.0 release, from `gitlab.com/bitcoin-cash-node/bitcoin-cash-node`).

```
git clone https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node upstream
cd upstream && git checkout 89a591f
cd ..
git clone https://github.com/BitFaced2/bchn-dirty
diff -r upstream/src/ bchn-dirty/src/ | less
```

Or use the pre-generated patch attached to each GitHub release (`bchn-dirty.patch`) to apply the exact diff on top of upstream.

## Download

Grab `bchn_dirty.exe` from the [Releases page](https://github.com/BitFaced2/bchn-dirty/releases).

Verify the SHA256 checksum listed in the release notes before running:

```powershell
Get-FileHash bchn_dirty.exe -Algorithm SHA256
```

## Install / Run

1. Download `bchn_dirty.exe` (Windows x64).
2. Verify SHA256.
3. Double-click. Windows SmartScreen will warn about an unsigned binary — click **More info → Run anyway** (the binary is not code-signed).
4. On first run, it will use the same data directory as mainline BCHN if one exists, or prompt for one.

Your wallet and chain data are fully interchangeable with mainline BCHN.

## Not affiliated

This is a personal fork by [@Bit_Faced](https://x.com/Bit_Faced). It is **not** developed, endorsed, or supported by the Bitcoin Cash Node team. For the canonical, audited node client visit **[bitcoincashnode.org](https://bitcoincashnode.org)**.

## License

MIT, matching upstream BCHN. All original copyright notices preserved.
