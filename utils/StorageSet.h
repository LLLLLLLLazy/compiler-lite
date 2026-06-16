///
/// @file StorageSet.h
/// @brief 存储集合类
/// 

#pragma once

#include <unordered_set>

template <typename T, typename Hasher, typename Equal>
class StorageSet final {
	std::unordered_set<T, Hasher, Equal> mStorage;

public:
	template <typename... Args>
	const T * get(Args &&... args)
	{
		return &*mStorage.emplace(std::forward<Args>(args)...).first;
	}
};
