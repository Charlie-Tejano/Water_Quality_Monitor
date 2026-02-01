# This is a Python (pandas) program - used to analyze data for eg. recently collected data logged from Serial Monitor (Arduino IDE)

# Submitted to GitHub repository - "Water Quality Monitor"

# by Charles (or Charlie) Tejano - 01/31/26
import os
import pandas as pd

# -----------------------------
# Purpose:
#   Load Arduino CSV logs for Brita vs Tap and produce:
#   1) clean merged dataset
#   2) summary stats per sample type
#   3) stability metrics (noise) + trend (rolling mean)
#   4) export clean tables to /analysis/outputs
# -----------------------------

DATA_DIR = "."
OUT_DIR = "analysis/outputs"

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
BRITA_CSV = os.path.join(DATA_DIR, "britacollection_uncalibrated.csv")
TAP_CSV   = os.path.join(DATA_DIR, "tapwtr_uncalibrated.csv")

os.makedirs(OUT_DIR, exist_ok=True)

def load_and_clean(csv_path: str, label: str) -> pd.DataFrame:
    """
    Reads a CSV exported from Arduino Serial logging.
    Adds:
      - source label (brita/tap)
      - time_s: time in seconds since start of that run
    Ensures numeric columns are parsed safely.
    """
    df = pd.read_csv(csv_path)

    # Normalize column names (helps if you renamed headers slightly)
    df.columns = [c.strip().lower().replace(" ", "_") for c in df.columns]

    # Expected columns from your logger:
    # milliseconds_(ms) or ms, raw_median, ema_raw, index, status, cal_loaded, clear_raw, cloudy_raw
    # We'll handle a few likely variants gracefully.
    if "ms" not in df.columns:
        # Sometimes you used "milliseconds_(ms)" on GitHub preview
        candidates = [c for c in df.columns if "ms" in c]
        if candidates:
            df = df.rename(columns={candidates[0]: "ms"})

    df["source"] = label

    # Make sure numeric fields are numeric (coerce errors to NaN)
    for col in ["ms", "raw_median", "ema_raw", "index", "cal_loaded", "clear_raw", "cloudy_raw"]:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")

    # Convert ms to seconds relative to start of that dataset
    df["time_s"] = (df["ms"] - df["ms"].min()) / 1000.0

    return df

def summarize(df: pd.DataFrame) -> pd.DataFrame:
    """
    Returns per-source summary stats showing:
      - central tendency
      - range
      - noise (std)
    """
    stats = (
        df.groupby("source")
          .agg(
              samples=("index", "count"),
              duration_s=("time_s", "max"),
              index_mean=("index", "mean"),
              index_median=("index", "median"),
              index_std=("index", "std"),
              index_min=("index", "min"),
              index_max=("index", "max"),
              ema_mean=("ema_raw", "mean"),
              raw_mean=("raw_median", "mean"),
          )
          .reset_index()
    )
    return stats

def add_trends(df: pd.DataFrame, window: int = 10) -> pd.DataFrame:
    """
    Adds a rolling mean to visualize trend/stability.
    window=10 works well for ~0.35s sample interval (your delay(350)).
    """
    df = df.sort_values(["source", "time_s"]).copy()
    df["index_roll_mean"] = (
        df.groupby("source")["index"]
          .transform(lambda s: s.rolling(window=window, min_periods=1).mean())
    )
    return df

def main():
    brita = load_and_clean(BRITA_CSV, "brita")
    tap   = load_and_clean(TAP_CSV, "tap")

    combined = pd.concat([brita, tap], ignore_index=True)
    combined = add_trends(combined, window=10)

    # Export cleaned combined data
    combined_out = os.path.join(OUT_DIR, "combined_clean.csv")
    combined.to_csv(combined_out, index=False)

    # Summary table
    stats = summarize(combined)
    stats_out = os.path.join(OUT_DIR, "summary_stats.csv")
    stats.to_csv(stats_out, index=False)

    # Print results to terminal (nice for quick proof)
    print("\n=== Summary Stats (per source) ===")
    print(stats.to_string(index=False))

    print(f"\nSaved:\n- {combined_out}\n- {stats_out}")

if __name__ == "__main__":
    main()
