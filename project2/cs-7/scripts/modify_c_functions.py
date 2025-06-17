import json
import sys
import os
import re
# import time

def load_json(json_path):
    with open(json_path, 'r') as f:
        return json.load(f)

def save_c_file(file_path, lines):
    with open(file_path, 'w') as f:
        f.writelines(lines)

def modify_c_file(c_file_path, json_data):
    # 读取C文件
    with open(c_file_path, 'r') as f:
        lines = f.readlines()

    for key, item in json_data.items():
        if key.startswith("call"):
            # 处理函数调用替换
            line_num = int(item["lines"][0]) - 1
            location = item["location"]
            parts = location.split(":")
            column = int(parts[-1]) - 1
            new_func_name = item["name"]

            old_line = lines[line_num]

            # 找到列起点，从那里往后抓出旧函数名字
            func_pattern = r'\b[a-zA-Z_][a-zA-Z0-9_]*\b'
            match = re.match(func_pattern, old_line[column:])
            if match:
                old_func_name = match.group()
                start = column
                end = column + len(old_func_name)

                new_line = old_line[:start] + new_func_name + old_line[end:]
                lines[line_num] = new_line
            else:
                print(f"Warning: 第{line_num+1}行{column+1}列找不到函数名，跳过。")

        elif key.startswith("localVar"):
            # 处理局部变量类型修改
            line_num = int(item["lines"][0]) - 1
            var_name = item["name"]
            new_type = item["type"]

            old_line = lines[line_num]

            # 支持多词复合类型，如 long double、unsigned int
            pattern = r'(?P<type>(?:\w+\s+)*\w+)\s+' + re.escape(var_name) + r'\b'
            match = re.search(pattern, old_line)
            if match:
                start, end = match.span("type")
                new_line = old_line[:start] + new_type + old_line[end:]
                lines[line_num] = new_line
            else:
                print(f"Warning: 变量 {var_name} 在第 {line_num+1} 行找不到，跳过。")

    # timestamp = time.strftime("%Y%m%d_%H%M%S")

    # 生成新的文件名
    dir_name = os.path.dirname(c_file_path)
    base_name = os.path.basename(c_file_path)
    name, ext = os.path.splitext(base_name)
    new_file_name = f"{name}_mxp{ext}"
    new_file_path = os.path.join(dir_name, new_file_name)

    # 保存修改后的文件
    save_c_file(new_file_path, lines)
    print(f"修改后的文件保存为: {new_file_path}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("用法: python modify_c_file.py <input_json路径> <c源文件路径>")
        sys.exit(1)

    json_path = sys.argv[1]
    c_file_path = sys.argv[2]

    data = load_json(json_path)
    modify_c_file(c_file_path, data)
