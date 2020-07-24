/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef LIBSAVVY_TYPED_VALUE_HPP
#define LIBSAVVY_TYPED_VALUE_HPP

#include "compressed_vector.hpp"
#include "sample_subset.hpp"

#include <cstdint>
#include <type_traits>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <array>
#include <cassert>
#include <cstring>

namespace savvy
{
  static const std::vector<std::uint8_t> bcf_type_shift = { 0, 0, 1, 2, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

  static const std::int8_t missing_int8 = 0x80;
  static const std::int16_t missing_int16 = 0x8000;
  static const std::int32_t missing_int32 = 0x80000000;
  static const std::int64_t missing_int64 = 0x8000000080000000;

  static const std::int8_t end_of_vector_int8 = 0x81;
  static const std::int16_t end_of_vector_int16 = 0x8001;
  static const std::int32_t end_of_vector_int32 = 0x80000001;
  static const std::int64_t end_of_vector_int64 = 0x8000000080000001;

  namespace bcf
  {
    template<typename T>
    typename std::enable_if<std::is_signed<T>::value && std::is_integral<T>::value, std::uint8_t>::type
    int_type(T val)
    { // TODO: Handle missing values
      if (val <= std::numeric_limits<std::int8_t>::max() && val >= std::numeric_limits<std::int8_t>::min())
        return 0x01;
      else if (val <= std::numeric_limits<std::int16_t>::max() && val >= std::numeric_limits<std::int16_t>::min())
        return 0x02;
      else if (val <= std::numeric_limits<std::int32_t>::max() && val >= std::numeric_limits<std::int32_t>::min())
        return 0x03;
      else
        return 0x04;
    }

    template <typename Iter, typename IntT>
    Iter deserialize_int(Iter it, Iter end, IntT& dest)
    {
      if (it != end)
      {
        std::uint8_t type_byte = *(it++);
        std::size_t int_width = 1u << bcf_type_shift[type_byte & 0x0Fu];
        if (end - it >= int_width)
        {
          switch (int_width)
          {
          case 1u:
          {
            dest = IntT(*(it++));
            return it;
          }
          case 2u:
          {
            std::int16_t tmp;
            char *tmp_p = (char *)&tmp;
            *(tmp_p) = *(it++);
            *(tmp_p + 1) = *(it++);
            dest = tmp;
            return it;
          }
          case 4u:
          {
            std::int32_t tmp;
            char *tmp_p = (char *)&tmp;
            *(tmp_p) = *(it++);
            *(tmp_p + 1) = *(it++);
            *(tmp_p + 2) = *(it++);
            *(tmp_p + 3) = *(it++);
            dest = tmp;
            return it;
          }
          case 8u:
          {
            std::int64_t tmp;
            char *tmp_p = (char *)&tmp;
            *(tmp_p) = *(it++);
            *(tmp_p + 1) = *(it++);
            *(tmp_p + 2) = *(it++);
            *(tmp_p + 3) = *(it++);
            *(tmp_p + 4) = *(it++);
            *(tmp_p + 5) = *(it++);
            *(tmp_p + 6) = *(it++);
            *(tmp_p + 7) = *(it++);
            dest = tmp;
            return it;
          }
          }
        }
      }
      throw std::runtime_error("Not a BCF integer");
    }

//    template <typename Iter>
//    Iter deserialize_string(Iter it, Iter end, std::string& dest)
//    {
//      if (it == end) return end;
//
//      std::uint8_t type_byte = *(it++);
//      if (it == end || (type_byte & 0x0Fu) != 0x07u)
//        throw std::runtime_error("Not a BCF string");
//
//      std::int32_t sz = (type_byte >> 4u);
//      if (sz == 15)
//        it = deserialize_int(it, end, sz);
//
//      if (end - it < sz)
//        throw std::runtime_error("Invalid byte sequence");
//
//      dest.resize(sz);
//      std::copy_n(it, sz, dest.begin());
//      return it + sz;
//    }

    template <typename Iter, typename VecT>
    typename std::enable_if<std::is_same<typename std::iterator_traits<Iter>::value_type, char>::value, Iter>::type
    deserialize_vec(Iter it, Iter end, VecT& dest)
    {
      if (it == end)
        throw std::runtime_error("Invalid byte sequence");

      std::uint8_t type_byte = *(it++);

      std::int32_t sz = (type_byte >> 4u);
      if (sz == 15)
        it = deserialize_int(it, end, sz);

      std::size_t type_width = 1u << bcf_type_shift[0x0Fu & type_byte];

      if (end - it < sz * type_width)
        throw std::runtime_error("Invalid byte sequence");

      dest.resize(sz);
      char* char_p = &(*it);
      switch (0x0Fu & type_byte)
      {
      case 0x01u:
      {
        auto p = (std::int8_t *)char_p;
        std::copy_n(p, sz, dest.begin());
      }
      case 0x02u:
      {
        auto p = (std::int16_t *)char_p;
        std::copy_n(p, sz, dest.begin()); // TODO: use transform to switch endian when needed
      }
      case 0x03u:
      {
        auto p = (std::int32_t *)char_p;
        std::copy_n(p, sz, dest.begin());
      }
      case 0x04u:
      {
        auto p = (std::int64_t *)char_p;
        std::copy_n(p, sz, dest.begin());
      }
      case 0x05u:
      {
        auto p = (float *)char_p;
        std::copy_n(p, sz, dest.begin());
      }
      case 0x07u:
      {
        std::copy_n(char_p, sz, dest.begin());
      }
      }

      return it + (sz * type_width);
    }

    template <typename OutT, typename T>
    typename std::enable_if<std::is_signed<T>::value && std::is_integral<T>::value, bool>::type
    serialize_typed_int_exact(OutT out_it, const T& val)
    {
      T v = static_cast<T>(val);
      std::uint8_t type;
      if (std::is_same<T, std::int8_t>::value)
        type = 0x01;
      else if (std::is_same<T, std::int16_t>::value)
        type = 0x02;
      else if (std::is_same<T, std::int32_t>::value)
        type = 0x03;
      else if (std::is_same<T, std::int64_t>::value)
        type = 0x04;
      else
        return false;

      *(out_it++) = (1u << 4u) | type;

      char* p_end = ((char*)&v) + sizeof(T);
      for (char* p = (char*)&v; p != p_end; ++p)
      {
        *(out_it++) = *p;
      }
      return true;
    }

    template <typename OutT, typename T>
    typename std::enable_if<std::is_signed<T>::value, bool>::type
    serialize_typed_scalar(OutT out_it, const T& val)
    {
      std::uint8_t type_byte = 1;
      if (std::is_integral<T>::value)
      {
        if (val <= std::numeric_limits<std::int8_t>::max() && val >= std::numeric_limits<std::int8_t>::min())
          return serialize_typed_int_exact(out_it, std::int8_t(val));
        else if (val <= std::numeric_limits<std::int16_t>::max() && val >= std::numeric_limits<std::int16_t>::min())
          return serialize_typed_int_exact(out_it, std::int16_t(val));
        else if (val <= std::numeric_limits<std::int32_t>::max() && val >= std::numeric_limits<std::int32_t>::min())
          return serialize_typed_int_exact(out_it, std::int32_t(val));
        else
          return serialize_typed_int_exact(out_it, std::int64_t(val));
      }

      if (std::is_same<T, float>::value)
        *(out_it++) = (1u << 4u) | 0x05u;
      else if (std::is_same<T, double>::value)
        *(out_it++) = (1u << 4u) | 0x06u;
      else
        return false;

      char* p_end = ((char*)&val) + sizeof(T);
      for (char* p = (char*)&val; p != p_end; ++p)
      {
        *(out_it++) = *p;
      }

      return true;
    }

    template <typename T>
    typename std::enable_if<std::is_signed<T>::value, bool>::type
    write_typed_scalar(std::ostream& os, const T& val)
    {
      std::uint8_t type_byte = 1;
      if (std::is_integral<T>::value)
      {
        if (std::is_same<T, std::int8_t>::value)
        {
          type_byte = (type_byte << 4) | 0x01;
        }
        else if (std::is_same<T, std::int16_t>::value)
        {
          type_byte = (type_byte << 4) | 0x02;
        }
        else if (std::is_same<T, std::int32_t>::value)
        {
          type_byte = (type_byte << 4) | 0x03;
        }
        else
        {
          return false;
        }
      }
      else if (std::is_same<T, float>::value)
      {
        type_byte = (type_byte << 4) | 0x05;
      }
      else
      {
        return false;
      }

      os.write((char*)&type_byte, 1);
      os.write((char*)&val, sizeof(val));
      return true;
    }

    template <typename OutT>
    bool serialize_type_and_size(OutT out_it, std::uint8_t type, std::size_t size)
    {
      if (size < 15)
      {
        *out_it = size << 4u | type;
        ++out_it;
        return true;
      }

      *out_it = 0xF0 | type;
      ++out_it;

      return serialize_typed_scalar(out_it, (std::int64_t)size);
    }

    template <typename Iter, typename T>
    typename std::enable_if<std::is_signed<T>::value, void>::type
    serialize_typed_vec(Iter out_it, const std::vector<T>& vec) // TODO: use smallest int type.
    {
      static_assert(!std::is_same<T, std::int64_t>::value && !std::is_same<T, double>::value, "64-bit integers not allowed in BCF spec.");

      std::uint8_t type_byte = vec.size() < 15 ? vec.size() : 15;
      if (std::is_same<T, std::int8_t>::value)
      {
        type_byte = (type_byte << 4) | 0x01;
      }
      else if (std::is_same<T, std::int16_t>::value)
      {
        type_byte = (type_byte << 4) | 0x02;
      }
      else if (std::is_same<T, std::int32_t>::value)
      {
        type_byte = (type_byte << 4) | 0x03;
      }
      else if (std::is_same<T, float>::value)
      {
        type_byte = (type_byte << 4) | 0x05;
      }

      *out_it = type_byte;

      if (vec.size() >= 15)
      {
        if (vec.size() <= 0x7F)
          serialize_typed_int_exact(out_it, (std::int8_t)vec.size());
        else if (vec.size() <= 0x7FFF)
          serialize_typed_int_exact(out_it, (std::int16_t)vec.size());
        else if (vec.size() <= 0x7FFFFFFF)
          serialize_typed_int_exact(out_it, (std::int32_t)vec.size());
        else
          throw std::runtime_error("string too big");
      }

      std::copy_n((char*)vec.data(), sizeof(T) * vec.size(), out_it);
    }

    template <typename T>
    typename std::enable_if<std::is_signed<T>::value, void>::type
    write_typed_vec(std::ostream& os, const std::vector<T>& vec)
    {
      static_assert(!std::is_same<T, std::int64_t>::value && !std::is_same<T, double>::value, "64-bit integers not allowed in BCF spec.");

      std::uint8_t type_byte = vec.size() < 15 ? vec.size() : 15;
      if (std::is_same<T, std::int8_t>::value)
      {
        type_byte = (type_byte << 4) | 0x01;
      }
      else if (std::is_same<T, std::int16_t>::value)
      {
        type_byte = (type_byte << 4) | 0x02;
      }
      else if (std::is_same<T, std::int32_t>::value)
      {
        type_byte = (type_byte << 4) | 0x03;
      }
      else if (std::is_same<T, float>::value)
      {
        type_byte = (type_byte << 4) | 0x05;
      }

      os.write((char*)&type_byte, 1);

      if (vec.size() >= 15)
      {
        if (vec.size() <= 0x7F)
          write_typed_scalar(os, (std::int8_t)vec.size());
        else if (vec.size() <= 0x7FFF)
          write_typed_scalar(os, (std::int16_t)vec.size());
        else if (vec.size() <= 0x7FFFFFFF)
          write_typed_scalar(os, (std::int32_t)vec.size());
        else
          throw std::runtime_error("vector too big");
      }

      os.write((char*)vec.data(), std::int32_t(sizeof(T) * vec.size()));
    }

    template <typename OutT>
    void serialize_typed_str(OutT out_it, const std::string& str)
    {
      std::uint8_t type_byte = str.size() < 15 ? str.size() : 15;
      type_byte = (type_byte << 4) | 0x07;

      *out_it = type_byte;

      if (str.size() >= 15)
      {
        if (str.size() <= 0x7F)
          serialize_typed_int_exact(out_it, (std::int8_t)str.size());
        else if (str.size() <= 0x7FFF)
          serialize_typed_int_exact(out_it, (std::int16_t)str.size());
        else if (str.size() <= 0x7FFFFFFF)
          serialize_typed_int_exact(out_it, (std::int32_t)str.size());
        else
          throw std::runtime_error("string too big");
      }

      std::copy_n(str.begin(), str.size(), out_it);
    }

    inline void write_typed_str(std::ostream& os, const std::string& str)
    {
      std::uint8_t type_byte = str.size() < 15 ? str.size() : 15;
      type_byte = (type_byte << 4) | 0x07;


      os.write((char*)&type_byte, 1);
      if (str.size() >= 15)
      {
        if (str.size() <= 0x7F)
          serialize_typed_int_exact(std::ostreambuf_iterator<char>(os), (std::int8_t)str.size());
        else if (str.size() <= 0x7FFF)
          serialize_typed_int_exact(std::ostreambuf_iterator<char>(os), (std::int16_t)str.size());
        else if (str.size() <= 0x7FFFFFFF)
          serialize_typed_int_exact(std::ostreambuf_iterator<char>(os), (std::int32_t)str.size());
        else
          throw std::runtime_error("string too big");
      }

      os.write(str.data(), str.size());
    }

    template <typename T>
    typename std::enable_if<std::is_signed<typename T::value_type>::value, std::uint32_t>::type
    get_typed_value_size(const T& vec)
    {
      static_assert(!std::is_same<typename T::value_type, std::int64_t>::value && !std::is_same<typename T::value_type, double>::value, "64-bit integers not allowed in BCF spec.");
      std::uint32_t ret;
      if (vec.size() < 15)
        ret = 1;
      else if (vec.size() <= 0x7F)
        ret = 2 + 1;
      else if (vec.size() <= 0x7FFF)
        ret = 2 + 2;
      else if (vec.size() <= 0x7FFFFFFF)
        ret = 2 + 4;
      else
        return -1; // vec too big

      ret += vec.size() * sizeof(typename T::value_type);
      return ret;
    }

    template <typename T>
    typename std::enable_if<std::is_signed<T>::value, std::uint32_t>::type
    get_typed_value_size(T vec)
    {
      static_assert(!std::is_same<T, std::int64_t>::value, "64-bit integers not allowed in BCF spec.");
      return 1 + sizeof(T);
    }
  }

  namespace v2
  {
    class reader;
    class writer;
    class variant;
  }

  class typed_value
  {
    friend class v2::reader;
    friend class v2::writer;
    friend class v2::variant;
  public:
    static const std::uint8_t int8 = 1;
    static const std::uint8_t int16 = 2;
    static const std::uint8_t int32 = 3;
    static const std::uint8_t int64 = 4;
    static const std::uint8_t real = 5;
    static const std::uint8_t real64 = 6;
    static const std::uint8_t str = 7;
    static const std::uint8_t sparse = 8;

//    template<typename T>
//    static T missing_value()
//    {
//      std::uint8_t tcode = type_code<T>();
//
//      if (tcode >= 1 && tcode <= 4)
//        return std::numeric_limits<T>::min();
//
//      if (tcode == 5)
//      {
//        std::uint32_t i = 0x7F800001;
//        T ret;
//        std::memcpy(&ret, &i, sizeof(i));
//        return ret;
//      }
//
//      if (tcode == 6)
//      {
//        std::uint64_t i = 0x7FF0000000000001;
//        T ret;
//        std::memcpy(&ret, &i, sizeof(i));
//      }
//
////      if (tcode == 7)
////        return 0x07;
//
//      return T();
//    }

    template<typename T>
    inline static T missing_value();

    template<typename T>
    inline static T end_of_vector_value();

    template<typename T>
    static typename std::enable_if<std::is_signed<T>::value && std::is_integral<T>::value, bool>::type
    is_missing(const T& v);

    template<typename T>
    static typename std::enable_if<std::is_same<T, float>::value || std::is_same<T, double>::value, bool>::type
    is_missing(const T& v);

    template<typename T>
    inline static bool is_end_of_vector(const T& v);

    template<typename T>
    static typename std::enable_if<std::is_signed<T>::value, std::uint8_t>::type
    type_code();

    template<typename T>
    static typename std::enable_if<std::is_signed<T>::value, std::uint8_t>::type
    type_code(const T& val);

    template<typename T>
    static typename std::enable_if<std::is_signed<T>::value, std::uint8_t>::type
    type_code_ignore_missing(const T& val);

    template<typename T>
    static typename std::enable_if<std::is_unsigned<T>::value, std::uint8_t>::type
    offset_type_code(const T& val);

    template<typename DestT, typename SrcT>
    static DestT reserved_transformation(SrcT in);


    template <typename T>
    void bcf_encode_gt(T* val_ptr, T* val_end_ptr, bool phased)
    {
      for ( ; val_ptr != val_end_ptr; ++val_ptr)
      {
        if (*val_ptr == std::numeric_limits<T>::min() + 1) return;
        if (*val_ptr == std::numeric_limits<T>::min())
          *val_ptr = T(-1);
        *val_ptr = ((*val_ptr + 1) << 1u) | T(phased); // TODO: restrict values so that they fit (max val for int8_t is 126).
      }
    }


    template <typename T>
    void bcf_encode_gt_ph(T* val_ptr, T* val_end_ptr, const std::int8_t* phase, std::size_t stride)
    {
      for (std::size_t i = 0; val_ptr != val_end_ptr; ++val_ptr, ++i)
      {
        if (*val_ptr != std::numeric_limits<T>::min() + 1)
        {
          if (*val_ptr == std::numeric_limits<T>::min())
            *val_ptr = T(-1);
          if (i % stride)
            *val_ptr = ((*val_ptr + 1) << 1u) | T(*phase++);
          else
            *val_ptr = ((*val_ptr + 1) << 1u);
        }
        ++i;
      }
    }

    void bcf_encode_gt_ph(float* val_ptr, float* val_end_ptr, const std::int8_t* phase, std::size_t stride) { }


    class bcf_gt_encoder
    {
    public:
      template <typename T>
      void operator()(T* val_ptr, T* val_end_ptr, bool phased)
      {
        for ( ; val_ptr != val_end_ptr; ++val_ptr)
        {
          if (*val_ptr == std::numeric_limits<T>::min() + 1) return;
          if (*val_ptr == std::numeric_limits<T>::min())
            *val_ptr = T(-1);
          *val_ptr = ((*val_ptr + 1) << 1u) | T(phased); // TODO: restrict values so that they fit (max val for int8_t is 126).
        }
      }

      template <typename T>
      void operator()(T* val_ptr, T* val_end_ptr, const std::int8_t* phase, std::size_t stride)
      {
        for (std::size_t i = 0; val_ptr != val_end_ptr; ++val_ptr,++i)
        {
          if (*val_ptr != std::numeric_limits<T>::min() + 1)
          {
            if (*val_ptr == std::numeric_limits<T>::min())
              *val_ptr = T(-1);
            if (i % stride)
              *val_ptr = ((*val_ptr + 1) << 1u) | T(*phase++); // TODO: restrict values so that they fit (max val for int8_t is 126).
            else
              *val_ptr = ((*val_ptr + 1) << 1u);
          }
        }
      }

      void operator()(float* val_ptr, float* val_end_ptr, bool phased) { return; }
      void operator()(float* val_ptr, float* val_end_ptr, const std::int8_t* phase, std::size_t stride) { return; }
    };

    class bcf_gt_decoder
    {
    public:
      bcf_gt_decoder()
      {}

      template <typename T>
      void operator()(T* valp, T* endp)
      {
        for ( ; valp != endp; ++valp)
        {
          if (*valp == std::numeric_limits<T>::min() + 1) return;
          *valp = T(unsigned(*valp) >> 1u) - 1;
          if (*valp == -1)
            *valp = std::numeric_limits<T>::min();
        }
      }

      template <typename T>
      void operator()(T* valp, T* endp, std::int8_t* phasep, std::size_t stride)
      {
        for (std::size_t i = 0; valp != endp; ++valp,++i)
        {
          std::int8_t ph;

          if (*valp == std::numeric_limits<T>::min() + 1)
          {
            ph = std::int8_t(0x81);
          }
          else
          {
            ph = 0x1 & *valp;
            *valp = T(unsigned(*valp) >> 1u) - 1;
            if (*valp == -1)
              *valp = std::numeric_limits<T>::min();
          }

          if (i % stride)
            (*phasep++) = ph;
        }
      }

      void operator()(float* valp, float* endp) { return; }
      void operator()(float* valp, float* endp, std::int8_t* ph, std::size_t stride) { return; }
    };

    enum class get_status : std::uint8_t
    {
      ok = 0,
      does_not_fit,
      not_a_scalar,
      not_a_vector
    };

    template<typename T>
    class compressed_offset_iterator
    {
    public:
      typedef compressed_offset_iterator self_type;
      typedef std::ptrdiff_t difference_type;
      typedef std::size_t value_type;
      typedef void reference;
      typedef void pointer;
      typedef std::input_iterator_tag iterator_category;

      compressed_offset_iterator() : ptr_(nullptr), last_offset_(0) {}

      compressed_offset_iterator(const T *p) :
        ptr_(p),
        last_offset_(0)
      {

      }

      self_type operator++()
      {
        self_type ret = *this;
        last_offset_ += (*ptr_) + 1;
        ++ptr_;
        return ret;
      }

      void operator++(int)
      {
        last_offset_ += (*ptr_) + 1;
        ++ptr_;
      }

      value_type operator*() const { return last_offset_ + (*ptr_); }

      //const pointer operator->() const { return &uncompressed_offset_; }
      bool operator==(const self_type& rhs) const { return (ptr_ == rhs.ptr_); }

      bool operator!=(const self_type& rhs) const { return (ptr_ != rhs.ptr_); }

    private:
      const T *ptr_;
      value_type last_offset_;
    };

  public:
    typed_value() {}

    template<typename T>
    typed_value(const T& v)
    {
      init(v);
    }

    typed_value(std::uint8_t type, std::size_t sz, char *data_ptr);
    typed_value(std::uint8_t val_type, std::size_t sz, std::uint8_t off_type, std::size_t sp_sz, char *data_ptr);
    typed_value(std::int8_t type, char* str, char*const str_end);

    void init(std::uint8_t type, std::size_t sz, char *data_ptr);
    void init(std::uint8_t val_type, std::size_t sz, std::uint8_t off_type, std::size_t sp_sz, char *data_ptr);

    typed_value(typed_value&& src)
    {
      operator=(std::move(src));
    }

    std::size_t size() const { return size_; }

    bool is_sparse() const { return off_ptr_ != nullptr; }

    template<typename T>
    typed_value& operator=(const T& v)
    {
      if ((void *) this != (void *) &v)
      {
        clear();
        init(v);
      }
      return *this;
    }

    typed_value& operator=(typed_value&& src);

    bool copy_as_dense(typed_value& dest) const
    {
      if (off_ptr_)
      {
        dest.local_data_.resize(0);
        dest.local_data_.resize(size_ * (1u << bcf_type_shift[val_type_]));

        switch (val_type_)
        {
        case 0x01u:
        {
          auto p = (std::int8_t*)dest.local_data_.data();
          return copy_sparse1<std::int8_t>(p);
        }
        case 0x02u:
        {
          auto p = (std::int16_t*)dest.local_data_.data();
          return copy_sparse1<std::int16_t>(p); // TODO: handle endianess
        }
        case 0x03u:
        {
          auto p = (std::int32_t*)dest.local_data_.data();
          return copy_sparse1<std::int32_t>(p);
        }
        case 0x04u:
        {
          auto p = (std::int64_t*)dest.local_data_.data();
          return copy_sparse1<std::int64_t>(p);
        }
        case 0x05u:
        {
          auto p = (float*)dest.local_data_.data();
          return copy_sparse1<float>(p);
        }
        default:
          return false;
        }
      }
      else if (val_ptr_)
      {
        dest.local_data_.resize(size_ * (1u << bcf_type_shift[val_type_]));
        switch (val_type_)
        {
        case 0x01u:
          std::copy_n((std::int8_t*)val_ptr_, size_, (std::int8_t*)dest.local_data_.data());
          break;
        case 0x02u:
          std::copy_n((std::int16_t*)val_ptr_, size_, (std::int16_t*)dest.local_data_.data()); // TODO: handle endianess
          break;
        case 0x03u:
          std::copy_n((std::int32_t*)val_ptr_, size_, (std::int32_t*)dest.local_data_.data());
          break;
        case 0x04u:
          std::copy_n((std::int64_t*)val_ptr_, size_, (std::int64_t*)dest.local_data_.data());
          break;
        case 0x05u:
          std::copy_n((float*)val_ptr_, size_, (float*)dest.local_data_.data());
          break;
        default:
          return false;
        }
        dest.val_type_ = val_type_;
        dest.size_ = size_;
        dest.val_ptr_ = dest.local_data_.data();
      }

      return true;
    }

//    struct subset_functor
//    {
//      template <typename ValT, typename OffT>
//      void operator()(ValT* val_ptr, ValT* val_end, OffT* off_ptr, const std::vector<std::size_t>& subset_map, std::size_t subset_size, typed_value& parent)
//      {
//        std::size_t stride = parent.size_ / subset_map.size();
//        std::uint8_t max_off_type = offset_type_code(subset_size);
//        std::uint8_t dest_off_type = parent.off_type_;
//
//        if (parent.sparse_size_)
//        {
//          std::size_t map_offset = 0;
//          OffT* off_end = off_ptr + (val_end - val_ptr);
//          std::size_t new_off = 0;
//          for (OffT* off_it = off_ptr + 1; max_off_type > dest_off_type && off_it != off_end; ++off_it)
//          {
//            map_offset += *(off_it - 1);
//            if (subset_map[map_offset] == std::numeric_limits<std::size_t>::max())
//            {
//              new_off = *(off_it - 1) + *off_it + 1;
//              dest_off_type = offset_type_code(new_off);
//            }
//          }
//        }
//
//      }
//
//      template <typename T>
//      void operator()(T* valp, T* endp, const std::vector<std::size_t>& subset_map, std::size_t stride)
//      {
//        for (std::size_t i = 0; i < subset_map.size(); ++i)
//        {
//          if (subset_map[i] < std::numeric_limits<std::size_t>::max())
//          {
//            for (std::size_t j = 0; j < stride; ++j)
//              valp[subset_map[i] * stride + j] = valp[i * stride + j];
//          }
//        }
//      }
//    };
//
//    bool subset(const std::vector<std::size_t>& subset_map)
//    {
//      if (!size_ || size_ % subset_map.size() != 0)
//        return false;
//
//      std::size_t stride = size_ / subset_map.size();
//
//      if (off_ptr_)
//        return apply_sparse(subset_functor(), subset_map, stride);
//      else if (val_ptr_)
//        return apply(subset_functor(), subset_map, stride);
//
//      return true;
//    }

    template <typename ValT, typename Fn, typename... Args>
    bool apply_sparse_offsets(Fn fn, Args... args)
    {
      if (!off_ptr_)
        return false;
      switch (off_type_)
      {
      case 0x01u:
        fn((ValT*)val_ptr_, ((ValT*)val_ptr_) + sparse_size_, (std::uint8_t*)off_ptr_, args...);
        break;
      case 0x02u:
        fn((ValT*)val_ptr_, ((ValT*)val_ptr_) + sparse_size_, (std::uint16_t*)off_ptr_, args...); // TODO: handle endianess
        break;
      case 0x03u:
        fn((ValT*)val_ptr_, ((ValT*)val_ptr_) + sparse_size_, (std::uint32_t*)off_ptr_, args...);
        break;
      case 0x04u:
        fn((ValT*)val_ptr_, ((ValT*)val_ptr_) + sparse_size_, (std::uint64_t*)off_ptr_, args...);
        break;
      default:
        return false;
      }
      return true;
    }

    template <typename Fn, typename... Args>
    bool apply_sparse(Fn fn, Args... args)
    {
      switch (val_type_)
      {
      case 0x01u:
        return apply_sparse_offsets<std::int8_t>(std::forward<Fn>(fn), std::forward<Args>(args)...);
      case 0x02u:
        return apply_sparse_offsets<std::int16_t>(std::forward<Fn>(fn), std::forward<Args>(args)...); // TODO: handle endianess
      case 0x03u:
        return apply_sparse_offsets<std::int32_t>(std::forward<Fn>(fn), std::forward<Args>(args)...);
      case 0x04u:
        return apply_sparse_offsets<std::int64_t>(std::forward<Fn>(fn), std::forward<Args>(args)...);
      case 0x05u:
        return apply_sparse_offsets<float>(std::forward<Fn>(fn), std::forward<Args>(args)...);
      case 0x07u:
        return apply_sparse_offsets<char>(std::forward<Fn>(fn), std::forward<Args>(args)...);
      default:
        return false;
      }
    }

    template <typename ValT, typename Fn, typename... Args>
    bool capply_sparse_offsets(Fn fn, Args... args) const
    {
      if (!off_ptr_)
        return false;
      switch (off_type_)
      {
      case 0x01u:
        fn((const ValT*)val_ptr_, ((const ValT*)val_ptr_) + sparse_size_, (const std::uint8_t*)off_ptr_, args...);
        break;
      case 0x02u:
        fn((const ValT*)val_ptr_, ((const ValT*)val_ptr_) + sparse_size_, (const std::uint16_t*)off_ptr_, args...); // TODO: handle endianess
        break;
      case 0x03u:
        fn((const ValT*)val_ptr_, ((const ValT*)val_ptr_) + sparse_size_, (const std::uint32_t*)off_ptr_, args...);
        break;
      case 0x04u:
        fn((const ValT*)val_ptr_, ((const ValT*)val_ptr_) + sparse_size_, (const std::uint64_t*)off_ptr_, args...);
        break;
      default:
        return false;
      }
      return true;
    }

    template <typename Fn, typename... Args>
    bool capply_sparse(Fn fn, Args... args) const
    {
      switch (val_type_)
      {
      case 0x01u:
        return capply_sparse_offsets<std::int8_t>(std::forward<Fn>(fn), std::forward<Args>(args)...);
      case 0x02u:
        return capply_sparse_offsets<std::int16_t>(std::forward<Fn>(fn), std::forward<Args>(args)...); // TODO: handle endianess
      case 0x03u:
        return capply_sparse_offsets<std::int32_t>(std::forward<Fn>(fn), std::forward<Args>(args)...);
      case 0x04u:
        return capply_sparse_offsets<std::int64_t>(std::forward<Fn>(fn), std::forward<Args>(args)...);
      case 0x05u:
        return capply_sparse_offsets<float>(std::forward<Fn>(fn), std::forward<Args>(args)...);
      case 0x07u:
        return capply_sparse_offsets<char>(std::forward<Fn>(fn), std::forward<Args>(args)...);
      default:
        return false;
      }
    }

    template <typename Fn, typename... Args>
    bool apply(Fn fn, Args... args)
    {
      std::size_t sz = off_ptr_ ? sparse_size_ : size_;

      switch (val_type_)
      {
      case 0x01u:
        fn((std::int8_t*)val_ptr_, ((std::int8_t*)val_ptr_) + sz, args...);
        break;
      case 0x02u:
        fn((std::int16_t*)val_ptr_, ((std::int16_t*)val_ptr_) + sz, args...); // TODO: handle endianess
        break;
      case 0x03u:
        fn((std::int32_t*)val_ptr_, ((std::int32_t*)val_ptr_) + sz, args...);
        break;
      case 0x04u:
        fn((std::int64_t*)val_ptr_, ((std::int64_t*)val_ptr_) + sz, args...);
        break;
      case 0x05u:
        fn((float*)val_ptr_, ((float*)val_ptr_) + sz, args...);
        break;
      case 0x07u:
        fn(val_ptr_, val_ptr_ + sz, args...);
        break;
      default:
        return false;
      }
      return true;
    }

    template <typename Fn, typename... Args>
    bool capply(Fn fn, Args... args) const
    {
      std::size_t sz = off_ptr_ ? sparse_size_ : size_;

      switch (val_type_)
      {
      case 0x01u:
        fn((const std::int8_t*)val_ptr_, ((const std::int8_t*)val_ptr_) + sz, args...);
        break;
      case 0x02u:
        fn((const std::int16_t*)val_ptr_, ((const std::int16_t*)val_ptr_) + sz, args...); // TODO: handle endianess
        break;
      case 0x03u:
        fn((const std::int32_t*)val_ptr_, ((const std::int32_t*)val_ptr_) + sz, args...);
        break;
      case 0x04u:
        fn((const std::int64_t*)val_ptr_, ((const std::int64_t*)val_ptr_) + sz, args...);
        break;
      case 0x05u:
        fn((const float*)val_ptr_, ((const float*)val_ptr_) + sz, args...);
        break;
      case 0x07u:
        fn((const char*)val_ptr_, ((const char*)val_ptr_) + sz, args...);
        break;
      default:
        return false;
      }
      return true;
    }

    template <typename Fn>
    bool foreach_value(Fn& fn)
    {
      std::size_t sz = off_ptr_ ? sparse_size_ : size_;
      switch (val_type_)
      {
      case 0x01u:
        {
          typedef std::int8_t T;
          std::for_each((T*)val_ptr_, ((T*)val_ptr_) + sz, fn);
        }
        break;
      case 0x02u:
        {
          typedef std::int16_t T;
          std::for_each((T*)val_ptr_, ((T*)val_ptr_) + sz, fn); // TODO: handle endianess
        }
        break;
      case 0x03u:
        {
          typedef std::int32_t T;
          std::for_each((T*)val_ptr_, ((T*)val_ptr_) + sz, fn);
        }
        break;
      case 0x04u:
        {
          typedef std::int64_t T;
          std::for_each((T*)val_ptr_, ((T*)val_ptr_) + sz, fn);
        }
        break;
      case 0x05u:
        {
          typedef float T;
          std::for_each((T*)val_ptr_, ((T*)val_ptr_) + sz, fn);
        }
        break;
      default:
        return false;
      }

      return true;
    }

    template<typename T>
    bool get(T& dest) const
    {
      if (!val_ptr_ || size_ == 0)
        return false;

      switch (val_type_)
      {
      case 0x01u:
        dest = reserved_transformation<T>(*((std::int8_t*) val_ptr_));
        break;
      case 0x02u:
        dest = reserved_transformation<T>(*((std::int16_t*) val_ptr_)); // TODO: handle endianess
        break;
      case 0x03u:
        dest = reserved_transformation<T>(*((std::int32_t*) val_ptr_));
        break;
      case 0x04u:
        dest = reserved_transformation<T>(*((std::int64_t*) val_ptr_));
        break;
      case 0x05u:
        dest = *((float*)val_ptr_);
        break;
      default:
        return false;
      }

      return true;
    }

    bool get(std::string& dest) const
    {
      if (!val_ptr_ || size_ == 0)
        return false;

      switch (val_type_)
      {
//      case 0x01u:
//        dest = missing_transformation<T>(*((std::int8_t*)val_ptr_));
//        break;
//      case 0x02u:
//        dest = missing_transformation<T>(*((std::int16_t*)val_ptr_)); // TODO: handle endianess
//        break;
//      case 0x03u:
//        dest = missing_transformation<T>(*((std::int32_t*)val_ptr_));
//        break;
//      case 0x04u:
//        dest = missing_transformation<T>(*((std::int64_t*)val_ptr_));
//        break;
      case 0x07u:
        dest.assign(val_ptr_, val_ptr_ + size_);
        break;
      default:
        return false;
      }

      return true;
    }


    template<typename T>
    bool get(std::vector<T>& dest) const // TOOD: handle missing / end_of_vector
    {
      static_assert(std::is_signed<T>::value, "Destination value_type must be signed.");
      static_assert(!std::is_same<T, char>::value, "Destination value_type cannot be char. Use std::int8_t instead.");

      if (val_type_ == 0x07u) return false;

      if (off_ptr_)
      {
        dest.resize(0);
        dest.resize(size_);
        switch (val_type_)
        {
        case 0x01u:
          return copy_sparse1<std::int8_t>(dest);
        case 0x02u:
          return copy_sparse1<std::int16_t>(dest); // TODO: handle endianess
        case 0x03u:
          return copy_sparse1<std::int32_t>(dest);
        case 0x04u:
          return copy_sparse1<std::int64_t>(dest);
        case 0x05u:
          return copy_sparse1<float>(dest);
        default:
          return false;
        }
      }
      else if (val_ptr_)
      {
        dest.resize(size_);
        switch (val_type_)
        {
        case 0x01u:
          std::copy_n((std::int8_t*)val_ptr_, size_, dest.data());
          break;
        case 0x02u:
          std::copy_n((std::int16_t*)val_ptr_, size_, dest.data()); // TODO: handle endianess
          break;
        case 0x03u:
          std::copy_n((std::int32_t*)val_ptr_, size_, dest.data());
          break;
        case 0x04u:
          std::copy_n((std::int64_t*)val_ptr_, size_, dest.data());
          break;
        case 0x05u:
          std::copy_n((float*)val_ptr_, size_, dest.data());
          break;
        default:
          return false;
        }
      }
      else
      {
        return false;
      }
      return true;
    }

    struct subset_samples_functor
    {
      template <typename ValT, typename OffT, typename DestT>
      void operator()(const ValT* val_ptr, const ValT* val_end, const OffT* off_ptr, const std::vector<std::size_t>& subset_map, std::size_t stride, DestT& dest)
      {
        std::size_t total_offset = 0;
        std::size_t sp_sz = val_ptr - val_end;
        for (std::size_t i = 0; i < sp_sz; ++i)
        {
          total_offset += ((const OffT *) off_ptr)[i];;
          if (subset_map[total_offset] < std::numeric_limits<std::size_t>::max())
            dest[subset_map[total_offset / stride] * stride + (total_offset % stride)] = reserved_transformation<typename DestT::value_type>(((const ValT*) val_ptr)[i]);
          ++total_offset;
        }
      }

      template <typename T, typename DestT>
      void operator()(const T* valp, const T* endp, const std::vector<std::size_t>& subset_map, compressed_vector<DestT>& dest)
      {
        std::size_t sz = endp - valp;
        std::size_t stride = sz / subset_map.size();

        for (std::size_t i = 0; i < subset_map.size(); ++i)
        {
          if (subset_map[i] < std::numeric_limits<std::size_t>::max())
          {
            for (std::size_t j = 0; j < stride; ++j)
            {
              T v = valp[i * stride + j];
              if (v)
                dest[subset_map[i] * stride + j] = reserved_transformation<DestT>(v);
            }
          }
        }
      }

      template <typename T, typename DestT>
      void operator()(const T* valp, const T* endp, const std::vector<std::size_t>& subset_map, std::vector<DestT>& dest)
      {
        std::size_t sz = endp - valp;
        std::size_t stride = sz / subset_map.size();

        for (std::size_t i = 0; i < subset_map.size(); ++i)
        {
          if (subset_map[i] < std::numeric_limits<std::size_t>::max())
          {
            for (std::size_t j = 0; j < stride; ++j)
              dest[subset_map[i] * stride + j] = reserved_transformation<DestT>(valp[i * stride + j]); // TODO: handle missing / end_of_vector
          }
        }
      }
    };

    template<typename T>
    bool get(std::vector<T>& dest, const sample_subset& subset) const
    {
      static_assert(std::is_signed<T>::value, "Destination value_type must be signed.");
      static_assert(!std::is_same<T, char>::value, "Destination value_type cannot be char. Use std::int8_t instead.");

      if (val_type_ != 0x07u && size_ % subset.mask().size() == 0)
      {
        std::size_t stride = size_ % subset.mask().size();
        if (off_ptr_)
        {
          dest.resize(0);
          dest.resize(subset.ids().size() * stride);
          return capply_sparse(subset_samples_functor(), subset.mask(), stride, dest); // TODO: handle endianess
        }
        else if (val_ptr_)
        {
          dest.resize(subset.ids().size() * stride);
          return capply(subset_samples_functor(), subset.mask(), dest); // TODO: handle endianess
        }
      }
      return false;
    }

    template<typename T>
    bool get(savvy::compressed_vector<T>& dest) const
    {
      static_assert(std::is_signed<T>::value, "Destination value_type must be signed.");
      static_assert(!std::is_same<T, char>::value, "Destination value_type cannot be char. Use std::int8_t instead.");

      if (val_type_ == 0x07u) return false;

      if (off_ptr_)
      {
        // TODO: support relative offsets
        dest.resize(0);

        switch (val_type_)
        {
        case 0x01u:
        {
          auto *vp = (std::int8_t *) val_ptr_;
          switch (off_type_)
          {
          case 0x01u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint8_t>((std::uint8_t *) off_ptr_), size_);
            break;
          case 0x02u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint16_t>((std::uint16_t *) off_ptr_), size_);
            break; // TODO: handle endianess
          case 0x03u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint32_t>((std::uint32_t *) off_ptr_), size_);
            break;
          case 0x04u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint64_t>((std::uint64_t *) off_ptr_), size_);
            break;
          default:
            return false;
          }
          break;
        }
        case 0x02u:
        {
          auto *vp = (std::int16_t *) val_ptr_;
          switch (off_type_)
          {
          case 0x01u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint8_t>((std::uint8_t *) off_ptr_), size_);
            break;
          case 0x02u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint16_t>((std::uint16_t *) off_ptr_), size_);
            break; // TODO: handle endianess
          case 0x03u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint32_t>((std::uint32_t *) off_ptr_), size_);
            break;
          case 0x04u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint64_t>((std::uint64_t *) off_ptr_), size_);
            break;
          default:
            return false;
          }
          break;
        }// TODO: handle endianess
        case 0x03u:
        {
          auto *vp = (std::int32_t *) val_ptr_;
          switch (off_type_)
          {
          case 0x01u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint8_t>((std::uint8_t *) off_ptr_), size_);
            break;
          case 0x02u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint16_t>((std::uint16_t *) off_ptr_), size_);
            break; // TODO: handle endianess
          case 0x03u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint32_t>((std::uint32_t *) off_ptr_), size_);
            break;
          case 0x04u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint64_t>((std::uint64_t *) off_ptr_), size_);
            break;
          default:
            return false;
          }
          break;
        }
        case 0x04u:
        {
          auto *vp = (std::int64_t *) val_ptr_;
          switch (off_type_)
          {
          case 0x01u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint8_t>((std::uint8_t *) off_ptr_), size_);
            break;
          case 0x02u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint16_t>((std::uint16_t *) off_ptr_), size_);
            break; // TODO: handle endianess
          case 0x03u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint32_t>((std::uint32_t *) off_ptr_), size_);
            break;
          case 0x04u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint64_t>((std::uint64_t *) off_ptr_), size_);
            break;
          default:
            return false;
          }
          break;
        }
        case 0x05u:
        {
          auto *vp = (float *) val_ptr_;
          switch (off_type_)
          {
          case 0x01u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint8_t>((std::uint8_t *) off_ptr_), size_);
            break;
          case 0x02u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint16_t>((std::uint16_t *) off_ptr_), size_);
            break; // TODO: handle endianess
          case 0x03u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint32_t>((std::uint32_t *) off_ptr_), size_);
            break;
          case 0x04u:
            dest.assign(vp, vp + sparse_size_, compressed_offset_iterator<std::uint64_t>((std::uint64_t *) off_ptr_), size_);
            break;
          default:
            return false;
          }
          break;
        }
        default:
          return false;
        }

        return true;
      }
      else if (val_ptr_)
      {
        //dest.resize(0);
        switch (val_type_)
        {
        case 0x01u:
        {
          auto *p = (std::int8_t *) val_ptr_;
          dest.assign(p, p + size_);
          break;
        }
        case 0x02u:
        {
          auto *p = (std::int16_t *) val_ptr_;
          dest.assign(p, p + size_);
          break;
        }// TODO: handle endianess
        case 0x03u:
        {
          auto *p = (std::int32_t *) val_ptr_;
          dest.assign(p, p + size_);
          break;
        }
        case 0x04u:
        {
          auto *p = (std::int64_t *) val_ptr_;
          dest.assign(p, p + size_);
          break;
        }
        case 0x05u:
        {
          auto *p = (float *) val_ptr_;
          dest.assign(p, p + size_);
          break;
        }
        default:
          return false;
        }
        return true;
      }

      return false;
    }

    friend std::ostream& operator<<(std::ostream& os, const typed_value& val);

    class internal
    {
    public:
      static void pbwt_unsort(typed_value& v, std::vector<std::size_t>& sort_mapping, std::vector<std::size_t>& prev_sort_mapping, std::vector<std::size_t>& counts);

      template<typename InIter, typename OutIter>
      static void pbwt_sort(InIter in_data, std::size_t in_data_size, OutIter out_it, std::vector<std::size_t>& sort_mapping, std::vector<std::size_t>& prev_sort_mapping, std::vector<std::size_t>& counts);

      template<typename Iter>
      static void serialize(const typed_value& v, Iter out_it, std::size_t size_divisor);

      template<typename Iter>
      static void serialize(const typed_value& v, Iter out_it, std::vector<std::size_t>& sort_mapping, std::vector<std::size_t>& prev_sort_mapping, std::vector<std::size_t>& counts);
    };

  private:
    void clear()
    {
      sparse_size_ = 0;
      size_ = 0;
      off_ptr_ = nullptr;
      val_ptr_ = nullptr;
      off_type_ = 0;
      val_type_ = 0;
      local_data_.clear();
    }

    void serialize_vcf(std::size_t idx, std::ostream& os) const
    {
      assert(!off_ptr_ && idx < size_);

      switch (val_type_)
      {
      case 0x01u:
        os << static_cast<int>(((std::int8_t*)val_ptr_)[idx]);
        break;
      case 0x02u:
        os << ((std::int16_t*)val_ptr_)[idx]; // TODO: handle endianess
        break;
      case 0x03u:
        os << ((std::int32_t*)val_ptr_)[idx];
        break;
      case 0x04u:
        os << ((std::int64_t*)val_ptr_)[idx];
        break;
      case 0x05u:
        os << ((float*)val_ptr_)[idx];
        break;
      default:
        os.setstate(os.rdstate() | std::ios::failbit);
      }
    }

    void deserialize_vcf(std::size_t idx, std::size_t length, char* str);

    template<typename ValT, typename OffT, typename DestT>
    void copy_sparse2(DestT& dest) const
    {
      std::size_t total_offset = 0;
      for (std::size_t i = 0; i < sparse_size_; ++i)
      {
        int tmp_off = ((const OffT *) off_ptr_)[i];
        total_offset += tmp_off;
        dest[total_offset++] = ((const ValT *) val_ptr_)[i];
      }
    }

    template<typename ValT, typename DestT>
    bool copy_sparse1(DestT& dest) const
    {
      switch (off_type_)
      {
      case 0x01u:
        copy_sparse2<ValT, std::uint8_t>(dest);
        break;
      case 0x02u:
        copy_sparse2<ValT, std::uint16_t>(dest); // TODO: handle endianess
        break;
      case 0x03u:
        copy_sparse2<ValT, std::uint32_t>(dest);
        break;
      case 0x04u:
        copy_sparse2<ValT, std::uint64_t>(dest);
        break;
      default:
        return false;
      }
      return true;
    }

    template<typename T>
    typename std::enable_if<std::is_signed<T>::value, void>::type
    init(const T& v);

    template<typename T>
    struct is_dense_vector
    {
      static const bool value =
        !std::is_signed<T>::value &&
          std::is_signed<typename T::value_type>::value
            && !std::is_same<std::string, T>::value
          &&
          (
            std::is_same<std::random_access_iterator_tag, typename std::iterator_traits<typename T::iterator>::iterator_category>::value
#if 0
          || std::is_same<T, boost::numeric::ublas::vector<typename T::value_type>>::value
#endif
        );
    };


    template<typename T>
    typename std::enable_if<is_dense_vector<T>::value, void>::type
    init(const T& vec);

    template<typename T>
    typename std::enable_if<std::is_same<T, ::savvy::compressed_vector<typename T::value_type>>::value && std::is_signed<typename T::value_type>::value, void>::type
    init(const T& vec);

    template<typename T>
    typename std::enable_if<std::is_same<T, std::string>::value, void>::type
    init(const T& vec);

  private:
    std::uint8_t val_type_ = 0;
    std::uint8_t off_type_ = 0;
    std::size_t size_ = 0;
    std::size_t sparse_size_ = 0;
    char *off_ptr_ = nullptr;
    char *val_ptr_ = nullptr;
    std::vector<char> local_data_;
  };

  template<>
  struct typed_value::is_dense_vector<std::int8_t>
  {
    static const bool value = false;
  };
  template<>
  struct typed_value::is_dense_vector<std::int16_t>
  {
    static const bool value = false;
  };
  template<>
  struct typed_value::is_dense_vector<std::int32_t>
  {
    static const bool value = false;
  };
  template<>
  struct typed_value::is_dense_vector<std::int64_t>
  {
    static const bool value = false;
  };
  template<>
  struct typed_value::is_dense_vector<float>
  {
    static const bool value = false;
  };
  template<>
  struct typed_value::is_dense_vector<double>
  {
    static const bool value = false;
  };

  template<typename T>
  typename std::enable_if<std::is_signed<T>::value && std::is_integral<T>::value, bool>::type
  typed_value::is_missing(const T& v)
  {
    return v == std::numeric_limits<T>::min();
  }

  template<typename T>
  typename std::enable_if<std::is_same<T, float>::value || std::is_same<T, double>::value, bool>::type
  typed_value::is_missing(const T& v)
  {
    return std::isnan(v); // TODO: differentiate between end_of_vector
  }

  template<typename T>
  inline bool typed_value::is_end_of_vector(const T& v)
  {
    return v == end_of_vector_value<T>();
  }

  template<>
  inline bool typed_value::is_end_of_vector(const char& v)
  {
    return false;
  }

  template<typename T>
  typename std::enable_if<std::is_signed<T>::value, std::uint8_t>::type typed_value::type_code()
  {
    if (std::is_same<T, std::int8_t>::value)
      return typed_value::int8;
    if (std::is_same<T, std::int16_t>::value)
      return typed_value::int16;
    if (std::is_same<T, std::int32_t>::value)
      return typed_value::int32;
    if (std::is_same<T, std::int64_t>::value)
      return typed_value::int64;
    if (std::is_same<T, float>::value)
      return typed_value::real;
    if (std::is_same<T, double>::value)
      return typed_value::real64;
    return 0;
  }

  template<typename T>
  typename std::enable_if<std::is_signed<T>::value, std::uint8_t>::type typed_value::type_code_ignore_missing(const T& val)
  {
    std::uint8_t type = type_code<T>();
    if (type >= typed_value::int16 && type <= typed_value::int64)
    {
      if (val <= std::numeric_limits<std::int8_t>::max() && val >= std::numeric_limits<std::int8_t>::min())
        type = typed_value::int8;
      else if (val <= std::numeric_limits<std::int16_t>::max() && val >= std::numeric_limits<std::int16_t>::min())
        return typed_value::int16;
      else if (val <= std::numeric_limits<std::int32_t>::max() && val >= std::numeric_limits<std::int32_t>::min())
        return typed_value::int32;
      else
        return typed_value::int64;
    }
    return type;
  }

  template<typename T>
  typename std::enable_if<std::is_unsigned<T>::value, std::uint8_t>::type typed_value::offset_type_code(const T& val)
  {
    if (val <= std::numeric_limits<std::uint8_t>::max())
      return typed_value::int8;
    else if (val <= std::numeric_limits<std::uint16_t>::max())
      return typed_value::int16;
    else if (val <= std::numeric_limits<std::uint32_t>::max())
      return typed_value::int32;
    else
      return typed_value::int64;
  }

  template<typename T>
  typename std::enable_if<std::is_signed<T>::value, std::uint8_t>::type typed_value::type_code(const T& val)
  {
    std::uint8_t type = type_code<T>();
    if (type >= typed_value::int16 && type <= typed_value::int64)
    {
      if (val <= std::numeric_limits<std::int8_t>::max() && val > std::numeric_limits<std::int8_t>::min()) // TODO: include other reserved values
        type = typed_value::int8;
      else if (val <= std::numeric_limits<std::int16_t>::max() && val > std::numeric_limits<std::int16_t>::min())
        type = typed_value::int16;
      else if (val <= std::numeric_limits<std::int32_t>::max() && val > std::numeric_limits<std::int32_t>::min())
        type = typed_value::int32;
      else
        type = typed_value::int64;
    }
    return type;
  }

  template <> inline std::int8_t typed_value::missing_value<std::int8_t>() { return 0x80; }
  template <> inline std::int16_t typed_value::missing_value<std::int16_t>() { return 0x8000; }
  template <> inline std::int32_t typed_value::missing_value<std::int32_t>() { return 0x80000000; }
  template <> inline std::int64_t typed_value::missing_value<std::int64_t>() { return 0x8000000000000000; }

  template <> inline float typed_value::missing_value<float>()
  {
    union
    {
      float f;
      std::uint32_t i;
    } ret;
    ret.i = 0x7F800001;
    return ret.f;
  }

  template <> inline double typed_value::missing_value<double>()
  {
    union
    {
      double f;
      std::uint64_t i;
    } ret;
    ret.i = 0x7FF0000000000001;
    return ret.f;
  }

  template <> inline std::int8_t typed_value::end_of_vector_value<std::int8_t>() { return 0x81; }
  template <> inline std::int16_t typed_value::end_of_vector_value<std::int16_t>() { return 0x8001; }
  template <> inline std::int32_t typed_value::end_of_vector_value<std::int32_t>() { return 0x80000001; }
  template <> inline std::int64_t typed_value::end_of_vector_value<std::int64_t>() { return 0x8000000000000001; }

  template <> inline float typed_value::end_of_vector_value<float>()
  {
    union
    {
      float f;
      std::uint32_t i;
    } ret;
    ret.i = 0x7F800002;
    return ret.f;
  }

  template <> inline double typed_value::end_of_vector_value<double>()
  {
    union
    {
      double f;
      std::uint64_t i;
    } ret;
    ret.i = 0x7FF0000000000002;
    return ret.f;
  }

  template <typename DestT, typename SrcT>
  DestT typed_value::reserved_transformation(SrcT in)
  {
    if (is_missing(in))
      return missing_value<DestT>();
    if (is_end_of_vector(in))
      return end_of_vector_value<DestT>();
    return DestT(in);
  }

  inline
  typed_value::typed_value(std::uint8_t type, std::size_t sz, char *data_ptr) :
    val_type_(type),
    size_(sz),
    val_ptr_(data_ptr)
  {
    if (!val_ptr_)
    {
      local_data_.resize(size_ * (1u << bcf_type_shift[val_type_]));
      val_ptr_ = local_data_.data();
    }
  }

  inline
  void typed_value::init(std::uint8_t type, std::size_t sz, char *data_ptr)
  {
    val_type_ = type;
    off_type_ =  0;
    size_ = sz;
    sparse_size_ = 0;
    val_ptr_ = data_ptr;
    off_ptr_ = nullptr;
    //local_data_.clear();
  }

  inline
  typed_value::typed_value(std::uint8_t val_type, std::size_t sz, std::uint8_t off_type, std::size_t sp_sz, char *data_ptr) :
    val_type_(val_type),
    off_type_(off_type),
    size_(sz),
    sparse_size_(sp_sz),
    val_ptr_(data_ptr + sp_sz * (1u << bcf_type_shift[off_type])),
    off_ptr_(data_ptr)
  {
  }

  inline
  void typed_value::init(std::uint8_t val_type, std::size_t sz, std::uint8_t off_type, std::size_t sp_sz, char *data_ptr)
  {
    val_type_ = val_type;
    off_type_ = off_type;
    size_ = sz;
    sparse_size_ = sp_sz;
    val_ptr_ = data_ptr + sp_sz * (1u << bcf_type_shift[off_type]);
    off_ptr_ = data_ptr;
    //local_data_.clear();
  }

  inline
  typed_value& typed_value::operator=(typed_value&& src)
  {
    if (&src != this)
    {
      val_type_ = src.val_type_;
      off_type_ = src.off_type_;
      size_ = src.size_;
      sparse_size_ = src.sparse_size_;
      val_ptr_ = src.val_ptr_;
      off_ptr_ = src.off_ptr_;
      local_data_ = std::move(src.local_data_);

      src.val_type_ = 0;
      src.off_type_ = 0;
      src.size_ = 0;
      src.sparse_size_ = 0;
      src.val_ptr_ = nullptr;
      src.off_ptr_ = nullptr;
    }
    return *this;
  }

  /*template<typename SrcT, typename DestT>
  static void pbwt_unsort(SrcT src_ptr, std::size_t sz, DestT dest_ptr, std::vector<std::size_t>& sort_mapping, std::vector<std::size_t>& prev_sort_mapping, std::vector<std::size_t>& counts_old, omp::internal::thread_pool2& tpool)
  {
    std::swap(sort_mapping, prev_sort_mapping);
    if (prev_sort_mapping.empty())
    {
      prev_sort_mapping.resize(sz);
      std::iota(prev_sort_mapping.begin(), prev_sort_mapping.end(), 0);
    }

    if (sort_mapping.empty())
      sort_mapping.resize(sz);

    if (prev_sort_mapping.size() != sz)
    {
      fprintf(stderr, "Variable-sized data vectors not allowed with PBWT\n"); // TODO: handle better
      exit(-1);
    }

    std::size_t thread_cnt = tpool.thread_count();
    std::size_t counts_vec_size = std::numeric_limits<typename std::make_unsigned<typename std::iterator_traits<SrcT>::value_type>::type>::max() + 2;
    std::vector<std::vector<std::size_t>> counts(thread_cnt + 1, std::vector<std::size_t>(counts_vec_size));
    omp::parallel_for_exp(omp::static_schedule{}, counts.begin(), counts.begin() + thread_cnt, [src_ptr, sz, thread_cnt](std::vector<std::size_t>& c, const omp::iteration_context& ctx)
    {
      std::size_t* counts_ptr = c.data() + 1;
      std::size_t chunk_size = omp::internal::ceil_divide(sz, thread_cnt);
      auto* src_uptr = ((typename std::make_unsigned<typename std::iterator_traits<SrcT>::value_type>::type*)src_ptr) + ctx.thread_index * chunk_size;
      auto* end_uptr = std::min(((typename std::make_unsigned<typename std::iterator_traits<SrcT>::value_type>::type*)src_ptr) + sz, src_uptr + chunk_size);
      for (auto it = src_uptr; it != end_uptr; ++it)
      {
        ++(counts_ptr[*it]);
      }
    }, tpool);

//      tpool([&counts, counts_vec_size, thread_cnt](std::size_t tidx)
//      {
//        std::size_t chunk_size = omp::internal::ceil_divide(counts_vec_size, thread_cnt);
//        std::size_t off = chunk_size * tidx;
//
//        for (int i = 1; i < counts.size(); ++i)
//          counts[i][off] = counts[i - 1][off] + counts[i][off];
//      });

    for (std::size_t i = 0; i < counts_vec_size; ++i)
    {
      for (std::size_t j = 1; j < thread_cnt; ++j)
      {
        counts[j][i]
      }
    }

    for (int j = 1; j < counts.size(); ++j)
      counts[i][off] = counts[j - 1][off] + counts[i][off];

    for (int i = 1; i < counts.size(); ++i)
      counts[i] = counts[i - 1] + counts[i];

    for (int i = 0; i < prev_sort_mapping.size(); ++i)
    {
//        std::size_t unsorted_index = prev_sort_mapping[i];
//        dest_ptr[unsorted_index] = src_ptr[i];
//        std::uint8_t d(dest_ptr[unsorted_index]);
//        sort_mapping[counts[d]++] = unsorted_index;

      const std::size_t unsorted_index = prev_sort_mapping[i];
      dest_ptr[unsorted_index] = src_ptr[i];
      sort_mapping[counts[src_uptr[i]]++] = unsorted_index;
    }
  }*/

  template<typename SrcT, typename DestT>
  static void pbwt_unsort(SrcT src_ptr, std::size_t sz, DestT dest_ptr, std::vector<std::size_t>& sort_mapping, std::vector<std::size_t>& prev_sort_mapping, std::vector<std::size_t>& counts)
  {
    std::swap(sort_mapping, prev_sort_mapping);
    if (prev_sort_mapping.empty())
    {
      prev_sort_mapping.resize(sz);
      for (int i = 0; i < sz; ++i)
        prev_sort_mapping[i] = i;
    }

    if (sort_mapping.empty())
      sort_mapping.resize(sz);

    if (prev_sort_mapping.size() != sz)
    {
      fprintf(stderr, "Variable-sized data vectors not allowed with PBWT\n"); // TODO: handle better
      exit(-1);
    }

    typedef typename std::make_unsigned<typename std::iterator_traits<SrcT>::value_type>::type utype;
    auto src_uptr = (utype*)src_ptr;
    counts.clear();
    counts.resize(std::numeric_limits<utype>::max() + 2);
    auto counts_ptr = counts.data() + 1;
    for (int i = 0; i < sz; ++i)
    {
//        unsigned int d = utype(src_ptr[i]) + 1u;
//        if (d >= counts.size())
//          counts.resize(d + 1u);
//        ++counts[d];
      ++(counts_ptr[src_uptr[i]]);
    }

    for (int i = 1; i < counts.size(); ++i)
      counts[i] = counts[i - 1] + counts[i];

    for (int i = 0; i < prev_sort_mapping.size(); ++i)
    {
//        std::size_t unsorted_index = prev_sort_mapping[i];
//        dest_ptr[unsorted_index] = src_ptr[i];
//        std::uint8_t d(dest_ptr[unsorted_index]);
//        sort_mapping[counts[d]++] = unsorted_index;

      const std::size_t unsorted_index = prev_sort_mapping[i];
      dest_ptr[unsorted_index] = src_ptr[i];
      const utype d(src_ptr[i]);
      sort_mapping[counts[d]++] = unsorted_index;
    }
  }

  inline void typed_value::internal::pbwt_unsort(typed_value& v, std::vector<std::size_t>& sort_mapping, std::vector<std::size_t>& prev_sort_mapping, std::vector<std::size_t>& counts)
  {
    assert(v.off_ptr_ == nullptr);
    //assert(v.local_data_.empty());

    if (v.off_ptr_)
    {
      fprintf(stderr, "PBWT sort not supported with sparse vectors\n"); // TODO: implement
      exit(-1);
    }
    else if (v.val_ptr_)
    {

      v.local_data_.resize(v.size_ * (1u << bcf_type_shift[v.val_type_]));
      if (v.val_type_ == 0x01u) ::savvy::pbwt_unsort((std::int8_t *) v.val_ptr_, v.size_, (std::int8_t *) v.local_data_.data(), sort_mapping, prev_sort_mapping, counts);
      else if (v.val_type_ == 0x02u) ::savvy::pbwt_unsort((std::int16_t *) v.val_ptr_, v.size_, (std::int16_t *) v.local_data_.data(), sort_mapping, prev_sort_mapping, counts); // TODO: make sure this works
      else
      {
        fprintf(stderr, "PBWT sorted vector values cannot be wider than 16 bits\n"); // TODO: handle better
        exit(-1);
      }
      v.val_ptr_ = v.local_data_.data();
    }
  }

  template <typename Iter>
  void typed_value::internal::serialize(const typed_value& v, Iter out_it, std::size_t size_divisor)
  {
    assert(!v.off_type_ || size_divisor == 1);
    std::uint8_t type_byte =  v.off_type_ ? typed_value::sparse : v.val_type_;
    std::size_t sz = v.size_ / size_divisor;
    type_byte = std::uint8_t(std::min(std::size_t(15), sz) << 4u) | type_byte;
    *(out_it++) = type_byte;

    if (sz >= 15u)
      bcf::serialize_typed_scalar(out_it, static_cast<std::int64_t>(sz));

    if (v.off_type_)
    {
      type_byte = std::uint8_t(v.off_type_ << 4u) | v.val_type_;
      *(out_it++) = type_byte;
      bcf::serialize_typed_scalar(out_it, static_cast<std::int64_t>(v.sparse_size_));
      std::size_t pair_width = (1u << bcf_type_shift[v.off_type_]) + (1u << bcf_type_shift[v.val_type_]);
      assert(v.off_ptr_ == (v.val_ptr_ - (1u << bcf_type_shift[v.off_type_]) * v.sparse_size_));
      std::copy_n(v.off_ptr_, v.sparse_size_ * pair_width, out_it);
    }
    else
    {
      std::copy_n(v.val_ptr_, v.size_ * (1u << bcf_type_shift[v.val_type_]), out_it);
    }
  }

  template<typename InIter, typename OutIter>
  inline void typed_value::internal::pbwt_sort(InIter in_data, std::size_t in_data_sz, OutIter out_it, std::vector<std::size_t>& sort_mapping, std::vector<std::size_t>& prev_sort_mapping, std::vector<std::size_t>& counts)
  {
    std::swap(sort_mapping, prev_sort_mapping);
    if (prev_sort_mapping.empty())
    {
      prev_sort_mapping.resize(in_data_sz);
      for (int i = 0; i < in_data_sz; ++i)
        prev_sort_mapping[i] = i;
    }

    if (sort_mapping.empty())
      sort_mapping.resize(in_data_sz);

    if (prev_sort_mapping.size() != in_data_sz)
    {
      fprintf(stderr, "Variable-sized data vectors not allowed with PBWT\n"); // TODO: handle better
      exit(-1);
    }

    typedef typename std::iterator_traits<InIter>::value_type val_t;
    typedef typename std::make_unsigned<val_t>::type utype;
    counts.clear();
    for (std::size_t i = 0; i < in_data_sz; ++i)
    {
      unsigned int d = utype(in_data[i]) + 1u;
      if (d >= counts.size())
        counts.resize(d + 1u);
      ++counts[d];
    }

    for (int i = 1; i < counts.size(); ++i)
      counts[i] = counts[i - 1] + counts[i];

    for (int i = 0; i < prev_sort_mapping.size(); ++i)
    {
      std::size_t unsorted_index = prev_sort_mapping[i];
      val_t d(in_data[unsorted_index]);
      sort_mapping[counts[d]++] = unsorted_index;
    }

    //sorted_data.resize(in_data_sz);
    if (std::is_same<val_t, std::int8_t>::value)
    {
      for (int i = 0; i < prev_sort_mapping.size(); ++i)
      {
        *(out_it++) = in_data[prev_sort_mapping[i]];
      }
    }
    else //std::is_same<val_t, std::int16_t>::value
    {
      for (int i = 0; i < prev_sort_mapping.size(); ++i)
      {
        char* v_ptr = (char*)(&in_data[prev_sort_mapping[i]]);
        std::array<char, 2> bytes;
        std::memcpy(bytes.data(), v_ptr, 2); // TODO: handle endianess
        *(out_it++) = bytes[0];
        *(out_it++) = bytes[1];
      }
    }
  }

  template <typename Iter>
  void typed_value::internal::serialize(const typed_value& v, Iter out_it, std::vector<std::size_t>& sort_mapping, std::vector<std::size_t>& prev_sort_mapping, std::vector<std::size_t>& counts)
  {
    std::uint8_t type_byte =  v.off_type_ ? typed_value::sparse : v.val_type_;
    type_byte = std::uint8_t(std::min(std::size_t(15), v.size_) << 4u) | type_byte;
    *(out_it++) = type_byte;
    if (v.size_ >= 15u)
      bcf::serialize_typed_scalar(out_it, static_cast<std::int64_t>(v.size_));

    if (v.off_type_)
    {
      assert(!"This should never happen"); // TODO: Then why is this here?
      type_byte = std::uint8_t(v.off_type_ << 4u) | v.val_type_;
      *(out_it++) = type_byte;
      bcf::serialize_typed_scalar(out_it, static_cast<std::int64_t>(v.sparse_size_));
      std::size_t pair_width = (1u << bcf_type_shift[v.off_type_]) + (1u << bcf_type_shift[v.val_type_]);
      assert(v.off_ptr_ == (v.val_ptr_ - (1u << bcf_type_shift[v.off_type_]) * v.sparse_size_));
      std::copy_n(v.off_ptr_, v.sparse_size_ * pair_width, out_it);
    }
    else
    {
      // ---- PBWT ---- //
      if (v.val_type_ == 0x01u) internal::pbwt_sort((std::int8_t *) v.val_ptr_, v.size_, out_it, sort_mapping, prev_sort_mapping, counts);
      else if (v.val_type_ == 0x02u) internal::pbwt_sort((std::int16_t *) v.val_ptr_, v.size_, out_it, sort_mapping, prev_sort_mapping, counts); // TODO: make sure this works
      else
      {
        fprintf(stderr, "PBWT sorted vector values cannot be wider than 16 bits\n"); // TODO: handle better
        exit(-1);
      }
      // ---- PBWT_END ---- //
    }
  }

  template<typename T>
  typename std::enable_if<std::is_signed<T>::value, void>::type
  typed_value::init(const T& v)
  {
    val_type_ = type_code<T>();
    size_ = 1;
    std::size_t width = 1u << bcf_type_shift[val_type_];
    // TODO: handle endianess
    local_data_.resize(width);
    std::memcpy(local_data_.data(), &v, width);
    val_ptr_ = local_data_.data();
  }

  template<typename T>
  typename std::enable_if<typed_value::is_dense_vector<T>::value, void>::type
  typed_value::init(const T& vec)
  {
    typedef typename T::value_type vtype;
    if (std::is_integral<vtype>::value)
    {
      vtype min_val = 0;
      vtype max_val = 0;
      for (auto it = vec.begin(); it != vec.end(); ++it)
      {
        if (!is_missing(*it)) // TODO: handle vector_end use is_reserved()
        {
          if (*it > max_val)
            max_val = *it;
          if (*it < min_val)
            min_val = *it;
        }
      }

      val_type_ = type_code(std::min(vtype(-max_val), min_val));
    }
    else
    {
      val_type_ = type_code<vtype>();
    }

    size_ = vec.size();
    std::size_t width = 1u << bcf_type_shift[val_type_];

    local_data_.resize(width * size_);
    val_ptr_ = local_data_.data();

    switch (val_type_)
    {
    case 0x01u:
      std::transform(vec.begin(), vec.begin() + size_, (std::int8_t*) val_ptr_, reserved_transformation<std::int8_t, typename T::value_type>);
      break;
    case 0x02u:
      std::transform(vec.begin(), vec.begin() + size_, (std::int16_t*) val_ptr_, reserved_transformation<std::int16_t, typename T::value_type>); // TODO: handle endianess
      break;
    case 0x03u:
      std::transform(vec.begin(), vec.begin() + size_, (std::int32_t*) val_ptr_, reserved_transformation<std::int32_t, typename T::value_type>);
      break;
    case 0x04u:
      std::transform(vec.begin(), vec.begin() + size_, (std::int64_t*) val_ptr_, reserved_transformation<std::int64_t, typename T::value_type>);
      break;
    case 0x05u:
      std::transform(vec.begin(), vec.begin() + size_, (float*) val_ptr_, reserved_transformation<float, typename T::value_type>);
      break;
    }
  }

  template <typename T>
  void copy_offsets(const std::size_t* index_data, std::size_t sp_sz, T* off_ptr)
  {
    const std::size_t* index_data_end = index_data + sp_sz;
    std::size_t last_off = 0;
    for (auto it = index_data; it != index_data_end; ++it)
    {
      std::size_t off = (*it) - last_off;
      last_off = (*it) + 1;
      (*off_ptr) = off;
      ++off_ptr;
    }
  }

  template<typename T>
  typename std::enable_if<std::is_same<T, ::savvy::compressed_vector<typename T::value_type>>::value && std::is_signed<typename T::value_type>::value, void>::type
  typed_value::init(const T& vec)
  {
    std::size_t offset_max = 0;
    std::size_t last_off = 0;
    for (auto it = vec.begin(); it != vec.end(); ++it)
    {
      std::size_t off = it.offset() - last_off;
      last_off = it.offset() + 1;
      if (off > offset_max)
        offset_max = off;
    }

    off_type_ = type_code_ignore_missing(static_cast<std::int64_t>(offset_max));
    //auto max_abs_offset = vec.non_zero_size() ? *(vec.index_data() + vec.non_zero_size() - 1) : 0;
    //off_type_ = type_code_ignore_missing(static_cast<std::int64_t>(max_abs_offset)); //TODO: Revert back to line above

    typedef typename T::value_type vtype;
    if (std::is_integral<vtype>::value && !std::is_same<std::int8_t, vtype>::value)
    {
      vtype min_val = 0;
      vtype max_val = 0;
      for (auto it = vec.begin(); it != vec.end(); ++it)
      {
        if (!is_missing(*it)) // TODO: handle vector_end use is_reserved()
        {
          if (*it > max_val)
            max_val = *it;
          if (*it < min_val)
            min_val = *it;
        }
      }

      val_type_ = type_code(std::min(vtype(-max_val), min_val));
    }
    else
    {
      val_type_ = type_code<vtype>();
    }

    sparse_size_ = vec.non_zero_size();
    size_ = vec.size();
    std::size_t off_width = 1u << bcf_type_shift[off_type_];
    std::size_t val_width = 1u << bcf_type_shift[val_type_];
    std::size_t pair_width = off_width + val_width;

    local_data_.resize(pair_width * sparse_size_);
    off_ptr_ = local_data_.data();
    val_ptr_ = local_data_.data() + (sparse_size_ * off_width);




    switch (off_type_)
    {
    case 0x01u:
      copy_offsets(vec.index_data(), sparse_size_, (std::uint8_t*)off_ptr_);
      break;
    case 0x02u:
      copy_offsets(vec.index_data(), sparse_size_, (std::uint16_t*)off_ptr_); // TODO: handle endianess
      break;
    case 0x03u:
      copy_offsets(vec.index_data(), sparse_size_, (std::uint32_t*)off_ptr_);
      break;
    case 0x04u:
      copy_offsets(vec.index_data(), sparse_size_, (std::uint64_t*)off_ptr_);
      break;
    }

    switch (val_type_)
    {
    case 0x01u:
      std::transform(vec.value_data(), vec.value_data() + sparse_size_, (std::int8_t*) val_ptr_, reserved_transformation<std::int8_t, typename T::value_type>);
      break;
    case 0x02u:
      std::transform(vec.value_data(), vec.value_data() + sparse_size_, (std::int16_t*) val_ptr_, reserved_transformation<std::int16_t, typename T::value_type>); // TODO: handle endianess
      break;
    case 0x03u:
      std::transform(vec.value_data(), vec.value_data() + sparse_size_, (std::int32_t*) val_ptr_, reserved_transformation<std::int32_t, typename T::value_type>);
      break;
    case 0x04u:
      std::transform(vec.value_data(), vec.value_data() + sparse_size_, (std::int64_t*) val_ptr_, reserved_transformation<std::int64_t, typename T::value_type>);
      break;
    case 0x05u:
      std::transform(vec.value_data(), vec.value_data() + sparse_size_, (float*) val_ptr_, reserved_transformation<float, typename T::value_type>);
      break;
    }
  }

  template<typename T>
  typename std::enable_if<std::is_same<T, std::string>::value, void>::type
  typed_value::init(const T& vec)
  {
    val_type_ = typed_value::str;

    size_ = vec.size();

    local_data_.resize(size_);
    val_ptr_ = local_data_.data();
    std::copy_n(vec.begin(), size_, local_data_.begin());
  }




  inline
  std::ostream& operator<<(std::ostream& os, const typed_value& v)
  {
    if (!v.val_ptr_ || v.size_ == 0)
    {
      os << ".";
    }
    else
    {
      for (std::size_t i = 0; i < v.size_; ++i)
      {
        if (i > 0)
          os.put(',');
        switch (v.val_type_)
        {
        case 0x01u:
          os << static_cast<int>(*(((std::int8_t*)v.val_ptr_) + i));
          break;
        case 0x02u:
          os << *(((std::int16_t*)v.val_ptr_) + i); // TODO: handle endianess
          break;
        case 0x03u:
          os << *(((std::int32_t*)v.val_ptr_) + i);
          break;
        case 0x04u:
          os << *(((std::int64_t*)v.val_ptr_) + i);
          break;
        case 0x05u:
          os << *(((float*)v.val_ptr_) + i);
          break;
        case 0x07u:
          os.write(v.val_ptr_, v.size_);
          i = v.size_;
          break;
        default:
          os.setstate(os.rdstate() | std::ios::failbit);
        }
      }
    }

    return os;
  }

  inline
  typed_value::typed_value(std::int8_t type, char* str, char*const str_end)
  {
    val_type_ = type;
    size_ = 0;
    switch (val_type_)
    {
    case 0x01u:
    {
      for ( ; str < str_end; ++str)
      {
        typedef std::int8_t T;
        local_data_.resize(local_data_.size() + sizeof(T));
        if (*str == '.') ((T*)val_ptr_)[size_++] = T(0x80), ++str;
        else ((T*)local_data_.data())[size_++] = std::strtol(str, &str, 10);
      }
      break;
    }
    case 0x02u:
    {
      for ( ; str < str_end; ++str)
      {
        typedef std::int16_t T;
        local_data_.resize(local_data_.size() + sizeof(T));
        if (*str == '.') ((T*)val_ptr_)[size_++] = T(0x8000), ++str;
        else ((T*)local_data_.data())[size_++] = std::strtol(str, &str, 10);
      }
      break;
    }
    case 0x03u:
    {
      for ( ; str < str_end; ++str)
      {
        typedef std::int32_t T;
        local_data_.resize(local_data_.size() + sizeof(T));
        if (*str == '.') ((T*)val_ptr_)[size_++] = T(0x80000000), ++str;
        else ((T*)local_data_.data())[size_++] = std::strtol(str, &str, 10);
      }
      break;
    }
    case 0x04u:
    {
      for ( ; str < str_end; ++str)
      {
        typedef std::int64_t T;
        local_data_.resize(local_data_.size() + sizeof(T));
        if (*str == '.') ((T*)val_ptr_)[size_++] = T(0x8000000000000000), ++str;
        else ((T*)local_data_.data())[size_++] = std::strtol(str, &str, 10);
      }
      break;
    }
    case 0x05u:
    {
      for ( ; str < str_end; ++str)
      {
        typedef float T;
        local_data_.resize(local_data_.size() + sizeof(T));
        if (*str == '.') ((T*)val_ptr_)[size_++] = missing_value<float>(), ++str;
        else ((T*)local_data_.data())[size_++] = std::strtof(str, &str);
      }
      break;
    }
    case 0x07u:
    {
      local_data_.assign(str, str_end);
      size_ = local_data_.size();
      break;
    }
    default:
      return; // TODO: Maybe return false

    }

    val_ptr_ = local_data_.data();
  }

  inline
  void typed_value::deserialize_vcf(std::size_t idx, std::size_t length, char* str)
  {
    assert(!off_ptr_ && idx < size_);

    std::size_t end = idx + length;

    switch (val_type_)
    {
    case 0x01u:
    {
      for ( ; idx < end && *str != '\0'; ++idx,++str)
      {
        if (*str == '.') ((std::int8_t*)val_ptr_)[idx] = std::int8_t(0x80), ++str;
        else ((std::int8_t*)val_ptr_)[idx] = std::strtol(str, &str, 10);
      }

      for ( ; idx < end; ++idx)
        ((std::int8_t*)val_ptr_)[idx] = std::int8_t(0x81);
      break;
    }
    case 0x02u:
    {
      for ( ; idx < end && *str != '\0'; ++idx,++str)
      {
        if (*str == '.') ((std::int16_t*)val_ptr_)[idx] = std::int16_t(0x8000), ++str;
        else ((std::int16_t*)val_ptr_)[idx] = std::strtol(str, &str, 10);
      }

      for ( ; idx < end; ++idx)
        ((std::int16_t*)val_ptr_)[idx] = std::int16_t(0x8001);
      break;
    }
    case 0x03u:
    {
      for ( ; idx < end && *str != '\0'; ++idx,++str)
      {
        if (*str == '.') ((std::int32_t*)val_ptr_)[idx] = std::int32_t(0x80000000), ++str;
        else ((std::int32_t*)val_ptr_)[idx] = std::strtol(str, &str, 10);
      }

      for ( ; idx < end; ++idx)
        ((std::int32_t*)val_ptr_)[idx] = std::int32_t(0x80000001);
      break;
    }
    case 0x04u:
    {
      for ( ; idx < end && *str != '\0'; ++idx,++str)
      {
        if (*str == '.') ((std::int64_t*)val_ptr_)[idx] = std::int64_t(0x8000000000000000), ++str;
        else ((std::int64_t*)val_ptr_)[idx] = std::strtoll(str, &str, 10);
      }

      for ( ; idx < end; ++idx)
        ((std::int64_t*)val_ptr_)[idx] = std::int64_t(0x8000000000000001);
      break;
    }
    case 0x05u:
    {
      for ( ; idx < end && *str != '\0'; ++idx,++str)
      {
        if (*str == '.') ((std::int64_t*)val_ptr_)[idx] = missing_value<float>(), ++str;
        else ((float*)val_ptr_)[idx] = std::strtof(str, &str);
      }

      for ( ; idx < end; ++idx)
        ((float*)val_ptr_)[idx] = end_of_vector_value<float>();
      break;
    }
    default:
      return; // TODO: Maybe return false

    }
  }
}

#endif // LIBSAVVY_TYPED_VALUE_HPP