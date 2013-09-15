#pragma once

#include <city.h>
#include <openssl/md5.h>

#include <DB/Common/SipHash.h>
#include <DB/Common/Arena.h>
#include <DB/Core/Row.h>
#include <DB/Core/StringRef.h>
#include <DB/Columns/IColumn.h>
#include <DB/Interpreters/HashMap.h>


namespace DB
{


/// Для агрегации по SipHash.
struct UInt128
{
	UInt64 first;
	UInt64 second;

	bool operator== (const UInt128 rhs) const { return first == rhs.first && second == rhs.second; }
	bool operator!= (const UInt128 rhs) const { return first != rhs.first || second != rhs.second; }
};

struct UInt128Hash
{
	default_hash<UInt64> hash64;
	size_t operator()(UInt128 x) const { return hash64(hash64(x.first) ^ x.second); }
};

struct UInt128TrivialHash
{
	size_t operator()(UInt128 x) const { return x.first; }
};

struct UInt128ZeroTraits
{
	static inline bool check(UInt128 x) { return x.first == 0 && x.second == 0; }
	static inline void set(UInt128 & x) { x.first = 0; x.second = 0; }
};


/// Немного быстрее стандартного
struct StringHash
{
	size_t operator()(const String & x) const { return CityHash64(x.data(), x.size()); }
};


typedef std::vector<size_t> Sizes;


/// Записать набор ключей фиксированной длины в UInt128, уложив их подряд (при допущении, что они помещаются).
static inline UInt128 __attribute__((__always_inline__)) pack128(
	size_t i, size_t keys_size, const ConstColumnPlainPtrs & key_columns, const Sizes & key_sizes)
{
	union
	{
		UInt128 key;
		char bytes[16];
	};

	memset(bytes, 0, 16);
	size_t offset = 0;
	for (size_t j = 0; j < keys_size; ++j)
	{
		StringRef key_data = key_columns[j]->getDataAt(i);
		memcpy(bytes + offset, key_data.data, key_sizes[j]);
		offset += key_sizes[j];
	}

	return key;
}


/// Хэшировать набор ключей в UInt128.
static inline UInt128 __attribute__((__always_inline__)) hash128(
	size_t i, size_t keys_size, const ConstColumnPlainPtrs & key_columns, StringRefs & keys)
{
	UInt128 key;
	SipHash hash;

	for (size_t j = 0; j < keys_size; ++j)
	{
		/// Хэшируем ключ.
		keys[j] = key_columns[j]->getDataAtWithTerminatingZero(i);
		hash.update(keys[j].data, keys[j].size);
	}

    hash.get128(key.first, key.second);

	return key;
}


/// То же самое, но без возврата ссылок на данные ключей.
static inline UInt128 __attribute__((__always_inline__)) hash128(
	size_t i, size_t keys_size, const ConstColumnPlainPtrs & key_columns)
{
	UInt128 key;
	SipHash hash;

	for (size_t j = 0; j < keys_size; ++j)
	{
		/// Хэшируем ключ.
		StringRef key = key_columns[j]->getDataAtWithTerminatingZero(i);
		hash.update(key.data, key.size);
	}

    hash.get128(key.first, key.second);

	return key;
}


/// Скопировать ключи в пул. Потом разместить в пуле StringRef-ы на них и вернуть указатель на первый.
static inline StringRef * __attribute__((__always_inline__)) placeKeysInPool(
	size_t i, size_t keys_size, StringRefs & keys, Arena & pool)
{
	for (size_t j = 0; j < keys_size; ++j)
	{
		char * place = pool.alloc(keys[j].size);
		memcpy(place, keys[j].data, keys[j].size);
		keys[j].data = place;
	}

	/// Размещаем в пуле StringRef-ы на только что скопированные ключи.
	char * res = pool.alloc(keys_size * sizeof(StringRef));
	memcpy(res, &keys[0], keys_size * sizeof(StringRef));

	return reinterpret_cast<StringRef *>(res);
}


/// Скопировать ключи в пул. Потом разместить в пуле StringRef-ы на них и вернуть указатель на первый.
static inline StringRef * __attribute__((__always_inline__)) extractKeysAndPlaceInPool(
	size_t i, size_t keys_size, const ConstColumnPlainPtrs & key_columns, StringRefs & keys, Arena & pool)
{
	for (size_t j = 0; j < keys_size; ++j)
	{
		keys[j] = key_columns[j]->getDataAtWithTerminatingZero(i);
		char * place = pool.alloc(keys[j].size);
		memcpy(place, keys[j].data, keys[j].size);
		keys[j].data = place;
	}

	/// Размещаем в пуле StringRef-ы на только что скопированные ключи.
	char * res = pool.alloc(keys_size * sizeof(StringRef));
	memcpy(res, &keys[0], keys_size * sizeof(StringRef));

	return reinterpret_cast<StringRef *>(res);
}


}
