#!/usr/bin/env python3
"""
Market Maker Backtest Visualization

Reads the pnl_curve.csv exported by the C++ simulator and produces
a multi-panel chart showing PnL, inventory, and mid price over time.

Usage: python3 plot_backtest.py [pnl_curve.csv]
"""

import csv
import sys

def main():
    filename = sys.argv[1] if len(sys.argv) > 1 else "pnl_curve.csv"
    
    try:
        import matplotlib
        matplotlib.use('Agg')  # Non-interactive backend
        import matplotlib.pyplot as plt
        import matplotlib.ticker as ticker
    except ImportError:
        print("matplotlib not installed. Install with: pip3 install matplotlib")
        print("Falling back to text summary...\n")
        print_text_summary(filename)
        return

    # Read CSV
    ticks, positions, realized, unrealized, total_pnl, mid_prices = [], [], [], [], [], []
    
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            ticks.append(int(row['tick']))
            positions.append(int(row['position']))
            realized.append(float(row['realized_pnl']))
            unrealized.append(float(row['unrealized_pnl']))
            total_pnl.append(float(row['total_pnl']))
            mid_prices.append(float(row['mid_price']))

    if not ticks:
        print("No data in CSV.")
        return

    # Create figure with 3 subplots
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(14, 10), sharex=True,
                                         gridspec_kw={'height_ratios': [3, 2, 2]})
    fig.suptitle('Market Maker Backtest Results', fontsize=16, fontweight='bold')

    # --- Panel 1: PnL ---
    ax1.plot(ticks, total_pnl, color='#2196F3', linewidth=1.2, label='Total PnL')
    ax1.plot(ticks, realized, color='#4CAF50', linewidth=0.8, alpha=0.7, label='Realized PnL')
    ax1.axhline(y=0, color='gray', linewidth=0.5, linestyle='--')
    ax1.fill_between(ticks, total_pnl, 0, 
                     where=[p >= 0 for p in total_pnl], 
                     alpha=0.15, color='#4CAF50')
    ax1.fill_between(ticks, total_pnl, 0, 
                     where=[p < 0 for p in total_pnl], 
                     alpha=0.15, color='#F44336')
    ax1.set_ylabel('PnL ($)')
    ax1.legend(loc='upper left')
    ax1.grid(True, alpha=0.3)
    ax1.yaxis.set_major_formatter(ticker.FormatStrFormatter('$%.0f'))

    # --- Panel 2: Inventory ---
    colors = ['#F44336' if p > 0 else '#2196F3' for p in positions]
    ax2.fill_between(ticks, positions, 0, 
                     where=[p >= 0 for p in positions],
                     alpha=0.4, color='#F44336', label='Long')
    ax2.fill_between(ticks, positions, 0, 
                     where=[p < 0 for p in positions],
                     alpha=0.4, color='#2196F3', label='Short')
    ax2.plot(ticks, positions, color='black', linewidth=0.5, alpha=0.5)
    ax2.axhline(y=0, color='gray', linewidth=0.5, linestyle='--')
    ax2.set_ylabel('Net Position (shares)')
    ax2.legend(loc='upper left')
    ax2.grid(True, alpha=0.3)

    # --- Panel 3: Mid Price ---
    ax3.plot(ticks, mid_prices, color='#FF9800', linewidth=0.8)
    ax3.set_ylabel('Mid Price ($)')
    ax3.set_xlabel('Tick')
    ax3.grid(True, alpha=0.3)
    ax3.yaxis.set_major_formatter(ticker.FormatStrFormatter('$%.2f'))

    plt.tight_layout()
    outfile = filename.replace('.csv', '_report.png')
    plt.savefig(outfile, dpi=150, bbox_inches='tight')
    print(f"Chart saved to {outfile}")


def print_text_summary(filename):
    """Fallback text summary if matplotlib is not available."""
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    
    if not rows:
        print("No data.")
        return

    total_pnls = [float(r['total_pnl']) for r in rows]
    positions = [int(r['position']) for r in rows]
    
    print(f"Ticks:          {rows[-1]['tick']}")
    print(f"Final PnL:      ${total_pnls[-1]:.2f}")
    print(f"Max PnL:        ${max(total_pnls):.2f}")
    print(f"Min PnL:        ${min(total_pnls):.2f}")
    print(f"Final Position: {positions[-1]}")
    print(f"Max Long:       {max(positions)}")
    print(f"Max Short:      {min(positions)}")


if __name__ == "__main__":
    main()