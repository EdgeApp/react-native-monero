#include <stdio.h>
#include <string>
#include <stdexcept>
#include <map>
#include <set>
#include <algorithm>
#include <vector>
#include <sstream>
#include <fstream>
#include "monero-methods.hpp"
#include "wallet/api/wallet2_api.h"
#include "lws_frontend.h"

// Lower-level utilities for key generation without disk I/O
#include "cryptonote_basic/account.h"
#include "cryptonote_basic/cryptonote_basic_impl.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "mnemonics/electrum-words.h"
#include "string_tools.h"
#include "net/abstract_http_client.h"
#include <boost/algorithm/string.hpp>

// Forward declaration for LWSF api_key support (defined in patched rpc.cpp)
namespace lwsf { namespace config {
  void set_api_key(const std::string& k);
}}

// Counter for unique temp file names
static uint64_t g_tx_file_counter = 0;

// --- Global wallet-event callback (thread-safe) ---
static std::mutex g_eventCbMutex;
static WalletEventCallback g_walletEventCallback;

void setWalletEventCallback(WalletEventCallback cb) {
  std::lock_guard<std::mutex> lock(g_eventCbMutex);
  g_walletEventCallback = std::move(cb);
}

static void emitWalletEvent(const std::string& walletId,
                            const std::string& eventName,
                            const std::string& jsonPayload) {
  std::lock_guard<std::mutex> lock(g_eventCbMutex);
  if (g_walletEventCallback) {
    g_walletEventCallback(walletId, eventName, jsonPayload);
  }
}

std::string hello(const std::vector<const std::string> &args) {
  printf("LWSF says hello\n");
  return "hello";
}

// WalletListener implementation - handles wallet events and auto-saves during sync
class WalletListeners : public Monero::WalletListener {
public:
  WalletListeners(Monero::Wallet* wallet, const std::string& walletId)
    : m_wallet(wallet), m_walletId(walletId), m_lastSaveHeight(0) {}
  virtual ~WalletListeners() {}
  
  void moneySpent(const std::string &txId, uint64_t amount) override {}

  void moneyReceived(const std::string &txId, uint64_t amount) override {}

  void unconfirmedMoneyReceived(const std::string &txId, uint64_t amount) override {
    emitWalletEvent(m_walletId, "unconfirmedMoneyReceived",
      "{\"txId\":\"" + txId + "\",\"amount\":" + std::to_string(amount) + "}");
  }
  
  void newBlock(uint64_t height) override {
    // Save progress every 1000 blocks during INITIAL sync only.
    // Once synchronized(), refreshed() takes over save responsibility.
    // This is safe because newBlock() is called from the refresh thread.
    const uint64_t SAVE_INTERVAL_BLOCKS = 1000;
    
    // Only save during initial sync (before wallet is fully synchronized)
    if (m_wallet->synchronized()) {
      return; // Let refreshed() handle saves once fully synced
    }
    
    if (height >= m_lastSaveHeight + SAVE_INTERVAL_BLOCKS) {
      try {
        m_wallet->store("");
        m_lastSaveHeight = height;
      } catch (...) {
        // Ignore store errors during sync - will retry on next interval
      }
    }
  }
  
  void updated() override {}
  
  void refreshed() override {
    // Called when refresh cycle completes - safe to store here
    try {
      m_wallet->store("");
      m_lastSaveHeight = m_wallet->blockChainHeight();
    } catch (...) {
      // Ignore store errors - will retry on next refresh
    }
  }

private:
  Monero::Wallet* m_wallet;
  std::string m_walletId;
  uint64_t m_lastSaveHeight;
};

// Wallet tracking structure
struct WalletEntry {
  Monero::Wallet* wallet;
  WalletListeners* listener;  // Listener for auto-save on refresh
  std::string backend; // "lws" or "monero"
  std::string path;
  std::string wallet_id;
  
  // Cache for optimization
  uint64_t cached_synced_height = 0;
  uint64_t cached_balance = 0;
  uint64_t cached_unlocked_balance = 0;
};

// Global state - stores all open wallets by ID
static std::map<std::string, WalletEntry> g_wallets;

// Helper to get wallet manager based on backend type
static Monero::WalletManager* getWalletManager(const std::string& backend) {
  if (backend == "lws") {
    return lwsf::WalletManagerFactory::getWalletManager();
  } else {
    return Monero::WalletManagerFactory::getWalletManager();
  }
}

// Helper to find wallet by ID or throw exception
static WalletEntry& findWalletOrThrow(const std::string& wallet_id) {
  auto it = g_wallets.find(wallet_id);
  if (it == g_wallets.end()) {
    throw std::runtime_error("Wallet not found");
  }
  return it->second;
}

// Generate a new wallet's keys in memory (no disk I/O)
// Args: nettype, language
// Returns: JSON with mnemonic, secretSpendKey, publicSpendKey
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

// Derive all keys from a mnemonic (no disk I/O)
// Args: mnemonic, nettype
// Returns: JSON with address, secretViewKey, publicViewKey, secretSpendKey, publicSpendKey
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

// Get network blockchain height from daemon
// Args: backend, nettype, daemonAddress
// Returns: blockchain height as string
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
// Validate a Monero address
// Args: address, nettype
// Returns: "true" or "false"
std::string isValidAddress(const std::vector<const std::string> &args) {
  std::string address = args[0];
  int nettype = std::stoi(args[1]);
  Monero::NetworkType network = static_cast<Monero::NetworkType>(nettype);

  // addressValid is a static method on Monero::Wallet
  bool valid = Monero::Wallet::addressValid(address, network);
  return valid ? "true" : "false";
}

// Open or create a wallet
// Args: documentDirectory, walletId, backend, mnemonic, password, nettype, restoreHeight, daemonAddress
// Returns: JSON with syncedHeight, networkHeight, balance, and unlockedBalance
std::string openWallet(const std::vector<const std::string> &args) {
  std::string document_directory = args[0];
  std::string wallet_id = args[1];
  std::string backend = args[2];
  std::string mnemonic = args[3];
  std::string password = args[4];
  int nettype = std::stoi(args[5]);
  uint64_t restore_height = std::stoull(args[6]);
  std::string daemon_address = args[7];
  
  Monero::NetworkType network = static_cast<Monero::NetworkType>(nettype);
  Monero::WalletManager* manager = getWalletManager(backend);
  
  // Check if wallet is already open
  auto it = g_wallets.find(wallet_id);
  if (it != g_wallets.end()) {
    // Wallet already open, ensure it's refreshing and return current status
    WalletEntry& entry = it->second;
    Monero::Wallet* wallet = entry.wallet;
    wallet->startRefresh();
    
    uint64_t synced_height = wallet->blockChainHeight();
    uint64_t network_height = wallet->daemonBlockChainHeight();
    uint64_t balance = wallet->balanceAll();
    uint64_t unlocked_balance = wallet->unlockedBalanceAll();
    
    // Update cache
    entry.cached_synced_height = synced_height;
    entry.cached_balance = balance;
    entry.cached_unlocked_balance = unlocked_balance;
    
    std::string json = "{";
    json += "\"syncedHeight\":" + std::to_string(synced_height) + ",";
    json += "\"networkHeight\":" + std::to_string(network_height) + ",";
    json += "\"balance\":" + std::to_string(balance) + ",";
    json += "\"unlockedBalance\":" + std::to_string(unlocked_balance);
    json += "}";
    return json;
  }
  
  // Build wallet path from backend and wallet_id
  std::string path = document_directory + "/" + backend + "_" + wallet_id;
  
  Monero::Wallet* wallet = nullptr;
  
  // Check if wallet exists on disk
  if (manager->walletExists(path)) {
    // Open existing wallet
    wallet = manager->openWallet(path, password, network);
    wallet->setRecoveringFromSeed(true);  // Prevent doInit from overriding restore height
  } else {
    // Create new wallet from mnemonic
    wallet = manager->recoveryWallet(path, password, mnemonic, network, restore_height);
  }
  
  if (wallet == nullptr) {
    throw std::runtime_error("Failed to open or create wallet");
  }
  
  // Check wallet status
  if (wallet->status() != Monero::Wallet::Status_Ok) {
    std::string error = wallet->errorString();
    manager->closeWallet(wallet);
    throw std::runtime_error("Wallet error: " + error);
  }
  
  // Initialize wallet with daemon
  bool is_lws = (backend == "lws");
  wallet->init(daemon_address, 0, "", "", false, is_lws, "");

  // Create and set listener for auto-save on refresh completion + event emission
  WalletListeners* listener = new WalletListeners(wallet, wallet_id);
  wallet->setListener(listener);

  // Start background refresh
  wallet->startRefresh();
  
  // Get status (network height may be 0 if not connected)
  uint64_t synced_height = wallet->blockChainHeight();
  uint64_t network_height = wallet->daemonBlockChainHeight();
  uint64_t balance = wallet->balanceAll();
  uint64_t unlocked_balance = wallet->unlockedBalanceAll();
  
  // Store in global map with cache populated
  WalletEntry entry;
  entry.wallet = wallet;
  entry.listener = listener;
  entry.backend = backend;
  entry.path = path;
  entry.wallet_id = wallet_id;
  entry.cached_synced_height = synced_height;
  entry.cached_balance = balance;
  entry.cached_unlocked_balance = unlocked_balance;
  g_wallets[wallet_id] = entry;

  std::string json = "{";
  json += "\"syncedHeight\":" + std::to_string(synced_height) + ",";
  json += "\"networkHeight\":" + std::to_string(network_height) + ",";
  json += "\"balance\":" + std::to_string(balance) + ",";
  json += "\"unlockedBalance\":" + std::to_string(unlocked_balance);
  json += "}";
  
  return json;
}

// Get wallet status (synced and network heights, balances)
// Args: walletId
// Returns: JSON with syncedHeight, networkHeight, balance, and unlockedBalance
std::string getWalletStatus(const std::vector<const std::string> &args) {
  std::string wallet_id = args[0];
  WalletEntry& entry = findWalletOrThrow(wallet_id);
  Monero::Wallet* wallet = entry.wallet;
  
  uint64_t synced_height = wallet->blockChainHeight();
  bool height_changed = (synced_height != entry.cached_synced_height);
  
  // Only recalculate balances if sync progress changed
  if (height_changed) {
    entry.cached_balance = wallet->balanceAll();
    entry.cached_unlocked_balance = wallet->unlockedBalanceAll();
    entry.cached_synced_height = synced_height;
  }

  uint64_t network_height = wallet->daemonBlockChainHeight();
  uint64_t balance = entry.cached_balance;
  uint64_t unlocked_balance = entry.cached_unlocked_balance;
  
  std::string json = "{";
  json += "\"syncedHeight\":" + std::to_string(synced_height) + ",";
  json += "\"networkHeight\":" + std::to_string(network_height) + ",";
  json += "\"balance\":" + std::to_string(balance) + ",";
  json += "\"unlockedBalance\":" + std::to_string(unlocked_balance);
  json += "}";
  
  return json;
}

// Close an open wallet
// Args: walletId
// Returns: "ok"
std::string closeWallet(const std::vector<const std::string> &args) {
  std::string wallet_id = args[0];
  WalletEntry& entry = findWalletOrThrow(wallet_id);
  Monero::WalletManager* manager = getWalletManager(entry.backend);

  // Remove listener before closing wallet
  entry.wallet->setListener(nullptr);
  delete entry.listener;

  // Close the wallet
  manager->closeWallet(entry.wallet);
  
  // Remove from map
  g_wallets.erase(wallet_id);
  
  return "ok";
}

// Get all transactions with pagination
// Args: walletId, page (0-indexed), pageSize, sort ("asc" or "desc")
// Returns: JSON with transactions array, totalCount, page, pageSize
std::string getAllTransactions(const std::vector<const std::string> &args) {
  std::string wallet_id = args[0];
  int page = std::stoi(args[1]);
  int page_size = std::stoi(args[2]);
  bool ascending = (args[3] == "asc");
  
  Monero::Wallet* wallet = findWalletOrThrow(wallet_id).wallet;
  
  // Refresh and get all transactions
  Monero::TransactionHistory* history = wallet->history();
  history->refresh();
  std::vector<Monero::TransactionInfo*> txs = history->getAll();
  
  // Sort: pending always at end, confirmed by blockHeight (asc or desc)
  std::sort(txs.begin(), txs.end(), [ascending](Monero::TransactionInfo* a, Monero::TransactionInfo* b) {
    if (a->isPending() != b->isPending()) return !a->isPending();
    return ascending ? a->blockHeight() < b->blockHeight() : a->blockHeight() > b->blockHeight();
  });
  
  int total_count = static_cast<int>(txs.size());
  int start_index = page * page_size;
  int end_index = std::min(start_index + page_size, total_count);
  
  // Build JSON response
  std::string json = "{\"transactions\":[";
  for (int i = start_index; i < end_index; i++) {
    if (i > start_index) json += ",";
    Monero::TransactionInfo* tx = txs[i];
    json += "{\"hash\":\"" + tx->hash() + "\",";
    json += "\"direction\":" + std::to_string(tx->direction()) + ",";
    json += "\"isPending\":" + std::string(tx->isPending() ? "true" : "false") + ",";
    json += "\"isFailed\":" + std::string(tx->isFailed() ? "true" : "false") + ",";
    json += "\"isCoinbase\":" + std::string(tx->isCoinbase() ? "true" : "false") + ",";
    json += "\"amount\":" + std::to_string(tx->amount()) + ",";
    json += "\"fee\":" + std::to_string(tx->fee()) + ",";
    json += "\"blockHeight\":" + std::to_string(tx->blockHeight()) + ",";
    json += "\"confirmations\":" + std::to_string(tx->confirmations()) + ",";
    json += "\"timestamp\":" + std::to_string(tx->timestamp()) + ",";
    json += "\"paymentId\":\"" + tx->paymentId() + "\",";
    json += "\"description\":\"" + tx->description() + "\",";
    json += "\"label\":\"" + tx->label() + "\",";
    json += "\"unlockTime\":" + std::to_string(tx->unlockTime()) + ",";
    json += "\"subaddrAccount\":" + std::to_string(tx->subaddrAccount());
    
    // Try to get tx key (only available for outgoing transactions we sent)
    try {
      std::string tx_key = wallet->getTxKey(tx->hash());
      if (!tx_key.empty()) {
        json += ",\"txKey\":\"" + tx_key + "\"";
      }
    } catch (...) {
      // tx key not available - that's fine, it's optional
    }
    
    json += "}";
  }
  json += "],\"totalCount\":" + std::to_string(total_count) + ",";
  json += "\"page\":" + std::to_string(page) + ",\"pageSize\":" + std::to_string(page_size) + "}";
  
  return json;
}

// Helper to split a comma-separated string
static std::vector<std::string> splitString(const std::string& str, char delimiter) {
  std::vector<std::string> tokens;
  std::stringstream ss(str);
  std::string token;
  while (std::getline(ss, token, delimiter)) {
    tokens.push_back(token);
  }
  return tokens;
}


// Create a transaction (multi-recipient supported)
// Args: walletId, addresses (comma-separated), amounts (comma-separated), priority, documentDirectory
// Returns: JSON with txid, signedTxHex, and fee
std::string createTransaction(const std::vector<const std::string> &args) {
  std::string wallet_id = args[0];
  std::string addresses_str = args[1];
  std::string amounts_str = args[2];
  int priority = std::stoi(args[3]);
  std::string document_directory = args[4];
  
  WalletEntry& entry = findWalletOrThrow(wallet_id);
  Monero::Wallet* wallet = entry.wallet;
  
  // Parse addresses and amounts
  std::vector<std::string> addresses = splitString(addresses_str, ',');
  std::vector<std::string> amounts_strs = splitString(amounts_str, ',');
  
  if (addresses.empty() || addresses.size() != amounts_strs.size()) {
    throw std::runtime_error("Addresses and amounts must have same length and not be empty");
  }
  
  // Convert amounts to uint64_t
  std::vector<uint64_t> amounts;
  for (const auto& amt : amounts_strs) {
    amounts.push_back(std::stoull(amt));
  }

  // Single destination with amount 0 = sweep (send all minus fee to that address)
  Monero::optional<std::vector<uint64_t>> opt_amounts;
  if (addresses.size() == 1 && amounts.size() == 1 && amounts[0] == 0) {
    opt_amounts = std::nullopt;
  } else {
    opt_amounts = amounts;
  }
  
  // Pause refresh while creating transaction
  wallet->pauseRefresh();
  
  // Create the transaction
  Monero::PendingTransaction* ptx = wallet->createTransactionMultDest(
    addresses,
    "",  // payment_id (empty - integrated addresses handle this)
    opt_amounts,
    0,   // mixin_count (0 = default)
    static_cast<Monero::PendingTransaction::Priority>(priority)
  );
  
  wallet->startRefresh();
  
  if (ptx == nullptr) {
    throw std::runtime_error("Failed to create transaction");
  }
  
  if (ptx->status() != Monero::PendingTransaction::Status_Ok) {
    std::string error = ptx->errorString();
    wallet->disposeTransaction(ptx);
    throw std::runtime_error("Transaction error: " + error);
  }
  
  // Get tx hash and fee before saving
  std::vector<std::string> tx_ids = ptx->txid();
  std::string tx_hash = tx_ids.empty() ? "" : tx_ids[0];
  uint64_t fee = ptx->fee();
  
  // Generate temp file path for saving signed transaction
  std::string temp_file = document_directory + "/tx_" + std::to_string(++g_tx_file_counter) + ".signed";
  
  // Save signed transaction to file (commit with filename saves instead of broadcasting)
  if (!ptx->commit(temp_file, true)) {
    std::string error = ptx->errorString();
    wallet->disposeTransaction(ptx);
    throw std::runtime_error("Failed to save transaction: " + error);
  }
  
  wallet->disposeTransaction(ptx);
  
  // Read file contents
  std::ifstream file(temp_file, std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to read signed transaction file");
  }
  std::string file_contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  file.close();
  
  // Delete temp file
  std::remove(temp_file.c_str());
  
  // Convert to hex string
  std::string signed_tx_hex = epee::string_tools::buff_to_hex_nodelimer(file_contents);
  
  // Return JSON with txid, signedTxHex, and fee
  return "{\"txid\":\"" + tx_hash + "\",\"signedTxHex\":\"" + signed_tx_hex + "\",\"fee\":\"" + std::to_string(fee) + "\"}";
}

// Broadcast a previously created transaction
// Args: walletId, signedTxHex (hex string from createTransaction), documentDirectory
// Returns: "success" on success (txid is obtained from createTransaction result)
std::string broadcastTransaction(const std::vector<const std::string> &args) {
  std::string wallet_id = args[0];
  std::string signed_tx_hex = args[1];
  std::string document_directory = args[2];
  
  // Find the wallet
  WalletEntry& entry = findWalletOrThrow(wallet_id);
  Monero::Wallet* wallet = entry.wallet;
  
  // Convert hex to binary
  std::string signed_tx_blob;
  if (!epee::string_tools::parse_hexstr_to_binbuff(signed_tx_hex, signed_tx_blob)) {
    throw std::runtime_error("Invalid hex string");
  }
  
  // Write to temp file
  std::string temp_file = document_directory + "/tx_broadcast_" + std::to_string(++g_tx_file_counter) + ".signed";
  std::ofstream file(temp_file, std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to create temp file for broadcast");
  }
  file.write(signed_tx_blob.data(), signed_tx_blob.size());
  file.close();
  
  // Submit the transaction
  bool success = wallet->submitTransaction(temp_file);
  
  // Delete temp file
  std::remove(temp_file.c_str());
  
  if (!success) {
    throw std::runtime_error("Broadcast failed: " + wallet->errorString());
  }
  
  return "success";
}

// Helper: escape a string for JSON (escape backslash and double-quote)
static std::string jsonEscape(const std::string& s) {
  std::string result;
  result.reserve(s.size());
  for (char c : s) {
    if (c == '\\') result += "\\\\";
    else if (c == '"') result += "\\\"";
    else if (c == '\n') result += "\\n";
    else if (c == '\r') result += "\\r";
    else if (c == '\t') result += "\\t";
    else result += c;
  }
  return result;
}

// Helper: validate long payment id (64 hex chars = 32 bytes)
static bool isValidLongPaymentId(const std::string& payment_id_str) {
  std::string payment_id_data;
  if (!epee::string_tools::parse_hexstr_to_binbuff(payment_id_str, payment_id_data))
    return false;
  return payment_id_data.size() == 32; // sizeof(crypto::hash)
}

// Parse a monero: URI
// Args: uri, nettype
// Returns: JSON with address, paymentId, amount (atomic string), txDescription, recipientName, unknownParameters[]
// or JSON with error field on failure
std::string parseUri(const std::vector<const std::string> &args) {
  std::string uri = args[0];
  int nettype = std::stoi(args[1]);
  cryptonote::network_type network = static_cast<cryptonote::network_type>(nettype);
  
  // Check scheme
  if (uri.substr(0, 7) != "monero:") {
    return "{\"error\":\"URI has wrong scheme (expected \\\"monero:\\\")\"}";
  }
  
  std::string remainder = uri.substr(7);
  const char *ptr = strchr(remainder.c_str(), '?');
  std::string address = ptr ? remainder.substr(0, ptr - remainder.c_str()) : remainder;
  
  // Validate address
  cryptonote::address_parse_info info;
  if (!cryptonote::get_account_address_from_str(info, network, address)) {
    return "{\"error\":\"URI has wrong address: " + jsonEscape(address) + "\"}";
  }
  
  // Initialize output values
  std::string payment_id;
  uint64_t amount = 0;
  std::string tx_description;
  std::string recipient_name;
  std::vector<std::string> unknown_parameters;
  
  // Parse query parameters if present
  if (strchr(remainder.c_str(), '?')) {
    std::string body = remainder.substr(address.size() + 1);
    if (!body.empty()) {
      std::vector<std::string> arguments;
      boost::split(arguments, body, boost::is_any_of("&"));
      std::set<std::string> have_arg;
      
      for (const auto &arg : arguments) {
        std::vector<std::string> kv;
        boost::split(kv, arg, boost::is_any_of("="));
        if (kv.size() != 2) {
          return "{\"error\":\"URI has wrong parameter: " + jsonEscape(arg) + "\"}";
        }
        if (have_arg.find(kv[0]) != have_arg.end()) {
          return "{\"error\":\"URI has more than one instance of " + jsonEscape(kv[0]) + "\"}";
        }
        have_arg.insert(kv[0]);
        
        if (kv[0] == "tx_amount") {
          if (!cryptonote::parse_amount(amount, kv[1])) {
            return "{\"error\":\"URI has invalid amount: " + jsonEscape(kv[1]) + "\"}";
          }
        } else if (kv[0] == "tx_payment_id") {
          if (info.has_payment_id) {
            return "{\"error\":\"Separate payment id given with an integrated address\"}";
          }
          if (!isValidLongPaymentId(kv[1])) {
            return "{\"error\":\"Invalid payment id: " + jsonEscape(kv[1]) + "\"}";
          }
          payment_id = kv[1];
        } else if (kv[0] == "recipient_name") {
          recipient_name = epee::net_utils::convert_from_url_format(kv[1]);
        } else if (kv[0] == "tx_description") {
          tx_description = epee::net_utils::convert_from_url_format(kv[1]);
        } else {
          unknown_parameters.push_back(arg);
        }
      }
    }
  }
  
  // Build JSON response
  std::string json = "{";
  json += "\"address\":\"" + jsonEscape(address) + "\",";
  json += "\"paymentId\":\"" + jsonEscape(payment_id) + "\",";
  json += "\"amount\":\"" + std::to_string(amount) + "\",";
  json += "\"txDescription\":\"" + jsonEscape(tx_description) + "\",";
  json += "\"recipientName\":\"" + jsonEscape(recipient_name) + "\",";
  json += "\"unknownParameters\":[";
  for (size_t i = 0; i < unknown_parameters.size(); ++i) {
    if (i > 0) json += ",";
    json += "\"" + jsonEscape(unknown_parameters[i]) + "\"";
  }
  json += "]}";
  
  return json;
}

// Encode a monero: URI
// Args: address, paymentId, amount (atomic string), txDescription, recipientName, nettype
// Returns: URI string, or JSON with error field on failure
std::string encodeUri(const std::vector<const std::string> &args) {
  std::string address = args[0];
  std::string payment_id = args[1];
  std::string amount_str = args[2];
  std::string tx_description = args[3];
  std::string recipient_name = args[4];
  int nettype = std::stoi(args[5]);
  cryptonote::network_type network = static_cast<cryptonote::network_type>(nettype);
  
  // Validate address
  cryptonote::address_parse_info info;
  if (!cryptonote::get_account_address_from_str(info, network, address)) {
    return "{\"error\":\"wrong address: " + jsonEscape(address) + "\"}";
  }
  
  // Check payment id constraints
  if (info.has_payment_id && !payment_id.empty()) {
    return "{\"error\":\"A single payment id is allowed\"}";
  }
  if (!payment_id.empty()) {
    return "{\"error\":\"Standalone payment id deprecated, use integrated address instead\"}";
  }
  
  // Parse amount
  uint64_t amount = 0;
  if (!amount_str.empty() && amount_str != "0") {
    try {
      amount = std::stoull(amount_str);
    } catch (...) {
      return "{\"error\":\"Invalid amount: " + jsonEscape(amount_str) + "\"}";
    }
  }
  
  // Build URI
  std::string uri = "monero:" + address;
  unsigned int n_fields = 0;
  
  if (amount > 0) {
    // URI encoded amount is in decimal units, not atomic units
    uri += (n_fields++ ? "&" : "?") + std::string("tx_amount=") + cryptonote::print_money(amount);
  }
  
  if (!recipient_name.empty()) {
    uri += (n_fields++ ? "&" : "?") + std::string("recipient_name=") + epee::net_utils::conver_to_url_format(recipient_name);
  }
  
  if (!tx_description.empty()) {
    uri += (n_fields++ ? "&" : "?") + std::string("tx_description=") + epee::net_utils::conver_to_url_format(tx_description);
  }
  
  return uri;
}

// Set the API key for LWS requests
// Args: apiKey
// Returns: "ok"
std::string setLwsApiKey(const std::vector<const std::string> &args) {
  std::string api_key = args[0];
  lwsf::config::set_api_key(api_key);
  return "ok";
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
  { "createTransaction", 5, createTransaction },
  { "broadcastTransaction", 3, broadcastTransaction },
  { "parseUri", 2, parseUri },
  { "encodeUri", 6, encodeUri },
  { "setLwsApiKey", 1, setLwsApiKey },
};

const unsigned moneroMethodCount = std::end(moneroMethods) - std::begin(moneroMethods);
