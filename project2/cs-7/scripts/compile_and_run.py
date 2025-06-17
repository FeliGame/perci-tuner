import os
import subprocess
import argparse
import re
import csv
import glob
import time

def load_subdirs_from_csv(csv_path):
    subdirs = []
    with open(csv_path, newline='') as csvfile:
        reader = csv.DictReader(csvfile)
        for row in reader:
            subdirs.append(row["subdir"])
    return subdirs

def compile_and_run(source_paths, output_path):
    if isinstance(source_paths, str):
        source_paths = [source_paths]

    compile_cmd = ["sh", "../../scripts/mxpcc.sh", "-mode=generate-config"] + source_paths
    try:
        subprocess.run(compile_cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if os.path.exists("output"):
            os.replace("output", output_path)
        else:
            print(f"[ERROR] No 'output' generated for {source_paths}")
            return None
    except subprocess.CalledProcessError as e:
        print(f"[ERROR] Failed to compile {source_paths}: {e}")
        return None

    # 尝试读取 run_args.txt
    run_dir = os.path.dirname(source_paths[0])
    args_file = os.path.join(run_dir, "run_args.txt")
    run_args = []

    if os.path.exists(args_file):
        with open(args_file, 'r') as f:
            args_line = f.read().strip()
            if args_line:
                run_args = args_line.split()

    try:
        start_time = time.perf_counter()    
        result = subprocess.run([output_path] + run_args, check=True, capture_output=True, text=True)
        end_time = time.perf_counter()
        runtime = end_time -start_time
        output_line = result.stdout.strip().splitlines()[-1]
        match = re.search(r'-?\d+\.?\d*(?:[eE][-+]?\d+)?', output_line)
        return float(match.group()), runtime if match else None
    except Exception as e:
        print(f"[ERROR] Failed to run {output_path} with args {run_args}: {e}")
        return None

def get_c_files(full_path, exclude_mxp=True):
    all_c_files = glob.glob(os.path.join(full_path, "*.c"))
    if exclude_mxp:
        return [f for f in all_c_files if not f.endswith("_mxp.c")]
    else:
        return [f for f in all_c_files if f.endswith("_mxp.c")]

def compare_results(benchmarks_root, subdirs, result_file):
    with open(result_file, 'w') as rf:
        rf.write("{:<30}{:<30}{:<30}{:<15}\n".format("Benchmark", "  Original", "  Mix-Precision", "rel_error"))

        for subdir in subdirs:
            full_path = os.path.join(benchmarks_root, subdir)
            if not os.path.isdir(full_path):
                print(f"[WARNING] Skipping missing directory: {subdir}")
                continue

            base_c_files = get_c_files(full_path, exclude_mxp=True)
            mxp_c_files  = get_c_files(full_path, exclude_mxp=False)
            base_out = os.path.join(full_path, "output_origin")
            mxp_out  = os.path.join(full_path, "output_mxp")

            print(f"[INFO] Processing {subdir}...")

            if not base_c_files:
                print(f"[ERROR] No base .c files found in {subdir}")
                rf.write("{:<30}{:<30}{:<30}{:<15}\n".format(subdir, "ERROR", "ERROR", "ERROR"))
                continue

            if not mxp_c_files:
                print(f"[ERROR] No _mxp.c files found in {subdir}")
                rf.write("{:<30}{:<30}{:<30}{:<15}\n".format(subdir, "ERROR", "ERROR", "ERROR"))
                continue
            original_result, original_runtime = compile_and_run(base_c_files, base_out)
            mxp_result, mxp_runtime = compile_and_run(mxp_c_files, mxp_out)

            if original_result is None or mxp_result is None:
                rf.write("{:<30}{:<30}{:<30}{:<15}\n".format(subdir, "ERROR", "ERROR", "ERROR"))
                continue

            try:
                rel_error = abs(mxp_result - original_result) / abs(original_result) if original_result != 0 else float('inf')
                rf.write("{:<30}{:<30.13e}{:<30.13e}{:<15.2e}\n".format(subdir, original_result, mxp_result, rel_error))
                print(f"[INFO] Original:         {original_result:.13e}")
                print(f"[INFO] Mix-Precision:    {mxp_result:.13e}")
                print(f"[INFO] Relative Err:     {rel_error:.2e}")
                print(f"[INFO] acc_rate:         {100*(original_runtime - mxp_runtime)/mxp_runtime:<8.6f} %")
                print()
            except Exception as e:
                print(f"[ERROR] Failed to compute relative error for {subdir}: {e}")
                rf.write("{:<30}{:<30}{:<30}{:<15}\n".format(subdir, "ERROR", "ERROR", "ERROR"))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Compare original and mxp versions of benchmarks.")
    parser.add_argument("benchmarks_root", help="Root directory of benchmarks")
    parser.add_argument("subdirs", nargs='*', help="List of benchmark subdirectories to compare (optional)")
    parser.add_argument("--csv", default="batch_config_copy.csv", help="CSV file to load subdirs from if not specified")
    parser.add_argument("--output", default="result.txt", help="Output file to save results")

    args = parser.parse_args()

    if not args.subdirs:
        args.subdirs = load_subdirs_from_csv(args.csv)

    compare_results(args.benchmarks_root, args.subdirs, args.output)
