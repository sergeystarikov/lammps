/*
//@HEADER
// ************************************************************************
//
//   Kokkos: Manycore Performance-Portable Multidimensional Arrays
//              Copyright (2012) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact  H. Carter Edwards (hcedwar@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#ifndef KOKKOS_BITSET_HPP
#define KOKKOS_BITSET_HPP

#include <Kokkos_Macros.hpp>
#include <Kokkos_Functional.hpp>
#include <Kokkos_View.hpp>
#include <Kokkos_Atomic.hpp>
#include <Kokkos_HostSpace.hpp>
#include <Kokkos_Pair.hpp>

#include <impl/Kokkos_Bitset_impl.hpp>

#include <stdexcept>

namespace Kokkos {

template <typename Device = Kokkos::DefaultExecutionSpace >
class Bitset;

template <typename Device = Kokkos::DefaultExecutionSpace >
class ConstBitset;

template <typename DstDevice, typename SrcDevice>
void deep_copy( Bitset<DstDevice> & dst, Bitset<SrcDevice> const& src);

template <typename DstDevice, typename SrcDevice>
void deep_copy( Bitset<DstDevice> & dst, ConstBitset<SrcDevice> const& src);

template <typename DstDevice, typename SrcDevice>
void deep_copy( ConstBitset<DstDevice> & dst, ConstBitset<SrcDevice> const& src);


/// A thread safe view to a bitset
template <typename Device>
class Bitset
{
public:
  typedef Device device_type;
  typedef unsigned size_type;

  enum { BIT_SCAN_REVERSE = 1u };
  enum { MOVE_HINT_BACKWARD = 2u };

  enum {
      BIT_SCAN_FORWARD_MOVE_HINT_FORWARD = 0u
    , BIT_SCAN_REVERSE_MOVE_HINT_FORWARD = BIT_SCAN_REVERSE
    , BIT_SCAN_FORWARD_MOVE_HINT_BACKWARD = MOVE_HINT_BACKWARD
    , BIT_SCAN_REVERSE_MOVE_HINT_BACKWARD = BIT_SCAN_REVERSE | MOVE_HINT_BACKWARD
  };

private:
  enum { block_size = static_cast<unsigned>(sizeof(unsigned)*CHAR_BIT) };
  enum { block_mask = block_size-1u };
  enum { block_shift = static_cast<int>(Impl::power_of_two<block_size>::value) };

public:


  /// constructor
  /// arg_size := number of bit in set
  Bitset(unsigned arg_size = 0u)
    : m_size(arg_size)
    , m_last_block_mask(0u)
    , m_blocks("Bitset", ((m_size + block_mask) >> block_shift) )
  {
    for (int i=0, end = static_cast<int>(m_size & block_mask); i < end; ++i) {
      m_last_block_mask |= 1u << i;
    }
  }

  /// assignment
  Bitset<Device> & operator = (Bitset<Device> const & rhs)
  {
    this->m_size = rhs.m_size;
    this->m_last_block_mask = rhs.m_last_block_mask;
    this->m_blocks = rhs.m_blocks;

    return *this;
  }

  /// copy constructor
  Bitset( Bitset<Device> const & rhs)
    : m_size( rhs.m_size )
    , m_last_block_mask( rhs.m_last_block_mask )
    , m_blocks( rhs.m_blocks )
  {}

  /// number of bits in the set
  /// can be call from the host or the device
  KOKKOS_FORCEINLINE_FUNCTION
  unsigned size() const
  { return m_size; }

  /// number of bits which are set to 1
  /// can only be called from the host
  unsigned count() const
  {
    Impl::BitsetCount< Bitset<Device> > f(*this);
    return f.apply();
  }

  /// set all bits to 1
  /// can only be called from the host
  void set()
  {
    Kokkos::deep_copy(m_blocks, ~0u );

    if (m_last_block_mask) {
      //clear the unused bits in the last block
      typedef Kokkos::Impl::DeepCopy< typename device_type::memory_space, Kokkos::HostSpace > raw_deep_copy;
      raw_deep_copy( m_blocks.ptr_on_device() + (m_blocks.size() -1u), &m_last_block_mask, sizeof(unsigned));
    }
  }

  /// set all bits to 0
  /// can only be called from the host
  void reset()
  {
    Kokkos::deep_copy(m_blocks, 0u );
  }

  /// set all bits to 0
  /// can only be called from the host
  void clear()
  {
    Kokkos::deep_copy(m_blocks, 0u );
  }

  /// set i'th bit to 1
  /// can only be called from the device
  KOKKOS_FORCEINLINE_FUNCTION
  bool set( unsigned i ) const
  {
    if ( i < m_size ) {
      unsigned * block_ptr = &m_blocks[ i >> block_shift ];
      const unsigned mask = 1u << static_cast<int>( i & block_mask );

      return !( atomic_fetch_or( block_ptr, mask ) & mask );
    }
    return false;
  }

  /// set i'th bit to 0
  /// can only be called from the device
  KOKKOS_FORCEINLINE_FUNCTION
  bool reset( unsigned i ) const
  {
    if ( i < m_size ) {
      unsigned * block_ptr = &m_blocks[ i >> block_shift ];
      const unsigned mask = 1u << static_cast<int>( i & block_mask );

      return atomic_fetch_and( block_ptr, ~mask ) & mask;
    }
    return false;
  }

  /// return true if the i'th bit set to 1
  /// can only be called from the device
  KOKKOS_FORCEINLINE_FUNCTION
  bool test( unsigned i ) const
  {
    if ( i < m_size ) {
      const unsigned block = volatile_load(&m_blocks[ i >> block_shift ]);
      const unsigned mask = 1u << static_cast<int>( i & block_mask );
      return block & mask;
    }
    return false;
  }

  /// used with find_any_set_near or find_any_unset_near functions
  /// returns the max number of times those functions should be call
  /// when searching for an available bit
  KOKKOS_FORCEINLINE_FUNCTION
  unsigned max_hint() const
  {
    return m_blocks.size();
  }

  /// find a bit set to 1 near the hint
  /// returns a pair< bool, unsigned> where if result.first is true then result.second is the bit found
  /// and if result.first is false the result.second is a new hint
  KOKKOS_INLINE_FUNCTION
  Kokkos::pair<bool, unsigned> find_any_set_near( unsigned hint , unsigned scan_direction = BIT_SCAN_FORWARD_MOVE_HINT_FORWARD ) const
  {
    const unsigned block_idx = (hint >> block_shift) < m_blocks.size() ? (hint >> block_shift) : 0;
    const unsigned offset = hint & block_mask;
    unsigned block = volatile_load(&m_blocks[ block_idx ]);
    block = !m_last_block_mask || (block_idx < (m_blocks.size()-1)) ? block : block & m_last_block_mask ;

    return find_any_helper(block_idx, offset, block, scan_direction);
  }

  /// find a bit set to 0 near the hint
  /// returns a pair< bool, unsigned> where if result.first is true then result.second is the bit found
  /// and if result.first is false the result.second is a new hint
  KOKKOS_INLINE_FUNCTION
  Kokkos::pair<bool, unsigned> find_any_unset_near( unsigned hint , unsigned scan_direction = BIT_SCAN_FORWARD_MOVE_HINT_FORWARD ) const
  {
    const unsigned block_idx = hint >> block_shift;
    const unsigned offset = hint & block_mask;
    unsigned block = volatile_load(&m_blocks[ block_idx ]);
    block = !m_last_block_mask || (block_idx < (m_blocks.size()-1) ) ? ~block : ~block & m_last_block_mask ;

    return find_any_helper(block_idx, offset, block, scan_direction);
  }

private:

  KOKKOS_FORCEINLINE_FUNCTION
  Kokkos::pair<bool, unsigned> find_any_helper(unsigned block_idx, unsigned offset, unsigned block, unsigned scan_direction) const
  {
    Kokkos::pair<bool, unsigned> result( block > 0u, 0);

    if (!result.first) {
      result.second = update_hint( block_idx, offset, scan_direction );
    }
    else {
      result.second = scan_block(  (block_idx << block_shift)
                                 , offset
                                 , block
                                 , scan_direction
                                );
    }
    return result;
  }


  KOKKOS_FORCEINLINE_FUNCTION
  unsigned scan_block(unsigned block_start, int offset, unsigned block, unsigned scan_direction ) const
  {
    offset = !(scan_direction & BIT_SCAN_REVERSE) ? offset : (offset + block_mask) & block_mask;
    block = Impl::rotate_right(block, offset);
    return ((( !(scan_direction & BIT_SCAN_REVERSE) ?
               Impl::bit_scan_forward(block) :
               Impl::bit_scan_reverse(block)
             ) + offset
            ) & block_mask
           ) + block_start;
  }

  KOKKOS_FORCEINLINE_FUNCTION
  unsigned update_hint( long long block_idx, unsigned offset, unsigned scan_direction ) const
  {
    block_idx += scan_direction & MOVE_HINT_BACKWARD ? -1 : 1;
    block_idx = block_idx >= 0 ? block_idx : m_blocks.size() - 1;
    block_idx = block_idx < static_cast<long long>(m_blocks.size()) ? block_idx : 0;

    return static_cast<unsigned>(block_idx)*block_size + offset;
  }

private:

  unsigned m_size;
  unsigned m_last_block_mask;
  View< unsigned *, device_type, MemoryTraits<RandomAccess> > m_blocks;

private:
  template <typename DDevice>
  friend class Bitset;

  template <typename DDevice>
  friend class ConstBitset;

  template <typename Bitset>
  friend struct Impl::BitsetCount;

  template <typename DstDevice, typename SrcDevice>
  friend void deep_copy( Bitset<DstDevice> & dst, Bitset<SrcDevice> const& src);

  template <typename DstDevice, typename SrcDevice>
  friend void deep_copy( Bitset<DstDevice> & dst, ConstBitset<SrcDevice> const& src);
};

/// a thread-safe view to a const bitset
/// i.e. can only test bits
template <typename Device>
class ConstBitset
{
public:
  typedef Device device_type;
  typedef unsigned size_type;

private:
  enum { block_size = static_cast<unsigned>(sizeof(unsigned)*CHAR_BIT) };
  enum { block_mask = block_size -1u };
  enum { block_shift = static_cast<int>(Impl::power_of_two<block_size>::value) };

public:
  ConstBitset()
    : m_size (0)
  {}

  ConstBitset(Bitset<Device> const& rhs)
    : m_size(rhs.m_size)
    , m_blocks(rhs.m_blocks)
  {}

  ConstBitset(ConstBitset<Device> const& rhs)
    : m_size( rhs.m_size )
    , m_blocks( rhs.m_blocks )
  {}

  ConstBitset<Device> & operator = (Bitset<Device> const & rhs)
  {
    this->m_size = rhs.m_size;
    this->m_blocks = rhs.m_blocks;

    return *this;
  }

  ConstBitset<Device> & operator = (ConstBitset<Device> const & rhs)
  {
    this->m_size = rhs.m_size;
    this->m_blocks = rhs.m_blocks;

    return *this;
  }


  KOKKOS_FORCEINLINE_FUNCTION
  unsigned size() const
  {
    return m_size;
  }

  unsigned count() const
  {
    Impl::BitsetCount< ConstBitset<Device> > f(*this);
    return f.apply();
  }

  KOKKOS_FORCEINLINE_FUNCTION
  bool test( unsigned i ) const
  {
    if ( i < m_size ) {
      const unsigned block = m_blocks[ i >> block_shift ];
      const unsigned mask = 1u << static_cast<int>( i & block_mask );
      return block & mask;
    }
    return false;
  }

private:

  unsigned m_size;
  View< const unsigned *, device_type, MemoryTraits<RandomAccess> > m_blocks;

private:
  template <typename DDevice>
  friend class ConstBitset;

  template <typename Bitset>
  friend struct Impl::BitsetCount;

  template <typename DstDevice, typename SrcDevice>
  friend void deep_copy( Bitset<DstDevice> & dst, ConstBitset<SrcDevice> const& src);

  template <typename DstDevice, typename SrcDevice>
  friend void deep_copy( ConstBitset<DstDevice> & dst, ConstBitset<SrcDevice> const& src);
};


template <typename DstDevice, typename SrcDevice>
void deep_copy( Bitset<DstDevice> & dst, Bitset<SrcDevice> const& src)
{
  if (dst.size() != src.size()) {
    throw std::runtime_error("Error: Cannot deep_copy bitsets of different sizes!");
  }

  typedef Kokkos::Impl::DeepCopy< typename DstDevice::memory_space, typename SrcDevice::memory_space > raw_deep_copy;
  raw_deep_copy(dst.m_blocks.ptr_on_device(), src.m_blocks.ptr_on_device(), sizeof(unsigned)*src.m_blocks.size());
}

template <typename DstDevice, typename SrcDevice>
void deep_copy( Bitset<DstDevice> & dst, ConstBitset<SrcDevice> const& src)
{
  if (dst.size() != src.size()) {
    throw std::runtime_error("Error: Cannot deep_copy bitsets of different sizes!");
  }

  typedef Kokkos::Impl::DeepCopy< typename DstDevice::memory_space, typename SrcDevice::memory_space > raw_deep_copy;
  raw_deep_copy(dst.m_blocks.ptr_on_device(), src.m_blocks.ptr_on_device(), sizeof(unsigned)*src.m_blocks.size());
}

template <typename DstDevice, typename SrcDevice>
void deep_copy( ConstBitset<DstDevice> & dst, ConstBitset<SrcDevice> const& src)
{
  if (dst.size() != src.size()) {
    throw std::runtime_error("Error: Cannot deep_copy bitsets of different sizes!");
  }

  typedef Kokkos::Impl::DeepCopy< typename DstDevice::memory_space, typename SrcDevice::memory_space > raw_deep_copy;
  raw_deep_copy(dst.m_blocks.ptr_on_device(), src.m_blocks.ptr_on_device(), sizeof(unsigned)*src.m_blocks.size());
}

} // namespace Kokkos

#endif //KOKKOS_BITSET_HPP
