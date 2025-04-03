// @Author: Guocheng - NUDT
// Usage: Parse MetaDataNode.

#ifndef INCLUDE_PARSE_MDNODE_HPP_
#define INCLUDE_PARSE_MDNODE_HPP_

#include <llvm/IR/Metadata.h>

  // 辅助函数：从call指令获取metadata节点
  MDNode *getMetadataNode(CallInst *CI, unsigned idx) {
    if (idx >= CI->arg_size()) return nullptr;
    if (MetadataAsValue *MAV = dyn_cast<MetadataAsValue>(CI->getArgOperand(idx))) {
      return dyn_cast<MDNode>(MAV->getMetadata());
    }
    return nullptr;
  }

  // 辅助函数：从metadata节点提取字符串列表
  SmallVector<StringRef> extractStringsFromMD(MDNode *MD) {
    SmallVector<StringRef> result;
    for (const MDOperand &MDOp : MD->operands()) {
      if (MDString *S = dyn_cast<MDString>(MDOp)) {
        result.push_back(S->getString());
      }
    }
    return result;
  }

# endif  // INCLUDE_PARSE_MDNODE_HPP_

