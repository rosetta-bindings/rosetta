// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED
//
// Bodies for Serie.h. Included from it; never on its own.
//
// Everything is `inline` because this is a header-only library — the manifest
// lists no `user_sources`, so each generated binding compiles these definitions
// into its own translation unit and the linker must be allowed to fold them.

#pragma once

#include "Serie.h"

namespace serie {

    // ---- construction ---------------------------------------------------

    inline Serie::Serie(std::vector<double> values, std::size_t itemSize)
        : values_(std::move(values)), itemSize_(itemSize ? itemSize : 1) {
        if (values_.size() % itemSize_ != 0) {
            throw std::invalid_argument("Serie: size is not a multiple of itemSize");
        }
    }

    inline Serie::Serie(std::initializer_list<double> values, std::size_t itemSize)
        : Serie(std::vector<double>(values), itemSize) {}

    // ---- shape ----------------------------------------------------------

    inline std::size_t Serie::itemSize() const { return itemSize_; }
    inline std::size_t Serie::count() const { return values_.size() / itemSize_; }
    inline std::size_t Serie::size() const { return values_.size(); }
    inline bool        Serie::empty() const { return values_.empty(); }

    // ---- access ---------------------------------------------------------

    inline std::vector<double> Serie::item(std::size_t i) const {
        if (i >= count()) {
            throw std::out_of_range("Serie::item: index out of range");
        }
        const auto first = values_.begin() + static_cast<std::ptrdiff_t>(i * itemSize_);
        return std::vector<double>(first, first + static_cast<std::ptrdiff_t>(itemSize_));
    }

    inline double Serie::scalar(std::size_t i) const {
        requireScalar("scalar");
        if (i >= values_.size()) {
            throw std::out_of_range("Serie::scalar: index out of range");
        }
        return values_[i];
    }

    inline const std::vector<double> &Serie::scalars() const {
        requireScalar("scalars");
        return values_;
    }

    inline const std::vector<double> &Serie::raw() const { return values_; }

    inline Serie &Serie::append(const Serie &other) {
        if (values_.empty()) {
            itemSize_ = other.itemSize_;
        } else if (other.itemSize_ != itemSize_) {
            throw std::invalid_argument("Serie::append: itemSize mismatch");
        }
        values_.insert(values_.end(), other.values_.begin(), other.values_.end());
        return *this;
    }

    inline std::string Serie::describe() const {
        return std::to_string(count()) + " items of " + std::to_string(itemSize_);
    }

    // ---- callbacks ------------------------------------------------------

    inline void Serie::forEach(const std::function<void(double)> &f) const {
        requireScalar("forEach");
        for (const double v : values_) {
            f(v);
        }
    }

    inline Serie Serie::map(const std::function<double(double)> &f) const {
        requireScalar("map");
        std::vector<double> out;
        out.reserve(values_.size());
        for (const double v : values_) {
            out.push_back(f(v));
        }
        return Serie(std::move(out), 1);
    }

    inline double Serie::reduce(const std::function<double(double, double)> &f, double init) const {
        requireScalar("reduce");
        double acc = init;
        for (const double v : values_) {
            acc = f(acc, v);
        }
        return acc;
    }

    // ---- private --------------------------------------------------------

    inline void Serie::requireScalar(const char *method) const {
        if (itemSize_ != 1) {
            throw std::invalid_argument(std::string("Serie::") + method +
                                        ": requires itemSize == 1");
        }
    }

    // ---- free functions -------------------------------------------------

    inline Serie weightedSum(const std::vector<Serie> &series, const std::vector<double> &alpha) {
        if (series.size() != alpha.size()) {
            throw std::invalid_argument("weightedSum: one alpha per serie");
        }
        if (series.empty()) {
            return Serie{};
        }
        const std::size_t is = series.front().itemSize();
        std::size_t       n  = 1;
        for (const Serie &s : series) {
            if (s.itemSize() != is) {
                throw std::invalid_argument("weightedSum: itemSize mismatch");
            }
            if (s.count() != 1) {
                if (n != 1 && s.count() != n) {
                    throw std::invalid_argument("weightedSum: count mismatch");
                }
                n = s.count();
            }
        }
        std::vector<double> out(n * is, 0.0);
        for (std::size_t j = 0; j < series.size(); ++j) {
            const std::vector<double> &v = series[j].raw();
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t src = (series[j].count() == 1 ? 0 : i);
                for (std::size_t k = 0; k < is; ++k) {
                    out[i * is + k] += alpha[j] * v[src * is + k];
                }
            }
        }
        return Serie(std::move(out), is);
    }

} // namespace serie
