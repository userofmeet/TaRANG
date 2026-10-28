
import pandas as pd
import matplotlib.pyplot as plt
from pathlib import Path
from matplotlib.ticker import MaxNLocator, ScalarFormatter

# --------------------------------------------------
# Configuration
# --------------------------------------------------
folder = Path(__file__).resolve().parent
file_path = folder / "print_08.csv"
output_path = folder / "print_08_plot.png"

# --------------------------------------------------
# Read CSV
# --------------------------------------------------
if not file_path.exists():
    raise FileNotFoundError(f"CSV file not found: {file_path}")

df = pd.read_csv(file_path, header=None)

# Convert both columns to numeric values
x = pd.to_numeric(df.iloc[:, 0], errors="coerce")
y = pd.to_numeric(df.iloc[:, 1], errors="coerce")

# Remove invalid rows
data = pd.DataFrame({"x": x, "y": y}).dropna()

if data.empty:
    raise ValueError("No valid numeric data found in the CSV.")

x = data["x"]
y = data["y"]

# --------------------------------------------------
# Professional plot styling
# --------------------------------------------------
plt.rcParams.update({
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "axes.edgecolor": "#333333",
    "axes.labelcolor": "#222222",
    "xtick.color": "#333333",
    "ytick.color": "#333333",
    "font.size": 11,
    "axes.titlesize": 15,
    "axes.labelsize": 12,
})

fig, ax = plt.subplots(figsize=(12, 6.5), constrained_layout=True)

# Plot the signal
ax.plot(
    x, y,
    color="#1677C8",
    linewidth=1.4,
    alpha=0.95,
    solid_capstyle="round"
)

# Titles and labels
ax.set_title("MagniSense Sensor Signal", pad=16, fontweight="bold")
ax.set_xlabel("Column 1", labelpad=10)
ax.set_ylabel("Column 2", labelpad=10)

# Keep a reasonable number of tick labels
ax.xaxis.set_major_locator(MaxNLocator(nbins=8))
ax.yaxis.set_major_locator(MaxNLocator(nbins=7))

# Avoid unnecessarily long scientific-notation tick labels
formatter = ScalarFormatter(useOffset=False)
formatter.set_powerlimits((-3, 5))
ax.yaxis.set_major_formatter(formatter)

# Subtle grid behind the signal
ax.set_axisbelow(True)
ax.grid(True, which="major", color="#DDE3E8", linewidth=0.8)

# Clean borders
ax.spines["top"].set_visible(False)
ax.spines["right"].set_visible(False)

# Add a little breathing room
ax.margins(x=0.01, y=0.08)

# --------------------------------------------------
# Save high-quality image
# --------------------------------------------------
fig.savefig(
    output_path,
    dpi=300,
    bbox_inches="tight",
    facecolor="white"
)

print(f"Plot saved to: {output_path}")

plt.show()
