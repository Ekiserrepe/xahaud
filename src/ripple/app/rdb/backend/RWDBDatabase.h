#ifndef RIPPLE_APP_RDB_BACKEND_MEMORYDATABASE_H_INCLUDED
#define RIPPLE_APP_RDB_BACKEND_MEMORYDATABASE_H_INCLUDED

#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/ledger/PendingSaves.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/misc/impl/AccountTxPaging.h>
#include <ripple/app/rdb/backend/SQLiteDatabase.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <vector>

namespace ripple {

class RWDBDatabase : public SQLiteDatabase
{
private:
    struct LedgerData
    {
        LedgerInfo info;
        std::map<uint256, AccountTx> transactions;
    };

    struct AccountTxData
    {
        AccountTxs transactions;
        std::map<uint32_t, std::map<uint32_t, size_t>>
            ledgerTxMap;  // ledgerSeq -> txSeq -> index in transactions
    };

    Application& app_;

    mutable std::shared_mutex mutex_;

    std::map<LedgerIndex, LedgerData> ledgers_;
    std::map<uint256, LedgerIndex> ledgerHashToSeq_;
    std::map<uint256, AccountTx> transactionMap_;
    std::map<AccountID, AccountTxData> accountTxMap_;

public:
    RWDBDatabase(Application& app, Config const& config, JobQueue& jobQueue)
        : app_(app)
    {
    }

    std::optional<LedgerIndex>
    getMinLedgerSeq() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ledgers_.empty())
            return std::nullopt;
        return ledgers_.begin()->first;
    }

    std::optional<LedgerIndex>
    getTransactionsMinLedgerSeq() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (transactionMap_.empty())
            return std::nullopt;
        return transactionMap_.begin()->second.second->getLgrSeq();
    }

    std::optional<LedgerIndex>
    getAccountTransactionsMinLedgerSeq() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (accountTxMap_.empty())
            return std::nullopt;
        LedgerIndex minSeq = std::numeric_limits<LedgerIndex>::max();
        for (const auto& [_, accountData] : accountTxMap_)
        {
            if (!accountData.ledgerTxMap.empty())
                minSeq =
                    std::min(minSeq, accountData.ledgerTxMap.begin()->first);
        }
        return minSeq == std::numeric_limits<LedgerIndex>::max()
            ? std::nullopt
            : std::optional<LedgerIndex>(minSeq);
    }

    std::optional<LedgerIndex>
    getMaxLedgerSeq() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ledgers_.empty())
            return std::nullopt;
        return ledgers_.rbegin()->first;
    }
    void
    deleteTransactionByLedgerSeq(LedgerIndex ledgerSeq) override
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.find(ledgerSeq);
        if (it != ledgers_.end())
        {
            for (const auto& [txHash, _] : it->second.transactions)
            {
                transactionMap_.erase(txHash);
            }
            it->second.transactions.clear();
        }
        for (auto& [_, accountData] : accountTxMap_)
        {
            accountData.ledgerTxMap.erase(ledgerSeq);
            accountData.transactions.erase(
                std::remove_if(
                    accountData.transactions.begin(),
                    accountData.transactions.end(),
                    [ledgerSeq](const AccountTx& tx) {
                        return tx.second->getLgrSeq() == ledgerSeq;
                    }),
                accountData.transactions.end());
        }
        ledgers_.erase(ledgerSeq);
    }

    void
    deleteBeforeLedgerSeq(LedgerIndex ledgerSeq) override
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.begin();
        while (it != ledgers_.end() && it->first < ledgerSeq)
        {
            for (const auto& [txHash, _] : it->second.transactions)
            {
                transactionMap_.erase(txHash);
            }
            ledgerHashToSeq_.erase(it->second.info.hash);
            it = ledgers_.erase(it);
        }
        for (auto& [_, accountData] : accountTxMap_)
        {
            auto txIt = accountData.ledgerTxMap.begin();
            while (txIt != accountData.ledgerTxMap.end() &&
                   txIt->first < ledgerSeq)
            {
                txIt = accountData.ledgerTxMap.erase(txIt);
            }
            accountData.transactions.erase(
                std::remove_if(
                    accountData.transactions.begin(),
                    accountData.transactions.end(),
                    [ledgerSeq](const AccountTx& tx) {
                        return tx.second->getLgrSeq() < ledgerSeq;
                    }),
                accountData.transactions.end());
        }
    }

    void
    deleteTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto& [seq, ledgerData] : ledgers_)
        {
            if (seq < ledgerSeq)
            {
                for (const auto& [txHash, _] : ledgerData.transactions)
                {
                    transactionMap_.erase(txHash);
                }
                ledgerData.transactions.clear();
            }
        }
        for (auto& [_, accountData] : accountTxMap_)
        {
            auto txIt = accountData.ledgerTxMap.begin();
            while (txIt != accountData.ledgerTxMap.end() &&
                   txIt->first < ledgerSeq)
            {
                txIt = accountData.ledgerTxMap.erase(txIt);
            }
            accountData.transactions.erase(
                std::remove_if(
                    accountData.transactions.begin(),
                    accountData.transactions.end(),
                    [ledgerSeq](const AccountTx& tx) {
                        return tx.second->getLgrSeq() < ledgerSeq;
                    }),
                accountData.transactions.end());
        }
    }

    void
    deleteAccountTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto& [_, accountData] : accountTxMap_)
        {
            auto txIt = accountData.ledgerTxMap.begin();
            while (txIt != accountData.ledgerTxMap.end() &&
                   txIt->first < ledgerSeq)
            {
                txIt = accountData.ledgerTxMap.erase(txIt);
            }
            accountData.transactions.erase(
                std::remove_if(
                    accountData.transactions.begin(),
                    accountData.transactions.end(),
                    [ledgerSeq](const AccountTx& tx) {
                        return tx.second->getLgrSeq() < ledgerSeq;
                    }),
                accountData.transactions.end());
        }
    }
    std::size_t
    getTransactionCount() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return transactionMap_.size();
    }

    std::size_t
    getAccountTransactionCount() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::size_t count = 0;
        for (const auto& [_, accountData] : accountTxMap_)
        {
            count += accountData.transactions.size();
        }
        return count;
    }

    CountMinMax
    getLedgerCountMinMax() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ledgers_.empty())
            return {0, 0, 0};
        return {
            ledgers_.size(), ledgers_.begin()->first, ledgers_.rbegin()->first};
    }

    bool
    saveValidatedLedger(
        std::shared_ptr<Ledger const> const& ledger,
        bool current) override
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        LedgerData ledgerData;
        ledgerData.info = ledger->info();
        auto j = app_.journal("Ledger");
        auto seq = ledger->info().seq;
        // auto aLedger = std::make_shared<AcceptedLedger>(ledger, app_);

        JLOG(j.trace()) << "saveValidatedLedger "
                        << (current ? "" : "fromAcquire ") << seq;

        if (!ledger->info().accountHash.isNonZero())
        {
            JLOG(j.fatal()) << "AH is zero: " << getJson({*ledger, {}});
            assert(false);
        }

        if (ledger->info().accountHash !=
            ledger->stateMap().getHash().as_uint256())
        {
            JLOG(j.fatal()) << "sAL: " << ledger->info().accountHash
                            << " != " << ledger->stateMap().getHash();
            JLOG(j.fatal())
                << "saveAcceptedLedger: seq=" << seq << ", current=" << current;
            assert(false);
        }

        assert(ledger->info().txHash == ledger->txMap().getHash().as_uint256());

        // Save the ledger header in the hashed object store
        {
            Serializer s(128);
            s.add32(HashPrefix::ledgerMaster);
            addRaw(ledger->info(), s);
            app_.getNodeStore().store(
                hotLEDGER, std::move(s.modData()), ledger->info().hash, seq);
        }

        std::shared_ptr<AcceptedLedger> aLedger;
        try
        {
            aLedger = app_.getAcceptedLedgerCache().fetch(ledger->info().hash);
            if (!aLedger)
            {
                aLedger = std::make_shared<AcceptedLedger>(ledger, app_);
                app_.getAcceptedLedgerCache().canonicalize_replace_client(
                    ledger->info().hash, aLedger);
            }
        }
        catch (std::exception const&)
        {
            JLOG(j.warn()) << "An accepted ledger was missing nodes";
            app_.getLedgerMaster().failedSave(seq, ledger->info().hash);
            // Clients can now trust the database for information about this
            // ledger sequence.
            app_.pendingSaves().finishWork(seq);
            return false;
        }

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
                if (accountTxMap_.find(account) == accountTxMap_.end())
                    accountTxMap_[account] = AccountTxData();
                auto& accountData = accountTxMap_[account];
                accountData.transactions.push_back(accTx);
                accountData.ledgerTxMap[ledger->info().seq]
                                       [acceptedLedgerTx->getTxnSeq()] =
                    accountData.transactions.size() - 1;
            }
            app_.getMasterTransaction().inLedger(
                id,
                seq,
                acceptedLedgerTx->getTxnSeq(),
                app_.config().NETWORK_ID);
        }

        ledgers_[ledger->info().seq] = std::move(ledgerData);
        ledgerHashToSeq_[ledger->info().hash] = ledger->info().seq;

        if (current)
        {
            auto const cutoffSeq =
                ledger->info().seq > app_.config().LEDGER_HISTORY
                ? ledger->info().seq - app_.config().LEDGER_HISTORY
                : 0;

            if (cutoffSeq > 0)
            {
                const std::size_t BATCH_SIZE = 128;
                std::size_t deleted = 0;

                std::vector<std::uint32_t> ledgersToDelete;
                for (const auto& item : ledgers_)
                {
                    if (deleted >= BATCH_SIZE)
                        break;
                    if (item.first < cutoffSeq)
                    {
                        ledgersToDelete.push_back(item.first);
                        deleted++;
                    }
                }

                for (auto seq : ledgersToDelete)
                {
                    auto& ledgerToDelete = ledgers_[seq];

                    for (const auto& txPair : ledgerToDelete.transactions)
                    {
                        transactionMap_.erase(txPair.first);
                    }

                    ledgerHashToSeq_.erase(ledgerToDelete.info.hash);
                    ledgers_.erase(seq);
                }

                if (deleted > 0)
                {
                    for (auto& [account, data] : accountTxMap_)
                    {
                        auto it = data.ledgerTxMap.begin();
                        while (it != data.ledgerTxMap.end())
                        {
                            if (it->first < cutoffSeq)
                            {
                                for (const auto& seqPair : it->second)
                                {
                                    if (seqPair.second <
                                        data.transactions.size())
                                    {
                                        auto& txPair =
                                            data.transactions[seqPair.second];
                                        txPair.first.reset();
                                        txPair.second.reset();
                                    }
                                }
                                it = data.ledgerTxMap.erase(it);
                            }
                            else
                            {
                                ++it;
                            }
                        }

                        data.transactions.erase(
                            std::remove_if(
                                data.transactions.begin(),
                                data.transactions.end(),
                                [](const auto& tx) {
                                    return !tx.first && !tx.second;
                                }),
                            data.transactions.end());

                        for (auto& [ledgerSeq, txMap] : data.ledgerTxMap)
                        {
                            for (auto& [txSeq, index] : txMap)
                            {
                                auto newIndex = std::distance(
                                    data.transactions.begin(),
                                    std::find(
                                        data.transactions.begin(),
                                        data.transactions.end(),
                                        data.transactions[index]));
                                index = newIndex;
                            }
                        }
                    }

                    app_.getLedgerMaster().clearPriorLedgers(cutoffSeq);
                }
            }
        }

        return true;
    }

    std::optional<LedgerInfo>
    getLedgerInfoByIndex(LedgerIndex ledgerSeq) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.find(ledgerSeq);
        if (it != ledgers_.end())
            return it->second.info;
        return std::nullopt;
    }

    std::optional<LedgerInfo>
    getNewestLedgerInfo() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ledgers_.empty())
            return std::nullopt;
        return ledgers_.rbegin()->second.info;
    }

    std::optional<LedgerInfo>
    getLimitedOldestLedgerInfo(LedgerIndex ledgerFirstIndex) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.lower_bound(ledgerFirstIndex);
        if (it != ledgers_.end())
            return it->second.info;
        return std::nullopt;
    }

    std::optional<LedgerInfo>
    getLimitedNewestLedgerInfo(LedgerIndex ledgerFirstIndex) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.lower_bound(ledgerFirstIndex);
        if (it == ledgers_.end())
            return std::nullopt;
        return ledgers_.rbegin()->second.info;
    }

    std::optional<LedgerInfo>
    getLedgerInfoByHash(uint256 const& ledgerHash) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgerHashToSeq_.find(ledgerHash);
        if (it != ledgerHashToSeq_.end())
            return ledgers_.at(it->second).info;
        return std::nullopt;
    }
    uint256
    getHashByIndex(LedgerIndex ledgerIndex) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.find(ledgerIndex);
        if (it != ledgers_.end())
            return it->second.info.hash;
        return uint256();
    }

    std::optional<LedgerHashPair>
    getHashesByIndex(LedgerIndex ledgerIndex) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = ledgers_.find(ledgerIndex);
        if (it != ledgers_.end())
        {
            return LedgerHashPair{
                it->second.info.hash, it->second.info.parentHash};
        }
        return std::nullopt;
    }

    std::map<LedgerIndex, LedgerHashPair>
    getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::map<LedgerIndex, LedgerHashPair> result;
        auto it = ledgers_.lower_bound(minSeq);
        auto end = ledgers_.upper_bound(maxSeq);
        for (; it != end; ++it)
        {
            result[it->first] = LedgerHashPair{
                it->second.info.hash, it->second.info.parentHash};
        }
        return result;
    }

    std::variant<AccountTx, TxSearched>
    getTransaction(
        uint256 const& id,
        std::optional<ClosedInterval<std::uint32_t>> const& range,
        error_code_i& ec) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = transactionMap_.find(id);
        if (it != transactionMap_.end())
        {
            const auto& [txn, txMeta] = it->second;
            std::uint32_t inLedger =
                rangeCheckedCast<std::uint32_t>(txMeta->getLgrSeq());
            it->second.first->setStatus(COMMITTED);
            it->second.first->setLedger(inLedger);
            return it->second;
        }

        if (range)
        {
            std::size_t count = 0;
            for (LedgerIndex seq = range->first(); seq <= range->last(); ++seq)
            {
                if (ledgers_.find(seq) != ledgers_.end())
                    ++count;
            }
            return (count == (range->last() - range->first() + 1))
                ? TxSearched::all
                : TxSearched::some;
        }

        return TxSearched::unknown;
    }

    bool
    ledgerDbHasSpace(Config const& config) override
    {
        return true;  // In-memory database always has space
    }

    bool
    transactionDbHasSpace(Config const& config) override
    {
        return true;  // In-memory database always has space
    }

    std::uint32_t
    getKBUsedAll() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::uint32_t size = sizeof(*this);
        size += ledgers_.size() * (sizeof(LedgerIndex) + sizeof(LedgerData));
        size +=
            ledgerHashToSeq_.size() * (sizeof(uint256) + sizeof(LedgerIndex));
        size += transactionMap_.size() * (sizeof(uint256) + sizeof(AccountTx));
        for (const auto& [_, accountData] : accountTxMap_)
        {
            size += sizeof(AccountID) + sizeof(AccountTxData);
            size += accountData.transactions.size() * sizeof(AccountTx);
            for (const auto& [_, innerMap] : accountData.ledgerTxMap)
            {
                size += sizeof(uint32_t) +
                    innerMap.size() * (sizeof(uint32_t) + sizeof(size_t));
            }
        }
        return size / 1024;
    }

    std::uint32_t
    getKBUsedLedger() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::uint32_t size = 0;
        size += ledgers_.size() * (sizeof(LedgerIndex) + sizeof(LedgerData));
        size +=
            ledgerHashToSeq_.size() * (sizeof(uint256) + sizeof(LedgerIndex));
        return size / 1024;
    }

    std::uint32_t
    getKBUsedTransaction() override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::uint32_t size = 0;
        size += transactionMap_.size() * (sizeof(uint256) + sizeof(AccountTx));
        for (const auto& [_, accountData] : accountTxMap_)
        {
            size += sizeof(AccountID) + sizeof(AccountTxData);
            size += accountData.transactions.size() * sizeof(AccountTx);
            for (const auto& [_, innerMap] : accountData.ledgerTxMap)
            {
                size += sizeof(uint32_t) +
                    innerMap.size() * (sizeof(uint32_t) + sizeof(size_t));
            }
        }
        return size / 1024;
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

    ~RWDBDatabase()
    {
        // Regular maps can use standard clear
        accountTxMap_.clear();
        transactionMap_.clear();
        for (auto& ledger : ledgers_)
        {
            ledger.second.transactions.clear();
        }
        ledgers_.clear();
        ledgerHashToSeq_.clear();
    }

    std::vector<std::shared_ptr<Transaction>>
    getTxHistory(LedgerIndex startIndex) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<std::shared_ptr<Transaction>> result;

        int skipped = 0;
        int collected = 0;

        for (auto it = ledgers_.rbegin(); it != ledgers_.rend(); ++it)
        {
            const auto& transactions = it->second.transactions;
            for (const auto& [txHash, accountTx] : transactions)
            {
                if (skipped < startIndex)
                {
                    ++skipped;
                    continue;
                }

                if (collected >= 20)
                {
                    break;
                }

                std::uint32_t const inLedger = rangeCheckedCast<std::uint32_t>(
                    accountTx.second->getLgrSeq());
                accountTx.first->setStatus(COMMITTED);
                accountTx.first->setLedger(inLedger);
                result.push_back(accountTx.first);
                ++collected;
            }

            if (collected >= 20)
                break;
        }
        return result;
    }

    // Helper function to handle limits
    template <typename Container>
    void
    applyLimit(Container& container, std::size_t limit, bool bUnlimited)
    {
        if (!bUnlimited && limit > 0 && container.size() > limit)
        {
            container.resize(limit);
        }
    }

    AccountTxs
    getOldestAccountTxs(AccountTxOptions const& options) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        AccountTxs result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::size_t skipped = 0;
        for (; txIt != txEnd &&
             (options.bUnlimited || result.size() < options.limit);
             ++txIt)
        {
            for (const auto& [txSeq, txIndex] : txIt->second)
            {
                if (skipped < options.offset)
                {
                    ++skipped;
                    continue;
                }
                AccountTx const accountTx = accountData.transactions[txIndex];
                std::uint32_t const inLedger = rangeCheckedCast<std::uint32_t>(
                    accountTx.second->getLgrSeq());
                accountTx.first->setStatus(COMMITTED);
                accountTx.first->setLedger(inLedger);
                result.push_back(accountTx);
                if (!options.bUnlimited && result.size() >= options.limit)
                    break;
            }
        }

        return result;
    }

    AccountTxs
    getNewestAccountTxs(AccountTxOptions const& options) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        AccountTxs result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::size_t skipped = 0;
        for (auto rIt = std::make_reverse_iterator(txEnd);
             rIt != std::make_reverse_iterator(txIt) &&
             (options.bUnlimited || result.size() < options.limit);
             ++rIt)
        {
            for (auto innerRIt = rIt->second.rbegin();
                 innerRIt != rIt->second.rend();
                 ++innerRIt)
            {
                if (skipped < options.offset)
                {
                    ++skipped;
                    continue;
                }
                AccountTx const accountTx =
                    accountData.transactions[innerRIt->second];
                std::uint32_t const inLedger = rangeCheckedCast<std::uint32_t>(
                    accountTx.second->getLgrSeq());
                accountTx.first->setLedger(inLedger);
                result.push_back(accountTx);
                if (!options.bUnlimited && result.size() >= options.limit)
                    break;
            }
        }

        return result;
    }

    MetaTxsList
    getOldestAccountTxsB(AccountTxOptions const& options) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        MetaTxsList result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::size_t skipped = 0;
        for (; txIt != txEnd &&
             (options.bUnlimited || result.size() < options.limit);
             ++txIt)
        {
            for (const auto& [txSeq, txIndex] : txIt->second)
            {
                if (skipped < options.offset)
                {
                    ++skipped;
                    continue;
                }
                const auto& [txn, txMeta] = accountData.transactions[txIndex];
                result.emplace_back(
                    txn->getSTransaction()->getSerializer().peekData(),
                    txMeta->getAsObject().getSerializer().peekData(),
                    txIt->first);
                if (!options.bUnlimited && result.size() >= options.limit)
                    break;
            }
        }

        return result;
    }

    MetaTxsList
    getNewestAccountTxsB(AccountTxOptions const& options) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {};

        MetaTxsList result;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        std::size_t skipped = 0;
        for (auto rIt = std::make_reverse_iterator(txEnd);
             rIt != std::make_reverse_iterator(txIt) &&
             (options.bUnlimited || result.size() < options.limit);
             ++rIt)
        {
            for (auto innerRIt = rIt->second.rbegin();
                 innerRIt != rIt->second.rend();
                 ++innerRIt)
            {
                if (skipped < options.offset)
                {
                    ++skipped;
                    continue;
                }
                const auto& [txn, txMeta] =
                    accountData.transactions[innerRIt->second];
                result.emplace_back(
                    txn->getSTransaction()->getSerializer().peekData(),
                    txMeta->getAsObject().getSerializer().peekData(),
                    rIt->first);
                if (!options.bUnlimited && result.size() >= options.limit)
                    break;
            }
        }

        return result;
    }

    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    oldestAccountTxPage(AccountTxPageOptions const& options) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {{}, std::nullopt};

        AccountTxs result;

        auto onUnsavedLedger =
            std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);

        std::optional<AccountTxMarker> marker;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        bool lookingForMarker = options.marker.has_value();
        std::size_t count = 0;
        std::uint32_t findLedger = 0, findSeq = 0;
        if (lookingForMarker)
        {
            findLedger = options.marker->ledgerSeq;
            findSeq = options.marker->txnSeq;
        }

        for (; txIt != txEnd; ++txIt)
        {
            for (auto seqIt = txIt->second.begin(); seqIt != txIt->second.end();
                 ++seqIt)
            {
                const auto& [txSeq, txIndex] = *seqIt;

                if (lookingForMarker)
                {
                    if (findLedger == txIt->first && findSeq == txSeq)
                        lookingForMarker = false;
                    continue;
                }

                std::uint32_t const ledgerSeq = txIt->first;
                onUnsavedLedger(ledgerSeq);
                convertBlobsToTxResult(
                    result,
                    rangeCheckedCast<std::uint32_t>(ledgerSeq),
                    "COMMITTED",
                    accountData.transactions[txIndex]
                        .first->getSTransaction()
                        ->getSerializer()
                        .peekData(),
                    accountData.transactions[txIndex]
                        .second->getAsObject()
                        .getSerializer()
                        .peekData(),
                    app_);
                ++count;

                if (options.limit > 0 && count >= options.limit)
                {
                    marker = AccountTxMarker{txIt->first, txSeq};
                    auto nextSeqIt = seqIt;
                    ++nextSeqIt;
                    bool hasMore = (nextSeqIt != txIt->second.end());

                    if (!hasMore)
                    {
                        auto nextTxIt = txIt;
                        ++nextTxIt;
                        hasMore = (nextTxIt != txEnd);
                    }

                    if (!hasMore)
                        marker = std::nullopt;
                    break;
                }
            }

            if (options.limit > 0 && count >= options.limit)
            {
                break;
            }
        }

        return {result, marker};
    }

    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    newestAccountTxPage(AccountTxPageOptions const& options) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {{}, std::nullopt};

        AccountTxs result;

        auto onUnsavedLedger =
            std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);

        std::optional<AccountTxMarker> marker;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        bool lookingForMarker = options.marker.has_value();
        std::size_t count = 0;
        std::uint32_t findLedger = 0, findSeq = 0;
        if (lookingForMarker)
        {
            findLedger = options.marker->ledgerSeq;
            findSeq = options.marker->txnSeq;
        }

        auto rtxIt = std::make_reverse_iterator(txEnd);
        auto rtxEnd = std::make_reverse_iterator(txIt);
        for (; rtxIt != rtxEnd; ++rtxIt)
        {
            for (auto innerRIt = rtxIt->second.rbegin();
                 innerRIt != rtxIt->second.rend();
                 ++innerRIt)
            {
                const auto& [txSeq, txIndex] = *innerRIt;

                if (lookingForMarker)
                {
                    if (findLedger == rtxIt->first && findSeq == txSeq)
                        lookingForMarker = false;
                    continue;
                }
                std::uint32_t const ledgerSeq = rtxIt->first;
                onUnsavedLedger(ledgerSeq);
                convertBlobsToTxResult(
                    result,
                    rangeCheckedCast<std::uint32_t>(ledgerSeq),
                    "COMMITTED",
                    accountData.transactions[txIndex]
                        .first->getSTransaction()
                        ->getSerializer()
                        .peekData(),
                    accountData.transactions[txIndex]
                        .second->getAsObject()
                        .getSerializer()
                        .peekData(),
                    app_);
                ++count;

                if (options.limit > 0 && count >= options.limit)
                {
                    marker = AccountTxMarker{rtxIt->first, txSeq};
                    auto nextSeqIt = innerRIt;
                    ++nextSeqIt;
                    bool hasMore = (nextSeqIt != rtxIt->second.rend());

                    if (!hasMore)
                    {
                        auto nextTxIt = rtxIt;
                        ++nextTxIt;
                        hasMore = (nextTxIt != rtxEnd);
                    }

                    if (!hasMore)
                        marker = std::nullopt;

                    break;
                }
            }

            if (options.limit > 0 && count >= options.limit)
            {
                break;
            }
        }

        return {result, marker};
    }

    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    oldestAccountTxPageB(AccountTxPageOptions const& options) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {{}, std::nullopt};

        MetaTxsList result;

        auto onUnsavedLedger =
            std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);

        std::optional<AccountTxMarker> marker;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        bool lookingForMarker = options.marker.has_value();
        std::size_t count = 0;
        std::uint32_t findLedger = 0, findSeq = 0;
        if (lookingForMarker)
        {
            findLedger = options.marker->ledgerSeq;
            findSeq = options.marker->txnSeq;
        }

        for (; txIt != txEnd && (options.limit == 0 || count < options.limit);
             ++txIt)
        {
            for (const auto& [txSeq, txIndex] : txIt->second)
            {
                if (lookingForMarker)
                {
                    if (findLedger == txIt->first && findSeq == txSeq)
                        lookingForMarker = false;
                    continue;
                }

                const auto& [txn, txMeta] = accountData.transactions[txIndex];
                std::uint32_t const ledgerSeq = txIt->first;
                onUnsavedLedger(ledgerSeq);
                result.emplace_back(
                    txn->getSTransaction()->getSerializer().peekData(),
                    txMeta->getAsObject().getSerializer().peekData(),
                    txIt->first);
                ++count;

                if (options.limit > 0 && count >= options.limit)
                {
                    marker = AccountTxMarker{txIt->first, txSeq};
                    break;
                }
            }
        }

        return {result, marker};
    }

    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    newestAccountTxPageB(AccountTxPageOptions const& options) override
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = accountTxMap_.find(options.account);
        if (it == accountTxMap_.end())
            return {{}, std::nullopt};

        MetaTxsList result;

        auto onUnsavedLedger =
            std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);

        std::optional<AccountTxMarker> marker;
        const auto& accountData = it->second;
        auto txIt = accountData.ledgerTxMap.lower_bound(options.minLedger);
        auto txEnd = accountData.ledgerTxMap.upper_bound(options.maxLedger);

        bool lookingForMarker = options.marker.has_value();
        std::size_t count = 0;
        std::uint32_t findLedger = 0, findSeq = 0;
        if (lookingForMarker)
        {
            findLedger = options.marker->ledgerSeq;
            findSeq = options.marker->txnSeq;
        }

        for (auto rIt = std::make_reverse_iterator(txEnd);
             rIt != std::make_reverse_iterator(txIt) &&
             (options.limit == 0 || count < options.limit);
             ++rIt)
        {
            for (auto innerRIt = rIt->second.rbegin();
                 innerRIt != rIt->second.rend();
                 ++innerRIt)
            {
                if (lookingForMarker)
                {
                    if (findLedger == rIt->first && findSeq == innerRIt->first)
                        lookingForMarker = false;
                    continue;
                }

                const auto& [txn, txMeta] =
                    accountData.transactions[innerRIt->second];
                std::uint32_t const ledgerSeq = txIt->first;
                onUnsavedLedger(ledgerSeq);
                result.emplace_back(
                    txn->getSTransaction()->getSerializer().peekData(),
                    txMeta->getAsObject().getSerializer().peekData(),
                    rIt->first);
                ++count;

                if (options.limit > 0 && count >= options.limit)
                {
                    marker = AccountTxMarker{rIt->first, innerRIt->first};
                    break;
                }
            }
        }

        return {result, marker};
    }
};

// Factory function
std::unique_ptr<SQLiteDatabase>
getRWDBDatabase(Application& app, Config const& config, JobQueue& jobQueue)
{
    return std::make_unique<RWDBDatabase>(app, config, jobQueue);
}

}  // namespace ripple
#endif  // RIPPLE_APP_RDB_BACKEND_MEMORYDATABASE_H_INCLUDED
