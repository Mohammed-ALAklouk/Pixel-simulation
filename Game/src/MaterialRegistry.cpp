#include "MaterialRegistry.h"


int scree::MaterialRegistry::ClampField(int value, int min, int max, const std::string& field, MaterialID materialID)
{
	if (value < min || value > max) {
		logs.push_back(Log::CreateOutOfRange(materialID, GetName(materialID), field, value, min, max));
		return std::clamp(value, min, max);
	}

	return value;
}

int scree::MaterialRegistry::ClampField(const nlohmann::json& value, int min, int max, const std::string& field, MaterialID materialID)
{
	int whole = value.get<int>();
	if (value.is_number_float())
		logs.push_back(Log::CreateNotIntegral(materialID, GetName(materialID), field, value.dump(), whole));

	return ClampField(whole, min, max, field, materialID);
}

int scree::MaterialRegistry::ReadField(const nlohmann::json& parent, const std::string& key, int fallback,
	int min, int max, const std::string& field, MaterialID materialID)
{
	if (!parent.contains(key)) return fallback;

	return ClampField(parent[key], min, max, field, materialID);
}

scree::MaterialRegistry::MaterialRegistry()
{
	LoadAir();
}

bool scree::MaterialRegistry::LoadMaterials()
{
	Clear();

	LoadAir();
	
	std::string path = std::string(GetApplicationDirectory()) + "assets/materials.json";
	std::ifstream file(path);
	if (!file.is_open()) {
		logs.push_back(Log::CreateFileMissing(path));
		return false;
	}

	nlohmann::json data;
	try {
		data = nlohmann::json::parse(file);
	}
	catch (const nlohmann::json::exception& e) {
		logs.push_back(Log::CreateParseFailed(e.what()));
		return false;
	}

	// Tags have to exist before any material is parsed -- ParseTags(material, out) looks
	// every tag up in tagMap and reports an unknown tag otherwise.
	ParseTags(data);
	ValidateMaterials(data);

	for (auto& material: data["materials"])
	{
		if(!material["valid"])
			continue;

		// ValidateMaterials put every valid entry in materialMap, so this is the id it
		// was given there -- and the slot ParseMaterial's push_back is about to fill.
		ParseMaterial(material, materialMap[material["name"].get<std::string>()]);
	}

	if (Log::Worst(logs) == Log::Severity::RejectFile)
		return false;
	
	return true;
}

void scree::MaterialRegistry::LoadAir()
{
	// Air is not in materials.json because the simulation cannot run without it. Its name
	// is registered before ValidateMaterials so that id 0 is taken, the ids handed out to
	// file materials start at 1, and a stray "Air" entry in the file is caught as a
	// duplicate. The name has to match the one file transitions refer to.
	std::string airStr = R"({
		"name": "Air",
		"movement": {
			"y_direction": 1,
			"density": 0,
			"scatter_chance": 0,
			"can_fall": false,
			"can_cascade": false,
			"is_fluid": true,
			"is_liquid": false
		},
		"tags": {},
		"interpolate_color": false,
		"color_min": [0, 0, 0],
		"color_max": [0, 0, 0],
		"steps": 1
	})";
	nlohmann::json airJSON = nlohmann::json::parse(airStr);
	materialNames.push_back(airJSON["name"].get<std::string>());
	materialMap[airJSON["name"].get<std::string>()] = AIR_ID;
	ParseMaterial(airJSON, AIR_ID);
}

void scree::MaterialRegistry::ParseMaterial(nlohmann::json& material, MaterialID id)
{
	// Only the logs this call adds decide this entry's fate. Folding over the whole
	// vector would let the first rejected material condemn every one parsed after it.
	const std::size_t logStart = logs.size();

	MaterialData inMaterial{};
	ParseIntrinsics(material, id, inMaterial);
	ParseMovement(material, id, inMaterial.movement);
	ParseTags(material, id, inMaterial);
	ParseLifespan(material, id, inMaterial.lifespanData);
	ParseReactions(material, id, inMaterial);

	// The id was handed out in ValidateMaterials, so the slot has to be filled either way --
	// skipping the push would shift every later material off its name. A rejected entry
	// gets the defaults: no reactions, no tick, and magenta so it is obvious.
	if (Log::Worst(logs, logStart) >= Log::Severity::RejectMaterial) {
		materials.push_back(MaterialData{}); 
		return;
	}

	materials.push_back(inMaterial);
}

void scree::MaterialRegistry::ParseTags(nlohmann::json& data)
{
	int index = 0;
	for (auto& tag : data["tags"]) {
		if (index >= scree::MAX_TAGS) {
			logs.push_back(Log::CreateLimitExceeded("tags", scree::MAX_TAGS));
			break;
		}

		try {
			auto name = tag.get<std::string>();
			tags.push_back(name);
			tagMap[name] = static_cast<MaterialID>(index);
		}
		catch (const nlohmann::json::exception&) {
			logs.push_back(Log::CreateBadTagName(tag.dump()));
			continue;
		}

		index++;
	}
}

void scree::MaterialRegistry::ValidateMaterials(nlohmann::json& data)
{
	int index = 1;
	bool overflow_reported = false;
	for (auto& material : data["materials"]) {
		if (index >= scree::MAX_MATERIALS) {
			if (!overflow_reported) {
				logs.push_back(Log::CreateLimitExceeded("materials", scree::MAX_MATERIALS));
				overflow_reported = true;
			}

			material["valid"] = false;
			continue;
		}

		if (!material.contains("name")) {
			logs.push_back(Log::CreateMissingName());
			material["valid"] = false;
			continue;
		}

		try {
			auto name = material["name"].get<std::string>();

			if (std::find(materialNames.begin(), materialNames.end(), name) != materialNames.end()) {
				logs.push_back(Log::CreateDuplicateName(name));
				material["valid"] = false;
				continue;
			}

			material["valid"] = true;
			materialNames.push_back(name);
			materialMap[name] = static_cast<MaterialID>(index);
			index++;
		}
		catch (const nlohmann::json::exception&) {
			logs.push_back(Log::CreateBadMaterialName(material["name"].dump()));
			material["valid"] = false;
			continue;
		}
	}
}

void scree::MaterialRegistry::ParseMovement(const nlohmann::json& material, MaterialID id, scree::Movement& out)
{
	if (!material.contains("movement")) return;
	const auto& movement = material["movement"];

	try {
		// Y_direction is used as a grid offset, so anything outside -1..1 would step over
		// neighbouring tiles.
		out.Y_direction = ReadField(movement, "y_direction", 1, -1, 1, "y_direction", id);
		if (out.Y_direction == 0) {
			out.Y_direction = 1;
			logs.push_back(Log::CreateOutOfRange(id, GetName(id), "y_direction", 0, -1, 1));
		}

		out.density = ReadField(movement, "density", 0, 0, 255, "density", id);
		out.scatter_chance = ReadField(movement, "scatter_chance", 0, 0, 100, "scatter_chance", id);
		out.can_fall = movement.value("can_fall", false);
		out.can_cascade = movement.value("can_cascade", false);
		out.is_fluid = movement.value("is_fluid", false);
		out.is_liquid = movement.value("is_liquid", false);
	}
	catch (const nlohmann::json::exception& e) {
		logs.push_back(Log::CreateWrongType(id, GetName(id), "movement", e.what()));
	}
}

void scree::MaterialRegistry::ParseTags(const nlohmann::json& material, MaterialID id, scree::MaterialData& out)
{
	// An omitted tags block is legal, and operator[] on a const json asserts rather than
	// inserting, so bind to an empty object instead of indexing.
	static const nlohmann::json emptyObject = nlohmann::json::object();
	const auto& tagEntries = material.contains("tags") ? material["tags"] : emptyObject;

	for (auto& tagEntry : tagEntries.items())
	{
		auto tag = tagMap.find(tagEntry.key());
		if (tag == tagMap.end()) {
			logs.push_back(Log::CreateUnknownReference(id, GetName(id), "tag", tagEntry.key()));
			continue;
		}

		try {
			// Intensity doubles as a reaction chance, so it is a percent rather than a
			// free-running byte.
			out.tagData[tag->second].intensity = ClampField(tagEntry.value(), 0, 100,
				"tag '" + tagEntry.key() + "' intensity", id);
		}
		catch (const nlohmann::json::exception&) {
			logs.push_back(Log::CreateWrongType(id, GetName(id), "tag '" + tagEntry.key() + "' intensity",
				"expected a number, got " + tagEntry.value().dump()));
			continue;
		}
	}

	if (material.contains("anchor")) {
		try {
			auto anchor = material["anchor"].get<std::string>();
			if (tagMap.find(anchor) == tagMap.end()) {
				logs.push_back(Log::CreateUnknownReference(id, GetName(id), "anchor tag", anchor));
				return;
			}

			out.tagData[tagMap[anchor]].isAnchor = true;
		}
		catch (const nlohmann::json::exception&) {
			logs.push_back(Log::CreateWrongType(id, GetName(id), "anchor",
				"expected a string, got " + material["anchor"].dump()));
			return;
		}
	}
}

void scree::MaterialRegistry::ParseIntrinsics(const nlohmann::json& material, MaterialID id, scree::MaterialData& out)
{
	try {
		out.interpolateColor = material.value("interpolate_color", false);

		// A short array converts without complaint, so the size has to be checked before
		// indexing -- vector::operator[] would run off the end rather than throw.
		auto color_min = material.value("color_min", std::vector<std::uint8_t>{255, 0, 255});
		if (color_min.size() != 3) {
			logs.push_back(Log::CreateWrongType(id, GetName(id), "color_min", "expected three channels"));
			return;
		}

		auto color_max = material.value("color_max", std::vector<std::uint8_t>{255, 0, 255});
		if (color_max.size() != 3) {
			logs.push_back(Log::CreateWrongType(id, GetName(id), "color_max", "expected three channels"));
			return;
		}

		out.minColor = RGB(color_min[0], color_min[1], color_min[2]);
		out.maxColor = RGB(color_max[0], color_max[1], color_max[2]);

		// Random_step takes fast_rand() % numberOfSteps, so zero is a divide by zero
		// rather than a bad colour.
		out.numberOfSteps = ReadField(material, "steps", 1, 1, 255, "steps", id);
	}
	catch (const nlohmann::json::exception& e) {
		logs.push_back(Log::CreateWrongType(id, GetName(id), "an intrinsic field", e.what()));
	}
}

scree::TransitionsSpan scree::MaterialRegistry::ParseTransitionSpan(const nlohmann::json& array,
	MaterialID id, const std::string& label, bool allowReactorAndSelf)
{
	TransitionsSpan span = { static_cast<std::uint16_t>(transitions.size()), 0, 0 };

	for (const auto& transitionData : array) {
		Transition transition;

		try {
			transition.noTransition = transitionData.value("no_transition", false);
			if (!transition.noTransition) {
				if (!transitionData.contains("material")) {
					logs.push_back(Log::CreateMissingField(id, GetName(id), label + " material"));
					continue;
				}

				else if (materialMap.find(transitionData["material"]) == materialMap.end()) {
					logs.push_back(Log::CreateUnknownReference(id, GetName(id), label + " material",
						transitionData["material"].get<std::string>()));
					continue;
				}

				transition.nextID = materialMap[transitionData["material"]];

				auto lifespanBaseStr = transitionData.value("lifespan_base", "initial");
				if (!allowReactorAndSelf) {
					if (lifespanBaseStr == "self") {
						logs.push_back(Log::CreateNotAllowed(id, GetName(id), label + " lifespan_base",
							lifespanBaseStr, "the material is gone by the time it is read"));
					}
					else if (lifespanBaseStr == "reactor") {
						logs.push_back(Log::CreateNotAllowed(id, GetName(id), label + " lifespan_base",
							lifespanBaseStr, "an on_death transition has no reactor"));
					}
				}
				else if (lifespanBaseStr == "self")
					transition.lifespanBase = Transition::LifeSpanBase::Self;
				else if (lifespanBaseStr == "reactor")
					transition.lifespanBase = Transition::LifeSpanBase::Reactor;
				else if (lifespanBaseStr == "initial")
					transition.lifespanBase = Transition::LifeSpanBase::Initial;
				else {
					logs.push_back(Log::CreateUnknownReference(id, GetName(id),
						label + " lifespan_base", lifespanBaseStr));
					continue;
				}
			}

			if (!transitionData.contains("weight")) {
				logs.push_back(Log::CreateMissingField(id, GetName(id), label + " weight"));
				continue;
			}

			transition.weight = ClampField(transitionData["weight"], 0, 255, label + " weight", id);
		}
		catch (const nlohmann::json::exception& e) {
			logs.push_back(Log::CreateWrongType(id, GetName(id), label + " field", e.what()));
			continue;
		}

		transitions.push_back(transition);
		span.count++;
		span.totalWeight += transition.weight;
	}

	return span;
}

void scree::MaterialRegistry::ParseLifespan(const nlohmann::json& material, MaterialID id, scree::LifeSpan& out)
{
	if (!material.contains("lifespan")) return;

	const auto& lifespan = material["lifespan"];
	int initial = 255;
	int tick = 0;

	try {
		initial = ReadField(lifespan, "initial", initial, 0, 255, "lifespan.initial", id);
		tick = ReadField(lifespan, "tick", tick, 0, 255, "lifespan.tick", id);
	}
	catch (const nlohmann::json::exception& e) {
		logs.push_back(Log::CreateWrongType(id, GetName(id), "lifespan", e.what()));
		return;
	}

	// Committed here so the on_death paths below can bail without discarding them.
	out.Initial = initial;
	out.Tick = tick;

	// A material that never ticks never dies, so on_death is only read when it does.
	if (tick == 0) return;

	if (!lifespan.contains("on_death")) {
		logs.push_back(Log::CreateMissingField(id, GetName(id), "lifespan.on_death",
			"absent, and the lifespan ticks"));
		return;
	}

	if (!lifespan["on_death"].is_array()) {
		logs.push_back(Log::CreateWrongType(id, GetName(id), "lifespan.on_death", "expected an array"));
		return;
	}

	if (lifespan["on_death"].empty()) {
		logs.push_back(Log::CreateMissingField(id, GetName(id), "lifespan.on_death",
			"empty, and the lifespan ticks"));
		return;
	}

	out.OnDeathTransitionSpan = ParseTransitionSpan(lifespan["on_death"], id, "on_death transition", false);
}

void scree::MaterialRegistry::ParseReactions(const nlohmann::json& material, MaterialID id, scree::MaterialData& out)
{
	if (!material.contains("reactions")) return;
	if (!material["reactions"].is_array()) {
		logs.push_back(Log::CreateWrongType(id, GetName(id), "reactions", "expected an array"));
		return;
	}

	// Stands in for an absent transition list so the loops below can bind a reference
	// without operator[] inserting a null into the DOM.
	static const nlohmann::json emptyArray = nlohmann::json::array();

	out.reactionSpan.start = static_cast<std::uint16_t>(reactions.size());
	out.reactionSpan.count = 0;
	int reactionCount = static_cast<int>(material["reactions"].size());
	for (int i = 0; i < reactionCount; ++i) {
		const auto& reactionData = material["reactions"][i];
		Reaction reaction;

		if (!reactionData.contains("target")) {
			logs.push_back(Log::CreateMissingField(id, GetName(id), "reaction target"));
			continue;
		}

		if (!reactionData.contains("target_transitions") && !reactionData.contains("self_transitions")) {
			logs.push_back(Log::CreateMissingField(id, GetName(id), "reaction target_transitions or self_transitions"));
			continue;
		}

		if (reactionData.contains("target_transitions") && !reactionData["target_transitions"].is_array()) {
			logs.push_back(Log::CreateWrongType(id, GetName(id), "reaction target_transitions", "expected an array"));
			continue;
		}

		if (reactionData.contains("self_transitions") && !reactionData["self_transitions"].is_array()) {
			logs.push_back(Log::CreateWrongType(id, GetName(id), "reaction self_transitions", "expected an array"));
			continue;
		}

		if (!reactionData.contains("target_type")) {
			logs.push_back(Log::CreateMissingField(id, GetName(id), "reaction target_type"));
			continue;
		}

		try {
			auto targetName = reactionData["target"].get<std::string>();
			auto targetType = reactionData["target_type"].get<std::string>();
			if (targetType == "material") {
				if (materialMap.find(targetName) == materialMap.end()) {
					logs.push_back(Log::CreateUnknownReference(id, GetName(id), "reaction target material", targetName));
					continue;
				}

				reaction.targetType = Reaction::TargetType::Material;
				reaction.TargetID = materialMap[targetName];
				reaction.Chance = ReadField(reactionData, "chance", 100, 0, 100, "reaction chance", id);
			}
			else if (targetType == "tag") {
				if (tagMap.find(targetName) == tagMap.end()) {
					logs.push_back(Log::CreateUnknownReference(id, GetName(id), "reaction target tag", targetName));
					continue;
				}

				reaction.targetType = Reaction::TargetType::Tag;
				reaction.TargetID = tagMap[targetName];
			}
			else {
				logs.push_back(Log::CreateUnknownReference(id, GetName(id), "reaction target_type", targetType));
				continue;
			}

			if (reactionData.contains("scan_sample")) {
				auto scanSample = reactionData["scan_sample"].get<std::string>();
				if (scanSample == "all") {
					reaction.sample = Reaction::Sample::All;
				}
				else if (scanSample == "first_to_react") {
					reaction.sample = Reaction::Sample::FirstToReact;
				}
				else {
					logs.push_back(Log::CreateUnknownReference(id, GetName(id), "reaction scan_sample", scanSample));
					continue;
				}
			}

			reaction.HaltUpdate = reactionData.value("halt_update", false);
		}
		catch (const nlohmann::json::exception& e) {
			logs.push_back(Log::CreateWrongType(id, GetName(id), "a reaction field", e.what()));
			continue;
		}

		const auto& targetTransitions = reactionData.contains("target_transitions") ? reactionData["target_transitions"] : emptyArray;
		reaction.TargetTransitionsSpan = ParseTransitionSpan(targetTransitions, id, "reaction target transition", true);

		const auto& selfTransitions = reactionData.contains("self_transitions") ? reactionData["self_transitions"] : emptyArray;
		reaction.SelfTransitionsSpan = ParseTransitionSpan(selfTransitions, id, "reaction self transition", true);

		if (reaction.TargetTransitionsSpan.count == 0 && reaction.SelfTransitionsSpan.count == 0) {
			logs.push_back(Log::CreateMissingField(id, GetName(id), "reaction transitions",
				"empty after parsing"));
			continue;
		}

		reactions.push_back(reaction);
		out.reactionSpan.count++;
	}
}
