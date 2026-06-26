# react-native-monero

## Unreleased

- fixed: `getWalletStatus` now reads the live wallet balance on every call instead of only recomputing it when the synced block height changes, so a pending incoming transaction (or pending change after a send) is reflected immediately rather than after the next block.
