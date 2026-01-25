#include <stdio.h>
#include <string>
#include <stdexcept>
#include "monero-methods.hpp"
#include "wallet/api/wallet2_api.h"
#include "lws_frontend.h"

/** Lower-level utilities for key generation without disk I/O. */
#include "cryptonote_basic/account.h"
#include "mnemonics/electrum-words.h"
#include "string_tools.h"

std::string hello(const std::vector<const std::string> &args) {
  printf("LWSF says hello\n");
  return "hello";
}

/** Wallet tracking structure. */
struct WalletEntry {
  Monero::Wallet* wallet;
  std::string backend;
  std::string path;
  std::string walletId;
  
  uint64_t cachedSyncedHeight = 0;
  uint64_t cachedBalance = 0;
  uint64_t cachedUnlockedBalance = 0;
};

/**
 * Global state - stores all open wallets by ID.
 *
 * Thread-safety: g_wallets is only ever accessed from the serial bridge queue
 * (iOS DISPATCH_QUEUE_SERIAL, Android single-thread executor), so map reads and
 * writes are never concurrent. The refresh-thread WalletListener uses its own
 * wallet pointer and never touches this map, and closeWallet stops the wallet
 * (joining its refresh thread) before erasing the entry. Do not access
 * g_wallets from any other thread without adding synchronization.
 */
static std::map<std::string, WalletEntry> g_wallets;

/** Helper to get wallet manager based on backend type. */
static Monero::WalletManager* getWalletManager(const std::string& backend) {
  if (backend == "lws") {
    return lwsf::WalletManagerFactory::getWalletManager();
  } else {
    return Monero::WalletManagerFactory::getWalletManager();
  }
}

/** Helper to find wallet by ID or throw exception. */
static WalletEntry& findWalletOrThrow(const std::string& walletId) {
  auto it = g_wallets.find(walletId);
  if (it == g_wallets.end()) {
    throw std::runtime_error("Wallet not found");
  }
  return it->second;
}

/** Helper to find any open wallet matching the given nettype. */
static Monero::Wallet* findWalletByNettype(int nettype) {
  Monero::NetworkType network = static_cast<Monero::NetworkType>(nettype);
  for (const auto& pair : g_wallets) {
    if (pair.second.wallet->nettype() == network) {
      return pair.second.wallet;
    }
  }
  throw std::runtime_error("No open wallet found for the requested network type");
}

/**
 * Generate a new wallet's keys in memory (no disk I/O).
 * Args: nettype, language
 * Returns: JSON with mnemonic, secretSpendKey, publicSpendKey
 */
std::string generateWallet(const std::vector<const std::string> &args) {
  int nettype = std::stoi(args[0]);
  std::string language = args[1];
  
  // Generate keys in memory using account_base (no disk persistence)
  cryptonote::account_base account;
  account.generate();
  
  const auto& keys = account.get_keys();
  
  // Convert spend secret key to mnemonic
  epee::wipeable_string mnemonic;
  if (!crypto::ElectrumWords::bytes_to_words(keys.m_spend_secret_key, mnemonic, language)) {
    throw std::runtime_error("Failed to convert keys to mnemonic");
  }
  
  // Convert keys to hex strings (secret keys need unwrap() to get underlying POD)
  std::string secret_spend_key = epee::string_tools::pod_to_hex(unwrap(unwrap(keys.m_spend_secret_key)));
  std::string public_spend_key = epee::string_tools::pod_to_hex(keys.m_account_address.m_spend_public_key);
  
  // Build JSON response
  std::string json = "{";
  json += "\"mnemonic\":\"" + std::string(mnemonic.data(), mnemonic.size()) + "\",";
  json += "\"secretSpendKey\":\"" + secret_spend_key + "\",";
  json += "\"publicSpendKey\":\"" + public_spend_key + "\"";
  json += "}";
  
  return json;
}

/**
 * Derive all keys from a mnemonic (no disk I/O).
 * Args: mnemonic, nettype
 * Returns: JSON with address, secretViewKey, publicViewKey, secretSpendKey, publicSpendKey
 */
std::string seedAndKeysFromMnemonic(const std::vector<const std::string> &args) {
  std::string mnemonic_str = args[0];
  int nettype = std::stoi(args[1]);
  
  // Convert mnemonic to spend secret key
  crypto::secret_key spend_secret;
  std::string language_name;
  epee::wipeable_string mnemonic_ws(mnemonic_str);
  
  if (!crypto::ElectrumWords::words_to_bytes(mnemonic_ws, spend_secret, language_name)) {
    throw std::runtime_error("Invalid mnemonic");
  }
  
  // Recover account from spend key (derives view key automatically)
  cryptonote::account_base account;
  account.generate(spend_secret, true, false);  // recover=true
  
  const auto& keys = account.get_keys();
  
  // Get address string
  cryptonote::network_type network = static_cast<cryptonote::network_type>(nettype);
  std::string address = account.get_public_address_str(network);
  
  // Convert keys to hex strings (secret keys need unwrap() to get underlying POD)
  std::string secret_view_key = epee::string_tools::pod_to_hex(unwrap(unwrap(keys.m_view_secret_key)));
  std::string public_view_key = epee::string_tools::pod_to_hex(keys.m_account_address.m_view_public_key);
  std::string secret_spend_key = epee::string_tools::pod_to_hex(unwrap(unwrap(keys.m_spend_secret_key)));
  std::string public_spend_key = epee::string_tools::pod_to_hex(keys.m_account_address.m_spend_public_key);
  
  // Build JSON response
  std::string json = "{";
  json += "\"address\":\"" + address + "\",";
  json += "\"secretViewKey\":\"" + secret_view_key + "\",";
  json += "\"publicViewKey\":\"" + public_view_key + "\",";
  json += "\"secretSpendKey\":\"" + secret_spend_key + "\",";
  json += "\"publicSpendKey\":\"" + public_spend_key + "\"";
  json += "}";
  
  return json;
}

/**
 * Get network blockchain height from daemon.
 * Args: backend, nettype, daemonAddress
 * Returns: blockchain height as string
 */
std::string getNetworkBlockHeight(const std::vector<const std::string> &args) {
  std::string backend = args[0];
  int nettype = std::stoi(args[1]);
  std::string daemon_address = args[2];
  
  // The WalletManager is a shared singleton, but its daemon address is only
  // read by this method, which re-sets it on every call before querying. Wallet
  // operations connect via their own wallet->init(), not the manager's address,
  // so this transient mutation cannot perturb other wallets/sessions.
  Monero::WalletManager* manager = getWalletManager(backend);
  manager->setDaemonAddress(daemon_address);
  
  // Check if connected
  if (!manager->connected()) {
    throw std::runtime_error("Failed to connect to daemon at " + daemon_address);
  }
  
  uint64_t height = manager->blockchainHeight();
  return std::to_string(height);
}

/**
 * Validate a Monero address.
 * Args: address, nettype
 * Returns: "true" or "false"
 */
std::string isValidAddress(const std::vector<const std::string> &args) {
  std::string address = args[0];
  int nettype = std::stoi(args[1]);
  Monero::NetworkType network = static_cast<Monero::NetworkType>(nettype);
  
  // addressValid is a static method on Monero::Wallet
  bool valid = Monero::Wallet::addressValid(address, network);
  return valid ? "true" : "false";
}

const MoneroMethod moneroMethods[] = {
  { "hello", 0, hello },
  { "generateWallet", 2, generateWallet },
  { "seedAndKeysFromMnemonic", 2, seedAndKeysFromMnemonic },
  { "getNetworkBlockHeight", 3, getNetworkBlockHeight },
  { "isValidAddress", 2, isValidAddress }
};

const unsigned moneroMethodCount = std::end(moneroMethods) - std::begin(moneroMethods);
