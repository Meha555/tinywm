#ifndef UTILS_HPP
#define UTILS_HPP

#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

// Declarations
namespace utils {

template <typename T = int16_t> struct Size {
    T width, height;
    Size() = default;
    Size(T w, T h) : width(w), height(h) {}
    std::string toString() const;
};

template <typename T = int16_t> struct Position {
    T x, y;
    Position() = default;
    Position(T _x, T _y) : x(_x), y(_y) {}
    std::string toString() const;
};

template <typename T = int16_t> struct Vector2D {
    T x, y;
    Vector2D() = default;
    Vector2D(T _x, T _y) : x(_x), y(_y) {}
    std::string toString() const;
    static T haha;
};

} // namespace utils

// Definitions
namespace utils {

template <typename T>std::string Size<T>::toString() const {
    std::ostringstream out;
    out << width << 'x' << height;
    return out.str();
}
template <typename T>std::string Position<T>::toString() const {
    std::ostringstream out;
    out << "(" << x << ", " << y << ")";
    return out.str();
}
template <typename T>std::string Vector2D<T>::toString() const {
    std::ostringstream out;
    out << "(" << x << ", " << y << ")";
    return out.str();
}
template <typename T>std::string toString(const T &x) {
    std::ostringstream out;
    out << x;
    return out.str();
}

template <typename T>
std::ostream &operator<<(std::ostream &out, const Size<T> &size) {
    return out << size.toString();
}

template <typename T>
Vector2D<T> operator-(const Position<T> &a, const Position<T> &b) {
	return Vector2D<T>(a.x - b.x, a.y - b.y);
}

template <typename T>
Position<T> operator+(const Position<T> &a, const Vector2D<T> &v) {
    return Position<T>(a.x + v.x, a.y + v.y);
}

template <typename T>
Position<T> operator+(const Vector2D<T> &v, const Position<T> &a) {
    return Position<T>(a.x + v.x, a.y + v.y);
}

template <typename T>
Position<T> operator-(const Position<T> &a, const Vector2D<T> &v) {
    return Position<T>(a.x - v.x, a.y - v.y);
}

template <typename T>
Vector2D<T> operator-(const Size<T> &a, const Size<T> &b) {
    return Vector2D<T>(a.width - b.width, a.height - b.height);
}

template <typename T>
Size<T> operator+(const Size<T> &a, const Vector2D<T> &v) {
    return Size<T>(a.width + v.x, a.height + v.y);
}

template <typename T>
Size<T> operator+(const Vector2D<T> &v, const Size<T> &a) {
    return Size<T>(a.width + v.x, a.height + v.y);
}

template <typename T>
Size<T> operator-(const Size<T> &a, const Vector2D<T> &v) {
    return Size<T>(a.width - v.x, a.height - v.y);
}

template <typename Container>
std::string Join(const Container &container, const std::string &delimiter) {
    std::ostringstream out;
    for (auto i = container.cbegin(); i != container.cend(); ++i) {
        if (i != container.cbegin()) {
            out << delimiter;
        }
        out << *i;
    }
    return out.str();
}

template <typename Container, typename Converter>
std::string Join(const Container &container, const std::string &delimiter,
                   Converter converter) {
    std::vector<std::string> converted_container(container.size());
    std::transform(container.cbegin(), container.cend(),
                     converted_container.begin(), converter);
    return Join(converted_container, delimiter);
}

} // namespace utils

#endif // UTILS_HPP