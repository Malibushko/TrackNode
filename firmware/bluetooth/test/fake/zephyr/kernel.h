#pragma once
#include <cstdint>
#include <functional>
#include <algorithm>
#include <cerrno>
#include <cstring>
using k_timeout_t = std::int64_t;
using k_timepoint_t = std::int64_t;
inline std::int64_t simulated_time = 0;
inline std::function<void()> pump;
inline constexpr k_timeout_t K_MSEC(std::uint32_t ms)
{
	return ms;
}
struct k_spinlock {};
inline int k_spin_lock(k_spinlock *)
{
	return 0;
}
inline void k_spin_unlock(k_spinlock *, int) {}
struct k_sem
{
	unsigned count = 0;
	unsigned limit = 1;
};
inline int k_sem_init(k_sem *s, unsigned count, unsigned limit)
{
	s->count = count;
	s->limit = limit;
	return 0;
}
inline void k_sem_reset(k_sem *s)
{
	s->count = 0;
}
inline void k_sem_give(k_sem *s)
{
	s->count = std::min(s->count + 1, s->limit);
}
inline int k_sem_take(k_sem *s, k_timeout_t timeout)
{
	const auto deadline = simulated_time + timeout;
	while (s->count == 0 && simulated_time < deadline)
	{
		if (pump)
		{
			pump();
		}
		++simulated_time;
	}
	if (s->count != 0)
	{
		--s->count;
		return 0;
	}
	return -EAGAIN;
}
struct k_mutex {};
inline int k_mutex_init(k_mutex *)
{
	return 0;
}
inline int k_mutex_lock(k_mutex *, k_timeout_t)
{
	return 0;
}
inline int k_mutex_unlock(k_mutex *)
{
	return 0;
}

inline constexpr k_timeout_t K_NO_WAIT = 0;
struct k_msgq
{
	char *storage = nullptr;
	std::size_t item_size = 0;
	std::uint32_t capacity = 0;
	std::uint32_t head = 0;
	std::uint32_t count = 0;
};
inline void k_msgq_init(k_msgq *q, char *storage, std::size_t size, std::uint32_t capacity)
{
	*q = {storage, size, capacity, 0, 0};
}
inline int k_msgq_put(k_msgq *q, const void *item, k_timeout_t)
{
	if (q->count == q->capacity)
	{
		return -ENOMSG;
	}
	std::memcpy(q->storage + ((q->head + q->count) % q->capacity) * q->item_size, item, q->item_size);
	++q->count;
	return 0;
}
inline int k_msgq_get(k_msgq *q, void *item, k_timeout_t)
{
	if (q->count == 0)
	{
		return -ENOMSG;
	}
	std::memcpy(item, q->storage + q->head * q->item_size, q->item_size);
	q->head = (q->head + 1) % q->capacity;
	--q->count;
	return 0;
}
inline void k_msgq_purge(k_msgq *q)
{
	q->head = 0;
	q->count = 0;
}
