# Naming & visibility per backend

- alias("foo") / rename_for("python", "foo_bar") — override the exposed name; lets you keep snake_case in Python and 
camelCase in JS from a single C++ source. (Class-level renaming exists today via the manifest "expose" field —
see MANIFEST.md; the per-member / per-backend annotation form is what's still open.)
- hidden (or internal) — exclude from all bindings; useful for cache/scratch fields you can't make private for ABI reasons.
- writeonly — the dual of readonly. Passwords, set-once tokens: bind a setter but no getter. 

# Validation (beyond range)

- min / max as separate one-sided bounds. 
- length(min, max) — for strings and containers. 
- regex("…") — string pattern. 
- non_empty, non_null, required — terse predicates.
- one_of(a, b, c) — restricted set; could be auto-derived from an enum but useful for std::string fields.

# Serialization shape

- json_name("foo") — wire-format key when it must differ from the identifier (e.g. "$ref").
- skip_if_default / skip_if_null — omit from JSON when value is trivial. Big quality-of-life win for REST/WASM payloads. 
- format("iso8601"), format("base64") — string encoding hints for dates, blobs.
- default_value(42) — what to fill in when missing from input. Also feeds OpenAPI generation.

# REST/OpenAPI-specific 

- endpoint("/users/{id}"), http_method(GET) on methods.
- path_param, query_param, body on parameters.
- example("alice@example.com") — populates Swagger UI samples. 
- status_code(201) for response types.

# UI / editor hints (if Rosetta ever drives a property editor)

- widget("slider"), widget("color"), widget("file").
- step(0.1) — pairs with range.
- display_name("User-friendly label"), group("Advanced"), order(3).

# Versioning / lifecycle

- since("1.2.0") — first version that exposed this.
- deprecated("use foo()") — bindings emit a runtime warning or DeprecationWarning in Python.
- experimental — gate behind an opt-in flag in each backend.

# Units & semantic types

- unit("m/s") — physical units carried into Python (could plug into Pint) or rendered in REST docs.
- currency("USD") — for finance-flavored APIs.

# Threading / call-site 

- noexcept_binding — backend skips the try/catch wrapper.
- async — N-API exposes the method as Promise-returning; WASM dispatches to a worker.

# Persistence (if you ever cross into ORM territory) 

- primary_key, unique, indexed, foreign_key(^^OtherType, &OtherType::id) — the last one is the genuinely interesting case, 
because a reflection-based annotation can refer to another reflected entity, which a string-based attribute system can't.

# Example

The shape of the work is: define the annotation types, walk them in the registration loop with if constexpr type dispatch, emit
a backend-neutral schema, and let each renderer (web, ImGui, Qt) consume that schema. 

## 1. Annotation types 

Follow the existing recipe in annotations.h: NTTP-eligible structural types, defaulted operator==, strings routed through 
define_static_string. 

```cpp
namespace rosetta {
 
    enum class widget_kind {
        text, text_area, number, slider, checkbox,
        color, file, dir, dropdown, date, time,
    };
    
    struct widget { 
        widget_kind kind;  
        bool operator==(const widget&) const = default; 
    }; 
    
    struct step {  // slider/number granularity
        double value;
        bool operator==(const step&) const = default;
    };
    
    struct display_name {// human-friendly label 
        const char *text;  
        consteval display_name(const char *t): text(std::define_static_string(t)) {}  
        bool operator==(const display_name&) const = default;
    };  
    
    struct group { // section / tab name 
        const char *name;
        consteval group(const char *n): name(std::define_static_string(n)) {} 
        bool operator==(const group&) const = default;
    };
    
    struct order { int n; bool operator==(const order&) const = default; };
    
    struct placeholder { 
        const char *text;  
        consteval placeholder(const char *t): text(std::define_static_string(t)) {}
        bool operator==(const placeholder&) const = default;  
    };
  
} // namespace rosetta
```
 
Strong-typing widget with an enum class rather than a string is worth it: a typo becomes a compile error, and the renderer can  
switch on the enum.
  
## 2. Applying them 

```cpp
struct AppSettings {
    [[=rosetta::display_name("Volume"), 
    =rosetta::group("Audio"),  
    =rosetta::widget(rosetta::widget_kind::slider),
    =rosetta::range{0.0, 100.0}, 
    =rosetta::step{1.0},
    =rosetta::order{10}]]  
    double volume = 50.0;
  
    [[=rosetta::display_name("Theme color"),
    =rosetta::group("Appearance"),  
    =rosetta::widget(rosetta::widget_kind::color)]]  
    std::uint32_t theme_color = 0xFF00FFFF; 

    [[=rosetta::display_name("Save path"),  
    =rosetta::group("Storage"), 
    =rosetta::widget(rosetta::widget_kind::file)]] 
    std::string save_path;
};
```
  
## 3. Harvesting them in the registration loop 

The existing register_reflected<T>() already walks each field. Add an inner expansion over its annotations and dispatch on
type_of:  

```cpp
struct FieldUI {
    std::string id, label, group, widget; 
    std::optional<double> min, max, step;
    int order = 0;
};
  
template <typename T> UISchema build_schema() {
    UISchema       schema;
    constexpr auto ctx = std::meta::access_context::current();

    template for (constexpr auto m :
                  std::define_static_array(std::meta::nonstatic_data_members_of(^^T, ctx))) {

        FieldUI f;
        f.id = std::meta::identifier_of(m);

        template for (constexpr auto a : std::define_static_array(std::meta::annotations_of(m))) {

            using A          = typename[:std::meta::type_of(a):];
            constexpr auto v = [:std::meta::value_of(a):];

            ifconstexpr(std::same_as<A, rosetta::display_name>) f.label   = v.text;
            else if constexpr (std::same_as<A, rosetta::group>) f.group   = v.name;
            else if constexpr (std::same_as<A, rosetta::widget>) f.widget = to_string(v.kind);
            else if constexpr (std::same_as<A, rosetta::range>) {
                f.min = v.min;
                f.max = v.max;
            }
            else if constexpr (std::same_as<A, rosetta::step>) f.step   = v.value;
            else if constexpr (std::same_as<A, rosetta::order>) f.order = v.n;
        }

        if (f.widget.empty())
            f.widget = infer_widget<[:std::meta::type_of(m):]>();
        if (f.label.empty())
            f.label = humanize(f.id);

        schema.fields.push_back(std::move(f));
    }
    std::ranges::sort(schema.fields, {}, &FieldUI::order);
    return schema;
}
```
  
Two pieces worth their own lines: 
- value_of(a) gives the constant value of the annotation; splicing it ([: ... :]) brings it into the expression as a value of 
type A. From there it's just a normal field access.  
- The infer_widget<U>() fallback is what makes the system pleasant — bool → checkbox, enum → dropdown built from
enumerators_of(^^U), std::filesystem::path → file, arithmetic → number (or slider if a range was also seen).  
  
## 4. The schema and the renderers  
  
The harvested UISchema should be a plain data structure (JSON-serializable). One reflection backend → many renderers:  
 
- Web — serialize to JSON, a React/Svelte form generator switches on widget. 
- ImGui — walk the schema at runtime, emit ImGui::SliderFloat, ImGui::ColorEdit4, etc.
- Qt — build a QFormLayout with QSlider/QColorDialog/QFileDialog per widget kind.  
  
Keeping the schema renderer-neutral is the key design move: the annotations describe intent (“this is a color”), not framework  
specifics (“use ImGui::ColorPicker4”). That way one annotated struct drives an editor in every UI stack the project might bind  
to — same philosophy as the existing Python/Node/REST/WASM generators. 
 
## 5. Two non-obvious considerations

- Conflict resolution. Decide up-front whether widget(slider) + range(0,1) is required-together or whether range alone implies  
slider for arithmetic types. I'd lean toward implicit: fewer annotations on the call site.
- Per-backend overrides. Eventually someone will want the web form to call the field "Volume %" while the desktop UI keeps
"Volume". Add a tagged variant like display_name_for("web", "Volume %") rather than encoding the override in display_name itself.