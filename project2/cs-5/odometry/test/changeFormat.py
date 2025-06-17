import json
import os

# === 加载原始 JSON 文件 ===
with open("optimized_config.json") as f:
    data = json.load(f)

output = {}
index = 1

# === 处理 calls 函数调用 ===
target_funcs = {"sin", "cos", "sqrt", "sinf", "cosf", "sqrtf", "tan", "tanf","atan","atanf", "exp", "expf", "log", "logf", "pow", "powf", "fabs", "fabsf"}

for func_name, calls in data.get("calls", {}).items():
    if not calls:  # calls 为 None 或 空列表 时跳过
        continue
    for call in calls:
        func_type = call.get("type")
        if func_type not in target_funcs:
            continue
        file_path = call["file"]
        filename = os.path.basename(file_path)
        relative_path = file_path.split("cs-7/", 1)[-1] if "cs-7/" in file_path else file_path
        line = call["line"]
        column = call["column"]
        arg_type = ["float", "float"] if func_type.endswith("f") else ["double", "double"]

        entry = {
            "file": filename,
            "function": func_name,
            "lines": [str(line)],
            "location": f"{relative_path}:{line}:{column}",
            "name": func_type,
            "switch": func_type,
            "type": arg_type
        }
        key = f"call{index}"
        output[key] = entry
        index += 1

# # === 处理 local 变量 ===
# for func_name, vars_list in data.get("local", {}).items():
#     if not vars_list:  # calls 为 None 或 空列表 时跳过
#         continue
#     for var in vars_list:
#         entry = {
#             "file": var["file"].split("/")[-1],
#             "function": func_name,
#             "lines": [var["line"]],
#             "location": "parmVar" if var.get("isParam") else "localVar",
#             "name": var["name"],
#             "type": var["type"]
#         }
#         key = f"localVar{index}"
#         output[key] = entry
#         index += 1

# === 处理 local 变量 ===
for func_name, vars_list in data.get("local", {}).items():
    if not vars_list:  # calls 为 None 或 空列表 时跳过
        continue
    for var in vars_list:
        clean_type = var["type"].replace("const ", "")  # 去掉 const 前缀
        entry = {
            "file": var["file"].split("/")[-1],
            "function": func_name,
            "lines": [var["line"]],
            "location": "parmVar" if var.get("isParam") else "localVar",
            "name": var["name"],
            "type": clean_type
        }
        key = f"localVar{index}"
        output[key] = entry
        index += 1

# === 保存输出 JSON ===
with open("mix-precision_config.json", "w") as f:
    json.dump(output, f, indent=4)
