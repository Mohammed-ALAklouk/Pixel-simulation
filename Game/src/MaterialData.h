#pragma once

#include <array>
#include <string>
#include <cstdint>
#include "rgb.h"



namespace scree
{
	static constexpr int MAX_TAGS = 8;
	static constexpr int MAX_MATERIALS = 256;
	using MaterialID = std::uint8_t;

	// Index range into one of the g_* arenas below. Which arena depends on the field:
	// every transition span points into g_transitions, reactionSpan into g_reactions.
	struct Span {
		std::uint16_t start = 0;
		std::uint16_t count = 0;
	};

	struct TransitionsSpan {
		std::uint16_t start = 0;
		std::uint16_t count = 0;
		std::uint16_t totalWeight = 0;	
	};

	struct Movement {
		std::int8_t Y_direction = 1;
		// Integer scale, not the old float. Comparisons are strict, so equal densities
		// never displace each other: air 0, smoke 1, fire 2, steam 10, oil 20,
		// dirty water 20, water 30, acid 31, lava 50, sand/ash 80, stone/wood 100.
		std::uint8_t density = 0;
		std::uint8_t scatter_chance = 0;
		bool can_fall = false;
		bool can_cascade = false;
		bool is_fluid = false;
		bool is_liquid = false;
	};

	// One possible outcome. A transition span is a partition -- weights are relative to
	// the others in the same span and exactly one entry is always chosen -- so "nothing
	// happens" is not the absence of an entry, it is an entry with noTransition set.
	struct Transition {
		enum class LifeSpanBase : std::uint8_t {
			Self,		// use the old lifespan of the current material
			// Use the lifespan of the material that caused the transition, read AFTER
			// that material has ticked this frame. Hot stone spreading through water
			// relies on this to cool by one per generation, so the lifespan tick must
			// stay ordered before reactions in Update_pixel.
			// Meaningless in OnDeathTransitionSpan (there is no reactor) -- reject at load.
			Reactor,
			Initial,    // use the default lifespan of the new material it turns into
		};

		bool noTransition = false;			// leaves the block untouched; nextID unused
		MaterialID nextID = 0;
		std::uint8_t weight = 0;		// relative to the other weights in the same span
		LifeSpanBase lifespanBase = LifeSpanBase::Initial;
	};

	// A material with no lifespan is Initial 255, Tick 0. An omitted lifespan block in
	// the JSON resolves to those same values.
	struct LifeSpan {
		std::uint8_t Initial = 255;
		std::uint8_t Tick = 0;			// subtracted once per update
		TransitionsSpan OnDeathTransitionSpan = { 0, 0, 0};
	};

	struct Reaction {
		enum class TargetType : std::uint8_t {
			Material,
			Tag,
		};

		enum class Sample : std::uint8_t {
			All,
			FirstToReact,
		};

		TargetType targetType = TargetType::Material;
		MaterialID TargetID = 0;		// material id when Material, tag id when Tag
		// Percent. For Tag targets it scales the target's intensity for that tag
		// (Chance * intensity / 100), so 100 means "use the intensity as-is".
		std::uint8_t Chance = 0;
		TransitionsSpan TargetTransitionsSpan = { 0, 0, 0 };
		TransitionsSpan SelfTransitionsSpan = { 0, 0, 0 };

		Sample sample = Sample::FirstToReact;
		// On firing: stop scanning the remaining neighbours AND skip movement this tick.
		bool HaltUpdate = false;
	};

	struct TagData {
		std::uint8_t intensity = 0;		// 0 = this material does not have the tag
		// Stick to neighbours carrying this tag (suppresses movement). Independent of
		// intensity -- fire anchors to flammable while being flammable 0 itself.
		bool isAnchor = false;
	};

	struct MaterialData {
		Movement movement;
		Span reactionSpan;
		LifeSpan lifespanData;

		bool interpolateColor = false;		// colour from lifespan: minColor at 0, maxColor at Initial
		RGB minColor = {255, 0, 255};
		RGB maxColor = {255, 0, 255};
		std::uint8_t numberOfSteps = 1;	// random colour steps, used when not interpolating

		std::array<TagData, MAX_TAGS> tagData;	// indexed by tag id
	};
}
