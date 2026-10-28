import pandas as pd
import numpy as np
from pathlib import Path

# ============================================================
# LOAD CALIBRATION DATA
# ============================================================

csv_path = Path(__file__).resolve().parent / "digipot_characterization.csv"
df = pd.read_csv(csv_path)

# ============================================================
# SORT DATA
# ============================================================

df_dmm = df.sort_values("measured_dmm_kohm")
df_smd = df.sort_values("measured_smd_kohm")

# ============================================================
# CALIBRATION FUNCTIONS
# ============================================================


def calculate_sent_value(desired_kohm, measured_column):
    """
    Calculate the resistance to send to the MCU.

    Uses piecewise linear interpolation within the calibration
    range and linear extrapolation outside it.
    """

    measured = df[measured_column].to_numpy(dtype=float)
    sent = df["sent_res_kohm"].to_numpy(dtype=float)

    # Sort by measured resistance
    order = np.argsort(measured)
    measured = measured[order]
    sent = sent[order]

    if desired_kohm < measured.min():
        print("\nWARNING: Below calibration range.")
        print("Using extrapolation from the two lowest points.")

        x1, x2 = measured[0], measured[1]
        y1, y2 = sent[0], sent[1]

        sent_kohm = y1 + (
            (desired_kohm - x1) * (y2 - y1) / (x2 - x1)
        )

    elif desired_kohm > measured.max():
        print("\nWARNING: Above calibration range.")
        print("Using extrapolation from the two highest points.")

        x1, x2 = measured[-2], measured[-1]
        y1, y2 = sent[-2], sent[-1]

        sent_kohm = y1 + (
            (desired_kohm - x1) * (y2 - y1) / (x2 - x1)
        )

    else:
        # Normal interpolation inside the calibration range
        sent_kohm = np.interp(
            desired_kohm,
            measured,
            sent
        )

    return sent_kohm

# ============================================================
# USER SELECTION
# ============================================================

print("\n==========================================")
print("      AD8403 RESISTANCE CALIBRATION")
print("==========================================")

print("\nCalibrate using:")
print("1. DMM")
print("2. SMD")

choice = input("\nEnter choice (1 or 2): ").strip()

if choice == "1":

    instrument = "DMM"
    measured_column = "measured_dmm_kohm"

elif choice == "2":

    instrument = "SMD"
    measured_column = "measured_smd_kohm"

else:

    raise ValueError("Invalid choice. Enter 1 or 2.")


# ============================================================
# DESIRED RESISTANCE
# ============================================================

desired_kohm = float(
    input(
        f"\nEnter desired {instrument} resistance (kOhm): "
    )
)

if desired_kohm <= 0:
    raise ValueError("Resistance must be greater than zero.")


# ============================================================
# CALCULATE REQUIRED SENT VALUE
# ============================================================

sent_kohm = calculate_sent_value(
    desired_kohm,
    measured_column
)

sent_ohms = round(sent_kohm * 1000)


# ============================================================
# CHECK AD8403 RANGE
# ============================================================

if sent_ohms < 50:

    print("\nWARNING: Value is below the AD8403 minimum.")

elif sent_ohms > 50000:

    print("\nWARNING: Value is above the AD8403 maximum.")


# ============================================================
# DISPLAY RESULT
# ============================================================

print("\n==========================================")
print("             CALIBRATION RESULT")
print("==========================================")

print(
    f"\nDesired {instrument} reading : "
    f"{desired_kohm:.3f} kOhm"
)

print(
    f"Calculated sent resistance  : "
    f"{sent_kohm:.3f} kOhm"
)

print(
    f"Enter this into MCU         : "
    f"{sent_ohms} ohms"
)

print("\n==========================================")
print("Enter this value in your MCU terminal.")
print("==========================================")