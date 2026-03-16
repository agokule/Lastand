#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ostream>
#include <vector>

struct Color {
    uint8_t r, g, b, a;
};

template <typename InputIt>
struct IteratorRange {
    InputIt start;
    InputIt end;

    using InputType = typename std::iterator_traits<InputIt>::value_type;
    using ReferenceType = typename std::iterator_traits<InputIt>::reference;

    size_t size() const {
        return std::distance(start, end);
    }

    ReferenceType operator[](size_t index) {
        return *std::next(start, index);
    }

    ReferenceType at(size_t index) {
        if (index >= size())
            throw std::out_of_range("index passed is out of range");
        return (*this)[index];
    }
};

template <typename InputIt> IteratorRange(InputIt, InputIt) -> IteratorRange<InputIt>;

template <typename InputIt>
std::ostream &operator<<(std::ostream &os, IteratorRange<InputIt> range) {
    std::ios_base::fmtflags f( os.flags() );  // save flags state

    os << "range{ " << std::hex;
    for (auto it = range.start; it != range.end; it++)
        os << (int)(*it) << ' ';
    os << '}';
    os.flags(f);
    return os;
}

template <typename T>
std::ostream &operator<<(std::ostream &os, const std::vector<T> &v) {
    std::ios_base::fmtflags f( os.flags() );  // save flags state
    os << "vec{ " << std::hex;
    for (auto i: v)
        os << (int)i << ' ';
    os << '}';
    os.flags(f);

    return os;
}

template <typename T, typename std::size_t n>
std::ostream &operator<<(std::ostream &os, const std::array<T, n> &v) {
    std::ios_base::fmtflags f( os.flags() );  // save flags state
    os << "arr{ " << std::hex;
    for (auto i: v)
        os << (int)i << ' ';
    os << '}';
    os.flags(f);

    return os;
}

bool is_within(int a, int b, double c);

Color random_color();

