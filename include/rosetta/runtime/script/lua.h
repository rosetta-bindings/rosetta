// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED
//
// sol2 type caster for rosetta::script::Value — target `lua`.
// See <rosetta/runtime/script_casters.h> for what this is and how it gets in.

#pragma once

#include "../../script.h"

#include <string>
#include <vector>

#if defined(SOL_VERSION_STRING)

namespace rosetta::script::casters {

    /** @brief Value -> Lua stack. Returns the number of values pushed, which
     *  sol2's pusher protocol wants, and which is always 1. */
    struct lua_sink {
        lua_State *L;

        int on_none() {
            lua_pushnil(L);
            return 1;
        }
        int on_bool(bool b) {
            lua_pushboolean(L, b ? 1 : 0);
            return 1;
        }
        int on_int(long long i) {
            lua_pushinteger(L, static_cast<lua_Integer>(i));
            return 1;
        }
        int on_real(double d) {
            lua_pushnumber(L, static_cast<lua_Number>(d));
            return 1;
        }
        int on_string(const std::string &s) {
            lua_pushlstring(L, s.data(), s.size());
            return 1;
        }
        int on_enum(long long i, const TypeInfo &) { return on_int(i); }

        int on_list(const std::vector<Value> &xs) {
            lua_createtable(L, static_cast<int>(xs.size()), 0);
            int i = 1;
            for (const Value &e : xs) {
                visit(e, *this);
                lua_rawseti(L, -2, i++);
            }
            return 1;
        }

        int on_object(const Instance &o) { return sol::stack::push(L, o); }
        int on_unknown(const std::string &s) { return on_string(s); }
    };

} // namespace rosetta::script::casters

namespace sol {

    template <> struct lua_size<rosetta::script::Value> : std::integral_constant<int, 1> {};

    /** @brief type::poly — "any Lua type", which is what a Value accepts. */
    template <>
    struct lua_type_of<rosetta::script::Value> : std::integral_constant<type, type::poly> {};

    namespace stack {

        template <> struct unqualified_checker<rosetta::script::Value, type::poly, void> {
            template <typename Handler>
            static bool check(lua_State *, int, Handler &&, record &tracking) {
                tracking.use(1);
                return true; // every Lua value has a Value spelling
            }
        };

        template <> struct unqualified_getter<rosetta::script::Value> {
            static rosetta::script::Value get(lua_State *L, int index, record &tracking) {
                tracking.use(1);
                return convert(L, lua_absindex(L, index));
            }

            /** @brief Lua -> Value: the irreducibly per-language half. */
            static rosetta::script::Value convert(lua_State *L, int idx) {
                using V = rosetta::script::Value;
                switch (lua_type(L, idx)) {
                case LUA_TNONE:
                case LUA_TNIL:
                    return V::none();
                case LUA_TBOOLEAN:
                    return V::boolean(lua_toboolean(L, idx) != 0);
                case LUA_TNUMBER:
#if LUA_VERSION_NUM >= 503
                    if (lua_isinteger(L, idx)) {
                        return V::integer(static_cast<long long>(lua_tointeger(L, idx)));
                    }
#endif
                    return V::number(static_cast<double>(lua_tonumber(L, idx)));
                case LUA_TSTRING: {
                    std::size_t n = 0;
                    const char *s = lua_tolstring(L, idx, &n);
                    return V::text(std::string(s, n));
                }
                case LUA_TTABLE: {
                    // The sequence part only — a Lua table with string keys is
                    // not a list and has no Value spelling.
                    const int      n = static_cast<int>(lua_rawlen(L, idx));
                    std::vector<V> items;
                    items.reserve(static_cast<std::size_t>(n));
                    for (int i = 1; i <= n; ++i) {
                        lua_rawgeti(L, idx, i);
                        items.push_back(convert(L, lua_gettop(L)));
                        lua_pop(L, 1);
                    }
                    return V::list(items);
                }
                case LUA_TUSERDATA:
                    if (stack::check_usertype<rosetta::script::Instance>(L, idx)) {
                        return V::from(stack::get<rosetta::script::Instance>(L, idx));
                    }
                    break;
                default:
                    break;
                }
                return V::none();
            }
        };

        template <> struct unqualified_pusher<rosetta::script::Value> {
            static int push(lua_State *L, const rosetta::script::Value &v) {
                return rosetta::script::visit(v, rosetta::script::casters::lua_sink{L});
            }
        };

    } // namespace stack
} // namespace sol

#endif // sol2
