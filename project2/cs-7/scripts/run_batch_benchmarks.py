import csv
import subprocess
import argparse
import os

def run_batch(benchmarks_root, config_file):
    with open(config_file, newline='') as csvfile:
        reader = csv.DictReader(csvfile)
        for row in reader:
            subdir = row['subdir']
            thres = row['thres']
            repeat = row['repeat']
            timeout = row['timeout']

            print(f"\n[INFO] Running benchmark '{subdir}' with thres={thres}, repeat={repeat}, timeout={timeout}")

            try:
                subprocess.run([
                    "python3", "scripts/run_single_benchmark.py",
                    benchmarks_root, subdir, thres, repeat, timeout
                ], check=True)
            except subprocess.CalledProcessError as e:
                print(f"[ERROR] Benchmark '{subdir}' failed: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Batch run multiple benchmarks from a config CSV.")
    parser.add_argument("benchmarks_root", help="Path to benchmarks root directory.")
    parser.add_argument("config_file", help="CSV file with parameters for each subdir.")

    args = parser.parse_args()
    run_batch(args.benchmarks_root, args.config_file)
