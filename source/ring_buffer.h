#pragma once
#include <cstddef>

template<class _Ty, std::size_t _Size>
class RingBuffer
{
private:
	static_assert((_Size & (_Size - 1)) == 0, "_Size must be power of 2.");

	typedef _Ty value_type;
	typedef std::size_t size_type;
	value_type _data[_Size]{};
	size_type _tail{ 0 };
	size_type _head{ 0 };
	size_type _size{ 0 };
	static constexpr size_type _mask = (_Size - 1);
public:
	inline bool push(const value_type& _Val)
	{
		if (full()) return false;

		_data[_head] = _Val;
		_head = (_head + 1) & _mask;
		_size++;
		return true;
	}

	inline bool pop(void)
	{
		if (empty()) return false;

		_tail = (_tail + 1) & _mask;
		_size--;
		return true;
	}

	inline bool pop(value_type& _Val)
	{
		if (empty()) return false;

		_Val = _data[_tail];
		_tail = (_tail + 1) & _mask;
		_size--;
		return true;
	}

	inline size_type size()
	{
		return (_size);
	}

	inline bool empty()
	{
		return (_size == 0);
	}

	inline bool full()
	{
		return (_size == buffer_size());
	}

	inline size_type buffer_size()
	{
		return _Size;
	}
};
