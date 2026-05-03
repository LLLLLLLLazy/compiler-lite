///
/// @file ArrayType.cpp
/// @brief 数组类型描述类
///

#include "ArrayType.h"

/// @brief 计算数组类型的哈希值
/// @param type 数组类型对象
/// @return 哈希值
size_t ArrayType::ArrayTypeHasher::operator()(const ArrayType & type) const noexcept
{
    size_t h1 = std::hash<const Type *>{}(type.getElementType());
    size_t h2 = std::hash<int32_t>{}(type.getNumElements());
    return h1 ^ (h2 << 1U);
}

/// @brief 判断两个数组类型是否相等
/// @param lhs 左侧数组类型
/// @param rhs 右侧数组类型
/// @return 是否相等
bool ArrayType::ArrayTypeEqual::operator()(const ArrayType & lhs, const ArrayType & rhs) const noexcept
{
    return lhs.getElementType() == rhs.getElementType() &&
           lhs.getNumElements() == rhs.getNumElements();
}

/// @brief 构造数组类型
/// @param elementType 元素类型
/// @param numElements 元素个数
ArrayType::ArrayType(Type * elementType, int32_t numElements)
    : Type(ArrayTyID), elementType(elementType), numElements(numElements)
{}

/// @brief 获取元素类型
/// @return 元素类型
Type * ArrayType::getElementType() const
{
    return elementType;
}

/// @brief 获取元素个数
/// @return 元素个数
int32_t ArrayType::getNumElements() const
{
    return numElements;
}

/// @brief 获取数组类型所占空间大小
/// @return 空间大小
int32_t ArrayType::getSize() const
{
    return elementType->getSize() * numElements;
}

/// @brief 获取唯一数组类型对象
/// @param elementType 元素类型
/// @param numElements 元素个数
/// @return 数组类型对象
ArrayType * ArrayType::get(Type * elementType, int32_t numElements)
{
    static StorageSet<ArrayType, ArrayTypeHasher, ArrayTypeEqual> storageSet;
    return const_cast<ArrayType *>(storageSet.get(elementType, numElements));
}

/// @brief 转为 IR 文本形式
/// @return 数组类型字符串
std::string ArrayType::toString() const
{
    return "[" + std::to_string(numElements) + " x " + elementType->toString() + "]";
}
