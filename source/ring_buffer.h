#pragma once
#include <cstddef>
#include <atomic>

template<class _Ty, std::size_t _Size>
class RingBuffer
{
private:
	static_assert((_Size & (_Size - 1)) == 0, "_Size must be power of 2.");

	typedef _Ty value_type;
	typedef std::size_t size_type;
	
	alignas(64) std::atomic<size_type> _head{ 0 };
	alignas(64) std::atomic<size_type> _tail{ 0 };
	alignas(64) value_type _data[_Size]{};
	static constexpr size_type _mask = (_Size - 1);
public:
	inline bool push(const value_type& _Val)
	{
		size_type head = _head.load(std::memory_order_relaxed);
		size_type tail = _tail.load(std::memory_order_acquire);
		size_type next_head = (head + 1) & _mask;
		if (next_head == tail) return false; // is full

		_data[head] = _Val;
		_head.store(next_head, std::memory_order_release);
		return true;
	}

	inline bool pop(void)
	{
		size_type tail = _tail.load(std::memory_order_relaxed);
		size_type head = _head.load(std::memory_order_acquire);
		if (tail == head) return false; // is empty

		_tail.store((tail + 1) & _mask, std::memory_order_release);
		return true;
	}

	inline bool pop(value_type& _Val)
	{
		size_type tail = _tail.load(std::memory_order_relaxed);
		size_type head = _head.load(std::memory_order_acquire);
		if (tail == head) return false;

		_Val = _data[tail];
		_tail.store((tail + 1) & _mask, std::memory_order_release);
		return true;
	}

	inline size_type size()
	{
		size_type head = _head.load(std::memory_order_acquire);
		size_type tail = _tail.load(std::memory_order_acquire);

		if (head >= tail)
			return head - tail;
		else
			return _Size + head - tail;
	}

	inline bool empty()
	{
		size_type head = _head.load(std::memory_order_acquire);
		size_type tail = _tail.load(std::memory_order_acquire);
		return (head == tail);
	}

	inline bool full()
	{
		size_type head = _head.load(std::memory_order_acquire);
		size_type tail = _tail.load(std::memory_order_acquire);
		size_type next_head = (head + 1) & _mask;
		return (next_head == tail);
	}

	inline size_type buffer_size()
	{
		return _Size;
	}
};
