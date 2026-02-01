# This is a Python (pandas) program - used to analyze data for eg. recently collected data logged from Serial Monitor (Arduino IDE)

# Submitted to GitHub repository - "Water Quality Monitor"

# by Charles (or Charlie) Tejano - 01/31/26
import pandas as pd

def load_csv(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)

    # Normalize column names just in case
    df.columns = [c.strip().lower().replace(" ", "_") for c in df.columns]

    # Expected columns (yours look like these):
    # ms, raw_median, ema_raw, index, status, cal_loaded, clear_raw, cloudy_raw

    # Convert ms -> seconds (nice for plotting + readability)
    if "ms" in df.columns:
        df["seconds"] = df["ms"] / 1000.0

    return df

def summarize(df: pd.DataFrame, label: str) -> pd.DataFrame:
    cols = [c for c in ["raw_median", "ema_raw", "index"] if c in df.columns]
    summary = df[cols].agg(["count", "mean", "median", "std", "min", "max"]).T
    summary.insert(0, "dataset", label)
    return summary.reset_index(names="metric")

def main():
    brita = load_csv("data/britacollection_uncalibrated.csv")
    tap   = load_csv("data/tapwtr_uncalibrated.csv")

    brita_summary = summarize(brita, "brita")
    tap_summary   = summarize(tap, "tap")

    out = pd.concat([brita_summary, tap_summary], ignore_index=True)
    out.to_csv("analysis/summary_stats.csv", index=False)

    print("\n=== Summary stats saved to analysis/summary_stats.csv ===\n")
    print(out)

if __name__ == "__main__":
    main()
