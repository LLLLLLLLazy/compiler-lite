///
/// @file VectorType.cpp
/// @brief RVV 可伸缩向量类型实现
///

#include "VectorType.h"

size_t VectorType::VectorTypeHasher::operator()(const VectorType & type) const noexcept
{
    return std::hash<const Type *>{}(type.getElementType());
}

bool VectorType::VectorTypeEqual::operator()(const VectorType & lhs, const VectorType & rhs) const noexcept
{
    return lhs.getElementType() == rhs.getElementType();
}

VectorType::VectorType(Type * elementType)
    : Type(VectorTyID), elementType(elementType)
{}

VectorType * VectorType::get(Type * elementType)
{
    static StorageSet<VectorType, VectorTypeHasher, VectorTypeEqual> storageSet;
    return const_cast<VectorType *>(storageSet.get(elementType));
}

std::string VectorType::toString() const
{
    return "<vscale x ? x " + (elementType ? elementType->toString() : std::string("void")) + ">";
}
