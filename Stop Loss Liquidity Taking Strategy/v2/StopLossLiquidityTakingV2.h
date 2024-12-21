#pragma once

#ifndef _STRATEGY_STUDIO_LIB_STOP_LOSS_HUNTER_V2_STRATEGY_H_
#define _STRATEGY_STUDIO_LIB_STOP_LOSS_HUNTER_V2_STRATEGY_H_

#ifdef _WIN32
    #define _STRATEGY_EXPORTS __declspec(dllexport)
#else
    #ifndef _STRATEGY_EXPORTS
    #define _STRATEGY_EXPORTS
    #endif
#endif

#include <Strategy.h>
#include "FillInfo.h"
#include "AllEventMsg.h"
#include "ExecutionTypes.h"
#include <Analytics/ScalarRollingWindow.h>
#include <MarketModels/Instrument.h>
#include <Utilities/ParseConfig.h>

#include <algorithm>
#include <unordered_map>
#include <deque>
#include <tuple>

using namespace RCM::StrategyStudio;

struct InstrumentState {
    enum Status {
        IDLE,           // Idle, waiting for something favorable in markets
        HUNTING,        // Near significant level, ready to enter
        IN_POSITION,    // Have an active position
        EXITING,        // Exit orders working
        NO_TRADE       // Level breached, waiting for new hourly bar
    };

    InstrumentState() : 
        status(IDLE),
        hourly_high(0),
        hourly_low(std::numeric_limits<double>::max()),
        entry_price(0),
        target_price(0),
        entry_time(boost::posix_time::not_a_date_time),
        last_bar_time(boost::posix_time::not_a_date_time),
        last_tick_price(0),
        position_side(0),
        market_order_id(0),
        limit_order_id(0) {}

    Status status;
    double hourly_high;    // High from the last completed 1-hour bar
    double hourly_low;     // Low from the last completed 1-hour bar
    double entry_price;    // Market order fill price
    double target_price;   // Limit order target price
    TimeType entry_time;   // Time of market order fill
    TimeType last_bar_time;
    double last_tick_price;
    int position_side;
    OrderID market_order_id;  // Track market order
    OrderID limit_order_id;   // Track limit order
    std::deque<int> tick_directions;
};

class StopLossHunterV2 : public Strategy {
public:
    StopLossHunterV2(StrategyID strategyID, const std::string& strategyName, const std::string& groupName);
    ~StopLossHunterV2();

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
    bool IsNearSignificantLevel(const Instrument* instrument, double price, bool& is_near_high);
    bool IsSafeToTrade(const Instrument* instrument);
    void UpdateTickMomentum(const Instrument* instrument, double price);
    int GetTickMomentumSignal(const Instrument* instrument);
    void ProcessPotentialEntry(const Instrument* instrument, double price);
    void CheckTimeBasedExit(const Instrument* instrument);
    void SendMarketOrder(const Instrument* instrument, bool is_buy, int quantity);
    void SendLimitOrder(const Instrument* instrument, bool is_buy, int quantity, double price);
    void ExitPosition(const Instrument* instrument);
    void ManageExits(const Instrument* instrument);

private: // Strategy parameters
    double entry_range_ticks_;     // Range around highs/lows to enter
    double target_ticks_;          // Profit target in ticks from entry price
    int tick_lookback_;            // Number of ticks to look back (default 19)
    int momentum_threshold_;       // Threshold for momentum signal
    int max_hold_seconds_;        // Maximum time to hold position (default 15)
    double account_risk_per_trade_; // Risk per trade (0.1%)
    bool debug_;                   // Debug mode flag

private: // Strategy state
    std::unordered_map<const Instrument*, InstrumentState> instrument_states_;
    TimeType current_strategy_time_;  // Track current time based on trade events
};

extern "C" {
    _STRATEGY_EXPORTS const char* GetType() { return "StopLossHunterV2"; }
    _STRATEGY_EXPORTS const char* GetAuthor() { return "dlariviere"; }
    _STRATEGY_EXPORTS const char* GetAuthorGroup() { return "UIUC"; }
    _STRATEGY_EXPORTS const char* GetReleaseVersion() { return Strategy::release_version(); }
    
    _STRATEGY_EXPORTS IStrategy* CreateStrategy(const char* strategyType, 
                                              unsigned strategyID, 
                                              const char* strategyName,
                                              const char* groupName) {
        if (strcmp(strategyType, GetType()) == 0) {
            return *(new StopLossHunterV2(strategyID, strategyName, groupName));
        }
        return nullptr;
    }
}

#endif