// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#ifndef VARDIFF_VARDIFF_HPP
#define VARDIFF_VARDIFF_HPP

#include <algorithm>
#include <any>
#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace vardiff {

// Specialize with static auto fields() returning a tuple of field descriptors.
template <typename T>
struct schema {};

namespace detail {

template <typename T>
concept registered = requires { schema<std::remove_cv_t<T>>::fields(); };

template <typename T>
struct container_traits {
    static constexpr bool vector = false, array = false, optional = false, map = false;
};

template <typename T, typename Allocator>
struct container_traits<std::vector<T, Allocator>> {
    using element_type = T;
    static constexpr bool vector = true, array = false, optional = false, map = false;
};

template <typename T, std::size_t Size>
struct container_traits<std::array<T, Size>> {
    using element_type = T;
    static constexpr bool vector = false, array = true, optional = false, map = false;
};

template <typename T>
struct container_traits<std::optional<T>> {
    using element_type = T;
    static constexpr bool vector = false, array = false, optional = true, map = false;
};

template <typename Key, typename T, typename Compare, typename Allocator>
struct container_traits<std::map<Key, T, Compare, Allocator>> {
    using element_type = T;
    static constexpr bool vector = false, array = false, optional = false, map = true;
};

template <typename Key, typename T, typename Hash, typename Equal, typename Allocator>
struct container_traits<std::unordered_map<Key, T, Hash, Equal, Allocator>> {
    using element_type = T;
    static constexpr bool vector = false, array = false, optional = false, map = true;
};

template <typename T>
consteval bool is_supported() {
    using Item = std::remove_cv_t<T>;
    using Traits = container_traits<Item>;
    if constexpr (std::is_volatile_v<T> || !std::is_copy_constructible_v<Item>) {
        return false;
    } else if constexpr (registered<Item>) {
        return true;
    } else if constexpr (Traits::vector || Traits::array || Traits::optional || Traits::map) {
        return is_supported<typename Traits::element_type>();
    } else {
        return requires(const Item& a, const Item& b) {
            { a == b } -> std::convertible_to<bool>;
        };
    }
}

template <typename T>
bool equal_values(const T& a, const T& b,
                  std::size_t remaining = std::numeric_limits<std::size_t>::max());

template <typename T>
void print_value(std::ostream& stream, const T& item);

template <registered T>
const auto& registered_fields() {
    static const auto fields = schema<T>::fields();
    return fields;
}

struct default_comparator {
    template <typename T>
    bool operator()(const T& a, const T& b) const { return equal_values(a, b); }
};

} // namespace detail

template <typename T>
concept supported_type = detail::is_supported<T>();

// Owns a copy. An empty value represents an absent container item.
class value {
public:
    value() = default;

    template <typename T>
        requires (!std::same_as<std::remove_cv_t<T>, value>) &&
                 std::is_copy_constructible_v<T>
    explicit value(const T& item)
        : data_(item),
          print_([](std::ostream& stream, const std::any& data) {
              detail::print_value(stream, std::any_cast<const std::remove_cvref_t<T>&>(data));
          }),
          equal_([]([[maybe_unused]] const std::any& a,
                    [[maybe_unused]] const std::any& b) -> bool {
              using Item = std::remove_cvref_t<T>;
              if constexpr (supported_type<Item>) {
                  return detail::equal_values(std::any_cast<const Item&>(a),
                                              std::any_cast<const Item&>(b));
              } else {
                  throw std::logic_error("vardiff::value: type has no equality comparison");
              }
          }) {}

    [[nodiscard]] bool has_value() const noexcept { return data_.has_value(); }
    [[nodiscard]] const std::type_info& type() const noexcept { return data_.type(); }

    template <typename T>
    [[nodiscard]] const T& get() const {
        const auto* item = std::any_cast<T>(&data_);
        if (!item) {
            throw std::bad_variant_access();
        }
        return *item;
    }

    friend bool operator==(const value& a, const value& b) {
        if (a.type() != b.type()) {
            return false;
        }
        return !a.has_value() || a.equal_(a.data_, b.data_);
    }

    friend std::ostream& operator<<(std::ostream& stream, const value& item) {
        if (item.has_value()) {
            item.print_(stream, item.data_);
        } else {
            stream << "<missing>";
        }
        return stream;
    }

private:
    std::any data_;
    void (*print_)(std::ostream&, const std::any&) = nullptr;
    bool (*equal_)(const std::any&, const std::any&) = nullptr;
};

enum class change_kind { modified, added, removed, moved };

struct change {
    std::string name;
    value before;
    value after;
    change_kind kind = change_kind::modified;
};

// The original change aggregate stays unchanged for existing callers.
struct deep_change : change {
    std::string from;
    std::optional<std::size_t> old_index;
    std::optional<std::size_t> new_index;
};

struct diff_statistics {
    std::size_t added = 0;
    std::size_t removed = 0;
    std::size_t modified = 0;
    std::size_t moved = 0;
    std::size_t total = 0;
};

// Counts emitted records without copying values or comparing objects again.
template <typename Change>
    requires std::same_as<Change, change> || std::same_as<Change, deep_change>
[[nodiscard]] diff_statistics summarize(const std::vector<Change>& changes) noexcept {
    diff_statistics result;
    result.total = changes.size();
    for (const auto& item : changes) {
        switch (item.kind) {
        case change_kind::added: ++result.added; break;
        case change_kind::removed: ++result.removed; break;
        case change_kind::modified: ++result.modified; break;
        case change_kind::moved: ++result.moved; break;
        }
    }
    return result;
}

enum class duplicate_key_policy { match_in_order, reject };

struct deep_options {
    bool detect_moves = true;
    std::size_t max_depth = 256; // Root is depth zero; custom comparators are atomic.
    duplicate_key_policy duplicates = duplicate_key_policy::match_in_order;
    std::vector<std::string> ignored_paths;
};

template <typename Owner, typename Member>
struct key_selector {
    Member Owner::* member;
    std::string path; // Empty applies to every sequence of this element type.
};

template <typename Owner, typename Member>
    requires std::equality_comparable<Member>
[[nodiscard]] auto key(Member Owner::* member) {
    if (member == nullptr) {
        throw std::invalid_argument("vardiff::key: null member pointer");
    }
    return key_selector<Owner, Member>{member, {}};
}

template <typename Owner, typename Member>
    requires std::equality_comparable<Member>
[[nodiscard]] auto key(std::string_view path, Member Owner::* member) {
    auto result = key(member);
    if (path.empty()) {
        throw std::invalid_argument("vardiff::key: empty path");
    }
    result.path = path;
    return result;
}

template <typename Owner, typename Member, typename Compare = detail::default_comparator>
    requires std::is_copy_constructible_v<std::remove_cv_t<Member>> && (!std::is_volatile_v<Member>)
struct field_descriptor {
    using value_type = std::remove_cv_t<Member>;
    using comparator_type = Compare;
    std::string name;
    Member Owner::* member;
    [[no_unique_address]] Compare compare;
};

// Defer comparability until tracking, allowing schema<Node> to describe vector<Node>.
template <typename Owner, typename Member>
    requires std::is_copy_constructible_v<std::remove_cv_t<Member>> &&
             (!std::is_volatile_v<Member>)
[[nodiscard]] auto field(std::string_view name, Member Owner::* member) {
    if (member == nullptr) {
        throw std::invalid_argument("vardiff::field: null member pointer");
    }
    return field_descriptor<Owner, Member>{std::string(name), member, {}};
}

template <typename Owner, typename Member, typename Compare>
    requires std::is_copy_constructible_v<std::remove_cv_t<Member>> &&
             (!std::is_volatile_v<Member>) && std::copy_constructible<Compare> &&
             std::predicate<const Compare&, const Member&, const Member&>
[[nodiscard]] auto field(std::string_view name, Member Owner::* member, Compare compare) {
    if (member == nullptr) {
        throw std::invalid_argument("vardiff::field: null member pointer");
    }
    return field_descriptor<Owner, Member, Compare>{std::string(name), member, std::move(compare)};
}

namespace detail {

template <typename>
struct is_field_descriptor : std::false_type {};

template <typename Owner, typename Member, typename Compare>
struct is_field_descriptor<field_descriptor<Owner, Member, Compare>> : std::true_type {};

template <typename Field, typename Object>
concept readable_field = is_field_descriptor<Field>::value &&
    (!std::same_as<typename Field::comparator_type, default_comparator> ||
     supported_type<typename Field::value_type>) &&
    (!std::is_volatile_v<Object>) &&
    requires(const Object& object, const Field& descriptor) { object.*descriptor.member; };

template <typename Field, typename Object>
concept writable_field = readable_field<Field, Object> &&
    requires(Object& object, const Field& descriptor, const typename Field::value_type& saved) {
        object.*descriptor.member = saved;
    };

// Check before any other members are copied/moved or assigned during dispatch.
struct poll_state {
    bool active = false;
    poll_state() = default;
    poll_state(const poll_state& other) {
        if (other.active) {
            throw std::logic_error("vardiff: copying or moving an active watcher");
        }
    }
    poll_state& operator=(const poll_state& other) {
        if (active || other.active) {
            throw std::logic_error("vardiff: assigning an active watcher");
        }
        return *this;
    }
};

inline bool path_matches(std::string_view path, std::string_view prefix) {
    return path == prefix ||
           (path.starts_with(prefix) && path.size() > prefix.size() &&
            (path[prefix.size()] == '.' || path[prefix.size()] == '['));
}

inline bool ignored(std::string_view path, const std::vector<std::string>& paths) {
    return std::any_of(paths.begin(), paths.end(),
                       [path](const auto& prefix) { return path_matches(path, prefix); });
}

inline std::string member_path(const std::string& parent, const std::string& name) {
    return parent.empty() ? name : parent + '.' + name;
}

inline std::string index_path(const std::string& parent, std::size_t index) {
    return parent + '[' + std::to_string(index) + ']';
}

template <typename Container>
decltype(auto) element(const Container& items, std::size_t index) {
    if constexpr (std::same_as<typename Container::value_type, bool>) {
        return static_cast<bool>(items[index]); // Includes vector<bool>'s proxy.
    } else {
        return (items[index]);
    }
}

template <typename T>
bool equal_child(const T& a, const T& b, std::size_t remaining) {
    if (remaining == 0) {
        throw std::length_error("vardiff::deep_diff: maximum depth exceeded");
    }
    return equal_values(a, b, remaining - 1);
}

template <typename T>
bool equal_values(const T& a, const T& b, std::size_t remaining) {
    using Traits = container_traits<T>;
    if constexpr (registered<T>) {
        return std::apply([&](const auto&... descriptor) {
            const auto equal = [&](const auto& entry) {
                if (remaining == 0) {
                    throw std::length_error("vardiff::deep_diff: maximum depth exceeded");
                }
                if constexpr (std::same_as<typename std::remove_cvref_t<decltype(entry)>::comparator_type,
                                           default_comparator>) {
                    return equal_values(a.*entry.member, b.*entry.member, remaining - 1);
                } else {
                    return static_cast<bool>(std::invoke(entry.compare,
                                                         a.*entry.member, b.*entry.member));
                }
            };
            return (... && equal(descriptor));
        }, registered_fields<T>());
    } else if constexpr (Traits::vector || Traits::array) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!equal_child(element(a, i), element(b, i), remaining)) {
                return false;
            }
        }
        return true;
    } else if constexpr (Traits::optional) {
        return a.has_value() == b.has_value() && (!a || equal_child(*a, *b, remaining));
    } else if constexpr (Traits::map) {
        if (a.size() != b.size()) {
            return false;
        }
        for (const auto& [key_value, mapped] : a) {
            const auto other = b.find(key_value);
            if (other == b.end() || !equal_child(mapped, other->second, remaining)) {
                return false;
            }
        }
        return true;
    } else {
        return static_cast<bool>(a == b);
    }
}

template <typename T>
void print_value(std::ostream& stream, const T& item) {
    using Traits = container_traits<T>;
    if constexpr (requires { stream << item; }) {
        stream << item;
    } else if constexpr (Traits::vector || Traits::array) {
        stream << '[';
        for (std::size_t i = 0; i < item.size(); ++i) {
            if (i != 0) {
                stream << ", ";
            }
            print_value(stream, element(item, i));
        }
        stream << ']';
    } else if constexpr (Traits::map) {
        stream << '{';
        bool first = true;
        for (const auto& [key_value, mapped] : item) {
            if (!first) {
                stream << ", ";
            }
            first = false;
            print_value(stream, key_value);
            stream << ": ";
            print_value(stream, mapped);
        }
        stream << '}';
    } else if constexpr (Traits::optional) {
        if (item) {
            print_value(stream, *item);
        } else {
            stream << "<missing>";
        }
    } else if constexpr (registered<T>) {
        stream << '{';
        bool first = true;
        std::apply([&](const auto&... descriptor) {
            const auto print_field = [&](const auto& entry) {
                if (!first) {
                    stream << ", ";
                }
                first = false;
                stream << entry.name << ": ";
                print_value(stream, item.*entry.member);
            };
            (print_field(descriptor), ...);
        }, registered_fields<T>());
        stream << '}';
    } else {
        stream << "<value>";
    }
}

// One path buffer per traversal. Only returned events own path strings.
struct path_scope {
    std::string& path;
    std::size_t size;
    std::string& source;
    std::size_t source_size;
    path_scope(std::string& current, std::string& original)
        : path(current), size(current.size()), source(original), source_size(original.size()) {}
    ~path_scope() { path.resize(size); source.resize(source_size); }
};

inline void append_member(std::string& path, std::string_view name) {
    if (!path.empty()) {
        path += '.';
    }
    path += name;
}

inline void append_index(std::string& path, std::size_t index) {
    path += '[';
    char buffer[std::numeric_limits<std::size_t>::digits10 + 2];
    const auto converted = std::to_chars(std::begin(buffer), std::end(buffer), index);
    path.append(buffer, converted.ptr);
    path += ']';
}

inline void append_quoted(std::string& path, std::string_view text) {
    constexpr char hex[] = "0123456789abcdef";
    path += '"';
    for (unsigned char byte : text) {
        if (byte == '"' || byte == '\\') {
            path += '\\';
            path += static_cast<char>(byte);
        } else if (byte < 0x20) {
            path += "\\u00";
            path += hex[byte >> 4];
            path += hex[byte & 15];
        } else {
            path += static_cast<char>(byte);
        }
    }
    path += '"';
}

template <typename Key>
void append_map_key(std::string& path, const Key& key_value) {
    if constexpr (std::is_enum_v<Key>) {
        append_map_key(path, static_cast<std::underlying_type_t<Key>>(key_value));
    } else {
        path += '[';
        if constexpr (std::same_as<Key, std::string> || std::same_as<Key, std::string_view>) {
            append_quoted(path, key_value);
        } else if constexpr (std::same_as<Key, bool>) {
            path += key_value ? "true" : "false";
        } else if constexpr (std::is_integral_v<Key>) {
            char buffer[std::numeric_limits<Key>::digits10 + 4];
            const auto converted = std::to_chars(std::begin(buffer), std::end(buffer), key_value);
            path.append(buffer, converted.ptr);
        } else {
            static_assert(requires(std::ostream& stream) { stream << key_value; },
                          "Map keys need operator<< to form diff paths");
            std::ostringstream stream;
            stream.imbue(std::locale::classic());
            if constexpr (std::is_floating_point_v<Key>) {
                stream.precision(std::numeric_limits<Key>::max_digits10);
            }
            stream << key_value;
            append_quoted(path, stream.str());
        }
        path += ']';
    }
}

template <typename T>
struct is_key_selector : std::false_type {};

template <typename Owner, typename Member>
struct is_key_selector<key_selector<Owner, Member>> : std::true_type {};

template <bool Collect, bool Deep = false, typename... Keys>
struct traversal_context {
    static constexpr bool deep = Deep;
    using event_type = std::conditional_t<Deep, deep_change, change>;
    const std::vector<std::string>& ignored_paths;
    std::vector<event_type>* changes;
    const deep_options* options = nullptr;
    std::tuple<const Keys&...> keys;
    std::string path;
    std::string source_path{};

    [[nodiscard]] bool excluded(bool removal = false) const {
        return ignored(Deep && removal ? source_path : path, ignored_paths);
    }

    void push_member(std::string_view name) {
        append_member(path, name);
        if constexpr (Deep) { append_member(source_path, name); }
    }

    void push_index(std::size_t destination, std::size_t source) {
        append_index(path, destination);
        if constexpr (Deep) { append_index(source_path, source); }
    }

    template <typename Key>
    void push_key(const Key& destination, const Key& source) {
        append_map_key(path, destination);
        if constexpr (Deep) { append_map_key(source_path, source); }
    }

    [[nodiscard]] std::size_t remaining(std::size_t depth) const {
        if constexpr (Deep) {
            if (depth > options->max_depth) {
                throw std::length_error("vardiff::deep_diff: maximum depth exceeded");
            }
            return options->max_depth - depth;
        } else {
            return std::numeric_limits<std::size_t>::max();
        }
    }

    void emit(change_kind kind, value before, value after) {
        event_type event{};
        event.name = Deep && kind == change_kind::removed ? source_path : path;
        event.before = std::move(before);
        event.after = std::move(after);
        event.kind = kind;
        changes->push_back(std::move(event));
    }

    void move(std::size_t old_index, std::size_t new_index)
        requires Deep {
        emit(change_kind::moved, {}, {});
        auto& event = changes->back();
        event.from = source_path;
        event.old_index = old_index;
        event.new_index = new_index;
    }

    template <typename Element, typename Function>
    bool with_key([[maybe_unused]] Function&& function) const {
        // An exact path takes precedence over a type-wide key, in either order.
        for (bool exact : {true, false}) {
            const bool found = std::apply([&](const auto&... selected) {
                [[maybe_unused]] const auto try_one = [&](const auto& selector) {
                    if constexpr (std::is_invocable_v<decltype(selector.member), const Element&>) {
                        if (exact ? !selector.path.empty() && selector.path == path
                                  : selector.path.empty()) {
                            function(selector);
                            return true;
                        }
                    }
                    return false;
                };
                return (... || try_one(selected));
            }, keys);
            if (found) {
                return true;
            }
        }
        return false;
    }
};

template <bool Collect, typename T, typename Context>
bool walk_difference(const T& a, const T& b, Context& context, std::size_t depth);

template <bool Collect, typename Field, typename T, typename Context>
bool walk_field(const Field& descriptor, const T& a, const T& b,
                Context& context, std::size_t depth) {
    if constexpr (std::same_as<typename Field::comparator_type, default_comparator>) {
        return walk_difference<Collect>(a, b, context, depth);
    } else {
        if (context.excluded()) {
            return false;
        }
        (void)context.remaining(depth);
        if (std::invoke(descriptor.compare, a, b)) {
            return false;
        }
        if constexpr (Collect) {
            context.emit(change_kind::modified, value(a), value(b));
        }
        return true;
    }
}

template <bool Collect, typename T, typename Context>
bool walk_indexed(const T& a, const T& b, Context& context, std::size_t depth) {
    bool found = false;
    const auto common = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < common; ++i) {
        // With selectors, equality alone cannot skip nested identity changes.
        if constexpr (!Context::deep) {
            if (context.ignored_paths.empty() && equal_values(element(a, i), element(b, i))) {
                continue;
            }
        }
        path_scope scope(context.path, context.source_path);
        context.push_index(i, i);
        found = walk_difference<Collect>(element(a, i), element(b, i), context, depth + 1) || found;
        if constexpr (!Collect) {
            if (found) {
                return true;
            }
        }
    }
    for (std::size_t i = common; i < std::max(a.size(), b.size()); ++i) {
        path_scope scope(context.path, context.source_path);
        context.push_index(i, i);
        if (context.excluded(i < a.size())) {
            continue;
        }
        (void)context.remaining(depth + 1);
        if constexpr (Collect) {
            if (i < a.size()) {
                context.emit(change_kind::removed, value(element(a, i)), {});
            } else {
                context.emit(change_kind::added, {}, value(element(b, i)));
            }
        }
        found = true;
        if constexpr (!Collect) {
            return true;
        }
    }
    return found;
}

// Stable FIFO matching keeps duplicate identities deterministic. Index tokens
// refer into the input sequences, so matching never copies elements or keys.
template <bool Keyed, typename T, typename Context, typename Identity>
bool walk_matched(const T& a, const T& b, Context& context, std::size_t depth,
                  Identity identity) {
    if ((a.empty() || b.empty()) &&
        (!Keyed || context.options->duplicates != duplicate_key_policy::reject)) {
        return walk_indexed<true>(a, b, context, depth);
    }
    const auto same = [&](bool right_a, std::size_t i, bool right_b, std::size_t j) {
        if constexpr (Keyed) {
            return static_cast<bool>(identity(right_a, i) == identity(right_b, j));
        } else {
            return equal_values(identity(right_a, i), identity(right_b, j), context.remaining(depth + 1));
        }
    };
    // Common keyed case: unchanged order needs no matching workspace at all.
    if constexpr (Keyed) {
        if (context.options->duplicates != duplicate_key_policy::reject && a.size() == b.size()) {
            bool aligned = true;
            for (std::size_t i = 0; i < a.size() && aligned; ++i) {
                aligned = same(false, i, true, i);
            }
            if (aligned) {
                return walk_indexed<true>(a, b, context, depth);
            }
        }
    }
    constexpr auto missing = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> old_to_new(a.size(), missing), new_to_old(b.size(), missing);
    using Key = std::remove_cvref_t<decltype(identity(false, std::size_t{}))>;
    using Traits = container_traits<Key>;
    constexpr bool hashable = requires(const Key& item) {
        { std::hash<Key>{}(item) } -> std::convertible_to<std::size_t>;
    };
    if constexpr (hashable && (Keyed || !(registered<Key> || Traits::vector ||
                                         Traits::array || Traits::optional || Traits::map))) {
        struct token { bool right; std::size_t index; };
        struct bucket {
            std::size_t head = std::numeric_limits<std::size_t>::max();
            std::size_t tail = std::numeric_limits<std::size_t>::max();
            bool seen_right = false;
        };
        const auto hash = [&](const token& item) {
            return std::hash<Key>{}(identity(item.right, item.index));
        };
        const auto equal = [&](const token& x, const token& y) {
            if (x.right == y.right && x.index == y.index) {
                return true; // Reflexive tokens, even for an unkeyed NaN value.
            }
            return same(x.right, x.index, y.right, y.index);
        };
        std::unordered_map<token, bucket, decltype(hash), decltype(equal)> index(0, hash, equal);
        index.reserve(a.size());
        std::vector<std::size_t> next(a.size(), missing);
        for (std::size_t i = 0; i < a.size(); ++i) {
            auto [entry, inserted] = index.try_emplace(token{false, i}, bucket{i, i, false});
            if (!inserted) {
                if constexpr (Keyed) {
                    if (context.options->duplicates == duplicate_key_policy::reject) {
                        throw std::invalid_argument("vardiff::deep_diff: duplicate key in before");
                    }
                }
                next[entry->second.tail] = i;
                entry->second.tail = i;
            }
        }
        for (std::size_t j = 0; j < b.size(); ++j) {
            auto entry = index.find(token{true, j});
            if constexpr (Keyed) {
                if (context.options->duplicates == duplicate_key_policy::reject) {
                    if (entry == index.end()) {
                        entry = index.try_emplace(token{true, j}).first;
                    }
                    if (entry->second.seen_right) {
                        throw std::invalid_argument("vardiff::deep_diff: duplicate key in after");
                    }
                    entry->second.seen_right = true;
                }
            }
            if (entry != index.end() && entry->second.head != missing) {
                const auto i = entry->second.head;
                entry->second.head = next[i];
                old_to_new[i] = j;
                new_to_old[j] = i;
            }
        }
    } else {
        if constexpr (Keyed) {
            if (context.options->duplicates == duplicate_key_policy::reject) {
                for (bool right : {false, true}) {
                    const auto count = right ? b.size() : a.size();
                    for (std::size_t i = 0; i < count; ++i) {
                        for (std::size_t j = 0; j < i; ++j) {
                            if (same(right, i, right, j)) {
                                throw std::invalid_argument("vardiff::deep_diff: duplicate key");
                            }
                        }
                    }
                }
            }
        }
        for (std::size_t j = 0; j < b.size(); ++j) {
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (old_to_new[i] == missing && same(false, i, true, j)) {
                    old_to_new[i] = j;
                    new_to_old[j] = i;
                    break;
                }
            }
        }
    }
    if constexpr (!Keyed) {
        // With no identity, an unmatched value at the same position is an edit.
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            if (old_to_new[i] == missing && new_to_old[i] == missing) {
                old_to_new[i] = i;
                new_to_old[i] = i;
            }
        }
    }

    // A longest increasing subsequence is the largest group that can stay put.
    // Its complement gives the minimum number of moves for this matching.
    std::vector<bool> stationary;
    if (context.options->detect_moves) {
        stationary.resize(b.size(), false);
        std::vector<std::size_t> tails, previous(b.size(), missing);
        tails.reserve(std::min(a.size(), b.size()));
        for (std::size_t j = 0; j < b.size(); ++j) {
            if (new_to_old[j] == missing) {
                continue;
            }
            const auto pos = std::lower_bound(tails.begin(), tails.end(), new_to_old[j],
                [&](std::size_t tail, std::size_t old) { return new_to_old[tail] < old; });
            if (pos != tails.begin()) {
                previous[j] = *(pos - 1);
            }
            if (pos == tails.end()) {
                tails.push_back(j);
            } else {
                *pos = j;
            }
        }
        for (auto j = tails.empty() ? missing : tails.back(); j != missing; j = previous[j]) {
            stationary[j] = true;
        }
    }
    bool found = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (old_to_new[i] == missing) {
            path_scope scope(context.path, context.source_path);
            context.push_index(i, i);
            if (!context.excluded(true)) {
                (void)context.remaining(depth + 1);
                context.emit(change_kind::removed, value(element(a, i)), {});
                found = true;
            }
        }
    }
    for (std::size_t j = 0; j < b.size(); ++j) {
        path_scope scope(context.path, context.source_path);
        context.push_index(j, new_to_old[j] == missing ? j : new_to_old[j]);
        const auto i = new_to_old[j];
        if (i == missing) {
            if (context.excluded()) {
                continue;
            }
            (void)context.remaining(depth + 1);
            context.emit(change_kind::added, {}, value(element(b, j)));
            found = true;
        } else {
            if (context.options->detect_moves && !stationary[j] && !context.excluded()) {
                (void)context.remaining(depth + 1);
                context.move(i, j);
                found = true;
            }
            found = walk_difference<true>(element(a, i), element(b, j), context, depth + 1) || found;
        }
    }
    return found;
}

template <bool Collect, typename T, typename Context>
bool walk_difference(const T& a, const T& b, Context& context, std::size_t depth) {
    using Traits = container_traits<T>;
    const bool removal = [&] {
        if constexpr (Traits::optional) { return a.has_value() && !b.has_value(); }
        else { return false; }
    }();
    if (context.excluded(removal)) {
        return false;
    }
    const auto remaining = context.remaining(depth);
    if constexpr (registered<T> || Traits::vector || Traits::array || Traits::optional || Traits::map) {
        // A key can observe members that are intentionally outside schema<T>.
        // Never let structural equality hide those identity changes.
        if constexpr (std::tuple_size_v<decltype(context.keys)> == 0) {
            // A deep traversal compares a changed tree once, rather than
            // rescanning every ancestor of a deeply nested edit.
            if ((Context::deep ? depth == 0 : depth != 0) && context.ignored_paths.empty() &&
                equal_values(a, b, remaining)) {
                return false;
            }
        }
    }
    if constexpr (registered<T>) {
        bool found = false;
        std::apply([&](const auto&... descriptor) {
            const auto visit = [&](const auto& entry) {
                if constexpr (!Collect) {
                    if (found) {
                        return;
                    }
                }
                path_scope scope(context.path, context.source_path);
                context.push_member(entry.name);
                found = walk_field<Collect>(entry, a.*entry.member, b.*entry.member,
                                             context, depth + 1) || found;
            };
            (visit(descriptor), ...);
        }, registered_fields<T>());
        return found;
    } else if constexpr (Traits::vector || Traits::array) {
        if constexpr (Context::deep) {
            bool found = false;
            if (context.template with_key<typename T::value_type>([&](const auto& selected) {
                    const auto identity = [&](bool right, std::size_t i) -> decltype(auto) {
                        return (element(right ? b : a, i).*selected.member);
                    };
                    found = walk_matched<true>(a, b, context, depth, identity);
                })) {
                return found;
            }
            if (context.options->detect_moves && !a.empty() && !b.empty()) {
                const auto identity = [&](bool right, std::size_t i) -> decltype(auto) {
                    return element(right ? b : a, i);
                };
                return walk_matched<false>(a, b, context, depth, identity);
            }
        }
        return walk_indexed<Collect>(a, b, context, depth);
    } else if constexpr (Traits::map) {
        bool found = false;
        for (const auto& [key_value, mapped] : a) {
            const auto other = b.find(key_value);
            if constexpr (!Context::deep) {
                if (other != b.end() && context.ignored_paths.empty() && equal_values(mapped, other->second)) {
                    continue;
                }
            }
            path_scope scope(context.path, context.source_path);
            context.push_key(other == b.end() ? key_value : other->first, key_value);
            if (context.excluded(other == b.end())) {
                continue;
            }
            (void)context.remaining(depth + 1);
            if (other == b.end()) {
                if constexpr (Collect) {
                    context.emit(change_kind::removed, value(mapped), {});
                }
                found = true;
            } else {
                found = walk_difference<Collect>(mapped, other->second, context, depth + 1) || found;
            }
            if constexpr (!Collect) {
                if (found) {
                    return true;
                }
            }
        }
        for (const auto& [key_value, mapped] : b) {
            if (a.find(key_value) != a.end()) {
                continue;
            }
            path_scope scope(context.path, context.source_path);
            context.push_key(key_value, key_value);
            if (!context.excluded()) {
                (void)context.remaining(depth + 1);
                if constexpr (Collect) {
                    context.emit(change_kind::added, {}, value(mapped));
                }
                found = true;
                if constexpr (!Collect) {
                    return true;
                }
            }
        }
        return found;
    } else if constexpr (Traits::optional) {
        if (a && b) {
            return walk_difference<Collect>(*a, *b, context, depth + 1);
        }
        if (a.has_value() == b.has_value()) {
            return false;
        }
        (void)context.remaining(depth + 1);
        if constexpr (Collect) {
            if (a) {
                context.emit(change_kind::removed, value(*a), {});
            } else {
                context.emit(change_kind::added, {}, value(*b));
            }
        }
        return true;
    } else {
        if (a == b) {
            return false;
        }
        if constexpr (Collect) {
            context.emit(change_kind::modified, value(a), value(b));
        }
        return true;
    }
}

// Existing watcher/free diff APIs use the same traversal with index semantics.
template <bool Collect, typename Field, typename A, typename B>
bool visit_field(const Field& descriptor, const A& a, const B& b,
                 const std::string& path, const std::vector<std::string>& ignored_paths,
                 std::vector<change>* changes) {
    if (ignored(path, ignored_paths)) {
        return false;
    }
    if constexpr (std::same_as<typename Field::comparator_type, default_comparator>) {
        // Preserve allocation-free idle reads even for long top-level names:
        // inspect the borrowed name before creating mutable traversal buffers.
        if (ignored_paths.empty() && equal_values(a, b)) {
            return false;
        }
    }
    traversal_context<Collect> context{ignored_paths, changes, nullptr, {}, path};
    return walk_field<Collect>(descriptor, a, b, context, 0);
}


template <typename T>
void restore_value(T& target, const T& saved, const std::string& path,
                   const std::vector<std::string>& ignored_paths);

template <typename Field, typename T>
void restore_field(const Field&, T& target, const T& saved, const std::string& path,
                   const std::vector<std::string>& ignored_paths) {
    if (ignored(path, ignored_paths)) {
        return;
    }
    if constexpr (std::same_as<typename Field::comparator_type, default_comparator>) {
        restore_value(target, saved, path, ignored_paths);
    } else {
        target = saved;
    }
}

template <typename T>
void restore_value(T& target, const T& saved, const std::string& path,
                   const std::vector<std::string>& ignored_paths) {
    if (ignored(path, ignored_paths)) {
        return;
    }
    using Traits = container_traits<T>;
    if constexpr (registered<T>) {
        std::apply([&](const auto&... descriptor) {
            (restore_field(descriptor, target.*descriptor.member, saved.*descriptor.member,
                           member_path(path, descriptor.name), ignored_paths), ...);
        }, registered_fields<T>());
    } else if constexpr (Traits::vector || Traits::array) {
        const auto common = std::min(target.size(), saved.size());
        for (std::size_t i = 0; i < common; ++i) {
            if constexpr (std::same_as<typename T::value_type, bool>) {
                if (!ignored(index_path(path, i), ignored_paths)) {
                    target[i] = saved[i];
                }
            } else {
                restore_value(target[i], saved[i], index_path(path, i), ignored_paths);
            }
        }
        if constexpr (Traits::vector) {
            while (target.size() > saved.size()) {
                target.pop_back();
            }
            while (target.size() < saved.size()) {
                target.push_back(saved[target.size()]);
            }
        }
    } else if constexpr (Traits::optional) {
        if (!saved) {
            target.reset();
        } else if (!target) {
            target.emplace(*saved);
        } else {
            restore_value(*target, *saved, path, ignored_paths);
        }
    } else if constexpr (Traits::map) {
        for (auto item = target.begin(); item != target.end();) {
            auto item_path = path;
            append_map_key(item_path, item->first);
            if (ignored(item_path, ignored_paths)) {
                ++item;
                continue;
            }
            const auto previous = saved.find(item->first);
            if (previous == saved.end()) {
                item = target.erase(item);
            } else {
                restore_value(item->second, previous->second, item_path, ignored_paths);
                ++item;
            }
        }
        for (const auto& [key_value, mapped] : saved) {
            if (target.find(key_value) == target.end()) {
                auto item_path = path;
                append_map_key(item_path, key_value);
                if (!ignored(item_path, ignored_paths)) {
                    target.emplace(key_value, mapped);
                }
            }
        }
    } else {
        target = saved;
    }
}

} // namespace detail

struct watch_options {
    bool auto_snapshot = true;
    std::size_t history_limit = 64; // Includes the current snapshot.
};

template <typename Object, typename... Fields>
    requires (sizeof...(Fields) > 0) && (detail::readable_field<Fields, Object> && ...)
class watcher {
public:
    using snapshot_type = std::tuple<typename Fields::value_type...>;
    using changes_type = std::vector<change>;
    using callback_type = std::function<void(const changes_type&)>;

    explicit watcher(Object& object, Fields... fields)
        : watcher(object, watch_options{}, std::move(fields)...) {}

    explicit watcher(Object& object, watch_options options, Fields... fields)
        : object_(std::addressof(object)), fields_(std::move(fields)...), options_(options) {
        validate_limit(options_.history_limit);
        history_.push_back(capture());
    }

    watcher(Object&&, Fields...) = delete;
    watcher(Object&&, watch_options, Fields...) = delete;

    [[nodiscard]] bool changed() const {
        return changed_impl(std::index_sequence_for<Fields...>{});
    }

    [[nodiscard]] changes_type diff() const {
        changes_type changes;
        append_current_changes(changes, std::index_sequence_for<Fields...>{});
        return changes;
    }

    void snapshot() {
        ensure_idle();
        append_snapshot(capture());
    }

    watcher& on_change(callback_type callback) {
        if (!callback) {
            throw std::invalid_argument("vardiff::on_change: empty callback");
        }
        callbacks_.push_back(std::move(callback));
        return *this;
    }

    watcher& on_change(std::string_view path, std::function<void(const change&)> callback) {
        if (path.empty() || !callback) {
            throw std::invalid_argument("vardiff::on_change: empty path or callback");
        }
        return on_change([prefix = std::string(path), callback = std::move(callback)]
                         (const changes_type& changes) {
            for (const auto& item : changes) {
                if (detail::path_matches(item.name, prefix)) {
                    callback(item);
                }
            }
        });
    }

    changes_type poll() {
        ensure_idle();
        struct guard {
            bool& active;
            explicit guard(bool& state) : active(state) { active = true; }
            ~guard() { active = false; }
        } polling(polling_.active);

        if (!changed()) {
            return {};
        }
        const bool update = options_.auto_snapshot;
        auto observed = capture();
        changes_type changes;
        append_saved_changes(changes, *observed, std::index_sequence_for<Fields...>{});
        if (changes.empty()) {
            return changes;
        }
        // New registrations take effect on the next poll.
        const auto callback_count = callbacks_.size();
        for (std::size_t i = 0; i < callback_count; ++i) {
            callbacks_[i](changes);
        }
        if (update) {
            append_snapshot(std::move(observed));
        }
        return changes;
    }

    watcher& ignore(std::string_view path) {
        if (path.empty()) {
            throw std::invalid_argument("vardiff::ignore: empty path");
        }
        if (std::find(ignored_.begin(), ignored_.end(), path) == ignored_.end()) {
            ignored_.emplace_back(path);
        }
        return *this;
    }

    watcher& set_auto_snapshot(bool enabled) noexcept {
        options_.auto_snapshot = enabled;
        return *this;
    }

    [[nodiscard]] bool auto_snapshot_enabled() const noexcept { return options_.auto_snapshot; }

    watcher& set_history_limit(std::size_t limit) {
        ensure_idle();
        validate_limit(limit);
        options_.history_limit = limit;
        trim_history();
        return *this;
    }

    [[nodiscard]] std::size_t history_limit() const noexcept { return options_.history_limit; }

    // Independent copies; values in each tuple follow field descriptor order.
    [[nodiscard]] std::vector<snapshot_type> history() const {
        // A forward range constructs all copies without instantiating vector's
        // reallocation/move path for snapshots containing copy-only values.
        auto snapshots = std::views::transform(history_,
            [](const auto& saved) -> const snapshot_type& { return *saved; });
        return std::vector<snapshot_type>(snapshots.begin(), snapshots.end());
    }

    [[nodiscard]] std::size_t history_position() const noexcept { return cursor_; }
    [[nodiscard]] std::size_t history_size() const noexcept { return history_.size(); }
    [[nodiscard]] bool can_undo() const noexcept { return cursor_ != 0; }
    [[nodiscard]] bool can_redo() const noexcept { return cursor_ + 1 < history_.size(); }

    bool undo() requires (detail::writable_field<Fields, Object> && ...) {
        ensure_idle();
        if (!can_undo()) {
            return false;
        }
        restore(*history_[cursor_ - 1], std::index_sequence_for<Fields...>{});
        --cursor_;
        return true;
    }

    bool redo() requires (detail::writable_field<Fields, Object> && ...) {
        ensure_idle();
        if (!can_redo()) {
            return false;
        }
        restore(*history_[cursor_ + 1], std::index_sequence_for<Fields...>{});
        ++cursor_;
        return true;
    }

    bool rollback() requires (detail::writable_field<Fields, Object> && ...) { return undo(); }

    bool rollback(std::size_t position) requires (detail::writable_field<Fields, Object> && ...) {
        ensure_idle();
        if (position >= history_.size()) {
            throw std::out_of_range("vardiff::rollback: invalid history position");
        }
        restore(*history_[position], std::index_sequence_for<Fields...>{});
        cursor_ = position;
        return true;
    }

private:
    using saved_snapshot = std::shared_ptr<const snapshot_type>;

    void ensure_idle() const {
        if (polling_.active) {
            throw std::logic_error("vardiff: poll or history mutation during callback");
        }
    }

    static void validate_limit(std::size_t limit) {
        if (limit == 0) {
            throw std::invalid_argument("vardiff: history limit must be at least one");
        }
    }

    [[nodiscard]] const snapshot_type& baseline() const { return *history_[cursor_]; }

    template <std::size_t... Indices>
    [[nodiscard]] saved_snapshot capture_impl(std::index_sequence<Indices...>) const {
        return std::make_shared<const snapshot_type>(
            (object_->*std::get<Indices>(fields_).member)...);
    }

    [[nodiscard]] saved_snapshot capture() const {
        return capture_impl(std::index_sequence_for<Fields...>{});
    }

    void trim_history() noexcept {
        while (history_.size() > options_.history_limit) {
            if (cursor_ != 0) {
                history_.pop_front();
                --cursor_;
            } else {
                history_.pop_back();
            }
        }
    }

    void append_snapshot(saved_snapshot saved) {
        if (can_redo()) {
            // Allocate before changing the old timeline.
            std::deque<saved_snapshot> next;
            for (std::size_t i = 0; i <= cursor_; ++i) {
                next.push_back(history_[i]);
            }
            next.push_back(std::move(saved));
            history_.swap(next);
        } else {
            history_.push_back(std::move(saved));
        }
        cursor_ = history_.size() - 1;
        trim_history();
    }

    template <std::size_t Index>
    [[nodiscard]] bool field_changed() const {
        const auto& descriptor = std::get<Index>(fields_);
        const auto& before = std::get<Index>(baseline());
        const auto& after = object_->*descriptor.member;
        if (ignored_.empty()) {
            // No diff values, copies, path strings or allocations on this path.
            return !std::invoke(descriptor.compare, before, after);
        }
        return detail::visit_field<false>(descriptor, before, after,
                                          descriptor.name, ignored_, nullptr);
    }

    template <std::size_t... Indices>
    [[nodiscard]] bool changed_impl(std::index_sequence<Indices...>) const {
        return (... || field_changed<Indices>());
    }

    template <std::size_t... Indices>
    void append_current_changes(changes_type& changes, std::index_sequence<Indices...>) const {
        (detail::visit_field<true>(std::get<Indices>(fields_),
                                  std::get<Indices>(baseline()),
                                  object_->*std::get<Indices>(fields_).member,
                                  std::get<Indices>(fields_).name, ignored_, &changes), ...);
    }

    template <std::size_t... Indices>
    void append_saved_changes(changes_type& changes, const snapshot_type& observed,
                              std::index_sequence<Indices...>) const {
        (detail::visit_field<true>(std::get<Indices>(fields_),
                                  std::get<Indices>(baseline()), std::get<Indices>(observed),
                                  std::get<Indices>(fields_).name, ignored_, &changes), ...);
    }

    template <std::size_t... Indices>
    void restore(const snapshot_type& saved, std::index_sequence<Indices...>) {
        (detail::restore_field(std::get<Indices>(fields_),
                              object_->*std::get<Indices>(fields_).member,
                              std::get<Indices>(saved), std::get<Indices>(fields_).name,
                              ignored_), ...);
    }

    detail::poll_state polling_;
    Object* object_;
    std::tuple<Fields...> fields_;
    watch_options options_;
    std::deque<saved_snapshot> history_;
    std::size_t cursor_ = 0;
    std::vector<std::string> ignored_;
    std::deque<callback_type> callbacks_;
};

template <typename Object, typename... Fields>
    requires std::is_lvalue_reference_v<Object> && (sizeof...(Fields) > 0) &&
             (detail::readable_field<Fields, std::remove_reference_t<Object>> && ...)
[[nodiscard]] auto track(Object&& object, Fields... fields) {
    return watcher<std::remove_reference_t<Object>, Fields...>(object, std::move(fields)...);
}

template <typename Object, typename... Fields>
    requires std::is_lvalue_reference_v<Object> && (sizeof...(Fields) > 0) &&
             (detail::readable_field<Fields, std::remove_reference_t<Object>> && ...)
[[nodiscard]] auto track(Object&& object, watch_options options, Fields... fields) {
    return watcher<std::remove_reference_t<Object>, Fields...>(object, options, std::move(fields)...);
}

template <typename Object>
    requires std::is_lvalue_reference_v<Object> && detail::registered<std::remove_reference_t<Object>>
[[nodiscard]] auto track(Object&& object, watch_options options = {}) {
    return std::apply([&](auto... descriptor) {
        return track(object, options, std::move(descriptor)...);
    }, schema<std::remove_cvref_t<Object>>::fields());
}

template <typename Object, typename... Fields>
    requires (sizeof...(Fields) > 0) && (detail::readable_field<Fields, Object> && ...)
[[nodiscard]] std::vector<change> diff(const Object& before, const Object& after,
                                      const Fields&... fields) {
    std::vector<change> result;
    const std::vector<std::string> ignored;
    (detail::visit_field<true>(fields, before.*fields.member, after.*fields.member,
                              fields.name, ignored, &result), ...);
    return result;
}

template <typename Object, typename... Fields>
    requires (sizeof...(Fields) > 0) && (detail::readable_field<Fields, Object> && ...)
[[nodiscard]] bool changed(const Object& before, const Object& after, const Fields&... fields) {
    return (... || !std::invoke(fields.compare, before.*fields.member, after.*fields.member));
}

template <detail::registered Object>
[[nodiscard]] std::vector<change> diff(const Object& before, const Object& after) {
    return std::apply([&](const auto&... fields) {
        return diff(before, after, fields...);
    }, schema<Object>::fields());
}

template <detail::registered Object>
[[nodiscard]] bool changed(const Object& before, const Object& after) {
    return !detail::equal_values(before, after);
}

// Keys select sequence identity by element type, optionally at an exact path.
// Inputs are borrowed only during this call; event values own their copies.
template <supported_type Object, typename... Keys>
    requires (detail::is_key_selector<Keys>::value && ...)
[[nodiscard]] std::vector<deep_change> deep_diff(const Object& before, const Object& after,
                                               const deep_options& options = {},
                                               const Keys&... keys) {
    for (const auto& path : options.ignored_paths) {
        if (path.empty()) {
            throw std::invalid_argument("vardiff::deep_diff: empty ignored path");
        }
    }
    std::vector<deep_change> changes;
    detail::traversal_context<true, true, Keys...> context{
        options.ignored_paths, &changes, &options, std::tie(keys...), {}};
    detail::walk_difference<true>(before, after, context, 0);
    return changes;
}

template <supported_type Object, typename FirstKey, typename... Keys>
    requires detail::is_key_selector<FirstKey>::value &&
             (detail::is_key_selector<Keys>::value && ...)
[[nodiscard]] std::vector<deep_change> deep_diff(const Object& before, const Object& after,
                                               const FirstKey& first, const Keys&... keys) {
    return deep_diff(before, after, deep_options{}, first, keys...);
}

} // namespace vardiff

#endif // VARDIFF_VARDIFF_HPP
