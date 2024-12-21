#ifdef _WIN32
   #include "stdafx.h"
#endif

#include "StopLossLiquidityTaking.h"
#include <Utilities/Cast.h>
#include <Utilities/utils.h>

#include <math.h>
#include <iostream>
#include <sstream>
#include <cassert>

using namespace RCM::StrategyStudio;
using namespace RCM::StrategyStudio::MarketModels;
using namespace RCM::StrategyStudio::Utilities;
using namespace std;

StopLossHunter::StopLossHunter(StrategyID strategyID, const std::string& strategyName, const std::string& groupName):
   Strategy(strategyID, strategyName, groupName),
   entry_range_ticks_(3),         
   target_ticks_(5),           
   max_loss_ticks_(3),         
   lookback_period_(1000),       
   volatility_period_(20),      
   volatility_threshold_(0.0001),
   account_risk_per_trade_(0.001), // 0.1% risk per trade
   debug_(true)
{
}

StopLossHunter::~StopLossHunter()
{
}

void StopLossHunter::OnResetStrategyState()
{
   instrument_states_.clear();
   price_windows_.clear();
   volatility_windows_.clear();
}

void StopLossHunter::DefineStrategyParams()
{
   params().CreateParam(CreateStrategyParamArgs("entry_range_ticks", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, entry_range_ticks_));
   params().CreateParam(CreateStrategyParamArgs("target_ticks", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, target_ticks_));
   params().CreateParam(CreateStrategyParamArgs("max_loss_ticks", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, max_loss_ticks_));
   params().CreateParam(CreateStrategyParamArgs("lookback_period", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, lookback_period_));
   params().CreateParam(CreateStrategyParamArgs("volatility_period", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, volatility_period_));
   params().CreateParam(CreateStrategyParamArgs("volatility_threshold", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, volatility_threshold_));
   params().CreateParam(CreateStrategyParamArgs("account_risk_per_trade", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, account_risk_per_trade_));
   params().CreateParam(CreateStrategyParamArgs("debug", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_BOOL, debug_));
}

void StopLossHunter::RegisterForStrategyEvents(StrategyEventRegister* eventRegister, DateType currDate)
{
    for (SymbolSetConstIter it = symbols_begin(); it != symbols_end(); ++it) {
        eventRegister->RegisterForMarketData(*it);
    }

    // Initialize state for each instrument
    for (InstrumentSetConstIter it = instrument_begin(); it != instrument_end(); ++it) {
        const Instrument* instrument = it->second;
        
        instrument_states_.emplace(instrument, InstrumentState());
        
        // Correctly specify both key and value construction tuples

        // price_windows[instrument] = ScalarRollingWindow()

        price_windows_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(instrument),
            std::forward_as_tuple(lookback_period_)  // Constructor argument for ScalarRollingWindow
        );
        
        volatility_windows_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(instrument),
            std::forward_as_tuple(volatility_period_)  // Constructor argument for ScalarRollingWindow
        );
    }
}


void StopLossHunter::OnTrade(const TradeDataEventMsg& msg)
{
   const Instrument* instrument = &msg.instrument();
   double price = msg.trade().price();
  
   UpdateHighLow(instrument, price);
  
   auto& state = instrument_states_[instrument];
  
   switch(state.status) {
       case InstrumentState::IDLE:
       {
           // Look For entries
           bool is_near_high;
           if (IsNearSignificantLevel(instrument, price, is_near_high)) {
               // state.status = InstrumentState::HUNTING;
               ProcessPotentialEntry(instrument, price);
           }
           break;
       }
       case InstrumentState::HUNTING:
           // Already hunting - handled by ProcessPotentialEntry in previous state
           break;
          
       case InstrumentState::IN_POSITION:
           ManagePosition(instrument, price);
           break;
          
       case InstrumentState::EXITING:
       {
           if (portfolio().position(instrument) == 0) {
               state.status = InstrumentState::IDLE;
               state.position_side = 0;
               state.entry_price = 0;
           }
           break;
       }
   }
}

void StopLossHunter::UpdateHighLow(const Instrument* instrument, double price)
{
   auto& price_window = price_windows_[instrument];
   price_window.push_back(price);
  
   if (!price_window.full()) {
       return;
   }

   auto& state = instrument_states_[instrument];
  
   state.last_high = *(std::max_element(price_window.begin(), price_window.end()));
   state.last_low = *(std::min_element(price_window.begin(), price_window.end()));
  
   // if (debug_) {
   //     std::stringstream ss;
   //     ss << "Updated levels for " << instrument->symbol()
   //        << " High: " << state.last_high
   //        << " Low: " << state.last_low;
   //     logger().LogToClient(LOGLEVEL_DEBUG, ss.str());
   // }
  
}

bool StopLossHunter::IsNearSignificantLevel(const Instrument* instrument, double price, bool& is_near_high)
{
   const auto& state = instrument_states_[instrument];
   double tick_size = instrument->min_tick_size();
  
   double high_distance = fabs(price - state.last_high);
   double low_distance = fabs(price - state.last_low);
  
   if (high_distance <= entry_range_ticks_ * tick_size) {
       is_near_high = true;
       return true;
   } else if (low_distance <= entry_range_ticks_ * tick_size) {
       is_near_high = false; // Is near low = True
       return true;
   }
  
   return false;
}

bool StopLossHunter::IsSafeToTrade(const Instrument* instrument)
{
   // Check if we have valid quote
   const auto& quote = instrument->top_quote();
   if (!quote.ask_side().IsValid() || !quote.bid_side().IsValid()) {
       return false;
   }
  
   // Check volatility
   double vol = CalculateVolatility(instrument);
   if (vol < volatility_threshold_) {
       // It means that the price is revolving around the region and we might not have good momentum to break the high/low
       return false;
   }
  
   return true;
}

double StopLossHunter::CalculateVolatility(const Instrument* instrument)
{
   auto& vol_window = volatility_windows_[instrument];
   if (!vol_window.full()) {
       return 0.0;
   }
  
   return vol_window.StdDev();
}

void StopLossHunter::ProcessPotentialEntry(const Instrument* instrument, double price)
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
  
   state.status = InstrumentState::HUNTING;
  
   // Calculate position size based on risk
   double risk_amount = portfolio().cash_balance() * account_risk_per_trade_;
   double risk_per_share = max_loss_ticks_ * instrument->min_tick_size();
   int position_size = static_cast<int>(risk_amount / risk_per_share);
  
   // Enter long near high, short near low
   if (is_near_high) {
       SendOrder(instrument, true, 1);  // Buy at market when near high
       state.position_side = 1;
   } else {
       SendOrder(instrument, false, 1); // Sell at market when near low
       state.position_side = -1;
   }
  
   // state.entry_price = price;
   // state.entry_time = GetCurrentTime();
   // state.status = InstrumentState::IN_POSITION;
}

void StopLossHunter::SendOrder(const Instrument* instrument, bool is_buy, int quantity)
{
   if (quantity <= 0) return;

   OrderParams params(*instrument,
                     quantity,
                     0.0,  // Price is ignored for market orders
                     MARKET_CENTER_ID_IEX,
                     is_buy ? ORDER_SIDE_BUY : ORDER_SIDE_SELL,
                     ORDER_TIF_DAY,
                     ORDER_TYPE_MARKET);

   if (debug_) {
       cout << "Sending Market " << (is_buy ? "Buy" : "Sell")
          << " order for " << instrument->symbol()
          << " Qty: " << quantity << endl;
   }

   trade_actions()->SendNewOrder(params);
}

void StopLossHunter::ManagePosition(const Instrument* instrument, double price)
{
   auto& state = instrument_states_[instrument];
   double tick_size = 0.01; 
  
   // Calculate profit in ticks
   double profit_ticks = state.position_side * (price - state.entry_price) / tick_size;
  
   if (profit_ticks >= target_ticks_ || profit_ticks <= -max_loss_ticks_) {
       state.status = InstrumentState::EXITING;
      
       // Exit position
       int current_position = portfolio().position(instrument);
       if (current_position != 0) {
           SendOrder(instrument, current_position < 0, abs(current_position));
       }
   }
}

void StopLossHunter::OnOrderUpdate(const OrderUpdateEventMsg& msg) {
  if (debug_) {
      std::stringstream ss;
      ss << "Order Update: " << msg.order().instrument()->symbol()
         << " Status: " << msg.order().order_state();
      logger().LogToClient(LOGLEVEL_DEBUG, ss.str());
  }

  if (msg.update_type() == ORDER_UPDATE_TYPE_FILL) {
      auto& state = instrument_states_[msg.order().instrument()];

      if (state.status == InstrumentState::HUNTING) {
          // We have successfully filled the entry orders
          state.status = InstrumentState::IN_POSITION;
          state.entry_price = msg.fill()->fill_price();
          state.entry_time = msg.event_time();

          if (debug_) {
              cout << "Entry filled for " << msg.order().instrument()->symbol()
                 << " at price: " << state.entry_price << endl;
          }
      }

      if (state.status == InstrumentState::EXITING) {
          state.status = InstrumentState::IDLE;
          state.position_side = 0;
          state.entry_price = 0;
          state.entry_time = boost::posix_time::not_a_date_time;

          if (debug_) {
              cout  << "Exit complete for " << msg.order().instrument()->symbol() << endl;
          }
      }
  }
}

void StopLossHunter::OnTopQuote(const QuoteEventMsg& msg)
{
   // Update volatility using mid price
   auto& vol_window = volatility_windows_[&msg.instrument()];
   double mid_price = (msg.quote().ask() + msg.quote().bid()) / 2.0;
   vol_window.push_back(mid_price);
}

void StopLossHunter::OnBar(const BarEventMsg& msg)
{
   // Not using bars for this strategy
}

void StopLossHunter::DefineStrategyCommands()
{
   // No custom commands needed for now
}

void StopLossHunter::OnParamChanged(StrategyParam& param)
{
   if (param.param_name() == "entry_range_ticks") {
       if (!param.Get(&entry_range_ticks_))
           throw StrategyStudioException("Could not get entry_range_ticks");
   } else if (param.param_name() == "target_ticks") {
       if (!param.Get(&target_ticks_))
           throw StrategyStudioException("Could not get target_ticks");
   } else if (param.param_name() == "max_loss_ticks") {
       if (!param.Get(&max_loss_ticks_))
           throw StrategyStudioException("Could not get max_loss_ticks");
   } else if (param.param_name() == "lookback_period") {
       if (!param.Get(&lookback_period_))
           throw StrategyStudioException("Could not get lookback_period");
   } else if (param.param_name() == "volatility_period") {
       if (!param.Get(&volatility_period_))
           throw StrategyStudioException("Could not get volatility_period");
   } else if (param.param_name() == "volatility_threshold") {
       if (!param.Get(&volatility_threshold_))
           throw StrategyStudioException("Could not get volatility_threshold");
   } else if (param.param_name() == "account_risk_per_trade") {
       if (!param.Get(&account_risk_per_trade_))
           throw StrategyStudioException("Could not get account_risk_per_trade");
   } else if (param.param_name() == "debug") {
       if (!param.Get(&debug_))
           throw StrategyStudioException("Could not get debug");
   }
}





