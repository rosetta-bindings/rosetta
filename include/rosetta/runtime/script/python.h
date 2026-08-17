// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED
//
// pybind11 type caster for rosetta::script::Value — target `python`.
// See <rosetta/runtime/script_casters.h> for what this is and how it gets in.

#pragma once

#include "../../script.h"

#include <string>
#include <vector>

#if defined(PYBIND11_VERSION_MAJOR)

#include <pybind11/pybind11.h>

namespace rosetta::script::casters {

    /** @brief Value -> Python: the sink half of visit(). */
    struct py_sink {
        pybind11::return_value_policy policy;
        pybind11::handle              parent;

        pybind11::handle on_none() { return pybind11::none().release(); }
        pybind11::handle on_bool(bool b) { return pybind11::bool_(b).release(); }
        pybind11::handle on_int(long long i) { return pybind11::int_(i).release(); }
        pybind11::handle on_real(double d) { return pybind11::float_(d).release(); }
        pybind11::handle on_string(const std::string &s) { return pybind11::str(s).release(); }

        /** @brief An enumerator crosses as its integer, matching Any's canonical
         *  representation. A UI maps it back through
         *  TypeInfo::enumerator_names(); returning the NAME here would read
         *  better but would not round-trip through set(). */
        pybind11::handle on_enum(long long i, const TypeInfo &) {
            return pybind11::int_(i).release();
        }

        pybind11::handle on_list(const std::vector<Value> &xs) {
            pybind11::list out;
            for (const Value &e : xs) {
                out.append(pybind11::reinterpret_steal<pybind11::object>(visit(e, *this)));
            }
            return out.release();
        }

        pybind11::handle on_object(const Instance &o) { return pybind11::cast(o).release(); }
        pybind11::handle on_unknown(const std::string &s) { return pybind11::str(s).release(); }
    };

} // namespace rosetta::script::casters

namespace pybind11 {
    namespace detail {

        template <> struct type_caster<rosetta::script::Value> {
            using V = rosetta::script::Value;
            PYBIND11_TYPE_CASTER(V, const_name("Value"));

            /** @brief Python -> Value: the irreducibly per-language half.
             *  Ordered by how often each arrives; bool BEFORE int, since
             *  PyLong_Check accepts a bool. */
            bool load(handle src, bool convert) {
                if (!src) {
                    return false;
                }
                if (src.is_none()) {
                    value = V::none();
                    return true;
                }
                if (PyBool_Check(src.ptr())) {
                    value = V::boolean(src.cast<bool>());
                    return true;
                }
                if (PyLong_Check(src.ptr())) {
                    value = V::integer(src.cast<long long>());
                    return true;
                }
                if (PyFloat_Check(src.ptr())) {
                    value = V::number(src.cast<double>());
                    return true;
                }
                if (PyUnicode_Check(src.ptr())) {
                    value = V::text(src.cast<std::string>());
                    return true;
                }
                if (isinstance<rosetta::script::Instance>(src)) {
                    value = V::from(src.cast<rosetta::script::Instance>());
                    return true;
                }
                if (isinstance<list>(src) || isinstance<tuple>(src)) {
                    std::vector<V> items;
                    for (handle h : reinterpret_borrow<sequence>(src)) {
                        type_caster<V> elem;
                        if (!elem.load(h, convert)) {
                            return false;
                        }
                        items.push_back(elem.value);
                    }
                    value = V::list(items);
                    return true;
                }
                // A real py::class_<Value> instance — only Value() makes one,
                // but it must not become a TypeError.
                if (isinstance<V>(src)) {
                    type_caster_base<V> base;
                    if (base.load(src, false)) {
                        value = static_cast<V &>(base);
                        return true;
                    }
                }
                return false;
            }

            static handle cast(const V &v, return_value_policy policy, handle parent) {
                return rosetta::script::visit(v,
                                              rosetta::script::casters::py_sink{policy, parent});
            }
        };

    } // namespace detail
} // namespace pybind11

#endif // pybind11
