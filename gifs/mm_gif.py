import matplotlib.pyplot as plt
import matplotlib.animation as animation
import numpy as np

# Synthetic market data
times = np.arange(0, 200)
prices = 100 + np.cumsum(np.random.randn(len(times)) * 0.2)

# Simulate trades
trade_sizes = np.random.randint(1, 5, len(times))  # Trade sizes (1 to 5 units)
trade_types = np.random.choice([1, -1], size=len(times))  # 1 = Buy, -1 = Sell
trade_impacts = []

# Parameters for market making strategy
impact_multiplier = 2.5
levels_to_consider = 4
rolling_window = 50
bid_quotes = []
ask_quotes = []

# Simulate the strategy
for t, (price, trade_size, trade_type) in enumerate(zip(prices, trade_sizes, trade_types)):
    # Simulated trade impact
    impact = impact_multiplier * trade_type * (trade_size / 10)
    trade_impacts.append(impact)

    # Imaginary quote adjustment logic
    spread = 0.05
    mid_price = price + impact
    bid_price = mid_price - spread / 2
    ask_price = mid_price + spread / 2
    bid_quotes.append(bid_price)
    ask_quotes.append(ask_price)

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), gridspec_kw={'height_ratios': [3, 1]})

# Plot for price and quotes
line_price, = ax1.plot([], [], color='blue', label='Mid Price')
line_bid, = ax1.plot([], [], color='green', label='Bid Quote')
line_ask, = ax1.plot([], [], color='red', label='Ask Quote')

# Bar graph for trade impacts
bar_impacts = ax2.bar([], [])

ax1.set_xlim(0, max(times))
ax1.set_ylim(min(prices) - 2, max(prices) + 2)
ax1.set_xlabel('Time')
ax1.set_ylabel('Price')
ax1.legend()

ax2.set_xlim(0, max(times))
ax2.set_ylim(-5, 5)
ax2.set_xlabel('Time')
ax2.set_ylabel('Trade Impact')

def init():
    line_price.set_data([], [])
    line_bid.set_data([], [])
    line_ask.set_data([], [])
    return line_price, line_bid, line_ask, bar_impacts

def update(frame):
    # Update line chart
    line_price.set_data(times[:frame], prices[:frame])
    line_bid.set_data(times[:frame], bid_quotes[:frame])
    line_ask.set_data(times[:frame], ask_quotes[:frame])

    # Update bar chart
    ax2.clear()
    ax2.bar(times[:frame], trade_impacts[:frame], color=['green' if t > 0 else 'red' for t in trade_types[:frame]])
    ax2.set_xlim(0, max(times))
    ax2.set_ylim(-5, 5)
    ax2.set_xlabel('Time')
    ax2.set_ylabel('Trade Impact')

    return line_price, line_bid, line_ask

ani = animation.FuncAnimation(fig, update, frames=len(times), init_func=init, interval=100, blit=False)


# Save as GIF (requires imagemagick or ffmpeg)
ani.save("TradeImpactMM_demo.gif", writer='imagemagick', fps=10)
