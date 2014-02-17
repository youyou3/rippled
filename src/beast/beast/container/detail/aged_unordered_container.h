//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_CONTAINER_AGED_UNORDERED_CONTAINER_H_INCLUDED
#define BEAST_CONTAINER_AGED_UNORDERED_CONTAINER_H_INCLUDED

#include "aged_container_iterator.h"
#include "aged_associative_container.h"

#include "../../chrono/abstract_clock.h"
#include "../../equal.h"
#include "../../equal_to.h"
#include "../../is_constructible.h"
#include "../../utility/empty_base_optimization.h"

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

/*

TODO

- Add constructor variations that take a bucket count

*/

namespace beast {
namespace detail {

/** Associative container where each element is also indexed by time.

    This container mirrors the interface of the standard library unordered
    associative containers, with the addition that each element is associated
    with a `when` `time_point` which is obtained from the value of the clock's
    `now`. The function `touch` updates the time for an element to the current
    time as reported by the clock.

    An extra set of iterator types and member functions are provided in the
    `chronological` memberspace that allow traversal in temporal or reverse
    temporal order. This container is useful as a building block for caches
    whose items expire after a certain amount of time. The chronological
    iterators allow for fully customizable expiration strategies.

    @see aged_unordered_set, aged_unordered_multiset
    @see aged_unordered_map, aged_unordered_multimap
*/
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Duration = std::chrono::seconds,
    class Hash = std::hash <Key>,
    class KeyEqual = std::equal_to <Key>,
    class Allocator = std::allocator <
        typename std::conditional <IsMap,
            std::pair <Key const, T>,
                Key>::type>
>
class aged_unordered_container
{
public:
    typedef abstract_clock <Duration> clock_type;
    typedef typename clock_type::time_point time_point;
    typedef typename clock_type::duration duration;
    typedef Key key_type;
    typedef T mapped_type;
    typedef typename std::conditional <IsMap,
        std::pair <Key const, T>,
        Key>::type value_type;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    // Introspection (for unit tests)
    typedef std::true_type is_unordered;
    typedef std::integral_constant <bool, IsMulti> is_multi;
    typedef std::integral_constant <bool, IsMap> is_map;

    // VFALCO TODO How can we reorder the declarations to keep
    //             all the public things together contiguously?

private:
    static Key const& extract (value_type const& value)
    {
        return aged_associative_container_extract_t <IsMap> () (value);
    }

    // VFALCO TODO hoist to remove template argument dependencies
    struct element
        : boost::intrusive::unordered_set_base_hook <
            boost::intrusive::link_mode <
                boost::intrusive::normal_link>
            >
        , boost::intrusive::list_base_hook <
            boost::intrusive::link_mode <
                boost::intrusive::normal_link>
            >
    {
        // Stash types here so the iterator doesn't
        // need to see the container declaration.
        struct stashed
        {
            typedef typename aged_unordered_container::value_type value_type;
            typedef typename aged_unordered_container::time_point time_point;
        };

        element (
            time_point const& when_,
            value_type const& value_)
            : value (value_)
            , when (when_)
        {
        }

        element (
            time_point const& when_,
            value_type&& value_)
            : value (std::move (value_))
            , when (when_)
        {
        }

        template <
            class... Args,
            class = typename std::enable_if <
                std::is_constructible <value_type,
                    Args...>::value>::type
        >
        element (time_point const& when_, Args&&... args)
            : value (std::forward <Args> (args)...)
            , when (when_)
        {
        }

        value_type value;
        time_point when;
    };

    // VFALCO TODO hoist to remove template argument dependencies
    class ValueHash
        : private empty_base_optimization <Hash>
        , public std::unary_function <element, std::size_t>
    {
    public:
        ValueHash ()
        {
        }

        ValueHash (Hash const& hash)
            : empty_base_optimization <Hash> (hash)
        {
        }

        std::size_t operator() (element const& e) const
        {
            return this->member() (extract (e.value));
        }
  
        Hash& hash_function()
        {
            return this->member();
        }

        Hash const& hash_function() const
        {
            return this->member();
        }
    };

    // Compares value_type against element, used in find/insert_check
    // VFALCO TODO hoist to remove template argument dependencies
    class KeyValueEqual
        : private empty_base_optimization <KeyEqual>
        , public std::binary_function <Key, element, bool>
    {
    public:
        KeyValueEqual ()
        {
        }

        KeyValueEqual (KeyEqual const& keyEqual)
            : empty_base_optimization <KeyEqual> (keyEqual)
        {
        }

        // VFALCO NOTE WE might want only to enable these overloads
        //                if Compare has is_transparent
#if 0
        template <class K>
        bool operator() (K const& k, element const& e) const
        {
            return this->member() (k, extract (e.value));
        }
        
        template <class K>
        bool operator() (element const& e, K const& k) const
        {
            return this->member() (extract (e.value), k);
        }
#endif

        bool operator() (Key const& k, element const& e) const
        {
            return this->member() (k, extract (e.value));
        }
  
        bool operator() (element const& e, Key const& k) const
        {
            return this->member() (extract (e.value), k);
        }

        bool operator() (element const& lhs, element const& rhs) const
        {
            return this->member() (extract (lhs.value), extract (rhs.value));
        }

        KeyEqual& key_eq()
        {
            return this->member();
        }

        KeyEqual const& key_eq() const
        {
            return this->member();
        }
    };

    typedef typename boost::intrusive::make_list <element,
        boost::intrusive::constant_time_size <false>
            >::type list_type;

    typedef typename std::conditional <
        IsMulti,
        typename boost::intrusive::make_unordered_multiset <element,
            boost::intrusive::constant_time_size <true>,
            boost::intrusive::hash <ValueHash>,
            boost::intrusive::equal <KeyValueEqual>,
            boost::intrusive::cache_begin <true>
                    >::type,
        typename boost::intrusive::make_unordered_set <element,
            boost::intrusive::constant_time_size <true>,
            boost::intrusive::hash <ValueHash>,
            boost::intrusive::equal <KeyValueEqual>,
            boost::intrusive::cache_begin <true>
                    >::type
        >::type cont_type;

    typedef typename cont_type::bucket_type bucket_type;
    typedef typename cont_type::bucket_traits bucket_traits;

    typedef typename std::allocator_traits <
        Allocator>::template rebind_alloc <element> ElementAllocator;

    using ElementAllocatorTraits = std::allocator_traits <ElementAllocator>;

    typedef typename std::allocator_traits <
        Allocator>::template rebind_alloc <element> BucketAllocator;

    using BucketAllocatorTraits = std::allocator_traits <BucketAllocator>;

    class config_t
        : private ValueHash
        , private KeyValueEqual
        , private empty_base_optimization <ElementAllocator>
    {
    public:
        explicit config_t (
            clock_type& clock_)
            : clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            Hash const& hash)
            : ValueHash (hash)
            , clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            KeyEqual const& keyEqual)
            : KeyValueEqual (keyEqual)
            , clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            Allocator const& alloc_)
            : empty_base_optimization <ElementAllocator> (alloc_)
            , clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            Hash const& hash,
            KeyEqual const& keyEqual)
            : ValueHash (hash)
            , KeyValueEqual (keyEqual)
            , clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            Hash const& hash,
            Allocator const& alloc_)
            : ValueHash (hash)
            , empty_base_optimization <ElementAllocator> (alloc_)
            , clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            KeyEqual const& keyEqual,
            Allocator const& alloc_)
            : KeyValueEqual (keyEqual)
            , empty_base_optimization <ElementAllocator> (alloc_)
            , clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            Hash const& hash,
            KeyEqual const& keyEqual,
            Allocator const& alloc_)
            : ValueHash (hash)
            , KeyValueEqual (keyEqual)
            , empty_base_optimization <ElementAllocator> (alloc_)
            , clock (clock_)
        {
        }

        config_t (config_t const& other)
            : ValueHash (other.hash_function())
            , KeyValueEqual (other.key_eq())
            , empty_base_optimization <ElementAllocator> (
                ElementAllocatorTraits::
                    select_on_container_copy_construction (
                        other.alloc()))
            , clock (other.clock)
        {
        }

        config_t (config_t const& other, Allocator const& alloc)
            : ValueHash (other.hash_function())
            , KeyValueEqual (other.key_eq())
            , empty_base_optimization <ElementAllocator> (alloc)
            , clock (other.clock)
        {
        }

        config_t (config_t&& other)
            : ValueHash (std::move (other.hash_function()))
            , KeyValueEqual (std::move (other.key_eq()))
            , empty_base_optimization <ElementAllocator> (
                std::move (other.alloc()))
            , clock (other.clock)
        {
        }

        config_t (config_t&& other, Allocator const& alloc)
            : ValueHash (std::move (other.hash_function()))
            , KeyValueEqual (std::move (other.key_eq()))
            , empty_base_optimization <ElementAllocator> (alloc)
            , clock (other.clock)
        {
        }

        config_t& operator= (config_t const& other)
        {
            hash_function() = other.hash_function();
            key_eq() = other.key_eq();
            alloc() = other.alloc();
            clock = other.clock;
            return *this;
        }

        config_t& operator= (config_t&& other)
        {
            hash_function() = std::move (other.hash_function());
            key_eq() = std::move (other.key_eq());
            alloc() = std::move (other.alloc());
            clock = other.clock;
            return *this;
        }

        ValueHash& value_hash()
        {
            return *this;
        }

        ValueHash const& value_hash() const
        {
            return *this;
        }

        Hash& hash_function()
        {
            return ValueHash::hash_function();
        }

        Hash const& hash_function() const
        {
            return ValueHash::hash_function();
        }

        KeyValueEqual& key_value_equal()
        {
            return *this;
        }

        KeyValueEqual const& key_value_equal() const
        {
            return *this;
        }

        KeyEqual& key_eq()
        {
            return key_value_equal().key_eq();
        }

        KeyEqual const& key_eq() const
        {
            return key_value_equal().key_eq();
        }

        ElementAllocator& alloc()
        {
            return empty_base_optimization <
                ElementAllocator>::member();
        }

        ElementAllocator const& alloc() const
        {
            return empty_base_optimization <
                ElementAllocator>::member();
        }

        std::reference_wrapper <clock_type> clock;
    };

    class Buckets
    {
    public:
        typedef std::vector <
            bucket_type,
            typename std::allocator_traits <Allocator>::
                template rebind_alloc <bucket_type>> vec_type;

        Buckets ()
            : m_max_load_factor (1.f)
            , m_vec ()
        {
            m_vec.resize (
                cont_type::suggested_upper_bucket_count (0));
        }

        Buckets (Allocator const& alloc)
            : m_max_load_factor (1.f)
            , m_vec (alloc)
        {
            m_vec.resize (
                cont_type::suggested_upper_bucket_count (0));
        }

        operator bucket_traits()
        {
            return bucket_traits (&m_vec[0], m_vec.size());
        }

        void clear()
        {
            m_vec.clear();
        }

        size_type max_bucket_count() const
        {
            return m_vec.max_size();
        }

        float& max_load_factor()
        {
            return m_max_load_factor;
        }

        float const& max_load_factor() const
        {
            return m_max_load_factor;
        }

        // count is the number of buckets
        template <class Container>
        void rehash (size_type count, Container& c)
        {
            size_type const size (m_vec.size());
            if (count == size)
                return;
            if (count > m_vec.capacity())
            {
                // Need two vectors otherwise we
                // will destroy non-empty buckets.
                vec_type vec (m_vec.get_allocator());
                std::swap (m_vec, vec);
                m_vec.resize (count);
                c.rehash (bucket_traits (
                    &m_vec[0], m_vec.size()));
                return;
            }
            // Rehash in place.
            if (count > size)
            {
                // This should not reallocate since
                // we checked capacity earlier.
                m_vec.resize (count);
                c.rehash (bucket_traits (
                    &m_vec[0], count));
                return;
            }
            // Resize must happen after rehash otherwise
            // we might destroy non-empty buckets.
            c.rehash (bucket_traits (
                &m_vec[0], count));
            m_vec.resize (count);
        }

        // Resize the buckets to accomodate at least n items.
        template <class Container>
        void resize (size_type n, Container& c)
        {
            size_type const suggested (
                cont_type::suggested_upper_bucket_count (n));
            rehash (suggested, c);
        }

    private:
        float m_max_load_factor;
        vec_type m_vec;
    };

    template <class... Args>
    element* new_element (Args&&... args)
    {
        element* const p (
            ElementAllocatorTraits::allocate (m_config.alloc(), 1));
        ElementAllocatorTraits::construct (m_config.alloc(),
            p, clock().now(), std::forward <Args> (args)...);
        return p;
    }

    void delete_element (element* p)
    {
        ElementAllocatorTraits::destroy (m_config.alloc(), p);
        ElementAllocatorTraits::deallocate (m_config.alloc(), p, 1);
    }

    void unlink_and_delete_element (element* p)
    {
        chronological.list.erase (
            chronological.list.iterator_to (*p));
        m_cont.erase (m_cont.iterator_to (*p));
        delete_element (p);
    }

public:
    typedef Hash hasher;
    typedef KeyEqual key_equal;
    typedef Allocator allocator_type;
    typedef value_type& reference;
    typedef value_type const& const_reference;
    typedef typename std::allocator_traits <
        Allocator>::pointer pointer;
    typedef typename std::allocator_traits <
        Allocator>::const_pointer const_pointer;

    typedef detail::aged_container_iterator <false,
        typename cont_type::iterator> iterator;
    typedef detail::aged_container_iterator <true,
        typename cont_type::iterator> const_iterator;

    typedef detail::aged_container_iterator <false,
        typename cont_type::local_iterator> local_iterator;
    typedef detail::aged_container_iterator <true,
        typename cont_type::local_iterator> const_local_iterator;

    //--------------------------------------------------------------------------
    //
    // Chronological ordered iterators
    //
    // "Memberspace"
    // http://accu.org/index.php/journals/1527
    //
    //--------------------------------------------------------------------------

    class chronological_t
    {
    public:
        typedef detail::aged_container_iterator <false,
            typename list_type::iterator> iterator;
        typedef detail::aged_container_iterator <true,
            typename list_type::iterator> const_iterator;
        typedef detail::aged_container_iterator <false,
            typename list_type::reverse_iterator> reverse_iterator;
        typedef detail::aged_container_iterator <true,
            typename list_type::reverse_iterator> const_reverse_iterator;

        iterator begin ()
         {
            return iterator (list.begin());
        }

        const_iterator begin () const
        {
            return const_iterator (list.begin ());
        }

        const_iterator cbegin() const
        {
            return const_iterator (list.begin ());
        }

        iterator end ()
        {
            return iterator (list.end ());
        }

        const_iterator end () const
        {
            return const_iterator (list.end ());
        }

        const_iterator cend () const
        {
            return const_iterator (list.end ());
        }

        reverse_iterator rbegin ()
        {
            return reverse_iterator (list.rbegin());
        }

        const_reverse_iterator rbegin () const
        {
            return const_reverse_iterator (list.rbegin ());
        }

        const_reverse_iterator crbegin() const
        {
            return const_reverse_iterator (list.rbegin ());
        }

        reverse_iterator rend ()
        {
            return reverse_iterator (list.rend ());
        }

        const_reverse_iterator rend () const
        {
            return const_reverse_iterator (list.rend ());
        }

        const_reverse_iterator crend () const
        {
            return const_reverse_iterator (list.rend ());
        }

        iterator iterator_to (value_type& value)
        {
            static_assert (std::is_standard_layout <element>::value,
                "must be standard layout");
            return list.iterator_to (*reinterpret_cast <element*>(
                 reinterpret_cast<uint8_t*>(&value)-((std::size_t)
                    std::addressof(((element*)0)->member))));
        }

        const_iterator iterator_to (value_type const& value) const
        {
            static_assert (std::is_standard_layout <element>::value,
                "must be standard layout");
            return list.iterator_to (*reinterpret_cast <element const*>(
                 reinterpret_cast<uint8_t const*>(&value)-((std::size_t)
                    std::addressof(((element*)0)->member))));
        }

    private:
        chronological_t ()
        {
        }

        chronological_t (chronological_t const&) = delete;
        chronological_t (chronological_t&&) = delete;

        friend class aged_unordered_container;
        list_type mutable list;
    } chronological;

    //--------------------------------------------------------------------------
    //
    // Construction
    //
    //--------------------------------------------------------------------------

    explicit aged_unordered_container (
        clock_type& clock)
        : m_config (clock)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
    }

    aged_unordered_container (
        clock_type& clock,
        Hash const& hash)
        : m_config (clock, hash)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
    }

    aged_unordered_container (
        clock_type& clock,
        KeyEqual const& key_eq)
        : m_config (clock, key_eq)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
    }

    aged_unordered_container (
        clock_type& clock,
        Allocator const& alloc)
        : m_config (clock, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
    }

    aged_unordered_container (
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& key_eq)
        : m_config (clock, hash, key_eq)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
    }

    aged_unordered_container (
        clock_type& clock,
        Hash const& hash,
        Allocator const& alloc)
        : m_config (clock, hash, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
    }

    aged_unordered_container (
        clock_type& clock,
        KeyEqual const& key_eq,
        Allocator const& alloc)
        : m_config (clock, key_eq, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
    }

    aged_unordered_container (
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& key_eq,
        Allocator const& alloc)
        : m_config (clock, hash, key_eq, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
    }

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock)
        : m_config (clock)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (first, last);
    }

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock,
        Hash const& hash)
        : m_config (clock, hash)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (first, last);
    }

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock,
        KeyEqual const& key_eq)
        : m_config (clock, key_eq)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (first, last);
    }

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock,
        Allocator const& alloc)
        : m_config (clock, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (first, last);
    }

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& key_eq)
        : m_config (clock, hash, key_eq)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (first, last);
    }

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock,
        Hash const& hash,
        Allocator const& alloc)
        : m_config (clock, hash, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (first, last);
    }

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock,
        KeyEqual const& key_eq,
        Allocator const& alloc)
        : m_config (clock, key_eq, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (first, last);
    }

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& key_eq,
        Allocator const& alloc)
        : m_config (clock, hash, key_eq, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (first, last);
    }

    aged_unordered_container (aged_unordered_container const& other)
        : m_config (other.m_config)
        , m_buck (m_config.alloc())
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (other.cbegin(), other.cend());
    }

    aged_unordered_container (aged_unordered_container const& other,
        Allocator const& alloc)
        : m_config (other.m_config, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (other.cbegin(), other.cend());
    }

    aged_unordered_container (aged_unordered_container&& other)
        : m_config (std::move (other.m_config))
        , m_buck (m_config.alloc())
        , m_cont (std::move (other.m_cont))
    {
        chronological.list = std::move (other.chronological.list);
        assert (other.size() == 0);
    }

    aged_unordered_container (aged_unordered_container&& other,
        Allocator const& alloc)
        : m_config (std::move (other.m_config), alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (other.cbegin(), other.cend());
        other.clear ();
    }

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock)
        : m_config (clock)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (init.begin(), init.end());
    }

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock,
        Hash const& hash)
        : m_config (clock, hash)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (init.begin(), init.end());
    }

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock,
        KeyEqual const& key_eq)
        : m_config (clock, key_eq)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (init.begin(), init.end());
    }

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock,
        Allocator const& alloc)
        : m_config (clock, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (init.begin(), init.end());
    }

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& key_eq)
        : m_config (clock, hash, key_eq)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (init.begin(), init.end());
    }

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock,
        Hash const& hash,
        Allocator const& alloc)
        : m_config (clock, hash, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (init.begin(), init.end());
    }

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock,
        KeyEqual const& key_eq,
        Allocator const& alloc)
        : m_config (clock, key_eq, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (init.begin(), init.end());
    }

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& key_eq,
        Allocator const& alloc)
        : m_config (clock, hash, key_eq, alloc)
        , m_buck (alloc)
        , m_cont (m_buck,
            std::cref (m_config.value_hash()),
                std::cref (m_config.key_value_equal()))
    {
        insert (init.begin(), init.end());
    }

    ~aged_unordered_container()
    {
        clear();
    }

    aged_unordered_container& operator= (aged_unordered_container const& other)
    {
        if (this != &other)
        {
            size_type const n (other.size());
            clear();
            m_config = other.m_config;
            m_buck = Buckets (m_config.alloc());
            maybe_rehash (n);
            insert_unchecked (other.begin(), other.end());
        }
        return *this;
    }

    aged_unordered_container& operator= (aged_unordered_container&& other)
    {
        size_type const n (other.size());
        clear();
        m_config = std::move (other.m_config);
        m_buck = Buckets (m_config.alloc());
        maybe_rehash (n);
        insert_unchecked (other.begin(), other.end());
        other.clear();
        return *this;
    }

    aged_unordered_container& operator= (std::initializer_list <value_type> init)
    {
        clear ();
        insert (init);
        return *this;
    }

    allocator_type get_allocator() const
    {
        return m_config.alloc();
    }

    clock_type& clock()
    {
        return m_config.clock;
    }

    clock_type const& clock() const
    {
        return m_config.clock;
    }

    //--------------------------------------------------------------------------
    //
    // Element access (maps)
    //
    //--------------------------------------------------------------------------

    template <
        class K,
        bool maybe_multi = IsMulti,
        bool maybe_map = IsMap,
        class = typename std::enable_if <maybe_map && ! maybe_multi>::type>
    typename std::conditional <IsMap, T, void*>::type&
    at (K const& k)
    {
        auto const iter (m_cont.find (k,
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal())));
        if (iter == m_cont.end())
            throw std::out_of_range ("key not found");
        return iter->value.second;
    }

    template <
        class K,
        bool maybe_multi = IsMulti,
        bool maybe_map = IsMap,
        class = typename std::enable_if <maybe_map && ! maybe_multi>::type>
    typename std::conditional <IsMap, T, void*>::type const&
    at (K const& k) const
    {
        auto const iter (m_cont.find (k,
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal())));
        if (iter == m_cont.end())
            throw std::out_of_range ("key not found");
        return iter->value.second;
    }

    template <
        bool maybe_multi = IsMulti,
        bool maybe_map = IsMap,
        class = typename std::enable_if <maybe_map && ! maybe_multi>::type>
    typename std::conditional <IsMap, T, void*>::type&
    operator[] (Key const& key)
    {
        maybe_rehash (1);
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (key,
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal()), d));
        if (result.second)
        {
            element* const p (new_element (
                std::piecewise_construct,
                    std::forward_as_tuple (key),
                        std::forward_as_tuple ()));
            chronological.list.push_back (*p);
            auto const iter (m_cont.insert_commit (*p, d));
            return p->value.second;
        }
        return result.first->value.second;
    }

    template <
        bool maybe_multi = IsMulti,
        bool maybe_map = IsMap,
        class = typename std::enable_if <maybe_map && ! maybe_multi>::type>
    typename std::conditional <IsMap, T, void*>::type&
    operator[] (Key&& key)
    {
        maybe_rehash (1);
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (key,
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal()), d));
        if (result.second)
        {
            element* const p (new_element (
                std::piecewise_construct,
                    std::forward_as_tuple (std::move (key)),
                        std::forward_as_tuple ()));
            chronological.list.push_back (*p);
            auto const iter (m_cont.insert_commit (*p, d));
            return p->value.second;
        }
        return result.first->value.second;
    }

    //--------------------------------------------------------------------------
    //
    // Iterators
    //
    //--------------------------------------------------------------------------

    iterator begin ()
    {
        return iterator (m_cont.begin());
    }

    const_iterator begin () const
    {
        return const_iterator (m_cont.begin ());
    }

    const_iterator cbegin() const
    {
        return const_iterator (m_cont.begin ());
    }

    iterator end ()
    {
        return iterator (m_cont.end ());
    }

    const_iterator end () const
    {
        return const_iterator (m_cont.end ());
    }

    const_iterator cend () const
    {
        return const_iterator (m_cont.end ());
    }

    iterator iterator_to (value_type& value)
    {
        static_assert (std::is_standard_layout <element>::value,
            "must be standard layout");
        return m_cont.iterator_to (*reinterpret_cast <element*>(
             reinterpret_cast<uint8_t*>(&value)-((std::size_t)
                std::addressof(((element*)0)->member))));
    }

    const_iterator iterator_to (value_type const& value) const
    {
        static_assert (std::is_standard_layout <element>::value,
            "must be standard layout");
        return m_cont.iterator_to (*reinterpret_cast <element const*>(
             reinterpret_cast<uint8_t const*>(&value)-((std::size_t)
                std::addressof(((element*)0)->member))));
    }

    //--------------------------------------------------------------------------
    //
    // Capacity
    //
    //--------------------------------------------------------------------------

    bool empty() const noexcept
    {
        return m_cont.empty();
    }

    size_type size() const noexcept
    {
        return m_cont.size();
    }

    size_type max_size() const noexcept
    {
        return m_config.max_size();
    }

    //--------------------------------------------------------------------------
    //
    // Modifiers
    //
    //--------------------------------------------------------------------------

    void clear() noexcept
    {
        for (auto iter (chronological.list.begin());
            iter != chronological.list.end();)
            unlink_and_delete_element (&*iter++);
        chronological.list.clear();
        m_cont.clear();
        m_buck.clear();
    }

    // map, set
    template <bool maybe_multi = IsMulti>
    std::pair <iterator, bool>
    insert (value_type const& value,
        typename std::enable_if <! maybe_multi>::type* = 0)
    {
        maybe_rehash (1);
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (extract (value),
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal()), d));
        if (result.second)
        {
            element* const p (new_element (value));
            chronological.list.push_back (*p);
            auto const iter (m_cont.insert_commit (*p, d));
            return std::make_pair (iterator (iter), true);
        }
        return std::make_pair (iterator (result.first), false);
    }

    // map, set
    template <bool maybe_multi = IsMulti, bool maybe_map = IsMap>
    std::pair <iterator, bool>
    insert (value_type&& value,
        typename std::enable_if <! maybe_multi && ! maybe_map>::type* = 0)
    {
        maybe_rehash (1);
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (extract (value),
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal()), d));
        if (result.second)
        {
            element* const p (new_element (std::move (value)));
            chronological.list.push_back (*p);
            auto const iter (m_cont.insert_commit (*p, d));
            return std::make_pair (iterator (iter), true);
        }
        return std::make_pair (iterator (result.first), false);
    }

    // map, set
    template <bool maybe_multi = IsMulti>
    iterator
    insert (const_iterator /*hint*/, value_type const& value,
        typename std::enable_if <! maybe_multi>::type* = 0)
    {
        // Hint is ignored but we provide the interface so
        // callers may use ordered and unordered interchangeably.
        return insert (value).first;
    }

    // map, set
    template <bool maybe_multi = IsMulti>
    iterator
    insert (const_iterator /*hint*/, value_type&& value,
        typename std::enable_if <! maybe_multi>::type* = 0)
    {
        // Hint is ignored but we provide the interface so
        // callers may use ordered and unordered interchangeably.
        return insert (std::move (value)).first;
    }

    // multimap, multiset
    template <bool maybe_multi = IsMulti>
    iterator
    insert (value_type const& value,
        typename std::enable_if <maybe_multi>::type* = 0)
    {
        maybe_rehash (1);
        element* const p (new_element (value));
        chronological.list.push_back (*p);
        auto const iter (m_cont.insert (*p));
        return iterator (iter);
    }

    // multimap, multiset
    template <bool maybe_multi = IsMulti, bool maybe_map = IsMap>
    iterator
    insert (value_type&& value,
        typename std::enable_if <maybe_multi && ! maybe_map>::type* = 0)
    {
        maybe_rehash (1);
        element* const p (new_element (std::move (value)));
        chronological.list.push_back (*p);
        auto const iter (m_cont.insert (*p));
        return iterator (iter);
    }

    // multimap, multiset
    template <bool maybe_multi = IsMulti>
    iterator
    insert (const_iterator const& /*hint*/, value_type const& value,
        typename std::enable_if <maybe_multi>::type* = 0)
    {
        // VFALCO TODO The hint could be used to let
        //             the client order equal ranges
        return insert (value);
    }

    // multimap, multiset
    template <bool maybe_multi = IsMulti>
    iterator
    insert (const_iterator const& /*hint*/, value_type&& value,
        typename std::enable_if <maybe_multi>::type* = 0)
    {
        // VFALCO TODO The hint could be used to let
        //             the client order equal ranges
        return insert (std::move (value));
    }

    //--------------------------------------------------------------------------

    // map, multimap
    template <
        class P,
        bool maybe_map = IsMap
    >
    typename std::conditional <IsMulti,
        iterator,
        std::pair <iterator, bool>
    >::type
    insert (P&& value,
        typename std::enable_if <
            maybe_map &&
            std::is_constructible <value_type, P&&>::value>::type* = 0)
    {
        return emplace (std::forward <P> (value));
    }

    // map, multimap
    template <
        class P,
        bool maybe_map = IsMap
    >
    typename std::conditional <IsMulti,
        iterator,
        std::pair <iterator, bool>
    >::type
    insert (const_iterator hint, P&& value,
        typename std::enable_if <
            maybe_map &&
            std::is_constructible <value_type, P&&>::value>::type* = 0)
    {
        return emplace_hint (hint, std::forward <P> (value));
    }

    //--------------------------------------------------------------------------

    template <class InputIt>
    void insert (InputIt first, InputIt const& last)
    {
        insert (first, last,
            typename std::iterator_traits <
                InputIt>::iterator_category());
    }

    void insert (std::initializer_list <value_type> init)
    {
        insert (init.begin(), init.end());
    }

    template <class... Args>
    typename std::conditional <IsMulti,
        iterator,
        std::pair <iterator, bool>
    >::type
    emplace (Args&&... args)
    {
        return emplace <IsMulti> (nullptr,
            std::forward <Args> (args)...);
    }

    template <class... Args>
    typename std::conditional <IsMulti,
        iterator,
        std::pair <iterator, bool>
    >::type
    emplace_hint (
        const_iterator const& hint, Args&&... args)
    {
        return emplace_hint <IsMulti> (nullptr, hint,
            std::forward <Args> (args)...);
    }

    template <bool is_const, class Iterator, class Base>
    detail::aged_container_iterator <false, Iterator, Base>
    erase (detail::aged_container_iterator <
        is_const, Iterator, Base> const& pos)
    {
        auto iter (pos.iterator());
        auto p (&*iter++);
        unlink_and_delete_element (p);
        return detail::aged_container_iterator <
            false, Iterator, Base> (iter);
    }

    template <bool is_const, class Iterator, class Base>
    detail::aged_container_iterator <false, Iterator, Base>
    erase (detail::aged_container_iterator <
        is_const, Iterator, Base> first,
            detail::aged_container_iterator <
                is_const, Iterator, Base> const& last)
    {
        size_type n (0);
        for (; first != last; ++n)
        {
            auto p (&*first++);
            unlink_and_delete_element (p);
        }
        return detail::aged_container_iterator <
            false, Iterator, Base> (first.iterator());
    }

    template <class K>
    size_type erase (K const& k)
    {
        auto iter (m_cont.find (k, std::cref (m_config.hash_function()),
            std::cref (m_config.key_value_equal())));
        if (iter == m_cont.end())
            return 0;
        size_type n (0);
        for (;;)
        {
            auto p (&*iter++);
            bool const done (
                m_config (*p, extract (iter->value)));
            unlink_and_delete_element (p);
            ++n;
            if (done)
                break;
        }
        return n;
    }

    void swap (aged_unordered_container& other)
    {
        swap_data (other);
        std::swap (chronological, other.chronological);
        std::swap (m_cont, other.m_cont);
    }

    //--------------------------------------------------------------------------

    template <bool is_const, class Iterator, class Base>
    void touch (detail::aged_container_iterator <
        is_const, Iterator, Base> const& pos)
    {
        touch (pos, clock().now());
    }

    template <class K>
    size_type touch (K const& k)
    {
        auto const now (clock().now());
        size_type n (0);
        auto const range (equal_range (k));
        for (auto iter : range)
        {
            touch (iter, now);
            ++n;
        }
        return n;
    }

    //--------------------------------------------------------------------------
    //
    // Lookup
    //
    //--------------------------------------------------------------------------

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    size_type count (K const& k) const
    {
        return m_cont.count (k, std::cref (m_config.hash_function()),
            std::cref (m_config.key_value_equal()));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    iterator find (K const& k)
    {
        return iterator (m_cont.find (k,
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    const_iterator find (K const& k) const
    {
        return const_iterator (m_cont.find (k,
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair <iterator, iterator>
    equal_range (K const& k)
    {
        auto const r (m_cont.equal_range (k,
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal())));
        return std::make_pair (iterator (r.first), iterator (r.last));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair <const_iterator, const_iterator>
    equal_range (K const& k) const
    {
        auto const r (m_cont.equal_range (k,
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal())));
        return std::make_pair (const_iterator (r.first),
            const_iterator (r.last));
    }

    //--------------------------------------------------------------------------
    //
    // Bucket interface
    //
    //--------------------------------------------------------------------------

    local_iterator begin (size_type n)
    {
        return local_iterator (m_cont.begin (n));
    }

    const_local_iterator begin (size_type n) const
    {
        return const_local_iterator (m_cont.begin (n));
    }

    const_local_iterator cbegin (size_type n) const
    {
        return const_local_iterator (m_cont.begin (n));
    }

    local_iterator end (size_type n)
    {
        return local_iterator (m_cont.end (n));
    }

    const_local_iterator end (size_type n) const
    {
        return const_local_iterator (m_cont.end (n));
    }

    const_local_iterator cend (size_type n) const
    {
        return const_local_iterator (m_cont.end (n));
    }

    size_type bucket_count() const
    {
        return m_cont.bucket_count();
    }

    size_type max_bucket_count() const
    {
        return m_buck.max_bucket_count();
    }

    size_type bucket_size (size_type n) const
    {
        return m_cont.bucket_size (n);
    }

    size_type bucket (Key const& k) const
    {
        assert (bucket_count() != 0);
        return m_cont.bucket (k,
            std::cref (m_config.hash_function()));
    }

    //--------------------------------------------------------------------------
    //
    // Hash policy
    //
    //--------------------------------------------------------------------------

    float load_factor() const
    {
        return size() /
            static_cast <float> (m_cont.bucket_count());
    }

    float max_load_factor() const
    {
        return m_buck.max_load_factor();
    }

    void max_load_factor (float ml)
    {
        m_buck.max_load_factor () =
            std::max (ml, m_buck.max_load_factor());
    }

    void rehash (size_type count)
    {
        count = std::max (count,
            size_type (size() / max_load_factor()));
        m_buck.rehash (count, m_cont);
    }

    void reserve (size_type count)
    {
        rehash (std::ceil (count / max_load_factor()));
    }

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    hasher const& hash_function() const
    {
        return m_config.hash_function();
    }

    key_equal const& key_eq () const
    {
        return m_config.key_eq();
    }

    //--------------------------------------------------------------------------
    //
    // Comparison
    //
    //--------------------------------------------------------------------------

// VFALCO TODO These are surely wrong
#if 0
    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherKey,
        class OtherT,
        class OtherDuration,
        class OtherHash,
        class OtherKeyEqual,
        class OtherAllocator
    >
    bool operator== (
        aged_unordered_container <OtherIsMulti, OtherIsMap,
            OtherKey, OtherT, OtherDuration, OtherHash, OtherKeyEqual,
                OtherAllocator> const& other) const
    {
        typedef aged_unordered_container <OtherIsMulti, OtherIsMap,
            OtherKey, OtherT, OtherDuration, OtherHash, OtherKeyEqual,
                OtherAllocator> Other;
        if (size() != other.size())
            return false;
        std::equal_to <void> eq;
        return std::equal (cbegin(), cend(), other.cbegin(), other.cend(),
            [&eq, &other](value_type const& lhs,
                typename Other::value_type const& rhs)
            {
                return eq (extract (lhs), other.extract (rhs));
            });
    }

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherKey,
        class OtherT,
        class OtherDuration,
        class OtherHash,
        class OtherKeyEqual,
        class OtherAllocator
    >
    bool operator!= (
        aged_unordered_container <OtherIsMulti, OtherIsMap,
            OtherKey, OtherT, OtherDuration, OtherHash, OtherKeyEqual,
                OtherAllocator> const& other) const
    {
        return ! (this->operator== (other));
    }
#endif

private:
    bool would_exceed (size_type additional) const
    {
        return size() + additional >
            bucket_count() * max_load_factor();
    }

    void maybe_rehash (size_type additional)
    {
        if (would_exceed (additional))
            m_buck.resize (size() + additional, m_cont);
        assert (load_factor() <= max_load_factor());
    }

    // map, set
    template <bool maybe_multi = IsMulti>
    std::pair <iterator, bool>
    insert_unchecked (value_type const& value,
        typename std::enable_if <! maybe_multi>::type* = 0)
    {
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (extract (value),
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal()), d));
        if (result.second)
        {
            element* const p (new_element (value));
            chronological.list.push_back (*p);
            auto const iter (m_cont.insert_commit (*p, d));
            return std::make_pair (iterator (iter), true);
        }
        return std::make_pair (iterator (result.first), false);
    }

    // multimap, multiset
    template <bool maybe_multi = IsMulti>
    iterator
    insert_unchecked (value_type const& value,
        typename std::enable_if <maybe_multi>::type* = 0)
    {
        element* const p (new_element (value));
        chronological.list.push_back (*p);
        auto const iter (m_cont.insert (*p));
        return iterator (iter);
    }

    template <class InputIt>
    void insert_unchecked (InputIt first, InputIt const& last)
    {
        for (; first != last; ++first)
            insert_unchecked (*first);
    }

    template <class InputIt>
    void insert (InputIt first, InputIt const& last,
        std::input_iterator_tag)
    {
        for (; first != last; ++first)
            insert (*first);
    }

    template <class InputIt>
    void insert (InputIt first, InputIt const& last,
        std::random_access_iterator_tag)
    {
        auto const n (std::distance (first, last));
        maybe_rehash (n);
        insert_unchecked (first, last);
    }

    // set, map
    template <bool maybe_multi = IsMulti, class... Args>
    std::pair <iterator, bool>
    emplace (typename std::enable_if <! maybe_multi>::type*,
        Args&&... args)
    {
        maybe_rehash (1);
        // VFALCO NOTE Its unfortunate that we need to
        //             construct element here
        element* const p (new_element (
            std::forward <Args> (args)...));
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (extract (p->value),
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal()), d));
        if (result.second)
        {
            chronological.list.push_back (*p);
            auto const iter (m_cont.insert_commit (*p, d));
            return std::make_pair (iterator (iter), true);
        }
        delete_element (p);
        return std::make_pair (iterator (result.first), false);
    }

    // set, map
    template <bool maybe_multi = IsMulti, class... Args>
    std::pair <iterator, bool>
    emplace_hint (typename std::enable_if <! maybe_multi>::type*,
        const_iterator const& /*hint*/, Args&&... args)
    {
        maybe_rehash (1);
        // VFALCO NOTE Its unfortunate that we need to
        //             construct element here
        element* const p (new_element (
            std::forward <Args> (args)...));
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (extract (p->value),
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal()), d));
        if (result.second)
        {
            chronological.list.push_back (*p);
            auto const iter (m_cont.insert_commit (*p, d));
            return std::make_pair (iterator (iter), true);
        }
        delete_element (p);
        return std::make_pair (iterator (result.first), false);
    }

    // multiset, multimap
    template <bool maybe_multi = IsMulti, class... Args>
    iterator
    emplace (typename std::enable_if <maybe_multi>::type*,
        Args&&... args)
    {
        maybe_rehash (1);
        element* const p (new_element (
            std::forward <Args> (args)...));
        chronological.list.push_back (*p);
        auto const iter (m_cont.insert (*p));
        return iterator (iter);
    }

    // multiset, multimap
    template <bool maybe_multi = IsMulti, class... Args>
    iterator
    emplace_hint (typename std::enable_if <maybe_multi>::type*,
        const_iterator const& /*hint*/, Args&&... args)
    {
        // VFALCO TODO The hint could be used for multi, to let
        //             the client order equal ranges
        return emplace <maybe_multi> (nullptr,
            std::forward <Args> (args)...);
    }

    template <bool is_const, class Iterator, class Base>
    void touch (detail::aged_container_iterator <
        is_const, Iterator, Base> const& pos,
            typename clock_type::time_point const& now)
    {
        auto& e (*pos.iterator());
        e.when = now;
        chronological.list.erase (chronological.list.iterator_to (e));
        chronological.list.push_back (e);
    }

    template <bool maybe_propagate = std::allocator_traits <
        Allocator>::propagate_on_container_swap::value>
    void swap_data (aged_unordered_container& other,
        typename std::enable_if <maybe_propagate>::type* = 0)
    {
        std::swap (m_config.key_compare(), other.m_config.key_compare());
        std::swap (m_config.alloc(), other.m_config.alloc());
        std::swap (m_config.clock, other.m_config.clock);
    }

    template <bool maybe_propagate = std::allocator_traits <
        Allocator>::propagate_on_container_swap::value>
    void swap_data (aged_unordered_container& other,
        typename std::enable_if <! maybe_propagate>::type* = 0)
    {
        std::swap (m_config.key_compare(), other.m_config.key_compare());
        std::swap (m_config.clock, other.m_config.clock);
    }

private:
    config_t m_config;
    Buckets m_buck;
    cont_type mutable m_cont;
};

//------------------------------------------------------------------------------

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Duration,
    class Hash,
    class KeyEqual,
    class Allocator
>
void swap (
    aged_unordered_container <IsMulti, IsMap,
        Key, T, Duration, Hash, KeyEqual, Allocator>& lhs,
    aged_unordered_container <IsMulti, IsMap,
        Key, T, Duration, Hash, KeyEqual, Allocator>& rhs)
{
    lhs.swap (rhs);
}

}
}

#endif
