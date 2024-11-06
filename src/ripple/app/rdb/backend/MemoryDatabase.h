#ifndef RIPPLE_APP_RDB_BACKEND_MEMORYDATABASE_H_INCLUDED
#define RIPPLE_APP_RDB_BACKEND_MEMORYDATABASE_H_INCLUDED

#define RDB_CONCURRENT 0

#ifndef RDB_CONCURRENT

#include <ripple/app/rdb/backend/SQLiteDatabase.h>
#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <mutex>
#include <map>
#include <vector>
#include <optional>
#include <shared_mutex>
#include <algorithm>

namespace ripple {

class MemoryDatabase : public SQLiteDatabase {
private:
    struct LedgerData {
        LedgerInfo info;
        std::map<uint256, AccountTx> transactions;
    };

    struct AccountTxData {
        AccountTxs transactions;
        std::map<uint32_t, std::map<uint32_t, size_t>> ledgerTxMap; // ledgerSeq -> txSeq -> index in transactions
    };

    Application& app_;
    Config const& config_;
    JobQueue& jobQueue_;

    mutable std::shared_mutex mutex_;
    std::map<LedgerIndex, LedgerData> ledgers_;
    std::map<uint256, LedgerIndex> ledgerHashToSeq_;
    std::map<uint256, AccountTx> transactionMap_;
    std::map<AccountID, AccountTxData> accountTxMap_;

public:
    MemoryDatabase(Application& app, Config const& config, JobQueue& jobQueue)
        : app_(app), config_(config), jobQueue_(jobQueue) {}

    std::optional<LedgerIndex> getMinLedgerSeq() override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ledgers_.empty())
            return std::nullopt;
        return ledgers_.begin()->first;
    }

    std::optional<LedgerIndex> getTransactionsMinLedgerSeq() override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (transactionMap_.empty())
            return std::nullopt;
        return transactionMap_.begin()->second.second->getLgrSeq();
    }

    std::optional<LedgerIndex> getAccountTransactionsMinLedgerSeq() override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (accountTxMap_.empty())
            return std::nullopt;
        LedgerIndex minSeq = std::numeric_limits<LedgerIndex>::max();
        for (const auto& [_, accountData] : accountTxMap_) {
            if (!accountData.ledgerTxMap.empty())
                minSeq = std::min(minSeq, accountData.ledgerTxMap.begin()->first);
        }
        return minSeq == std::numeric_limits<LedgerIndex>::max() ? std::nullopt : std::optional<LedgerIndex>(minSeq);
    }

    std::optional<LedgerIndex> getMaxLedgerSeq() override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ledgers_.empty())
            return std::nullopt;
        return ledgers_.rbegin()->first;
    }

    void deleteTransactionByLedgerSeq(LedgerIndex ledgerSeq) override {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.find(ledgerSeq);
        if (it != ledgers_.end()) {
            for (const auto& [txHash, _] : it->second.transactions) {
                transactionMap_.erase(txHash);
            }
            it->second.transactions.clear();
        }
        for (auto& [_, accountData] : accountTxMap_) {
            accountData.ledgerTxMap.erase(ledgerSeq);
            accountData.transactions.erase(
                std::remove_if(accountData.transactions.begin(), accountData.transactions.end(),
                    [ledgerSeq](const AccountTx& tx) { return tx.second->getLgrSeq() == ledgerSeq; }),
                accountData.transactions.end());
        }
    }

    void deleteBeforeLedgerSeq(LedgerIndex ledgerSeq) override {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.begin();
        while (it != ledgers_.end() && it->first < ledgerSeq) {
            for (const auto& [txHash, _] : it->second.transactions) {
                transactionMap_.erase(txHash);
            }
            ledgerHashToSeq_.erase(it->second.info.hash);
            it = ledgers_.erase(it);
        }
        for (auto& [_, accountData] : accountTxMap_) {
            auto txIt = accountData.ledgerTxMap.begin();
            while (txIt != accountData.ledgerTxMap.end() && txIt->first < ledgerSeq) {
                txIt = accountData.ledgerTxMap.erase(txIt);
            }
            accountData.transactions.erase(
                std::remove_if(accountData.transactions.begin(), accountData.transactions.end(),
                    [ledgerSeq](const AccountTx& tx) { return tx.second->getLgrSeq() < ledgerSeq; }),
                accountData.transactions.end());
        }
    }

    void deleteTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto& [seq, ledgerData] : ledgers_) {
            if (seq < ledgerSeq) {
                for (const auto& [txHash, _] : ledgerData.transactions) {
                    transactionMap_.erase(txHash);
                }
                ledgerData.transactions.clear();
            }
        }
        for (auto& [_, accountData] : accountTxMap_) {
            auto txIt = accountData.ledgerTxMap.begin();
            while (txIt != accountData.ledgerTxMap.end() && txIt->first < ledgerSeq) {
                txIt = accountData.ledgerTxMap.erase(txIt);
            }
            accountData.transactions.erase(
                std::remove_if(accountData.transactions.begin(), accountData.transactions.end(),
                    [ledgerSeq](const AccountTx& tx) { return tx.second->getLgrSeq() < ledgerSeq; }),
                accountData.transactions.end());
        }
    }

    void deleteAccountTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto& [_, accountData] : accountTxMap_) {
            auto txIt = accountData.ledgerTxMap.begin();
            while (txIt != accountData.ledgerTxMap.end() && txIt->first < ledgerSeq) {
                txIt = accountData.ledgerTxMap.erase(txIt);
            }
            accountData.transactions.erase(
                std::remove_if(accountData.transactions.begin(), accountData.transactions.end(),
                    [ledgerSeq](const AccountTx& tx) { return tx.second->getLgrSeq() < ledgerSeq; }),
                accountData.transactions.end());
        }
    }

    std::size_t getTransactionCount() override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return transactionMap_.size();
    }

    std::size_t getAccountTransactionCount() override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::size_t count = 0;
        for (const auto& [_, accountData] : accountTxMap_) {
            count += accountData.transactions.size();
        }
        return count;
    }

    CountMinMax getLedgerCountMinMax() override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ledgers_.empty())
            return {0, 0, 0};
        return {
            ledgers_.size(),
            ledgers_.begin()->first,
            ledgers_.rbegin()->first
        };
    }


    bool saveValidatedLedger(std::shared_ptr<Ledger const> const& ledger, bool current) override
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        LedgerData ledgerData;
        ledgerData.info = ledger->info();
        auto aLedger = std::make_shared<AcceptedLedger>(ledger, app_);
        
        for (auto const& acceptedLedgerTx : *aLedger) {
            auto const& txn = acceptedLedgerTx->getTxn();
            auto const& meta = acceptedLedgerTx->getMeta();
            auto const& id = txn->getTransactionID();
            std::string reason;
            
            // Create the transaction pair
            auto accTx = std::make_pair(
                std::make_shared<ripple::Transaction>(txn, reason, app_),
                std::make_shared<ripple::TxMeta>(meta)
            );
            
            // Store in ledger data and global transaction map
            ledgerData.transactions.emplace(id, accTx);
            transactionMap_.emplace(id, accTx);
            
            // Update account transaction mappings
            for (auto const& account : meta.getAffectedAccounts())
            {
                if (accountTxMap_.find(account) == accountTxMap_.end())
                    accountTxMap_[account] = AccountTxData();
                auto& accountData = accountTxMap_[account];
                accountData.transactions.push_back(accTx);
                accountData.ledgerTxMap[ledger->info().seq][acceptedLedgerTx->getTxnSeq()] =
                    accountData.transactions.size() - 1;
            }
        }
        
        ledgers_[ledger->info().seq] = std::move(ledgerData);
        ledgerHashToSeq_[ledger->info().hash] = ledger->info().seq;

        // Only perform cleanup when saving the current ledger
        if (current)
        {
            // Calculate the cutoff sequence for old ledgers
            auto const cutoffSeq = ledger->info().seq > app_.config().LEDGER_HISTORY
                ? ledger->info().seq - app_.config().LEDGER_HISTORY
                : 0;
                
            // Amortized deletion of old ledgers and their associated data
            if (cutoffSeq > 0)
            {
                // We'll delete a batch of old ledgers at a time
                const std::size_t BATCH_SIZE = 128;
                std::size_t deleted = 0;
                
                // Create a temporary list of ledgers to delete
                std::vector<std::uint32_t> ledgersToDelete;
                for (const auto& item : ledgers_) {
                    if (deleted >= BATCH_SIZE)
                        break;
                    if (item.first < cutoffSeq) {
                        ledgersToDelete.push_back(item.first);
                        deleted++;
                    }
                }
                
                // Process the deletion batch
                for (auto seq : ledgersToDelete) {
                    auto& ledgerToDelete = ledgers_[seq];
                    
                    // Delete associated transactions from the transaction map
                    for (const auto& txPair : ledgerToDelete.transactions) {
                        transactionMap_.erase(txPair.first);
                    }
                    
                    // Remove the ledger hash mapping
                    ledgerHashToSeq_.erase(ledgerToDelete.info.hash);
                    
                    // Remove the ledger itself
                    ledgers_.erase(seq);
                }
                
                // Clean up associated account transactions if any ledgers were deleted
                if (deleted > 0)
                {
                    for (auto& [account, data] : accountTxMap_) {
                        // Remove transactions from old ledgers
                        auto it = data.ledgerTxMap.begin();
                        while (it != data.ledgerTxMap.end()) {
                            if (it->first < cutoffSeq) {
                                // Remove the corresponding transactions
                                for (const auto& seqPair : it->second) {
                                    if (seqPair.second < data.transactions.size()) {
                                        data.transactions[seqPair.second] = std::make_pair(
                                            nullptr, nullptr);  // Clear the shared_ptr
                                    }
                                }
                                it = data.ledgerTxMap.erase(it);
                            } else {
                                ++it;
                            }
                        }
                        
                        // Clean up null transactions
                        data.transactions.erase(
                            std::remove_if(
                                data.transactions.begin(),
                                data.transactions.end(),
                                [](const auto& tx) { return !tx.first && !tx.second; }
                            ),
                            data.transactions.end()
                        );
                        
                        // Update indices in ledgerTxMap after cleanup
                        for (auto& [ledgerSeq, txMap] : data.ledgerTxMap) {
                            for (auto& [txSeq, index] : txMap) {
                                // Recalculate indices based on new positions
                                auto newIndex = std::distance(
                                    data.transactions.begin(),
                                    std::find(
                                        data.transactions.begin(),
                                        data.transactions.end(),
                                        data.transactions[index]
                                    )
                                );
                                index = newIndex;
                            }
                        }
                    }
                    
                    // Update ledger master
                    app_.getLedgerMaster().clearPriorLedgers(cutoffSeq);
                }
            }
        }
        
        return true;
    }


    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    oldestAccountTxPageB(AccountTxPageOptions const& options) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {{}, std::nullopt};

        MetaTxsList result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::optional<AccountTxMarker> marker;
        bool lookingForMarker = options.marker.has_value();
        std::size_t count = 0;

        for (; txIt != txEnd && count < options.limit; ++txIt) {
            for (const auto& [txSeq, txIndex] : txIt->second) {
                if (lookingForMarker) {
                    if (txIt->first == options.marker->ledgerSeq && txSeq == options.marker->txnSeq)
                        lookingForMarker = false;
                    else
                        continue;
                }

                const auto& [txn, txMeta] = accountData.transactions[txIndex];
                result.emplace_back(
                    txn->getSTransaction()->getSerializer().peekData(),
                    txMeta->getAsObject().getSerializer().peekData(),
                    txIt->first
                );
                ++count;

                if (count >= options.limit) {
                    marker = AccountTxMarker{txIt->first, txSeq};
                    break;
                }
            }
        }

        return {result, marker};
    }

    std::optional<LedgerInfo> getLedgerInfoByIndex(LedgerIndex ledgerSeq) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.find(ledgerSeq);
        if (it != ledgers_.end())
            return it->second.info;
        return std::nullopt;
    }

    std::optional<LedgerInfo> getNewestLedgerInfo() override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ledgers_.empty())
            return std::nullopt;
        return ledgers_.rbegin()->second.info;
    }

    std::optional<LedgerInfo> getLimitedOldestLedgerInfo(LedgerIndex ledgerFirstIndex) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.lower_bound(ledgerFirstIndex);
        if (it != ledgers_.end())
            return it->second.info;
        return std::nullopt;
    }

    std::optional<LedgerInfo> getLimitedNewestLedgerInfo(LedgerIndex ledgerFirstIndex) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.lower_bound(ledgerFirstIndex);
        if (it == ledgers_.end())
            return std::nullopt;
        return ledgers_.rbegin()->second.info;
    }

    std::optional<LedgerInfo> getLedgerInfoByHash(uint256 const& ledgerHash) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgerHashToSeq_.find(ledgerHash);
        if (it != ledgerHashToSeq_.end())
            return ledgers_.at(it->second).info;
        return std::nullopt;
    }

    uint256 getHashByIndex(LedgerIndex ledgerIndex) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.find(ledgerIndex);
        if (it != ledgers_.end())
            return it->second.info.hash;
        return uint256();
    }

    std::optional<LedgerHashPair> getHashesByIndex(LedgerIndex ledgerIndex) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.find(ledgerIndex);
        if (it != ledgers_.end()) {
            return LedgerHashPair{
                it->second.info.hash,
                it->second.info.parentHash
            };
        }
        return std::nullopt;
    }

    std::map<LedgerIndex, LedgerHashPair> getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::map<LedgerIndex, LedgerHashPair> result;
        auto it = ledgers_.lower_bound(minSeq);
        auto end = ledgers_.upper_bound(maxSeq);
        for (; it != end; ++it) {
            result[it->first] = LedgerHashPair{
                it->second.info.hash,
                it->second.info.parentHash
            };
        }
        return result;
    }

    std::vector<std::shared_ptr<Transaction>> getTxHistory(LedgerIndex startIndex) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<std::shared_ptr<Transaction>> result;
        auto it = ledgers_.lower_bound(startIndex);
        int count = 0;
        while (it != ledgers_.end() && count < 20) {
            for (const auto& [txHash, accountTx] : it->second.transactions) {
                result.push_back(accountTx.first);
                if (++count >= 20)
                    break;
            }
            ++it;
        }
        return result;
    }

    AccountTxs getOldestAccountTxs(AccountTxOptions const& options) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        AccountTxs result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::size_t skipped = 0;
        for (; txIt != txEnd && result.size() < options.limit; ++txIt) {
            for (const auto& [txSeq, txIndex] : txIt->second) {
                if (skipped < options.offset) {
                    ++skipped;
                    continue;
                }
                result.push_back(accountData.transactions[txIndex]);
                if (result.size() >= options.limit && !options.bUnlimited)
                    break;
            }
        }

        return result;
    }

    AccountTxs getNewestAccountTxs(AccountTxOptions const& options) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        AccountTxs result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);
std::vector<AccountTx> tempResult;
        std::size_t skipped = 0;
        for (auto rIt = std::make_reverse_iterator(txEnd); rIt != std::make_reverse_iterator(txIt); ++rIt) {
            for (auto innerRIt = rIt->second.rbegin(); innerRIt != rIt->second.rend(); ++innerRIt) {
                if (skipped < options.offset) {
                    ++skipped;
                    continue;
                }
                tempResult.push_back(accountData.transactions[innerRIt->second]);
                if (tempResult.size() >= options.limit && !options.bUnlimited)
                    break;
            }
            if (tempResult.size() >= options.limit && !options.bUnlimited)
                break;
        }

        result.insert(result.end(), tempResult.rbegin(), tempResult.rend());
        return result;
    }

    MetaTxsList getOldestAccountTxsB(AccountTxOptions const& options) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        MetaTxsList result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::size_t skipped = 0;
        for (; txIt != txEnd && result.size() < options.limit; ++txIt) {
            for (const auto& [txSeq, txIndex] : txIt->second) {
                if (skipped < options.offset) {
                    ++skipped;
                    continue;
                }
                const auto& [txn, txMeta] = accountData.transactions[txIndex];
                result.emplace_back(
                    txn->getSTransaction()->getSerializer().peekData(),
                    txMeta->getAsObject().getSerializer().peekData(),
                    txIt->first
                );
                if (result.size() >= options.limit && !options.bUnlimited)
                    break;
            }
        }

        return result;
    }

    MetaTxsList getNewestAccountTxsB(AccountTxOptions const& options) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        MetaTxsList result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::vector<txnMetaLedgerType> tempResult;
        std::size_t skipped = 0;
        for (auto rIt = std::make_reverse_iterator(txEnd); rIt != std::make_reverse_iterator(txIt); ++rIt) {
            for (auto innerRIt = rIt->second.rbegin(); innerRIt != rIt->second.rend(); ++innerRIt) {
                if (skipped < options.offset) {
                    ++skipped;
                    continue;
                }
                const auto& [txn, txMeta] = accountData.transactions[innerRIt->second];
                tempResult.emplace_back(
                    txn->getSTransaction()->getSerializer().peekData(),
                    txMeta->getAsObject().getSerializer().peekData(),
                    txIt->first                        
                );
                if (tempResult.size() >= options.limit && !options.bUnlimited)
                    break;
            }
            if (tempResult.size() >= options.limit && !options.bUnlimited)
                break;
        }

        result.insert(result.end(), tempResult.rbegin(), tempResult.rend());
        return result;
    }

    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    oldestAccountTxPage(AccountTxPageOptions const& options) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {{}, std::nullopt};

        AccountTxs result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::optional<AccountTxMarker> marker;
        bool lookingForMarker = options.marker.has_value();
        std::size_t count = 0;

        for (; txIt != txEnd && count < options.limit; ++txIt) {
            for (const auto& [txSeq, txIndex] : txIt->second) {
                if (lookingForMarker) {
                    if (txIt->first == options.marker->ledgerSeq && txSeq == options.marker->txnSeq)
                        lookingForMarker = false;
                    else
                        continue;
                }

                result.push_back(accountData.transactions[txIndex]);
                ++count;

                if (count >= options.limit) {
                    marker = AccountTxMarker{txIt->first, txSeq};
                    break;
                }
            }
        }

        return {result, marker};
    }

    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    newestAccountTxPage(AccountTxPageOptions const& options) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {{}, std::nullopt};

        AccountTxs result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::optional<AccountTxMarker> marker;
        bool lookingForMarker = options.marker.has_value();
        std::size_t count = 0;

        for (auto rIt = std::make_reverse_iterator(txEnd); rIt != std::make_reverse_iterator(txIt) && count < options.limit; ++rIt) {
            for (auto innerRIt = rIt->second.rbegin(); innerRIt != rIt->second.rend(); ++innerRIt) {
                if (lookingForMarker) {
                    if (rIt->first == options.marker->ledgerSeq && innerRIt->first == options.marker->txnSeq)
                        lookingForMarker = false;
                    else
                        continue;
                }

                result.push_back(accountData.transactions[innerRIt->second]);
                ++count;

                if (count >= options.limit) {
                    marker = AccountTxMarker{rIt->first, innerRIt->first};
                    break;
                }
            }
        }

        return {result, marker};
    }


    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    newestAccountTxPageB(AccountTxPageOptions const& options) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {{}, std::nullopt};

        MetaTxsList result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::optional<AccountTxMarker> marker;
        bool lookingForMarker = options.marker.has_value();
        std::size_t count = 0;

        for (auto rIt = std::make_reverse_iterator(txEnd); rIt != std::make_reverse_iterator(txIt) && count < options.limit; ++rIt) {
            for (auto innerRIt = rIt->second.rbegin(); innerRIt != rIt->second.rend(); ++innerRIt) {
                if (lookingForMarker) {
                    if (rIt->first == options.marker->ledgerSeq && innerRIt->first == options.marker->txnSeq)
                        lookingForMarker = false;
                    else
                        continue;
                }

                const auto& [txn, txMeta] = accountData.transactions[innerRIt->second];
                result.emplace_back(
                    txn->getSTransaction()->getSerializer().peekData(),
                    txMeta->getAsObject().getSerializer().peekData(),
                    txIt->first                        
                );
                ++count;

                if (count >= options.limit) {
                    marker = AccountTxMarker{rIt->first, innerRIt->first};
                    break;
                }
            }
        }

        return {result, marker};
    }

    std::variant<AccountTx, TxSearched>
    getTransaction(
        uint256 const& id,
        std::optional<ClosedInterval<std::uint32_t>> const& range,
        error_code_i& ec) override {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = transactionMap_.find(id);
        if (it != transactionMap_.end()) {
            const auto& [txn, txMeta] = it->second;
            if (!range || (range->lower() <= txMeta->getLgrSeq() && txMeta->getLgrSeq() <= range->upper()))
                return it->second;
        }

        if (range) {
            bool allPresent = true;
            for (LedgerIndex seq = range->lower(); seq <= range->upper(); ++seq) {
                if (ledgers_.find(seq) == ledgers_.end()) {
                    allPresent = false;
                    break;
                }
            }
            return allPresent ? TxSearched::all : TxSearched::some;
        }

        return TxSearched::unknown;
    }
    
    bool ledgerDbHasSpace(Config const& config) override {
        // In-memory database always has space
        return true;
    }

    bool transactionDbHasSpace(Config const& config) override {
        // In-memory database always has space
        return true;
    }

    std::uint32_t getKBUsedAll() override {
        // Estimate memory usage (this is a rough estimate)
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::uint32_t size = 0;
        size += sizeof(*this);
        size += ledgers_.size() * (sizeof(LedgerIndex) + sizeof(LedgerData));
        size += ledgerHashToSeq_.size() * (sizeof(uint256) + sizeof(LedgerIndex));
        size += transactionMap_.size() * (sizeof(uint256) + sizeof(AccountTx));
        for (const auto& [_, accountData] : accountTxMap_) {
            size += sizeof(AccountID) + sizeof(AccountTxData);
            size += accountData.transactions.size() * sizeof(AccountTx);
            for (const auto& [_, innerMap] : accountData.ledgerTxMap) {
                size += sizeof(uint32_t) + innerMap.size() * (sizeof(uint32_t) + sizeof(size_t));
            }
        }
        return size / 1024; // Convert to KB
    }

    std::uint32_t getKBUsedLedger() override {
        // Estimate memory usage for ledger data
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::uint32_t size = 0;
        size += ledgers_.size() * (sizeof(LedgerIndex) + sizeof(LedgerData));
        size += ledgerHashToSeq_.size() * (sizeof(uint256) + sizeof(LedgerIndex));
        return size / 1024; // Convert to KB
    }

    std::uint32_t getKBUsedTransaction() override {
        // Estimate memory usage for transaction data
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::uint32_t size = 0;
        size += transactionMap_.size() * (sizeof(uint256) + sizeof(AccountTx));
        for (const auto& [_, accountData] : accountTxMap_) {
            size += sizeof(AccountID) + sizeof(AccountTxData);
            size += accountData.transactions.size() * sizeof(AccountTx);
            for (const auto& [_, innerMap] : accountData.ledgerTxMap) {
                size += sizeof(uint32_t) + innerMap.size() * (sizeof(uint32_t) + sizeof(size_t));
            }
        }
        return size / 1024; // Convert to KB
    }

    void closeLedgerDB() override {
        // No-op for in-memory database
    }

    void closeTransactionDB() override {
        // No-op for in-memory database
    }
};

// Factory function
std::unique_ptr<SQLiteDatabase>
getMemoryDatabase(Application& app, Config const& config, JobQueue& jobQueue)
{
    return std::make_unique<MemoryDatabase>(app, config, jobQueue);
}

} // namespace ripple

#else // RDB_CONCURRENT=1

#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/rdb/RelationalDatabase.h>
#include <ripple/app/rdb/backend/SQLiteDatabase.h>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <algorithm>
#include <optional>

namespace ripple {

struct base_uint_hasher
{
    using result_type = std::size_t;

    result_type
    operator()(base_uint<256> const& value) const
    {
        return hardened_hash<>{}(value);
    }

    result_type
    operator()(AccountID const& value) const
    {
        return hardened_hash<>{}(value);
    }
};

class MemoryDatabase : public SQLiteDatabase
{
private:
    struct LedgerData
    {
        LedgerInfo info;
        boost::unordered::
            concurrent_flat_map<uint256, AccountTx, base_uint_hasher>
                transactions;
    };

    struct AccountTxData
    {
        boost::unordered::
            concurrent_flat_map<std::pair<uint32_t, uint32_t>, AccountTx>
                transactions;
    };

    Application& app_;

    boost::unordered::concurrent_flat_map<LedgerIndex, LedgerData> ledgers_;
    boost::unordered::
        concurrent_flat_map<uint256, LedgerIndex, base_uint_hasher>
            ledgerHashToSeq_;
    boost::unordered::concurrent_flat_map<uint256, AccountTx, base_uint_hasher>
        transactionMap_;
    boost::unordered::
        concurrent_flat_map<AccountID, AccountTxData, base_uint_hasher>
            accountTxMap_;

public:
    MemoryDatabase(Application& app, Config const&, JobQueue&) : app_(app)
    {
    }

    std::optional<LedgerIndex>
    getMinLedgerSeq() override
    {
        std::optional<LedgerIndex> minSeq;
        ledgers_.visit_all([&minSeq](auto const& pair) {
            if (!minSeq || pair.first < *minSeq)
            {
                minSeq = pair.first;
            }
        });
        return minSeq;
    }

    std::optional<LedgerIndex>
    getTransactionsMinLedgerSeq() override
    {
        std::optional<LedgerIndex> minSeq;
        transactionMap_.visit_all([&minSeq](auto const& pair) {
            LedgerIndex seq = pair.second.second->getLgrSeq();
            if (!minSeq || seq < *minSeq)
            {
                minSeq = seq;
            }
        });
        return minSeq;
    }

    std::optional<LedgerIndex>
    getAccountTransactionsMinLedgerSeq() override
    {
        std::optional<LedgerIndex> minSeq;
        accountTxMap_.visit_all([&minSeq](auto const& pair) {
            pair.second.transactions.visit_all([&minSeq](auto const& tx) {
                if (!minSeq || tx.first.first < *minSeq)
                {
                    minSeq = tx.first.first;
                }
            });
        });
        return minSeq;
    }

    std::optional<LedgerIndex>
    getMaxLedgerSeq() override
    {
        std::optional<LedgerIndex> maxSeq;
        ledgers_.visit_all([&maxSeq](auto const& pair) {
            if (!maxSeq || pair.first > *maxSeq)
            {
                maxSeq = pair.first;
            }
        });
        return maxSeq;
    }

    void
    deleteTransactionByLedgerSeq(LedgerIndex ledgerSeq) override
    {
        ledgers_.visit(ledgerSeq, [this](auto& item) {
            item.second.transactions.visit_all([this](auto const& txPair) {
                transactionMap_.erase(txPair.first);
            });
            item.second.transactions.clear();
        });

        accountTxMap_.visit_all([ledgerSeq](auto& item) {
            item.second.transactions.erase_if([ledgerSeq](auto const& tx) {
                return tx.first.first == ledgerSeq;
            });
        });
    }

    void
    deleteBeforeLedgerSeq(LedgerIndex ledgerSeq) override
    {
        ledgers_.erase_if([this, ledgerSeq](auto const& item) {
            if (item.first < ledgerSeq)
            {
                item.second.transactions.visit_all([this](auto const& txPair) {
                    transactionMap_.erase(txPair.first);
                });
                ledgerHashToSeq_.erase(item.second.info.hash);
                return true;
            }
            return false;
        });

        accountTxMap_.visit_all([ledgerSeq](auto& item) {
            item.second.transactions.erase_if([ledgerSeq](auto const& tx) {
                return tx.first.first < ledgerSeq;
            });
        });
    }

    void
    deleteTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override
    {
        ledgers_.visit_all([this, ledgerSeq](auto& item) {
            if (item.first < ledgerSeq)
            {
                item.second.transactions.visit_all([this](auto const& txPair) {
                    transactionMap_.erase(txPair.first);
                });
                item.second.transactions.clear();
            }
        });

        accountTxMap_.visit_all([ledgerSeq](auto& item) {
            item.second.transactions.erase_if([ledgerSeq](auto const& tx) {
                return tx.first.first < ledgerSeq;
            });
        });
    }

    void
    deleteAccountTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override
    {
        accountTxMap_.visit_all([ledgerSeq](auto& item) {
            item.second.transactions.erase_if([ledgerSeq](auto const& tx) {
                return tx.first.first < ledgerSeq;
            });
        });
    }

    std::size_t
    getTransactionCount() override
    {
        return transactionMap_.size();
    }

    std::size_t
    getAccountTransactionCount() override
    {
        std::size_t count = 0;
        accountTxMap_.visit_all([&count](auto const& item) {
            count += item.second.transactions.size();
        });
        return count;
    }

    CountMinMax
    getLedgerCountMinMax() override
    {
        CountMinMax result{0, 0, 0};
        ledgers_.visit_all([&result](auto const& item) {
            result.numberOfRows++;
            if (result.minLedgerSequence == 0 ||
                item.first < result.minLedgerSequence)
            {
                result.minLedgerSequence = item.first;
            }
            if (item.first > result.maxLedgerSequence)
            {
                result.maxLedgerSequence = item.first;
            }
        });
        return result;
    }

    bool
    saveValidatedLedger(
        std::shared_ptr<Ledger const> const& ledger,
        bool current) override
    {
        LedgerData ledgerData;
        ledgerData.info = ledger->info();

        auto aLedger = std::make_shared<AcceptedLedger>(ledger, app_);
        for (auto const& acceptedLedgerTx : *aLedger)
        {
            auto const& txn = acceptedLedgerTx->getTxn();
            auto const& meta = acceptedLedgerTx->getMeta();
            auto const& id = txn->getTransactionID();

            std::string reason;
            auto accTx = std::make_pair(
                std::make_shared<ripple::Transaction>(txn, reason, app_),
                std::make_shared<ripple::TxMeta>(meta));

            ledgerData.transactions.emplace(id, accTx);
            transactionMap_.emplace(id, accTx);

            for (auto const& account : meta.getAffectedAccounts())
            {
                accountTxMap_.visit(account, [&](auto& data) {
                    data.second.transactions.emplace(
                        std::make_pair(
                            ledger->info().seq, acceptedLedgerTx->getTxnSeq()),
                        accTx);
                });
            }
        }

        ledgers_.emplace(ledger->info().seq, std::move(ledgerData));
        ledgerHashToSeq_.emplace(ledger->info().hash, ledger->info().seq);

        // Only perform cleanup when saving the current ledger
        if (current)
        {
            // Calculate the cutoff sequence for old ledgers
            auto const cutoffSeq = ledger->info().seq > app_.config().LEDGER_HISTORY
                ? ledger->info().seq - app_.config().LEDGER_HISTORY
                : 0;

            // Amortized deletion of old ledgers and their associated data
            if (cutoffSeq > 0)
            {
                // We'll delete a batch of old ledgers at a time
                const std::size_t BATCH_SIZE = 128;
                std::size_t deleted = 0;

                ledgers_.erase_if([&](auto const& item) {
                    if (deleted >= BATCH_SIZE)
                        return false;

                    if (item.first < cutoffSeq)
                    {
                        // Delete associated transactions from the transaction map
                        item.second.transactions.visit_all(
                            [this](auto const& txPair) {
                                transactionMap_.erase(txPair.first);
                            });

                        // Remove the ledger hash mapping
                        ledgerHashToSeq_.erase(item.second.info.hash);

                        deleted++;
                        return true;
                    }
                    return false;
                });

                // Clean up associated account transactions
                if (deleted > 0)
                {
                    accountTxMap_.visit_all([cutoffSeq](auto& item) {
                        item.second.transactions.erase_if(
                            [cutoffSeq](auto const& tx) {
                                return tx.first.first < cutoffSeq;
                            });
                    });
                }

                // update ledger master
                app_.getLedgerMaster().clearPriorLedgers(cutoffSeq);
            }
        }

        return true;
    }


    std::optional<LedgerInfo>
    getLedgerInfoByIndex(LedgerIndex ledgerSeq) override
    {
        std::optional<LedgerInfo> result;
        ledgers_.visit(ledgerSeq, [&result](auto const& item) {
            result = item.second.info;
        });
        return result;
    }

    std::optional<LedgerInfo>
    getNewestLedgerInfo() override
    {
        std::optional<LedgerInfo> result;
        ledgers_.visit_all([&result](auto const& item) {
            if (!result || item.second.info.seq > result->seq)
            {
                result = item.second.info;
            }
        });
        return result;
    }

    std::optional<LedgerInfo>
    getLimitedOldestLedgerInfo(LedgerIndex ledgerFirstIndex) override
    {
        std::optional<LedgerInfo> result;
        ledgers_.visit_all([&](auto const& item) {
            if (item.first >= ledgerFirstIndex &&
                (!result || item.first < result->seq))
            {
                result = item.second.info;
            }
        });
        return result;
    }

    std::optional<LedgerInfo>
    getLimitedNewestLedgerInfo(LedgerIndex ledgerFirstIndex) override
    {
        std::optional<LedgerInfo> result;
        ledgers_.visit_all([&](auto const& item) {
            if (item.first >= ledgerFirstIndex &&
                (!result || item.first > result->seq))
            {
                result = item.second.info;
            }
        });
        return result;
    }

    std::optional<LedgerInfo>
    getLedgerInfoByHash(uint256 const& ledgerHash) override
    {
        std::optional<LedgerInfo> result;
        ledgerHashToSeq_.visit(ledgerHash, [this, &result](auto const& item) {
            ledgers_.visit(item.second, [&result](auto const& item) {
                result = item.second.info;
            });
        });
        return result;
    }

    uint256
    getHashByIndex(LedgerIndex ledgerIndex) override
    {
        uint256 result;
        ledgers_.visit(ledgerIndex, [&result](auto const& item) {
            result = item.second.info.hash;
        });
        return result;
    }

    std::optional<LedgerHashPair>
    getHashesByIndex(LedgerIndex ledgerIndex) override
    {
        std::optional<LedgerHashPair> result;
        ledgers_.visit(ledgerIndex, [&result](auto const& item) {
            result = LedgerHashPair{
                item.second.info.hash, item.second.info.parentHash};
        });
        return result;
    }

    std::map<LedgerIndex, LedgerHashPair>
    getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq) override
    {
        std::map<LedgerIndex, LedgerHashPair> result;
        ledgers_.visit_all([&](auto const& item) {
            if (item.first >= minSeq && item.first <= maxSeq)
            {
                result[item.first] = LedgerHashPair{
                    item.second.info.hash, item.second.info.parentHash};
            }
        });
        return result;
    }

    std::vector<std::shared_ptr<Transaction>>
    getTxHistory(LedgerIndex startIndex) override
    {
        std::vector<std::shared_ptr<Transaction>> result;
        transactionMap_.visit_all([&](auto const& item) {
            if (item.second.second->getLgrSeq() >= startIndex)
            {
                result.push_back(item.second.first);
            }
        });
        std::sort(
            result.begin(), result.end(), [](auto const& a, auto const& b) {
                return a->getLedger() > b->getLedger();
            });
        if (result.size() > 20)
        {
            result.resize(20);
        }
        return result;
    }

    AccountTxs
    getOldestAccountTxs(AccountTxOptions const& options) override
    {
        AccountTxs result;
        accountTxMap_.visit(options.account, [&](auto const& item) {
            item.second.transactions.visit_all([&](auto const& tx) {
                if (tx.first.first >= options.minLedger &&
                    tx.first.first <= options.maxLedger)
                {
                    result.push_back(tx.second);
                }
            });
        });
        std::sort(
            result.begin(), result.end(), [](auto const& a, auto const& b) {
                return a.second->getLgrSeq() < b.second->getLgrSeq();
            });
        if (!options.bUnlimited && result.size() > options.limit)
        {
            result.resize(options.limit);
        }
        return result;
    }

    AccountTxs
    getNewestAccountTxs(AccountTxOptions const& options) override
    {
        AccountTxs result;
        accountTxMap_.visit(options.account, [&](auto const& item) {
            item.second.transactions.visit_all([&](auto const& tx) {
                if (tx.first.first >= options.minLedger &&
                    tx.first.first <= options.maxLedger)
                {
                    result.push_back(tx.second);
                }
            });
        });
        std::sort(
            result.begin(), result.end(), [](auto const& a, auto const& b) {
                return a.second->getLgrSeq() > b.second->getLgrSeq();
            });
        if (!options.bUnlimited && result.size() > options.limit)
        {
            result.resize(options.limit);
        }
        return result;
    }

    MetaTxsList
    getOldestAccountTxsB(AccountTxOptions const& options) override
    {
        MetaTxsList result;
        accountTxMap_.visit(options.account, [&](auto const& item) {
            item.second.transactions.visit_all([&](auto const& tx) {
                if (tx.first.first >= options.minLedger &&
                    tx.first.first <= options.maxLedger)
                {
                    result.emplace_back(
                        tx.second.first->getSTransaction()
                            ->getSerializer()
                            .peekData(),
                        tx.second.second->getAsObject()
                            .getSerializer()
                            .peekData(),
                        tx.first.first);
                }
            });
        });
        std::sort(
            result.begin(), result.end(), [](auto const& a, auto const& b) {
                return std::get<2>(a) < std::get<2>(b);
            });
        if (!options.bUnlimited && result.size() > options.limit)
        {
            result.resize(options.limit);
        }
        return result;
    }

    MetaTxsList
    getNewestAccountTxsB(AccountTxOptions const& options) override
    {
        MetaTxsList result;
        accountTxMap_.visit(options.account, [&](auto const& item) {
            item.second.transactions.visit_all([&](auto const& tx) {
                if (tx.first.first >= options.minLedger &&
                    tx.first.first <= options.maxLedger)
                {
                    result.emplace_back(
                        tx.second.first->getSTransaction()
                            ->getSerializer()
                            .peekData(),
                        tx.second.second->getAsObject()
                            .getSerializer()
                            .peekData(),
                        tx.first.first);
                }
            });
        });
        std::sort(
            result.begin(), result.end(), [](auto const& a, auto const& b) {
                return std::get<2>(a) > std::get<2>(b);
            });
        if (!options.bUnlimited && result.size() > options.limit)
        {
            result.resize(options.limit);
        }
        return result;
    }

    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    oldestAccountTxPage(AccountTxPageOptions const& options) override
    {
        AccountTxs result;
        std::optional<AccountTxMarker> marker;
        accountTxMap_.visit(options.account, [&](auto const& item) {
            std::vector<std::pair<std::pair<uint32_t, uint32_t>, AccountTx>>
                txs;
            item.second.transactions.visit_all([&](auto const& tx) {
                if (tx.first.first >= options.minLedger &&
                    tx.first.first <= options.maxLedger)
                {
                    txs.emplace_back(tx);
                }
            });
            std::sort(txs.begin(), txs.end(), [](auto const& a, auto const& b) {
                return a.first < b.first;
            });

            auto it = txs.begin();
            if (options.marker)
            {
                it = std::find_if(txs.begin(), txs.end(), [&](auto const& tx) {
                    return tx.first.first == options.marker->ledgerSeq &&
                        tx.first.second == options.marker->txnSeq;
                });
                if (it != txs.end())
                    ++it;
            }

            for (; it != txs.end() && result.size() < options.limit; ++it)
            {
                result.push_back(it->second);
            }

            if (it != txs.end())
            {
                marker = AccountTxMarker{it->first.first, it->first.second};
            }
        });
        return {result, marker};
    }

    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    newestAccountTxPage(AccountTxPageOptions const& options) override
    {
        AccountTxs result;
        std::optional<AccountTxMarker> marker;
        accountTxMap_.visit(options.account, [&](auto const& item) {
            std::vector<std::pair<std::pair<uint32_t, uint32_t>, AccountTx>>
                txs;
            item.second.transactions.visit_all([&](auto const& tx) {
                if (tx.first.first >= options.minLedger &&
                    tx.first.first <= options.maxLedger)
                {
                    txs.emplace_back(tx);
                }
            });
            std::sort(txs.begin(), txs.end(), [](auto const& a, auto const& b) {
                return a.first > b.first;
            });

            auto it = txs.begin();
            if (options.marker)
            {
                it = std::find_if(txs.begin(), txs.end(), [&](auto const& tx) {
                    return tx.first.first == options.marker->ledgerSeq &&
                        tx.first.second == options.marker->txnSeq;
                });
                if (it != txs.end())
                    ++it;
            }

            for (; it != txs.end() && result.size() < options.limit; ++it)
            {
                result.push_back(it->second);
            }

            if (it != txs.end())
            {
                marker = AccountTxMarker{it->first.first, it->first.second};
            }
        });
        return {result, marker};
    }

    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    oldestAccountTxPageB(AccountTxPageOptions const& options) override
    {
        MetaTxsList result;
        std::optional<AccountTxMarker> marker;
        accountTxMap_.visit(options.account, [&](auto const& item) {
            std::vector<std::pair<std::pair<uint32_t, uint32_t>, AccountTx>>
                txs;
            item.second.transactions.visit_all([&](auto const& tx) {
                if (tx.first.first >= options.minLedger &&
                    tx.first.first <= options.maxLedger)
                {
                    txs.emplace_back(tx);
                }
            });
            std::sort(txs.begin(), txs.end(), [](auto const& a, auto const& b) {
                return a.first < b.first;
            });

            auto it = txs.begin();
            if (options.marker)
            {
                it = std::find_if(txs.begin(), txs.end(), [&](auto const& tx) {
                    return tx.first.first == options.marker->ledgerSeq &&
                        tx.first.second == options.marker->txnSeq;
                });
                if (it != txs.end())
                    ++it;
            }

            for (; it != txs.end() && result.size() < options.limit; ++it)
            {
                result.emplace_back(
                    it->second.first->getSTransaction()
                        ->getSerializer()
                        .peekData(),
                    it->second.second->getAsObject().getSerializer().peekData(),
                    it->first.first);
            }

            if (it != txs.end())
            {
                marker = AccountTxMarker{it->first.first, it->first.second};
            }
        });
        return {result, marker};
    }

    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    newestAccountTxPageB(AccountTxPageOptions const& options) override
    {
        MetaTxsList result;
        std::optional<AccountTxMarker> marker;
        accountTxMap_.visit(options.account, [&](auto const& item) {
            std::vector<std::pair<std::pair<uint32_t, uint32_t>, AccountTx>>
                txs;
            item.second.transactions.visit_all([&](auto const& tx) {
                if (tx.first.first >= options.minLedger &&
                    tx.first.first <= options.maxLedger)
                {
                    txs.emplace_back(tx);
                }
            });
            std::sort(txs.begin(), txs.end(), [](auto const& a, auto const& b) {
                return a.first > b.first;
            });

            auto it = txs.begin();
            if (options.marker)
            {
                it = std::find_if(txs.begin(), txs.end(), [&](auto const& tx) {
                    return tx.first.first == options.marker->ledgerSeq &&
                        tx.first.second == options.marker->txnSeq;
                });
                if (it != txs.end())
                    ++it;
            }

            for (; it != txs.end() && result.size() < options.limit; ++it)
            {
                result.emplace_back(
                    it->second.first->getSTransaction()
                        ->getSerializer()
                        .peekData(),
                    it->second.second->getAsObject().getSerializer().peekData(),
                    it->first.first);
            }

            if (it != txs.end())
            {
                marker = AccountTxMarker{it->first.first, it->first.second};
            }
        });
        return {result, marker};
    }

    std::variant<AccountTx, TxSearched>
    getTransaction(
        uint256 const& id,
        std::optional<ClosedInterval<std::uint32_t>> const& range,
        error_code_i& ec) override
    {
        std::variant<AccountTx, TxSearched> result = TxSearched::unknown;
        transactionMap_.visit(id, [&](auto const& item) {
            auto const& tx = item.second;
            if (!range ||
                (range->lower() <= tx.second->getLgrSeq() &&
                 tx.second->getLgrSeq() <= range->upper()))
            {
                result = tx;
            }
            else
            {
                result = TxSearched::all;
            }
        });
        return result;
    }

    bool
    ledgerDbHasSpace(Config const& config) override
    {
        // In-memory database always has space
        return true;
    }

    bool
    transactionDbHasSpace(Config const& config) override
    {
        // In-memory database always has space
        return true;
    }

    std::uint32_t
    getKBUsedAll() override
    {
        // Estimate memory usage (this is a rough estimate)
        std::uint32_t size = sizeof(*this);
        size += ledgers_.size() * (sizeof(LedgerIndex) + sizeof(LedgerData));
        size +=
            ledgerHashToSeq_.size() * (sizeof(uint256) + sizeof(LedgerIndex));
        size += transactionMap_.size() * (sizeof(uint256) + sizeof(AccountTx));
        accountTxMap_.visit_all([&size](auto const& item) {
            size += sizeof(AccountID) + sizeof(AccountTxData);
            size += item.second.transactions.size() *
                (sizeof(std::pair<uint32_t, uint32_t>) + sizeof(AccountTx));
        });
        return size / 1024;  // Convert to KB
    }

    std::uint32_t
    getKBUsedLedger() override
    {
        std::uint32_t size =
            ledgers_.size() * (sizeof(LedgerIndex) + sizeof(LedgerData));
        size +=
            ledgerHashToSeq_.size() * (sizeof(uint256) + sizeof(LedgerIndex));
        return size / 1024;  // Convert to KB
    }

    std::uint32_t
    getKBUsedTransaction() override
    {
        std::uint32_t size =
            transactionMap_.size() * (sizeof(uint256) + sizeof(AccountTx));
        accountTxMap_.visit_all([&size](auto const& item) {
            size += sizeof(AccountID) + sizeof(AccountTxData);
            size += item.second.transactions.size() *
                (sizeof(std::pair<uint32_t, uint32_t>) + sizeof(AccountTx));
        });
        return size / 1024;  // Convert to KB
    }

    void
    closeLedgerDB() override
    {
        // No-op for in-memory database
    }

    void
    closeTransactionDB() override
    {
        // No-op for in-memory database
    }
};

// Factory function
std::unique_ptr<SQLiteDatabase>
getMemoryDatabase(Application& app, Config const& config, JobQueue& jobQueue)
{
    return std::make_unique<MemoryDatabase>(app, config, jobQueue);
}
}  // namespace ripple
#endif // RDB_CONCURRENT
#endif // INCLUDE GUARD
