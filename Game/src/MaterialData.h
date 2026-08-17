#pragma once

#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include "rgb.h"



namespace scree
{
	static constexpr int MAX_TAGS = 8;			// one std::uint8_t slot per tag and an 8-bit mask on every material
	static constexpr int MAX_MATERIALS = 256;	// the whole MaterialID range; extras are dropped at load
	using MaterialID = std::uint8_t;

	// Index range into the reaction arena. Transition spans need a weight total too, so
	// they use TransitionsSpan; reactionSpan is all that's left on this one.
	struct Span {
		std::uint16_t start = 0;
		std::uint16_t count = 0;
	};

	// Index range into the transition arena, plus its weights summed at load. A total of 0
	// (empty, or all weights 0) tells PickTransition there is nothing to pick.
	struct TransitionsSpan {
		std::uint16_t start = 0;
		std::uint16_t count = 0;
		std::uint16_t totalWeight = 0;
	};

	struct Movement {
		std::int8_t Y_direction = 1;
		// Strict comparison, so equal densities never displace each other.
		std::uint8_t density = 0;
		std::uint8_t scatter_chance = 0;
		bool can_fall = false;
		bool can_cascade = false;
		bool is_fluid = false;
		bool is_liquid = false;
	};

	// One weighted outcome in a span. "Nothing happens" is not an absent entry but one
	// with noTransition set, and it needs a weight like any other.
	struct Transition {
		// on_death allows Initial only; ParseTransitionSpan warns on the other two.
		enum class LifeSpanBase : std::uint8_t {
			Self,		// old lifespan of the tile being rewritten
			// Lifespan of the other participant, read AFTER it ticks this frame -- so the
			// lifespan tick must stay ordered before reactions in Update_pixel.
			Reactor,
			Initial,    // use the default lifespan of the new material it turns into
		};

		bool noTransition = false;			// leaves the block untouched; nextID unused
		MaterialID nextID = 0;
		std::uint8_t weight = 1;		// relative to the other weights in the same span
		LifeSpanBase lifespanBase = LifeSpanBase::Initial;
	};

	// No lifespan block resolves to Initial 255, Tick 0.
	struct LifeSpan {
		std::uint8_t Initial = 255;
		// Subtracted once per update; 0 means never dies, so on_death is unused.
		std::uint8_t Tick = 0;
		std::uint8_t Chance = 100;
		TransitionsSpan OnDeathTransitionSpan = { 0, 0, 0};
	};

	struct Reaction {
		enum class TargetType : std::uint8_t {
			Material,
			Tag,
		};

		enum class Sample : std::uint8_t {
			All,			// keep scanning; one reaction can fire on all four neighbours
			FirstToReact,	// stop at the neighbour that fired
		};

		TargetType targetType = TargetType::Material;
		MaterialID TargetID = 0;		// material id when Material, tag id when Tag
		// Percent, Material targets only (default 100). Tag targets use intensity instead.
		std::uint8_t Chance = 0;
		TransitionsSpan TargetTransitionsSpan = { 0, 0, 0 };
		// Firing one ends the tile's update -- it's a different material now.
		TransitionsSpan SelfTransitionsSpan = { 0, 0, 0 };

		Sample sample = Sample::FirstToReact;
		// After firing once, skip this material's remaining reactions. Movement still runs.
		bool HaltUpdate = false;
	};

	struct MaterialData {
		Movement movement;
		Span reactionSpan;
		LifeSpan lifespanData;

		bool interpolateColor = false;		// colour from lifespan: minColor at 0, maxColor at Initial
		// Magenta, which is also what a material rejected at load is left holding.
		RGB minColor = {255, 0, 255};
		RGB maxColor = {255, 0, 255};
		// Random colour steps between min and max when not interpolating; 1 = minColor.
		std::uint8_t numberOfSteps = 1;

		// How strongly the block glows, 0 = not a light source at all.
		std::uint8_t emission = 0;

		// Bit i set = stick to neighbours carrying tag i (suppresses movement). Independent
		// of tagIntensity -- fire anchors to flammable while being flammable 0 itself.
		std::uint8_t anchorTagBitmask = 0;
		static_assert(MAX_TAGS <= 8, "anchorTagBitmask is one bit per tag, so it caps at 8");

		// Indexed by tag id. 0 = untagged; otherwise it doubles as the percent chance a
		// Tag-target reaction rolls against.
		std::array<std::uint8_t, MAX_TAGS> tagIntensity;

		// Bit i set = tagIntensity[i] is non-zero. Derived from the array above at load,
		// so that "does this neighbour carry any tag I anchor to" is one AND against
		// anchorTagBitmask rather than a walk over all MAX_TAGS slots per neighbour.
		std::uint8_t tagBitmask = 0;
	};

	// Editing form of a reaction: the spans only exist once the arenas are appended to.
	struct EditReaction {
		Reaction reaction;
		std::vector<Transition> targetTransitions;
		std::vector<Transition> selfTransitions;
	};
}
