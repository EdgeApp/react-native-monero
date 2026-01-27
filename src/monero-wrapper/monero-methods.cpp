#include <stdio.h>
#include <string>
#include <stdexcept>
#include <map>
#include <algorithm>
#include <vector>
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

/** Global state - stores all open wallets by ID. */
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

/**
 * Open or create a wallet.
 * Args: documentDirectory, walletId, backend, mnemonic, password, nettype, restoreHeight, daemonAddress
 * Returns: JSON with syncedHeight, networkHeight, balance, and unlockedBalance
 */
std::string openWallet(const std::vector<const std::string> &args) {
  std::string documentDirectory = args[0];
  std::string walletId = args[1];
  std::string backend = args[2];
  std::string mnemonic = args[3];
  std::string password = args[4];
  int nettype = std::stoi(args[5]);
  uint64_t restoreHeight = std::stoull(args[6]);
  std::string daemonAddress = args[7];
  
  Monero::NetworkType network = static_cast<Monero::NetworkType>(nettype);
  Monero::WalletManager* manager = getWalletManager(backend);
  
  // Check if wallet is already open
  auto it = g_wallets.find(walletId);
  if (it != g_wallets.end()) {
    WalletEntry& entry = it->second;
    Monero::Wallet* wallet = entry.wallet;
    wallet->startRefresh();
    
    uint64_t syncedHeight = wallet->blockChainHeight();
    uint64_t networkHeight = wallet->daemonBlockChainHeight();
    uint64_t balance = wallet->balanceAll();
    uint64_t unlockedBalance = wallet->unlockedBalanceAll();
    
    entry.cachedSyncedHeight = syncedHeight;
    entry.cachedBalance = balance;
    entry.cachedUnlockedBalance = unlockedBalance;
    
    std::string json = "{";
    json += "\"syncedHeight\":" + std::to_string(syncedHeight) + ",";
    json += "\"networkHeight\":" + std::to_string(networkHeight) + ",";
    json += "\"balance\":\"" + std::to_string(balance) + "\",";
    json += "\"unlockedBalance\":\"" + std::to_string(unlockedBalance) + "\"";
    json += "}";
    return json;
  }
  
  std::string path = documentDirectory + "/" + backend + "_" + walletId;
  
  Monero::Wallet* wallet = nullptr;
  
  if (manager->walletExists(path)) {
    wallet = manager->openWallet(path, password, network);
    wallet->setRecoveringFromSeed(true);
  } else {
    wallet = manager->recoveryWallet(path, password, mnemonic, network, restoreHeight);
  }
  
  if (wallet == nullptr) {
    throw std::runtime_error("Failed to open or create wallet");
  }
  
  if (wallet->status() != Monero::Wallet::Status_Ok) {
    std::string error = wallet->errorString();
    manager->closeWallet(wallet);
    throw std::runtime_error("Wallet error: " + error);
  }
  
  bool isLws = (backend == "lws");
  wallet->init(daemonAddress, 0, "", "", false, isLws, "");

  wallet->startRefresh();
  
  uint64_t syncedHeight = wallet->blockChainHeight();
  uint64_t networkHeight = wallet->daemonBlockChainHeight();
  uint64_t balance = wallet->balanceAll();
  uint64_t unlockedBalance = wallet->unlockedBalanceAll();
  
  WalletEntry entry;
  entry.wallet = wallet;
  entry.backend = backend;
  entry.path = path;
  entry.walletId = walletId;
  entry.cachedSyncedHeight = syncedHeight;
  entry.cachedBalance = balance;
  entry.cachedUnlockedBalance = unlockedBalance;
  g_wallets[walletId] = entry;

  std::string json = "{";
  json += "\"syncedHeight\":" + std::to_string(syncedHeight) + ",";
  json += "\"networkHeight\":" + std::to_string(networkHeight) + ",";
  json += "\"balance\":\"" + std::to_string(balance) + "\",";
  json += "\"unlockedBalance\":\"" + std::to_string(unlockedBalance) + "\"";
  json += "}";
  
  return json;
}

/**
 * Get wallet status (synced and network heights, balances).
 * Args: walletId
 * Returns: JSON with syncedHeight, networkHeight, balance, and unlockedBalance
 */
std::string getWalletStatus(const std::vector<const std::string> &args) {
  std::string walletId = args[0];
  WalletEntry& entry = findWalletOrThrow(walletId);
  Monero::Wallet* wallet = entry.wallet;
  
  uint64_t syncedHeight = wallet->blockChainHeight();
  bool heightChanged = (syncedHeight != entry.cachedSyncedHeight);
  
  if (heightChanged) {
    entry.cachedBalance = wallet->balanceAll();
    entry.cachedUnlockedBalance = wallet->unlockedBalanceAll();
    entry.cachedSyncedHeight = syncedHeight;
  }

  uint64_t networkHeight = wallet->daemonBlockChainHeight();
  uint64_t balance = entry.cachedBalance;
  uint64_t unlockedBalance = entry.cachedUnlockedBalance;
  
  std::string json = "{";
  json += "\"syncedHeight\":" + std::to_string(syncedHeight) + ",";
  json += "\"networkHeight\":" + std::to_string(networkHeight) + ",";
  json += "\"balance\":\"" + std::to_string(balance) + "\",";
  json += "\"unlockedBalance\":\"" + std::to_string(unlockedBalance) + "\"";
  json += "}";
  
  return json;
}

/**
 * Close an open wallet.
 * Args: walletId
 * Returns: "ok"
 */
std::string closeWallet(const std::vector<const std::string> &args) {
  std::string walletId = args[0];
  WalletEntry& entry = findWalletOrThrow(walletId);
  Monero::WalletManager* manager = getWalletManager(entry.backend);

  manager->closeWallet(entry.wallet);
  
  g_wallets.erase(walletId);
  
  return "ok";
}

/**
 * Get all transactions with pagination.
 * Args: walletId, page (0-indexed), pageSize, sort ("asc" or "desc")
 * Returns: JSON with transactions array, totalCount, page, pageSize
 */
std::string getAllTransactions(const std::vector<const std::string> &args) {
  std::string walletId = args[0];
  int page = std::stoi(args[1]);
  int pageSize = std::stoi(args[2]);
  bool ascending = (args[3] == "asc");
  
  Monero::Wallet* wallet = findWalletOrThrow(walletId).wallet;
  
  Monero::TransactionHistory* history = wallet->history();
  history->refresh();
  std::vector<Monero::TransactionInfo*> txs = history->getAll();
  
  std::sort(txs.begin(), txs.end(), [ascending](Monero::TransactionInfo* a, Monero::TransactionInfo* b) {
    if (a->isPending() != b->isPending()) return !a->isPending();
    return ascending ? a->blockHeight() < b->blockHeight() : a->blockHeight() > b->blockHeight();
  });
  
  int totalCount = static_cast<int>(txs.size());
  int startIndex = page * pageSize;
  int endIndex = std::min(startIndex + pageSize, totalCount);
  
  std::string json = "{\"transactions\":[";
  for (int i = startIndex; i < endIndex; i++) {
    if (i > startIndex) json += ",";
    Monero::TransactionInfo* tx = txs[i];
    json += "{\"hash\":\"" + tx->hash() + "\",";
    json += "\"direction\":" + std::to_string(tx->direction()) + ",";
    json += "\"isPending\":" + std::string(tx->isPending() ? "true" : "false") + ",";
    json += "\"isFailed\":" + std::string(tx->isFailed() ? "true" : "false") + ",";
    json += "\"isCoinbase\":" + std::string(tx->isCoinbase() ? "true" : "false") + ",";
    json += "\"amount\":\"" + std::to_string(tx->amount()) + "\",";
    json += "\"fee\":\"" + std::to_string(tx->fee()) + "\",";
    json += "\"blockHeight\":" + std::to_string(tx->blockHeight()) + ",";
    json += "\"confirmations\":" + std::to_string(tx->confirmations()) + ",";
    json += "\"timestamp\":" + std::to_string(tx->timestamp()) + ",";
    json += "\"paymentId\":\"" + tx->paymentId() + "\",";
    json += "\"description\":\"" + tx->description() + "\",";
    json += "\"label\":\"" + tx->label() + "\",";
    json += "\"unlockTime\":" + std::to_string(tx->unlockTime()) + ",";
    json += "\"subaddrAccount\":" + std::to_string(tx->subaddrAccount());
    
    try {
      std::string txKey = wallet->getTxKey(tx->hash());
      if (!txKey.empty()) {
        json += ",\"txKey\":\"" + txKey + "\"";
      }
    } catch (...) {
    }
    
    json += "}";
  }
  json += "],\"totalCount\":" + std::to_string(totalCount) + ",";
  json += "\"page\":" + std::to_string(page) + ",\"pageSize\":" + std::to_string(pageSize) + "}";
  
  return json;
}

const MoneroMethod moneroMethods[] = {
  { "hello", 0, hello },
  { "generateWallet", 2, generateWallet },
  { "seedAndKeysFromMnemonic", 2, seedAndKeysFromMnemonic },
  { "getNetworkBlockHeight", 3, getNetworkBlockHeight },
  { "isValidAddress", 2, isValidAddress },
  { "openWallet", 8, openWallet },
  { "getWalletStatus", 1, getWalletStatus },
  { "getAllTransactions", 4, getAllTransactions },
  { "closeWallet", 1, closeWallet },
};

const unsigned moneroMethodCount = std::end(moneroMethods) - std::begin(moneroMethods);
