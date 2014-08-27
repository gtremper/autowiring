// Copyright (C) 2012-2014 Leap Motion, Inc. All rights reserved.
#pragma once

/// <summary>
/// Utility type which enables the composition of a sequence [0, sizeof...(Ts))
template<int ...>
struct index_tuple {};

template<unsigned int N, unsigned int... S>
struct make_index_tuple: make_index_tuple<N - 1, N - 1, S...> {};

template<unsigned int... S>
struct make_index_tuple<0, S...> {
  typedef index_tuple<S...> type;
};

static_assert(
  std::is_same<
    make_index_tuple<3>::type,
    index_tuple<0,1,2>
  >::value,
  "Index tuple base case was not correctly specialized"
);
