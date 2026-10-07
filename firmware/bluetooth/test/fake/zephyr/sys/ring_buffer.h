#pragma once
#include <cstdint>
#include <cstddef>
struct ring_buf
{
	std::uint8_t *storage = nullptr;
	std::uint32_t capacity = 0;
	std::uint32_t head = 0;
	std::uint32_t count = 0;
};
inline void ring_buf_init(ring_buf *b, std::uint32_t size, std::uint8_t *storage)
{
	*b = {storage, size, 0, 0};
}
inline void ring_buf_reset(ring_buf *b)
{
	b->head = 0;
	b->count = 0;
}
inline std::uint32_t ring_buf_put(ring_buf *b, const std::uint8_t *data, std::uint32_t size)
{
	std::uint32_t copied = 0;
	while (copied < size && b->count < b->capacity)
	{
		b->storage[(b->head + b->count) % b->capacity] = data[copied++];
		++b->count;
	}
	return copied;
}
inline std::uint32_t ring_buf_get(ring_buf *b, std::uint8_t *data, std::uint32_t size)
{
	std::uint32_t copied = 0;
	while (copied < size && b->count != 0)
	{
		data[copied++] = b->storage[b->head];
		b->head = (b->head + 1) % b->capacity;
		--b->count;
	}
	return copied;
}
