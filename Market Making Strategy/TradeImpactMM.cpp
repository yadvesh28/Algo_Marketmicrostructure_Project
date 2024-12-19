/*================================================================================
*     Copyright (c) RCM-X, 2011 - 2024.
*     All rights reserved.
/*================================================================================*/

#ifdef _WIN32
    #include "stdafx.h"
#endif

#include "TradeImpactMM.h"
#include <Utilities/Cast.h>
#include <Utilities/utils.h>
#include <cmath>
#include <algorithm>
#include <sstream>

using namespace RCM::StrategyStudio;
using namespace RCM::StrategyStudio::MarketModels;
using namespace RCM::StrategyStudio::Utilities;
using namespace std;

TradeImpactMM::TradeImpactMM(StrategyID strategyID, const std::string& strategyName, const std::string& groupName):
    Strategy(strategyID, strategyName, groupName),
    impact_multiplier_(2.5),
    rolling_window_(50),
    quantile_threshold_(0.1),
    levels_to_consider_(4),
    tick_size_(0.01),
    max_position_(100),
    risk_limit_pct_(0.02),
    min_spread_ticks_(2),
    max_spread_ticks_(20),
    quote_size_(100),
    min_quote_size_(10),
    max_quote_size_(1000),
    debug_(true)
{
}

TradeImpactMM::~TradeImpactMM()
{
}

void TradeImpactMM::OnResetStrategyState()
{
    try {
        trade_impacts_.clear();
        instrument_states_.clear();
        LogDebug("Strategy state reset");
    } catch (const std::exception& e) {
        logger().LogToClient(LOGLEVEL_ERROR, std::string("Error in reset: ") + e.what());
    }
}

void TradeImpactMM::DefineStrategyParams()
{
    params().CreateParam(CreateStrategyParamArgs("impact_multiplier", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, impact_multiplier_));
    params().CreateParam(CreateStrategyParamArgs("rolling_window", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, rolling_window_));
    params().CreateParam(CreateStrategyParamArgs("quantile_threshold", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, quantile_threshold_));
    params().CreateParam(CreateStrategyParamArgs("levels_to_consider", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, levels_to_consider_));
    params().CreateParam(CreateStrategyParamArgs("tick_size", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, tick_size_));
    params().CreateParam(CreateStrategyParamArgs("max_position", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, max_position_));
    params().CreateParam(CreateStrategyParamArgs("risk_limit_pct", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, risk_limit_pct_));
    params().CreateParam(CreateStrategyParamArgs("min_spread_ticks", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, min_spread_ticks_));
    params().CreateParam(CreateStrategyParamArgs("max_spread_ticks", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, max_spread_ticks_));
    params().CreateParam(CreateStrategyParamArgs("quote_size", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, quote_size_));
    params().CreateParam(CreateStrategyParamArgs("min_quote_size", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, min_quote_size_));
    params().CreateParam(CreateStrategyParamArgs("max_quote_size", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, max_quote_size_));
    params().CreateParam(CreateStrategyParamArgs("debug", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_BOOL, debug_));
}

void TradeImpactMM::RegisterForStrategyEvents(StrategyEventRegister* eventRegister, DateType currDate)
{
    for (SymbolSetConstIter it = symbols_begin(); it != symbols_end(); ++it) {
        eventRegister->RegisterForMarketData(*it);
    }

    for (InstrumentSetConstIter it = instrument_begin(); it != instrument_end(); ++it) {
        instrument_states_.emplace(it->second, InstrumentState());
    }
    
    LogDebug("Strategy events registered");
}

double TradeImpactMM::CalculateTradeImpact(const Instrument* instrument, double trade_size, bool is_buy)
{
    double total_bid_size = 0;
    double total_ask_size = 0;
    
    const Quote& quote = instrument->top_quote();
    
    // Sum up liquidity for top levels
    for (int i = 0; i < levels_to_consider_; ++i) {
        if (quote.bid_side().price_levels().size() > i)
            total_bid_size += quote.bid_side().price_levels()[i].size();
        if (quote.ask_side().price_levels().size() > i)
            total_ask_size += quote.ask_side().price_levels()[i].size();
    }
    
    if (total_bid_size + total_ask_size == 0) return 0;

    return impact_multiplier_ * (is_buy ? 1.0 : -1.0) * 
           (trade_size / (total_bid_size + total_ask_size));
}

std::pair<double, double> TradeImpactMM::CalculateQuotes(const Instrument* instrument)
{
    auto& impacts = trade_impacts_[instrument];
    if (impacts.size() < rolling_window_) {
        return {0.0, 0.0};
    }

    vector<double> buy_impacts, sell_impacts;
    for (const auto& impact : impacts) {
        if (impact > 0) {
            buy_impacts.push_back(impact);
        } else {
            sell_impacts.push_back(abs(impact));
        }
    }

    if (buy_impacts.empty() || sell_impacts.empty()) {
        return {0.0, 0.0};
    }

    sort(buy_impacts.begin(), buy_impacts.end());
    sort(sell_impacts.begin(), sell_impacts.end());

    int buy_idx = max(0, (int)(buy_impacts.size() * quantile_threshold_) - 1);
    int sell_idx = max(0, (int)(sell_impacts.size() * quantile_threshold_) - 1);

    double buy_quantile = buy_impacts[buy_idx];
    double sell_quantile = sell_impacts[sell_idx];

    const Quote& quote = instrument->top_quote();
    if (!quote.ask_side().IsValid() || !quote.bid_side().IsValid()) {
        return {0.0, 0.0};
    }

    double mid_price = (quote.ask() + quote.bid()) / 2.0;

    // Position adjustment
    double position_factor = (portfolio().position(instrument) / max_position_) * risk_limit_pct_;

    // Calculate theoretical prices
    double theo_bid = mid_price - sell_quantile - (position_factor * mid_price);
    double theo_ask = mid_price + buy_quantile - (position_factor * mid_price);

    // Apply spread constraints
    double min_spread = min_spread_ticks_ * tick_size_;
    double max_spread = max_spread_ticks_ * tick_size_;
    if (theo_ask - theo_bid < min_spread) {
        double mid = (theo_ask + theo_bid) / 2.0;
        theo_bid = mid - min_spread / 2.0;
        theo_ask = mid + min_spread / 2.0;
    } else if (theo_ask - theo_bid > max_spread) {
        double mid = (theo_ask + theo_bid) / 2.0;
        theo_bid = mid - max_spread / 2.0;
        theo_ask = mid + max_spread / 2.0;
    }

    // Round to tick size
    theo_bid = floor(theo_bid / tick_size_) * tick_size_;
    theo_ask = ceil(theo_ask / tick_size_) * tick_size_;

    return {theo_bid, theo_ask};
}

void TradeImpactMM::UpdateQuotes(const Instrument* instrument)
{
    try {
        auto& state = instrument_states_[instrument];
        
        // Cancel existing orders
        CancelAllOrders(instrument);

        auto [bid_price, ask_price] = CalculateQuotes(instrument);
        if (bid_price <= 0 || ask_price <= 0 || !IsSafeToQuote(instrument, bid_price, ask_price)) {
            return;
        }

        // Calculate position-adjusted sizes
        double current_pos = portfolio().position(instrument);
        double position_ratio = current_pos / max_position_;
        
        // Base quote size adjusted by position
        double base_size = quote_size_ * (1.0 - abs(position_ratio));
        
        // Adjust bid/ask sizes based on position
        double bid_size = min(max_quote_size_, 
                            max(min_quote_size_, 
                                base_size * (1.0 - position_ratio)));
        
        double ask_size = min(max_quote_size_,
                            max(min_quote_size_,
                                base_size * (1.0 + position_ratio)));

        // Place orders
        if (bid_size >= min_quote_size_) {
            OrderParams bid_params(*instrument,
                                 bid_size,
                                 bid_price,
                                 MARKET_CENTER_ID_IEX,
                                 ORDER_SIDE_BUY,
                                 ORDER_TIF_DAY,
                                 ORDER_TYPE_LIMIT);
            string order_id = trade_actions()->SendNewOrder(bid_params);
            state.active_orders.insert(order_id);
            state.current_bid = bid_price;
        }

        if (ask_size >= min_quote_size_) {
            OrderParams ask_params(*instrument,
                                 ask_size,
                                 ask_price,
                                 MARKET_CENTER_ID_IEX,
                                 ORDER_SIDE_SELL,
                                 ORDER_TIF_DAY,
                                 ORDER_TYPE_LIMIT);
            string order_id = trade_actions()->SendNewOrder(ask_params);
            state.active_orders.insert(order_id);
            state.current_ask = ask_price;
        }

        if (debug_) {
            stringstream ss;
            ss << "Updated quotes for " << instrument->symbol()
               << " Bid: " << bid_price << " x " << bid_size
               << " Ask: " << ask_price << " x " << ask_size
               << " Pos: " << current_pos;
            LogDebug(ss.str());
        }

    } catch (const std::exception& e) {
        logger().LogToClient(LOGLEVEL_ERROR, 
            std::string("Error updating quotes: ") + e.what());
    }
}

void TradeImpactMM::CancelAllOrders(const Instrument* instrument)
{
    auto& state = instrument_states_[instrument];
    for (const auto& order_id : state.active_orders) {
        trade_actions()->SendCancelOrder(order_id);
    }
    state.active_orders.clear();
}

bool TradeImpactMM::IsSafeToQuote(const Instrument* instrument, double bid_price, double ask_price)
{
    const Quote& quote = instrument->top_quote();
    if (!quote.ask_side().IsValid() || !quote.bid_side().IsValid()) {
        return false;
    }

    // Don't cross the market
    if (bid_price >= quote.ask() || ask_price <= quote.bid()) {
        return false;
    }

    // Check spread is reasonable
    double spread = ask_price - bid_price;
    if (spread < min_spread_ticks_ * tick_size_ || spread > max_spread_ticks_ * tick_size_) {
        return false;
    }

    return true;
}

void TradeImpactMM::OnTrade(const TradeDataEventMsg& msg)
{
    try {
        const Instrument* instrument = &msg.instrument();
        double trade_size = msg.trade().size();
        bool is_buy = msg.trade().side() == ORDER_SIDE_BUY;

        // Calculate and store trade impact
        double impact = CalculateTradeImpact(instrument, trade_size, is_buy);
        
        auto& impacts = trade_impacts_[instrument];
        impacts.push_back(impact);
        if (impacts.size() > rolling_window_) {
            impacts.pop_front();
        }

        // Update quotes
        UpdateQuotes(instrument);

        if (debug_) {
            stringstream ss;
            ss << "Trade processed: " << instrument->symbol()
               << " Size: " << trade_size
               << " Side: " << (is_buy ? "BUY" : "SELL")
               << " Impact: " << impact;
            LogDebug(ss.str());
        }
    } catch (const std::exception& e) {
        logger().LogToClient(LOGLEVEL_ERROR, 
            std::string("Error processing trade: ") + e.what());
    }
}

void TradeImpactMM::OnOrderUpdate(const OrderUpdateEventMsg& msg)
{
    try {
        auto& state = instrument_states_[msg.order().instrument()];

        switch (msg.update_type()) {
            case ORDER_UPDATE_TYPE_FILL: {
                // Update position tracking
                double fill_price = msg.fill()->fill_price();
                double fill_size = msg.fill()->fill_size();
                
                // Update average position price
                double current_pos = portfolio().position(msg.order().instrument());
                if (current_pos != 0) {
                    state.avg_position_price = fill_price;
                }
                
                // Remove filled order from tracking
                state.active_orders.erase(msg.order().order_id());
                
                // Update quotes after fill
                UpdateQuotes(msg.order().instrument());
                
                if (debug_) {
                    stringstream ss;
                    ss << "Fill: " << msg.order().instrument()->symbol()
                       << " Price: " << fill_price
                       << " Size: " << fill_size
                       << " Current Pos: " << current_pos;
                    LogDebug(ss.str());
                }
                break;
            }
            case ORDER_UPDATE_TYPE_CANCEL: {
                state.active_orders.erase(msg.order().order_id());
                break;
            }
        }
    } catch (const std::exception& e) {
        logger().LogToClient(LOGLEVEL_ERROR, 
            std::string("Error in order update: ") + e.what());
    }
}

void TradeImpactMM::OnTopQuote(const QuoteEventMsg& msg)
{
    try {
        const Instrument* instrument = &msg.instrument();
        auto& state = instrument_states_[instrument];
        state.last_quote_update = msg.event_time();
        UpdateQuotes(instrument);
    } catch (const std::exception& e) {
        logger().LogToClient(LOGLEVEL_ERROR, 
            std::string("Error in quote update: ") + e.what());
    }
}

void TradeImpactMM::OnBar(const BarEventMsg& msg)
{
    // Not using bars for this strategy
}

void TradeImpactMM::OnOrderBook(const OrderBookEventMsg& msg)
{
    // Using top quote updates instead of full order book
}

void TradeImpactMM::LogDebug(const std::string& message)
{
    if (debug_) {
        logger().LogToClient(LOGLEVEL_DEBUG, message);
    }
}

void TradeImpactMM::DefineStrategyCommands()
{
    commands().CreateCommand("flatten", "Flatten all positions");
    commands().CreateCommand("cancel_all", "Cancel all open orders");
    commands().CreateCommand("reset", "Reset strategy state");
}

void TradeImpactMM::OnStrategyCommand(const String& command)
{
    try {
        if (command == "flatten") {
            for (auto& pair : instrument_states_) {
                const Instrument* instrument = pair.first;
                double position = portfolio().position(instrument);
                if (position != 0) {
                    OrderParams params(*instrument,
                                    abs(position),
                                    0.0,  // Market order
                                    MARKET_CENTER_ID_IEX,
                                    position > 0 ? ORDER_SIDE_SELL : ORDER_SIDE_BUY,
                                    ORDER_TIF_DAY,
                                    ORDER_TYPE_MARKET);
                    trade_actions()->SendNewOrder(params);
                }
            }
            LogDebug("Flattening all positions");
        }
        else if (command == "cancel_all") {
            for (auto& pair : instrument_states_) {
                CancelAllOrders(pair.first);
            }
            LogDebug("Cancelling all orders");
        }
        else if (command == "reset") {
            OnResetStrategyState();
            LogDebug("Strategy state reset");
        }
    } catch (const std::exception& e) {
        logger().LogToClient(LOGLEVEL_ERROR, 
            std::string("Error executing command: ") + e.what());
    }
}

void TradeImpactMM::OnStartOfDay(const StartOfDayEventMsg& msg)
{
    try {
        // Reset daily tracking
        for (auto& pair : instrument_states_) {
            pair.second.active_orders.clear();
            pair.second.current_bid = 0;
            pair.second.current_ask = 0;
        }
        LogDebug("Start of day initialization complete");
    } catch (const std::exception& e) {
        logger().LogToClient(LOGLEVEL_ERROR, 
            std::string("Error in start of day: ") + e.what());
    }
}

void TradeImpactMM::OnEndOfDay(const EndOfDayEventMsg& msg)
{
    try {
        // Cancel all open orders
        for (auto& pair : instrument_states_) {
            CancelAllOrders(pair.first);
        }
        LogDebug("End of day cleanup complete");
    } catch (const std::exception& e) {
        logger().LogToClient(LOGLEVEL_ERROR, 
            std::string("Error in end of day: ") + e.what());
    }
}

void TradeImpactMM::OnParamChanged(StrategyParam& param)
{
    if (param.param_name() == "impact_multiplier") {
        if (!param.Get(&impact_multiplier_))
            throw StrategyStudioException("Could not get impact_multiplier");
    }
    else if (param.param_name() == "rolling_window") {
        if (!param.Get(&rolling_window_))
            throw StrategyStudioException("Could not get rolling_window");
    }
    else if (param.param_name() == "quantile_threshold") {
        if (!param.Get(&quantile_threshold_))
            throw StrategyStudioException("Could not get quantile_threshold");
    }
    else if (param.param_name() == "levels_to_consider") {
        if (!param.Get(&levels_to_consider_))
            throw StrategyStudioException("Could not get levels_to_consider");
    }
    else if (param.param_name() == "tick_size") {
        if (!param.Get(&tick_size_))
            throw StrategyStudioException("Could not get tick_size");
    }
    else if (param.param_name() == "max_position") {
        if (!param.Get(&max_position_))
            throw StrategyStudioException("Could not get max_position");
    }
    else if (param.param_name() == "risk_limit_pct") {
        if (!param.Get(&risk_limit_pct_))
            throw StrategyStudioException("Could not get risk_limit_pct");
    }
    else if (param.param_name() == "min_spread_ticks") {
        if (!param.Get(&min_spread_ticks_))
            throw StrategyStudioException("Could not get min_spread_ticks");
    }
    else if (param.param_name() == "max_spread_ticks") {
        if (!param.Get(&max_spread_ticks_))
            throw StrategyStudioException("Could not get max_spread_ticks");
    }
    else if (param.param_name() == "quote_size") {
        if (!param.Get(&quote_size_))
            throw StrategyStudioException("Could not get quote_size");
    }
    else if (param.param_name() == "min_quote_size") {
        if (!param.Get(&min_quote_size_))
            throw StrategyStudioException("Could not get min_quote_size");
    }
    else if (param.param_name() == "max_quote_size") {
        if (!param.Get(&max_quote_size_))
            throw StrategyStudioException("Could not get max_quote_size");
    }
    else if (param.param_name() == "debug") {
        if (!param.Get(&debug_))
            throw StrategyStudioException("Could not get debug");
    }
}