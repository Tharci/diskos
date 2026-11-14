// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <concepts>
#include <mutex>
#include <type_traits>
#include <utility>

namespace diskos::utils {

/**
 * A value shared between threads, with the mutex that guards it: the value is only reachable
 * through with(), which holds the mutex while it runs.
 */
template <typename T>
class Synchronized {
public:
    /**
     * Call `access` with the value, holding the mutex until it returns, and return what it
     * returns. It returns by value, so no reference to the value leaves the lock; `access` must
     * not keep one either.
     */
    template <std::invocable<T&> Access>
        requires(!std::is_reference_v<std::invoke_result_t<Access, T&>>)
    auto with(Access&& access) {
        const std::scoped_lock lock{mutex_};
        return std::forward<Access>(access)(value_);
    }

private:
    std::mutex mutex_;
    T value_{};
};

}  // namespace diskos::utils
