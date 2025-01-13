#pragma once

#ifndef _STRATEGY_STUDIO_LIB_TRADE_IMPACT_MM_STRATEGY_H_
#define _STRATEGY_STUDIO_LIB_TRADE_IMPACT_MM_STRATEGY_H_

#ifdef _WIN32
    #define _STRATEGY_EXPORTS __declspec(dllexport)
#else
    #ifndef _STRATEGY_EXPORTS
    #define _STRATEGY_EXPORTS
    #endif
#endif

#include <Strategy.h>
#include <Analytics/ScalarRollingWindow.h>
#include <MarketModels/Instrument.h>
#include <Utilities/ParseConfig.h>
#include "FillInfo.h"
#include "AllEventMsg.h"
#include "ExecutionTypes.h"
#include <deque>
#include <set>
#include <unordered_map>

using namespace RCM::StrategyStudio;
using namespace RCM::StrategyStudio::MarketModels;

// Trading state for each instrument
struct InstrumentState {
    InstrumentState() : 
        current_bid(0),
        current_ask(0),
        avg_position_price(0),
        last_quote_update(boost::posix_time::not_a_date_time) {}

    std::set<OrderID> active_orders;  // Changed from string to OrderID
    double current_bid;
    double current_ask;
    double avg_position_price;
    TimeType last_quote_update;
};

class TradeImpactMM : public Strategy {
public:
    TradeImpactMM(StrategyID strategyID, const std::string& strategyName, const std::string& groupName);
    ~TradeImpactMM();

public: // Event handlers
    virtual void OnTrade(const TradeDataEventMsg& msg);
    virtual void OnTopQuote(const QuoteEventMsg& msg);
    virtual void OnBar(const BarEventMsg& msg);
    virtual void OnOrderUpdate(const OrderUpdateEventMsg& msg);
    virtual void OnResetStrategyState();
    virtual void OnParamChanged(StrategyParam& param);

private: // Strategy setup
    virtual void RegisterForStrategyEvents(StrategyEventRegister* eventRegister, DateType currDate);
    virtual void DefineStrategyParams();
    virtual void DefineStrategyCommands();

private: // Trading logic
    double CalculateTradeImpact(const Instrument* instrument, double trade_size, bool is_buy);
    std::pair<double, double> CalculateQuotes(const Instrument* instrument);
    void UpdateQuotes(const Instrument* instrument);
    void CancelAllOrders(const Instrument* instrument);
    bool IsSafeToQuote(const Instrument* instrument, double bid_price, double ask_price);
    void LogDebug(const std::string& message);

private: // Strategy parameters
    double impact_multiplier_;      // Trade impact scaling factor
    int rolling_window_;           // Number of trades to consider
    double quantile_threshold_;    // Quantile for quote calculation
    int levels_to_consider_;       // Order book depth to consider
    double tick_size_;            // Minimum price increment
    double max_position_;         // Maximum allowed position
    double risk_limit_pct_;       // Risk limit percentage
    double min_spread_ticks_;     // Minimum quote spread in ticks
    double max_spread_ticks_;     // Maximum quote spread in ticks
    int quote_size_;             // Base quote size
    double min_quote_size_;      // Minimum quote size
    double max_quote_size_;      // Maximum quote size
    bool debug_;                 // Debug mode flag

private: // Strategy state
    std::unordered_map<const Instrument*, std::deque<double>> trade_impacts_;
    std::unordered_map<const Instrument*, InstrumentState> instrument_states_;
};

extern "C" {
    _STRATEGY_EXPORTS const char* GetType() { return "TradeImpactMM"; }
    _STRATEGY_EXPORTS const char* GetAuthor() { return "##"; }
    _STRATEGY_EXPORTS const char* GetAuthorGroup() { return "##"; }
    _STRATEGY_EXPORTS const char* GetReleaseVersion() { return Strategy::release_version(); }

    _STRATEGY_EXPORTS IStrategy* CreateStrategy(const char* strategyType,
                                              unsigned strategyID,
                                              const char* strategyName,
                                              const char* groupName) {
        if (strcmp(strategyType, GetType()) == 0) {
            return *(new TradeImpactMM(strategyID, strategyName, groupName));
        }
        return nullptr;
    }
}
#endif
