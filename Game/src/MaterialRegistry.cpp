#include "MaterialRegistry.h"

#include <nlohmann/json.hpp>
#include <raylib.h>
#include <algorithm>
#include <fstream>


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

bool scree::MaterialRegistry::LoadMaterials(const std::string& corePath, const std::string& customPath)
{
	ClearLogs();

	std::ifstream file(corePath);
	if (!file.is_open()) {
		logs.push_back(Log::CreateFileMissing(corePath));
		return false;
	}

	nlohmann::json data;
	try {
		data = nlohmann::json::parse(std::string(std::istreambuf_iterator<char>(file), {}));
	}
	catch (const nlohmann::json::exception& e) {
		logs.push_back(Log::CreateParseFailed(e.what()));
		return false;
	}

	// A non-object top level (e.g. "[1,2,3]") would throw on the first jsonData["tags"] below.
	if (!data.is_object()) {
		logs.push_back(Log::CreateParseFailed("expected an object at the top level, got "
			+ std::string(data.type_name()) + "."));
		return false;
	}

	if (!customPath.empty()) {
		std::ifstream customFile(customPath);
		if (!customFile.is_open()) {
			logs.push_back(Log::CreateOptionalFileMissing(customPath));
		}
		else {
			bool loaded = true;
			nlohmann::json customData;
			try {
				customData = nlohmann::json::parse(std::string(std::istreambuf_iterator<char>(customFile), {}));
			}
			catch (const nlohmann::json::exception& e) {
				logs.push_back(Log::CreateOptionalParseFailed(customPath, e.what()));
				loaded = false;
			}

			if (loaded && !customData.is_object()) {
				logs.push_back(Log::CreateOptionalParseFailed(customPath,
					"expected an object at the top level, got " + std::string(customData.type_name()) + "."));
				loaded = false;
			}

			if (loaded && (!customData.contains("materials") || !customData["materials"].is_array())) {
				logs.push_back(Log::CreateOptionalParseFailed(customPath, "expected a 'materials' array."));
				loaded = false;
			}

			if (loaded) {
				for (auto& material : customData["materials"]) {
					if (material.is_object()) material["custom"] = true;
					data["materials"].push_back(material);
				}
			}
		}
	}

	return LoadMaterialsFromJSON(data);
}


bool scree::MaterialRegistry::LoadMaterialsFromJSON(std::string_view str)
{
	ClearLogs();

	nlohmann::json jsonData;
	try {
		jsonData = nlohmann::json::parse(str);
	}
	catch (const nlohmann::json::exception& e) {
		logs.push_back(Log::CreateParseFailed(e.what()));
		return false;
	}

	// A non-object top level (e.g. "[1,2,3]") would throw on the first jsonData["tags"] below.
	if (!jsonData.is_object()) {
		logs.push_back(Log::CreateParseFailed("expected an object at the top level, got "
			+ std::string(jsonData.type_name()) + "."));
		return false;
	}
	return LoadMaterialsFromJSON(jsonData);
}

bool scree::MaterialRegistry::LoadMaterialsFromJSON(nlohmann::json& data)
{
	Clear();
	LoadAir();

	// Catches throws the Parse* helpers don't, e.g. writing "valid" into a non-object entry.
	try {
		// Tags must be registered before materials, which look them up in tagMap.
		ParseTags(data);
		ValidateMaterials(data);

		for (auto& material : data["materials"])
		{
			if (!material["valid"])
				continue;

			// The id ValidateMaterials assigned, and the slot ParseMaterial will fill.
			ParseMaterial(material, materialMap[material["name"].get<std::string>()]);
		}
	}
	catch (const nlohmann::json::exception& e) {
		logs.push_back(Log::CreateParseFailed(e.what()));
		return false;
	}

	return Log::Worst(logs) != Log::Severity::RejectFile;
}


void scree::MaterialRegistry::LoadAir()
{
	// Air holds id 0 (registered before ValidateMaterials), so file materials start at 1
	// and a file "Air" entry is caught as a duplicate. Not in materials.json.
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
		"color_min": [8, 8, 10],
		"color_max": [10, 10, 12],
		"steps": 2
	})";
	nlohmann::json airJSON = nlohmann::json::parse(airStr);
	materialNames.push_back(airJSON["name"].get<std::string>());
	materialMap[airJSON["name"].get<std::string>()] = AIR_ID;
	coreMaterialCount = 1;
	ParseMaterial(airJSON, AIR_ID);
}

scree::MaterialID scree::MaterialRegistry::RegisterMaterialName(const std::string& name)
{
	if (materialNames.size() >= static_cast<std::size_t>(MAX_MATERIALS)) {
		logs.push_back(Log::CreateLimitExceeded("materials", MAX_MATERIALS));
		return AIR_ID;
	}

	if (name.empty()) {
		logs.push_back(Log::CreateMissingName());
		return AIR_ID;
	}

	if (materialMap.find(name) != materialMap.end()) {
		logs.push_back(Log::CreateDuplicateName(name));
		return AIR_ID;
	}

	const MaterialID id = static_cast<MaterialID>(materialNames.size());
	materialNames.push_back(name);
	materialMap[name] = id;
	return id;
}

scree::MaterialID scree::MaterialRegistry::AddMaterial(const std::string& name, const MaterialData& data)
{
	const MaterialID id = RegisterMaterialName(name);
	if (id == AIR_ID) return AIR_ID;

	materials.push_back(data);
	return id;
}

scree::TransitionsSpan scree::MaterialRegistry::AddTransitions(const std::vector<Transition>& list)
{
	TransitionsSpan span = { static_cast<std::uint16_t>(transitions.size()), 0, 0 };

	for (const auto& transition : list) {
		transitions.push_back(transition);
		span.count++;
		span.totalWeight += transition.weight;
	}

	return span;
}

// Reactions and transitions are separate arenas, so the nested spans cannot split this block.
scree::Span scree::MaterialRegistry::AddReactions(const std::vector<EditReaction>& list)
{
	Span span = { static_cast<std::uint16_t>(reactions.size()), 0 };

	for (const auto& entry : list) {
		Reaction reaction = entry.reaction;
		reaction.TargetTransitionsSpan = AddTransitions(entry.targetTransitions);
		reaction.SelfTransitionsSpan = AddTransitions(entry.selfTransitions);
		reactions.push_back(reaction);
		span.count++;
	}

	return span;
}

std::vector<scree::Transition> scree::MaterialRegistry::ReadTransitions(TransitionsSpan span) const
{
	std::vector<Transition> list;
	list.reserve(span.count);
	for (int i = 0; i < span.count; ++i)
		list.push_back(transitions[span.start + i]);

	return list;
}

std::vector<scree::EditReaction> scree::MaterialRegistry::ReadReactions(Span span) const
{
	std::vector<EditReaction> list;
	list.reserve(span.count);
	for (int i = 0; i < span.count; ++i) {
		const Reaction& reaction = reactions[span.start + i];
		list.push_back(EditReaction{
			reaction,
			ReadTransitions(reaction.TargetTransitionsSpan),
			ReadTransitions(reaction.SelfTransitionsSpan),
		});
	}

	return list;
}

scree::MaterialID scree::MaterialRegistry::AddMaterial(const std::string& name, MaterialData data,
	const std::vector<Transition>& deathTransitions,
	const std::vector<EditReaction>& reactionList)
{
	const MaterialID id = RegisterMaterialName(name);
	if (id == AIR_ID) return AIR_ID;

	data.lifespanData.OnDeathTransitionSpan = AddTransitions(deathTransitions);
	data.reactionSpan = AddReactions(reactionList);
	materials.push_back(data);
	return id;
}

bool scree::MaterialRegistry::RenameMaterial(MaterialID id, const std::string& name)
{
	if (name.empty() || name == materialNames[id]) return false;

	if (materialMap.find(name) != materialMap.end()) {
		logs.push_back(Log::CreateDuplicateName(name));
		return false;
	}

	materialMap.erase(materialNames[id]);
	materialNames[id] = name;
	materialMap[name] = id;
	return true;
}

// Every id above the removed one shifts down, so the grid and every stored id must follow.
std::vector<scree::MaterialID> scree::MaterialRegistry::DeleteMaterial(MaterialID id)
{
	if (IsCore(id) || id >= materials.size()) return {};

	std::vector<MaterialID> remap(materials.size());
	for (std::size_t i = 0; i < remap.size(); ++i) {
		if (i < id)       remap[i] = static_cast<MaterialID>(i);
		else if (i == id) remap[i] = AIR_ID;
		else              remap[i] = static_cast<MaterialID>(i - 1);
	}

	materials.erase(materials.begin() + id);
	materialNames.erase(materialNames.begin() + id);

	materialMap.clear();
	for (std::size_t i = 0; i < materialNames.size(); ++i)
		materialMap[materialNames[i]] = static_cast<MaterialID>(i);

	// A transition into the removed material becomes "nothing happens" rather than a hole.
	for (auto& transition : transitions) {
		if (transition.noTransition) continue;
		if (transition.nextID == id) transition.noTransition = true;
		else transition.nextID = remap[transition.nextID];
	}

	// Tag targets index the tag table, not the material table, so they are left alone.
	for (auto& reaction : reactions) {
		if (reaction.targetType != Reaction::TargetType::Material) continue;
		if (reaction.TargetID == id) {
			reaction.TargetID = AIR_ID;
			reaction.Chance = 0;
		}
		else reaction.TargetID = remap[reaction.TargetID];
	}

	return remap;
}

// The material's previous spans are left behind in the arenas rather than compacted out.
void scree::MaterialRegistry::ReplaceMaterial(MaterialID id, MaterialData data,
	const std::vector<Transition>& deathTransitions,
	const std::vector<EditReaction>& reactionList)
{
	data.lifespanData.OnDeathTransitionSpan = AddTransitions(deathTransitions);
	data.reactionSpan = AddReactions(reactionList);
	materials[id] = data;
}

nlohmann::json scree::MaterialRegistry::TransitionToJSON(const Transition& transition, bool allowBase) const
{
	nlohmann::json out = nlohmann::json::object();

	if (transition.noTransition) {
		out["no_transition"] = true;
		out["weight"] = transition.weight;
		return out;
	}

	out["material"] = GetName(transition.nextID);
	out["weight"] = transition.weight;

	if (allowBase && transition.lifespanBase == Transition::LifeSpanBase::Self)
		out["lifespan_base"] = "self";
	else if (allowBase && transition.lifespanBase == Transition::LifeSpanBase::Reactor)
		out["lifespan_base"] = "reactor";

	return out;
}

nlohmann::json scree::MaterialRegistry::TransitionsToJSON(TransitionsSpan span, bool allowBase) const
{
	nlohmann::json out = nlohmann::json::array();
	for (int i = 0; i < span.count; ++i)
		out.push_back(TransitionToJSON(transitions[span.start + i], allowBase));

	return out;
}

nlohmann::json scree::MaterialRegistry::ReactionToJSON(const Reaction& reaction) const
{
	nlohmann::json out = nlohmann::json::object();

	if (reaction.targetType == Reaction::TargetType::Material) {
		out["target_type"] = "material";
		out["target"] = GetName(reaction.TargetID);
		if (reaction.Chance != 100)
			out["chance"] = reaction.Chance;
	}
	else {
		out["target_type"] = "tag";
		out["target"] = tags[reaction.TargetID];
	}

	if (reaction.sample == Reaction::Sample::All)
		out["scan_sample"] = "all";
	if (reaction.HaltUpdate)
		out["halt_update"] = true;

	if (reaction.TargetTransitionsSpan.count)
		out["target_transitions"] = TransitionsToJSON(reaction.TargetTransitionsSpan, true);
	if (reaction.SelfTransitionsSpan.count)
		out["self_transitions"] = TransitionsToJSON(reaction.SelfTransitionsSpan, true);

	return out;
}

nlohmann::json scree::MaterialRegistry::MaterialToJSON(MaterialID id) const
{
	const MaterialData& data = materials[id];
	nlohmann::json out = nlohmann::json::object();

	out["name"] = GetName(id);

	if (data.interpolateColor)
		out["interpolate_color"] = true;

	if (data.minColor.r != 255 || data.minColor.g != 0 || data.minColor.b != 255)
		out["color_min"] = nlohmann::json::array({ int(data.minColor.r), int(data.minColor.g), int(data.minColor.b) });
	if (data.maxColor.r != 255 || data.maxColor.g != 0 || data.maxColor.b != 255)
		out["color_max"] = nlohmann::json::array({ int(data.maxColor.r), int(data.maxColor.g), int(data.maxColor.b) });

	if (!data.interpolateColor && data.numberOfSteps != 1)
		out["steps"] = data.numberOfSteps;

	nlohmann::json movement = nlohmann::json::object();
	if (data.movement.Y_direction != 1) movement["y_direction"] = int(data.movement.Y_direction);
	if (data.movement.density != 0) movement["density"] = data.movement.density;
	const int scatter = std::min<int>(data.movement.scatter_chance, 100);
	if (scatter != 0) movement["scatter_chance"] = scatter;
	if (data.movement.can_fall) movement["can_fall"] = true;
	if (data.movement.can_cascade) movement["can_cascade"] = true;
	if (data.movement.is_fluid) movement["is_fluid"] = true;
	if (data.movement.is_liquid) movement["is_liquid"] = true;
	if (!movement.empty()) out["movement"] = movement;

	nlohmann::json tagsOut = nlohmann::json::object();
	for (std::size_t i = 0; i < tags.size(); ++i)
		if (data.tagIntensity[i]) tagsOut[tags[i]] = data.tagIntensity[i];
	if (!tagsOut.empty()) out["tags"] = tagsOut;

	for (std::size_t i = 0; i < tags.size(); ++i)
		if (data.anchorTagBitmask & (1u << i)) {
			out["anchor"] = tags[i];
			break;
		}

	nlohmann::json lifespan = nlohmann::json::object();
	if (data.lifespanData.Initial != 255) lifespan["initial"] = data.lifespanData.Initial;
	if (data.lifespanData.Tick != 0) lifespan["tick"] = data.lifespanData.Tick;
	if (data.lifespanData.Chance != 100) lifespan["chance"] = data.lifespanData.Chance;
	if (data.lifespanData.Tick != 0)
		lifespan["on_death"] = TransitionsToJSON(data.lifespanData.OnDeathTransitionSpan, false);
	if (!lifespan.empty()) out["lifespan"] = lifespan;

	if (data.reactionSpan.count) {
		nlohmann::json reactionsOut = nlohmann::json::array();
		for (int i = 0; i < data.reactionSpan.count; ++i)
			reactionsOut.push_back(ReactionToJSON(reactions[data.reactionSpan.start + i]));
		out["reactions"] = reactionsOut;
	}

	return out;
}

void scree::MaterialRegistry::ParseMaterial(nlohmann::json& material, MaterialID id)
{
	// Judge only the logs this call adds, or one bad material condemns the rest.
	const std::size_t logStart = logs.size();

	MaterialData inMaterial{};
	ParseIntrinsics(material, id, inMaterial);
	ParseMovement(material, id, inMaterial.movement);
	ParseTags(material, id, inMaterial);
	ParseLifespan(material, id, inMaterial.lifespanData);
	ParseReactions(material, id, inMaterial);

	// Fill the slot either way; skipping would shift every later material off its id.
	// A reject gets the magenta defaults.
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
	bool overflow_reported = false;
	for (auto& material : data["materials"]) {
		if (materialNames.size() >= static_cast<std::size_t>(scree::MAX_MATERIALS)) {
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
			const bool registered = (RegisterMaterialName(name) != AIR_ID);
			material["valid"] = registered;
			if (registered && !material.value("custom", false))
				coreMaterialCount = static_cast<MaterialID>(materialNames.size());
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
		// Used as a grid offset; outside -1..1 would step over neighbours.
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
	// Omitted tags is legal; operator[] on a const json asserts, so bind an empty object.
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
			// Intensity doubles as a reaction chance, so it's a percent.
			out.tagIntensity[tag->second] = ClampField(tagEntry.value(), 0, 100,
				"tag '" + tagEntry.key() + "' intensity", id);
		}
		catch (const nlohmann::json::exception&) {
			logs.push_back(Log::CreateWrongType(id, GetName(id), "tag '" + tagEntry.key() + "' intensity",
				"expected a number, got " + tagEntry.value().dump()));
			continue;
		}
	}

	out.tagBitmask = 0;
	for (int i = 0; i < MAX_TAGS; i++)
		if (out.tagIntensity[i]) out.tagBitmask |= static_cast<std::uint8_t>(1 << i);

	if (material.contains("anchor")) {
		try {
			auto anchor = material["anchor"].get<std::string>();
			if (tagMap.find(anchor) == tagMap.end()) {
				logs.push_back(Log::CreateUnknownReference(id, GetName(id), "anchor tag", anchor));
				return;
			}

			auto mask = static_cast<std::uint8_t>(1 << tagMap[anchor]);
			out.anchorTagBitmask |= mask;
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

		// A short array converts without complaint, so check size before indexing.
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

		// Feeds fast_rand() % numberOfSteps, so 0 would divide by zero.
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
	int chance = 100;

	try {
		initial = ReadField(lifespan, "initial", initial, 0, 255, "lifespan.initial", id);
		tick = ReadField(lifespan, "tick", tick, 0, 255, "lifespan.tick", id);
		chance = ReadField(lifespan, "chance", chance, 0, 100, "lifespan.chance", id);
	}
	catch (const nlohmann::json::exception& e) {
		logs.push_back(Log::CreateWrongType(id, GetName(id), "lifespan", e.what()));
		return;
	}

	// Commit before the on_death checks below, which may bail.
	out.Initial = initial;
	out.Tick = tick;
	out.Chance = chance;

	// Never ticks, never dies; on_death only matters when it ticks.
	if (tick == 0) return;

	if (chance == 0)
		logs.push_back(Log::CreateMissingField(id, GetName(id), "lifespan.chance",
			"0, so the lifespan ticks but can never expire"));

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

	// Either leaves a dead tile with nothing to become, so it re-rolls and wakes its
	// chunk every frame. Reject it.
	const auto& span = out.OnDeathTransitionSpan;
	if (span.count && span.totalWeight == 0)
		logs.push_back(Log::CreateMissingField(id, GetName(id), "lifespan.on_death",
			"every weight is 0, so nothing can be chosen"));

	for (int i = 0; i < span.count; ++i) {
		if (transitions[span.start + i].noTransition) {
			logs.push_back(Log::CreateMissingField(id, GetName(id), "lifespan.on_death",
				"a no_transition entry would leave the tile dead but unchanged"));
			break;
		}
	}
}

void scree::MaterialRegistry::ParseReactions(const nlohmann::json& material, MaterialID id, scree::MaterialData& out)
{
	if (!material.contains("reactions")) return;
	if (!material["reactions"].is_array()) {
		logs.push_back(Log::CreateWrongType(id, GetName(id), "reactions", "expected an array"));
		return;
	}

	// Stand-in for an absent list so the loops bind a reference without operator[]
	// inserting a null.
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
