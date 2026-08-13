#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include "MaterialRegistry.h"

using namespace scree;

namespace {
	// Assertions go through Log::Type and Log::Severity, never through the message text --
	// the wording changes far more often than the meaning does.
	int CountOfType(const MaterialRegistry& registry, Log::Type type)
	{
		int count = 0;
		for (const auto& log : registry.GetLogs())
			if (log.type == type) ++count;

		return count;
	}

	// Air holds id 0, so the first material in the document is always 1.
	constexpr MaterialID FIRST = 1;

	// Named M0, M1, ... in document order.
	std::string DocumentWithMaterials(int count)
	{
		nlohmann::json document;
		document["tags"] = nlohmann::json::array();
		document["materials"] = nlohmann::json::array();

		for (int i = 0; i < count; ++i) {
			nlohmann::json material = nlohmann::json::object();
			material["name"] = "M" + std::to_string(i);
			document["materials"].push_back(material);
		}

		return document.dump();
	}
}

// --- document level ---------------------------------------------------------

TEST_CASE("a malformed document is rejected whole")
{
	MaterialRegistry registry;

	CHECK_FALSE(registry.LoadMaterialsFromJSON(R"({ "materials": [)"));
	CHECK(CountOfType(registry, Log::Type::ParseFailed) == 1);
	CHECK(Log::Worst(registry.GetLogs()) == Log::Severity::RejectFile);
}

TEST_CASE("a document whose top level is not an object is rejected, not thrown out of")
{
	MaterialRegistry registry;

	CHECK_FALSE(registry.LoadMaterialsFromJSON("[1, 2, 3]"));
	CHECK(CountOfType(registry, Log::Type::ParseFailed) == 1);
}

TEST_CASE("a materials entry that is not an object is rejected, not thrown out of")
{
	MaterialRegistry registry;

	CHECK_FALSE(registry.LoadMaterialsFromJSON(R"({ "tags": [], "materials": [1, 2, 3] })"));
	CHECK(Log::Worst(registry.GetLogs()) == Log::Severity::RejectFile);
}

TEST_CASE("a missing file leaves an air-only registry")
{
	MaterialRegistry registry;

	CHECK_FALSE(registry.LoadMaterials("no/such/materials.json"));
	CHECK(CountOfType(registry, Log::Type::FileMissing) == 1);
	CHECK(registry.GetMaterialsCount() == 1);
	CHECK(registry.GetName(MaterialRegistry::AIR_ID) == "Air");
}

TEST_CASE("an empty document loads air and nothing else")
{
	MaterialRegistry registry;

	CHECK(registry.LoadMaterialsFromJSON("{}"));
	CHECK(registry.GetMaterialsCount() == 1);
	CHECK(registry.GetLogs().empty());
}

TEST_CASE("a second load replaces the first rather than appending to it")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [], "materials": [{ "name": "Sand" }]
	})"));
	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [], "materials": [{ "name": "Water", "movement": { "density": 30 } }]
	})"));

	CHECK(registry.GetMaterialsCount() == 2);
	CHECK(registry.GetName(FIRST) == "Water");
	CHECK(registry.Get(FIRST).movement.density == 30);
	CHECK(registry.GetLogs().empty());
}

// --- identity ---------------------------------------------------------------

TEST_CASE("a duplicate name is rejected without taking the file down")
{
	MaterialRegistry registry;

	CHECK(registry.LoadMaterialsFromJSON(R"({
		"tags": [], "materials": [{ "name": "Sand" }, { "name": "Sand" }]
	})"));
	CHECK(CountOfType(registry, Log::Type::Duplicate) == 1);
	CHECK(registry.GetMaterialsCount() == 2);
}

TEST_CASE("an Air entry in the file collides with the built-in one")
{
	MaterialRegistry registry;

	CHECK(registry.LoadMaterialsFromJSON(R"({ "tags": [], "materials": [{ "name": "Air" }] })"));
	CHECK(CountOfType(registry, Log::Type::Duplicate) == 1);
	CHECK(registry.GetMaterialsCount() == 1);
}

TEST_CASE("a nameless entry is rejected")
{
	MaterialRegistry registry;

	CHECK(registry.LoadMaterialsFromJSON(R"({ "tags": [], "materials": [{ "steps": 4 }] })"));
	CHECK(CountOfType(registry, Log::Type::MissingField) == 1);
	CHECK(registry.GetMaterialsCount() == 1);
}

// The placeholder push_back in ParseMaterial exists for this. If ids ever shift, every
// material after a rejected one silently becomes a different material.
TEST_CASE("a rejected material does not shift the ids of the ones after it")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [],
		"materials": [
			{ "name": "Broken", "tags": { "no_such_tag": 50 } },
			{ "name": "Water",  "movement": { "density": 30 } }
		]
	})"));

	REQUIRE(registry.GetMaterialsCount() == 3);
	CHECK(registry.GetName(2) == "Water");
	CHECK(registry.Get(2).movement.density == 30);
	CHECK(CountOfType(registry, Log::Type::UnknownReference) == 1);
}

// --- numeric fields ---------------------------------------------------------

TEST_CASE("a y_direction of 0 becomes 1 and warns")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [], "materials": [{ "name": "Sand", "movement": { "y_direction": 0 } }]
	})"));

	CHECK(registry.Get(FIRST).movement.Y_direction == 1);
	CHECK(CountOfType(registry, Log::Type::OutOfRange) == 1);
	CHECK(Log::Worst(registry.GetLogs()) == Log::Severity::Warning);
}

TEST_CASE("an out of range value is clamped rather than wrapped")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [], "materials": [{ "name": "Sand", "lifespan": { "initial": 300 } }]
	})"));

	// 300 narrowed into the uint8_t unchecked would be 44.
	CHECK(registry.Get(FIRST).lifespanData.Initial == 255);
	CHECK(CountOfType(registry, Log::Type::OutOfRange) == 1);
}

TEST_CASE("steps of 0 is raised to 1")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [], "materials": [{ "name": "Sand", "steps": 0 }]
	})"));

	// Random_step takes fast_rand() % numberOfSteps.
	CHECK(registry.Get(FIRST).numberOfSteps == 1);
	CHECK(CountOfType(registry, Log::Type::OutOfRange) == 1);
}

TEST_CASE("a fractional value is truncated and warns")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [], "materials": [{ "name": "Sand", "movement": { "density": 30.7 } }]
	})"));

	CHECK(registry.Get(FIRST).movement.density == 30);
	CHECK(CountOfType(registry, Log::Type::WrongType) == 1);
	CHECK(Log::Worst(registry.GetLogs()) == Log::Severity::Warning);
}

TEST_CASE("an omitted movement block leaves the defaults in place")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({ "tags": [], "materials": [{ "name": "Sand" }] })"));

	const auto& movement = registry.Get(FIRST).movement;
	CHECK(movement.Y_direction == 1);
	CHECK(movement.density == 0);
	CHECK_FALSE(movement.can_fall);
	CHECK(registry.GetLogs().empty());
}

// --- references -------------------------------------------------------------

TEST_CASE("an unknown tag rejects the material")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": ["flammable"],
		"materials": [{ "name": "Oil", "tags": { "flammble": 100 } }]
	})"));

	CHECK(CountOfType(registry, Log::Type::UnknownReference) == 1);
	CHECK(Log::Worst(registry.GetLogs()) == Log::Severity::RejectMaterial);
}

TEST_CASE("a known tag records its intensity and anchor flag")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": ["flammable", "corrodible"],
		"materials": [{ "name": "Fire", "tags": { "corrodible": 20 }, "anchor": "flammable" }]
	})"));

	REQUIRE(registry.GetLogs().empty());
	CHECK(registry.GetTagIntensity(FIRST, 1) == 20);
	// Whole-mask compare, so a stray bit on another tag fails too.
	CHECK(registry.Get(FIRST).anchorTagBitmask == 0b0000'0001);
	// Anchoring to a tag is independent of carrying it.
	CHECK(registry.GetTagIntensity(FIRST, 0) == 0);
}

TEST_CASE("an unknown anchor rejects the material")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": ["flammable"],
		"materials": [{ "name": "Fire", "anchor": "flammble" }]
	})"));

	CHECK(CountOfType(registry, Log::Type::UnknownReference) == 1);
}

TEST_CASE("an unknown transition material rejects the material")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [],
		"materials": [{ "name": "Fire", "lifespan": { "initial": 10, "tick": 1,
			"on_death": [{ "material": "Smoke", "weight": 1 }] } }]
	})"));

	CHECK(CountOfType(registry, Log::Type::UnknownReference) == 1);
	CHECK(Log::Worst(registry.GetLogs()) == Log::Severity::RejectMaterial);
}

// --- transitions ------------------------------------------------------------

// on_death is required only when the lifespan actually ticks -- a tick of 0 never dies,
// so it has nothing to transition into. Both halves of that rule are pinned here because
// the deciding `if (tick == 0) return` is easy to drop or invert without anything crashing.
TEST_CASE("on_death is required when the lifespan ticks, optional when it does not")
{
	SECTION("ticks without on_death is rejected") {
		MaterialRegistry registry;
		REQUIRE(registry.LoadMaterialsFromJSON(R"({
			"tags": [],
			"materials": [{ "name": "Fire", "lifespan": { "initial": 10, "tick": 1 } }]
		})"));
		CHECK(CountOfType(registry, Log::Type::MissingField) == 1);
	}

	SECTION("does not tick, so no on_death is needed") {
		MaterialRegistry registry;
		REQUIRE(registry.LoadMaterialsFromJSON(R"({
			"tags": [],
			"materials": [{ "name": "Stone", "lifespan": { "initial": 10, "tick": 0 } }]
		})"));
		CHECK(registry.GetLogs().empty());
	}
}

TEST_CASE("an on_death transition is stored and picked")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [],
		"materials": [{ "name": "Fire", "lifespan": { "initial": 10, "tick": 2,
			"on_death": [{ "material": "Air", "weight": 3 }] } }]
	})"));

	REQUIRE(registry.GetLogs().empty());
	const auto& span = registry.Get(FIRST).lifespanData.OnDeathTransitionSpan;
	CHECK(span.count == 1);
	CHECK(span.totalWeight == 3);

	const Transition* picked = registry.PickTransition(span);
	REQUIRE(picked != nullptr);
	CHECK(picked->nextID == MaterialRegistry::AIR_ID);
	CHECK(picked->lifespanBase == Transition::LifeSpanBase::Initial);
}

TEST_CASE("a lifespan_base of self in on_death warns and falls back to initial")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [],
		"materials": [{ "name": "Fire", "lifespan": { "initial": 10, "tick": 2,
			"on_death": [{ "material": "Air", "weight": 1, "lifespan_base": "self" }] } }]
	})"));

	CHECK(Log::Worst(registry.GetLogs()) == Log::Severity::Warning);
	CHECK(CountOfType(registry, Log::Type::WrongType) == 1);

	const Transition* picked = registry.PickTransition(registry.Get(FIRST).lifespanData.OnDeathTransitionSpan);
	REQUIRE(picked != nullptr);
	CHECK(picked->lifespanBase == Transition::LifeSpanBase::Initial);
}

TEST_CASE("a span of weight 0 picks nothing instead of reading past itself")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [],
		"materials": [{ "name": "Fire", "lifespan": { "initial": 10, "tick": 2,
			"on_death": [{ "material": "Air", "weight": 0 }] } }]
	})"));

	CHECK(registry.PickTransition(registry.Get(FIRST).lifespanData.OnDeathTransitionSpan) == nullptr);
}

// --- reactions --------------------------------------------------------------

TEST_CASE("a reaction resolves a forward reference to a later material")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [],
		"materials": [
			{ "name": "Acid", "reactions": [{
				"target": "Stone", "target_type": "material", "chance": 50,
				"target_transitions": [{ "material": "Air", "weight": 1 }]
			}] },
			{ "name": "Stone" }
		]
	})"));

	REQUIRE(registry.GetLogs().empty());
	const auto& span = registry.Get(FIRST).reactionSpan;
	REQUIRE(span.count == 1);

	const Reaction* reaction = registry.GetReaction(span.start);
	CHECK(reaction->targetType == Reaction::TargetType::Material);
	CHECK(reaction->TargetID == 2);
	CHECK(reaction->Chance == 50);
	CHECK(registry.PickTransition(reaction->TargetTransitionsSpan)->nextID == MaterialRegistry::AIR_ID);
}

// --- limits -----------------------------------------------------------------
//
// tagIntensity is a std::array<std::uint8_t, MAX_TAGS> indexed by the ids ParseTags hands
// out, and MaterialID is a uint8_t. These guards are the only thing keeping either in
// range, so they get tested even though nothing in the shipped file comes near them.

TEST_CASE("tags past MAX_TAGS are dropped rather than indexed out of the array")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": ["t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8"],
		"materials": [{ "name": "Sand", "tags": { "t7": 10 } }]
	})"));

	CHECK(CountOfType(registry, Log::Type::LimitExceeded) == 1);
	CHECK(registry.GetTagIntensity(FIRST, MAX_TAGS - 1) == 10);
}

TEST_CASE("a material referring to a dropped tag gets an unknown reference")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": ["t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8"],
		"materials": [{ "name": "Sand", "tags": { "t8": 10 } }]
	})"));

	CHECK(CountOfType(registry, Log::Type::UnknownReference) == 1);
}

TEST_CASE("materials past MAX_MATERIALS are dropped")
{
	MaterialRegistry registry;

	// Air takes one of the 256 ids, so the last accepted file material is the 255th.
	REQUIRE(registry.LoadMaterialsFromJSON(DocumentWithMaterials(MAX_MATERIALS)));

	CHECK(registry.GetMaterialsCount() == MAX_MATERIALS);
	CHECK(registry.GetName(MAX_MATERIALS - 1) == "M254");
	CHECK(CountOfType(registry, Log::Type::LimitExceeded) == 1);
}

TEST_CASE("the material limit is reported once, not once per entry")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(DocumentWithMaterials(MAX_MATERIALS + 50)));
	CHECK(CountOfType(registry, Log::Type::LimitExceeded) == 1);
}

// Pins what the code does today, which is not what the comment on Log::CreateBadTagName
// describes: the entry is skipped without consuming an id, so the tags after it keep
// consecutive ids, and the load succeeds.
TEST_CASE("a non-string tag is skipped without shifting the tags after it")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": ["flammable", 5, "corrodible"],
		"materials": [{ "name": "Sand", "tags": { "corrodible": 10 } }]
	})"));

	CHECK(CountOfType(registry, Log::Type::WrongType) == 1);
	CHECK(registry.GetTagIntensity(FIRST, 1) == 10);
	CHECK(registry.GetTagIntensity(FIRST, 2) == 0);
}

// --- weighted picking -------------------------------------------------------

TEST_CASE("every entry in a span is reachable, in proportion to its weight")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [],
		"materials": [
			{ "name": "Fire", "lifespan": { "initial": 10, "tick": 1, "on_death": [
				{ "material": "Air",   "weight": 1 },
				{ "material": "Ash",   "weight": 1 },
				{ "material": "Smoke", "weight": 2 }
			] } },
			{ "name": "Ash" },
			{ "name": "Smoke" }
		]
	})"));

	REQUIRE(registry.GetLogs().empty());
	const auto& span = registry.Get(FIRST).lifespanData.OnDeathTransitionSpan;
	REQUIRE(span.count == 3);
	REQUIRE(span.totalWeight == 4);

	constexpr int DRAWS = 20000;
	int picks[4] = { 0, 0, 0, 0 };
	for (int i = 0; i < DRAWS; ++i) {
		const Transition* picked = registry.PickTransition(span);
		REQUIRE(picked != nullptr);
		REQUIRE(picked->nextID < 4);
		picks[picked->nextID]++;
	}

	// The last entry of a span is what an off-by-one in the roll drops first.
	CHECK(picks[MaterialRegistry::AIR_ID] > 0);
	CHECK(picks[2] > 0);
	CHECK(picks[3] > picks[2]);
	CHECK(picks[3] > picks[MaterialRegistry::AIR_ID]);
	CHECK(picks[1] == 0);
}

// Spans are windows into one shared transitions vector, so a wrong start reads a
// neighbour's entries rather than going out of bounds -- silent, and invisible in any
// test that only ever defines one span.
TEST_CASE("a span never picks a transition belonging to another material")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [],
		"materials": [
			{ "name": "Fire",  "lifespan": { "initial": 10, "tick": 1,
				"on_death": [{ "material": "Ash", "weight": 1 }] } },
			{ "name": "Ash" },
			{ "name": "Steam", "lifespan": { "initial": 10, "tick": 1,
				"on_death": [{ "material": "Air", "weight": 1 }] } }
		]
	})"));

	REQUIRE(registry.GetLogs().empty());
	const auto& fire = registry.Get(FIRST).lifespanData.OnDeathTransitionSpan;
	const auto& steam = registry.Get(3).lifespanData.OnDeathTransitionSpan;
	REQUIRE(fire.start != steam.start);

	for (int i = 0; i < 100; ++i) {
		REQUIRE(registry.PickTransition(fire)->nextID == 2);
		REQUIRE(registry.PickTransition(steam)->nextID == MaterialRegistry::AIR_ID);
	}
}

// --- wrong types ------------------------------------------------------------
//
// The parser catches nlohmann's exceptions and the malformed shapes JSON does not throw
// on, turning both into logs. These cover one of each mechanism rather than every field
// that shares it -- the rest are the same two lines copied.

// A short color array converts to a vector without complaint, so color_min[2] would read
// off the end. The size check is the only thing standing between a two-element array and
// a garbage colour channel -- exactly the kind of fault nothing else would surface.
TEST_CASE("a color array of the wrong length is caught before it is indexed")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [], "materials": [{ "name": "Sand", "color_min": [255, 0] }]
	})"));

	CHECK(CountOfType(registry, Log::Type::WrongType) == 1);
}

// A string where a number is expected throws inside nlohmann; the parser must turn that
// into a rejected material, not let it escape the load. Rejected materials get the
// magenta default so they are obvious in-game -- pinned here since nothing else reads it.
TEST_CASE("a wrong-typed field rejects the material as a magenta placeholder")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [], "materials": [{ "name": "Sand", "steps": "eight" }]
	})"));

	CHECK(CountOfType(registry, Log::Type::WrongType) == 1);
	CHECK(Log::Worst(registry.GetLogs()) == Log::Severity::RejectMaterial);

	const RGB& color = registry.Get(FIRST).minColor;
	CHECK(color.r == 255);
	CHECK(color.g == 0);
	CHECK(color.b == 255);
}

// reactions is read with is_array() before it is iterated, so an object here is a clean
// rejection rather than a crash on the range-for below it.
TEST_CASE("a non-array reactions field is rejected, not iterated")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterialsFromJSON(R"({
		"tags": [], "materials": [{ "name": "Acid", "reactions": {} }]
	})"));

	CHECK(CountOfType(registry, Log::Type::WrongType) == 1);
}

// --- the shipped asset ------------------------------------------------------

TEST_CASE("the shipped materials.json loads with nothing rejected")
{
	MaterialRegistry registry;

	REQUIRE(registry.LoadMaterials(SCREE_ASSETS_DIR "/materials.json"));
	// Only prints when a check below fails.
	INFO(Log::FormatLogs(registry.GetLogs()));
	CHECK(Log::Worst(registry.GetLogs()) == Log::Severity::Warning);
	CHECK(registry.GetMaterialsCount() > 1);
}
