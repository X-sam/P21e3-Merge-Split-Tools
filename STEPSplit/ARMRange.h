//ARMRange.h - A templated wrapper class and related macros.
//Allows one to transparently traverse ARMCursors of any type without having to deal with casting and such.
//sample usage:
//for(auto i : ARM_RANGE(Workpiece,design))
//{ 
//	i->get_root_object();
//}
//This loop will traverse every ARM Workpiece in the RoseDesign design.
//Much more succinct than other ways of doing it.
//
//Composed by Samson, adapted from the similar RoseRange tool, written by Nathan West. Thanks Nate!
//
//6/27/2014

#include <ARM.h>
#include <iterator>
#include "classmappy.h"

#pragma once

template<class T>
class ARMRange
{
	RoseDesign* design;
	RoseManagerType type;

public:
	ARMRange(RoseDesign* design, RoseManagerType type) :
		design(design),
		type(type)
	{}

	//C++ style iterator around a RoseCursor
	class ARMIterator :
		public std::iterator<std::bidirectional_iterator_tag, T>
	{
		ARMCursor cursor;
		ARMRange* range;
		T* current;
		ARMCastPtr fp;
		//ITERATOR IMPLEMENTATION
		//Modeled after boost::iterator_facade
		//http://www.boost.org/doc/libs/1_55_0/libs/iterator/doc/iterator_facade.html

		//Get the current ARMObject
		T& dereference() const
		{
			return *current;
		}

		//Compare to another iterator
		bool equal(const ARMIterator& rhs) const
		{
			return current == rhs.current;
		}

		//Cast the ARMObject to the underlying type. 
		T* iter_ARM_cast(ARMObject* object)
		{
			return static_cast<T*> (fp(object));
		}

		//Increment iterator
		void increment()
		{
			current = iter_ARM_cast(cursor.next());
		}

		//Decrement iterator
		void decrement()
		{
			current = iter_ARM_cast(cursor.previous());
		}

	public:
		//Begin constructor
		ARMIterator(ARMRange* range) :
			range(range)
		{
			//Set up domain and traversal
			cursor.traverse(range->design);
			cursor.domain(range->type);


			T *dumy = T::newInstance(rose_trash());
			//We need to lookup the ARMCast function for the class, so we check the lookup table using the class name to get a pointer to the corresponding ARMCastTo... function.
			fp = ARMCastLookupTable.at(dumy->getModuleName());
			//Need initial increment to get the first next()
			increment();
		}

		//End-of-range iterator is a null pointer
		ARMIterator() :
			range(0),
			current(0)
		{}

		//Public interface uses the private implementation methods

		//Comparison
		bool operator ==(const ARMIterator& rhs) const { return equal(rhs); }
		bool operator !=(const ARMIterator& rhs) const { return !equal(rhs); }

		//Prefix operators (++it, --it)
		ARMIterator& operator++() { increment(); return *this; }
		ARMIterator& operator--() { decrement(); return *this; }

		//Postfix operators (it++, it--)
		ARMIterator operator++(int)
		{
			ARMIterator tmp = *this;
			increment();
			return tmp;
		}

		ARMIterator operator--(int)
		{
			ARMIterator tmp = *this;
			decrement();
			return tmp;
		}

		//Pointer operators
		T& operator*() const { return dereference(); }
		T* operator->() const { return &dereference(); }

		//Direct access to the cursor. Could be useful.
		ARMCursor& get_cursor() { return cursor; }
		const ARMCursor& get_cursor() const { return cursor; }

		//Explicit conversion to pointer
		T* ptr() { return current; }
	};

	//Standard begin and end methods for getting iterators
	ARMIterator begin()
	{
		return ARMIterator(this);
	}

	ARMIterator end()
	{
		return ARMIterator();
	}

	template<class Predicate>
	ARMIterator find(Predicate&& predicate)
	{
		return std::find_if(begin(), end(), std::forward<Predicate>(predicate));
	}
};

//Macro to create a ARMRange, for use in new-style for loops or STL algorithms
#define ARM_RANGE(TYP, DESIGN) (ARMRange<TYP>((DESIGN), (TYP##::type())))

//Now we're getting somewhere. Macro to perform a search.
#define ARM_FIND(TYP, DESIGN, VAR, PRED) (ARM_RANGE(TYP, DESIGN).find([&](TYP & VAR) PRED))

//One more, to get the pointer
#define ARM_FIND_PTR(TYP, DESIGN, VAR, PRED) ((ARM_FIND(TYP, DESIGN, VAR, PRED)).ptr())
