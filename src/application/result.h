// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace hinkwise::application {

/// The outcome of an operation that can fail for a reason the user has to read:
/// a database that will not open, a backup from a newer version, a row whose
/// text no longer matches the domain's enums.
///
/// Nothing in the infrastructure layer lets an exception out. Every failure it
/// can report becomes a Result error carrying a message fit for a dialog, and
/// the application layer decides what to do about it. There is no error code
/// enum, because the only consumer of the message is a human.
template <typename T> class Result {
public:
    static Result ok(T value) { return Result{std::move(value), std::string{}}; }

    static Result err(std::string message) { return Result{std::nullopt, std::move(message)}; }

    bool ok() const noexcept { return m_value.has_value(); }

    /// Precondition: ok(). Reading the value of a failed Result is a
    /// programming mistake rather than a runtime condition, so it throws
    /// instead of inventing a value that would hide the bug.
    const T& value() const
    {
        requireValue();
        return *m_value;
    }

    T& value()
    {
        requireValue();
        return *m_value;
    }

    /// Empty when ok().
    const std::string& error() const noexcept { return m_error; }

private:
    Result(std::optional<T> value, std::string error)
        : m_value(std::move(value))
        , m_error(std::move(error))
    {
    }

    void requireValue() const
    {
        if (!m_value)
            throw std::logic_error{"Result::value() called on an error: " + m_error};
    }

    std::optional<T> m_value;
    std::string m_error;
};

/// A Result for operations that produce nothing: saving a row, deleting one,
/// appending to the phase history. `ok()` is true exactly when there is no
/// message.
///
/// The successful value is built with `success()` rather than `ok()`, because a
/// class cannot have a static and a member function with the same signature.
class Status {
public:
    static Status success() { return Status{std::string{}}; }

    static Status err(std::string message) { return Status{std::move(message)}; }

    bool ok() const noexcept { return m_error.empty(); }

    /// Empty when ok().
    const std::string& error() const noexcept { return m_error; }

private:
    explicit Status(std::string error)
        : m_error(std::move(error))
    {
    }

    std::string m_error;
};

} // namespace hinkwise::application
