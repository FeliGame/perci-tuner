import os
import subprocess
import argparse
import glob

def run_single_benchmark(benchmarks_root, subdir, thres, repeat, timeout):
    full_path = os.path.join(benchmarks_root, subdir)
    if not os.path.isdir(full_path):
        print(f"Error: {full_path} does not exist.")
        return

    # Step 1: 执行 mxpcc.sh -mode=search-optimize
    cmd = [
        "sh", os.path.expandvars("$PERCI_TUNER/scripts/mxpcc.sh"),
        "-mode=search-optimize",
        f"-thres={thres}",
        f"-repeat={repeat}",
        f"-timeout={timeout}",
        "-history",
        # f"{subdir}.c",
        "*.c"
    ]
    print(f"[INFO] Running mxpcc.sh in {full_path}")
    try:
        subprocess.run(" ".join(cmd), shell=True, check=True, cwd=full_path)
    except subprocess.CalledProcessError as e:
        print(f"[ERROR] mxpcc.sh failed in {full_path}: {e}")
        return

    # Step 2: 执行 changeType.py
    print(f"[INFO] Running changeType.py in {full_path}")
    try:
        subprocess.run(["python3", "../../scripts/changeType.py"], check=True, cwd=full_path)
    except subprocess.CalledProcessError as e:
        print(f"[ERROR] changeType.py failed: {e}")
        return

    # Step 3: 执行 modify_c_functions.py <subdir>.c
    c_program_path = f"{subdir}.c"
    print(f"[INFO] Running modify_c_functions.py on {c_program_path}")
    try:
        subprocess.run(["python3", "../../scripts/modify_c_functions.py", "mix-precision_config.json", c_program_path],
                       check=True, cwd=full_path)
    except subprocess.CalledProcessError as e:
        print(f"[ERROR] modify_c_functions.py failed: {e}")
        return

    # Step 4: 删除 temp* 文件
    temp_files = glob.glob(os.path.join(full_path, "temp*"))
    for temp_file in temp_files:
        try:
            os.remove(temp_file)
            print(f"[INFO] Deleted {temp_file}")
        except OSError as e:
            print(f"[WARNING] Failed to delete {temp_file}: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run mxpcc.sh + scripts on a single benchmark.")
    parser.add_argument("benchmarks_root", help="Path to benchmarks root directory.")
    parser.add_argument("subdir", help="Name of the subdirectory to run (and source file without .c).")
    parser.add_argument("thres", help="Threshold value.")
    parser.add_argument("repeat", type=int, help="Repeat count.")
    parser.add_argument("timeout", type=float, help="Timeout in seconds.")

    args = parser.parse_args()
    run_single_benchmark(args.benchmarks_root, args.subdir, args.thres, args.repeat, args.timeout)
