///
/// @file PointerType.cpp
/// @brief 指针类型描述类
///

#include "PointerType.h"

/// @brief 计算指针类型的哈希值
/// @param type 指针类型对象
/// @return 哈希值
size_t PointerType::PointerTypeHasher::operator()(const PointerType & type) const noexcept
{
    return std::hash<const Type *>{}(type.getPointeeType());
}

/// @brief 判断两个指针类型是否相等
/// @param lhs 左侧指针类型
/// @param rhs 右侧指针类型
/// @return 是否相等
size_t PointerType::PointerTypeEqual::operator()(const PointerType & lhs, const PointerType & rhs) const noexcept
{
    return lhs.getPointeeType() == rhs.getPointeeType();
}

/// @brief 构造指针类型
/// @param pointeeType 指向的目标类型
PointerType::PointerType(const Type * pointeeType) : Type(PointerTyID)
{
    this->pointeeType = pointeeType;

    if (pointeeType->isPointerType()) {
        Instanceof(pType, const PointerType *, pointeeType);
        this->rootType = pType->getRootType();
        this->depth = pType->getDepth() + 1;
    } else {
        this->rootType = pointeeType;
        this->depth = 1;
    }
}

/// @brief 获取根类型
/// @return 根类型
const Type * PointerType::getRootType() const
{
    return rootType;
}

/// @brief 获取指向的目标类型
/// @return 目标类型
const Type * PointerType::getPointeeType() const
{
    return pointeeType;
}

/// @brief 获取指针深度
/// @return 指针深度
int32_t PointerType::getDepth() const
{
    return depth;
}

/// @brief 获取唯一指针类型对象
/// @param pointee 指向的目标类型
/// @return 指针类型对象
const PointerType * PointerType::get(Type * pointee)
{
    static StorageSet<PointerType, PointerTypeHasher, PointerTypeEqual> storageSet;
    return storageSet.get(pointee);
}

/// @brief 获取指针类型所占空间大小
/// @return 空间大小
int32_t PointerType::getSize() const
{
    return 8;
}

/// @brief 转为 IR 文本形式
/// @return 指针类型字符串
std::string PointerType::toString() const
{
    return pointeeType->toString() + "*";
}
