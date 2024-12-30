#pragma once

#include <array>
#include <type_traits>
#include <utility>

#define fwd(x) static_cast<decltype(x) &&>(x)

namespace erased {

struct erased_type_t {};

template <bool condition> struct fast_conditional {
  template <typename T, typename F> using apply = T;
};

template <> struct fast_conditional<false> {
  template <typename T, typename F> using apply = F;
};

template <typename Method, typename Signature> struct MethodTraitImpl;

template <typename Method, typename ReturnType, typename ErasedType,
          typename... Args>
struct MethodTraitImpl<Method, ReturnType (*)(ErasedType, Args...)> {
  using function =
      fast_conditional<std::is_const_v<ErasedType>>::template apply<
          ReturnType(Args...) const, ReturnType(Args...)>;
};

template <typename Method>
using MethodTrait =
    MethodTraitImpl<Method,
                    decltype(&Method::template invoker<erased::erased_type_t>)>;

template <typename Method>
using MethodPtr = typename MethodTrait<Method>::function;

template <typename Method, typename F> struct Signature;

template <typename Method>
using CreateSignature = Signature<Method, MethodPtr<Method>>;

struct Copy {
  static void invoker(const auto &) {}
};
struct Move {
  static void invoker(const auto &) {}
};

namespace details::base {
template <typename...> struct base_with_methods;

template <> struct base_with_methods<> {
  constexpr void invoker() {}
  constexpr virtual ~base_with_methods() = default;
};

template <typename Method, typename R, typename... Args, typename... Others>
struct base_with_methods<Signature<Method, R(Args...) const>, Others...>
    : base_with_methods<Others...> {
  using base_with_methods<Others...>::invoker;
  constexpr virtual R invoker(Method, Args...) const = 0;
};

template <typename Method, typename R, typename... Args, typename... Others>
struct base_with_methods<Signature<Method, R(Args...)>, Others...>
    : base_with_methods<Others...> {
  using base_with_methods<Others...>::invoker;
  constexpr virtual R invoker(Method, Args...) = 0;
};

template <typename... Signatures>
struct base : public base_with_methods<Signatures...> {
  using base_with_methods<Signatures...>::invoker;
  constexpr virtual base *clone(bool isDynamic, std::byte *buffer) const = 0;
  constexpr virtual base *move(bool isDynamic, std::byte *buffer) noexcept = 0;
};

} // namespace details::base

namespace details::concrete {
template <typename Base, typename Type, typename... Ts> struct concrete_method;

template <typename Base, typename Type>
struct concrete_method<Base, Type> : Base {
  Type m_object;

  constexpr concrete_method(auto &&...args) : m_object{fwd(args)...} {}
};

template <typename Base, typename Type, typename Method, typename R,
          typename... Args, typename... Others>
struct concrete_method<Base, Type, Signature<Method, R(Args...) const>,
                       Others...> : concrete_method<Base, Type, Others...> {
  using concrete_method<Base, Type, Others...>::concrete_method;
  constexpr R invoker(Method, Args... args) const override {
    return Method::invoker(this->m_object, fwd(args)...);
  }
};

template <typename Base, typename Type, typename Method, typename R,
          typename... Args, typename... Others>
struct concrete_method<Base, Type, Signature<Method, R(Args...)>, Others...>
    : concrete_method<Base, Type, Others...> {
  using concrete_method<Base, Type, Others...>::concrete_method;
  constexpr R invoker(Method, Args... args) override {
    return Method::invoker(this->m_object, fwd(args)...);
  }
};

template <typename Base, typename Type, bool Movable, bool Copyable,
          typename... Signatures>
struct concrete : concrete_method<Base, Type, Signatures...> {
  using concrete_method<Base, Type, Signatures...>::concrete_method;

  constexpr virtual Base *clone(bool is_dynamic,
                                std::byte *soo_buffer) const override {
    if constexpr (Copyable) {
      if (is_dynamic)
        return new concrete{this->m_object};
      return new (soo_buffer) concrete{this->m_object};
    } else {
      return nullptr;
    }
  }

  constexpr virtual Base *move(bool is_dynamic,
                               std::byte *soo_buffer) noexcept override {
    if constexpr (Movable) {
      if (is_dynamic)
        return new concrete{std::move(this->m_object)};
      return new (soo_buffer) concrete{std::move(this->m_object)};
    } else {
      return nullptr;
    }
  }
};

} // namespace details::concrete
static_assert(sizeof(bool) == 1);

template <typename T, int N> constexpr bool is_dynamic() {
  if constexpr (sizeof(T) <= N)
    return std::is_constant_evaluated();
  else
    return true;
}

template <typename T, std::size_t N>
constexpr auto *construct(std::array<std::byte, N> &array, auto &&...args) {
  if constexpr (sizeof(T) <= N) {
    if (std::is_constant_evaluated())
      return new T{fwd(args)...};
    return new (array.data()) T{fwd(args)...};
  } else {
    return new T{fwd(args)...};
  }
}

template <typename T, typename... List> constexpr bool contains() {
  return (std::is_same_v<T, List> || ...);
}

template <int Size, typename... Methods>
struct alignas(Size) basic_erased : public Methods... {
  using Base = details::base::base<CreateSignature<Methods>...>;

  static constexpr auto buffer_size = Size - sizeof(bool) - sizeof(Base *);

  static constexpr bool copyable = contains<Copy, Methods...>();
  static constexpr bool movable = contains<Move, Methods...>();

  std::array<std::byte, buffer_size> m_array;
  bool m_dynamic;
  Base *m_ptr;

  template <typename T>
  constexpr basic_erased(std::in_place_type_t<T>, auto &&...args) noexcept
      : m_dynamic{is_dynamic<
            details::concrete::concrete<Base, T, movable, copyable,
                                        CreateSignature<Methods>...>,
            buffer_size>()},
        m_ptr{
            construct<details::concrete::concrete<Base, T, movable, copyable,
                                                  CreateSignature<Methods>...>>(
                m_array, fwd(args)...)} {}

  template <typename T>
  constexpr basic_erased(T x) noexcept
      : basic_erased{std::in_place_type<T>, std::move(x)} {}

  constexpr decltype(auto) invoke(auto method, auto &&...xs) const {
    return m_ptr->invoker(method, fwd(xs)...);
  }

  constexpr decltype(auto) invoke(auto method, auto &&...xs) {
    return m_ptr->invoker(method, fwd(xs)...);
  }

  constexpr basic_erased(basic_erased &&other) noexcept
    requires movable
      : m_dynamic(other.m_dynamic),
        m_ptr{other.m_ptr->move(m_dynamic, m_array.data())} {}

  constexpr basic_erased(const basic_erased &other)
    requires copyable
      : m_dynamic(other.m_dynamic),
        m_ptr{other.m_ptr->clone(m_dynamic, m_array.data())} {}

  constexpr basic_erased &operator=(basic_erased &&other) noexcept
    requires movable
  {
    destroy();
    m_dynamic = other.m_dynamic;
    m_ptr = other.m_ptr->move(m_dynamic, m_array.data());
    return *this;
  }

  constexpr basic_erased &operator=(const basic_erased &other)
    requires copyable
  {
    destroy();
    m_dynamic = other.m_dynamic;
    m_ptr = other.m_ptr->clone(m_dynamic, m_array.data());
    return *this;
  }
  constexpr void destroy() {
    if (m_dynamic)
      delete m_ptr;
    else
      m_ptr->~Base();
  }

  constexpr ~basic_erased() { destroy(); }
};

template <typename... Methods> using erased = basic_erased<32, Methods...>;
} // namespace erased

#undef fwd
