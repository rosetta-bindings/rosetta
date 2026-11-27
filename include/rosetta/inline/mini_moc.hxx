namespace rosetta::moc {

    // -------------------------------------------------------------------------
    // ScopedConnection: an RAII handle that ties a single signal/slot connection to a scope (or to
    // an owning object). When the ScopedConnection is destroyed it disconnects the connection it
    // owns, so callers no longer have to remember to call Signal::disconnect(id) by hand. This is
    // the standard fix for the "dangling slot" hazard: a slot that captures a receiver outlives that
    // receiver and the next emission calls into freed memory. Storing a ScopedConnection as a member
    // of the receiver makes the connection die with the receiver automatically.
    //
    // The disconnect action is type-erased into a std::function<void()>, so one (non-templated)
    // ScopedConnection type can own a connection to any Signal<Args...>. It is move-only: a
    // connection has exactly one owner, and copying would let two objects both try to disconnect the
    // same Id. reset() disconnects early; release() detaches ownership without disconnecting (the
    // connection then lives on, like std::unique_ptr::release).
    //
    // Lifetime caveat: a ScopedConnection captures a back-reference to its Signal, so the Signal
    // must outlive any ScopedConnection that targets it; otherwise ~ScopedConnection would
    // disconnect on a dead object. For this prototype that ordering is documented rather than
    // enforced.
    // -------------------------------------------------------------------------
    class ScopedConnection {
        std::function<void()> disconnect_;

    public:
        ScopedConnection() = default;
        explicit ScopedConnection(std::function<void()> d);

        ScopedConnection(ScopedConnection &&o) noexcept;
        ScopedConnection &operator=(ScopedConnection &&o) noexcept;
        ScopedConnection(const ScopedConnection &)            = delete;
        ScopedConnection &operator=(const ScopedConnection &) = delete;

        ~ScopedConnection();

        /// Disconnect now, if still owning a live connection.
        void reset();

        /// Detach ownership without disconnecting; the connection survives.
        void release();

        /// True while this handle still owns a connection.
        bool connected() const;
    };

    // -------------------------------------------------------------------------
    // Signal: a simple signal that can connect to multiple subscribers. It stores the connected
    // slots as std::function and operator() calls each one. The template parameters allow signals
    // with any number of arguments.
    //
    // Disconnection: connect() returns a stable Id (a monotonically increasing handle) that
    // identifies the connection independently of its position in the underlying list. disconnect()
    // removes the connection with that Id, and disconnect_all() removes every connection. Stable Ids
    // are necessary because std::function is not equality-comparable, so there would otherwise be
    // nothing to disconnect by. Emission is re-entrancy-safe: a slot may connect or disconnect
    // itself (or another slot) while the signal is firing without invalidating the in-progress
    // iteration.
    //
    // Note that this implementation does not handle return values from slots or manage object
    // lifetimes, but it serves the purpose of demonstrating the core concept of signals and slots.
    // -------------------------------------------------------------------------
    template <class... Args> class Signal {
        struct Conn {
            std::size_t                  id;
            std::function<void(Args...)> fn; // null == tombstoned, pending removal
        };
        // Node-based storage: a slot may connect or disconnect during emission, and
        // std::list keeps every other node's iterator valid across both, so we never
        // move or destroy the std::function we are currently calling.
        mutable std::list<Conn> subs_;
        mutable std::size_t     emitting_{0};
        std::size_t             next_{1};

        // Physically drop tombstoned connections. Only safe when not iterating.
        void compact() const;

    public:
        /// Opaque handle identifying a single connection. 0 is never a valid Id.
        using Id = std::size_t;

        /// Connect a slot; returns a stable Id that can be passed to disconnect().
        Id connect(std::function<void(Args...)> f);

        /// Remove the connection with the given Id. Returns true if one was removed.
        bool disconnect(Id id);

        /// Remove every connection.
        void disconnect_all();

        /// Connect a slot and hand back an RAII handle that disconnects it on destruction.
        ScopedConnection scoped_connect(std::function<void(Args...)> f);

        void operator()(Args... a) const;
    };

    // -------------------------------------------------------------------------
    // Signal<Args...> members.
    // -------------------------------------------------------------------------
    template <class... Args> void Signal<Args...>::compact() const {
        std::erase_if(subs_, [](Conn const &c) { return !c.fn; });
    }

    template <class... Args>
    auto Signal<Args...>::connect(std::function<void(Args...)> f) -> Id {
        Id id = next_++;
        subs_.push_back({id, std::move(f)});
        return id;
    }

    template <class... Args> bool Signal<Args...>::disconnect(Id id) {
        for (auto &c : subs_) {
            if (c.id == id && c.fn) {
                c.fn = nullptr; // tombstone now; skipped from here on
                if (emitting_ == 0) {
                    compact(); // safe to physically erase when idle
                }
                return true;
            }
        }
        return false;
    }

    template <class... Args>
    ScopedConnection Signal<Args...>::scoped_connect(std::function<void(Args...)> f) {
        Id id = connect(std::move(f));
        return ScopedConnection{[this, id] { this->disconnect(id); }};
    }

    template <class... Args> void Signal<Args...>::disconnect_all() {
        if (emitting_ != 0) {
            for (auto &c : subs_) {
                c.fn = nullptr;
            }
        } else {
            subs_.clear();
        }
    }

    template <class... Args> void Signal<Args...>::operator()(Args... a) const {
        ++emitting_;
        // list iterators survive inserts/erases of other nodes; tombstoned slots
        // (including any disconnected mid-emission) are skipped via the null check.
        for (auto const &c : subs_) {
            if (c.fn) {
                c.fn(a...);
            }
        }
        if (--emitting_ == 0) {
            compact();
        }
    }

    // -------------------------------------------------------------------------
    // ScopedConnection members.
    // -------------------------------------------------------------------------
    inline ScopedConnection::ScopedConnection(std::function<void()> d) : disconnect_(std::move(d)) {}

    inline ScopedConnection::ScopedConnection(ScopedConnection &&o) noexcept
        : disconnect_(std::exchange(o.disconnect_, nullptr)) {}

    inline ScopedConnection &ScopedConnection::operator=(ScopedConnection &&o) noexcept {
        if (this != &o) {
            reset();
            disconnect_ = std::exchange(o.disconnect_, nullptr);
        }
        return *this;
    }

    inline ScopedConnection::~ScopedConnection() { reset(); }

    inline void ScopedConnection::reset() {
        if (disconnect_) {
            disconnect_();
            disconnect_ = nullptr;
        }
    }

    inline void ScopedConnection::release() { disconnect_ = nullptr; }

    inline bool ScopedConnection::connected() const { return static_cast<bool>(disconnect_); }

    // -------------------------------------------------------------------------
    // Reflection helpers.
    // -------------------------------------------------------------------------
    namespace detail {

        template <class Tag, class T> consteval std::meta::info find_tagged(std::string_view name) {
            constexpr auto ctx = std::meta::access_context::unchecked();
            for (auto m : std::meta::members_of(^^T, ctx)) {
                if (rosetta::annotation_of_type<Tag>(m).has_value() &&
                    std::meta::identifier_of(m) == name) {
                    return m;
                }
            }
            return {};
        }

        // Reflection of the Signal<...> primary template, obtained from a dummy
        // specialization (the bare template name isn't directly spellable with
        // ^^). Used to recognize signal members by their type.
        constexpr std::meta::info signal_template = std::meta::template_of(^^Signal<int>);

        // Find a signal by name: a Signal<...> data member of T with that
        // identifier. Signals carry no annotation — the type is the marker, so
        // there is nothing to keep in sync between an annotation and the type.
        template <class T> consteval std::meta::info find_signal(std::string_view name) {
            constexpr auto ctx = std::meta::access_context::unchecked();
            for (auto m : std::meta::nonstatic_data_members_of(^^T, ctx)) {
                auto t = std::meta::type_of(m);
                if (std::meta::has_template_arguments(t) &&
                    std::meta::template_of(t) == signal_template &&
                    std::meta::identifier_of(m) == name) {
                    return m;
                }
            }
            return {};
        }

        template <class T> consteval std::meta::info find_property_field(std::string_view name) {
            /**
             * This assumes that the property annotation is on the field itself, not on the
             * getter/setter. We could support either, but this is simpler and more consistent with
             * the idea of a "property" as a field with some extra metadata. If we wanted to support
             * annotations on getters/setters, we'd need to look at member functions as well and
             * figure out which one is the "main" one for the property.
             */
            constexpr auto ctx = std::meta::access_context::unchecked();
            
            for (auto m : std::meta::nonstatic_data_members_of(^^T, ctx)) {
                auto pa = rosetta::annotation_of_type<property>(m);
                if (pa.has_value() && std::string_view{pa->name} == name) {
                    return m;
                }
            }
            return {};
        }

    } // namespace detail

    // -------------------------------------------------------------------------
    // connect<"sig","slot">(sender, receiver)
    //
    // Compile-time-checked: missing names static_assert, mismatched argument
    // types fail at the lambda body.
    // -------------------------------------------------------------------------
    template <fixed_string Sig, fixed_string Slot, class S, class R>
    inline auto connect(S &sender, R &receiver) {
        constexpr auto sig_r = detail::find_signal<S>(Sig.view());
        // static: the lambda below reads it without a capture (capturing a
        // std::meta::info would make the lambda an immediate function).
        static constexpr auto slot_r = detail::find_tagged<slot_tag, R>(Slot.view());
        static_assert(sig_r != std::meta::info{},
                      "no Signal<...> member with that name on sender");
        static_assert(slot_r != std::meta::info{},
                      "no [[=slot]]-tagged member with that name on receiver");

        return (sender.[:sig_r:]).connect([&receiver](auto &&...args) {
            (receiver.[:slot_r:])(std::forward<decltype(args)>(args)...);
        });
    }

    // -------------------------------------------------------------------------
    // get<"prop">(obj): read access for a [[=property]]-annotated field.
    // -------------------------------------------------------------------------
    template <fixed_string Name, class T> inline auto const &get(T const &obj) {
        constexpr auto field_r = detail::find_property_field<T>(Name.view());
        static_assert(field_r != std::meta::info{}, "no [[=property]] with that handle on T");
        return obj.[:field_r:];
    }

    // -------------------------------------------------------------------------
    // set<"prop">(obj, value): write access. Equality-gated; if the property
    // declares a NOTIFY signal, fires it after a successful change.
    // -------------------------------------------------------------------------
    template <fixed_string Name, class T, class V> inline void set(T &obj, V &&value) {
        constexpr auto field_r = detail::find_property_field<T>(Name.view());
        static_assert(field_r != std::meta::info{}, "no [[=property]] with that handle on T");

        using F = [:std::meta::type_of(field_r):];
        F v(std::forward<V>(value));
        if (obj.[:field_r:] == v) {
            return;
        }
        obj.[:field_r:] = std::move(v);

        constexpr auto pa          = rosetta::annotation_of_type<property>(field_r).value();
        constexpr auto notify_name = std::string_view{pa.notify};
        if constexpr (!notify_name.empty()) {
            constexpr auto sig_r = detail::find_signal<T>(notify_name);
            static_assert(sig_r != std::meta::info{},
                          "NOTIFY signal named by property is not declared");
            (obj.[:sig_r:])(obj.[:field_r:]);
        }
    }

} // namespace rosetta::moc
