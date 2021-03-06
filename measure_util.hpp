#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <algorithm>
#include <limits>
#include <type_traits>
#include <thread>
#include "util.hpp"
#include "random.hpp"
#include "cmdline_option.hpp"
#include "thread_util.hpp"


constexpr size_t CACHE_LINE_SIZE = 64;


template <typename T>
void unused(T&)
{
}


void sleepMs(size_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}


template <typename Random>
void fillMuIdVecLoop(std::vector<size_t>& muIdV, Random& rand, size_t max)
{
    for (size_t i = 0; i < muIdV.size(); i++) {
      retry:
        size_t v = rand() % max;
        for (size_t j = 0; j < i; j++) {
            if (muIdV[j] == v) goto retry;
        }
        muIdV[i] = v;
    }
}


template <typename Random>
void fillMuIdVecHash(std::vector<size_t>& muIdV, Random& rand, size_t max)
{
    std::unordered_set<size_t> set;
    std::unordered_set<size_t>::iterator it;
    for (size_t i = 0; i < muIdV.size(); i++) {
        for (;;) {
            size_t v = rand() % max;
            bool b;
            std::tie(it, b) = set.insert(v);
            if (!b) continue;
            muIdV[i] = v;
            break;
        }
    }
}

template <typename Random>
void fillMuIdVecTree(std::vector<size_t>& muIdV, Random& rand, size_t max)
{
    assert(muIdV.size() <= max);
    std::set<size_t> set;
    for (size_t i = 0; i < max; i++) set.insert(i);

    for (size_t i = 0; i < muIdV.size(); i++) {
        assert(!set.empty());
        size_t v = rand() % max;
        std::set<size_t>::iterator it = set.lower_bound(v);
        if (it == set.end()) --it;
        v = *it;
        set.erase(it);
        muIdV[i] = v;
    }
}

template <typename Random>
void fillMuIdVecArray(std::vector<size_t>& muIdV, Random& rand, size_t max, std::vector<size_t>& tmpV)
{
    assert(max > 1);
    assert(muIdV.size() <= max);
    tmpV.resize(max);
    for (size_t i = 0; i < max; i++) tmpV[i] = i;
    const size_t n = muIdV.size();
    for (size_t i = 0; i < n; i++) {
        size_t j = i + (rand() % (max - i));
        std::swap(tmpV[i], tmpV[j]);
        muIdV[i] = tmpV[i];
    }
    muIdV[n - 1] = tmpV[n - 1];
}

template <typename Random>
void fillModeVec(std::vector<bool>& isWriteV, Random& rand, size_t nrWrite, std::vector<size_t>& tmpV)
{
    const size_t max = isWriteV.size();
    assert(nrWrite <= max);
#if 0
    for (size_t i = 0; i < nrWrite; i++) {
        isWriteV[i] = true;
    }
    for (size_t i = nrWrite; i < max; i++) {
        isWriteV[i] = false;
    }
    for (size_t i = 0; i < nrWrite; i++) {
        size_t j = i + (rand() % (max - i));
        std::swap(isWriteV[i], isWriteV[j]);
    }
#else
    tmpV.resize(max);
    for (size_t i = 0; i < max; i++) tmpV[i] = i;
    for (size_t i = 0; i < nrWrite; i++) {
        size_t j = i + (rand() % (max - i));
        std::swap(tmpV[i], tmpV[j]);
    }
    for (size_t i = 0; i < max; i++) isWriteV[i] = false;
    for (size_t i = 0; i < nrWrite; i++) isWriteV[tmpV[i]] = true;
#endif
}


template <typename Random>
class DistinctRandom
{
    using Value = typename Random::ResultType;
    Random& rand_;
    Value max_;
    std::unordered_set<Value> set_;
public:
    explicit DistinctRandom(Random& rand)
        : rand_(rand), max_(std::numeric_limits<Value>::max()), set_() {
    }
    Value operator()() {
        assert(max_ > 0);
        Value v;
        typename std::unordered_set<Value>::iterator it;
        bool ret = false;
        while (!ret) {
            v = rand_() % max_;
            std::tie(it, ret) = set_.insert(v);
        }
        return v;
    }
    void clear(Value max) {
        assert(max > 0);
        max_ = max;
        set_.clear();
    }
};


template <typename Random>
class BoolRandom
{
    Random rand_;
    typename Random::ResultType value_;
    uint16_t counts_;
public:
    explicit BoolRandom(Random& rand) : rand_(rand), value_(0), counts_(0) {}
    bool operator()() {
        if (counts_ == 0) {
            value_ = rand_();
            counts_ = sizeof(typename Random::ResultType) * 8;
        }
        const bool ret = value_ & 0x1;
        value_ >>= 1;
        --counts_;
        return ret;
    }
};


struct RetryCounts
{
    using Pair = std::pair<size_t, size_t>;

    std::unordered_map<size_t, size_t> retryCounts;

    void add(size_t nrRetry, size_t nr = 1) {
        std::unordered_map<size_t, size_t>::iterator it = retryCounts.find(nrRetry);
        if (it == retryCounts.end()) {
            retryCounts.emplace(nrRetry, nr);
        } else {
            it->second += nr;
        }
    }
    void merge(const RetryCounts& rhs) {
        for (const Pair& p : rhs.retryCounts) {
            add(p.first, p.second);
        }
    }
    friend std::ostream& out(std::ostream& os, const RetryCounts& rc, bool verbose) {
        std::vector<Pair> v;
        v.reserve(rc.retryCounts.size());
        for (const Pair& p : rc.retryCounts) {
            v.push_back(p);
        }
        std::sort(v.begin(), v.end());

#if 0
        if (verbose) {
            for (const Pair& p : v) {
                os << cybozu::util::formatString("%5zu %zu\n", p.first, p.second);
            }
        } else {
            if (!v.empty()) {
                os << cybozu::util::formatString("max_retry %zu", v.back().first);
            } else {
                os << "max_retry 0";
            }
        }
#else
        unused(verbose);
#endif
        return os;
    }
    friend std::ostream& operator<<(std::ostream& os, const RetryCounts& rc) {
        return out(os, rc, true);
    }
    std::string str(bool verbose = true) const {
        std::stringstream ss;
        out(ss, *this, verbose);
        return ss.str();
    }
};


struct Result
{
    RetryCounts rcS;
    RetryCounts rcL;
    size_t value[6];
    Result() : rcS(), rcL(), value() {}
    void operator+=(const Result& rhs) {
        rcS.merge(rhs.rcS);
        rcL.merge(rhs.rcL);
        for (size_t i = 0; i < 6; i++) {
            value[i] += rhs.value[i];
        }
    }
    size_t nrCommit() const { return value[0] + value[1]; }
    void incCommit(bool isLongTx) { value[isLongTx ? 1 : 0]++; }
    void addCommit(bool isLongTx, size_t v) { value[isLongTx ? 1 : 0] += v; }
    void incAbort(bool isLongTx) { value[isLongTx ? 3 : 2]++; }
    void incIntercepted(bool isLongTx) { value[isLongTx ? 5 : 4]++; }
    void addRetryCount(bool isLongTx, size_t nrRetry) {
#if 0
        if (isLongTx) {
            rcL.add(nrRetry);
        } else {
            rcS.add(nrRetry);
        }
#else
        unused(isLongTx);
        unused(nrRetry);
#endif
    }

    friend std::ostream& operator<<(std::ostream& os, const Result& res) {
        const bool verbose = false; // QQQ
#if 0
        os << cybozu::util::formatString(
            "commit S %zu L %zu  abort S %zu L %zu  intercepted S %zu L %zu"
            , res.value[0], res.value[1]
            , res.value[2], res.value[3]
            , res.value[4], res.value[5]);
        os << "  ";
        out(os, res.rcS, verbose);
        os << "  ";
        out(os, res.rcL, verbose);
        os << "\n";
        return os;
#else
        os << cybozu::util::formatString(
            "commitS:%zu commitL:%zu abortS:%zu abortL:%zu interceptedS:%zu interceptedL:%zu"
            , res.value[0], res.value[1]
            , res.value[2], res.value[3]
            , res.value[4], res.value[5]);
        if (verbose) {
            // not yet implemented.
        }
        return os;
#endif
    }
    std::string str() const {
        std::stringstream ss;
        ss << *this;
        return ss.str();
    }
};


template <typename SharedData, typename Worker>
void runExec(const CmdLineOption& opt, SharedData& shared, Worker&& worker)
{
    const size_t nrTh = opt.nrTh;

    bool start = false;
    bool quit = false;
    bool shouldQuit = false;
    cybozu::thread::ThreadRunnerSet thS;
    std::vector<Result> resV(nrTh);
    for (size_t i = 0; i < nrTh; i++) {
        thS.add([&,i]() {
                resV[i] = worker(i, start, quit, shouldQuit, shared);
            });
    }
    thS.start();
    start = true;
    size_t sec = 0;
    for (size_t i = 0; i < opt.runSec; i++) {
        if (opt.verbose) {
            ::printf("%zu\n", i);
        }
        sleepMs(1000);
        sec++;
        if (shouldQuit) break;
    }
    quit = true;
    thS.join();
    Result res;
    for (size_t i = 0; i < nrTh; i++) {
        if (opt.verbose) {
            ::printf("worker %zu  %s\n", i, resV[i].str().c_str());
        }
        res += resV[i];
    }
    ::printf("%s tps:%.03f %s\n"
             , opt.str().c_str()
             , res.nrCommit() / (double)opt.runSec
             , res.str().c_str());
    ::fflush(::stdout);
}

enum TxMode
{
    USE_LAST_WRITE_TX = 0,
    USE_FIRST_WRITE_TX = 1,
    USE_READONLY_TX = 2,
    USE_WRITEONLY_TX = 3,
    USE_HALF_AND_HALF_TX = 4,
    USE_MIX_TX = 5,
};

enum TxIdGenType
{
    SCALABLE_TXID_GEN = 0,
    BULK_TXID_GEN = 1,
    SIMPLE_TXID_GEN = 2,
};



template <typename Random, typename Mode>
class GetModeFunc
{
    Mode (GetModeFunc::*getMode_)(size_t);
    BoolRandom<Random>& boolRand_;
    const std::vector<bool>& isWriteV_;
    size_t sz_;
    size_t nrWr_;

public:
    GetModeFunc(BoolRandom<Random>& boolRand, const std::vector<bool>& isWriteV,
                bool isLongTx, int shortTxMode, int longTxMode, size_t sz, size_t nrWr)
        : getMode_(), boolRand_(boolRand), isWriteV_(isWriteV), sz_(sz), nrWr_(nrWr) {
        if (isLongTx) {
            switch (longTxMode) {
            case USE_HALF_AND_HALF_TX:
                getMode_ = &GetModeFunc::getLong<USE_HALF_AND_HALF_TX>;
                break;
            case USE_READONLY_TX:
                getMode_ = &GetModeFunc::getLong<USE_READONLY_TX>;
                break;
            case USE_FIRST_WRITE_TX:
                getMode_ = &GetModeFunc::getLong<USE_FIRST_WRITE_TX>;
                break;
            default:
                getMode_ = &GetModeFunc::getLong<USE_LAST_WRITE_TX>;
            }
        } else {
            switch (shortTxMode) {
            case USE_MIX_TX:
                getMode_ = &GetModeFunc::getShort<USE_MIX_TX>;
                break;
            case USE_HALF_AND_HALF_TX:
                getMode_ = &GetModeFunc::getShort<USE_HALF_AND_HALF_TX>;
                break;
            case USE_READONLY_TX:
                getMode_ = &GetModeFunc::getShort<USE_READONLY_TX>;
                break;
            case USE_WRITEONLY_TX:
                getMode_ = &GetModeFunc::getShort<USE_WRITEONLY_TX>;
                break;
            case USE_FIRST_WRITE_TX:
                getMode_ = &GetModeFunc::getShort<USE_FIRST_WRITE_TX>;
                break;
            default:
                getMode_ = &GetModeFunc::getShort<USE_LAST_WRITE_TX>;
            }
        }
    }
    Mode operator()(size_t i) {
        return (this->*getMode_)(i);
    }
    template <int shortTxMode>
    Mode getShort(size_t i) {
        switch (shortTxMode) {
        case USE_MIX_TX:
            return isWriteV_[i] ? Mode::X : Mode::S;
        case USE_HALF_AND_HALF_TX:
            return boolRand_() ? Mode::X : Mode::S;
        case USE_READONLY_TX:
            return Mode::S;
        case USE_WRITEONLY_TX:
            return Mode::X;
        case USE_FIRST_WRITE_TX:
            return i < nrWr_ ? Mode::X : Mode::S;
        default:
            assert(shortTxMode == USE_LAST_WRITE_TX);
            return i >= sz_ - nrWr_ ? Mode::X : Mode::S;
        }
    }
    template <int longTxMode>
    Mode getLong(size_t i) {
        switch (longTxMode) {
        case USE_HALF_AND_HALF_TX:
            return boolRand_() ? Mode::X : Mode::S;
        case USE_READONLY_TX:
            return Mode::S;
        case USE_FIRST_WRITE_TX:
            return i < nrWr_ ? Mode::X : Mode::S;
        default:
            assert(longTxMode == USE_LAST_WRITE_TX);
            return i >= sz_ - nrWr_ ? Mode::X : Mode::S;
        }
    }
};
