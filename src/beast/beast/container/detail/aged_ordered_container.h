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

#ifndef BEAST_CONTAINER_AGED_ORDERED_CONTAINER_H_INCLUDED
#define BEAST_CONTAINER_AGED_ORDERED_CONTAINER_H_INCLUDED

#include "aged_container_iterator.h"
#include "aged_associative_container.h"

#include "../../chrono/abstract_clock.h"
#include "../../equal.h"
#include "../../equal_to.h"
#include "../../is_constructible.h"
#include "../../utility/empty_base_optimization.h"

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>

#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

namespace beast {
namespace detail {

/** Associative container where each element is also indexed by time.

    This container mirrors the interface of the standard library ordered
    associative containers, with the addition that each element is associated
    with a `when` `time_point` which is obtained from the value of the clock's
    `now`. The function `touch` updates the time for an element to the current
    time as reported by the clock.

    An extra set of iterator types and member functions are provided in the
    `chronological` memberspace that allow traversal in temporal or reverse
    temporal order. This container is useful as a building block for caches
    whose items expire after a certain amount of time. The chronological
    iterators allow for fully customizable expiration strategies.

    @see aged_set, aged_multiset, aged_map, aged_multimap
*/
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Duration = std::chrono::seconds,
    class Compare = std::less <Key>,
    class Allocator = std::allocator <
        typename std::conditional <IsMap,
            std::pair <Key const, T>,
                Key>::type>
>
class aged_ordered_container
{
public:
    typedef abstract_clock <Duration> clock_type;
    typedef typename clock_type::time_point time_point;
    typedef typename clock_type::duration duration;
    typedef Key key_type;
    typedef T mapped_type;
    typedef typename std::conditional <
        IsMap,
        std::pair <Key const, T>,
        Key>::type value_type;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    // Introspection (for unit tests)
    typedef std::false_type is_unordered;
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
        : boost::intrusive::set_base_hook <
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
            typedef typename aged_ordered_container::value_type value_type;
            typedef typename aged_ordered_container::time_point time_point;
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

    // VFALCO TODO This should only be enabled for maps.
    class pair_value_compare
        : private empty_base_optimization <Compare>
        , public std::binary_function <value_type, value_type, bool>
    {
    public:
        bool operator() (value_type const& lhs, value_type const& rhs) const
        {
            return this->member() (lhs.first, rhs.first);
        }

        pair_value_compare ()
        {
        }

        pair_value_compare (pair_value_compare const& other)
            : empty_base_optimization <Compare> (other)
        {
        }

    private:
        friend aged_ordered_container;

        pair_value_compare (Compare const& compare)
            : empty_base_optimization <Compare> (compare)
        {
        }
    };

    // Compares value_type against element, used in insert_check
    // VFALCO TODO hoist to remove template argument dependencies
    class KeyValueCompare
        : private empty_base_optimization <Compare>
        , public std::binary_function <Key, element, bool>
    {
    public:
#if BEAST_AGED_CONTAINER_SIMPLIFICATION
        KeyValueCompare ()
        {
        }

        KeyValueCompare (Compare const& compare)
            : empty_base_optimization <Compare> (compare)
        {
        }

#else
        template <
            class... Args,
            class = typename std::enable_if <
                std::is_constructible <
                    empty_base_optimization <Compare>,
                        Args...>::value>::type
        >
        KeyValueCompare (Args&&... args)
            : empty_base_optimization <Compare> (
                std::forward <Args> (args)...)
        {
        }

#endif

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

        Compare& compare()
        {
            return empty_base_optimization <Compare>::member();
        }

        Compare const& compare() const
        {
            return empty_base_optimization <Compare>::member();
        }
    };

    typedef typename boost::intrusive::make_list <element,
        boost::intrusive::constant_time_size <false>
            >::type list_type;

    typedef typename std::conditional <
        IsMulti,
        typename boost::intrusive::make_multiset <element,
            boost::intrusive::constant_time_size <true>
                >::type,
        typename boost::intrusive::make_set <element,
            boost::intrusive::constant_time_size <true>
                >::type
        >::type cont_type;

    typedef typename std::allocator_traits <
        Allocator>::template rebind_alloc <element> ElementAllocator;

    using ElementAllocatorTraits = std::allocator_traits <ElementAllocator>;

    class config_t
        : private KeyValueCompare
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
            Compare const& comp)
            : KeyValueCompare (comp)
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
            Compare const& comp,
            Allocator const& alloc_)
            : KeyValueCompare (comp)
            , empty_base_optimization <ElementAllocator> (alloc_)
            , clock (clock_)
        {
        }

        config_t (config_t const& other)
            : KeyValueCompare (other.key_compare())
            , empty_base_optimization <ElementAllocator> (
                ElementAllocatorTraits::
                    select_on_container_copy_construction (
                        other.alloc()))
            , clock (other.clock)
        {
        }

        config_t (config_t const& other, Allocator const& alloc)
            : KeyValueCompare (other.key_compare())
            , empty_base_optimization <ElementAllocator> (alloc)
            , clock (other.clock)
        {
        }

        config_t (config_t&& other)
            : KeyValueCompare (std::move (other.key_compare()))
            , empty_base_optimization <ElementAllocator> (
                std::move (other))
            , clock (other.clock)
        {
        }

        config_t (config_t&& other, Allocator const& alloc)
            : KeyValueCompare (std::move (other.key_compare()))
            , empty_base_optimization <ElementAllocator> (alloc)
            , clock (other.clock)
        {
        }

        config_t& operator= (config_t const& other)
        {
            if (this != &other)
            {
                compare() = other.compare();
                alloc() = other.alloc();
                clock = other.clock;
            }
            return *this;
        }

        config_t& operator= (config_t&& other)
        {
            compare() = std::move (other.compare());
            alloc() = std::move (other.alloc());
            clock = other.clock;
            return *this;
        }

        Compare& compare ()
        {
            return KeyValueCompare::compare();
        }

        Compare const& compare () const
        {
            return KeyValueCompare::compare();
        }

        KeyValueCompare& key_compare()
        {
            return *this;
        }

        KeyValueCompare const& key_compare() const
        {
            return *this;
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
    typedef Compare key_compare;
    typedef typename std::conditional <
        IsMap,
        pair_value_compare,
        Compare>::type value_compare;
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
        typename cont_type::reverse_iterator> reverse_iterator;
    typedef detail::aged_container_iterator <true,
        typename cont_type::reverse_iterator> const_reverse_iterator;

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

        friend class aged_ordered_container;
        list_type mutable list;
    } chronological;

    //--------------------------------------------------------------------------
    //
    // Construction
    //
    //--------------------------------------------------------------------------

    explicit aged_ordered_container (
        clock_type& clock)
        : m_config (clock)
    {
    }

    aged_ordered_container (
        clock_type& clock,
        Compare const& comp)
        : m_config (clock, comp)
    {
    }

    aged_ordered_container (
        clock_type& clock,
        Allocator const& alloc)
        : m_config (clock, alloc)
    {
    }

    aged_ordered_container (
        clock_type& clock,
        Compare const& comp,
        Allocator const& alloc)
        : m_config (clock, comp, alloc)
    {
    }

    template <class InputIt>
    aged_ordered_container (InputIt first, InputIt last,
        clock_type& clock)
        : m_config (clock)
    {
        insert (first, last);
    }

    template <class InputIt>
    aged_ordered_container (InputIt first, InputIt last,
        clock_type& clock,
        Compare const& comp)
        : m_config (clock, comp)
    {
        insert (first, last);
    }

    template <class InputIt>
    aged_ordered_container (InputIt first, InputIt last,
        clock_type& clock,
        Allocator const& alloc)
        : m_config (clock, alloc)
    {
        insert (first, last);
    }

    template <class InputIt>
    aged_ordered_container (InputIt first, InputIt last,
        clock_type& clock,
        Compare const& comp,
        Allocator const& alloc)
        : m_config (clock, comp, alloc)
    {
        insert (first, last);
    }

    aged_ordered_container (aged_ordered_container const& other)
        : m_config (other.m_config)
    {
        insert (other.cbegin(), other.cend());
    }

    aged_ordered_container (aged_ordered_container const& other,
        Allocator const& alloc)
        : m_config (other.m_config, alloc)
    {
        insert (other.cbegin(), other.cend());
    }

    aged_ordered_container (aged_ordered_container&& other)
        : m_config (std::move (other.m_config))
        , m_cont (std::move (other.m_cont))
    {
        chronological.list = std::move (other.chronological.list);
    }

    aged_ordered_container (aged_ordered_container&& other,
        Allocator const& alloc)
        : m_config (std::move (other.m_config), alloc)
    {
        insert (other.cbegin(), other.cend());
        other.clear ();
    }

    aged_ordered_container (std::initializer_list <value_type> init,
        clock_type& clock)
        : m_config (clock)
    {
        insert (init.begin(), init.end());
    }

    aged_ordered_container (std::initializer_list <value_type> init,
        clock_type& clock,
        Compare const& comp)
        : m_config (clock, comp)
    {
        insert (init.begin(), init.end());
    }

    aged_ordered_container (std::initializer_list <value_type> init,
        clock_type& clock,
        Allocator const& alloc)
        : m_config (clock, alloc)
    {
        insert (init.begin(), init.end());
    }

    aged_ordered_container (std::initializer_list <value_type> init,
        clock_type& clock,
        Compare const& comp,
        Allocator const& alloc)
        : m_config (clock, comp, alloc)
    {
        insert (init.begin(), init.end());
    }

    ~aged_ordered_container()
    {
        clear();
    }

    aged_ordered_container& operator= (aged_ordered_container const& other)
    {
        if (this != &other)
        {
            clear();
            this->m_config = other.m_config;
            insert (other.begin(), other.end());
        }
        return *this;
    }

    aged_ordered_container& operator= (aged_ordered_container&& other)
    {
        clear();
        this->m_config = std::move (other.m_config);
        insert (other.begin(), other.end());
        other.clear();
        return *this;
    }

    aged_ordered_container& operator= (std::initializer_list <value_type> init)
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
            std::cref (m_config.key_compare())));
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
            std::cref (m_config.key_compare())));
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
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (key,
            std::cref (m_config.key_compare()), d));
        if (result.second)
        {
            element* const p (new_element (
                std::piecewise_construct, std::forward_as_tuple (key),
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
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (key,
            std::cref (m_config.key_compare()), d));
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

    reverse_iterator rbegin ()
    {
        return reverse_iterator (m_cont.rbegin());
    }

    const_reverse_iterator rbegin () const
    {
        return const_reverse_iterator (m_cont.rbegin ());
    }

    const_reverse_iterator crbegin() const
    {
        return const_reverse_iterator (m_cont.rbegin ());
    }

    reverse_iterator rend ()
    {
        return reverse_iterator (m_cont.rend ());
    }

    const_reverse_iterator rend () const
    {
        return const_reverse_iterator (m_cont.rend ());
    }

    const_reverse_iterator crend () const
    {
        return const_reverse_iterator (m_cont.rend ());
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
            delete_element (&*iter++);
        chronological.list.clear();
        m_cont.clear();
    }

    //
    // map, set
    //

    template <bool maybe_multi = IsMulti>
    std::pair <iterator, bool>
    insert (value_type const& value,
        typename std::enable_if <! maybe_multi>::type* = 0)
    {
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (extract (value),
            std::cref (m_config.key_compare()), d));
        if (result.second)
        {
            element* const p (new_element (value));
            chronological.list.push_back (*p);
            auto const iter (m_cont.insert_commit (*p, d));
            return std::make_pair (iterator (iter), true);
        }
        return std::make_pair (iterator (result.first), false);
    }

    template <bool maybe_multi = IsMulti, bool maybe_map = IsMap>
    std::pair <iterator, bool>
    insert (value_type&& value,
        typename std::enable_if <! maybe_multi && ! maybe_map>::type* = 0)
    {
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (extract (value),
            std::cref (m_config.key_compare()), d));
        if (result.second)
        {
            element* const p (new_element (std::move (value)));
            chronological.list.push_back (*p);
            auto const iter (m_cont.insert_commit (*p, d));
            return std::make_pair (iterator (iter), true);
        }
        return std::make_pair (iterator (result.first), false);
    }

    template <bool maybe_multi = IsMulti>
    iterator
    insert (const_iterator hint, value_type const& value,
        typename std::enable_if <! maybe_multi>::type* = 0)
    {
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (hint.iterator(),
            extract (value), std::cref (m_config.key_compare()), d));
        if (result.second)
        {
            element* const p (new_element (value));
            chronological.list.push_back (*p);
            auto const iter (m_cont.insert_commit (*p, d));
            return iterator (iter);
        }
        return iterator (result.first);
    }

    template <bool maybe_multi = IsMulti>
    iterator
    insert (const_iterator hint, value_type&& value,
        typename std::enable_if <! maybe_multi>::type* = 0)
    {
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (hint.iterator(),
            extract (value), std::cref (m_config.key_compare()), d));
        if (result.second)
        {
            element* const p (new_element (std::move (value)));
            chronological.list.push_back (*p);
            auto const iter (m_cont.insert_commit (*p, d));
            return iterator (iter);
        }
        return iterator (result.first);
    }

    //
    // multimap, multiset
    //

    template <bool maybe_multi = IsMulti>
    iterator
    insert (value_type const& value,
        typename std::enable_if <maybe_multi>::type* = 0)
    {
        auto const before (m_cont.upper_bound (
            extract (value), std::cref (m_config.key_compare())));
        element* const p (new_element (value));
        chronological.list.push_back (*p);
        auto const iter (m_cont.insert_before (before, *p));
        return iterator (iter);
    }

    template <bool maybe_multi = IsMulti, bool maybe_map = IsMap>
    iterator
    insert (value_type&& value,
        typename std::enable_if <maybe_multi && ! maybe_map>::type* = 0)
    {
        auto const before (m_cont.upper_bound (
            extract (value), std::cref (m_config.key_compare())));
        element* const p (new_element (std::move (value)));
        chronological.list.push_back (*p);
        auto const iter (m_cont.insert_before (before, *p));
        return iterator (iter);
    }

    template <bool maybe_multi = IsMulti>
    iterator
    insert (const_iterator const& /*hint*/, value_type const& value,
        typename std::enable_if <maybe_multi>::type* = 0)
    {
        // VFALCO TODO Figure out how to utilize 'hint'
        return insert (value);
    }

    template <bool maybe_multi = IsMulti>
    iterator
    insert (const_iterator const& /*hint*/, value_type&& value,
        typename std::enable_if <maybe_multi>::type* = 0)
    {
        // VFALCO TODO Figure out how to utilize 'hint'
        return insert (std::move (value));
    }

    //--------------------------------------------------------------------------

    //
    // map, multimap
    //

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
        for (; first != last; ++first)
            insert (cend(), *first);
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
        for (; first != last;)
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
        auto iter (m_cont.find (k,
            std::cref (m_config.key_compare())));
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

    void swap (aged_ordered_container& other)
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
        return m_cont.count (k,
            std::cref (m_config.key_compare()));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    iterator find (K const& k)
    {
        return iterator (m_cont.find (k,
            std::cref (m_config.key_compare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    const_iterator find (K const& k) const
    {
        return const_iterator (m_cont.find (k,
            std::cref (m_config.key_compare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair <iterator, iterator>
    equal_range (K const& k)
    {
        auto const r (m_cont.equal_range (k,
            std::cref (m_config.key_compare())));
        return std::make_pair (iterator (r.first), iterator (r.last));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair <const_iterator, const_iterator>
    equal_range (K const& k) const
    {
        auto const r (m_cont.equal_range (k,
            std::cref (m_config.key_compare())));
        return std::make_pair (const_iterator (r.first),
            const_iterator (r.last));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    iterator lower_bound (K const& k)
    {
        return iterator (m_cont.lower_bound (k,
            std::cref (m_config.key_compare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    const_iterator lower_bound (K const& k) const
    {
        return const_iterator (m_cont.lower_bound (k,
            std::cref (m_config.key_compare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    iterator upper_bound (K const& k)
    {
        return iterator (m_cont.upper_bound (k,
            std::cref (m_config.key_compare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    const_iterator upper_bound (K const& k) const
    {
        return const_iterator (m_cont.upper_bound (k, 
            std::cref (m_config.key_compare())));
    }

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    key_compare key_comp() const
    {
        return m_config.compare();
    }

    // VFALCO TODO Should this return const reference for set?
    value_compare value_comp() const
    {
        return value_compare (m_config.compare());
    }

    //--------------------------------------------------------------------------
    //
    // Comparison
    //
    //--------------------------------------------------------------------------

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherT,
        class OtherDuration,
        class OtherAllocator
    >
    bool operator== (
        aged_ordered_container <OtherIsMulti, OtherIsMap,
            Key, OtherT, OtherDuration, Compare,
                OtherAllocator> const& other) const
    {
        typedef aged_ordered_container <OtherIsMulti, OtherIsMap,
            Key, OtherT, OtherDuration, Compare,
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
        class OtherT,
        class OtherDuration,
        class OtherAllocator
    >
    bool operator!= (
        aged_ordered_container <OtherIsMulti, OtherIsMap,
            Key, OtherT, OtherDuration, Compare,
                OtherAllocator> const& other) const
    {
        return ! (this->operator== (other));
    }

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherT,
        class OtherDuration,
        class OtherAllocator
    >
    bool operator< (
        aged_ordered_container <OtherIsMulti, OtherIsMap,
            Key, OtherT, OtherDuration, Compare,
                OtherAllocator> const& other) const
    {
        value_compare const comp (value_comp ());
        return std::lexicographical_compare (
            cbegin(), cend(), other.cbegin(), other.cend(), comp);
    }

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherT,
        class OtherDuration,
        class OtherAllocator
    >
    bool operator<= (
        aged_ordered_container <OtherIsMulti, OtherIsMap,
            Key, OtherT, OtherDuration, Compare,
                OtherAllocator> const& other) const
    {
        return ! (other < *this);
    }

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherT,
        class OtherDuration,
        class OtherAllocator
    >
    bool operator> (
        aged_ordered_container <OtherIsMulti, OtherIsMap,
            Key, OtherT, OtherDuration, Compare,
                OtherAllocator> const& other) const
    {
        return other < *this;
    }

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherT,
        class OtherDuration,
        class OtherAllocator
    >
    bool operator>= (
        aged_ordered_container <OtherIsMulti, OtherIsMap,
            Key, OtherT, OtherDuration, Compare,
                OtherAllocator> const& other) const
    {
        return ! (*this < other);
    }

private:
    // set, map
    template <bool maybe_multi = IsMulti, class... Args>
    std::pair <iterator, bool>
    emplace (typename std::enable_if <! maybe_multi>::type*,
        Args&&... args)
    {
        // VFALCO NOTE Its unfortunate that we need to
        //             construct element here
        element* const p (new_element (
            std::forward <Args> (args)...));
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (extract (p->value),
            std::cref (m_config.key_compare()), d));
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
        const_iterator const& hint, Args&&... args)
    {
        // VFALCO NOTE Its unfortunate that we need to
        //             construct element here
        element* const p (new_element (
            std::forward <Args> (args)...));
        typename cont_type::insert_commit_data d;
        auto const result (m_cont.insert_check (hint.iterator(),
            extract (p->value), std::cref (m_config.key_compare()), d));
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
        element* const p (new_element (
            std::forward <Args> (args)...));
        auto const before (m_cont.upper_bound (extract (p->value),
            std::cref (m_config.key_compare())));
        chronological.list.push_back (*p);
        auto const iter (m_cont.insert_before (before, *p));
        return iterator (iter);
    }

    // multiset, multimap
    template <bool maybe_multi = IsMulti, class... Args>
    iterator
    emplace_hint (typename std::enable_if <maybe_multi>::type*,
        const_iterator const& /*hint*/, Args&&... args)
    {
        // VFALCO TODO Figure out how to utilize 'hint'
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
    void swap_data (aged_ordered_container& other,
        typename std::enable_if <maybe_propagate>::type* = 0)
    {
        std::swap (m_config.key_compare(), other.m_config.key_compare());
        std::swap (m_config.alloc(), other.m_config.alloc());
        std::swap (m_config.clock, other.m_config.clock);
    }

    template <bool maybe_propagate = std::allocator_traits <
        Allocator>::propagate_on_container_swap::value>
    void swap_data (aged_ordered_container& other,
        typename std::enable_if <! maybe_propagate>::type* = 0)
    {
        std::swap (m_config.key_compare(), other.m_config.key_compare());
        std::swap (m_config.clock, other.m_config.clock);
    }

private:
    config_t m_config;
    cont_type mutable m_cont;
};

//------------------------------------------------------------------------------

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Duration,
    class Compare,
    class Allocator
>
void swap (
    aged_ordered_container <IsMulti, IsMap,
        Key, T, Duration, Compare, Allocator>& lhs,
    aged_ordered_container <IsMulti, IsMap,
        Key, T, Duration, Compare, Allocator>& rhs)
{
    lhs.swap (rhs);
}

}
}

#endif
