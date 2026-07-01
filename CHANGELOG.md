# react-native-monero

## Unreleased

- fixed: Monero Full Node (monerod) sends no longer fail with "Failed to load transaction from file". `createTransaction` now retains the signed transaction natively and `broadcastTransaction` commits it directly, instead of saving it to a file and reloading it: on the full-node (wallet2) backend `save_tx` writes an unsigned tx set while `submitTransaction`'s `load_tx` requires a signed one, so the file round-trip always failed. Retained transactions are capped at 50 per wallet (oldest disposed first) and cleared when the wallet closes. The JS API is unchanged.

## 0.2.0 (2026-06-29)

- changed: Switch the package manager from yarn to npm (package-lock.json + `.npmrc` legacy-peer-deps), matching the other Edge currency repos.
- fixed: Monero Full Node (monerod) wallets now sync, calculate max, and send when the Nym mixnet is enabled. The Nym HTTP client rebuilt request URLs from an unreliable SSL flag and emitted `http://` on port 443, so every monerod RPC failed under Nym while the same wallet worked with Nym off. Derive the scheme from the daemon address (and treat port 443 as https).
- fixed: `getWalletStatus` now reads the live wallet balance on every call instead of only recomputing it when the synced block height changes, so a pending incoming transaction (or pending change after a send) is reflected immediately rather than after the next block.

## 0.1.0 (2026-06-15)

- Initial release
