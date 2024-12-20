import matplotlib.pyplot as plt
import matplotlib.animation as animation
import numpy as np

# Synthetic market data
times = np.arange(0, 200)
prices = 100 + np.cumsum(np.random.randn(len(times)) * 0.5)

# Parameters for Stop Loss Hunting Strategy
entry_range_ticks = 2
target_ticks = 5
max_loss_ticks = 3
tick_size = 0.1
position = 0
entry_price = None
profits = []
entries = []
exits = []

# Simulated stop loss hunting strategy
for t, price in enumerate(prices):
    # Monitor recent highs/lows
    recent_high = max(prices[max(0, t-50):t+1])  # Lookback of 50 periods
    recent_low = min(prices[max(0, t-50):t+1])
    profit = 0

    if position == 0:
        # Entry logic: Buy near recent low or Sell near recent high
        if abs(price - recent_high) <= entry_range_ticks * tick_size:
            position = -1  # Short position
            entry_price = price
            entries.append((t, price))
        elif abs(price - recent_low) <= entry_range_ticks * tick_size:
            position = 1  # Long position
            entry_price = price
            entries.append((t, price))
    else:
        # Exit logic: Profit target or stop loss
        profit_ticks = (price - entry_price) / tick_size if position == 1 else (entry_price - price) / tick_size
        if profit_ticks >= target_ticks or profit_ticks <= -max_loss_ticks:
            profit = profit_ticks * tick_size * position
            profits.append(profit)
            exits.append((t, price))
            position = 0
            entry_price = None

# For plotting
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), gridspec_kw={'height_ratios': [3, 1]})

# Line chart for price, high/low levels, entries, and exits
line_price, = ax1.plot([], [], color='blue', label='Price')
line_high, = ax1.plot([], [], color='green', linestyle='--', label='Recent High')
line_low, = ax1.plot([], [], color='red', linestyle='--', label='Recent Low')
entry_markers, = ax1.plot([], [], 'go', label='Entry')
exit_markers, = ax1.plot([], [], 'ro', label='Exit')

# Bar chart for profits and losses
bar_profits = ax2.bar([], [])

ax1.set_xlim(0, max(times))
ax1.set_ylim(min(prices) - 5, max(prices) + 5)
ax1.set_xlabel('Time')
ax1.set_ylabel('Price')
ax1.legend()

ax2.set_xlim(0, max(times))
ax2.set_ylim(-2, 2)
ax2.set_xlabel('Time')
ax2.set_ylabel('Profit/Loss')

def init():
    line_price.set_data([], [])
    line_high.set_data([], [])
    line_low.set_data([], [])
    entry_markers.set_data([], [])
    exit_markers.set_data([], [])
    return line_price, line_high, line_low, entry_markers, exit_markers, bar_profits

def update(frame):
    # Update line chart
    line_price.set_data(times[:frame], prices[:frame])
    
    # Calculate high/low lines safely
    highs = [max(prices[max(0, i-50):i+1]) for i in range(frame)]
    lows = [min(prices[max(0, i-50):i+1]) for i in range(frame)]
    line_high.set_data(times[:frame], highs)
    line_low.set_data(times[:frame], lows)

    # Update markers for entries and exits - handle empty cases safely
    entry_points = [(t, p) for t, p in entries if t <= frame]
    exit_points = [(t, p) for t, p in exits if t <= frame]
    
    if entry_points:
        entry_x, entry_y = zip(*entry_points)
        entry_markers.set_data(entry_x, entry_y)
    else:
        entry_markers.set_data([], [])
        
    if exit_points:
        exit_x, exit_y = zip(*exit_points)
        exit_markers.set_data(exit_x, exit_y)
    else:
        exit_markers.set_data([], [])

    # Update bar chart for profits
    ax2.clear()
    if profits[:frame]:
        ax2.bar(range(len(profits[:frame])), profits[:frame], 
                color=['green' if p > 0 else 'red' for p in profits[:frame]])
    ax2.set_xlim(0, max(times))
    ax2.set_ylim(-2, 2)
    ax2.set_xlabel('Time')
    ax2.set_ylabel('Profit/Loss')

    return line_price, line_high, line_low, entry_markers, exit_markers

ani = animation.FuncAnimation(fig, update, frames=len(times), init_func=init, interval=100, blit=False)


# Save as GIF (requires imagemagick or ffmpeg)
ani.save("StopLossHunter_demo.gif", writer='pillow', fps=10)
