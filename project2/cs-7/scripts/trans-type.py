import json
import os
import sys

current_path = os.environ.get("PATH", "")
new_path = "/usr/local/llvm-17/bin:" + current_path
os.environ["PATH"] = new_path

print(new_path)

file = sys.argv[1]
CONFIG=sys.argv[2]

print("开始源到源转换")
os.system("clang -Xclang -load -Xclang /home/xuzicai/create_searchspace/precimonious/plugin/TransformType.so -Xclang -plugin -Xclang trans-type -Xclang -plugin-arg-trans-type -Xclang -output-path -Xclang -plugin-arg-trans-type -Xclang ../../tempscripts/ -Xclang -plugin-arg-trans-type -Xclang -input-config -Xclang -plugin-arg-trans-type -Xclang {} ./{} > /dev/null 2>&1".format(CONFIG, file))
print("源到源转换已完成")
os.system("cp ../../tempscripts/{} ./mix-precision.c".format(file))
print("转换后的程序已经存储在当前目录下的mix-precision.c文件")
print("使用如下命令查看二者差别：")
print("colordiff -y --suppress-common-lines mix-precision.c odometry.c")
