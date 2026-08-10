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

namespace scree {
	class MaterialRegistry {
	public:
		MaterialRegistry() = default;

		// Air is the simulation's notion of "empty" rather than an ordinary material --
		// Chunk fills new chunks with it and the eraser brush paints it -- so it needs a
		// name the code can reach without a lookup. It is pinned to id 0, which makes it
		// the first entry in materials.v2.json by contract.
		static constexpr MaterialID AIR_ID = 0;

		static std::string LoadMaterials();
		static std::string ParseTags(nlohmann::json& data);
		static std::string ValidateMaterials(nlohmann::json& data);
		static std::string ParseIntrensics(const nlohmann::json& material, scree::MaterialData& out);
		static std::string ParseMovement(const nlohmann::json& material, scree::Movement& out);
		static std::string ParseTags(const nlohmann::json& material, scree::MaterialData& out);
		static std::string ParseLifespan(const nlohmann::json& material, scree::LifeSpan& out);
		static std::string ParseReactions(const nlohmann::json& material, scree::MaterialData& out);

		static void Clear() {
			materials.clear();
			tags.clear();
			reactions.clear();
			transitions.clear();
			materialNames.clear();
			materialMap.clear();
			tagMap.clear();
		}


		static const MaterialData& Get(MaterialID id) {
			return materials[id];
		}

		static int GetMaterialsCount() {
			return static_cast<int>(materials.size());
		}

		static const Transition* PickTransition(TransitionsSpan span)
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

		static const Reaction* GetReaction(int index) {
			return &reactions[index];
		}

		static std::uint8_t GetTagIntensity(MaterialID id, MaterialID tag) {
			return materials[id].tagData[tag].intensity;
		}

		static bool CanReact(MaterialID id, MaterialID target, Reaction::TargetType targetType)
		{
			if (targetType == Reaction::TargetType::Material)
				return id == target;
			else
				return GetTagIntensity(id, target);
		}

		static std::string GetName(MaterialID id) {
			return materialNames[id];
		}

	private:
		static std::vector<MaterialData> materials;
		static std::vector<std::string> tags;
		static std::vector<Reaction>   reactions;
		static std::vector<Transition> transitions;
		static std::vector<std::string> materialNames;
		static std::unordered_map<std::string, MaterialID> materialMap;
		static std::unordered_map<std::string, MaterialID> tagMap;
	};
}