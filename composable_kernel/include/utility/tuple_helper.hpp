#ifndef CK_TUPLE_HELPER_HPP
#define CK_TUPLE_HELPER_HPP

#include "tuple_helper.hpp"

namespace ck {

template <typename... Xs>
__host__ __device__ constexpr auto make_tuple(Xs&&... xs)
{
    return Tuple<remove_cv_t<remove_reference_t<Xs>>...>(std::forward<Xs>(xs)...);
}

template <typename F, index_t N>
__host__ __device__ constexpr auto generate_tuple(F&& f, Number<N>)
{
    return unpack([&f](auto&&... xs) { return make_tuple(f(xs)...); },
                  typename arithmetic_sequence_gen<0, N, 1>::type{});
}

template <typename... Tuples>
__host__ __device__ constexpr auto merge_tuples(Tuples... tuples)
{
    return unpack([&tuples...](auto... xs) { return make_tuple(xs...); }, tuples...);
}

namespace detail {

template <typename F, typename X, index_t... Is>
__host__ __device__ constexpr auto transform_tuples_impl(F f, const X& x, Sequence<Is...>)
{
    return make_tuple(f(x.At(Number<Is>{}))...);
}

template <typename F, typename X, typename Y, index_t... Is>
__host__ __device__ constexpr auto
transform_tuples_impl(F f, const X& x, const Y& y, Sequence<Is...>)
{
    return make_tuple(f(x.At(Number<Is>{}), y.At(Number<Is>{}))...);
}

template <typename F, typename X, typename Y, typename Z, index_t... Is>
__host__ __device__ constexpr auto
transform_tuples_impl(F f, const X& x, const Y& y, const Z& z, Sequence<Is...>)
{
    return make_tuple(f(x.At(Number<Is>{}), y.At(Number<Is>{}), z.At(Number<Is>{}))...);
}

} // namespace detail

template <typename F, typename X>
__host__ __device__ constexpr auto transform_tuples(F f, const X& x)
{
    return detail::transform_tuples_impl(
        f, x, typename arithmetic_sequence_gen<0, X::Size(), 1>::type{});
}

template <typename F, typename X, typename Y>
__host__ __device__ constexpr auto transform_tuples(F f, const X& x, const Y& y)
{
    return detail::transform_tuples_impl(
        f, x, y, typename arithmetic_sequence_gen<0, X::Size(), 1>::type{});
}

template <typename F, typename X, typename Y, typename Z>
__host__ __device__ constexpr auto transform_tuples(F f, const X& x, const Y& y, const Z& z)
{
    return detail::transform_tuples_impl(
        f, x, y, z, typename arithmetic_sequence_gen<0, X::Size(), 1>::type{});
}

} // namespace ck
#endif