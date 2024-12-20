# Market Making and Liquidity Taking Strategies

This README provides a comprehensive overview of two algorithmic trading strategies—**Market Making** and **Liquidity Taking (StopLossHunter)**—including their objectives, methodologies, and mathematical underpinnings.

[[_TOC_]]

## Team

**Yadvesh Yadav**

Email: [yyada@illinois.edu](mailto:yyada@illinois.edu)  
LinkedIn: [https://www.linkedin.com/in/yadvesh/](https://www.linkedin.com/in/yadvesh/)

As a Master's student in Financial Mathematics at the University of Illinois Urbana-Champaign, with a background as a Data Science Engineer, I specialize in financial data analysis, predictive modeling, and algorithmic trading. My passion lies in leveraging mathematical and computational techniques to develop innovative, quantitative trading strategies and solutions.

---

# Project Summary

## Objective

The primary objective of this project is to implement and evaluate two distinct algorithmic trading strategies—Market Making and Liquidity Taking—within the Strategy Studio environment. These strategies operate directly on real-time market data, executing trades and adjusting parameters based on evolving market conditions. The goal is to establish robust frameworks that can profit from market microstructure properties, liquidity patterns, and price dynamics.

## Strategies Overview

1. **Market Making Strategy:**
   *Aim:* Continuously provide liquidity to the market by placing both bid and ask orders around a theoretical fair price. The strategy attempts to earn profits from the spread and maintain balanced inventory.

2. **Liquidity Taking (StopLossHunter) Strategy:**
   *Aim:* Identify critical price levels (recent highs and lows) and enter positions when the price approaches these levels. The strategy seeks to capture potential breakouts or mean reversions with well-defined profit targets and stop-losses.

---

# Methodology

## Data Inputs and Platform

The strategies are developed using Strategy Studio, an event-driven trading framework that processes real-time quotes, trades, and order book events. By reacting to top-of-book quotes and trade messages, the strategies dynamically adjust their orders, sizes, and price levels.

## Market Making Strategy Methodology

1. **Trade Impact Computation:**
   The strategy monitors incoming trades and calculates their impact on the aggregated bid/ask liquidity. For a trade of size \( T \):
   \[
   \text{impact} = \text{impact_multiplier} \times \frac{T}{(\text{total_bid_size} + \text{total_ask_size})} \times 
   \begin{cases}
   +1 & \text{if buy trade}\\
   -1 & \text{if sell trade}
   \end{cases}
   \]

2. **Rolling Window & Quantiles:**
   A rolling window of past trade impacts is maintained. The strategy computes quantiles (e.g., 10th percentile) to determine how to adjust theoretical bid and ask quotes. Let \(I_{b,q}\) be the buy-impact quantile and \(I_{s,q}\) the sell-impact quantile.

3. **Theoretical Prices & Spreads:**
   Given the best bid and best ask, the mid-price is:
   \[
   \text{mid_price} = \frac{\text{best_ask} + \text{best_bid}}{2}.
   \]

   The strategy calculates theoretical bid/ask levels:
   \[
   \text{theo_bid} = \text{mid_price} - I_{s,q} - (\text{position_factor} \times \text{mid_price})
   \]
   \[
   \text{theo_ask} = \text{mid_price} + I_{b,q} - (\text{position_factor} \times \text{mid_price})
   \]

   Constraints ensure that the spread remains within set bounds:
   \[
   \text{min_spread_ticks} \times \text{tick_size} \leq (\text{theo_ask} - \text{theo_bid}) \leq \text{max_spread_ticks} \times \text{tick_size}.
   \]

4. **Inventory Control & Quote Sizes:**
   The size of each quote (order) is adjusted based on current inventory. As position size grows, the strategy modifies quote sizes and/or price levels to reduce directional exposure.

## Liquidity Taking (StopLossHunter) Strategy Methodology

1. **Identifying Key Levels:**
   A rolling window of prices is kept to find:
   \[
   \text{last_high} = \max\{p_1, \ldots, p_N\}, \quad \text{last_low} = \min\{p_1, \ldots, p_N\}.
   \]

   If the current price \( p \) is within a certain tick range of \(\text{last_high}\) or \(\text{last_low}\), the strategy considers taking a position.

2. **Volatility Check:**
   The strategy computes volatility as the standard deviation of recent mid-prices:
   \[
   \sigma = \sqrt{\frac{1}{M-1}\sum_{j=1}^{M}(p_j - \bar{p})^2}.
   \]

   If \(\sigma < \text{volatility_threshold}\), no trade is initiated due to lack of meaningful movement.

3. **Entry & Exit:**
   Upon a valid setup:
   - If near high, the strategy might go long; if near low, it might go short.
   
   After entry at \( p_{entry} \), a profit target and stop-loss are set:
   \[
   \text{take_profit} = p_{entry} \pm (\text{target_ticks} \times \text{tick_size}),
   \]
   \[
   \text{stop_loss} = p_{entry} \pm (\text{max_loss_ticks} \times \text{tick_size}),
   \]
   with the direction depending on position (long or short).

4. **Risk Management via Position Sizing:**
   The position size is determined by the risk per trade:
   \[
   \text{position_size} = \frac{\text{account_risk_per_trade} \times \text{cash_balance}}{\text{max_loss_ticks} \times \text{tick_size}}.
   \]

---

# Approach & Implementation

Both strategies are implemented using an event-driven approach, reacting to data provided by Strategy Studio:

- **OnTrade Event:** Processes trades, updates rolling windows, calculates impacts, and triggers quote or position adjustments.
- **OnTopQuote Event:** Updates volatility calculations and provides the latest best bid/ask for recalculating theoretical prices or validating positions.
- **Order Management:** Each strategy manages its orders by sending limit/market orders, cancelling stale quotes, and flattening positions when necessary.

The provided C++ code implementations integrate these logic flows, allowing for easy backtesting and tuning of parameters.

---

# Results and Discussion

These strategies serve as illustrative frameworks:

- **Market Making:** 
  - Generates consistent, small gains from the spread under stable market conditions.
  - Risk is managed by adjusting quote levels and sizes based on inventory and liquidity conditions.

- **Liquidity Taking (StopLossHunter):** 
  - Attempts to capture larger directional moves when volatility and price action near key levels are favorable.
  - Uses strict risk controls (stop-losses and profit targets) to manage downside risk.

While the raw profitability and stability depend on market conditions and parameter tuning, both strategies demonstrate how mathematical models, rolling statistics, and market structure insights can guide algorithmic decision-making in real-time.

---

# Future Steps

- **Parameter Optimization:** Use backtesting and statistical methods to refine parameters, e.g., quantile thresholds, volatility filters, and position sizing.
- **Machine Learning Integration:** Incorporate predictive signals (from machine learning models) to enhance entry/exit decisions, further improving the risk-adjusted returns.
- **Enhanced Risk Management:** Implement more sophisticated position and order book analytics to adaptively adjust risk and capture more complex market scenarios.

---

# Code

Below are references to the code for both strategies, which can be further adapted and integrated into a trading framework:

**Market Making Strategy (TradeImpactMM):**
```cpp
/* Refer to TradeImpactMM C++ code implementation provided previously */
