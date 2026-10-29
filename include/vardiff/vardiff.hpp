// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#ifndef VARDIFF_VARDIFF_HPP
#define VARDIFF_VARDIFF_HPP

#include <algorithm>
#include <any>
#include <array>
#include <concepts>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
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
    static constexpr bool vector = false, array = false, optional = false;
};

template <typename T, typename Allocator>
struct container_traits<std::vector<T, Allocator>> {
    using element_type = T;
    static constexpr bool vector = true, array = false, optional = false;
};

template <typename T, std::size_t Size>
struct container_traits<std::array<T, Size>> {
    using element_type = T;
    static constexpr bool vector = false, array = true, optional = false;
};

template <typename T>
struct container_traits<std::optional<T>> {
    using element_type = T;
    static constexpr bool vector = false, array = false, optional = true;
};

template <typename T>
consteval bool is_supported() {
    using Item = std::remove_cv_t<T>;
    using Traits = container_traits<Item>;
    if constexpr (std::is_volatile_v<T> || !std::is_copy_constructible_v<Item>) {
        return false;
    } else if constexpr (registered<Item>) {
        return true;
    } else if constexpr (Traits::vector || Traits::array || Traits::optional) {
        return is_supported<typename Traits::element_type>();
    } else {
        return requires(const Item& a, const Item& b) {
            { a == b } -> std::convertible_to<bool>;
        };
    }
}

template <typename T>
bool equal_values(const T& a, const T& b);

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

enum class change_kind { modified, added, removed };

struct change {
    std::string name;
    value before;
    value after;
    change_kind kind = change_kind::modified;
};

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
bool equal_values(const T& a, const T& b) {
    using Traits = container_traits<T>;
    if constexpr (registered<T>) {
        return std::apply([&](const auto&... descriptor) {
            return (... && std::invoke(descriptor.compare,
                                      a.*descriptor.member, b.*descriptor.member));
        }, registered_fields<T>());
    } else if constexpr (Traits::vector || Traits::array) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!equal_values(element(a, i), element(b, i))) {
                return false;
            }
        }
        return true;
    } else if constexpr (Traits::optional) {
        return a.has_value() == b.has_value() && (!a || equal_values(*a, *b));
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

template <bool Collect, typename T>
bool visit_difference(const T& a, const T& b, const std::string& path,
                      const std::vector<std::string>& ignored_paths,
                      std::vector<change>* changes);

template <bool Collect, typename Field, typename A, typename B>
bool visit_field(const Field& descriptor, const A& a, const B& b,
                 const std::string& path, const std::vector<std::string>& ignored_paths,
                 std::vector<change>* changes) {
    if (ignored(path, ignored_paths)) {
        return false;
    }
    if constexpr (std::same_as<typename Field::comparator_type, default_comparator>) {
        using Item = std::remove_cvref_t<A>;
        using Traits = container_traits<Item>;
        if constexpr (registered<Item> || Traits::vector || Traits::array || Traits::optional) {
            // Skip unchanged subtrees before allocating any child paths.
            if (ignored_paths.empty() && equal_values(a, b)) {
                return false;
            }
        }
        return visit_difference<Collect>(a, b, path, ignored_paths, changes);
    } else {
        if (std::invoke(descriptor.compare, a, b)) {
            return false;
        }
        if constexpr (Collect) {
            changes->push_back(change{path, value(a), value(b)});
        }
        return true;
    }
}

template <bool Collect, typename T>
bool visit_difference(const T& a, const T& b, const std::string& path,
                      const std::vector<std::string>& ignored_paths,
                      std::vector<change>* changes) {
    if (ignored(path, ignored_paths)) {
        return false;
    }
    using Traits = container_traits<T>;
    if constexpr (registered<T>) {
        bool found = false;
        std::apply([&](const auto&... descriptor) {
            const auto visit = [&](const auto& entry) {
                if constexpr (!Collect) {
                    if (found) {
                        return;
                    }
                }
                const bool item_changed = visit_field<Collect>(
                    entry, a.*entry.member, b.*entry.member,
                    member_path(path, entry.name), ignored_paths, changes);
                found = found || item_changed;
            };
            (visit(descriptor), ...);
        }, registered_fields<T>());
        return found;
    } else if constexpr (Traits::vector || Traits::array) {
        bool found = false;
        const auto common = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < common; ++i) {
            if (ignored_paths.empty() && equal_values(element(a, i), element(b, i))) {
                continue;
            }
            const bool item_changed = visit_difference<Collect>(
                element(a, i), element(b, i), index_path(path, i), ignored_paths, changes);
            found = found || item_changed;
            if constexpr (!Collect) {
                if (found) {
                    return true;
                }
            }
        }
        for (std::size_t i = common; i < std::max(a.size(), b.size()); ++i) {
            const auto item_path = index_path(path, i);
            if (ignored(item_path, ignored_paths)) {
                continue;
            }
            if constexpr (Collect) {
                if (i < a.size()) {
                    changes->push_back(change{item_path, value(element(a, i)), value{},
                                              change_kind::removed});
                } else {
                    changes->push_back(change{item_path, value{}, value(element(b, i)),
                                              change_kind::added});
                }
            }
            found = true;
            if constexpr (!Collect) {
                return true;
            }
        }
        return found;
    } else if constexpr (Traits::optional) {
        if (a && b) {
            return visit_difference<Collect>(*a, *b, path, ignored_paths, changes);
        }
        if (a.has_value() == b.has_value()) {
            return false;
        }
        if constexpr (Collect) {
            if (a) {
                changes->push_back(change{path, value(*a), value{}, change_kind::removed});
            } else {
                changes->push_back(change{path, value{}, value(*b), change_kind::added});
            }
        }
        return true;
    } else {
        if (a == b) {
            return false;
        }
        if constexpr (Collect) {
            changes->push_back(change{path, value(a), value(b)});
        }
        return true;
    }
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

} // namespace vardiff

#endif // VARDIFF_VARDIFF_HPP
