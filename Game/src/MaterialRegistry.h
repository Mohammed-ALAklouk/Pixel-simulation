#pragma once
// Grid.h includes this, so keep the full nlohmann header in the .cpp -- only the
// parsing decls need json, and only by reference.
#include <nlohmann/json_fwd.hpp>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include "MaterialData.h"
#include "fast_rand.h"
#include "Block.h"
#include "Log.h"

namespace scree {
	class MaterialRegistry {
	public:
		MaterialRegistry();

		// Air is reached by id, not name (Grid::Clear fills with it). LoadAir pins it to 0.
		static constexpr MaterialID AIR_ID = 0;

		// False means nothing loaded; problems go to logs. Both replace existing contents.
		bool LoadMaterials(const std::string& corePath, const std::string& customPath = "");
		bool LoadMaterialsFromJSON(nlohmann::json& data);
		bool LoadMaterialsFromJSON(std::string_view data);
		void LoadAir();
		MaterialID AddMaterial(const std::string& name, const MaterialData& data);
		MaterialID AddMaterial(const std::string& name, MaterialData data,
			const std::vector<Transition>& deathTransitions,
			const std::vector<EditReaction>& reactionList);
		MaterialID RegisterMaterialName(const std::string& name);

		// Appends to the arenas and returns the span covering what was added.
		TransitionsSpan AddTransitions(const std::vector<Transition>& list);
		Span AddReactions(const std::vector<EditReaction>& list);

		// The reverse, for loading an existing material back into the editor.
		std::vector<Transition> ReadTransitions(TransitionsSpan span) const;
		std::vector<EditReaction> ReadReactions(Span span) const;

		void ReplaceMaterial(MaterialID id, MaterialData data,
			const std::vector<Transition>& deathTransitions,
			const std::vector<EditReaction>& reactionList);
		bool RenameMaterial(MaterialID id, const std::string& name);
		// Returns the old-id -> new-id table the grid needs, or empty when nothing was removed.
		std::vector<MaterialID> DeleteMaterial(MaterialID id);

		MaterialData& GetMutable(MaterialID id) { return materials[id]; }

		bool HasMaterial(const std::string& name) const { return materialMap.find(name) != materialMap.end(); }

		bool IsCore(MaterialID id) const { return id < coreMaterialCount; }
		int GetCoreMaterialsCount() const { return coreMaterialCount; }

		nlohmann::json MaterialToJSON(MaterialID id) const;

		void ParseMaterial(nlohmann::json& material, MaterialID id);
		void ParseTags(nlohmann::json& data);
		void ValidateMaterials(nlohmann::json& data);
		void ParseIntrinsics(const nlohmann::json& material, MaterialID id, scree::MaterialData& out);
		void ParseMovement(const nlohmann::json& material, MaterialID id, scree::Movement& out);
		void ParseTags(const nlohmann::json& material, MaterialID id, scree::MaterialData& out);
		void ParseLifespan(const nlohmann::json& material, MaterialID id, scree::LifeSpan& out);
		void ParseReactions(const nlohmann::json& material, MaterialID id, scree::MaterialData& out);
		// Pushes into transitions, returns the span it added. Label prefixes each error.
		// allowReactorAndSelf is false for on_death (neither base applies there).
		TransitionsSpan ParseTransitionSpan(const nlohmann::json& array, MaterialID id,
			const std::string& label, bool allowReactorAndSelf);

		const std::vector<Log>& GetLogs() const { return logs; }

		void Clear() {
			materials.clear();
			tags.clear();
			reactions.clear();
			transitions.clear();
			materialNames.clear();
			materialMap.clear();
			tagMap.clear();
			coreMaterialCount = 0;
		}

		void ClearLogs() {
			logs.clear();
		}


		// Unchecked on purpose: this is the innermost call in the update, hit several
		// times per pixel per pass. Every id reaching it comes out of a Block, and a
		// Block only ever holds an id the registry handed out -- Grid::Remap rewrites
		// the whole grid when materials reload, so no stale id survives.
		const MaterialData& Get(MaterialID id) const {
			return materials[id];
		}

		int GetMaterialsCount() const {
			return static_cast<int>(materials.size());
		}

		int GetTagsCount() const {
			return static_cast<int>(tags.size());
		}

		const std::string& GetTagName(MaterialID tag) const {
			return tags[tag];
		}

		const Transition* PickTransition(TransitionsSpan span) const
		{
			if (span.totalWeight <= 0) return nullptr;

			int roll = fast_rand() % span.totalWeight;
			for (int i = 0; i < span.count; ++i)
			{
				roll -= transitions[span.start + i].weight;
				if (roll < 0)
					return &transitions[span.start + i];
			}
			return nullptr; // unreachable when total > 0
		}

		const Reaction* GetReaction(int index) const {
			return &reactions[index];
		}

		std::uint8_t GetTagIntensity(MaterialID id, MaterialID tag) const {
			return materials[id].tagIntensity[tag];
		}

		bool CanReact(MaterialID id, MaterialID target, Reaction::TargetType targetType) const
		{
			if (targetType == Reaction::TargetType::Material)
				return id == target;
			else
				return GetTagIntensity(id, target);
		}

		const std::string& GetName(MaterialID id) const {
			return materialNames.at(id);
		}

		// Divide by steps-1 so the last step lands exactly on max. One step = one shade (min).
		static RGB Random_step(const RGB& min, const RGB& max, uint16_t number_of_steps)
		{
			if (number_of_steps <= 1) return min;

			int step = fast_rand() % number_of_steps;
			return RGB::lerp(min, max, static_cast<float>(step) / (number_of_steps - 1));
		}

		// Initial 0 is accepted at load, so guard the divide.
		static float lifespan_ratio(std::uint8_t lifespan, std::uint8_t initial)
		{
			return initial ? static_cast<float>(lifespan) / initial : 0.0f;
		}

		Block CreateBlock(MaterialID id) const
		{
			auto& data = Get(id);
			std::uint8_t lifespan = data.lifespanData.Initial;
			RGB color = data.interpolateColor ? RGB::lerp(data.minColor, data.maxColor, lifespan_ratio(lifespan, data.lifespanData.Initial)) : Random_step(data.minColor, data.maxColor, data.numberOfSteps);
			return Block(id, color, lifespan);
		}

		bool Tick(Block& block) const
		{
			auto& data = materials[block.id];
			if (fast_rand() % 100 < data.lifespanData.Chance) {
				if (block.lifespan < data.lifespanData.Tick) {
					block.lifespan = 0;
					return true;
				}

				block.lifespan -= data.lifespanData.Tick;
				if (data.interpolateColor)
					block.color = RGB::lerp(data.minColor, data.maxColor,
						lifespan_ratio(block.lifespan, data.lifespanData.Initial));

				return block.lifespan == 0;
			}

			return false;
		}

		void CreateBlock(Block& block, MaterialID id, std::uint8_t lifespan = 0) const
		{
			auto& data = Get(id);
			block.id = id;
			block.lifespan = lifespan;
			if (lifespan == 0)
				block.lifespan = data.lifespanData.Initial;

			if (data.interpolateColor)
				block.color = RGB::lerp(data.minColor, data.maxColor,
					lifespan_ratio(lifespan, data.lifespanData.Initial));
			else
				block.color = Random_step(data.minColor, data.maxColor, data.numberOfSteps);
		}

		const std::unordered_map<std::string, MaterialID>& GetTags() {
			return tagMap;
		}

	private:

		nlohmann::json TransitionToJSON(const Transition& transition, bool allowBase) const;
		nlohmann::json TransitionsToJSON(TransitionsSpan span, bool allowBase) const;
		nlohmann::json ReactionToJSON(const Reaction& reaction) const;

		// Numeric fields narrow to uint8_t/int8_t and wrap silently (300 -> 44), so clamp first.
		int ClampField(int value, int min, int max, const std::string& field, MaterialID materialID);
		// Same, but warns first if the JSON value was fractional.
		int ClampField(const nlohmann::json& value, int min, int max, const std::string& field, MaterialID materialID);
		// Reads parent[key], or fallback when it is absent.
		int ReadField(const nlohmann::json& parent, const std::string& key, int fallback,
			int min, int max, const std::string& field, MaterialID materialID);

		std::vector<MaterialData> materials;
		std::vector<std::string> tags;
		std::vector<Reaction>   reactions;
		std::vector<Transition> transitions;
		std::vector<std::string> materialNames;
		std::unordered_map<std::string, MaterialID> materialMap;
		MaterialID coreMaterialCount = 0;
		std::unordered_map<std::string, MaterialID> tagMap;
		std::vector<Log> logs;
	};
}