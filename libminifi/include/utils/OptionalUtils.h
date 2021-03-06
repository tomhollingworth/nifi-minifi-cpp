/**
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LIBMINIFI_INCLUDE_UTILS_OPTIONALUTILS_H_
#define LIBMINIFI_INCLUDE_UTILS_OPTIONALUTILS_H_

#include <type_traits>
#include <utility>

#include <nonstd/optional.hpp>
#include "utils/GeneralUtils.h"
#include "utils/gsl.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace utils {
using nonstd::optional;
using nonstd::nullopt;
using nonstd::make_optional;

template<typename T>
optional<typename gsl_lite::remove_cvref<T>::type> optional_from_ptr(T&& obj) {
  return obj == nullptr ? nullopt : optional<typename gsl_lite::remove_cvref<T>::type>{ std::forward<T>(obj) };
}

template<typename>
struct is_optional : std::false_type {};

template<typename T>
struct is_optional<optional<T>> : std::true_type {};

namespace detail {
template<typename T>
struct map_wrapper {
  T function;
};

// map implementation
template<typename SourceType, typename F>
auto operator|(const optional<SourceType>& o, map_wrapper<F> f) noexcept(noexcept(utils::invoke(std::forward<F>(f.function), *o)))
    -> optional<typename std::decay<decltype(utils::invoke(std::forward<F>(f.function), *o))>::type> {
  if (o.has_value()) {
    return make_optional(utils::invoke(std::forward<F>(f.function), *o));
  } else {
    return nullopt;
  }
}

template<typename T>
struct flat_map_wrapper {
  T function;
};

// flatMap implementation
template<typename SourceType, typename F>
auto operator|(const optional<SourceType>& o, flat_map_wrapper<F> f) noexcept(noexcept(utils::invoke(std::forward<F>(f.function), *o)))
    -> optional<typename std::decay<decltype(*utils::invoke(std::forward<F>(f.function), *o))>::type> {
  static_assert(is_optional<decltype(utils::invoke(std::forward<F>(f.function), *o))>::value, "flatMap expects a function returning optional");
  if (o.has_value()) {
    return utils::invoke(std::forward<F>(f.function), *o);
  } else {
    return nullopt;
  }
}

}  // namespace detail

template<typename T>
detail::map_wrapper<T&&> map(T&& func) noexcept { return {std::forward<T>(func)}; }

template<typename T>
detail::flat_map_wrapper<T&&> flatMap(T&& func) noexcept { return {std::forward<T>(func)}; }

}  // namespace utils
}  // namespace minifi
}  // namespace nifi
}  // namespace apache
}  // namespace org

#endif  // LIBMINIFI_INCLUDE_UTILS_OPTIONALUTILS_H_

