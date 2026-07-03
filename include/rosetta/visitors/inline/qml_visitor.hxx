// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <experimental/meta>
#include <rosetta/walk.h>
#include <string>
#include <type_traits>
#include <utility>

namespace rosetta {

    namespace qml_detail {

        // A type that can round-trip through QVariant in generated code. The
        // hard compile-breakers are raw C arrays (e.g. a method returning
        // `double (&)[3][3]`) and class types incomplete in this TU. Everything
        // else goes through QVariant as before; a pointer stays usable even to
        // an incomplete type.
        // `sizeof(T*)` is always valid, so a pointer is judged by its pointee
        // (incomplete pointee ⇒ not bindable); a std::vector recurses into its
        // element type so a vector of incomplete-pointee is rejected too.
        template <class P>
        concept pointee_ok =
            std::is_void_v<std::remove_cv_t<std::remove_pointer_t<P>>> ||
            requires { sizeof(std::remove_pointer_t<P>); };

        template <class T> struct vector_elem {
            static constexpr bool is = false;
            using type               = void;
        };
        template <class E, class A> struct vector_elem<std::vector<E, A>> {
            static constexpr bool is = true;
            using type               = E;
        };

        template <class T> consteval bool is_bindable() {
            using U = std::remove_cvref_t<T>;
            if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
                return false;
            } else if constexpr (std::is_pointer_v<U>) {
                return pointee_ok<U>;
            } else if constexpr (vector_elem<U>::is) {
                return is_bindable<typename vector_elem<U>::type>();
            } else {
                return std::is_arithmetic_v<U> || std::is_enum_v<U> || std::is_void_v<U> ||
                       requires { sizeof(U); };
            }
        }

        template <class T>
        concept bindable_type = is_bindable<T>();

        template <std::meta::info Fn, std::size_t... Is>
        consteval bool params_bindable(std::index_sequence<Is...>) {
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            return (bindable_type<typename[:std::meta::type_of(params[Is]):]> && ...);
        }

        template <std::meta::info Fn> consteval bool bindable_fn() {
            constexpr auto arity = std::meta::parameters_of(Fn).size();
            return bindable_type<typename[:std::meta::return_type_of(Fn):]> &&
                   params_bindable<Fn>(std::make_index_sequence<arity>{});
        }

        // Bridge std::string <-> QString; everything else round-trips via QVariant.
        template <typename F> QVariant to_variant(const F &v) {
            if constexpr (std::is_same_v<F, std::string>) {
                return QString::fromStdString(v);
            } else {
                return QVariant::fromValue(v);
            }
        }

        template <typename F> F from_variant(const QVariant &v) {
            if constexpr (std::is_same_v<F, std::string>) {
                return v.toString().toStdString();
            } else {
                return v.value<F>();
            }
        }

        // Coarse type tag for the QML side — enough for it to pick an editor.
        template <typename F> QString type_tag() {
            if constexpr (std::is_same_v<F, std::string>)
                return QStringLiteral("string");
            else if constexpr (std::is_same_v<F, bool>)
                return QStringLiteral("bool");
            else if constexpr (std::is_integral_v<F>)
                return QStringLiteral("int");
            else if constexpr (std::is_floating_point_v<F>)
                return QStringLiteral("double");
            else {
                return QStringLiteral("other");
            }
        }

        template <std::meta::info Fn, typename T, std::size_t... Is>
        QVariant invoke_method_impl(T &self, const QVariantList &args,
                                    std::index_sequence<Is...>) {
            using R               = [:std::meta::return_type_of(Fn):];
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            if constexpr (!bindable_fn<Fn>()) {
                // Return or a parameter type can't cross the QVariant boundary
                // (raw array / incomplete type) — keep the method inert.
                return QVariant();
            } else if constexpr (std::is_void_v<R>) {
                (self.[:Fn:])(
                    from_variant<
                        std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                        args[Is])...);
                return {};
            } else {
                R r = (self.[:Fn:])(
                    from_variant<
                        std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                        args[Is])...);
                return to_variant<R>(r);
            }
        }

        template <std::meta::info Fn, std::size_t... Is>
        QVariant invoke_static_impl(const QVariantList &args, std::index_sequence<Is...>) {
            using R               = [:std::meta::return_type_of(Fn):];
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            if constexpr (!bindable_fn<Fn>()) {
                // Return or a parameter type can't cross the QVariant boundary
                // (raw array / incomplete type) — keep the method inert.
                return QVariant();
            } else if constexpr (std::is_void_v<R>) {
                ([:Fn:])(
                    from_variant<
                        std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                        args[Is])...);
                return {};
            } else {
                R r = ([:Fn:])(
                    from_variant<
                        std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                        args[Is])...);
                return to_variant<R>(r);
            }
        }

    } // namespace qml_detail

    template <typename T>
    template <std::meta::info Fld, auto... Anns>
    inline void QmlVisitor<T>::field(const char *name) {
        using F                 = [:std::meta::type_of(Fld):];
        constexpr auto dann     = ann::get_or<doc>(doc{""}, Anns...);
        constexpr auto lbl      = ann::get_or<label>(label{""}, Anns...);
        const QString  qname    = QString::fromUtf8(name);
        const QString  qdisplay = (lbl.text[0] != '\0')
                                      ? QString::fromUtf8(lbl.text)
                                      : qname;

        QVariantMap info;
        info[QStringLiteral("name")]        = qname;
        info[QStringLiteral("displayName")] = qdisplay;
        info[QStringLiteral("type")]     = qml_detail::type_tag<F>();
        info[QStringLiteral("doc")]      = QString::fromUtf8(dann.text);
        info[QStringLiteral("readonly")] = ann::has<readonly>(Anns...);
        info[QStringLiteral("hasRange")] = ann::has<range>(Anns...);
        if constexpr (ann::has<range>(Anns...)) {
            constexpr auto r = ann::get_or<range>(range{0, 0}, Anns...);
            info[QStringLiteral("min")] = r.min;
            info[QStringLiteral("max")] = r.max;
        } else {
            info[QStringLiteral("min")] = 0.0;
            info[QStringLiteral("max")] = 0.0;
        }
        if constexpr (ann::has<widget::slider_tag>(Anns...)) {
            info[QStringLiteral("widget")] = QStringLiteral("slider");
        } else if constexpr (ann::has<widget::checkbox_tag>(Anns...)) {
            info[QStringLiteral("widget")] = QStringLiteral("checkbox");
        } else if constexpr (ann::has<widget::textfield_tag>(Anns...)) {
            info[QStringLiteral("widget")] = QStringLiteral("textfield");
        } else if constexpr (ann::has<widget::spin_tag>(Anns...)) {
            info[QStringLiteral("widget")] = QStringLiteral("spin");
        } else {
            info[QStringLiteral("widget")] = QString{};
        }
        info[QStringLiteral("hasChoices")] = ann::has<combobox>(Anns...);
        if constexpr (ann::has<combobox>(Anns...)) {
            constexpr auto cb = ann::get_or<combobox>(combobox{}, Anns...);
            QVariantList   choices;
            for (std::size_t i = 0; i < cb.count; ++i) {
                choices.append(QString::fromUtf8(cb.choices[i]));
            }
            info[QStringLiteral("choices")] = choices;
        } else {
            info[QStringLiteral("choices")] = QVariantList{};
        }
        if constexpr (qml_detail::bindable_type<F>) {
            info[QStringLiteral("value")] = qml_detail::to_variant<F>(target->[:Fld:]);
        } else {
            // raw-array field type can't cross the QVariant boundary
            info[QStringLiteral("value")] = QVariant();
        }
        obj->appendField(info);

        // The field is listed above either way; only register a getter/setter
        // when its type can round-trip through QVariant.
        if constexpr (qml_detail::bindable_type<F>) {
        T *tgt = target;
        obj->registerGetter(qname, [tgt]() -> QVariant {
            return qml_detail::to_variant<F>(tgt->[:Fld:]);
        });

        if constexpr (ann::has<readonly>(Anns...)) {
            obj->registerSetter(qname, [qname](const QVariant &) -> QString {
                return qname + QStringLiteral(" is read-only");
            });
        } else if constexpr (ann::has<combobox>(Anns...)) {
            constexpr auto cb = ann::get_or<combobox>(combobox{}, Anns...);
            obj->registerSetter(qname, [tgt, qname, cb](const QVariant &v) -> QString {
                const QString s = v.toString();
                bool          ok = false;
                for (std::size_t i = 0; i < cb.count; ++i) {
                    if (s == QString::fromUtf8(cb.choices[i])) {
                        ok = true;
                        break;
                    }
                }
                if (!ok) {
                    return qname + QStringLiteral(" not in allowed choices");
                }
                tgt->[:Fld:] = qml_detail::from_variant<F>(v);
                return {};
            });
        } else if constexpr (ann::has<range>(Anns...) && std::is_arithmetic_v<F>) {
            constexpr auto r = ann::get_or<range>(range{0, 0}, Anns...);
            obj->registerSetter(qname, [tgt, qname](const QVariant &v) -> QString {
                F      val = qml_detail::from_variant<F>(v);
                double d   = static_cast<double>(val);
                if (d < r.min || d > r.max) {
                    return qname + QStringLiteral(" out of range [") +
                           QString::number(r.min) + QStringLiteral(", ") +
                           QString::number(r.max) + QStringLiteral("]");
                }
                tgt->[:Fld:] = val;
                return {};
            });
        } else {
            obj->registerSetter(qname, [tgt](const QVariant &v) -> QString {
                tgt->[:Fld:] = qml_detail::from_variant<F>(v);
                return {};
            });
        }
        } // if constexpr (qml_detail::bindable_type<F>)
    }

    template <typename T>
    template <std::meta::info Fn, auto... Anns>
    inline void QmlVisitor<T>::method_instance(const char *name) {
        constexpr auto dann     = ann::get_or<doc>(doc{""}, Anns...);
        constexpr auto btn      = ann::get_or<button>(button{}, Anns...);
        constexpr auto lbl      = ann::get_or<label>(label{""}, Anns...);
        constexpr auto arity    = std::define_static_array(std::meta::parameters_of(Fn)).size();
        const QString  qname    = QString::fromUtf8(name);
        const QString  qdisplay = (lbl.text[0] != '\0')
                                      ? QString::fromUtf8(lbl.text)
                                      : qname;

        QVariantList paramTypes;
        template for (constexpr auto p :
                      std::define_static_array(std::meta::parameters_of(Fn))) {
            using PT = std::remove_cvref_t<typename [:std::meta::type_of(p):]>;
            paramTypes.append(qml_detail::type_tag<PT>());
        }

        QVariantMap info;
        info[QStringLiteral("name")]        = qname;
        info[QStringLiteral("displayName")] = qdisplay;
        info[QStringLiteral("doc")]        = QString::fromUtf8(dann.text);
        info[QStringLiteral("button")]     = QString::fromUtf8(btn.label);
        info[QStringLiteral("arity")]      = static_cast<int>(arity);
        info[QStringLiteral("isStatic")]   = false;
        info[QStringLiteral("paramTypes")] = paramTypes;
        obj->appendMethod(info);

        T *tgt = target;
        obj->registerInvoker(qname, [tgt](const QVariantList &args) -> QVariant {
            return qml_detail::invoke_method_impl<Fn>(*tgt, args,
                                                     std::make_index_sequence<arity>{});
        });
    }

    template <typename T>
    template <std::meta::info Fn, auto... Anns>
    inline void QmlVisitor<T>::method_static(const char *name) {
        constexpr auto dann     = ann::get_or<doc>(doc{""}, Anns...);
        constexpr auto btn      = ann::get_or<button>(button{}, Anns...);
        constexpr auto lbl      = ann::get_or<label>(label{""}, Anns...);
        constexpr auto arity    = std::define_static_array(std::meta::parameters_of(Fn)).size();
        const QString  qname    = QString::fromUtf8(name);
        const QString  qdisplay = (lbl.text[0] != '\0')
                                      ? QString::fromUtf8(lbl.text)
                                      : qname;

        QVariantList paramTypes;
        template for (constexpr auto p :
                      std::define_static_array(std::meta::parameters_of(Fn))) {
            using PT = std::remove_cvref_t<typename [:std::meta::type_of(p):]>;
            paramTypes.append(qml_detail::type_tag<PT>());
        }

        QVariantMap info;
        info[QStringLiteral("name")]        = qname;
        info[QStringLiteral("displayName")] = qdisplay;
        info[QStringLiteral("doc")]        = QString::fromUtf8(dann.text);
        info[QStringLiteral("button")]     = QString::fromUtf8(btn.label);
        info[QStringLiteral("arity")]      = static_cast<int>(arity);
        info[QStringLiteral("isStatic")]   = true;
        info[QStringLiteral("paramTypes")] = paramTypes;
        obj->appendMethod(info);

        obj->registerInvoker(qname, [](const QVariantList &args) -> QVariant {
            return qml_detail::invoke_static_impl<Fn>(args,
                                                     std::make_index_sequence<arity>{});
        });
    }

    template <typename T> inline void bind_qml(ReflectedObject *obj, T &target) {
        QmlVisitor<T> v{obj, &target};
        walk<T>(v);
    }

} // namespace rosetta
