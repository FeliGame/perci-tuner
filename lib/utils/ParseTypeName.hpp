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
    
    // class also count as StructType
    if (auto *ST = llvm::dyn_cast<llvm::StructType>(type)) {
        if (!ST->isLiteral()) {
            result += ST->getStructName().str();
        }
        result += "{";
        bool isFirst = true;
        for (llvm::Type *ElemTy : ST->elements()) {
            if (!isFirst) {
                result += ",";
            }
            result += parseTypeName(ElemTy);
            isFirst = false;
        }
        result += "}";
    } else if (auto *AT = llvm::dyn_cast<llvm::ArrayType>(type)) {
        result = parseTypeName(AT->getElementType()) + 
               "[" + std::to_string(AT->getNumElements()) + "]";
    } else if (llvm::dyn_cast<llvm::PointerType>(type)) {
        // TODO: can't parse pointer type
        result = "*";
    } else if (type->getTypeID() == Type::FP128TyID) {
        result = "long double";
    } else {
        // Other basic types
        llvm::raw_string_ostream stream(result);
        stream << *type;
        stream.flush();
    }

    return result;
}

inline string parseDITypeName(DIType* type) {
    if (!type) return "";

    if (auto *derivedType = llvm::dyn_cast<llvm::DIDerivedType>(type)) {
        switch (derivedType->getTag()) {
            case dwarf::DW_TAG_typedef: {
                return derivedType->getName().str();
            }
            case dwarf::DW_TAG_const_type: {
                if (auto baseType = derivedType->getBaseType())
                    return "const " + parseDITypeName(baseType);
                break;
            }
            case dwarf::DW_TAG_pointer_type: {
                if (auto baseType = derivedType->getBaseType())
                    return parseDITypeName(baseType) + "*";
                break;
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
            if (baseType->getSizeInBits() > 0) {
                auto arraySize = compositeType->getSizeInBits() / baseType->getSizeInBits();
                return parseDITypeName(baseType) + "["+ std::to_string(arraySize) + "]";
            }
        }
    }

    return type->getName().empty() ? "" : type->getName().str();
}

#endif // INCLUDE_PARSE_TYPENAME_HPP_