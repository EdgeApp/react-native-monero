# react-native-monero

## Unreleased

- changed: Switch the package manager from yarn to npm (package-lock.json + `.npmrc` legacy-peer-deps), matching the other Edge currency repos.
- fixed: Monero Full Node (monerod) wallets now sync, calculate max, and send when the Nym mixnet is enabled. The Nym HTTP client rebuilt request URLs from an unreliable SSL flag and emitted `http://` on port 443, so every monerod RPC failed under Nym while the same wallet worked with Nym off. Derive the scheme from the daemon address (and treat port 443 as https).
- fixed: `getWalletStatus` now reads the live wallet balance on every call instead of only recomputing it when the synced block height changes, so a pending incoming transaction (or pending change after a send) is reflected immediately rather than after the next block.
