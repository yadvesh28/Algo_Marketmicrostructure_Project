import pandas as pd
import numpy as np
from typing import List, Dict, Tuple
from datetime import datetime, timedelta
from dataclasses import dataclass
from enum import Enum
import logging

# Set up logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

class Side(Enum):
    BUY = 1
    SELL = -1

@dataclass
class Position:
    size: float = 0
    avg_price: float = 0
    unrealized_pnl: float = 0
    realized_pnl: float = 0

class BacktestEngine:
    def __init__(self, 
                 impact_multiplier: float = 2.5,
                 rolling_window: int = 50,
                 quantile_threshold: float = 0.1,
                 max_position: float = 100,
                 risk_limit_pct: float = 0.02,
                 tick_size: float = 0.05,
                 transaction_cost: float = 0.001):
        
        self.impact_multiplier = impact_multiplier
        self.rolling_window = rolling_window
        self.quantile_threshold = quantile_threshold
        self.max_position = max_position
        self.risk_limit_pct = risk_limit_pct
        self.tick_size = tick_size
        self.transaction_cost = transaction_cost
        
        # Initialize state
        self.position = Position()
        self.quotes: Dict[str, float] = {}
        self.trade_history: List[dict] = []
        self.trade_impacts: List[float] = []
        self.performance_metrics: Dict[str, List[float]] = {
            'timestamp': [],
            'position': [],
            'unrealized_pnl': [],
            'realized_pnl': [],
            'total_pnl': [],
            'bid_quote': [],
            'ask_quote': [],
            'mid_price': [],
            'spread': []
        }

    def calculate_trade_impact(self, price: float, size: float, side: Side, 
                             book_levels: Dict[str, List[Tuple[float, float]]]) -> float:
        """Calculate trade impact based on order book liquidity."""
        total_bid_size = sum(size for _, size in book_levels['bids'][:4])
        total_ask_size = sum(size for _, size in book_levels['asks'][:4])
        
        impact = self.impact_multiplier * side.value * (
            size / (total_bid_size + total_ask_size)
        )
        return impact

    def update_position(self, trade_price: float, trade_size: float, side: Side):
        """Update position and P&L after a trade."""
        if side == Side.BUY:
            new_size = self.position.size + trade_size
        else:
            new_size = self.position.size - trade_size
            
        if self.position.size != 0:
            # Calculate realized P&L for partial closeouts
            if (self.position.size > 0 and side == Side.SELL) or \
               (self.position.size < 0 and side == Side.BUY):
                trade_pnl = (trade_price - self.position.avg_price) * min(abs(self.position.size), trade_size)
                if side == Side.SELL:
                    trade_pnl *= -1
                self.position.realized_pnl += trade_pnl - (trade_size * self.transaction_cost)
        
        # Update average price
        if new_size != 0:
            if self.position.size == 0:
                self.position.avg_price = trade_price
            else:
                self.position.avg_price = (
                    (self.position.avg_price * abs(self.position.size) + 
                     trade_price * trade_size) / (abs(self.position.size) + trade_size)
                )
        
        self.position.size = new_size

    def calculate_quotes(self, 
                        current_price: float, 
                        book_levels: Dict[str, List[Tuple[float, float]]]) -> Dict[str, float]:
        """Calculate bid and ask quotes based on trade impacts and position."""
        if len(self.trade_impacts) < self.rolling_window:
            return {}
            
        # Separate impacts by side
        buy_impacts = [imp for imp in self.trade_impacts if imp > 0]
        sell_impacts = [imp for imp in self.trade_impacts if imp < 0]
        
        if not buy_impacts or not sell_impacts:
            return {}
            
        # Calculate impact quantiles
        buy_quantile = np.quantile(buy_impacts, self.quantile_threshold)
        sell_quantile = abs(np.quantile(sell_impacts, 1 - self.quantile_threshold))
        
        # Position-based quote adjustment
        position_factor = (self.position.size / self.max_position) * self.risk_limit_pct
        
        # Calculate theoretical prices
        theo_bid = current_price - sell_quantile - (position_factor * current_price)
        theo_ask = current_price + buy_quantile - (position_factor * current_price)
        
        # Round to tick size
        bid_price = np.floor(theo_bid / self.tick_size) * self.tick_size
        ask_price = np.ceil(theo_ask / self.tick_size) * self.tick_size
        
        return {'bid': bid_price, 'ask': ask_price}

    def process_market_data(self, 
                          timestamp: datetime,
                          market_trade: Dict,
                          book_levels: Dict[str, List[Tuple[float, float]]]) -> Dict:
        """Process incoming market data and update strategy state."""
        # Calculate mid price
        mid_price = (book_levels['bids'][0][0] + book_levels['asks'][0][0]) / 2
        
        # Calculate trade impact
        impact = self.calculate_trade_impact(
            market_trade['price'],
            market_trade['size'],
            Side.BUY if market_trade['side'] == 'BUY' else Side.SELL,
            book_levels
        )
        
        # Store trade impact
        self.trade_impacts.append(impact)
        if len(self.trade_impacts) > self.rolling_window:
            self.trade_impacts.pop(0)
        
        # Calculate new quotes
        self.quotes = self.calculate_quotes(mid_price, book_levels)
        
        # Check for executions against our quotes
        executed_trades = []
        if self.quotes:
            if market_trade['price'] <= self.quotes['ask']:
                executed_trades.append({
                    'price': self.quotes['ask'],
                    'size': market_trade['size'],
                    'side': Side.SELL
                })
            elif market_trade['price'] >= self.quotes['bid']:
                executed_trades.append({
                    'price': self.quotes['bid'],
                    'size': market_trade['size'],
                    'side': Side.BUY
                })
        
        # Process executions
        for trade in executed_trades:
            self.update_position(trade['price'], trade['size'], trade['side'])
        
        # Update performance metrics
        self.update_metrics(timestamp, mid_price)
        
        return {
            'quotes': self.quotes,
            'executed_trades': executed_trades,
            'position': self.position,
            'metrics': self.get_current_metrics()
        }

    def update_metrics(self, timestamp: datetime, mid_price: float):
        """Update performance metrics."""
        # Calculate unrealized P&L
        self.position.unrealized_pnl = (
            self.position.size * (mid_price - self.position.avg_price)
        )
        
        # Store metrics
        self.performance_metrics['timestamp'].append(timestamp)
        self.performance_metrics['position'].append(self.position.size)
        self.performance_metrics['unrealized_pnl'].append(self.position.unrealized_pnl)
        self.performance_metrics['realized_pnl'].append(self.position.realized_pnl)
        self.performance_metrics['total_pnl'].append(
            self.position.unrealized_pnl + self.position.realized_pnl
        )
        self.performance_metrics['bid_quote'].append(
            self.quotes.get('bid', np.nan)
        )
        self.performance_metrics['ask_quote'].append(
            self.quotes.get('ask', np.nan)
        )
        self.performance_metrics['mid_price'].append(mid_price)
        self.performance_metrics['spread'].append(
            self.quotes.get('ask', np.nan) - self.quotes.get('bid', np.nan)
            if self.quotes.get('ask') and self.quotes.get('bid') else np.nan
        )

    def get_current_metrics(self) -> Dict:
        """Get current performance metrics."""
        return {
            'position': self.position.size,
            'unrealized_pnl': self.position.unrealized_pnl,
            'realized_pnl': self.position.realized_pnl,
            'total_pnl': self.position.unrealized_pnl + self.position.realized_pnl
        }

    def get_performance_summary(self) -> pd.DataFrame:
        """Get performance summary as DataFrame."""
        return pd.DataFrame(self.performance_metrics)

def run_backtest(market_data: pd.DataFrame, strategy_params: Dict) -> BacktestEngine:
    """Run backtest with market data and strategy parameters."""
    engine = BacktestEngine(**strategy_params)
    
    for _, row in market_data.iterrows():
        # Convert market data row to required format
        book_levels = {
            'bids': [(p, s) for p, s in zip(row['bid_prices'], row['bid_sizes'])],
            'asks': [(p, s) for p, s in zip(row['ask_prices'], row['ask_sizes'])]
        }
        
        market_trade = {
            'price': row['trade_price'],
            'size': row['trade_size'],
            'side': row['trade_side']
        }
        
        # Process market data
        engine.process_market_data(row.name, market_trade, book_levels)
    
    return engine

# Example usage
if __name__ == "__main__":
    # Sample strategy parameters
    strategy_params = {
        'impact_multiplier': 2.5,
        'rolling_window': 50,
        'quantile_threshold': 0.1,
        'max_position': 100,
        'risk_limit_pct': 0.02,
        'tick_size': 0.05,
        'transaction_cost': 0.001
    }
    
    # Generate sample market data
    dates = pd.date_range(start='2024-01-01', end='2024-01-02', freq='1min')
    np.random.seed(42)
    
    market_data = pd.DataFrame({
        'trade_price': 100 + np.random.normal(0, 0.1, len(dates)),
        'trade_size': np.random.uniform(1, 10, len(dates)),
        'trade_side': np.random.choice(['BUY', 'SELL'], len(dates)),
        'bid_prices': [
            [p - i*0.01 for i in range(4)]
            for p in (100 + np.random.normal(0, 0.1, len(dates)))
        ],
        'bid_sizes': [
            [10 + i*5 for i in range(4)]
            for _ in range(len(dates))
        ],
        'ask_prices': [
            [p + i*0.01 for i in range(4)]
            for p in (100 + np.random.normal(0, 0.1, len(dates)))
        ],
        'ask_sizes': [
            [10 + i*5 for i in range(4)]
            for _ in range(len(dates))
        ]
    }, index=dates)
    
    # Run backtest
    backtest_engine = run_backtest(market_data, strategy_params)
    
    # Get performance summary
    results = backtest_engine.get_performance_summary()
    
    # Print summary statistics
    print("\nBacktest Results:")
    print(f"Total P&L: ${results['total_pnl'].iloc[-1]:.2f}")
    print(f"Realized P&L: ${results['realized_pnl'].iloc[-1]:.2f}")
    print(f"Max Position: {abs(results['position']).max():.2f}")
    print(f"Average Spread: ${results['spread'].mean():.4f}")