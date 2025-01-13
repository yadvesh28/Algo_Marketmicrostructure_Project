/*================================================================================
*     Source: ../RCM/StrategyStudio/examples/strategies/SimpleMomentumStrategy/SimpleMomentumStrategy.h
*     Last Update: 2021/04/15 13:55:14
*     Contents:
*     Distribution:
*
*
*     Copyright (c) RCM-X, 2011 - 2021.
*     All rights reserved.
*
*     This software is part of Licensed material, which is the property of RCM-X ("Company"), 
*     and constitutes Confidential Information of the Company.
*     Unauthorized use, modification, duplication or distribution is strictly prohibited by Federal law.
*     No title to or ownership of this software is hereby transferred.
*
*     The software is provided "as is", and in no event shall the Company or any of its affiliates or successors be liable for any 
*     damages, including any lost profits or other incidental or consequential damages relating to the use of this software.       
*     The Company makes no representations or warranties, express or implied, with regards to this software.                        
/*================================================================================*/

#pragma once

#ifndef _STRATEGY_STUDIO_LIB_STOP_LOSS_HUNTER_STRATEGY_H_
#define _STRATEGY_STUDIO_LIB_STOP_LOSS_HUNTER_STRATEGY_H_

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
#include <tuple>

using namespace RCM::StrategyStudio;

// Trading state for each instrument
struct InstrumentState {
    enum Status {
        IDLE,           // Idle, waiting for something favorable in markets
        HUNTING,        // Near significant level, ready to enter
        IN_POSITION,    // Have an active position
        EXITING         // Exit orders working
    };

    // Status: IDLE ---> HUNTING (Price in our target region, send orders) ---> IN_POSITION (Entered trade) ---> EXITING (Sending exit orders) ---> IDLE (Exit Orders Executed)

    InstrumentState() : 
        status(IDLE),
        last_high(0),
        last_low(0),
        entry_price(0),
        entry_time(boost::posix_time::not_a_date_time),
        position_side(0) {}  // 1 for long, -1 for short, 0 for flat

    Status status;
    double last_high;
    double last_low;
    double entry_price;
    TimeType entry_time;
    int position_side;
};

class StopLossHunter : public Strategy {
public:
    StopLossHunter(StrategyID strategyID, const std::string& strategyName, const std::string& groupName);
    ~StopLossHunter();

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
    void UpdateHighLow(const Instrument* instrument, double price);
    bool IsNearSignificantLevel(const Instrument* instrument, double price, bool& is_near_high);
    bool IsSafeToTrade(const Instrument* instrument);
    double CalculateVolatility(const Instrument* instrument);
    void ProcessPotentialEntry(const Instrument* instrument, double price);
    void ManagePosition(const Instrument* instrument, double price);
    void SendOrder(const Instrument* instrument, bool is_buy, int quantity);

private: // Strategy parameters
    double entry_range_ticks_;     // Range around highs/lows to enter
    double target_ticks_;          // Profit target in ticks from entry price
    double max_loss_ticks_;        // Stop loss in ticks from entry price
    int lookback_period_;          // Period for high/low calculation
    int volatility_period_;        // Period for volatility check
    double volatility_threshold_;  // Minimum rolling volatility needed
    double account_risk_per_trade_; // Risk per trade (0.1%)
    bool debug_;                   // Debug mode flag

private: // Strategy state
    std::unordered_map<const Instrument*, InstrumentState> instrument_states_;
    std::unordered_map<const Instrument*, Analytics::ScalarRollingWindow<double>> price_windows_;
    std::unordered_map<const Instrument*, Analytics::ScalarRollingWindow<double>> volatility_windows_;
};

extern "C" {
    _STRATEGY_EXPORTS const char* GetType() { return "StopLossHunter"; }
    _STRATEGY_EXPORTS const char* GetAuthor() { return "##"; }
    _STRATEGY_EXPORTS const char* GetAuthorGroup() { return "##"; }
    _STRATEGY_EXPORTS const char* GetReleaseVersion() { return Strategy::release_version(); }
    
    _STRATEGY_EXPORTS IStrategy* CreateStrategy(const char* strategyType, 
                                              unsigned strategyID, 
                                              const char* strategyName,
                                              const char* groupName) {
        if (strcmp(strategyType, GetType()) == 0) {
            return *(new StopLossHunter(strategyID, strategyName, groupName));
        }
        return nullptr;
    }
}

#endif
