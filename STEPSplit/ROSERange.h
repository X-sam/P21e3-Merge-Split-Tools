//Wrapper class for RoseCursor that implements C++ style iterators.
//Courtesy of Nathan West.
#pragma once

#include <iostream>
#include <iterator>
#include <string>

#include <rose.h>

template<class T>
class RoseRange
{
	RoseDesign* design;
	RoseTypePtr type;

public:
	RoseRange(RoseDesign* design, RoseTypePtr type) :
		design(design),
		type(type)
	{}

	//C++ style iterator around a RoseCursor
	class RoseIterator :
		public std::iterator<std::bidirectional_iterator_tag, T>
	{
		RoseCursor cursor;
		RoseRange* range;
		T* current;

		//ITERATOR IMPLEMENTATION
		//Modeled after boost::iterator_facade
		//http://www.boost.org/doc/libs/1_55_0/libs/iterator/doc/iterator_facade.html

		//Get the current RoseObject
		T& dereference() const
		{
			return *current;
		}

		//Compare to another iterator
		bool equal(const RoseIterator& rhs) const
		{
			return current == rhs.current;
		}

		//This is what ROSE_CAST does under the hood. We cant use ROSE_CAST without the literal type name.
		T* iter_rose_cast(RoseObject* object)
		{
			return static_cast<T*>(rose_cast(object, range->type));
		}

		//Increment iterator
		void increment()
		{
			current = iter_rose_cast(cursor.next());
		}

		//Decrement iterator
		void decrement()
		{
			current = iter_rose_cast(cursor.previous());
		}

	public:
		//Begin constructor
		RoseIterator(RoseRange* range) :
			range(range)
		{
			//Set up domain and traversal
			cursor.traverse(range->design);
			cursor.domain(range->type->domain());

			//Need initial increment to get the first next()
			increment();
		}

		//End-of-range iterator is a null pointer
		RoseIterator() :
			range(0),
			current(0)
		{}

		//Public interface uses the private implementation methods

		//Comparison
		bool operator ==(const RoseIterator& rhs) const { return equal(rhs); }
		bool operator !=(const RoseIterator& rhs) const { return !equal(rhs); }

		//Prefix operators (++it, --it)
		RoseIterator& operator++() { increment(); return *this; }
		RoseIterator& operator--() { decrement(); return *this; }

		//Postfix operators (it++, it--)
		RoseIterator operator++(int)
		{
			RoseIterator tmp = *this;
			increment();
			return tmp;
		}

		RoseIterator operator--(int)
		{
			RoseIterator tmp = *this;
			decrement();
			return tmp;
		}

		//Pointer operators
		T& operator*() const { return dereference(); }
		T* operator->() const { return &dereference(); }

		//Direct access to the cursor. Could be useful.
		RoseCursor& get_cursor() { return cursor; }
		const RoseCursor& get_cursor() const { return cursor; }

		//Explicit conversion to pointer
		T* ptr() { return current; }
	};

	//Standard begin and end methods for getting iterators
	RoseIterator begin()
	{
		return RoseIterator(this);
	}

	RoseIterator end()
	{
		return RoseIterator();
	}

};

//Macro to create a RoseRange, for use in new-style for loops or STL algorithms
#define ROSE_RANGE(TYP, DESIGN) (RoseRange<TYP>((DESIGN), (ROSE_TYPE(TYP))))
