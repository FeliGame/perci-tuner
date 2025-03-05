// @Author: Guocheng - NUDT
// Usage: Parse Type & DIType into recognizable string.

#ifndef INCLUDE_PARSE_TYPENAME_HPP_
#define INCLUDE_PARSE_TYPENAME_HPP_

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/BinaryFormat/Dwarf.h>

using namespace llvm;
using namespace std;


inline string parseTypeName(Type* type) {
    string result;
    llvm::raw_string_ostream stream(result);
    // stream << *type;  // simply output complete type info
    
    // class also count as StructType
    if (auto *ST = llvm::dyn_cast<llvm::StructType>(type)) {
        if (!ST->isLiteral()) {
            stream << ST->getStructName().str();
        }
        stream << "{";
        bool isFirst = true;
        for (llvm::Type *ElemTy : ST->elements()) {
            if (!isFirst) {
                stream << ",";
            }
            std::string elemTypeName = parseTypeName(ElemTy);
            stream << elemTypeName;
            isFirst = false;
        }
        stream << "}";
    } else if (auto *AT = llvm::dyn_cast<llvm::ArrayType>(type)) {
        std::string elemTypeName = parseTypeName(AT->getElementType());
        stream << elemTypeName << "[" << AT->getNumElements() << "]";
    } else if (llvm::dyn_cast<llvm::PointerType>(type)) {
        // All pointers are opaque
        stream << "*";
    } else {
        stream << *type;
    }

    stream.flush();
    return result;
}

inline string parseDITypeName(DIType* type) {
    if (auto *derivedType = llvm::dyn_cast<llvm::DIDerivedType>(type)) {
        switch (derivedType->getTag()) {
            case dwarf::DW_TAG_typedef: {
                return derivedType->getName().str();
            }
            case dwarf::DW_TAG_const_type: {
                return "const " + parseDITypeName(derivedType->getBaseType());
            }
            case dwarf::DW_TAG_pointer_type: {
                return parseDITypeName(derivedType->getBaseType()) + "*";
            }
            default: {
                if (auto baseType = derivedType->getBaseType()) {
                    return parseDITypeName(baseType);
                }
            }
        }
    }
    if (auto *compositeType = llvm::dyn_cast<llvm::DICompositeType>(type)) {
        if (auto baseType = compositeType->getBaseType()) {
            auto arraySize = compositeType->getSizeInBits() / baseType->getSizeInBits();
            return parseDITypeName(baseType) + "["+ std::to_string(arraySize) + "]";
        }
    }

    return type->getName().str();
}

#endif // INCLUDE_PARSE_TYPENAME_HPP_