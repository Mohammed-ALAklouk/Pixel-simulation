#pragma once
#include <vector>
#include <nlohmann/json.hpp>
#include <fstream>
#include <raylib.h>
#include <string>
#include <unordered_map>
#include <algorithm>
#include "MaterialData.h"
#include "fast_rand.h"
#include "Block.h"
#include "Log.h"

namespace scree {
	class MaterialRegistry {
	public:
		MaterialRegistry() = default;

		// Air is the simulation's notion of "empty" rather than an ordinary material --
		// Chunk fills new chunks with it and the eraser brush paints it -- so it needs a
		// name the code can reach without a lookup. It is pinned to id 0, which makes it
		// the first entry in materials.v2.json by contract.
		static constexpr MaterialID AIR_ID = 0;

		// Every problem found goes into logs. The returned string is those logs flattened
		// for the caller that still wants text; read GetLogs()/Worst() instead once the
		// caller can act on a severity.
		bool LoadMaterials();
		void ParseMaterial(nlohmann::json& material, MaterialID id);
		void ParseTags(nlohmann::json& data);
		void ValidateMaterials(nlohmann::json& data);
		void ParseIntrensics(const nlohmann::json& material, MaterialID id, scree::MaterialData& out);
		void ParseMovement(const nlohmann::json& material, MaterialID id, scree::Movement& out);
		void ParseTags(const nlohmann::json& material, MaterialID id, scree::MaterialData& out);
		void ParseLifespan(const nlohmann::json& material, MaterialID id, scree::LifeSpan& out);
		void ParseReactions(const nlohmann::json& material, MaterialID id, scree::MaterialData& out);

		const std::vector<Log>& GetLogs() const { return logs; }

		void Clear() {
			materials.clear();
			tags.clear();
			reactions.clear();
			transitions.clear();
			materialNames.clear();
			materialMap.clear();
			tagMap.clear();
			logs.clear();
		}


		const MaterialData& Get(MaterialID id) const {
			return materials.at(id);
		}

		int GetMaterialsCount() const {
			return static_cast<int>(materials.size());
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
			return materials[id].tagData[tag].intensity;
		}

		bool CanReact(MaterialID id, MaterialID target, Reaction::TargetType targetType) const
		{
			if (targetType == Reaction::TargetType::Material)
				return id == target;
			else
				return GetTagIntensity(id, target);
		}

		std::string GetName(MaterialID id) const {
			return materialNames[id];
		}

		// Divides by steps-1 so the last step lands exactly on max; dividing by steps
		// leaves max unreachable. One step means one shade, and that shade is min.
		static RGB Random_step(const RGB& min, const RGB& max, uint16_t number_of_steps)
		{
			if (number_of_steps <= 1) return min;

			int step = fast_rand() % number_of_steps;
			return RGB::lerp(min, max, static_cast<float>(step) / (number_of_steps - 1));
		}

		// An Initial of 0 is accepted at load and would divide here, so guard it.
		static float lifespan_ratio(std::uint8_t lifespan, std::uint8_t initial)
		{
			return initial ? static_cast<float>(lifespan) / initial : 0.0f;
		}

		Block CreateBlock(MaterialID id) const
		{
			auto& data = Get(id);
			return Block(id, Random_step(data.minColor, data.maxColor, data.numberOfSteps));
		}

		bool Tick(Block& block) const
		{
			auto& data = materials.at(block.id);
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

		void RecreateBlock(Block& block, MaterialID id, std::uint8_t lifespan) const
		{
			auto& data = Get(id);
			if (block.id != id) {
				block.color = Random_step(data.minColor, data.maxColor, data.numberOfSteps);
			}

			block.id = id;
			block.lifespan = lifespan;
		}

		void CreateBlock(Block& block, MaterialID id, std::uint8_t lifespan) const
		{
			auto& data = Get(id);
			block.id = id;
			block.lifespan = lifespan;
			if (data.interpolateColor)
				block.color = RGB::lerp(data.minColor, data.maxColor,
					lifespan_ratio(lifespan, data.lifespanData.Initial));
			else
				block.color = Random_step(data.minColor, data.maxColor, data.numberOfSteps);
		}

	private:

		// Every numeric field ends up in a uint8_t or an int8_t, and the narrowing conversion
		// wraps instead of failing -- an initial lifespan of 300 would silently become 44.
		int ClampField(int value, int min, int max, const std::string& field, MaterialID materialID);

		std::vector<MaterialData> materials;
		std::vector<std::string> tags;
		std::vector<Reaction>   reactions;
		std::vector<Transition> transitions;
		std::vector<std::string> materialNames;
		std::unordered_map<std::string, MaterialID> materialMap;
		std::unordered_map<std::string, MaterialID> tagMap;
		std::vector<Log> logs;
	};
}