#include "MaterialRegistry.h"

std::vector<PS::MaterialData> PS::MaterialRegistry::materials;
std::vector<std::string> PS::MaterialRegistry::tags;
std::vector<PS::Reaction> PS::MaterialRegistry::reactions;
std::vector<PS::Transition> PS::MaterialRegistry::transitions;
std::vector<std::string> PS::MaterialRegistry::materialNames;
std::unordered_map<std::string, int> PS::MaterialRegistry::materialMap;
std::unordered_map<std::string, int> PS::MaterialRegistry::tagMap;

namespace {
	// Every numeric field ends up in a uint8_t or an int8_t, and the narrowing conversion
	// wraps instead of failing -- an initial lifespan of 300 would silently become 44.
	int ClampField(int value, int min, int max, const char* field, const std::string& context, std::string& error_message)
	{
		if (value < min || value > max) {
			error_message += "[Warning] " + context + " has " + field + " = " + std::to_string(value)
				+ ", clamped to [" + std::to_string(min) + ", " + std::to_string(max) + "].\n";
			return std::clamp(value, min, max);
		}

		return value;
	}
}

std::string PS::MaterialRegistry::LoadMaterials()
{
	std::string error_message = "";
	std::string path = std::string(GetApplicationDirectory()) + "assets/materials.json";
	std::ifstream file(path);
	if (!file.is_open())
		return "[Error] Failed to load materials.json at " + path;
	
	Clear();

	nlohmann::json data;
	try {
		data = nlohmann::json::parse(file);
	}
	catch (const nlohmann::json::exception& e) {
		return "[Error] Failed to parse materials.json: " + std::string(e.what()) + "\n";
	}

	error_message += ParseTags(data);
	error_message += ValidateMaterials(data);

	printf("Loading %d materials\n", (int)materials.size());
	for (auto& material: data["materials"])
	{
		if(!material["valid"])
			continue;

		MaterialData inMaterial{};
		error_message += ParseIntrensics(material, inMaterial);		
		error_message += ParseMovement(material, inMaterial.movement);
		error_message += ParseTags(material, inMaterial);
		error_message += ParseLifespan(material, inMaterial.lifespanData);
		error_message += ParseReactions(material, inMaterial);

		

		materials.push_back(inMaterial);
	}


	printf("Loaded %d materials and %d tags.\n", (int)materials.size(), (int)tags.size());
	for (auto& material: materials)
	{
		// lifespan info
		printf("Material %s: Initial lifespan %d, Tick %d, OnDeathTransitionSpan start %d, count %d, totalWeight %d\n",
			materialNames[&material - &materials[0]].c_str(),
			material.lifespanData.Initial,
			material.lifespanData.Tick,
			material.lifespanData.OnDeathTransitionSpan.start,
			material.lifespanData.OnDeathTransitionSpan.count,
			material.lifespanData.OnDeathTransitionSpan.totalWeight);
	}

	return error_message;
}

std::string PS::MaterialRegistry::ParseTags(nlohmann::json& data)
{
	std::string error_message = "";
	int index = 0;
	for (auto& tag : data["tags"]) {
		if (index >= PS::MAX_TAGS) {
			error_message += "[Error] Too many tags defined in materials.json. Maximum allowed is " + std::to_string(PS::MAX_TAGS) + ".\n";
			break;
		}

		try {
			auto name = tag.get<std::string>();
			tags.push_back(name);
			tagMap[name] = index;
		}
		catch (const nlohmann::json::exception&) {
			error_message += "[Error] Tag must be a string, got " + tag.dump() + ".\n";
			continue;
		}

		index++;
	}

	return error_message;
}

std::string PS::MaterialRegistry::ValidateMaterials(nlohmann::json& data)
{
	std::string error_message = "";
	int index = 0;
	for (auto& material : data["materials"]) {
		bool ok = true;
		if (!material.contains("name")) {
			error_message += "[Error] Material entry is missing a name.\n";
			material["valid"] = false;
			continue;
		}

		try {
			auto name = material["name"].get<std::string>();

			if (std::find(materialNames.begin(), materialNames.end(), name) != materialNames.end()) {
				error_message += "[Error] Duplicate material name: " + name + "\n";
				material["valid"] = false;
				continue;
			}

			material["valid"] = true;
			materialNames.push_back(name);
			materialMap[name] = index;
			index++;
		}
		catch (const nlohmann::json::exception&) {
			error_message += "[Error] Material name must be a string, got " + material["name"].dump() + ".\n";
			material["valid"] = false;
			continue;
		}
	}

	return error_message;
}

std::string PS::MaterialRegistry::ParseMovement(const nlohmann::json& material, PS::Movement& out)
{
	if (!material.contains("movement")) return "";
	const auto& movement = material["movement"];

	std::string error_message = "";
	const std::string context = "Material '" + material["name"].get<std::string>() + "'";

	try {
		// Y_direction is used as a grid offset, so anything outside -1..1 would step over
		// neighbouring tiles.
		out.Y_direction = ClampField(movement.value("y_direction", 1), -1, 1, "y_direction", context, error_message);
		out.density = ClampField(movement.value("density", 0), 0, 255, "density", context, error_message);
		out.scatter_chance = ClampField(movement.value("scatter_chance", 0), 0, 100, "scatter_chance", context, error_message);
		out.can_fall = movement.value("can_fall", false);
		out.can_cascade = movement.value("can_cascade", false);
		out.is_fluid = movement.value("is_fluid", false);
		out.is_liquid = movement.value("is_liquid", false);
	}
	catch (const nlohmann::json::exception& e) {
		return "[Error] " + context + " has a movement field of the wrong type: " + e.what() + "\n";
	}

	return error_message;
}

std::string PS::MaterialRegistry::ParseTags(const nlohmann::json& material, PS::MaterialData& out)
{
	std::string error_message = "";

	// An omitted tags block is legal, and operator[] on a const json asserts rather than
	// inserting, so bind to an empty object instead of indexing.
	static const nlohmann::json emptyObject = nlohmann::json::object();
	const auto& tagEntries = material.contains("tags") ? material["tags"] : emptyObject;

	for (auto& tagEntry : tagEntries.items())
	{
		auto tag = tagMap.find(tagEntry.key());
		if (tag == tagMap.end()) {
			error_message += "[Error] Unknown tag '" + tagEntry.key() + "' in material '" + material["name"].get<std::string>() + "'.\n";
			continue;
		}

		try {
			// Intensity doubles as a reaction chance, so it is a percent rather than a
			// free-running byte.
			out.tagData[tag->second].intensity = ClampField(tagEntry.value(), 0, 100, "intensity",
				"Tag '" + tagEntry.key() + "' in material '" + material["name"].get<std::string>() + "'", error_message);
		}
		catch (const nlohmann::json::exception&) {
			error_message += "[Error] Tag '" + tagEntry.key() + "' in material '" + material["name"].get<std::string>() + "' must have a numeric intensity, got " + tagEntry.value().dump() + ".\n";
			continue;
		}

		out.tagData[tag->second].isAnchor = false;
	}

	if (material.contains("anchor")) {
		try {
			auto anchor = material["anchor"].get<std::string>();
			if (tagMap.find(anchor) == tagMap.end()) {
				error_message += "[Error] Unknown anchor tag '" + anchor + "' in material '" + material["name"].get<std::string>() + "'.\n";
				return error_message;
			}

			out.tagData[tagMap[anchor]].isAnchor = true;
		}
		catch (const nlohmann::json::exception&) {
			error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has an anchor that is not a string, got " + material["anchor"].dump() + ".\n";
			return error_message;
		}
	}

	return error_message;
}

std::string PS::MaterialRegistry::ParseIntrensics(const nlohmann::json& material, PS::MaterialData& out)
{
	std::string error_message = "";
	const std::string context = "Material '" + material["name"].get<std::string>() + "'";

	try {
		out.inert = material.value("inert", false);
		out.interpolateColor = material.value("interpolate_color", false);

		// A short array converts without complaint, so the size has to be checked before
		// indexing -- vector::operator[] would run off the end rather than throw.
		auto color_min = material.value("color_min", std::vector<std::uint8_t>{255, 0, 255});
		if (color_min.size() != 3)
			return "[Error] " + context + " has a color_min that is not three channels.\n";

		auto color_max = material.value("color_max", std::vector<std::uint8_t>{255, 0, 255});
		if (color_max.size() != 3)
			return "[Error] " + context + " has a color_max that is not three channels.\n";

		out.minColor = RGB(color_min[0], color_min[1], color_min[2]);
		out.maxColor = RGB(color_max[0], color_max[1], color_max[2]);

		// Block::Random_step takes rand() % numberOfSteps, so zero is a divide by zero
		// rather than a bad colour.
		out.numberOfSteps = ClampField(material.value("steps", 1), 1, 255, "steps", context, error_message);
	}
	catch (const nlohmann::json::exception& e) {
		return "[Error] " + context + " has an intrensic field of the wrong type: " + e.what() + "\n";
	}

	return error_message;
}

std::string PS::MaterialRegistry::ParseLifespan(const nlohmann::json& material, PS::LifeSpan& out)
{
	if (!material.contains("lifespan")) return "";

	const auto& lifespan = material["lifespan"];
	std::string error_message = "";
	const std::string context = "Material '" + material["name"].get<std::string>() + "'";
	int initial = 255;
	int tick = 0;

	try {
		initial = ClampField(lifespan.value("initial", initial), 0, 255, "initial", context, error_message);
		tick = ClampField(lifespan.value("tick", tick), 0, 255, "tick", context, error_message);
	}
	catch (const nlohmann::json::exception& e) {
		return error_message + "[Error] " + context + " has a lifespan field of the wrong type: " + e.what() + "\n";
	}

	TransitionsSpan onDeathSpan = {
		transitions.size(),
		0,
		0
	};

	int inJSON_Count = 0;

	if (tick > 0) {
		if (!lifespan.contains("on_death")) {
			return error_message + "[Error] " + context + " has a tick lifespan but no on_death transitions defined.\n";
		}

		if (!lifespan["on_death"].is_array()) {
			return error_message + "[Error] " + context + " has a tick lifespan but on_death is not an array.\n";
		}
		inJSON_Count = lifespan["on_death"].size();

		if (inJSON_Count == 0) {
			return error_message + "[Error] " + context + " has a tick lifespan but on_death is an empty array.\n";
		}
	}

	for (int i = 0; i < inJSON_Count; ++i) {
		const auto& transitionData = lifespan["on_death"][i];
		Transition transition;

		try {
			transition.noTransition = transitionData.value("no_transition", false);
			if (!transition.noTransition) {
				if (!transitionData.contains("material")) {
					error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a transition with no_transition=false but does not specify a material.\n";
					continue;
				}

				else if (materialMap.find(transitionData["material"]) == materialMap.end()) {
					error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a transition with an unknown material '" + transitionData["material"].get<std::string>() + "'.\n";
					continue;
				}

				transition.nextID = materialMap[transitionData["material"]];

				auto lifespanBaseStr = transitionData.value("lifespan_base", "initial");
				if (lifespanBaseStr == "self") {
					error_message += "[Warning] Material '" + material["name"].get<std::string>() + "' has a transition with lifespan_base='self' in on_death transitions, which is not allowed.\n";
				}
				else if (lifespanBaseStr == "reactor") {
					error_message += "[Warning] Material '" + material["name"].get<std::string>() + "' has a transition with lifespan_base='reactor' in on_death transitions, which is not allowed.\n";
				}
			}

			if (!transitionData.contains("weight")) {
				error_message += "[Error] " + context + " has a transition that does not specify a weight.\n";
				continue;
			}

			transition.weight = ClampField(transitionData["weight"], 0, 255, "weight", context, error_message);
		}
		catch (const nlohmann::json::exception& e) {
			error_message += "[Error] " + context + " has an on_death transition with a field of the wrong type: " + e.what() + "\n";
			continue;
		}

		transitions.push_back(transition);
		onDeathSpan.count++;
		onDeathSpan.totalWeight += transition.weight;
	}

	out.Initial = initial;
	out.Tick = tick;
	out.OnDeathTransitionSpan = onDeathSpan;

	printf("Material %s: Initial lifespan %d, Tick %d, OnDeathTransitionSpan start %d, count %d, totalWeight %d\n",
		material["name"].get<std::string>().c_str(),
		initial,
		tick,
		out.OnDeathTransitionSpan.start,
		out.OnDeathTransitionSpan.count,
		out.OnDeathTransitionSpan.totalWeight);
	
	return error_message;
}

std::string PS::MaterialRegistry::ParseReactions(const nlohmann::json& material, PS::MaterialData& out)
{
	if (!material.contains("reactions")) return "";
	if (!material["reactions"].is_array()) 
		return "[Error] Material '" + material["name"].get<std::string>() + "' has reactions defined but they are not in an array.\n";
	
	std::string error_message = "";
	const std::string context = "Material '" + material["name"].get<std::string>() + "'";

	// Stands in for an absent transition list so the loops below can bind a reference
	// without operator[] inserting a null into the DOM.
	static const nlohmann::json emptyArray = nlohmann::json::array();

	out.reactionSpan.start = reactions.size();
	out.reactionSpan.count = 0;
	int reactionCount = material["reactions"].size();
	for (int i = 0; i < reactionCount; ++i) {
		const auto& reactionData = material["reactions"][i];
		Reaction reaction;
		
		if (!reactionData.contains("target")) {
			error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with no target defined.\n";
			continue;
		}

		if (!reactionData.contains("target_transitions") && !reactionData.contains("self_transitions")) {
			continue;
		}

		if (reactionData.contains("target_transitions") && !reactionData["target_transitions"].is_array()) {
			error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction whose target_transitions are not in an array.\n";
			continue;
		}

		if (reactionData.contains("self_transitions") && !reactionData["self_transitions"].is_array()) {
			error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction whose self_transitions are not in an array.\n";
			continue;
		}

		if (!reactionData.contains("target_type")) {
			error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with no target_type defined.\n";
			continue;
		}

		try {
			auto targetName = reactionData["target"].get<std::string>();
			auto targetType = reactionData["target_type"].get<std::string>();
			if (targetType == "material") {
				if (materialMap.find(targetName) == materialMap.end()) {
					error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with an unknown target material '" + targetName + "'.\n";
					continue;
				}

				reaction.targetType = Reaction::TargetType::Material;
				reaction.TargetID = materialMap[targetName];
			}
			else if (targetType == "tag") {
				if (tagMap.find(targetName) == tagMap.end()) {
					error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with an unknown target tag '" + targetName + "'.\n";
					continue;
				}

				reaction.targetType = Reaction::TargetType::Tag;
				reaction.TargetID = tagMap[targetName];
			}
			else {
				error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with an unknown target '" + targetName + "'.\n";
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
					error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with an unknown scan_sample value '" + scanSample + "'.\n";
					continue;
				}
			}

			reaction.Chance = ClampField(reactionData.value("chance", 100), 0, 100, "chance", context, error_message);
			reaction.HaltUpdate = reactionData.value("halt_update", false);
		}
		catch (const nlohmann::json::exception& e) {
			error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with a field of the wrong type: " + e.what() + "\n";
			continue;
		}

		reaction.TargetTransitionsSpan.start = transitions.size();
		reaction.TargetTransitionsSpan.count = 0;
		reaction.TargetTransitionsSpan.totalWeight = 0;
		const auto& targetTransitions = reactionData.contains("target_transitions") ? reactionData["target_transitions"] : emptyArray;
		for (const auto& transitionData : targetTransitions) {
			Transition transition;

			try {
				transition.noTransition = transitionData.value("no_transition", false);
				if (!transition.noTransition) {
					if (!transitionData.contains("material")) {
						error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with a target transition that does not specify a material.\n";
						continue;
					}
					else if (materialMap.find(transitionData["material"]) == materialMap.end()) {
						error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with a target transition that specifies an unknown material '" + transitionData["material"].get<std::string>() + "'.\n";
						continue;
					}

					transition.nextID = materialMap[transitionData["material"]];

					auto lifespanBaseStr = transitionData.value("lifespan_base", "initial");
					if (lifespanBaseStr == "self")
						transition.lifespanBase = Transition::LifeSpanBase::Self;
					else if (lifespanBaseStr == "reactor")
						transition.lifespanBase = Transition::LifeSpanBase::Reactor;
					else if (lifespanBaseStr == "initial")
						transition.lifespanBase = Transition::LifeSpanBase::Initial;
					else {
						error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with a target transition that specifies an unknown lifespan_base '" + lifespanBaseStr + "'.\n";
						continue;
					}
				}

				if (!transitionData.contains("weight")) {
					error_message += "[Error] " + context + " has a reaction with a target transition that does not specify a weight.\n";
					continue;
				}

				transition.weight = ClampField(transitionData["weight"], 0, 255, "weight", context, error_message);
			}
			catch (const nlohmann::json::exception& e) {
				error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction target transition with a field of the wrong type: " + e.what() + "\n";
				continue;
			}

			transitions.push_back(transition);
			reaction.TargetTransitionsSpan.count++;
			reaction.TargetTransitionsSpan.totalWeight += transition.weight;
		}


		reaction.SelfTransitionsSpan.start = transitions.size();
		reaction.SelfTransitionsSpan.count = 0;
		reaction.SelfTransitionsSpan.totalWeight = 0;

		const auto& selfTransitions = reactionData.contains("self_transitions") ? reactionData["self_transitions"] : emptyArray;
		for (const auto& transitionData : selfTransitions) {
			Transition transition;

			try {
				transition.noTransition = transitionData.value("no_transition", false);
				if (!transition.noTransition) {
					if (!transitionData.contains("material")) {
						error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with a self transition that does not specify a material.\n";
						continue;
					}
					else if (materialMap.find(transitionData["material"]) == materialMap.end()) {
						error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with a self transition that specifies an unknown material '" + transitionData["material"].get<std::string>() + "'.\n";
						continue;
					}

					transition.nextID = materialMap[transitionData["material"]];

					auto lifespanBaseStr = transitionData.value("lifespan_base", "initial");
					if (lifespanBaseStr == "self")
						transition.lifespanBase = Transition::LifeSpanBase::Self;
					else if (lifespanBaseStr == "reactor")
						transition.lifespanBase = Transition::LifeSpanBase::Reactor;
					else if (lifespanBaseStr == "initial")
						transition.lifespanBase = Transition::LifeSpanBase::Initial;
					else {
						error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction with a self transition that specifies an unknown lifespan_base '" + lifespanBaseStr + "'.\n";
						continue;
					}
				}

				if (!transitionData.contains("weight")) {
					error_message += "[Error] " + context + " has a reaction with a self transition that does not specify a weight.\n";
					continue;
				}

				transition.weight = ClampField(transitionData["weight"], 0, 255, "weight", context, error_message);
			}
			catch (const nlohmann::json::exception& e) {
				error_message += "[Error] Material '" + material["name"].get<std::string>() + "' has a reaction self transition with a field of the wrong type: " + e.what() + "\n";
				continue;
			}

			transitions.push_back(transition);
			reaction.SelfTransitionsSpan.count++;
			reaction.SelfTransitionsSpan.totalWeight += transition.weight;
		}

		if (reaction.TargetTransitionsSpan.count == 0 && reaction.SelfTransitionsSpan.count == 0) 
			continue;

		reactions.push_back(reaction);
		out.reactionSpan.count++;
	}

	return error_message;
}
