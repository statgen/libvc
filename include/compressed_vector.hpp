
#ifndef LIBSAVVY_SPARSE_VECTOR_HPP
#define LIBSAVVY_SPARSE_VECTOR_HPP

#include <vector>
#include <algorithm>

namespace savvy
{
  template<typename T>
  class compressed_vector
  {
  public:
    typedef T value_type;
    typedef compressed_vector<T> self_type;
    static constexpr T const_value_type = value_type();

    compressed_vector() :
      size_(0)
    {
    }

    value_type& operator[](std::size_t pos)
    {
      if (offsets_.size() && offsets_.back() < pos)
      {
        offsets_.emplace_back(pos);
        values_.emplace_back();
        return values_.back();
      }
      else
      {
        auto it = std::upper_bound(offsets_.begin(), offsets_.end(), pos);
        if (it == offsets_.end() || *it != pos)
        {
          it = offsets_.insert(it, pos);
          return *(values_.insert(values_.begin() + std::distance(offsets_.begin(), it), value_type()));
        }
        return values_[it - offsets_.begin()];
      }
    }

    const value_type& operator[](std::size_t pos) const
    {
      auto it = std::upper_bound(offsets_.begin(), offsets_.end(), pos);
      if (it == offsets_.end() || *it != pos)
        return value_type();
      return values_[it - offsets_.begin()];
    }

    void resize(std::size_t sz, value_type val = value_type())
    {
      if (sz < size_)
      {
        auto it = std::lower_bound(offsets_.begin(), offsets_.end(), sz);
        offsets_.erase(it, offsets_.end());
        values_.resize(offsets_.size());
      }
      else if (val != value_type())
      {
        values_.resize(sz, val);
        offsets_.resize(sz);
        for (std::size_t i = 0; i < sz; ++i)
          offsets_[i] = i;
      }

      size_ = sz;
    }

    const std::size_t* const index_data() const { return offsets_.data(); }
    const value_type* const value_data() const { return values_.data(); }
    std::size_t size() const { return size_; }
    std::size_t non_zero_size() const { return values_.size(); }
  private:
    std::vector<value_type> values_;
    std::vector<std::size_t> offsets_;
    std::size_t size_;
  };
}

#endif //LIBSAVVY_SPARSE_VECTOR_HPP
