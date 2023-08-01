#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <utility>

template <typename T, std::size_t SMALL_SIZE>
class socow_vector {
public:
  using value_type = T;

  using reference = T&;
  using const_reference = const T&;

  using pointer = T*;
  using const_pointer = const T*;

  using iterator = pointer;
  using const_iterator = const_pointer;

private:
  class buffer {
  public:
    buffer(std::size_t capacity, std::size_t ref_count) : capacity_(capacity), ref_count_(ref_count) {}

    std::size_t capacity_;
    std::size_t ref_count_;
    value_type data_[0];
  };

  buffer* make_new_buffer(std::size_t capacity) {
    buffer* new_buf = static_cast<buffer*>(operator new(sizeof(buffer) + sizeof(T) * capacity));
    std::construct_at(new_buf, capacity, 1);
    return new_buf;
  }

  void destroy_buffer(buffer* buf) {
    std::destroy_at(buf);
    operator delete(buf);
  }

  void add_ref() noexcept {
    buf_->ref_count_++;
  }

  void release_ref() noexcept {
    buf_->ref_count_--;
    allocated_ = false;
    if (buf_->ref_count_ == 0) {
      clear();
      destroy_buffer(buf_);
    }
  }

  void unshare() {
    if (!sharing()) {
      return;
    }
    socow_vector tmp(*this, capacity(), size());
    swap(tmp);
  }

  bool sharing() const noexcept {
    return allocated_ && buf_->ref_count_ > 1;
  }

  void swap_small_buffers(socow_vector& other, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
      std::swap(small_buf_[i], other.small_buf_[i]);
    }
  }

  void suffix_try_copy_small(const socow_vector& other, socow_vector& backup, std::size_t offset = 0) {
    try {
      std::uninitialized_copy_n(other.data() + offset, other.size() - offset, small_buf_.data() + offset);
    } catch (...) {
      swap(backup);
      throw;
    }
  }

  void push_back_range(const socow_vector& other, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i) {
      push_back(other[i]);
    }
  }

private:
  std::size_t size_;
  bool allocated_;

  union {
    buffer* buf_;
    std::array<T, SMALL_SIZE> small_buf_;
  };

private:
  socow_vector(const socow_vector& other, std::size_t new_capacity, std::size_t copy_size, bool share_buf = false)
      : size_(copy_size),
        allocated_(new_capacity > SMALL_SIZE) {
    if (!allocated_) {
      std::uninitialized_copy_n(other.data(), copy_size, small_buf_.data());
    } else if (share_buf) {
      buf_ = other.buf_;
      add_ref();
    } else {
      buffer* new_buf = make_new_buffer(new_capacity);
      try {
        std::uninitialized_copy_n(other.data(), copy_size, new_buf->data_);
      } catch (...) {
        destroy_buffer(new_buf);
        throw;
      }
      buf_ = new_buf;
    }
  }

public:
  socow_vector() noexcept : size_(0), allocated_(false) {}

  socow_vector(const socow_vector& other) : socow_vector(other, other.capacity(), other.size(), true) {}

  socow_vector& operator=(const socow_vector& other) {
    if (&other == this) {
      return *this;
    }

    if (other.allocated_) {
      hard_clear();
      buf_ = other.buf_;
      add_ref();
    } else if (allocated_) {
      socow_vector tmp(*this);
      hard_clear();
      suffix_try_copy_small(other, tmp);
    } else if (size() < other.size()) {
      socow_vector tmp(other, other.capacity(), size());
      std::uninitialized_copy_n(other.data() + size(), other.size() - size(), small_buf_.data() + size());
      size_ = other.size();
      swap_small_buffers(tmp, tmp.size());
    } else {
      socow_vector tmp(other);
      swap_small_buffers(tmp, other.size());
      std::destroy_n(small_buf_.data() + other.size(), size() - other.size());
    }

    allocated_ = other.allocated_;
    size_ = other.size();

    return *this;
  }

  void swap(socow_vector& other) {
    if (&other == this) {
      return;
    }

    if (size() > other.size()) {
      other.swap(*this);
      return;
    }

    if (!allocated_ && !other.allocated_) {
      T* first = other.small_buf_.data() + size();
      std::size_t suffix_size = other.size() - size();
      T* dest = small_buf_.data() + size();

      std::uninitialized_copy_n(first, suffix_size, dest);
      try {
        swap_small_buffers(other, size());
      } catch (...) {
        std::destroy_n(dest, suffix_size);
        throw;
      }
      std::destroy_n(first, suffix_size);

      std::swap(size_, other.size_);
      return;
    }

    socow_vector tmp(other);
    other = *this;
    *this = tmp;
  }

  ~socow_vector() noexcept {
    hard_clear();
  }

  reference operator[](std::size_t index) {
    return data()[index];
  }

  const_reference operator[](std::size_t index) const {
    return data()[index];
  }

  pointer data() {
    if (!allocated_) {
      return small_buf_.data();
    }
    unshare();
    return buf_->data_;
  }

  const_pointer data() const noexcept {
    if (!allocated_) {
      return small_buf_.data();
    }
    return buf_->data_;
  }

  std::size_t size() const noexcept {
    return size_;
  }

  reference front() {
    return data()[0];
  }

  const_reference front() const {
    return data()[0];
  }

  reference back() {
    return data()[size() - 1];
  }

  const_reference back() const {
    return data()[size() - 1];
  }

  void push_back(const_reference value) {
    if (size() == capacity() || sharing()) {
      socow_vector tmp(*this, size() == capacity() ? capacity() * 2 : capacity(), size());
      tmp.push_back(value);
      *this = tmp;
    } else {
      std::construct_at(data() + size(), value);
      ++size_;
    }
  }

  void pop_back() {
    if (sharing()) {
      socow_vector tmp(*this, capacity(), size() - 1);
      swap(tmp);
    } else {
      std::destroy_at(data() + size() - 1);
      --size_;
    }
  }

  bool empty() const noexcept {
    return size() == 0;
  }

  size_t capacity() const noexcept {
    if (!allocated_) {
      return SMALL_SIZE;
    }
    return buf_->capacity_;
  }

  void reserve(std::size_t new_capacity) {
    if (new_capacity > capacity() || (new_capacity > size() && sharing())) {
      if (new_capacity <= SMALL_SIZE) {
        socow_vector tmp(*this);
        hard_clear();
        suffix_try_copy_small(tmp, tmp);
        size_ = tmp.size();
      } else {
        socow_vector tmp(*this, new_capacity, size());
        *this = tmp;
      }
    }
  }

  void shrink_to_fit() {
    if (allocated_ && capacity() > size()) {
      if (size() <= SMALL_SIZE) {
        socow_vector tmp(*this);
        hard_clear();
        suffix_try_copy_small(tmp, tmp);
        size_ = tmp.size();
      } else {
        socow_vector tmp(*this, size(), size());
        swap(tmp);
      }
    }
  }

  void hard_clear() noexcept {
    clear(false);
    if (allocated_) {
      release_ref();
    }
  }

  void clear() noexcept {
    clear(true);
  }

  void clear(bool save_capacity) {
    if (!sharing()) {
      while (size() != 0) {
        pop_back();
      }
    } else {
      release_ref();
      size_ = 0;
      if (save_capacity) {
        allocated_ = true;
        buf_ = make_new_buffer(capacity());
      }
    }
  }

  iterator begin() {
    return data();
  }

  iterator end() {
    return data() + size();
  }

  const_iterator begin() const noexcept {
    return data();
  }

  const_iterator end() const noexcept {
    return data() + size();
  }

  iterator insert(const_iterator pos, const_reference value) {
    std::size_t npos = pos - std::as_const(*this).begin();
    if (sharing() || size() == capacity()) {
      socow_vector tmp(*this, size() == capacity() ? capacity() * 2 : capacity(), npos);
      tmp.push_back(value);
      tmp.push_back_range(*this, npos, size());
      *this = tmp;
    } else {
      push_back(value);
      for (std::size_t i = size() - 1; i > npos; --i) {
        std::swap(data()[i], data()[i - 1]);
      }
    }

    return data() + npos;
  }

  iterator erase(const_iterator pos) {
    return erase(pos, pos + 1);
  }

  iterator erase(const_iterator first, const_iterator last) {
    std::size_t first_i = first - std::as_const(*this).begin();
    std::size_t last_i = last - std::as_const(*this).begin();
    std::size_t swap_distance = last - first;

    if (swap_distance != 0) {
      if (sharing()) {
        socow_vector tmp(*this, capacity(), first_i);
        tmp.push_back_range(*this, last_i, size());
        *this = tmp;
      } else {
        for (std::size_t i = last_i; i < size(); ++i) {
          std::swap(data()[i], data()[i - swap_distance]);
        }
        for (std::size_t i = 0; i < swap_distance; ++i) {
          pop_back();
        }
      }
    }

    return data() + first_i;
  }
};
