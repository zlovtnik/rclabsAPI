#pragma once

#include "template_utils.hpp"
#include "type_definitions.hpp"
#include <boost/hana.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace etl {
namespace hana_utils {

// Concept-like check for cloneable types
template <typename T> struct has_clone_method {
private:
  template <typename U>
  static auto test(int)
      -> decltype(std::declval<U>().clone(), std::true_type{});

  template <typename> static std::false_type test(...);

public:
  static constexpr bool value = decltype(test<T>(0))::value;
};

// Static assertion helper
template <typename T> void assert_cloneable() {
  static_assert(
      has_clone_method<T>::value,
      "Type must implement clone() method returning a compatible pointer type");
}

// ============================================================================
// Compile-time string utilities using Hana
// ============================================================================

// Compile-time string type
template <char... chars> struct ct_string {
  static constexpr char const value[sizeof...(chars) + 1] = {chars..., '\0'};
  static constexpr std::size_t size = sizeof...(chars);
};

// Note: Custom string literal operators are not used to avoid compiler warnings

// ============================================================================
// Type-safe configuration system using Hana
// ============================================================================

// Configuration key type
template <typename Tag> struct ConfigKey {
  using tag_type = Tag;
  static constexpr const char *name = Tag::name;
};

// Configuration value types
using ConfigValueTypes =
    boost::hana::tuple<boost::hana::type<int>, boost::hana::type<double>,
                       boost::hana::type<std::string>, boost::hana::type<bool>>;

// ============================================================================
// Hana-based visitor pattern for variant types
// ============================================================================

// Generic visitor that can work with any variant containing our strong ID types
template <typename Visitor, typename... Types>
auto visit_strong_ids(Visitor &&visitor, boost::hana::tuple<Types...>) {
  return boost::hana::unpack(
      boost::hana::tuple_t<Types...>, [&](auto... type_tags) {
        return std::forward<Visitor>(visitor)(type_tags...);
      });
}

// ============================================================================
// Compile-time type checking and validation
// ============================================================================

// Check if all types in a tuple satisfy a predicate
template <typename Tuple, typename Predicate>
constexpr auto all_satisfy(Tuple tuple, Predicate pred) {
  return boost::hana::all_of(tuple, pred);
}

// Check if any type in a tuple satisfies a predicate
template <typename Tuple, typename Predicate>
constexpr auto any_satisfy(Tuple tuple, Predicate pred) {
  return boost::hana::any_of(tuple, pred);
}

// ============================================================================
// Hana-based serialization utilities
// ============================================================================

// Generic to_string for strong IDs
template <typename Tag> std::string to_string(const StrongId<Tag> &id) {
  return std::to_string(id.value());
}

// Compile-time type name generation
template <typename T> constexpr auto type_name() {
  return boost::hana::type_c<T>;
}

// ============================================================================
// Advanced Hana metaprogramming examples
// ============================================================================

// Simple type filtering using Hana
template <typename... Types>
constexpr auto
count_strong_ids(boost::hana::tuple<boost::hana::type<Types>...>) {
  return boost::hana::size(boost::hana::filter(
      boost::hana::make_tuple(boost::hana::type_c<Types>...), [](auto type) {
        using T = typename decltype(type)::type;
        return boost::hana::bool_c<etl::template_utils::is_strong_id_v<T>>;
      }));
}

// ============================================================================
// Runtime type dispatch using Hana
// ============================================================================

template <typename... Handlers> class TypeDispatcher {
private:
  boost::hana::tuple<Handlers...> handlers_;

public:
  explicit TypeDispatcher(Handlers... handlers)
      : handlers_(std::move(handlers)...) {}

  template <typename T> void dispatch(const T &value) {
    boost::hana::for_each(handlers_, [&](auto &handler) {
      using HandlerType = std::decay_t<decltype(handler)>;
      if constexpr (std::is_invocable_v<HandlerType, T>) {
        handler(value);
      }
    });
  }
};

// ============================================================================
// Hana-based factory pattern
// ============================================================================

template <typename Base, typename... Derived> class HanaFactory {
private:
  boost::hana::tuple<std::unique_ptr<Derived>...> prototypes_;

public:
  template <typename... Args>
  explicit HanaFactory(Args&&... prototypes)
      : prototypes_(std::forward<Args>(prototypes)...) {
    // Compile-time check that all prototype types implement clone()
    (assert_cloneable<Derived>(), ...);
  }

  template <typename T> std::unique_ptr<Base> create() const {
    // Simple runtime lookup for the prototype
    std::unique_ptr<Base> result;

    auto found = boost::hana::find_if(prototypes_, [](const auto &proto) {
      using ProtoType = std::decay_t<decltype(*proto.get())>;
      return std::is_same_v<ProtoType, T>;
    });

    if (found != boost::hana::nothing) {
      result = (*found)->clone();
    }

    return result;
  }
};

} // namespace hana_utils
} // namespace etl
