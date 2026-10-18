// Copyright (c) fmaerten@gmail.com
// License: MIT

// The dynamic core's side of the extended annotations: what Object::set,
// Object::call and the MCP server enforce or report, from the key/value
// MetaAnnotation lists the `dynamic` backend emits. Hand-built metadata, in
// the emitted shape. Stock C++20.

#include <gtest/gtest.h>
#include <rosetta/runtime/mcp.h>
#include <string>

using namespace rosetta::dyn;
using rosetta::mcp::json;

namespace demo2 {
    struct Box {
        double      width  = 1;
        std::string label  = "box";
        std::string finish = "matte";
        bool        locked = false;
        double      hi     = 2;
        double      lo     = 1;

        double scaled(double factor, double offset) const { return width * factor + offset; }
        double area() const { return width * width; }
        void   wipe() { width = 0; }
    };
} // namespace demo2

namespace {
    extern const MetaClass kBox;

    TypeDesc td_d{.kind = Kind::number, .spelling = "double"};
    TypeDesc td_s{.kind = Kind::string, .spelling = "std::string"};
    TypeDesc td_b{.kind = Kind::boolean, .spelling = "bool"};
    TypeDesc td_v{.kind = Kind::void_, .spelling = "void"};
    TypeDesc td_box{.kind = Kind::object, .spelling = "demo2::Box", .object = "demo2::Box", .cls = &kBox};

    demo2::Box &B(const ObjectRef &r) { return *static_cast<demo2::Box *>(r.ptr); }

#define DOUBLE_FIELD(F)                                                                            \
    .get = +[](const ObjectRef &s, const ArgList &) { return make_any(&td_d, B(s).F); },          \
    .set = +[](const ObjectRef &s, const ArgList &a) {                                            \
        B(s).F = value_cast<double>(a[0]);                                                          \
        return Any::none();                                                                         \
    }
#define STRING_FIELD(F)                                                                            \
    .get = +[](const ObjectRef &s, const ArgList &) { return make_any(&td_s, B(s).F); },          \
    .set = +[](const ObjectRef &s, const ArgList &a) {                                            \
        B(s).F = value_cast<std::string>(a[0]);                                                     \
        return Any::none();                                                                         \
    }

    const MetaAnnotation width_anns[] = {{"unit", "m"}, {"min", "0"}, {"max", "10"},
                                         {"enabled_if", "!locked"}};
    const MetaAnnotation label_anns[] = {{"length", "[1, 8]"}, {"regex", "[a-z]+"}};
    const MetaAnnotation hi_anns[]    = {{"requires", "hi > lo"}, {"requires_message", "hi must exceed lo"}};
    const char *const    finishes[]   = {"matte", "gloss"};

    const MetaField kFields[] = {
        {.name = "width", .type = &td_d, .annotations = width_anns, .n_annotations = 4, DOUBLE_FIELD(width)},
        {.name = "label", .type = &td_s, .annotations = label_anns, .n_annotations = 2, STRING_FIELD(label)},
        {.name = "finish", .type = &td_s, .choices = finishes, .n_choices = 2, STRING_FIELD(finish)},
        {.name = "locked", .type = &td_b,
         .get = +[](const ObjectRef &s, const ArgList &) { return make_any(&td_b, B(s).locked); },
         .set = +[](const ObjectRef &s, const ArgList &a) {
             B(s).locked = value_cast<bool>(a[0]);
             return Any::none();
         }},
        {.name = "hi", .type = &td_d, .annotations = hi_anns, .n_annotations = 2, DOUBLE_FIELD(hi)},
        {.name = "lo", .type = &td_d, DOUBLE_FIELD(lo)},
    };

    const MetaParam      scaled_params[] = {{.name = "factor", .type = &td_d},
                                            {.name = "offset", .type = &td_d, .default_value = "0.5"}};
    const MetaAnnotation scaled_anns[]   = {{"requires", "factor < area() * 10"},
                                            {"requires_message", "factor too large"}};
    const MetaAnnotation wipe_anns[]     = {{"destructive", "true"}, {"confirm", "Wipe it?"}};

    const MetaMethod kMethods[] = {
        {.name = "scaled", .ret = &td_d, .params = scaled_params, .n_params = 2, .is_const = true,
         .annotations = scaled_anns, .n_annotations = 2,
         .invoke = +[](const ObjectRef &s, const ArgList &a) {
             return make_any(&td_d, B(s).scaled(value_cast<double>(a[0]), value_cast<double>(a[1])));
         }},
        {.name = "area", .ret = &td_d, .is_const = true,
         .invoke = +[](const ObjectRef &s, const ArgList &) { return make_any(&td_d, B(s).area()); }},
        {.name = "wipe", .ret = &td_v, .annotations = wipe_anns, .n_annotations = 2,
         .invoke = +[](const ObjectRef &s, const ArgList &) {
             B(s).wipe();
             return Any::none();
         }},
    };
    const MetaCtor kCtors[] = {{.construct = +[](const ArgList &) -> void * { return new demo2::Box(); }}};

    const MetaClass kBox{.name = "Box", .qualified = "demo2::Box",
                         .fields = kFields, .n_fields = std::size(kFields),
                         .methods = kMethods, .n_methods = std::size(kMethods),
                         .ctors = kCtors, .n_ctors = 1,
                         .destroy = +[](void *p) { delete static_cast<demo2::Box *>(p); },
                         .self = &td_box};

    struct DynAnn : ::testing::Test {
        static void SetUpTestSuite() {
            registry().add_class(&kBox);
            registry().link();
        }
        Object make() {
            const Result r = Object::create(kBox);
            return Object::adopt(kBox, r.value.as_object().ptr, r.value.as_object().owner);
        }
    };

    std::string err(const Result &r) { return r.error; }
} // namespace

TEST_F(DynAnn, MinMaxLengthRegexChoices) {
    Object o = make();
    EXPECT_NE(err(o.set("width", Any::real(-1))).find("below the minimum 0"), std::string::npos);
    EXPECT_NE(err(o.set("width", Any::real(11))).find("above the maximum 10"), std::string::npos);
    EXPECT_TRUE(o.set("width", Any::real(3)).ok());
    EXPECT_NE(err(o.set("label", Any::text(""))).find("length 0, allowed 1..8"), std::string::npos);
    EXPECT_NE(err(o.set("label", Any::text("Box9"))).find("does not match"), std::string::npos);
    EXPECT_TRUE(o.set("label", Any::text("crate")).ok());
    EXPECT_NE(err(o.set("finish", Any::text("satin"))).find("not one of: matte, gloss"), std::string::npos);
    EXPECT_TRUE(o.set("finish", Any::text("gloss")).ok());
}

TEST_F(DynAnn, FieldRequiresSeesTheNewValue) {
    Object o = make();
    EXPECT_NE(err(o.set("hi", Any::real(0.5))).find("hi must exceed lo"), std::string::npos);
    EXPECT_TRUE(o.set("hi", Any::real(5)).ok());
}

TEST_F(DynAnn, DefaultsFillOmittedTrailingArguments) {
    Object o = make();
    o.set("width", Any::real(2));
    const Result r = o.call("scaled", {Any::real(3)});
    ASSERT_TRUE(r.ok()) << r.error;
    EXPECT_DOUBLE_EQ(r.value.as_number(), 6.5); // 2 * 3 + 0.5
    EXPECT_DOUBLE_EQ(o.call("scaled", {Any::real(3), Any::real(1)}).value.as_number(), 7.0);
}

TEST_F(DynAnn, MethodPreconditionUsesParametersAndNullaryMethods) {
    Object o = make(); // width 1 -> area 1 -> factor must be < 10
    const Result r = o.call("scaled", {Any::real(50)});
    EXPECT_NE(r.error.find("precondition `factor < area() * 10` failed — factor too large"),
              std::string::npos)
        << r.error;
}

TEST_F(DynAnn, EvaluateAndCondition) {
    Object o = make();
    EXPECT_TRUE(condition(o, kBox.field("width")->annotation("enabled_if")));
    o.set("locked", Any::boolean(true));
    EXPECT_FALSE(condition(o, "!locked"));
    EXPECT_TRUE(condition(o, "finish == matte && (width + 1) * 2 >= 4"));
    EXPECT_TRUE(condition(o, "nonsense((")); // fails open
    EXPECT_FALSE(evaluate(o, "nope()").ok());
}

TEST_F(DynAnn, McpReportsAnnotationsAndGatesDestructiveCalls) {
    rosetta::mcp::Server s;
    auto call = [&](const char *tool, json args) { return s.call_tool(tool, args); };

    const json d = call("describe_class", {{"class", "Box"}})["structuredContent"];
    const json &width = d["fields"][0];
    EXPECT_EQ(width["unit"], "m");
    EXPECT_EQ(width["min"], 0.0);
    EXPECT_EQ(width["enabled_if"], "!locked");
    EXPECT_EQ(d["fields"][1]["length"], json::array({1, 8}));
    EXPECT_EQ(d["methods"][0]["params"][1]["default"], 0.5);
    EXPECT_EQ(d["methods"][2]["destructive"], true);

    call("create_object", {{"class", "Box"}, {"name", "b"}});
    const json refused = call("call_method", {{"target", "b"}, {"method", "wipe"}});
    EXPECT_TRUE(refused["isError"].get<bool>());
    EXPECT_NE(refused["content"][0]["text"].get<std::string>().find("Wipe it?"), std::string::npos);
    EXPECT_FALSE(call("call_method", {{"target", "b"}, {"method", "wipe"}, {"confirm", true}})["isError"]);

    // keyword arguments may omit a defaulted parameter (a fresh box: the wiped
    // one has area() == 0, so the precondition would — rightly — refuse)
    call("create_object", {{"class", "Box"}, {"name", "c"}});
    const json r = call("call_method", {{"target", "c"}, {"method", "scaled"}, {"args", {{"factor", 2}}}});
    EXPECT_FALSE(r["isError"].get<bool>()) << r;
    EXPECT_EQ(r["structuredContent"]["result"], 2.5); // 1 * 2 + 0.5
}
