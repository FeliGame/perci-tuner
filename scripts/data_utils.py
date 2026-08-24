#!/usr/bin/env python3
# Handle run_history.csv

# @Author: LinLanfeng, Guocheng —— NUDT

import os, csv, decimal, math
from decimal import Decimal, getcontext

# 高精度计算相对误差（避免内部运算误差）
getcontext().prec = 50

RUN_HISTORY = "run_history.csv"
ORIGINAL_TIME = float('inf')  # running time of original program
ORIGINAL_RESULT = ""          # running result of original program


def is_non_finite(value):
    try:
        numeric_value = float(str(value).strip())
        return math.isnan(numeric_value) or math.isinf(numeric_value)
    except (ValueError, TypeError):
        return False

# delete run_history.csv
def delete_data():
    if os.path.exists(RUN_HISTORY):
        os.remove(RUN_HISTORY)


# read datas from run_history.csv
def read_data():
    if not os.path.exists(RUN_HISTORY):
        return []
    try:
        with open(RUN_HISTORY, 'r', newline='') as csvfile:
            reader = csv.DictReader(csvfile)
            return list(reader)
    except csv.Error:
        return []


def create_new_data(new_line, path):
    with open(path, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=list(new_line.keys()))
        writer.writeheader()
        writer.writerow(new_line)
    return


# store the running time and result to run_history.csv
def store_data(run_id, time, result, changes, is_mxp=False, parent=0):
    new_line= {
        "run_id": run_id,
        "parent": parent,
        "acc_rate": f"{((ORIGINAL_TIME - time) / time):.6%}",
        "seconds": f"{time:.4e}",
        "rel_err": result if is_mxp else f"{get_rel_err(result, ORIGINAL_RESULT):e}",
        "result": None if is_mxp else result,
        "changes": changes
    }

    # if no run_history.csv, then create one
    existing_data = read_data()
    if existing_data == []:
        create_new_data(new_line, RUN_HISTORY)
        return

    with open(RUN_HISTORY, 'a', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=list(new_line.keys()))
        writer.writerow(new_line)


# check the current result is nearly equal to old result
def is_nearly_equal(new_result, old_result, thres):    
    # 调用修改后的get_rel_err函数计算相对误差
    rel_err = get_rel_err(new_result, old_result)
    
    # 如果相对误差是nan/inf，直接返回False
    if is_non_finite(rel_err):
        return False
    
    # 使用Decimal进行比较，确保精度
    try:
        return decimal.Decimal(str(rel_err)) <= decimal.Decimal(thres)
    except (decimal.InvalidOperation, ValueError, TypeError):
        # 阈值无法转换为浮点数时，视为不匹配
        return False


# use relative error to compare
def get_rel_err(new_result, old_result):    
    if new_result == "RUNTIME-ERROR" or old_result == "RUNTIME-ERROR":
        return float('inf')
    try:
        # 转换为字符串并去除首尾空格，便于格式判断
        new_str = str(new_result).strip()
        old_str = str(old_result).strip()

        if is_non_finite(new_str) or is_non_finite(old_str):
            if "nan" in new_str.lower() or "nan" in old_str.lower():
                return float('nan')
            return float('inf')
        
        # 判断是否为十六进制格式（支持正负号：0x或-0x开头）
        is_hex_new = new_str.startswith(('0x', '-0x'))
        is_hex_old = old_str.startswith(('0x', '-0x'))
        
        if is_hex_new and is_hex_old:
            # 两者均为十六进制，使用十六进制相对误差计算逻辑
            old_components = parse_hex_components(old_str)
            new_components = parse_hex_components(new_str)
            rel_err = compute_relative_error_hex(old_components, new_components)
            return float(rel_err)
        elif is_hex_new or is_hex_old:
            # 仅一方为十六进制，格式不匹配，返回无穷大误差
            return float('inf')
        else:
            # 十进制处理逻辑（沿用原有逻辑）
            # 计算需要的精度基于输入值的最大位数
            precision = 10  # 默认精度
            
            if '.' in new_str:
                # 小数点后位数 + 整数部分位数
                new_precision = len(new_str.split('.')[0]) + len(new_str.split('.')[1])
                precision = max(precision, new_precision)
            else:
                # 整数的位数
                precision = max(precision, len(new_str))
            
            if '.' in old_str:
                old_precision = len(old_str.split('.')[0]) + len(old_str.split('.')[1])
                precision = max(precision, old_precision)
            else:
                precision = max(precision, len(old_str))
            
            # 转换为Decimal类型
            n = decimal.Decimal(new_str)
            o = decimal.Decimal(old_str)
            
            # 计算相对误差
            if o == 0:
                return float(abs(n))
            
            relative_error = abs((n - o) / o)
            return float(relative_error)
    except (decimal.InvalidOperation, ValueError, TypeError):
        # 处理所有解析/计算错误（包括十六进制格式错误）
        return float('inf')


def parse_hex_components(hex_str):
    """
    适配x86_fp80（如0xe.7b51dp+2）和IEEE 754 fp128的十六进制浮点解析
    返回：(符号, 尾数整数, 二进制指数, 小数位数)
    支持格式：±0x[0-f].[0-f]p±n（x86_fp80）、±0x1.[0-f]p±n（IEEE 754 fp128）
    """
    sign = 1
    # 处理负号
    if hex_str.startswith('-'):
        sign = -1
        hex_str = hex_str[1:]
    # 校验前缀（支持0x/0X）
    if not hex_str.lower().startswith('0x'):
        raise ValueError(f"无效格式：{hex_str}，需以0x或-0x开头")
    # 移除可能的L后缀（x86_fp80长双精度标记，如0xe.7b51dp+2L）
    hex_str = hex_str.rstrip('L').rstrip('l')
    # 校验指数部分（必须包含p/P）
    if 'p' not in hex_str.lower():
        raise ValueError(f"无效格式：{hex_str}，缺少指数标记'p'（如p+5、p-3）")
    
    # 拆分尾数和指数（忽略p的大小写）
    mantissa_part, exp_str = hex_str.split('p', 1) if 'p' in hex_str else hex_str.split('P', 1)
    exponent = int(exp_str.strip())  # 二进制指数（2^exponent，x86_fp80无额外偏移）
    
    # 拆分尾数的整数和小数部分（移除0x前缀）
    mantissa_part = mantissa_part[2:].strip()
    if '.' in mantissa_part:
        int_hex, frac_hex = mantissa_part.split('.', 1)
        frac_hex = frac_hex.strip()
        frac_digits = len(frac_hex)
        mantissa_hex = int_hex + frac_hex  # 合并为完整十六进制尾数（整数形式）
    else:
        int_hex = mantissa_part.strip()
        frac_hex = ''
        frac_digits = 0
        mantissa_hex = int_hex
    
    # 移除「尾数整数部分必须为1」的强校验（适配x86_fp80非归一化形式）
    # 仅校验整数部分为合法十六进制字符
    if not int_hex or not all(c in '0123456789abcdefABCDEF' for c in int_hex):
        raise ValueError(f"无效尾数整数部分：{int_hex}，需为十六进制字符")
    # 转换尾数为十进制整数
    mantissa = int(mantissa_hex, 16) if mantissa_hex else 0
    
    return sign, mantissa, exponent, frac_digits


def compute_relative_error_hex(base_components, current_components):
    """
    复用核心误差计算逻辑：基于原生组件运算，无十进制转换
    相对误差公式：|(current - base) / base|
    当基础值为0时，计算绝对误差：|current|
    """
    # 解析基础值（x）和对比值（y）的组件
    b_sign, b_mantissa, b_exp, b_frac = base_components
    c_sign, c_mantissa, c_exp, c_frac = current_components
    
    # 检查基础值是否为0（尾数为0时整个数值为0）
    if b_mantissa == 0:
        # 计算当前值的绝对值作为绝对误差
        # 构建当前值的完整表示：sign * mantissa * 2^(exp - 4*frac)
        current_value = c_sign * Decimal(c_mantissa) * (Decimal(2) ** (c_exp - 4 * c_frac))
        return abs(current_value)
    
    # 1. 计算符号比值（仅±1，同号为1，异号为-1）
    sign_ratio = c_sign * b_sign
    # 2. 计算指数差（用于2的幂次运算）
    delta_e = (c_exp - 4 * c_frac) - (b_exp - 4 * b_frac)
    # 3. 高精度尾数比值（十六进制整数直接转Decimal，无舍入）
    m_ratio = Decimal(c_mantissa) / Decimal(b_mantissa)
    # 4. 高精度指数项（2^delta_e）
    exp_term = Decimal(2) ** delta_e
    # 5. 计算y/x的比值
    ratio = sign_ratio * m_ratio * exp_term
    # 6. 相对误差 = |(y/x) - 1|
    return abs(ratio - Decimal(1))
