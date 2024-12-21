#ifdef _WIN32
   #include "stdafx.h"
#endif

#include "StopLossLiquidityTakingV2.h"
#include <Utilities/Cast.h>
#include <Utilities/utils.h>

#include <math.h>
#include <iostream>
#include <sstream>
#include <cassert>
#include <numeric>

using namespace RCM::StrategyStudio;
using namespace RCM::StrategyStudio::MarketModels;
using namespace RCM::StrategyStudio::Utilities;
using namespace std;

StopLossHunterV2::StopLossHunterV2(StrategyID strategyID, const std::string& strategyName, const std::string& groupName):
   Strategy(strategyID, strategyName, groupName),
   entry_range_ticks_(3),         
   target_ticks_(1),           
   tick_lookback_(11),
   momentum_threshold_(0),
   max_hold_seconds_(15),
   account_risk_per_trade_(0.001),
   debug_(true),
   current_strategy_time_(boost::posix_time::not_a_date_time)  // Initialize time
{
}

StopLossHunterV2::~StopLossHunterV2()
{
}

void StopLossHunterV2::OnResetStrategyState()
{
   instrument_states_.clear();
}

void StopLossHunterV2::DefineStrategyParams()
{
   params().CreateParam(CreateStrategyParamArgs("entry_range_ticks", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, entry_range_ticks_));
   params().CreateParam(CreateStrategyParamArgs("target_ticks", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, target_ticks_));
   params().CreateParam(CreateStrategyParamArgs("tick_lookback", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, tick_lookback_));
   params().CreateParam(CreateStrategyParamArgs("momentum_threshold", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, momentum_threshold_));
   params().CreateParam(CreateStrategyParamArgs("max_hold_seconds", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, max_hold_seconds_));
   params().CreateParam(CreateStrategyParamArgs("account_risk_per_trade", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, account_risk_per_trade_));
   params().CreateParam(CreateStrategyParamArgs("debug", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_BOOL, debug_));
}

void StopLossHunterV2::RegisterForStrategyEvents(StrategyEventRegister* eventRegister, DateType currDate)
{
    for (SymbolSetConstIter it = symbols_begin(); it != symbols_end(); ++it) {
        eventRegister->RegisterForMarketData(*it);
        eventRegister->RegisterForBars(*it, BAR_TYPE_TIME, 3600);
    }

    for (InstrumentSetConstIter it = instrument_begin(); it != instrument_end(); ++it) {
        const Instrument* instrument = it->second;
        instrument_states_.emplace(instrument, InstrumentState());
    }
}

void StopLossHunterV2::OnTrade(const TradeDataEventMsg& msg)
{
    current_strategy_time_ = msg.adapter_time();

   const Instrument* instrument = &msg.instrument();
   double price = msg.trade().price();
   
   UpdateTickMomentum(instrument, price);
   
   auto& state = instrument_states_[instrument];
  
   switch(state.status) {
       case InstrumentState::IDLE:
       {
           bool is_near_high;
           if (IsNearSignificantLevel(instrument, price, is_near_high)) {
               ProcessPotentialEntry(instrument, price);
           }
           break;
       }
       case InstrumentState::HUNTING:
           break;
          
       case InstrumentState::IN_POSITION:
       {
           CheckTimeBasedExit(instrument);
           break;
       }
       case InstrumentState::EXITING:
           break;
       
       case InstrumentState::NO_TRADE:
           break;
   }
}

void StopLossHunterV2::OnBar(const BarEventMsg& msg)
{
    if (msg.type() != BAR_TYPE_TIME || msg.interval() != 3600) {
        return;
    }

    const Instrument* instrument = &msg.instrument();
    auto& state = instrument_states_[instrument];

    // New hour bar - reset to IDLE state if we were in NO_TRADE
    if (state.status == InstrumentState::NO_TRADE) {
        state.status = InstrumentState::IDLE;
    }

    state.hourly_high = msg.bar().high();
    state.hourly_low = msg.bar().low();
    state.last_bar_time = msg.event_time();

    cout << "Updated hourly levels for " << instrument->symbol()
           << " High: " << state.hourly_high
           << " Low: " << state.hourly_low
           << " Time: " << state.last_bar_time << endl
           << " Status: " << state.status << endl;    
}

bool StopLossHunterV2::IsNearSignificantLevel(const Instrument* instrument, double price, bool& is_near_high)
{
    const auto& state = instrument_states_[instrument];
    
    // Check if we have at least one completed bar
    if (state.last_bar_time == boost::posix_time::not_a_date_time) {
        return false;
    }

    double tick_size = instrument->min_tick_size();
    
    // Only proceed if we have valid hourly levels
    if (state.hourly_high <= 0 || state.hourly_low >= std::numeric_limits<double>::max()) {
        return false;
    }

    double high_distance = fabs(price - state.hourly_high);
    double low_distance = fabs(price - state.hourly_low);

    if (high_distance <= entry_range_ticks_ * tick_size) {
        is_near_high = true;
        return true;
    } else if (low_distance <= entry_range_ticks_ * tick_size) {
        is_near_high = false;
        return true;
    }

    return false;
}

void StopLossHunterV2::UpdateTickMomentum(const Instrument* instrument, double price)
{
    auto& state = instrument_states_[instrument];
    
    if (state.last_tick_price == 0) {
        state.last_tick_price = price;
        return;
    }
    
    int direction = 0;
    if (price > state.last_tick_price) {
        direction = 1;
    } else if (price < state.last_tick_price) {
        direction = -1;
    }
    
    state.tick_directions.push_back(direction);
    if (state.tick_directions.size() > tick_lookback_) {
        state.tick_directions.pop_front();
    }
    
    state.last_tick_price = price;
}

int StopLossHunterV2::GetTickMomentumSignal(const Instrument* instrument)
{
    const auto& state = instrument_states_[instrument];
    
    if (state.tick_directions.size() < tick_lookback_) {
        return 0;
    }
    
    int sum = std::accumulate(state.tick_directions.begin(), state.tick_directions.end(), 0);
    
    return sum;
}

bool StopLossHunterV2::IsSafeToTrade(const Instrument* instrument)
{
   const auto& quote = instrument->top_quote();
   if (!quote.ask_side().IsValid() || !quote.bid_side().IsValid()) {
       return false;
   }
   
   int momentum = GetTickMomentumSignal(instrument);
   if (momentum == 0) {
       return false;
   }
   
   return true;
}

void StopLossHunterV2::ProcessPotentialEntry(const Instrument* instrument, double price)
{
   auto& state = instrument_states_[instrument];
  
   if (!IsSafeToTrade(instrument)) {
       return;
   }
  
   bool is_near_high;
   if (!IsNearSignificantLevel(instrument, price, is_near_high)) {
       state.status = InstrumentState::IDLE;
       return;
   }
  
   int momentum = GetTickMomentumSignal(instrument);
   if ((is_near_high && momentum < momentum_threshold_) || (!is_near_high && momentum > -momentum_threshold_)) {
       return;
   }
  
   state.status = InstrumentState::HUNTING;
  
//    double risk_amount = portfolio().cash_balance() * account_risk_per_trade_;
//    double risk_per_share = target_ticks_ * instrument->min_tick_size();  // Using target ticks as risk
//    int position_size = static_cast<int>(risk_amount / risk_per_share);
  
    int position_size = 1; // For trial purposes

    cout << "Order Generated for " << instrument->symbol() << endl
         << "Parameters: Current Price(LTP):" << price << " Current High/Low: " << state.hourly_high << "/" << state.hourly_low << endl
         << "Momentum of the past " << tick_lookback_ << " ticks: " << momentum << " Min_Tick_Size for the symbol: " << instrument->min_tick_size() << endl;

   if (is_near_high) {
       SendMarketOrder(instrument, true, position_size);
       state.position_side = 1;
   } else {
       SendMarketOrder(instrument, false, position_size);
       state.position_side = -1;
   }
}

void StopLossHunterV2::SendMarketOrder(const Instrument* instrument, bool is_buy, int quantity)
{
   if (quantity <= 0) return;

   OrderParams params(*instrument,
                     quantity,
                     0.0,
                     MARKET_CENTER_ID_IEX,
                     is_buy ? ORDER_SIDE_BUY : ORDER_SIDE_SELL,
                     ORDER_TIF_DAY,
                     ORDER_TYPE_MARKET);

   cout << "Sending Market " << (is_buy ? "Buy" : "Sell")
          << " order for " << instrument->symbol()
          << " Qty: " << quantity << endl;

   trade_actions()->SendNewOrder(params);
}

void StopLossHunterV2::SendLimitOrder(const Instrument* instrument, bool is_buy, int quantity, double price)
{
   if (quantity < 0) return;

   OrderParams params(*instrument,
                     quantity,
                     price,
                     MARKET_CENTER_ID_IEX,
                     is_buy ? ORDER_SIDE_BUY : ORDER_SIDE_SELL,
                     ORDER_TIF_DAY,
                     ORDER_TYPE_LIMIT);

   cout << "Sending Limit " << (is_buy ? "Buy" : "Sell")
          << " order for " << instrument->symbol()
          << " Qty: " << quantity
          << " Price: " << price << endl;

   trade_actions()->SendNewOrder(params);
}

void StopLossHunterV2::CheckTimeBasedExit(const Instrument* instrument)
{
    auto& state = instrument_states_[instrument];
    
    if (state.entry_time == boost::posix_time::not_a_date_time) {
        return;
    }
    
    TimeType current_time = current_strategy_time_;
    if (current_time - state.entry_time > boost::posix_time::seconds(max_hold_seconds_)) {
        cout << "Exitting position for " << instrument->symbol() << " at time " << current_time << endl
             << "Reason for exit: Time based exit triggered" << endl;
        ExitPosition(instrument);
    }
}

void StopLossHunterV2::ExitPosition(const Instrument* instrument)
{
    auto& state = instrument_states_[instrument];
    
    state.status = InstrumentState::EXITING;

    if (state.limit_order_id != 0) {
        trade_actions()->SendCancelOrder(state.limit_order_id); // Canceling the limit orders
    }
    
    double current_position = portfolio().position(instrument);
    SendMarketOrder(instrument, current_position < 0, abs(current_position)); // Liquidating the position
    return;
}

void StopLossHunterV2::OnOrderUpdate(const OrderUpdateEventMsg& msg) {
    auto& state = instrument_states_[msg.order().instrument()];

    if(msg.update_type() == ORDER_UPDATE_TYPE_OPEN){

        cout << "Order Opened for " << msg.order().instrument()->symbol() << " at time: " << msg.event_time() << endl;

        if(msg.order().order_type() == ORDER_TYPE_MARKET){
            state.market_order_id = msg.order_id();
            cout << "Type: MARKET" << endl;
            cout << "OrderID: [" << state.market_order_id << "]" << endl;
        }else{
            state.limit_order_id = msg.order_id();
            cout << "Type: LIMIT" << endl;
            cout << "OrderID: [" << state.limit_order_id << "]" << endl;
        }
        return;
    }

    if(state.status == InstrumentState::HUNTING){
        // We have sent entry orders
        if (msg.update_type() == ORDER_UPDATE_TYPE_FILL || msg.update_type() == ORDER_UPDATE_TYPE_PARTIAL_FILL) {
            // Market order fill
            if (msg.order().order_id() == state.market_order_id) {
                state.status = InstrumentState::IN_POSITION;
                state.entry_price = msg.fill()->fill_price();
                state.entry_time = msg.event_time();
                
                // Calculate and send limit order for profit target
                double target_price = state.entry_price + 
                    (state.position_side * target_ticks_ * msg.order().instrument()->min_tick_size());
                
                cout << "Entry filled for " << msg.order().instrument()->symbol() << " quantity: " << msg.fill()->fill_size()
                    << " at price: " << state.entry_price 
                    << " target: " << target_price << endl
                    << " time: " << msg.update_time() << endl;

                SendLimitOrder(msg.order().instrument(), 
                            state.position_side < 0,  // Buy to cover if short
                            abs(msg.fill()->fill_size()),
                            target_price);
            }
            // Limit order fill
            else if (msg.order().order_id() == state.limit_order_id) {
                cout << "Target reached for " << msg.order().instrument()->symbol() 
                    << " at price: " << msg.fill()->fill_price() 
                    << "at time: " << msg.update_time() << endl
                    << "Profit: " << (msg.fill()->fill_size() * abs(msg.fill()->fill_price() - state.entry_price)) << endl;

                state.status = InstrumentState::NO_TRADE; // We will change this to IDLE when a new high/low is formed
                state.position_side = 0;
                state.entry_price = 0;
                state.entry_time = boost::posix_time::not_a_date_time;
                state.market_order_id = 0;
                state.limit_order_id = 0;
            }
        }
    }else if(state.status == InstrumentState::EXITING){
        if(msg.update_type() == ORDER_UPDATE_TYPE_FILL || msg.update_type() == ORDER_UPDATE_TYPE_PARTIAL_FILL){
            state.status = InstrumentState::NO_TRADE;
            cout << "Closed Position for " << msg.order().instrument()->symbol() << " at time: " << msg.event_time() << endl
                << "Current Status of the symbol: NO_TRADE" << endl
                << "PNL: " << (msg.fill()->fill_size()) * (state.entry_price - msg.fill()->fill_price()) << endl;
            state.position_side = 0;
            state.entry_price = 0;
            state.entry_time = boost::posix_time::not_a_date_time;
            state.market_order_id = 0;
            state.limit_order_id = 0;
        }
    } 
}

void StopLossHunterV2::OnTopQuote(const QuoteEventMsg& msg)
{
    // Not needed in V2
}

void StopLossHunterV2::DefineStrategyCommands()
{
    // No custom commands needed
}

void StopLossHunterV2::OnParamChanged(StrategyParam& param)
{
   if (param.param_name() == "entry_range_ticks") {
       if (!param.Get(&entry_range_ticks_))
           throw StrategyStudioException("Could not get entry_range_ticks");
   } else if (param.param_name() == "target_ticks") {
       if (!param.Get(&target_ticks_))
           throw StrategyStudioException("Could not get target_ticks");
   } else if (param.param_name() == "tick_lookback") {
       if (!param.Get(&tick_lookback_))
           throw StrategyStudioException("Could not get tick_lookback");
   } else if (param.param_name() == "momentum_threshold") {
       if (!param.Get(&momentum_threshold_))
           throw StrategyStudioException("Could not get momentum_threshold");
   } else if (param.param_name() == "max_hold_seconds") {
       if (!param.Get(&max_hold_seconds_))
           throw StrategyStudioException("Could not get max_hold_seconds");
   } else if (param.param_name() == "account_risk_per_trade") {
       if (!param.Get(&account_risk_per_trade_))
           throw StrategyStudioException("Could not get account_risk_per_trade");
   } else if (param.param_name() == "debug") {
       if (!param.Get(&debug_))
           throw StrategyStudioException("Could not get debug");
   }
} 